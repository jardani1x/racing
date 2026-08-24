// Copyright RacingSim. All Rights Reserved.

#include "Race/RaceLapTracker.h"
#include "Race/RaceRulesetDataAsset.h"
#include "Race/RaceStateMachine.h"
#include "Race/TrackCenterline.h"
#include "Race/TrackCheckpointGate.h"

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/**
 * RACE-004: the shortcut / reverse / double-trigger / reset COMBINATIONAL matrix.
 *
 * ===========================================================================
 * Why this file exists when RaceLapTrackerSpec.cpp already covers every axis
 * ===========================================================================
 *
 * RACE-002 and RACE-003 covered each fault ONE AT A TIME, and covered them well:
 * RacingSim.Race.LapOrdering (a skipped gate, a reverse finish crossing, a spin at an
 * ordinary gate), RacingSim.Race.LapLineSpin (multi-oscillation across the line),
 * RacingSim.Race.LapResetAndRestart (announced reset, unannounced teleport, restart
 * idempotence), RacingSim.Race.LapNoGateProgress (a lap that took no ordered gate).
 *
 * None of them drives TWO faults on ONE lap. That is the gap this ticket owns, and it is
 * not a hypothetical one: FRaceLapInvalidity's whole contract is FIRST FAULT WINS, and a
 * rule about which of two faults is reported is untested until two faults exist. Every
 * single-fault suite in this project would sit green over an implementation that latched
 * the LAST fault instead of the first -- with exactly one fault, first and last are the
 * same value.
 *
 * ===========================================================================
 * The matrix
 * ===========================================================================
 *
 * Three fault-producing axes and one benign axis:
 *
 *   S  Shortcut      -- run wide around gate 1, detected on crossing gate 2.
 *                       Expected: MissedCheckpoint naming gate 1.
 *   R  Reverse       -- cross the start/finish line against its legal direction.
 *                       Expected: ReverseFinishCrossing naming gate 0.
 *   V  VehicleReset  -- an announced reset under the default voiding ruleset.
 *                       Expected: VehicleReset.
 *   D  DoubleTrigger -- re-cross an already-satisfied ordinary gate (forward, back,
 *                       forward). Expected: NOTHING. This axis is in the matrix precisely
 *                       because it must NOT change any verdict it is composed with.
 *
 * Section 2 runs the three singles (each injector is potent on its own -- without this,
 * every first-fault-wins assertion below is vacuous, because an injector that did nothing
 * would also "lose" to the first fault).
 *
 * Section 3 runs all SIX ordered pairs of the three fault axes, asserting the FIRST
 * fault's reason and gate survive the second.
 *
 * Section 4 runs the self-pairs: the same fault twice must not re-latch onto the later
 * instance's gate.
 *
 * Section 5 is the differential double-trigger axis: the same drive with and without an
 * oscillation must produce an IDENTICAL verdict.
 *
 * Section 6 crosses restart with every cell: ResetForNewSession() after any of them
 * leaves a tracker that scores a clean lap exactly as a fresh one does.
 *
 * Section 7 is the negative-control block, and it is the reason this suite is falsifiable
 * rather than decorative. Sections 2-6 assert that faulted laps are invalid; an
 * implementation that marked EVERY lap invalid would pass all of them. Section 7 pins the
 * other direction: a clean lap counts, and a lap containing only a double-trigger counts.
 *
 * ===========================================================================
 * No level, no actor, no world -- and SmokeFilter is mandatory
 * ===========================================================================
 *
 * Same reason as RaceLapTrackerSpec.cpp, which this file deliberately mirrors:
 * Docs/Environment.md records that a SmokeFilter test in this project cannot construct a
 * non-template Actor (the typed-element registry is not registered when
 * FEngineLoop::PreInit runs the smoke tests), and that a test written outside the one
 * filter the documented gate uses once sat green and unexecuted. Everything here is a
 * procedurally generated circle, a gate set built from arithmetic, and a fake monotonic
 * clock. Nothing traces any real circuit (CLAUDE.md).
 */

namespace RaceFaultMatrixSpecPrivate
{
	// Named uniquely rather than placed in an anonymous namespace: unity builds
	// concatenate translation units, and duplicate anonymous symbols across a blob are a
	// redefinition rather than two file-local helpers. Same convention as
	// RaceLapSpecPrivate in RaceLapTrackerSpec.cpp.

	constexpr double MatrixCircleRadiusCm = 10000.0;   // 100 m radius, ~628 m lap
	constexpr int32 MatrixCircleSamples = 720;
	constexpr double MatrixGateHalfWidthCm = 900.0;
	constexpr double MatrixGateHalfHeightCm = 500.0;
	constexpr int32 MatrixStepsPerLap = 400;
	constexpr double MatrixSecondsPerStep = 0.016;

	/** How far outside the gate rectangle a "wide" line takes the car. */
	constexpr double MatrixWideOffsetCm = MatrixGateHalfWidthCm + 600.0;

	/** The fake monotonic clock. Captureless so it converts to FRaceTimeSourceFn. */
	double GMatrixNowSeconds = 0.0;
	double MatrixTimeSource()
	{
		return GMatrixNowSeconds;
	}

	/** Everything one Advance() step reported, accumulated over a drive. */
	struct FMatrixDriveSummary
	{
		int32 LapsClosed = 0;
		int32 LapsCounted = 0;
		int32 LapsOpened = 0;
		int32 GatesAdvanced = 0;

		TArray<FRacingLapTiming> ClosedLaps;

		void Accumulate(const FRaceLapTrackerUpdate& Update)
		{
			LapsClosed += Update.bLapClosed ? 1 : 0;
			LapsCounted += Update.bLapCounted ? 1 : 0;
			LapsOpened += Update.bLapOpened ? 1 : 0;
			GatesAdvanced += Update.GatesAdvanced;

			if (Update.bLapClosed)
			{
				ClosedLaps.Add(Update.ClosedLap);
			}
		}
	};

	/**
	 * A closed circular track, four ordered gates, three sectors, a state machine on a
	 * fake clock, and a tracker wired to all of it.
	 */
	struct FMatrixRig
	{
		FTrackCenterline Circle;
		FRacingCheckpointGateSet Gates;
		TArray<double> SectorStartsCm;
		TStrongObjectPtr<URaceRulesetDataAsset> Ruleset;
		TStrongObjectPtr<URaceStateMachine> Machine;
		TStrongObjectPtr<URaceLapTracker> Tracker;

		double LapLengthCm = 0.0;
		double CurrentDistanceCm = 0.0;

		bool Build(FAutomationTestBase& Test, const bool bResetInvalidatesLap = true)
		{
			GMatrixNowSeconds = 5000.0;

			const double TotalCm = 2.0 * UE_DOUBLE_PI * MatrixCircleRadiusCm;
			const double StepCm = TotalCm / static_cast<double>(MatrixCircleSamples);

			TArray<FVector> Locations;
			TArray<double> Distances;
			Locations.Reserve(MatrixCircleSamples);
			Distances.Reserve(MatrixCircleSamples);
			for (int32 Index = 0; Index < MatrixCircleSamples; ++Index)
			{
				const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(Index) / static_cast<double>(MatrixCircleSamples);
				Locations.Add(FVector(
					MatrixCircleRadiusCm * FMath::Cos(Angle),
					MatrixCircleRadiusCm * FMath::Sin(Angle),
					0.0));
				Distances.Add(static_cast<double>(Index) * StepCm);
			}

			FString Error;
			if (!Circle.Build(Locations, Distances, TotalCm, /*bClosedLoop*/ true, Error))
			{
				Test.AddError(FString::Printf(TEXT("Matrix centerline failed to build: %s"), *Error));
				return false;
			}

			LapLengthCm = Circle.GetLengthCm();

			TArray<FRacingCheckpointGateSpec> Specs;
			for (int32 Index = 0; Index < 4; ++Index)
			{
				FRacingCheckpointGateSpec Spec;
				Spec.GateId = (Index == 0)
					? FName(TEXT("Gate.StartFinish"))
					: FName(*FString::Printf(TEXT("Gate.%02d"), Index));
				Spec.DistanceAlongCm = (Index == 0) ? 0.0 : LapLengthCm * static_cast<double>(Index) / 4.0;
				Spec.HalfWidthCm = MatrixGateHalfWidthCm;
				Spec.HalfHeightCm = MatrixGateHalfHeightCm;
				Spec.LegalDirection = ERacingGateDirection::Forward;
				Specs.Add(Spec);
			}

			if (!Gates.Build(Specs, Circle, MatrixCircleRadiusCm, Error))
			{
				Test.AddError(FString::Printf(TEXT("Matrix gate set failed to build: %s"), *Error));
				return false;
			}

			SectorStartsCm.Reset();
			SectorStartsCm.Add(0.0);
			SectorStartsCm.Add(LapLengthCm / 3.0);
			SectorStartsCm.Add(LapLengthCm * 2.0 / 3.0);

			Ruleset.Reset(NewObject<URaceRulesetDataAsset>(GetTransientPackage()));
			Ruleset->RulesetId = FName(TEXT("Ruleset.Test.FaultMatrix"));
			Ruleset->CountdownSeconds = 3.0;
			Ruleset->bResetInvalidatesLap = bResetInvalidatesLap;

			Machine.Reset(URaceStateMachine::CreateWithTimeSource(
				GetTransientPackage(), Ruleset.Get(), &MatrixTimeSource));
			if (!Machine.IsValid())
			{
				Test.AddError(TEXT("URaceStateMachine::CreateWithTimeSource returned null."));
				return false;
			}

			Tracker.Reset(URaceLapTracker::Create(GetTransientPackage(), Machine.Get(), Ruleset.Get()));
			if (!Tracker.IsValid())
			{
				Test.AddError(TEXT("URaceLapTracker::Create returned null."));
				return false;
			}

			if (!Tracker->ConfigureTrack(Gates, SectorStartsCm, LapLengthCm, Error))
			{
				Test.AddError(FString::Printf(TEXT("Matrix lap tracker failed to configure: %s"), *Error));
				return false;
			}

			return true;
		}

		/** World position at an arc length, optionally offset radially outward (positive = wide). */
		FVector PositionAt(const double DistanceCm, const double OutwardOffsetCm = 0.0) const
		{
			const FVector Base = Circle.GetLocationAtDistanceCm(DistanceCm);
			if (FMath::IsNearlyZero(OutwardOffsetCm))
			{
				return Base;
			}

			return Base + Base.GetSafeNormal2D() * OutwardOffsetCm;
		}

		/** Put the car on the grid, behind the line, and go green. */
		void StartRacing(const double GridDistanceCm)
		{
			CurrentDistanceCm = GridDistanceCm;
			Tracker->SeedProgress(PositionAt(GridDistanceCm), GridDistanceCm);
			Machine->BeginCountdown();
			GMatrixNowSeconds += 3.0;
			Machine->StartRace();
		}

		/** One evaluation step to a new arc length. */
		FRaceLapTrackerUpdate Step(const double ToDistanceCm, const double OutwardOffsetCm = 0.0)
		{
			GMatrixNowSeconds += MatrixSecondsPerStep;
			CurrentDistanceCm = ToDistanceCm;
			return Tracker->Advance(PositionAt(ToDistanceCm, OutwardOffsetCm), ToDistanceCm);
		}

		/**
		 * Drive from the current arc length to ToDistanceCm in Steps steps. Works in both
		 * directions -- a reverse drive is just a descending Lerp.
		 *
		 * WideFrom/WideTo bound an arc-length window in which the car runs OutwardOffsetCm
		 * wide: through the gate PLANE but outside the gate RECTANGLE, which TRACK-002
		 * reports as OutsideExtent and this suite uses as its shortcut.
		 */
		FMatrixDriveSummary Drive(
			const double ToDistanceCm,
			const int32 Steps,
			const double WideFromCm = 0.0,
			const double WideToCm = -1.0,
			const double OutwardOffsetCm = 0.0)
		{
			FMatrixDriveSummary Summary;

			const double FromCm = CurrentDistanceCm;
			for (int32 Index = 1; Index <= Steps; ++Index)
			{
				const double Alpha = static_cast<double>(Index) / static_cast<double>(Steps);
				const double DistanceCm = FMath::Lerp(FromCm, ToDistanceCm, Alpha);
				const bool bWide = (WideToCm > WideFromCm) && (DistanceCm >= WideFromCm) && (DistanceCm <= WideToCm);
				Summary.Accumulate(Step(DistanceCm, bWide ? OutwardOffsetCm : 0.0));
			}

			return Summary;
		}

		/** Steps proportional to the arc covered, so no step ever approaches the quarter-lap teleport bound. */
		int32 StepsFor(const double ArcCm) const
		{
			return FMath::Max(8, FMath::RoundToInt32(MatrixStepsPerLap * FMath::Abs(ArcCm) / LapLengthCm));
		}

		/** Drive to an arc length with an automatically safe step count. */
		FMatrixDriveSummary DriveTo(const double ToDistanceCm)
		{
			return Drive(ToDistanceCm, StepsFor(ToDistanceCm - CurrentDistanceCm));
		}
	};

	// =======================================================================
	// The fault injectors
	// =======================================================================

	enum class EMatrixFault : uint8
	{
		Shortcut,
		Reverse,
		VehicleReset
	};

	const TCHAR* FaultName(const EMatrixFault Fault)
	{
		switch (Fault)
		{
		case EMatrixFault::Shortcut:     return TEXT("Shortcut");
		case EMatrixFault::Reverse:      return TEXT("Reverse");
		case EMatrixFault::VehicleReset: return TEXT("VehicleReset");
		default:                         return TEXT("<unknown>");
		}
	}

	/** The reason each injector is expected to produce when it is the FIRST fault on a lap. */
	ERaceLapInvalidReason ExpectedReason(const EMatrixFault Fault)
	{
		switch (Fault)
		{
		case EMatrixFault::Shortcut:     return ERaceLapInvalidReason::MissedCheckpoint;
		case EMatrixFault::Reverse:      return ERaceLapInvalidReason::ReverseFinishCrossing;
		case EMatrixFault::VehicleReset: return ERaceLapInvalidReason::VehicleReset;
		default:                         return ERaceLapInvalidReason::None;
		}
	}

	/**
	 * SPATIAL LAYOUT, and why the injectors compose in any order.
	 *
	 * Each injector owns a disjoint stretch of the circuit and returns the car to a known
	 * arc length, so any injector can follow any other:
	 *
	 *   Reverse       the start/finish line: forward past it, back over it, forward again.
	 *                 Leaves the car just past the line, gate 0 satisfied, gate 1 expected.
	 *   VehicleReset  an announced reset to a point just behind the car. Leaves the car
	 *                 wherever it was put.
	 *   Shortcut      wide around gate 1 (quarter lap), rejoining before gate 2 (half lap),
	 *                 which is where the out-of-order crossing is DETECTED. Leaves the car
	 *                 just past gate 2.
	 *
	 * Shortcut is deliberately built on gate 1 rather than a later gate: it must be able to
	 * run FIRST and still leave room for a Reverse to follow it, which means the car has to
	 * be able to get back to the line afterwards without closing a lap.
	 */

	/** Cross the line the wrong way. Assumes the car is behind or near the line. */
	void InjectReverse(FMatrixRig& Rig)
	{
		Rig.DriveTo(400.0);
		Rig.DriveTo(-600.0);
		Rig.DriveTo(600.0);
	}

	/**
	 * Announce a reset to a pose slightly behind the car's current position.
	 *
	 * THE GUARD IS NOT DECORATION. A reset pose is an arc length, and arc length is a
	 * WRAPPING domain: asking to be put back 2500 cm from 400 cm yields -2100, which the
	 * tracker correctly normalises to LapLength - 2100 -- i.e. a pose near the END of the
	 * lap, on the far side of the start/finish line from where the car actually is. That is
	 * the tracker behaving correctly and the TEST being wrong, and it cost a real failing
	 * run to find (RACE-004 repair 1). So the injector guarantees there is a lap's worth of
	 * road behind the car before it asks to be moved back onto it.
	 */
	void InjectVehicleReset(FMatrixRig& Rig)
	{
		if (Rig.CurrentDistanceCm < 4000.0)
		{
			Rig.DriveTo(4000.0);
		}

		const double ResetDistanceCm = Rig.CurrentDistanceCm - 2500.0;
		Rig.Tracker->NotifyVehicleReset(Rig.PositionAt(ResetDistanceCm), ResetDistanceCm);
		Rig.CurrentDistanceCm = ResetDistanceCm;
	}

	/** Run wide around gate 1, then cross gate 2 with gate 1 still unmet. */
	void InjectShortcut(FMatrixRig& Rig)
	{
		const double LapCm = Rig.LapLengthCm;
		const double GateOneCm = LapCm * 0.25;

		// Approach.
		Rig.DriveTo(GateOneCm - 4000.0);

		// Wide THROUGH gate 1's plane but outside its rectangle.
		Rig.Drive(GateOneCm + 4000.0, Rig.StepsFor(8000.0),
			GateOneCm - 3000.0, GateOneCm + 3000.0, MatrixWideOffsetCm);

		// Gate 2 is the out-of-order crossing that turns a missed gate into a verdict.
		Rig.DriveTo(LapCm * 0.5 + 2000.0);
	}

	void Inject(FMatrixRig& Rig, const EMatrixFault Fault)
	{
		switch (Fault)
		{
		case EMatrixFault::Shortcut:     InjectShortcut(Rig); break;
		case EMatrixFault::Reverse:      InjectReverse(Rig); break;
		case EMatrixFault::VehicleReset: InjectVehicleReset(Rig); break;
		default: break;
		}
	}

	/**
	 * Return the car to just behind the start/finish line WITHOUT closing a lap, so a
	 * Reverse can follow a fault that happened further round the circuit.
	 *
	 * Driving backwards is the only way to do this: driving forwards over the line is a lap
	 * boundary. The reverse sweep re-crosses gates the car already took, which rewinds them
	 * -- that is TRACK-002's established behaviour and is exactly what makes this a
	 * legitimate composition rather than a trick.
	 */
	void ReturnToLineBackwards(FMatrixRig& Rig)
	{
		Rig.DriveTo(400.0);
	}

	/**
	 * The benign axis: forward, back, forward across a gate the car has ALREADY satisfied.
	 * Nets zero advances and must record no fault.
	 */
	void InjectDoubleTrigger(FMatrixRig& Rig, const double GateCm)
	{
		Rig.DriveTo(GateCm - 400.0);
		Rig.Step(GateCm + 500.0);
		Rig.Step(GateCm - 300.0);
		Rig.Step(GateCm + 700.0);
	}

	/** Everything a verdict is, for the differential comparisons in section 5. */
	struct FMatrixVerdict
	{
		ERaceLapInvalidReason Reason = ERaceLapInvalidReason::None;
		int32 GateIndex = INDEX_NONE;
		FName GateId;
		int32 ValidLaps = 0;
		int32 LapsCompleted = 0;

		bool operator==(const FMatrixVerdict& Other) const
		{
			return Reason == Other.Reason
				&& GateIndex == Other.GateIndex
				&& GateId == Other.GateId
				&& ValidLaps == Other.ValidLaps
				&& LapsCompleted == Other.LapsCompleted;
		}

		FString ToString() const
		{
			return FString::Printf(
				TEXT("Reason=%d GateIndex=%d GateId=%s ValidLaps=%d LapsCompleted=%d"),
				static_cast<int32>(Reason), GateIndex, *GateId.ToString(), ValidLaps, LapsCompleted);
		}
	};

	FMatrixVerdict ReadVerdict(const FMatrixRig& Rig)
	{
		const FRaceLapInvalidity Invalidity = Rig.Tracker->GetCurrentLapInvalidity();

		FMatrixVerdict Verdict;
		Verdict.Reason = Invalidity.Reason;
		Verdict.GateIndex = Invalidity.GateIndex;
		Verdict.GateId = Invalidity.GateId;
		Verdict.ValidLaps = Rig.Tracker->GetValidLapsCompleted();
		Verdict.LapsCompleted = Rig.Tracker->GetLapsCompleted();
		return Verdict;
	}

	/** Open a lap: on the grid, green, and forward over the line. */
	void BeginLap(FMatrixRig& Rig)
	{
		Rig.StartRacing(-800.0);
		Rig.DriveTo(400.0);
	}
}

// ===========================================================================
// 1. The expected messages every faulted drive in this file may emit
// ===========================================================================

namespace RaceFaultMatrixSpecPrivate
{
	void ExpectFaultMessages(FAutomationTestBase& Test)
	{
		// Announced resets and wide gate passes both log. AutomationTest.cpp gates the
		// occurrence check on ExpectedNumberOfOccurrences > 0; any value < 0 (as used here)
		// is excluded from that check entirely, so zero occurrences never fails the test.
		// Same call shape RaceLapTrackerSpec.cpp uses.
		Test.AddExpectedMessage(TEXT("plausibility bounds; treating it as an unannounced teleport"),
			ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains,
			/*Occurrences=*/-1, /*IsRegex=*/false);
		Test.AddExpectedMessage(TEXT("NotifyVehicleReset was given a non-finite pose"),
			ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains,
			/*Occurrences=*/-1, /*IsRegex=*/false);
	}
}

// ===========================================================================
// 2. Each injector is POTENT ON ITS OWN
// ===========================================================================
//
// This section is load-bearing, not warm-up. Every first-fault-wins assertion in section 3
// has the shape "after A then B, the reason is A's". An injector B that silently did
// nothing would satisfy every one of those assertions while proving nothing at all. These
// singles are what rule that out.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceFaultMatrixSinglesTest,
	"RacingSim.Race.FaultMatrixSingles",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceFaultMatrixSinglesTest::RunTest(const FString& Parameters)
{
	using namespace RaceFaultMatrixSpecPrivate;

	ExpectFaultMessages(*this);

	const EMatrixFault AllFaults[] =
	{
		EMatrixFault::Shortcut,
		EMatrixFault::Reverse,
		EMatrixFault::VehicleReset
	};

	for (const EMatrixFault Fault : AllFaults)
	{
		FMatrixRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		BeginLap(Rig);
		TestTrue(FString::Printf(TEXT("[%s] the lap opens clean"), FaultName(Fault)),
			Rig.Tracker->GetCurrentLapInvalidity().IsClean());

		Inject(Rig, Fault);

		const FRaceLapInvalidity Invalidity = Rig.Tracker->GetCurrentLapInvalidity();
		TestFalse(FString::Printf(TEXT("[%s] injecting it alone invalidates the lap"), FaultName(Fault)),
			Invalidity.IsClean());
		TestEqual(FString::Printf(TEXT("[%s] ...with its own reason"), FaultName(Fault)),
			Invalidity.Reason, ExpectedReason(Fault));
		TestEqual(FString::Printf(TEXT("[%s] ...and no valid lap"), FaultName(Fault)),
			Rig.Tracker->GetValidLapsCompleted(), 0);
	}

	// The two gate-naming injectors name the RIGHT gate, which is what makes the
	// first-fault-wins assertions in section 3 discriminating rather than a bare enum check.
	{
		FMatrixRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		BeginLap(Rig);
		InjectShortcut(Rig);

		const FRaceLapInvalidity Invalidity = Rig.Tracker->GetCurrentLapInvalidity();
		TestEqual(TEXT("A shortcut names the gate that was MISSED, by index"), Invalidity.GateIndex, 1);
		TestEqual(TEXT("...and by stable id"), Invalidity.GateId, FName(TEXT("Gate.01")));
		TestEqual(TEXT("...mapping onto the Core shortcut validity"),
			Invalidity.ToRunValidity(), ERacingRunValidity::InvalidShortcut);
	}

	{
		FMatrixRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		BeginLap(Rig);
		InjectReverse(Rig);

		const FRaceLapInvalidity Invalidity = Rig.Tracker->GetCurrentLapInvalidity();
		TestEqual(TEXT("A reverse finish crossing names the start/finish gate"), Invalidity.GateIndex, 0);
		TestEqual(TEXT("...mapping onto the Core reverse-crossing validity"),
			Invalidity.ToRunValidity(), ERacingRunValidity::InvalidReverseCrossing);
	}

	return true;
}

// ===========================================================================
// 3. SIX ORDERED PAIRS: first fault wins
// ===========================================================================
//
// The contract FRaceLapInvalidity states in prose ("FIRST FAULT WINS, and that is
// deliberate ... Which fault is reported must not depend on how much further the car
// happened to drive afterwards") asserted as behaviour, for every ordered pair of the
// three fault axes.
//
// Read together with section 2 this is a genuine discrimination: section 2 proves each
// injector produces reason X on a clean lap, so when the same injector produces no change
// here, the only explanation left is that the earlier fault won.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceFaultMatrixOrderedPairsTest,
	"RacingSim.Race.FaultMatrixOrderedPairs",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceFaultMatrixOrderedPairsTest::RunTest(const FString& Parameters)
{
	using namespace RaceFaultMatrixSpecPrivate;

	ExpectFaultMessages(*this);

	struct FPair
	{
		EMatrixFault First;
		EMatrixFault Second;
	};

	const FPair Pairs[] =
	{
		{ EMatrixFault::Reverse,      EMatrixFault::VehicleReset },
		{ EMatrixFault::Reverse,      EMatrixFault::Shortcut     },
		{ EMatrixFault::VehicleReset, EMatrixFault::Reverse      },
		{ EMatrixFault::VehicleReset, EMatrixFault::Shortcut     },
		{ EMatrixFault::Shortcut,     EMatrixFault::Reverse      },
		{ EMatrixFault::Shortcut,     EMatrixFault::VehicleReset }
	};

	for (const FPair& Pair : Pairs)
	{
		const FString Label = FString::Printf(TEXT("[%s then %s]"),
			FaultName(Pair.First), FaultName(Pair.Second));

		FMatrixRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		BeginLap(Rig);
		Inject(Rig, Pair.First);

		const FRaceLapInvalidity AfterFirst = Rig.Tracker->GetCurrentLapInvalidity();
		TestEqual(*(Label + TEXT(" the first fault lands")), AfterFirst.Reason, ExpectedReason(Pair.First));

		// A Reverse must happen AT the line, so a fault that left the car further round the
		// circuit has to drive back first. That backwards drive is itself part of the test:
		// it rewinds gates, and none of that may disturb the latched fault.
		if (Pair.Second == EMatrixFault::Reverse)
		{
			ReturnToLineBackwards(Rig);
			TestEqual(*(Label + TEXT(" driving back to the line does not re-latch the fault")),
				Rig.Tracker->GetCurrentLapInvalidity().Reason, ExpectedReason(Pair.First));
		}

		Inject(Rig, Pair.Second);

		const FRaceLapInvalidity AfterSecond = Rig.Tracker->GetCurrentLapInvalidity();

		// THE ASSERTION THIS WHOLE FILE EXISTS FOR.
		TestEqual(*(Label + TEXT(" the FIRST fault's reason survives the second")),
			AfterSecond.Reason, ExpectedReason(Pair.First));
		TestEqual(*(Label + TEXT(" ...and so does the gate it named")),
			AfterSecond.GateIndex, AfterFirst.GateIndex);
		TestEqual(*(Label + TEXT(" ...and the gate id")),
			AfterSecond.GateId, AfterFirst.GateId);
		TestNotEqual(*(Label + TEXT(" ...and it is NOT the second fault's reason")),
			AfterSecond.Reason, ExpectedReason(Pair.Second));
		TestEqual(*(Label + TEXT(" no valid lap survives two faults")),
			Rig.Tracker->GetValidLapsCompleted(), 0);
	}

	return true;
}

// ===========================================================================
// 4. THE SELF-PAIRS: the same fault twice does not re-latch
// ===========================================================================
//
// A last-fault-wins implementation is caught by section 3. A "latch onto the most RECENT
// instance of the same reason" implementation is not -- the reason enum would match either
// way. This section discriminates on the GATE, which is the field that moves.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceFaultMatrixSelfPairsTest,
	"RacingSim.Race.FaultMatrixSelfPairs",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceFaultMatrixSelfPairsTest::RunTest(const FString& Parameters)
{
	using namespace RaceFaultMatrixSpecPrivate;

	ExpectFaultMessages(*this);

	// -- Shortcut, then a SECOND shortcut at a later gate --------------------
	//
	// The car misses gate 1 (detected at gate 2), then misses gate 3 as well. The verdict
	// must still name GATE 1: the first gate the driver actually skipped, not the last.
	{
		FMatrixRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		BeginLap(Rig);
		InjectShortcut(Rig);

		const FRaceLapInvalidity AfterFirst = Rig.Tracker->GetCurrentLapInvalidity();
		TestEqual(TEXT("The first shortcut names gate 1"), AfterFirst.GateIndex, 1);

		// Now run wide around gate 3 as well and come back to the line.
		const double GateThreeCm = LapCm * 0.75;
		Rig.DriveTo(GateThreeCm - 4000.0);
		Rig.Drive(GateThreeCm + 4000.0, Rig.StepsFor(8000.0),
			GateThreeCm - 3000.0, GateThreeCm + 3000.0, MatrixWideOffsetCm);

		const FRaceLapInvalidity AfterSecond = Rig.Tracker->GetCurrentLapInvalidity();
		TestEqual(TEXT("A second missed gate does not move the verdict onto gate 3"),
			AfterSecond.GateIndex, 1);
		TestEqual(TEXT("...it is still gate 1 by id"), AfterSecond.GateId, FName(TEXT("Gate.01")));
		TestEqual(TEXT("...and still a missed checkpoint"),
			AfterSecond.Reason, ERaceLapInvalidReason::MissedCheckpoint);
	}

	// -- Reverse, then reverse again -----------------------------------------
	{
		FMatrixRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		BeginLap(Rig);
		InjectReverse(Rig);
		const FRaceLapInvalidity AfterFirst = Rig.Tracker->GetCurrentLapInvalidity();
		TestEqual(TEXT("The first reverse crossing lands"),
			AfterFirst.Reason, ERaceLapInvalidReason::ReverseFinishCrossing);

		InjectReverse(Rig);
		const FRaceLapInvalidity AfterSecond = Rig.Tracker->GetCurrentLapInvalidity();
		TestEqual(TEXT("A second reverse crossing changes nothing"),
			AfterSecond.Reason, ERaceLapInvalidReason::ReverseFinishCrossing);
		TestEqual(TEXT("...still naming gate 0"), AfterSecond.GateIndex, 0);

		// AND IT MANUFACTURES NO LAPS. This is the RACE-002 H1 rule, composed with itself:
		// two full oscillations across the line, still no lap closed and still lap 1.
		TestEqual(TEXT("Two oscillations across the line close no lap"),
			Rig.Tracker->GetLapsCompleted(), 0);
		TestEqual(TEXT("...and the car is still on lap 1"), Rig.Tracker->GetCurrentLapNumber(), 1);
	}

	// -- Reset, then reset again ---------------------------------------------
	{
		FMatrixRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		BeginLap(Rig);

		// The two resets are put at DIFFERENT points on the circuit deliberately: if both
		// landed on the same pose, "progress tracks the LAST reset" would be trivially true
		// and would not discriminate between the two resets at all.
		Rig.DriveTo(LapCm * 0.25 + 4000.0);
		InjectVehicleReset(Rig);
		const double FirstResetPoseCm = Rig.CurrentDistanceCm;

		Rig.DriveTo(LapCm * 0.5 + 4000.0);
		InjectVehicleReset(Rig);
		const double SecondResetPoseCm = Rig.CurrentDistanceCm;

		TestNotEqual(TEXT("The two resets really are to different poses"),
			FirstResetPoseCm, SecondResetPoseCm);

		const FRaceLapInvalidity Invalidity = Rig.Tracker->GetCurrentLapInvalidity();
		TestEqual(TEXT("Two resets on one lap still report one vehicle reset"),
			Invalidity.Reason, ERaceLapInvalidReason::VehicleReset);
		TestEqual(TEXT("...and progress tracks the LAST reset pose, not the first"),
			Rig.Tracker->GetProgressDistanceCm(), SecondResetPoseCm, 1.0e-6);
	}

	return true;
}

// ===========================================================================
// 5. THE DOUBLE-TRIGGER AXIS, DIFFERENTIALLY
// ===========================================================================
//
// Double-triggering is the one axis in this matrix that must change NOTHING. Asserting
// that as an absolute ("the lap is clean") only works on an otherwise-clean lap; the
// interesting claim is that it also changes nothing on a lap that is ALREADY ruined, and
// that claim is only testable as a difference.
//
// So every case here is run TWICE -- once plain, once with an oscillation inserted -- and
// the two verdicts are compared field by field. A regression that made a re-crossing
// advance progress, or re-latch a fault, or manufacture a lap, breaks the comparison
// whatever the underlying verdict happened to be.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceFaultMatrixDoubleTriggerTest,
	"RacingSim.Race.FaultMatrixDoubleTrigger",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceFaultMatrixDoubleTriggerTest::RunTest(const FString& Parameters)
{
	using namespace RaceFaultMatrixSpecPrivate;

	ExpectFaultMessages(*this);

	// Drive a case with or without an oscillation across gate 2, and read the verdict.
	// Gate 2 is used because it is satisfied in every case below by the time the
	// oscillation happens -- re-crossing an ALREADY-TAKEN gate is the double-trigger this
	// matrix is about.
	auto RunCase =
		[this](const EMatrixFault* Fault, const bool bWithDoubleTrigger, FMatrixVerdict& OutVerdict) -> bool
	{
		FMatrixRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		BeginLap(Rig);

		if (Fault != nullptr)
		{
			Inject(Rig, *Fault);
		}

		// Reach gate 2 (satisfying it if it is not already satisfied), then optionally
		// oscillate across it.
		Rig.DriveTo(LapCm * 0.5 + 2000.0);
		if (bWithDoubleTrigger)
		{
			InjectDoubleTrigger(Rig, LapCm * 0.5);
		}

		// Finish the lap the same way in both arms.
		Rig.DriveTo(LapCm + 400.0);

		OutVerdict = ReadVerdict(Rig);
		return true;
	};

	const EMatrixFault AllFaults[] =
	{
		EMatrixFault::Shortcut,
		EMatrixFault::Reverse,
		EMatrixFault::VehicleReset
	};

	// -- The clean lap: an oscillation must not cost it its validity ----------
	{
		FMatrixVerdict Plain;
		FMatrixVerdict Oscillated;
		if (!RunCase(nullptr, false, Plain) || !RunCase(nullptr, true, Oscillated))
		{
			return false;
		}

		TestEqual(TEXT("A clean lap is clean"), Plain.Reason, ERaceLapInvalidReason::None);
		TestEqual(TEXT("...and counts"), Plain.ValidLaps, 1);

		if (!TestTrue(TEXT("A double-trigger changes NOTHING about a clean lap's verdict"),
				Plain == Oscillated))
		{
			AddError(FString::Printf(TEXT("  plain:      %s"), *Plain.ToString()));
			AddError(FString::Printf(TEXT("  oscillated: %s"), *Oscillated.ToString()));
		}
	}

	// -- Each faulted lap: an oscillation must not change the verdict either ---
	for (const EMatrixFault Fault : AllFaults)
	{
		FMatrixVerdict Plain;
		FMatrixVerdict Oscillated;
		if (!RunCase(&Fault, false, Plain) || !RunCase(&Fault, true, Oscillated))
		{
			return false;
		}

		TestEqual(FString::Printf(TEXT("[%s] the faulted arm has no valid lap"), FaultName(Fault)),
			Plain.ValidLaps, 0);

		if (!TestTrue(
				*FString::Printf(TEXT("[%s] a double-trigger changes NOTHING about the verdict"), FaultName(Fault)),
				Plain == Oscillated))
		{
			AddError(FString::Printf(TEXT("  plain:      %s"), *Plain.ToString()));
			AddError(FString::Printf(TEXT("  oscillated: %s"), *Oscillated.ToString()));
		}
	}

	// -- And the oscillation itself nets exactly zero forward advances --------
	//
	// The differential above would still pass if the oscillation advanced progress in a way
	// that happened not to reach any of the five compared fields. This pins the mechanism.
	{
		FMatrixRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		BeginLap(Rig);
		Rig.DriveTo(LapCm * 0.5 + 2000.0);

		TestTrue(TEXT("Gate 2 is satisfied before the oscillation"), Rig.Tracker->IsGateSatisfied(2));
		const int32 ExpectedBefore = Rig.Tracker->GetExpectedGateIndex();

		// EVERY LEG IS MEASURED, including the first backwards one. An earlier version of
		// this assertion started the sum only AFTER the car had already dropped back behind
		// the gate, so it summed -1/+1/-1 ... and reported a net of +1 for an oscillation
		// that genuinely nets zero (RACE-004 repair 1). The bug was in the test, but the
		// lesson is the rule: a net-zero claim has to count the whole excursion, from the
		// last known-good state back to it.
		const FMatrixDriveSummary Leg1 = Rig.Drive(LapCm * 0.5 - 400.0, Rig.StepsFor(2400.0));
		const FMatrixDriveSummary Leg2 = Rig.Drive(LapCm * 0.5 + 700.0, 8);
		const FMatrixDriveSummary Leg3 = Rig.Drive(LapCm * 0.5 - 300.0, 8);
		const FMatrixDriveSummary Leg4 = Rig.Drive(LapCm * 0.5 + 900.0, 8);

		// Each individual leg really does cross the gate -- otherwise "nets zero" would be
		// the trivial truth that nothing happened at all.
		TestEqual(TEXT("The backward leg rewinds the gate"), Leg1.GatesAdvanced, -1);
		TestEqual(TEXT("...and the forward leg re-takes it"), Leg2.GatesAdvanced, 1);

		TestEqual(TEXT("An oscillation across an already-taken gate nets zero advances"),
			Leg1.GatesAdvanced + Leg2.GatesAdvanced + Leg3.GatesAdvanced + Leg4.GatesAdvanced, 0);
		TestTrue(TEXT("...leaving it satisfied"), Rig.Tracker->IsGateSatisfied(2));
		TestEqual(TEXT("...and the expected-gate cursor where it was"),
			Rig.Tracker->GetExpectedGateIndex(), ExpectedBefore);
		TestTrue(TEXT("...and the lap still clean: a double-trigger is not a fault"),
			Rig.Tracker->GetCurrentLapInvalidity().IsClean());
	}

	return true;
}

// ===========================================================================
// 6. RESTART CROSSED WITH EVERY CELL
// ===========================================================================
//
// RACE-003 covered restart from a CLEAN session. The obligation this ticket adds is that
// restart is total regardless of what wrecked the previous session -- a latched fault, a
// rewound gate, a re-seeded reset pose, a half-open lap. If any of that leaked, the first
// lap of the new session would inherit a verdict nobody drove.
//
// The check is deliberately end-to-end rather than a field-by-field clear: after the
// restart the tracker must SCORE A CLEAN LAP VALID. A reset that cleared every field this
// test knows to look at but left one it does not would still be caught by that.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceFaultMatrixRestartTest,
	"RacingSim.Race.FaultMatrixRestart",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceFaultMatrixRestartTest::RunTest(const FString& Parameters)
{
	using namespace RaceFaultMatrixSpecPrivate;

	ExpectFaultMessages(*this);

	struct FCell
	{
		EMatrixFault First;
		const EMatrixFault* Second;

		/**
		 * Whether to also oscillate across an already-taken gate before restarting.
		 *
		 * The double-trigger axis is benign by design, which is exactly why it has to appear
		 * HERE as well as in section 5. Section 5 proves an oscillation changes no VERDICT.
		 * It says nothing about whether an oscillation leaves residue that survives
		 * ResetForNewSession() -- a rewound-then-re-satisfied gate flag, or a progress cursor
		 * that walked backwards -- and residue from a benign event is the kind nobody thinks
		 * to clear.
		 */
		bool bDoubleTrigger = false;
	};

	const EMatrixFault Shortcut = EMatrixFault::Shortcut;
	const EMatrixFault Reverse = EMatrixFault::Reverse;
	const EMatrixFault Reset = EMatrixFault::VehicleReset;

	// ALL SIX ordered pairs, not four. An earlier version of this section ran only the four
	// pairs in which Reverse is not the second fault, because a Reverse must happen AT the
	// line and a fault that left the car further round the circuit has to drive back first.
	// That is a reason to handle the case, not to drop it -- and the two dropped cells are
	// the ones whose pre-restart state is the messiest, because the backwards sweep rewinds
	// gates the car had already taken. Dropping the hardest cells from a restart matrix
	// inverts the point of having one.
	const FCell Cells[] =
	{
		{ Shortcut, nullptr,   false },
		{ Reverse,  nullptr,   false },
		{ Reset,    nullptr,   false },
		{ Reverse,  &Reset,    false },
		{ Reverse,  &Shortcut, false },
		{ Reset,    &Shortcut, false },
		{ Shortcut, &Reset,    false },
		{ Shortcut, &Reverse,  false },
		{ Reset,    &Reverse,  false },

		// The benign axis, alone and composed with a fault.
		{ Shortcut, nullptr,   true },
		{ Reset,    &Shortcut, true }
	};

	for (const FCell& Cell : Cells)
	{
		const FString Label = FString::Printf(TEXT("[restart after %s%s%s%s]"),
			FaultName(Cell.First),
			Cell.Second != nullptr ? TEXT("+") : TEXT(""),
			Cell.Second != nullptr ? FaultName(*Cell.Second) : TEXT(""),
			Cell.bDoubleTrigger ? TEXT("+DoubleTrigger") : TEXT(""));

		FMatrixRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;

		BeginLap(Rig);
		Inject(Rig, Cell.First);
		if (Cell.Second != nullptr)
		{
			// Same obligation as section 3: a Reverse has to be driven back to the line
			// first, and that backwards sweep is itself part of the state the restart must
			// then clear.
			if (*Cell.Second == EMatrixFault::Reverse)
			{
				ReturnToLineBackwards(Rig);
			}

			Inject(Rig, *Cell.Second);
		}

		if (Cell.bDoubleTrigger)
		{
			// Oscillate across a gate the car has already taken. Which gate is reachable
			// depends on where the injectors left the car, so drive to gate 2 first -- the
			// same gate section 5 uses, and satisfied in every cell by this point.
			Rig.DriveTo(LapCm * 0.5 + 2000.0);
			InjectDoubleTrigger(Rig, LapCm * 0.5);
		}

		TestFalse(*(Label + TEXT(" the wrecked session really is wrecked")),
			Rig.Tracker->GetCurrentLapInvalidity().IsClean());

		// Restart. Called twice, because RESTART MUST BE IDEMPOTENT -- a second call is
		// what a double-clicked menu button produces, and it must not half-initialise a
		// session the first call already cleared.
		Rig.Machine->Restart();
		Rig.Tracker->ResetForNewSession();
		Rig.Tracker->ResetForNewSession();

		TestTrue(*(Label + TEXT(" the latched fault is gone")),
			Rig.Tracker->GetCurrentLapInvalidity().IsClean());
		TestEqual(*(Label + TEXT(" the lap counter is zero")), Rig.Tracker->GetCurrentLapNumber(), 0);
		TestEqual(*(Label + TEXT(" the valid-lap counter is zero")), Rig.Tracker->GetValidLapsCompleted(), 0);
		TestEqual(*(Label + TEXT(" the completed-lap counter is zero")), Rig.Tracker->GetLapsCompleted(), 0);
		TestEqual(*(Label + TEXT(" the expected gate is the line again")),
			Rig.Tracker->GetExpectedGateIndex(), FRacingCheckpointGateSet::StartFinishGateIndex);
		TestFalse(*(Label + TEXT(" no gate is left satisfied")),
			Rig.Tracker->IsGateSatisfied(0) || Rig.Tracker->IsGateSatisfied(1)
				|| Rig.Tracker->IsGateSatisfied(2) || Rig.Tracker->IsGateSatisfied(3));
		TestFalse(*(Label + TEXT(" no lap is in progress")), Rig.Tracker->IsLapInProgress());
		TestEqual(*(Label + TEXT(" the run validity is Pending again")),
			Rig.Tracker->GetRunValidity(), ERacingRunValidity::Pending);
		TestEqual(*(Label + TEXT(" the best-lap slot is empty")), Rig.Tracker->GetBestValidLap().LapNumber, 0);
		TestEqual(*(Label + TEXT(" the last-lap slot is empty")), Rig.Tracker->GetLastCompletedLap().LapNumber, 0);
		TestTrue(*(Label + TEXT(" the track survives: it is the same circuit")),
			Rig.Tracker->IsConfigured());

		// THE END-TO-END CHECK. A clean lap in the new session must count, exactly once,
		// numbered 1, and be VALID.
		BeginLap(Rig);
		const FMatrixDriveSummary NewLap = Rig.Drive(LapCm + 400.0, MatrixStepsPerLap);

		TestEqual(*(Label + TEXT(" the restarted session counts its first clean lap exactly once")),
			NewLap.LapsCounted, 1);
		TestEqual(*(Label + TEXT(" ...closing exactly one lap")), NewLap.LapsClosed, 1);
		if (NewLap.ClosedLaps.Num() == 1)
		{
			TestEqual(*(Label + TEXT(" ...numbered from 1 again")), NewLap.ClosedLaps[0].LapNumber, 1);
			TestEqual(*(Label + TEXT(" ...and VALID: no fault crossed the restart boundary")),
				NewLap.ClosedLaps[0].Validity, ERacingRunValidity::Valid);
			TestTrue(*(Label + TEXT(" ...with sectors that add up")),
				NewLap.ClosedLaps[0].AreSectorsConsistent());
		}
		TestEqual(*(Label + TEXT(" ...and it is the session's one valid lap")),
			Rig.Tracker->GetValidLapsCompleted(), 1);
	}

	return true;
}

// ===========================================================================
// 7. NEGATIVE CONTROLS
// ===========================================================================
//
// EVERY ASSERTION IN SECTIONS 2-6 IS SATISFIED BY A TRACKER THAT REFUSES ALL LAPS.
// "The lap is invalid", "no valid lap exists", "the reason is still the first one" -- an
// implementation that hard-coded InvalidShortcut and never counted anything would pass a
// large fraction of this file. That is the failure mode this project has been bitten by
// before (Docs/Environment.md: a test that sat green and unexecuted; RACE-002's H1: a
// suite that only ever asserted the VALID count and so missed three phantom laps).
//
// This section is the other direction, and section 6's end-to-end clean lap is a third.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceFaultMatrixNegativeControlTest,
	"RacingSim.Race.FaultMatrixNegativeControl",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceFaultMatrixNegativeControlTest::RunTest(const FString& Parameters)
{
	using namespace RaceFaultMatrixSpecPrivate;

	ExpectFaultMessages(*this);

	// -- Control A: the rig CAN produce a valid lap ---------------------------
	{
		FMatrixRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		BeginLap(Rig);
		const FMatrixDriveSummary Lap = Rig.Drive(LapCm + 400.0, MatrixStepsPerLap);

		TestEqual(TEXT("CONTROL: a clean lap on this rig closes exactly once"), Lap.LapsClosed, 1);
		TestEqual(TEXT("CONTROL: ...and COUNTS"), Lap.LapsCounted, 1);
		TestEqual(TEXT("CONTROL: ...as the session's one valid lap"),
			Rig.Tracker->GetValidLapsCompleted(), 1);
		if (Lap.ClosedLaps.Num() == 1)
		{
			TestEqual(TEXT("CONTROL: ...marked Valid, not merely uncounted"),
				Lap.ClosedLaps[0].Validity, ERacingRunValidity::Valid);
			TestTrue(TEXT("CONTROL: ...with a positive duration"),
				Lap.ClosedLaps[0].LapDurationSeconds > 0.0);
		}
	}

	// -- Control B: two clean laps in a row both count ------------------------
	//
	// Rules out an implementation that allows exactly one lap and then jams.
	{
		FMatrixRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		BeginLap(Rig);
		Rig.Drive(LapCm + 400.0, MatrixStepsPerLap);
		const FMatrixDriveSummary Second = Rig.Drive(LapCm * 2.0 + 400.0, MatrixStepsPerLap);

		TestEqual(TEXT("CONTROL: the second clean lap counts too"), Second.LapsCounted, 1);
		TestEqual(TEXT("CONTROL: ...for two valid laps"), Rig.Tracker->GetValidLapsCompleted(), 2);
		if (Second.ClosedLaps.Num() == 1)
		{
			TestEqual(TEXT("CONTROL: ...numbered 2"), Second.ClosedLaps[0].LapNumber, 2);
		}
	}

	// -- Control C: a double-trigger alone leaves a lap VALID ------------------
	//
	// Without this, section 5's differential comparisons could all be comparing two equally
	// invalid verdicts and would prove nothing.
	{
		FMatrixRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		BeginLap(Rig);
		Rig.DriveTo(LapCm * 0.5 + 2000.0);
		InjectDoubleTrigger(Rig, LapCm * 0.5);
		const FMatrixDriveSummary Rest = Rig.Drive(LapCm + 400.0, Rig.StepsFor(LapCm * 0.5));

		TestEqual(TEXT("CONTROL: a lap containing only a double-trigger still COUNTS"),
			Rest.LapsCounted, 1);
		TestEqual(TEXT("CONTROL: ...as a valid lap"), Rig.Tracker->GetValidLapsCompleted(), 1);
		if (Rest.ClosedLaps.Num() == 1)
		{
			TestEqual(TEXT("CONTROL: ...marked Valid"),
				Rest.ClosedLaps[0].Validity, ERacingRunValidity::Valid);
		}
	}

	// -- Control D: the ruleset switch really does change the reset verdict ----
	//
	// Section 2 asserts a reset invalidates. If it invalidated unconditionally -- ignoring
	// bResetInvalidatesLap -- that assertion would still pass. This is the discriminator.
	{
		FMatrixRig Rig;
		if (!Rig.Build(*this, /*bResetInvalidatesLap*/ false))
		{
			return false;
		}

		BeginLap(Rig);
		Rig.DriveTo(Rig.LapLengthCm * 0.25 + 4000.0);
		InjectVehicleReset(Rig);

		TestTrue(TEXT("CONTROL: with bResetInvalidatesLap false, a reset leaves the lap clean"),
			Rig.Tracker->GetCurrentLapInvalidity().IsClean());

		const FMatrixDriveSummary Rest = Rig.Drive(Rig.LapLengthCm + 400.0, MatrixStepsPerLap);
		TestEqual(TEXT("CONTROL: ...and the lap still counts when it closes"), Rest.LapsCounted, 1);
	}

	return true;
}
