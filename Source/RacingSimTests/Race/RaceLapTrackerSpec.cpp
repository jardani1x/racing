// Copyright RacingSim. All Rights Reserved.

#include "Race/RaceLapTracker.h"
#include "Race/RaceRulesetDataAsset.h"
#include "Race/RaceStateMachine.h"
#include "Race/TrackCenterline.h"
#include "Race/TrackCheckpointGate.h"

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include <limits>

/**
 * RACE-002: URaceLapTracker -- lap counting, ordering, sector timing, validity.
 *
 * ===========================================================================
 * No level, no actor, no world. Deliberately.
 * ===========================================================================
 *
 * TRACK-002's TrackCheckpointGateSpec.cpp set the precedent and Docs/Environment.md
 * gives the hard reason: a SmokeFilter test in this project cannot construct a
 * non-template Actor at all (the typed-element registry is not registered when
 * FEngineLoop::PreInit runs the smoke tests), and the Product gate that can cannot
 * complete on this machine because of an unrelated Pixel Streaming assertion. So every
 * rule here is asserted against a PROCEDURALLY CONSTRUCTED centerline, a gate set built
 * from arithmetic, and a UObject state machine on a fake monotonic clock.
 *
 * That is not merely convenient. A lap-ordering test that needs a loaded map cannot
 * distinguish "the ordering rule is wrong" from "the map moved", and this is the file
 * that decides which laps count.
 *
 * TRACK-002 finding L2 (the graybox level has no drivable surface) is therefore a
 * non-issue for RACE-002, and this comment is the explicit record the finding asked
 * for: nothing in this suite needs a car to stand on anything. The first ticket that
 * needs real Chaos contact inherits that obligation unchanged.
 *
 * ===========================================================================
 * What is deliberately NOT tested here
 * ===========================================================================
 *
 * Per-gate crossing-direction detection -- forward, reverse, grazing, high-speed
 * single-tick, plane-vs-extent. TRACK-002 owns and covers all of it
 * (RacingSim.Race.GateCrossingDirection, RacingSim.Race.GateCurvedTrack), and
 * re-asserting it here would create a second opinion about the same rule. This suite
 * consumes those results and tests the ORDER built on top of them.
 *
 * SmokeFilter is mandatory: Docs/Environment.md records that a test outside the one
 * filter the documented gate uses once sat green and unexecuted.
 */

namespace RaceLapSpecPrivate
{
	// Named uniquely rather than placed in an anonymous namespace: unity builds
	// concatenate translation units, and duplicate anonymous symbols across a blob are a
	// redefinition rather than two file-local helpers.

	constexpr double LapSpecCircleRadiusCm = 10000.0;   // 100 m radius, ~628 m lap
	constexpr int32 LapSpecCircleSamples = 720;
	constexpr double LapSpecGateHalfWidthCm = 900.0;
	constexpr double LapSpecGateHalfHeightCm = 500.0;
	constexpr int32 LapSpecStepsPerLap = 400;
	constexpr double LapSpecSecondsPerStep = 0.016;

	/** The fake monotonic clock. Captureless so it converts to FRaceTimeSourceFn. */
	double GLapSpecNowSeconds = 0.0;
	double LapSpecTimeSource()
	{
		return GLapSpecNowSeconds;
	}

	/** A clock that never returns a usable reading. Drives the RACE-001 M4 path. */
	double LapSpecBrokenTimeSource()
	{
		return std::numeric_limits<double>::quiet_NaN();
	}

	/** Everything one Advance() step reported, accumulated over a drive. */
	struct FLapDriveSummary
	{
		int32 LapsClosed = 0;
		int32 LapsCounted = 0;
		int32 LapsOpened = 0;
		int32 GatesAdvanced = 0;
		int32 SectorsClosed = 0;
		int32 NearMisses = 0;
		int32 Teleports = 0;
		TArray<FRacingLapTiming> ClosedLaps;

		void Accumulate(const FRaceLapTrackerUpdate& Update)
		{
			LapsClosed += Update.bLapClosed ? 1 : 0;
			LapsCounted += Update.bLapCounted ? 1 : 0;
			LapsOpened += Update.bLapOpened ? 1 : 0;
			GatesAdvanced += Update.GatesAdvanced;
			SectorsClosed += Update.SectorsClosed;
			NearMisses += Update.NearMissCount;
			Teleports += Update.bTeleportDetected ? 1 : 0;

			if (Update.bLapClosed)
			{
				ClosedLaps.Add(Update.ClosedLap);
			}
		}
	};

	/**
	 * A closed circular track, four ordered gates, three sectors, a state machine on a
	 * fake clock, and a tracker wired to all of it.
	 *
	 * Everything is arithmetic. Nothing traces any circuit (CLAUDE.md).
	 */
	struct FLapSpecRig
	{
		FTrackCenterline Circle;
		FRacingCheckpointGateSet Gates;
		TArray<double> SectorStartsCm;
		TStrongObjectPtr<URaceRulesetDataAsset> Ruleset;
		TStrongObjectPtr<URaceStateMachine> Machine;
		TStrongObjectPtr<URaceLapTracker> Tracker;

		double LapLengthCm = 0.0;

		/** Where the car is, in arc length. Monotonically increasing across laps; the tracker wraps it. */
		double CurrentDistanceCm = 0.0;

		bool Build(FAutomationTestBase& Test, const bool bResetInvalidatesLap = true, const bool bBrokenClock = false)
		{
			GLapSpecNowSeconds = 5000.0;

			const double TotalCm = 2.0 * UE_DOUBLE_PI * LapSpecCircleRadiusCm;
			const double StepCm = TotalCm / static_cast<double>(LapSpecCircleSamples);

			TArray<FVector> Locations;
			TArray<double> Distances;
			Locations.Reserve(LapSpecCircleSamples);
			Distances.Reserve(LapSpecCircleSamples);
			for (int32 Index = 0; Index < LapSpecCircleSamples; ++Index)
			{
				const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(Index) / static_cast<double>(LapSpecCircleSamples);
				Locations.Add(FVector(
					LapSpecCircleRadiusCm * FMath::Cos(Angle),
					LapSpecCircleRadiusCm * FMath::Sin(Angle),
					0.0));
				Distances.Add(static_cast<double>(Index) * StepCm);
			}

			FString Error;
			if (!Circle.Build(Locations, Distances, TotalCm, /*bClosedLoop*/ true, Error))
			{
				Test.AddError(FString::Printf(TEXT("Circle centerline failed to build: %s"), *Error));
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
				Spec.HalfWidthCm = LapSpecGateHalfWidthCm;
				Spec.HalfHeightCm = LapSpecGateHalfHeightCm;
				Spec.LegalDirection = ERacingGateDirection::Forward;
				Specs.Add(Spec);
			}

			if (!Gates.Build(Specs, Circle, LapSpecCircleRadiusCm, Error))
			{
				Test.AddError(FString::Printf(TEXT("Gate set failed to build: %s"), *Error));
				return false;
			}

			SectorStartsCm.Reset();
			SectorStartsCm.Add(0.0);
			SectorStartsCm.Add(LapLengthCm / 3.0);
			SectorStartsCm.Add(LapLengthCm * 2.0 / 3.0);

			Ruleset.Reset(NewObject<URaceRulesetDataAsset>(GetTransientPackage()));
			Ruleset->RulesetId = FName(TEXT("Ruleset.Test.LapTracker"));
			Ruleset->CountdownSeconds = 3.0;
			Ruleset->bResetInvalidatesLap = bResetInvalidatesLap;

			Machine.Reset(URaceStateMachine::CreateWithTimeSource(
				GetTransientPackage(), Ruleset.Get(),
				bBrokenClock ? &LapSpecBrokenTimeSource : &LapSpecTimeSource));

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
				Test.AddError(FString::Printf(TEXT("Lap tracker failed to configure: %s"), *Error));
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

			// The circle is centred on the origin, so "radially outward" is the position's
			// own direction. On a counter-clockwise lap that is the driver's LEFT.
			return Base + Base.GetSafeNormal2D() * OutwardOffsetCm;
		}

		/** Put the car on the grid, behind the line, and go green. */
		void StartRacing(const double GridDistanceCm)
		{
			CurrentDistanceCm = GridDistanceCm;
			Tracker->SeedProgress(PositionAt(GridDistanceCm), GridDistanceCm);
			Machine->BeginCountdown();
			GLapSpecNowSeconds += 3.0;
			Machine->StartRace();
		}

		/** One evaluation step to a new arc length. */
		FRaceLapTrackerUpdate Step(const double ToDistanceCm, const double OutwardOffsetCm = 0.0)
		{
			GLapSpecNowSeconds += LapSpecSecondsPerStep;
			CurrentDistanceCm = ToDistanceCm;
			return Tracker->Advance(PositionAt(ToDistanceCm, OutwardOffsetCm), ToDistanceCm);
		}

		/**
		 * Drive forward from the current arc length to ToDistanceCm in Steps steps.
		 *
		 * WideFrom/WideTo bound an arc-length window in which the car runs OutwardOffsetCm
		 * wide -- the "went round the outside of the gate" case, which must register as a
		 * near miss and never as a crossing.
		 */
		FLapDriveSummary Drive(
			const double ToDistanceCm,
			const int32 Steps,
			const double WideFromCm = 0.0,
			const double WideToCm = -1.0,
			const double OutwardOffsetCm = 0.0)
		{
			FLapDriveSummary Summary;

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
	};

	/** One lap of steps, for a drive whose length is one lap. */
	int32 StepsFor(const double ArcCm, const double LapCm)
	{
		return FMath::Max(4, FMath::RoundToInt32(LapSpecStepsPerLap * ArcCm / LapCm));
	}
}

// ===========================================================================
// 1. Configuration refuses what it cannot validate laps on
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceLapTrackerConfigurationTest,
	"RacingSim.Race.LapTrackerConfiguration",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceLapTrackerConfigurationTest::RunTest(const FString& Parameters)
{
	using namespace RaceLapSpecPrivate;

	AddExpectedMessage(TEXT("URaceLapTracker::Create requires"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/-1, /*IsRegex=*/false);

	FLapSpecRig Rig;
	if (!Rig.Build(*this))
	{
		return false;
	}

	// -- Create() refuses what it cannot time --------------------------------
	TestNull(TEXT("A tracker cannot be created without an owner"),
		URaceLapTracker::Create(nullptr, Rig.Machine.Get()));
	TestNull(TEXT("A tracker cannot be created without a state machine: there is no other clock"),
		URaceLapTracker::Create(GetTransientPackage(), nullptr));

	TestTrue(TEXT("The rig's tracker is configured"), Rig.Tracker->IsConfigured());
	TestEqual(TEXT("It starts expecting the start/finish gate"),
		Rig.Tracker->GetExpectedGateIndex(), FRacingCheckpointGateSet::StartFinishGateIndex);
	TestEqual(TEXT("...on lap 0, because no line crossing has happened"), Rig.Tracker->GetCurrentLapNumber(), 0);
	TestEqual(TEXT("...with three sectors"), Rig.Tracker->GetNumSectors(), 3);
	TestEqual(TEXT("...and a Pending run validity"), Rig.Tracker->GetRunValidity(), ERacingRunValidity::Pending);

	// -- ConfigureTrack() rejections ----------------------------------------
	{
		const TStrongObjectPtr<URaceLapTracker> Fresh(
			URaceLapTracker::Create(GetTransientPackage(), Rig.Machine.Get()));
		TestTrue(TEXT("A fresh tracker exists"), Fresh.IsValid());
		TestFalse(TEXT("...and is not configured yet"), Fresh->IsConfigured());

		FString Error;
		const FRacingCheckpointGateSet Unbuilt;
		TestFalse(TEXT("An unbuilt gate set is refused"),
			Fresh->ConfigureTrack(Unbuilt, Rig.SectorStartsCm, Rig.LapLengthCm, Error));
		TestTrue(TEXT("...with a reason"), !Error.IsEmpty());

		// One gate: builds as geometry, enforces no order. The floor exists so a tracker
		// cannot silently validate laps it has no way of invalidating.
		FRacingCheckpointGateSet OneGate;
		TArray<FRacingCheckpointGateSpec> OneSpec;
		FRacingCheckpointGateSpec Spec;
		Spec.GateId = FName(TEXT("Gate.StartFinish"));
		Spec.DistanceAlongCm = 0.0;
		Spec.HalfWidthCm = LapSpecGateHalfWidthCm;
		Spec.HalfHeightCm = LapSpecGateHalfHeightCm;
		OneSpec.Add(Spec);
		FString BuildError;
		TestTrue(TEXT("A one-gate set is well-formed GEOMETRY"),
			OneGate.Build(OneSpec, Rig.Circle, LapSpecCircleRadiusCm, BuildError));

		Error.Reset();
		TestFalse(TEXT("...but a tracker refuses it: one gate has no order to enforce"),
			Fresh->ConfigureTrack(OneGate, Rig.SectorStartsCm, Rig.LapLengthCm, Error));
		TestTrue(TEXT("...and says so"), Error.Contains(TEXT("ordered gates")));

		Error.Reset();
		TestFalse(TEXT("A non-positive lap length is refused"),
			Fresh->ConfigureTrack(Rig.Gates, Rig.SectorStartsCm, 0.0, Error));

		Error.Reset();
		TArray<double> BadSectors;
		BadSectors.Add(100.0);
		TestFalse(TEXT("A sector table whose first entry is not 0 is refused"),
			Fresh->ConfigureTrack(Rig.Gates, BadSectors, Rig.LapLengthCm, Error));

		Error.Reset();
		TArray<double> UnorderedSectors;
		UnorderedSectors.Add(0.0);
		UnorderedSectors.Add(20000.0);
		UnorderedSectors.Add(10000.0);
		TestFalse(TEXT("A sector table that does not strictly increase is refused"),
			Fresh->ConfigureTrack(Rig.Gates, UnorderedSectors, Rig.LapLengthCm, Error));

		// A reverse-only start/finish gate can never open a lap. Rejected here as well as
		// in ATrackDefinitionActor::Validate(), because a tracker can be handed a set that
		// never went through that validation.
		Error.Reset();
		TArray<FRacingCheckpointGateSpec> ReverseSpecs;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			FRacingCheckpointGateSpec ReverseSpec;
			ReverseSpec.GateId = FName(*FString::Printf(TEXT("Gate.%02d"), Index));
			ReverseSpec.DistanceAlongCm = Rig.LapLengthCm * static_cast<double>(Index) / 4.0;
			ReverseSpec.HalfWidthCm = LapSpecGateHalfWidthCm;
			ReverseSpec.HalfHeightCm = LapSpecGateHalfHeightCm;
			ReverseSpec.LegalDirection = (Index == 0) ? ERacingGateDirection::Reverse : ERacingGateDirection::Forward;
			ReverseSpecs.Add(ReverseSpec);
		}
		FRacingCheckpointGateSet ReverseSet;
		TestTrue(TEXT("A reverse-only start/finish gate is well-formed geometry"),
			ReverseSet.Build(ReverseSpecs, Rig.Circle, LapSpecCircleRadiusCm, BuildError));
		TestFalse(TEXT("...but no lap could ever be completed on it, so a tracker refuses it"),
			Fresh->ConfigureTrack(ReverseSet, Rig.SectorStartsCm, Rig.LapLengthCm, Error));

		TestFalse(TEXT("Every rejection left the tracker unconfigured"), Fresh->IsConfigured());
	}

	// -- An empty sector table is legal: laps without splits -----------------
	{
		const TStrongObjectPtr<URaceLapTracker> NoSectors(
			URaceLapTracker::Create(GetTransientPackage(), Rig.Machine.Get()));
		FString Error;
		TestTrue(TEXT("A track with no authored sectors still counts laps"),
			NoSectors->ConfigureTrack(Rig.Gates, TArrayView<const double>(), Rig.LapLengthCm, Error));
		TestEqual(TEXT("...and reports zero sectors"), NoSectors->GetNumSectors(), 0);
	}

	// -- The teleport bounds are derived, not authored ------------------------
	TestEqual(TEXT("The implausible ARC bound is a quarter lap, derived from the track alone"),
		Rig.Tracker->GetImplausibleArcStepCm(), Rig.LapLengthCm * 0.25, 1.0e-9);
	TestEqual(TEXT("The implausible CHORD bound is half a lap, likewise"),
		Rig.Tracker->GetImplausibleChordStepCm(), Rig.LapLengthCm * 0.5, 1.0e-9);

	// The circle fixture is exactly the shape that makes a chord-only guard useless, and
	// pinning it here stops a future edit collapsing the two tests back into one: the
	// largest straight line available on this track (the diameter) is well UNDER the
	// chord bound, so on this fixture only the arc test can ever fire.
	TestTrue(TEXT("On a circle the largest possible chord is below the chord bound, so the arc test is load-bearing"),
		2.0 * LapSpecCircleRadiusCm < Rig.Tracker->GetImplausibleChordStepCm());

	return true;
}

// ===========================================================================
// 2. A clean lap: ordered gates, one increment, consistent sectors
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceLapCleanLapTest,
	"RacingSim.Race.LapCleanLap",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceLapCleanLapTest::RunTest(const FString& Parameters)
{
	using namespace RaceLapSpecPrivate;

	FLapSpecRig Rig;
	if (!Rig.Build(*this))
	{
		return false;
	}

	const double LapCm = Rig.LapLengthCm;

	// On the grid, 800 cm behind the line -- the same setback ATrackDefinitionActor
	// generates. Nothing has been crossed, so nothing may have been counted.
	Rig.StartRacing(-800.0);
	TestEqual(TEXT("Sitting on the grid completes no lap"), Rig.Tracker->GetLapsCompleted(), 0);

	// Roll up to and over the line: lap 1 opens, nothing closes.
	{
		const FLapDriveSummary Summary = Rig.Drive(400.0, 8);
		TestEqual(TEXT("Crossing the line from the grid opens exactly one lap"), Summary.LapsOpened, 1);
		TestEqual(TEXT("...and closes none: there was no lap to close"), Summary.LapsClosed, 0);
		TestEqual(TEXT("...leaving the car on lap 1"), Rig.Tracker->GetCurrentLapNumber(), 1);
		TestEqual(TEXT("...expecting gate 1"), Rig.Tracker->GetExpectedGateIndex(), 1);
		TestTrue(TEXT("...with the start/finish gate satisfied"), Rig.Tracker->IsGateSatisfied(0));
		TestEqual(TEXT("...timing sector 0"), Rig.Tracker->GetCurrentSectorIndex(), 0);
	}

	// One full lap on the centerline.
	const FLapDriveSummary Lap = Rig.Drive(400.0 + LapCm, LapSpecStepsPerLap);

	TestEqual(TEXT("A clean lap closes exactly once"), Lap.LapsClosed, 1);
	TestEqual(TEXT("...counts exactly once"), Lap.LapsCounted, 1);
	TestEqual(TEXT("...and opens the next lap exactly once"), Lap.LapsOpened, 1);
	TestEqual(TEXT("...crossing no gate outside its extent"), Lap.NearMisses, 0);
	TestEqual(TEXT("...detecting no teleport"), Lap.Teleports, 0);
	TestEqual(TEXT("The valid lap count is 1"), Rig.Tracker->GetValidLapsCompleted(), 1);
	TestEqual(TEXT("The car is now on lap 2"), Rig.Tracker->GetCurrentLapNumber(), 2);

	// Four gate advances: gates 1, 2, 3, then the line opening lap 2.
	TestEqual(TEXT("Four ordered gates were satisfied across the lap"), Lap.GatesAdvanced, 4);
	TestEqual(TEXT("Two sector boundaries were timed inside the lap"), Lap.SectorsClosed, 2);

	if (Lap.ClosedLaps.Num() != 1)
	{
		AddError(TEXT("Expected exactly one closed lap to inspect."));
		return false;
	}

	const FRacingLapTiming& Closed = Lap.ClosedLaps[0];
	TestEqual(TEXT("The closed lap is lap 1"), Closed.LapNumber, 1);
	TestEqual(TEXT("...and is Valid"), Closed.Validity, ERacingRunValidity::Valid);
	TestTrue(TEXT("...with a positive duration"), Closed.LapDurationSeconds > 0.0);
	TestTrue(TEXT("...and IsComplete()"), Closed.IsComplete());

	// THE CORE-002 CROSS-CHECK, which is an acceptance criterion in its own right:
	// sectors and the lap are timed from the same clock, so they must add up.
	TestEqual(TEXT("Three sector splits, one per authored sector"), Closed.SectorDurationsSeconds.Num(), 3);
	TestTrue(TEXT("AreSectorsConsistent() holds for a completed valid lap"), Closed.AreSectorsConsistent());
	TestNearlyEqual(TEXT("...and the splits sum to the lap duration"),
		Closed.GetSectorTotalSeconds(), Closed.LapDurationSeconds, 1.0e-9);

	for (const double SectorSeconds : Closed.SectorDurationsSeconds)
	{
		TestTrue(TEXT("Every split is strictly positive"), SectorSeconds > 0.0);
	}

	// The lap took very nearly a lap's worth of steps, so its duration is bounded by the
	// fixture's own arithmetic rather than by whatever the machine was doing.
	const double ExpectedLapSeconds = LapSpecStepsPerLap * LapSpecSecondsPerStep;
	TestTrue(TEXT("The lap duration is within a step of the driven time"),
		FMath::Abs(Closed.LapDurationSeconds - ExpectedLapSeconds) < LapSpecSecondsPerStep * 2.0);

	// The best-lap slot tracks the fastest VALID lap.
	TestEqual(TEXT("The best valid lap is lap 1"), Rig.Tracker->GetBestValidLap().LapNumber, 1);
	TestEqual(TEXT("The last completed lap is lap 1"), Rig.Tracker->GetLastCompletedLap().LapNumber, 1);

	// A second clean lap increments again, exactly once.
	const FLapDriveSummary SecondLap = Rig.Drive(400.0 + LapCm * 2.0, LapSpecStepsPerLap);
	TestEqual(TEXT("A second clean lap counts exactly once"), SecondLap.LapsCounted, 1);
	TestEqual(TEXT("...taking the valid lap count to 2"), Rig.Tracker->GetValidLapsCompleted(), 2);
	TestEqual(TEXT("...and the second lap's sectors are consistent too"),
		SecondLap.ClosedLaps.Num() == 1 && SecondLap.ClosedLaps[0].AreSectorsConsistent(), true);

	// The in-progress lap exposes partial splits, per FRacingLapTiming's contract.
	Rig.Drive(400.0 + LapCm * 2.0 + LapCm / 2.0, StepsFor(LapCm / 2.0, LapCm));
	const FRacingLapTiming Running = Rig.Tracker->GetCurrentLapTiming();
	TestEqual(TEXT("The lap in progress is lap 3"), Running.LapNumber, 3);
	TestEqual(TEXT("...is Pending"), Running.Validity, ERacingRunValidity::Pending);
	TestEqual(TEXT("...and publishes only the splits already closed"), Running.SectorDurationsSeconds.Num(), 1);
	TestTrue(TEXT("...with a running duration"), Running.LapDurationSeconds > 0.0);

	// The progress view model, which is what a HUD reads.
	const FRacingProgressSample Progress = Rig.Tracker->GetProgressSample();
	TestEqual(TEXT("Progress reports lap 3"), Progress.LapNumber, 3);
	TestEqual(TEXT("...names the last gate taken"), Progress.LastCheckpointIndex, 2);
	TestTrue(TEXT("...with a fraction near half a lap"),
		Progress.LapProgressFraction > 0.45f && Progress.LapProgressFraction < 0.55f);
	TestEqual(TEXT("...and no race position, because there are no opponents"), Progress.RacePosition, 0);

	return true;
}

// ===========================================================================
// 3. Ordering: skipped gates, reverse finish, double triggers, spins
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceLapOrderingTest,
	"RacingSim.Race.LapOrdering",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceLapOrderingTest::RunTest(const FString& Parameters)
{
	using namespace RaceLapSpecPrivate;

	// -- A skipped gate invalidates the lap and NAMES the gate ---------------
	{
		FLapSpecRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		Rig.StartRacing(-800.0);
		Rig.Drive(400.0, 8);

		// Gate 2 sits at half a lap. The car runs wide around it -- through the plane but
		// outside the rectangle -- which TRACK-002 reports as OutsideExtent and this
		// ticket must treat as "not crossed".
		const double WideFrom = LapCm * 0.5 - 3000.0;
		const double WideTo = LapCm * 0.5 + 3000.0;
		const FLapDriveSummary Lap = Rig.Drive(400.0 + LapCm, LapSpecStepsPerLap,
			WideFrom, WideTo, LapSpecGateHalfWidthCm + 600.0);

		TestTrue(TEXT("Going round the outside of a gate registers as a near miss"), Lap.NearMisses > 0);
		TestEqual(TEXT("The lap still CLOSES at the line"), Lap.LapsClosed, 1);
		TestEqual(TEXT("...but is not counted"), Lap.LapsCounted, 0);
		TestEqual(TEXT("...so no valid lap exists"), Rig.Tracker->GetValidLapsCompleted(), 0);
		TestEqual(TEXT("...and the total lap count still records that a lap happened"),
			Rig.Tracker->GetLapsCompleted(), 1);

		if (Lap.ClosedLaps.Num() == 1)
		{
			const FRacingLapTiming& Closed = Lap.ClosedLaps[0];
			TestEqual(TEXT("The lap is invalid as a shortcut"), Closed.Validity, ERacingRunValidity::InvalidShortcut);

			// AND ITS SECTORS STILL ADD UP, which is not a contradiction and is worth
			// pinning. AreSectorsConsistent() is a TIMING cross-check -- "do the splits
			// account for the lap" -- not a validity check. This car drove the whole
			// route, in order, past every sector boundary; it simply passed one GATE
			// outside its extent. Timing is therefore sound and validity is not, and the
			// two answers are supposed to be independent. A reviewer reading only the
			// clean-lap test could reasonably assume the opposite.
			TestTrue(TEXT("...while its sector timing is still internally consistent: timing and validity are different questions"),
				Closed.AreSectorsConsistent());
		}

		// THE POINT OF THE CRITERION: the reason names the SPECIFIC gate, not "invalid lap".
		// The invalidity is latched on the current lap, which by now is the next one, so
		// the closed lap's reason is read from the update; the tracker's own accessor is
		// checked immediately after the skip instead, below.
		TestEqual(TEXT("The best lap slot is still empty"), Rig.Tracker->GetBestValidLap().LapNumber, 0);
	}

	// -- The named gate, read at the moment of the skip -----------------------
	{
		FLapSpecRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		Rig.StartRacing(-800.0);
		Rig.Drive(400.0, 8);

		// Drive to just past gate 1, then run wide past gate 2 and rejoin before gate 3.
		Rig.Drive(LapCm * 0.25 + 2000.0, StepsFor(LapCm * 0.25, LapCm));
		TestTrue(TEXT("Gate 1 was taken"), Rig.Tracker->IsGateSatisfied(1));
		TestEqual(TEXT("...so gate 2 is expected"), Rig.Tracker->GetExpectedGateIndex(), 2);
		TestTrue(TEXT("...and the lap is still clean"), Rig.Tracker->GetCurrentLapInvalidity().IsClean());

		Rig.Drive(LapCm * 0.5 + 3000.0, StepsFor(LapCm * 0.25, LapCm),
			LapCm * 0.5 - 3000.0, LapCm * 0.5 + 3000.0, LapSpecGateHalfWidthCm + 600.0);
		TestTrue(TEXT("Missing a gate alone does not invalidate anything yet"),
			Rig.Tracker->GetCurrentLapInvalidity().IsClean());
		TestEqual(TEXT("...and the tracker is still waiting for gate 2"),
			Rig.Tracker->GetExpectedGateIndex(), 2);

		// Crossing gate 3 while gate 2 is still expected is the out-of-order event.
		Rig.Drive(LapCm * 0.75 + 2000.0, StepsFor(LapCm * 0.25, LapCm));

		const FRaceLapInvalidity Invalidity = Rig.Tracker->GetCurrentLapInvalidity();
		TestFalse(TEXT("Taking gate 3 with gate 2 unmet invalidates the lap"), Invalidity.IsClean());
		TestEqual(TEXT("...as a missed checkpoint"), Invalidity.Reason, ERaceLapInvalidReason::MissedCheckpoint);
		TestEqual(TEXT("...naming the gate that was MISSED, by index"), Invalidity.GateIndex, 2);
		TestEqual(TEXT("...and by stable id"), Invalidity.GateId, FName(TEXT("Gate.02")));
		TestTrue(TEXT("...and the debug string names it too"),
			Invalidity.ToDebugString().Contains(TEXT("Gate.02")));
		TestEqual(TEXT("...mapping onto the Core shortcut validity"),
			Invalidity.ToRunValidity(), ERacingRunValidity::InvalidShortcut);
	}

	// -- A reverse finish crossing is its own, distinct invalidity ------------
	{
		FLapSpecRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		Rig.StartRacing(-800.0);
		Rig.Drive(400.0, 8);
		TestEqual(TEXT("Lap 1 is open"), Rig.Tracker->GetCurrentLapNumber(), 1);

		// Back over the line, the wrong way.
		const FLapDriveSummary Backwards = Rig.Drive(-600.0, 8);
		TestEqual(TEXT("A reverse crossing of the line closes no lap"), Backwards.LapsClosed, 0);
		TestEqual(TEXT("...and counts none"), Backwards.LapsCounted, 0);

		const FRaceLapInvalidity Invalidity = Rig.Tracker->GetCurrentLapInvalidity();
		TestEqual(TEXT("It is reported as a reverse finish crossing"),
			Invalidity.Reason, ERaceLapInvalidReason::ReverseFinishCrossing);
		TestEqual(TEXT("...NOT as a missed checkpoint"),
			Invalidity.ToRunValidity(), ERacingRunValidity::InvalidReverseCrossing);
		TestEqual(TEXT("...naming the start/finish gate"), Invalidity.GateIndex, 0);

		// The reverse crossing REWINDS gate 0, exactly as a reverse crossing of an ordinary
		// gate rewinds that gate: the car is behind the line and has to cross it forwards
		// again before anything else can count.
		TestFalse(TEXT("The reverse crossing un-satisfies the start/finish gate"),
			Rig.Tracker->IsGateSatisfied(0));
		TestEqual(TEXT("...and points the cursor back at it"), Rig.Tracker->GetExpectedGateIndex(), 0);
		TestEqual(TEXT("...netting the forward pass that opened the lap back to zero"),
			Backwards.GatesAdvanced, -1);

		// TURNING ROUND AND CROSSING FORWARDS AGAIN IS NOT A LAP BOUNDARY. The car is back
		// where it was, having taken no ordered gate since the lap opened, so the forward
		// crossing re-triggers gate 0 and lap 1 keeps running on its original timer.
		//
		// THESE FOUR ASSERTIONS ARE THE H1 FIX AND THEY USED TO READ THE OTHER WAY ROUND --
		// this suite previously asserted the closed lap, the opened lap and the +1 on the
		// lap number that the bug produced, which is why the bug survived review. See
		// RacingSim.Race.LapLineSpin for the multi-oscillation case that makes the
		// difference visible as three phantom laps rather than one.
		const FLapDriveSummary Forwards = Rig.Drive(600.0, 8);
		TestEqual(TEXT("Re-crossing forwards with no ordered gate taken since the lap opened closes nothing"),
			Forwards.LapsClosed, 0);
		TestEqual(TEXT("...counts nothing"), Forwards.LapsCounted, 0);
		TestEqual(TEXT("...opens no new lap"), Forwards.LapsOpened, 0);
		TestEqual(TEXT("The car is STILL on lap 1"), Rig.Tracker->GetCurrentLapNumber(), 1);
		TestEqual(TEXT("...with no lap recorded as completed"), Rig.Tracker->GetLapsCompleted(), 0);
		TestEqual(TEXT("...and nothing in the last-completed-lap slot"),
			Rig.Tracker->GetLastCompletedLap().LapNumber, 0);
		TestEqual(TEXT("...netting the oscillation back to a single forward pass"),
			Forwards.GatesAdvanced, 1);
		TestTrue(TEXT("...with gate 0 satisfied again, so the lap can still be completed"),
			Rig.Tracker->IsGateSatisfied(0));
		TestEqual(TEXT("...and gate 1 expected"), Rig.Tracker->GetExpectedGateIndex(), 1);
		TestEqual(TEXT("The recorded fault is still the reverse crossing, unchanged by the re-trigger"),
			Rig.Tracker->GetCurrentLapInvalidity().Reason, ERaceLapInvalidReason::ReverseFinishCrossing);

		// Lap 1 closes when the car actually DRIVES a lap -- once, uncounted, carrying the
		// FIRST fault. The lap boundary happens where a lap happened, not where the car
		// wiggled.
		const FLapDriveSummary Ruined = Rig.Drive(600.0 + LapCm, LapSpecStepsPerLap);
		TestEqual(TEXT("Driving a real lap closes the ruined lap exactly once"), Ruined.LapsClosed, 1);
		TestEqual(TEXT("...counting nothing"), Ruined.LapsCounted, 0);
		TestEqual(TEXT("...and opening exactly one new lap"), Ruined.LapsOpened, 1);

		if (Ruined.ClosedLaps.Num() == 1)
		{
			TestEqual(TEXT("The closed lap is lap 1"), Ruined.ClosedLaps[0].LapNumber, 1);
			TestEqual(TEXT("...keeping the FIRST fault, the reverse crossing"),
				Ruined.ClosedLaps[0].Validity, ERacingRunValidity::InvalidReverseCrossing);
		}

		TestEqual(TEXT("The car is now on lap 2"), Rig.Tracker->GetCurrentLapNumber(), 2);
		TestEqual(TEXT("...with no valid laps"), Rig.Tracker->GetValidLapsCompleted(), 0);

		// And the ruined lap does not poison the next one: a clean lap from here counts.
		const FLapDriveSummary Clean = Rig.Drive(600.0 + LapCm * 2.0, LapSpecStepsPerLap);
		TestEqual(TEXT("The following clean lap counts exactly once"), Clean.LapsCounted, 1);
		TestEqual(TEXT("...and its sectors are consistent"),
			Clean.ClosedLaps.Num() == 1 && Clean.ClosedLaps[0].AreSectorsConsistent(), true);
	}

	// -- A spin at a gate nets exactly one forward advance --------------------
	{
		FLapSpecRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		Rig.StartRacing(-800.0);
		Rig.Drive(400.0, 8);

		const double GateCm = LapCm * 0.25;
		Rig.Drive(GateCm - 500.0, StepsFor(LapCm * 0.25, LapCm));
		TestEqual(TEXT("Approaching gate 1, it is still expected"), Rig.Tracker->GetExpectedGateIndex(), 1);

		// Forward, back, forward, back, forward -- the same alternating stream TRACK-002's
		// GateCurvedTrack spin case asserts nets to exactly one forward pass. The lap logic
		// built on top must not derive a different net from the same crossings.
		int32 NetAdvances = 0;
		const double SpinPositionsCm[] =
		{
			GateCm + 400.0, GateCm - 300.0, GateCm + 500.0, GateCm - 200.0, GateCm + 700.0
		};
		for (const double PositionCm : SpinPositionsCm)
		{
			NetAdvances += Rig.Step(PositionCm).GatesAdvanced;
		}

		TestEqual(TEXT("A triple spin across gate 1 nets exactly one forward advance"), NetAdvances, 1);
		TestTrue(TEXT("...leaving gate 1 satisfied"), Rig.Tracker->IsGateSatisfied(1));
		TestEqual(TEXT("...and gate 2 expected"), Rig.Tracker->GetExpectedGateIndex(), 2);
		TestTrue(TEXT("...with the lap still clean: a spin is an incident, not a shortcut"),
			Rig.Tracker->GetCurrentLapInvalidity().IsClean());

		// The rest of the lap still has to be driven in order, and then it counts.
		const FLapDriveSummary Rest = Rig.Drive(400.0 + LapCm, StepsFor(LapCm * 0.75, LapCm));
		TestEqual(TEXT("The lap containing the spin still counts exactly once"), Rest.LapsCounted, 1);
		TestEqual(TEXT("...and its sectors add up"),
			Rest.ClosedLaps.Num() == 1 && Rest.ClosedLaps[0].AreSectorsConsistent(), true);
	}

	// -- A lap cannot be manufactured out of spline distance alone ------------
	{
		FLapSpecRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		Rig.StartRacing(-800.0);

		// The car does not move; only its reported arc length does. This is the failure
		// Docs/03-TrackRaceUI.md rule 6 and FRacingProgressSample's comment both name:
		// distance wrapping past the track length is not a lap.
		const FVector Parked = Rig.PositionAt(-800.0);
		for (int32 Step = 0; Step < LapSpecStepsPerLap; ++Step)
		{
			GLapSpecNowSeconds += LapSpecSecondsPerStep;
			const double FakeDistanceCm = -800.0 + LapCm * 2.0 * static_cast<double>(Step) / LapSpecStepsPerLap;
			const FRaceLapTrackerUpdate Update = Rig.Tracker->Advance(Parked, FakeDistanceCm);
			if (Update.bLapClosed || Update.bLapOpened)
			{
				AddError(TEXT("Arc-length distance alone authorised a lap; only ordered gate crossings may."));
				break;
			}
		}

		TestEqual(TEXT("Two laps' worth of distance with no gate crossings completes no lap"),
			Rig.Tracker->GetLapsCompleted(), 0);
		TestEqual(TEXT("...and opens none"), Rig.Tracker->GetCurrentLapNumber(), 0);
	}

	return true;
}

// ===========================================================================
// 4. Reset, teleport and restart
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceLapResetAndRestartTest,
	"RacingSim.Race.LapResetAndRestart",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceLapResetAndRestartTest::RunTest(const FString& Parameters)
{
	using namespace RaceLapSpecPrivate;

	AddExpectedMessage(TEXT("beyond the"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/-1, /*IsRegex=*/false);
	AddExpectedMessage(TEXT("non-finite"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/-1, /*IsRegex=*/false);

	// -- A reset re-seeds progress and, by default, voids the lap -------------
	{
		FLapSpecRig Rig;
		if (!Rig.Build(*this, /*bResetInvalidatesLap*/ true))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		Rig.StartRacing(-800.0);
		Rig.Drive(400.0, 8);
		Rig.Drive(LapCm * 0.25 + 4000.0, StepsFor(LapCm * 0.25, LapCm));
		TestTrue(TEXT("Gate 1 has been taken"), Rig.Tracker->IsGateSatisfied(1));
		TestTrue(TEXT("...and the lap is clean so far"), Rig.Tracker->GetCurrentLapInvalidity().IsClean());

		// The car goes off and is put back at the most recent safe sample, which is
		// BEHIND gate 1. The distance comes from the reset sample itself -- TRACK-001's
		// GetResetSampleDistanceCm -- never from the stale pre-reset progress.
		const double ResetDistanceCm = LapCm * 0.25 - 2500.0;
		Rig.Tracker->NotifyVehicleReset(Rig.PositionAt(ResetDistanceCm), ResetDistanceCm);

		TestEqual(TEXT("Progress is re-seeded to the reset sample's own arc length"),
			Rig.Tracker->GetProgressDistanceCm(), ResetDistanceCm, 1.0e-6);
		TestEqual(TEXT("The default policy voids the lap the reset happened on"),
			Rig.Tracker->GetCurrentLapInvalidity().Reason, ERaceLapInvalidReason::VehicleReset);
		TestEqual(TEXT("...mapping onto the Core vehicle-reset validity"),
			Rig.Tracker->GetCurrentLapInvalidity().ToRunValidity(), ERacingRunValidity::InvalidVehicleReset);

		// THE DUPLICATE-TRIGGER CASE Docs/03-TrackRaceUI.md names: the car re-crosses a
		// gate it has already taken. It must not advance progress and must not count.
		Rig.CurrentDistanceCm = ResetDistanceCm;
		const FLapDriveSummary BackOver = Rig.Drive(LapCm * 0.25 + 2500.0, 24);
		TestEqual(TEXT("Re-crossing an already-satisfied gate advances nothing"), BackOver.GatesAdvanced, 0);
		TestEqual(TEXT("...and closes no lap"), BackOver.LapsClosed, 0);
		TestEqual(TEXT("...leaving gate 2 expected"), Rig.Tracker->GetExpectedGateIndex(), 2);

		// The lap finishes, but as an invalid one.
		const FLapDriveSummary Finish = Rig.Drive(400.0 + LapCm, StepsFor(LapCm * 0.75, LapCm));
		TestEqual(TEXT("The lap containing the reset closes"), Finish.LapsClosed, 1);
		TestEqual(TEXT("...and is not counted"), Finish.LapsCounted, 0);
		if (Finish.ClosedLaps.Num() == 1)
		{
			TestEqual(TEXT("...reported as a vehicle reset"),
				Finish.ClosedLaps[0].Validity, ERacingRunValidity::InvalidVehicleReset);
		}

		// The NEXT lap is clean again: a reset voids one lap, not the session.
		const FLapDriveSummary Next = Rig.Drive(400.0 + LapCm * 2.0, LapSpecStepsPerLap);
		TestEqual(TEXT("The lap after the reset counts normally"), Next.LapsCounted, 1);
	}

	// -- The opposite ruleset: a reset is free --------------------------------
	{
		FLapSpecRig Rig;
		if (!Rig.Build(*this, /*bResetInvalidatesLap*/ false))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		Rig.StartRacing(-800.0);
		Rig.Drive(400.0, 8);
		Rig.Drive(LapCm * 0.25 + 4000.0, StepsFor(LapCm * 0.25, LapCm));

		const double ResetDistanceCm = LapCm * 0.25 - 2500.0;
		Rig.Tracker->NotifyVehicleReset(Rig.PositionAt(ResetDistanceCm), ResetDistanceCm);
		TestTrue(TEXT("With bResetInvalidatesLap false the lap survives a reset"),
			Rig.Tracker->GetCurrentLapInvalidity().IsClean());

		Rig.CurrentDistanceCm = ResetDistanceCm;
		const FLapDriveSummary Rest = Rig.Drive(400.0 + LapCm, StepsFor(LapCm * 0.75 + 2500.0, LapCm));
		TestEqual(TEXT("...and still counts when it closes"), Rest.LapsCounted, 1);

		// Sector splits are still a complete, consistent set: the boundary the car was
		// pushed back behind is NOT re-timed, which is what keeps the splits telescoping.
		if (Rest.ClosedLaps.Num() == 1)
		{
			TestEqual(TEXT("...with all three splits"), Rest.ClosedLaps[0].SectorDurationsSeconds.Num(), 3);
			TestTrue(TEXT("...that add up to the lap"), Rest.ClosedLaps[0].AreSectorsConsistent());
		}
	}

	// -- A reset must not let the next step SWEEP through gates ---------------
	{
		FLapSpecRig Rig;
		if (!Rig.Build(*this, /*bResetInvalidatesLap*/ false))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		Rig.StartRacing(-800.0);
		Rig.Drive(400.0, 8);

		// Announce a reset from just past the line to three quarters of the way round.
		// Without the re-seed, the following step would test a segment spanning most of
		// the circuit and hand out gate crossings for track nobody drove.
		const double FarDistanceCm = LapCm * 0.75 + 1000.0;
		Rig.Tracker->NotifyVehicleReset(Rig.PositionAt(FarDistanceCm), FarDistanceCm);
		Rig.CurrentDistanceCm = FarDistanceCm;

		const FLapDriveSummary AfterReset = Rig.Drive(FarDistanceCm + 500.0, 4);
		TestEqual(TEXT("The step after an announced reset crosses no gate"), AfterReset.GatesAdvanced, 0);
		TestEqual(TEXT("...and closes no lap"), AfterReset.LapsClosed, 0);
		TestFalse(TEXT("...leaving gate 1 unsatisfied, because the car never drove through it"),
			Rig.Tracker->IsGateSatisfied(1));
	}

	// -- An UNANNOUNCED jump is caught by the plausibility bound --------------
	{
		FLapSpecRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		Rig.StartRacing(-800.0);
		Rig.Drive(400.0, 8);

		// A control first: a long but drivable step is NOT a teleport, so the assertion
		// below is the guard firing rather than the guard being permanently on.
		const FRaceLapTrackerUpdate Normal = Rig.Step(1400.0);
		TestFalse(TEXT("A long but drivable step is not treated as a teleport"), Normal.bTeleportDetected);

		// Now most of the way round the circuit in one step. NOTE the metric this fires
		// on: the ARC jump is ~0.39 of a lap, well over the quarter-lap bound, while the
		// straight-line distance is only ~19 km-cm -- under a third of the lap and under
		// the chord bound. On a circular track the chord test alone can never fire, which
		// is exactly how the first version of this guard passed its own review and failed
		// this test.
		const FRaceLapTrackerUpdate Jump = Rig.Step(LapCm * 0.4);
		TestTrue(TEXT("An implausible step is reported as a teleport"), Jump.bTeleportDetected);
		TestEqual(TEXT("...crossing no gate"), Jump.GatesAdvanced, 0);
		TestEqual(TEXT("...and voiding the lap"),
			Rig.Tracker->GetCurrentLapInvalidity().Reason, ERaceLapInvalidReason::VehicleReset);
		TestFalse(TEXT("...without crediting gates 1 or 2"),
			Rig.Tracker->IsGateSatisfied(1) || Rig.Tracker->IsGateSatisfied(2));
	}

	// -- A non-finite reset pose drops progress rather than trusting it -------
	{
		FLapSpecRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		Rig.StartRacing(-800.0);
		Rig.Drive(400.0, 8);

		Rig.Tracker->NotifyVehicleReset(
			FVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0), 1000.0);
		TestEqual(TEXT("A non-finite reset pose voids the lap"),
			Rig.Tracker->GetCurrentLapInvalidity().Reason, ERaceLapInvalidReason::VehicleReset);

		// The next step re-seeds instead of evaluating a segment across the discontinuity.
		const FRaceLapTrackerUpdate Next = Rig.Step(Rig.LapLengthCm * 0.5);
		TestFalse(TEXT("...and the next step re-seeds rather than evaluating a bogus segment"),
			Next.bEvaluated);
	}

	// -- Restart clears everything, and is idempotent -------------------------
	{
		FLapSpecRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		Rig.StartRacing(-800.0);
		Rig.Drive(400.0, 8);
		Rig.Drive(LapCm * 0.5 + 1000.0, StepsFor(LapCm * 0.5, LapCm));

		TestTrue(TEXT("Mid-race, gates have been taken"), Rig.Tracker->IsGateSatisfied(1));
		TestEqual(TEXT("...and a lap is in progress"), Rig.Tracker->GetCurrentLapNumber(), 1);

		// The session restarts. The tracker is told; it would also have noticed via the
		// session id, which the second half of this block checks.
		Rig.Machine->Restart();
		Rig.Tracker->ResetForNewSession();
		Rig.Tracker->ResetForNewSession();   // idempotent

		TestEqual(TEXT("Restart clears the lap counter"), Rig.Tracker->GetCurrentLapNumber(), 0);
		TestEqual(TEXT("...the valid lap counter"), Rig.Tracker->GetValidLapsCompleted(), 0);
		TestEqual(TEXT("...the completed lap counter"), Rig.Tracker->GetLapsCompleted(), 0);
		TestEqual(TEXT("...the expected gate"), Rig.Tracker->GetExpectedGateIndex(), 0);
		TestFalse(TEXT("...every gate-crossed flag"),
			Rig.Tracker->IsGateSatisfied(0) || Rig.Tracker->IsGateSatisfied(1)
				|| Rig.Tracker->IsGateSatisfied(2) || Rig.Tracker->IsGateSatisfied(3));
		TestFalse(TEXT("...the lap-in-progress flag"), Rig.Tracker->IsLapInProgress());
		TestTrue(TEXT("...the recorded invalidity"), Rig.Tracker->GetCurrentLapInvalidity().IsClean());
		TestEqual(TEXT("...the last completed lap"), Rig.Tracker->GetLastCompletedLap().LapNumber, 0);
		TestEqual(TEXT("...the best lap"), Rig.Tracker->GetBestValidLap().LapNumber, 0);
		TestEqual(TEXT("...and the run validity"), Rig.Tracker->GetRunValidity(), ERacingRunValidity::Pending);
		TestTrue(TEXT("The track snapshot survives a restart: it is the same circuit"),
			Rig.Tracker->IsConfigured());

		// A whole clean lap in the new session behaves exactly like the first one did.
		Rig.StartRacing(-800.0);
		Rig.Drive(400.0, 8);
		const FLapDriveSummary NewSessionLap = Rig.Drive(400.0 + LapCm, LapSpecStepsPerLap);
		TestEqual(TEXT("The restarted session counts its first lap exactly once"), NewSessionLap.LapsCounted, 1);
		if (NewSessionLap.ClosedLaps.Num() == 1)
		{
			TestEqual(TEXT("...numbered from 1 again"), NewSessionLap.ClosedLaps[0].LapNumber, 1);
			TestTrue(TEXT("...with consistent sectors"), NewSessionLap.ClosedLaps[0].AreSectorsConsistent());
		}
	}

	// -- The session-id watchdog, for an owner that forgot to tell us ---------
	{
		FLapSpecRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		Rig.StartRacing(-800.0);
		Rig.Drive(400.0, 8);
		Rig.Drive(LapCm * 0.5, StepsFor(LapCm * 0.5, LapCm));
		TestTrue(TEXT("Gates have been taken before the restart"), Rig.Tracker->IsGateSatisfied(2));

		// Restart WITHOUT calling ResetForNewSession. The next Advance must notice.
		Rig.Machine->Restart();
		Rig.Machine->BeginCountdown();
		GLapSpecNowSeconds += 3.0;
		Rig.Machine->StartRace();

		Rig.Step(LapCm * 0.5 + 200.0);
		TestFalse(TEXT("An unannounced restart is caught by the session id, clearing gate state"),
			Rig.Tracker->IsGateSatisfied(2));
		TestEqual(TEXT("...and the lap counter"), Rig.Tracker->GetCurrentLapNumber(), 0);
	}

	return true;
}

// ===========================================================================
// 5. RACE-001 M4: a clock that refuses to start invalidates the run
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceLapClockFaultTest,
	"RacingSim.Race.LapClockFault",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceLapClockFaultTest::RunTest(const FString& Parameters)
{
	using namespace RaceLapSpecPrivate;

	// Every one of these is the fault being reported correctly, which is the point of
	// the test. Declared so the report shows a green suite rather than an error.
	AddExpectedMessage(TEXT("rejected a non-finite timestamp"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/-1, /*IsRegex=*/false);
	AddExpectedMessage(TEXT("race clock refused to start"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/-1, /*IsRegex=*/false);
	AddExpectedMessage(TEXT("race clock was not running at the finish"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/-1, /*IsRegex=*/false);
	AddExpectedMessage(TEXT("authoritative race clock reported a fault"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/-1, /*IsRegex=*/false);

	FLapSpecRig Rig;
	if (!Rig.Build(*this, /*bResetInvalidatesLap*/ true, /*bBrokenClock*/ true))
	{
		return false;
	}

	const double LapCm = Rig.LapLengthCm;

	// A control first: the fault latch starts clear, so the assertions below are about
	// the refusal and not about a flag that was always set.
	TestFalse(TEXT("A fresh session reports no clock fault"), Rig.Machine->HasRaceClockFault());

	Rig.StartRacing(-800.0);

	// RACE-001 M4. Before this ticket, CommitTransition discarded FRaceClock::Start()'s
	// return: the machine entered Racing on a refused clock and later froze a silent
	// 0.000 with nothing marking the run.
	TestEqual(TEXT("The machine still entered Racing"), Rig.Machine->GetRaceState(), ERaceState::Racing);
	TestTrue(TEXT("...but the refused clock start is latched as a fault"), Rig.Machine->HasRaceClockFault());
	TestFalse(TEXT("...and the race clock is not running"), Rig.Machine->IsRaceClockRunning());

	Rig.Drive(400.0, 8);
	TestEqual(TEXT("The tracker marks the RUN invalid the moment it sees the fault"),
		Rig.Tracker->GetRunValidity(), ERacingRunValidity::InvalidIncomplete);

	// A LAP THAT IS PERFECT IN EVERY OTHER RESPECT still cannot be published.
	const FLapDriveSummary Lap = Rig.Drive(400.0 + LapCm, LapSpecStepsPerLap);
	TestEqual(TEXT("The lap closes"), Lap.LapsClosed, 1);
	TestEqual(TEXT("...and is NOT counted, despite every gate being taken in order"), Lap.LapsCounted, 0);
	TestEqual(TEXT("...so no valid lap exists"), Rig.Tracker->GetValidLapsCompleted(), 0);

	if (Lap.ClosedLaps.Num() == 1)
	{
		const FRacingLapTiming& Closed = Lap.ClosedLaps[0];
		TestEqual(TEXT("The lap is reported as incomplete, not as a fast one"),
			Closed.Validity, ERacingRunValidity::InvalidIncomplete);
		TestEqual(TEXT("...with the zero duration a broken clock produces"), Closed.LapDurationSeconds, 0.0);
		TestFalse(TEXT("...and IsComplete() is false, so nothing downstream treats it as a result"),
			Closed.IsComplete());
	}

	TestEqual(TEXT("The best-lap slot never took the 0.000"), Rig.Tracker->GetBestValidLap().LapNumber, 0);

	// Finishing on a clock that never ran latches the fault from the other side too.
	Rig.Machine->FinishRace();
	TestTrue(TEXT("Finishing a run whose clock never started keeps the fault latched"),
		Rig.Machine->HasRaceClockFault());

	// A restart clears the latch: the fault is per-session, like the clocks.
	Rig.Machine->Restart();
	TestFalse(TEXT("Restart clears the clock fault"), Rig.Machine->HasRaceClockFault());

	return true;
}

// ===========================================================================
// 6. A SPIN ON THE LINE. RACE-002 repair cycle 1, review finding H1/H2.
// ===========================================================================
//
// Docs/03-TrackRaceUI.md:46 names "spins on the line" as a required test case, and
// `.claude/rules/race-tests.md` requires "spins at gates" generally -- the start/finish
// line IS a gate. Section 3 above covers a spin at an ORDINARY gate and a single
// reverse/forward oscillation at the line; neither one caught this.
//
// WHAT WENT WRONG AND WHY THE OLD COVERAGE MISSED IT. Every forward crossing of the line
// unconditionally closed the lap in progress and opened a new one, so an oscillation
// F,R,F,R,F,R,F -- one spin, net zero real progress, and the exact stream TRACK-002's own
// crossing-direction case nets as ONE forward pass -- produced FOUR lap boundaries: three
// closed laps, CurrentLapNumber +4, a ~0.03 s LastCompletedLap and a ranking key
// (Docs/03-TrackRaceUI.md:43, lap * trackLength + splineDistance) three laps ahead of a
// car that had not moved. ValidLapsCompleted stayed 0 the whole time -- the phantom laps
// carried ReverseFinishCrossing -- which is exactly why a suite that only ever asserted
// the VALID count could sit green over it. This suite asserts the boundary count, the lap
// NUMBER, the total (valid-or-not) lap count and the ranking key, because those are the
// four numbers the bug moved.
//
// The control case at the end is not decoration: a fix that suppressed real laps as well
// as phantom ones would pass every assertion above it.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceLapLineSpinTest,
	"RacingSim.Race.LapLineSpin",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceLapLineSpinTest::RunTest(const FString& Parameters)
{
	using namespace RaceLapSpecPrivate;

	// -- A multi-oscillation spin ACROSS the start/finish line ----------------
	{
		FLapSpecRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		Rig.StartRacing(-800.0);

		// Seven crossings of the line, alternating, ending where it began: forward, back,
		// forward, back, forward, back, forward. Four forward passes and three reverse ones,
		// no two consecutive crossings sharing a direction -- TRACK-002's GateCurvedTrack
		// spin case nets that stream at EXACTLY ONE forward pass, and this layer is required
		// by acceptance criterion 6 not to derive a different net from the same stream.
		const double SpinPositionsCm[] = { 400.0, -300.0, 500.0, -200.0, 600.0, -400.0, 700.0 };

		FLapDriveSummary Spin;
		for (const double PositionCm : SpinPositionsCm)
		{
			Spin.Accumulate(Rig.Step(PositionCm));
		}

		// -- The four numbers the bug moved ----------------------------------
		TestEqual(TEXT("A spin on the line closes NO lap: three of the four forward crossings were not lap boundaries"),
			Spin.LapsClosed, 0);
		TestEqual(TEXT("...counts none"), Spin.LapsCounted, 0);
		TestEqual(TEXT("...and opens exactly one, the first crossing, which started the car's lap 1"),
			Spin.LapsOpened, 1);
		TestEqual(TEXT("The car is on lap 1, not lap 4: the lap number cannot outrun real progress"),
			Rig.Tracker->GetCurrentLapNumber(), 1);
		TestEqual(TEXT("No lap has been completed, valid or otherwise"),
			Rig.Tracker->GetLapsCompleted(), 0);
		TestEqual(TEXT("...and none validly"), Rig.Tracker->GetValidLapsCompleted(), 0);
		TestEqual(TEXT("Nothing reached the last-completed-lap slot, so no 0.03-second 'lap' can reach a HUD"),
			Rig.Tracker->GetLastCompletedLap().LapNumber, 0);
		TestEqual(TEXT("...nor the best-lap slot"), Rig.Tracker->GetBestValidLap().LapNumber, 0);

		// -- The net, against TRACK-002's own crossing-stream semantics -------
		TestEqual(TEXT("Seven alternating crossings of the line net exactly one forward advance"),
			Spin.GatesAdvanced, 1);
		TestTrue(TEXT("...leaving the start/finish gate satisfied"), Rig.Tracker->IsGateSatisfied(0));
		TestEqual(TEXT("...and gate 1 expected"), Rig.Tracker->GetExpectedGateIndex(), 1);
		TestFalse(TEXT("...with no ordered gate beyond the line credited"),
			Rig.Tracker->IsGateSatisfied(1) || Rig.Tracker->IsGateSatisfied(2) || Rig.Tracker->IsGateSatisfied(3));

		// -- The documented ranking key, which is where this reaches gameplay --
		//
		// Docs/03-TrackRaceUI.md:43: progress is ordered by lap * trackLength +
		// splineDistance. With the phantom laps the car sat a full three laps up the order
		// having driven 15 metres.
		const double RankingKeyCm =
			static_cast<double>(Rig.Tracker->GetCurrentLapNumber()) * LapCm + Rig.Tracker->GetProgressDistanceCm();
		TestEqual(TEXT("The ranking key reflects one lap plus 700 cm, not four laps plus 700 cm"),
			RankingKeyCm, LapCm + 700.0, 1.0);

		// -- The spin is still reported as what it was ------------------------
		//
		// Reclassifying the forward crossings does NOT withdraw the reverse-crossing
		// invalidity: CLAUDE.md names reverse finish crossings on their own, and section 3
		// asserts the reason stays distinct from a skipped gate.
		TestEqual(TEXT("The lap is still voided by the reverse crossings of the line"),
			Rig.Tracker->GetCurrentLapInvalidity().Reason, ERaceLapInvalidReason::ReverseFinishCrossing);
		TestEqual(TEXT("...naming the start/finish gate"), Rig.Tracker->GetCurrentLapInvalidity().GateIndex, 0);

		// -- And the lap the car is ACTUALLY on still closes, exactly once -----
		const FLapDriveSummary Rest = Rig.Drive(LapCm + 400.0, LapSpecStepsPerLap);
		TestEqual(TEXT("Driving the rest of the lap closes lap 1 exactly once"), Rest.LapsClosed, 1);
		TestEqual(TEXT("...uncounted, because of the reverse crossings"), Rest.LapsCounted, 0);
		TestEqual(TEXT("...and opens exactly one new lap"), Rest.LapsOpened, 1);
		TestEqual(TEXT("The session recorded exactly ONE lap boundary for the one lap driven"),
			Rig.Tracker->GetLapsCompleted(), 1);
		TestEqual(TEXT("...putting the car on lap 2"), Rig.Tracker->GetCurrentLapNumber(), 2);

		if (Rest.ClosedLaps.Num() == 1)
		{
			const FRacingLapTiming& Closed = Rest.ClosedLaps[0];
			TestEqual(TEXT("The closed lap is lap 1"), Closed.LapNumber, 1);
			TestEqual(TEXT("...invalid as a reverse crossing"), Closed.Validity, ERacingRunValidity::InvalidReverseCrossing);

			// THE PHANTOM-LAP SIGNATURE, asserted directly: the bug's closed laps were the
			// duration of a single evaluation step. A real lap here is ~407 steps.
			TestTrue(TEXT("...with a real lap's duration, not a single step's"),
				Closed.LapDurationSeconds > LapSpecStepsPerLap * LapSpecSecondsPerStep * 0.9);
			TestTrue(TEXT("...and no more than the spin plus the lap"),
				Closed.LapDurationSeconds < (LapSpecStepsPerLap + 20) * LapSpecSecondsPerStep);
		}
	}

	// -- THE CONTROL: a genuine lap still closes and counts exactly once -------
	//
	// The whole risk of the H1 fix is that it suppresses real lap boundaries along with
	// phantom ones. This is the same drive as RacingSim.Race.LapCleanLap's first lap,
	// restated here so the suppression case and the non-suppression case fail together or
	// not at all -- a fix that broke real laps would otherwise leave this suite green.
	{
		FLapSpecRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		const double LapCm = Rig.LapLengthCm;
		Rig.StartRacing(-800.0);

		const FLapDriveSummary Opening = Rig.Drive(400.0, 8);
		TestEqual(TEXT("Control: the grid crossing opens lap 1"), Opening.LapsOpened, 1);
		TestEqual(TEXT("...and closes nothing"), Opening.LapsClosed, 0);

		// Real forward progress all the way round, one clean finish crossing.
		const FLapDriveSummary Lap = Rig.Drive(400.0 + LapCm, LapSpecStepsPerLap);
		TestEqual(TEXT("Control: a genuine lap still CLOSES exactly once"), Lap.LapsClosed, 1);
		TestEqual(TEXT("...still COUNTS exactly once"), Lap.LapsCounted, 1);
		TestEqual(TEXT("...and still opens the next lap exactly once"), Lap.LapsOpened, 1);
		TestEqual(TEXT("Control: the valid lap count is 1"), Rig.Tracker->GetValidLapsCompleted(), 1);
		TestEqual(TEXT("Control: the total lap count is 1"), Rig.Tracker->GetLapsCompleted(), 1);
		TestEqual(TEXT("Control: the car is on lap 2"), Rig.Tracker->GetCurrentLapNumber(), 2);
		TestEqual(TEXT("Control: four ordered gates were satisfied across the lap"), Lap.GatesAdvanced, 4);

		if (Lap.ClosedLaps.Num() == 1)
		{
			TestEqual(TEXT("Control: the closed lap is Valid"),
				Lap.ClosedLaps[0].Validity, ERacingRunValidity::Valid);
			TestTrue(TEXT("...with consistent sectors"), Lap.ClosedLaps[0].AreSectorsConsistent());
		}

		// A SECOND genuine lap: the boundary rule must not be a one-shot that only the
		// first lap of a session can satisfy.
		const FLapDriveSummary Second = Rig.Drive(400.0 + LapCm * 2.0, LapSpecStepsPerLap);
		TestEqual(TEXT("Control: a second genuine lap counts exactly once too"), Second.LapsCounted, 1);
		TestEqual(TEXT("...taking the valid lap count to 2"), Rig.Tracker->GetValidLapsCompleted(), 2);
	}

	// -- The U-TURN case: a forward line crossing with NO reverse crossing -----
	//
	// The related defect the same review noted. The car crosses the line, then goes back
	// behind it round the OUTSIDE of the gate rectangle -- TRACK-002 reports that as
	// OutsideExtent, a near miss, which is not a crossing at all -- and then crosses the
	// line forwards a second time. There is no reverse crossing anywhere in that stream, so
	// the reverse-crossing handling could never have caught it; only the
	// "did this lap get anywhere" test does.
	{
		FLapSpecRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		Rig.StartRacing(-800.0);
		Rig.Drive(400.0, 8);
		TestEqual(TEXT("U-turn: lap 1 is open"), Rig.Tracker->GetCurrentLapNumber(), 1);

		// Back behind the line, wide of the gate.
		const FLapDriveSummary Wide = Rig.Drive(-600.0, 8,
			/*WideFrom*/ -900.0, /*WideTo*/ 900.0, LapSpecGateHalfWidthCm + 600.0);
		TestTrue(TEXT("Going back round the OUTSIDE of the line registers as a near miss"),
			Wide.NearMisses > 0);
		TestEqual(TEXT("...and not as a crossing, so no reverse-crossing fault is recorded"),
			Rig.Tracker->GetCurrentLapInvalidity().Reason, ERaceLapInvalidReason::None);

		// Forwards over the line again, on the centerline this time. The target is 700 rather
		// than 600 so that no interpolated step lands exactly ON the gate plane -- this case
		// is about the lap-boundary rule, not about re-litigating TRACK-002's half-open
		// sign convention at zero.
		const FLapDriveSummary Back = Rig.Drive(700.0, 8);
		TestEqual(TEXT("U-turn: returning to the line closes no lap -- no ordered gate was ever taken"),
			Back.LapsClosed, 0);
		TestEqual(TEXT("...counts none"), Back.LapsCounted, 0);
		TestEqual(TEXT("...opens none"), Back.LapsOpened, 0);
		TestEqual(TEXT("...and leaves the car on lap 1"), Rig.Tracker->GetCurrentLapNumber(), 1);
		TestEqual(TEXT("...with no lap recorded"), Rig.Tracker->GetLapsCompleted(), 0);
	}

	return true;
}
