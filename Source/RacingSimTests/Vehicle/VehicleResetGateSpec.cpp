// Copyright RacingSim. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Vehicle/RacingVehiclePawn.h"
#include "Vehicle/VehicleFailureDetection.h"
#include "Vehicle/VehicleResetMath.h"
#include "Vehicle/VehicleTelemetryTypes.h"

#include <limits>

/**
 * RACE-006: the vehicle-side driver reset gate and the reset cooldown VEH-007 routed to
 * this ticket ("cooldown longer than MaxContactSuppressionSeconds plus the floor's
 * evaluations at the active capture rate, pinned by a test").
 *
 * Both suites are pure -- the gate and the detector need no actor and no UWorld -- so
 * they run at the Smoke gate. The storm suite drives the real detector
 * (RacingSim::Vehicle::EvaluateVehicleFailures) with the real gate
 * (RacingSim::Vehicle::EvaluateResetGate), reproducing the pawn's capture pacing and its
 * reset bookkeeping, because the property being pinned is a property of those two
 * together: a driver holding reset cannot walk the detector's carried evaluation count
 * to its ceiling.
 */

namespace RacingSimResetGateSpecPrivate
{
	/** The detector's evaluation ceiling. Pinned by hand for the reason FailureDetectionSuppressionBound gives: it is not visible outside the detector. */
	constexpr int32 ResetStormCeilingEvaluations = 240;

	/**
	 * A stationary car with all four wheels in contact just under the body. Used before
	 * and after every reset: the reset "moves" the car by zero, the shortest reset there
	 * is, so every later contact matches the pre-reset basis and only the time budget,
	 * the floor or the ceiling can end suppression. That is the worst case for the
	 * ceiling, which is why the storm uses it.
	 */
	FVehicleTelemetrySnapshot MakeResetStormSnapshot(const double SimTimeSeconds, const int64 CaptureIndex, const float FrameDeltaSeconds)
	{
		FVehicleTelemetrySnapshot Snapshot;
		Snapshot.bIsValid = true;
		Snapshot.TimestampSeconds = SimTimeSeconds;
		Snapshot.SimulationTimeSeconds = SimTimeSeconds;
		Snapshot.CaptureIndex = CaptureIndex;
		Snapshot.FrameDeltaSeconds = FrameDeltaSeconds;
		Snapshot.LocationCm = FVector(1000.0, 2000.0, 60.0);
		Snapshot.EngineRpm = 900.0f;
		Snapshot.GearIndex = 1;
		Snapshot.TargetGearIndex = 1;

		Snapshot.NumWheels = MaxVehicleTelemetryWheels;
		for (int32 WheelIndex = 0; WheelIndex < MaxVehicleTelemetryWheels; ++WheelIndex)
		{
			FVehicleWheelTelemetry& Wheel = Snapshot.Wheels[WheelIndex];
			Wheel.bInContact = true;
			Wheel.NormalisedSuspensionLength = 0.5f;
			Wheel.SpringForceN = 3500.0f;
			Wheel.ContactPointCm = Snapshot.LocationCm + FVector(0.0, 0.0, -40.0);
			Wheel.bHasContactMaterial = true;
		}

		return Snapshot;
	}

	struct FResetStormResult
	{
		int32 Resets = 0;
		int32 Captures = 0;
		/** Resets executed while the previous reset's basis was still armed. */
		int32 ResetsWhileArmed = 0;
		/** The largest carried evaluation count any evaluation saw (the value it was compared with the ceiling at). */
		int32 MaxCarriedEvaluations = 0;
		bool bCeilingReached = false;
		bool bRaisedInvalidContact = false;
		bool bRaisedAnyFailure = false;
	};

	/**
	 * A driver holding reset for DurationSeconds of simulated time: a request on every
	 * frame, admitted by EvaluateResetGate, each admitted reset announced to the
	 * detector exactly as ARacingVehiclePawn::ExecuteSafeReset does (NotifyDiscontinuity
	 * with the last captured location).
	 *
	 * Frame order matches the runtime: the pawn ticks (simulated clock, paced capture
	 * re-based on now, evaluation), then the controller services the request.
	 *
	 * @param FrameSeconds  the frame's DeltaSeconds, given the frame index and how many
	 *                      frames have run since the last executed reset (0 for the
	 *                      first frame after it) -- so a case can put a hitch exactly
	 *                      where it hurts most.
	 * @param bUseArmedGate false feeds the gate bContactSuppressionArmed = false, which
	 *                      isolates the time cooldown.
	 */
	FResetStormResult RunResetStorm(
		const FVehicleFailureThresholds& Thresholds,
		const float CaptureRateHz,
		const double CooldownSeconds,
		const bool bUseArmedGate,
		const double DurationSeconds,
		TFunctionRef<double(int32 FrameIndex, int32 FramesSinceReset)> FrameSeconds)
	{
		FResetStormResult Result;
		FVehicleFailureDetectorState State;
		FVehicleTelemetrySnapshot LastSnapshot;

		double SimSeconds = 0.0;
		double NextCaptureSeconds = 0.0;
		const double CaptureIntervalSeconds = 1.0 / static_cast<double>(CaptureRateHz);
		bool bHasReset = false;
		double LastResetSeconds = 0.0;
		int32 FramesSinceReset = 1000000;
		int64 CaptureIndex = 0;

		constexpr int32 MaxFrames = 1000000;
		for (int32 FrameIndex = 0; FrameIndex < MaxFrames && SimSeconds < DurationSeconds; ++FrameIndex)
		{
			const double DeltaSeconds = FrameSeconds(FrameIndex, FramesSinceReset);
			SimSeconds += DeltaSeconds;

			// Pawn: paced capture and evaluation.
			if (SimSeconds >= NextCaptureSeconds)
			{
				NextCaptureSeconds = SimSeconds + CaptureIntervalSeconds;
				++CaptureIndex;
				++Result.Captures;

				const FVehicleTelemetrySnapshot Current =
					MakeResetStormSnapshot(SimSeconds, CaptureIndex, static_cast<float>(DeltaSeconds));

				if (State.bHasPreDiscontinuityLocation)
				{
					// The count this evaluation compares against the ceiling.
					const int32 ComparedCount = State.PreDiscontinuityEvaluations + 1;
					Result.MaxCarriedEvaluations = FMath::Max(Result.MaxCarriedEvaluations, ComparedCount);
					Result.bCeilingReached |= ComparedCount >= ResetStormCeilingEvaluations;
				}

				const FVehicleFailureReport Report =
					RacingSim::Vehicle::EvaluateVehicleFailures(LastSnapshot, Current, Thresholds, State);
				Result.bRaisedInvalidContact |= Report.Has(EVehicleFailureFlag::InvalidContact);
				Result.bRaisedAnyFailure |= Report.HasAnyFailure();
				LastSnapshot = Current;
			}

			// Controller: the driver is holding reset, so there is a request every frame.
			RacingSim::Vehicle::FVehicleResetGateInput GateInput;
			GateInput.bHasPreviousReset = bHasReset;
			GateInput.SimulationTimeSeconds = SimSeconds;
			GateInput.LastResetSimulationTimeSeconds = LastResetSeconds;
			GateInput.CooldownSeconds = CooldownSeconds;
			GateInput.bCaptureEnabled = true;
			GateInput.bContactSuppressionArmed = bUseArmedGate && State.bHasPreDiscontinuityLocation;

			if (LastSnapshot.bIsValid
				&& RacingSim::Vehicle::EvaluateResetGate(GateInput) == RacingSim::Vehicle::EVehicleResetGateResult::Accepted)
			{
				Result.ResetsWhileArmed += State.bHasPreDiscontinuityLocation ? 1 : 0;
				State.NotifyDiscontinuity(LastSnapshot.LocationCm);
				bHasReset = true;
				LastResetSeconds = SimSeconds;
				++Result.Resets;
				FramesSinceReset = 0;
			}
			else
			{
				++FramesSinceReset;
			}
		}

		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleResetGateTest,
	"RacingSim.Vehicle.ResetGate",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleResetGateTest::RunTest(const FString& Parameters)
{
	using namespace RacingSim::Vehicle;

	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const float NaNf = std::numeric_limits<float>::quiet_NaN();

	// -- The floor the minimum is built from. --
	TestEqual(TEXT("The detector's evaluation floor is 3 (the minimum below assumes Floor + 1 = 4 captures)"),
		GetMinContactSuppressionEvaluations(), 3);

	// -- ComputeMinimumResetCooldownSeconds, pinned by value. --
	TestNearlyEqual(TEXT("Minimum at the defaults is 0.5 + 4/60 s"),
		ComputeMinimumResetCooldownSeconds(0.5f, 60.0f), 0.5 + 4.0 / 60.0, 1e-6);
	TestNearlyEqual(TEXT("Minimum at 120 Hz is 0.5 + 4/120 s"),
		ComputeMinimumResetCooldownSeconds(0.5f, 120.0f), 0.5 + 4.0 / 120.0, 1e-6);
	TestNearlyEqual(TEXT("Minimum with a 2 s budget at 30 Hz is 2 + 4/30 s"),
		ComputeMinimumResetCooldownSeconds(2.0f, 30.0f), 2.0 + 4.0 / 30.0, 1e-6);
	TestNearlyEqual(TEXT("Capture disabled (rate 0) returns the budget alone"),
		ComputeMinimumResetCooldownSeconds(0.5f, 0.0f), 0.5, 1e-6);
	TestNearlyEqual(TEXT("A negative rate reads as capture disabled"),
		ComputeMinimumResetCooldownSeconds(0.5f, -60.0f), 0.5, 1e-6);
	TestNearlyEqual(TEXT("A non-finite rate reads as capture disabled"),
		ComputeMinimumResetCooldownSeconds(0.5f, NaNf), 0.5, 1e-6);
	TestNearlyEqual(TEXT("A non-finite budget reads as 0"),
		ComputeMinimumResetCooldownSeconds(NaNf, 60.0f), 4.0 / 60.0, 1e-6);
	TestTrue(TEXT("The minimum strictly exceeds the budget whenever capture is on"),
		ComputeMinimumResetCooldownSeconds(0.5f, 60.0f) > 0.5);

	// -- ResolveEffectiveResetCooldownSeconds. --
	const double DefaultMinimum = ComputeMinimumResetCooldownSeconds(0.5f, 60.0f);
	TestNearlyEqual(TEXT("An authored cooldown above the minimum is kept"),
		ResolveEffectiveResetCooldownSeconds(2.0f, 0.5f, 60.0f), 2.0, 1e-6);
	TestNearlyEqual(TEXT("An authored cooldown below the minimum is raised to it"),
		ResolveEffectiveResetCooldownSeconds(0.1f, 0.5f, 60.0f), DefaultMinimum, 1e-6);
	TestNearlyEqual(TEXT("A zero cooldown is raised to the minimum"),
		ResolveEffectiveResetCooldownSeconds(0.0f, 0.5f, 60.0f), DefaultMinimum, 1e-6);
	TestNearlyEqual(TEXT("A non-finite cooldown resolves to the minimum"),
		ResolveEffectiveResetCooldownSeconds(NaNf, 0.5f, 60.0f), DefaultMinimum, 1e-6);

	// -- The shipped default sits above the minimum. --
	const ARacingVehiclePawn* PawnDefaults = GetDefault<ARacingVehiclePawn>();
	if (TestNotNull(TEXT("ARacingVehiclePawn CDO"), PawnDefaults))
	{
		const FVehicleFailureThresholds DefaultThresholds;
		const double CdoMinimum = ComputeMinimumResetCooldownSeconds(
			DefaultThresholds.MaxContactSuppressionSeconds, PawnDefaults->TelemetrySampleRateHz);
		TestTrue(FString::Printf(TEXT("The pawn's default ResetCooldownSeconds (%f) is at or above the minimum (%f)"),
				PawnDefaults->ResetCooldownSeconds, CdoMinimum),
			static_cast<double>(PawnDefaults->ResetCooldownSeconds) >= CdoMinimum);
	}

	// -- EvaluateResetGate truth table. --
	FVehicleResetGateInput Base;
	Base.bHasPreviousReset = true;
	Base.SimulationTimeSeconds = 10.0;
	Base.LastResetSimulationTimeSeconds = 5.0;
	Base.CooldownSeconds = 1.0;
	Base.bCaptureEnabled = true;
	Base.bContactSuppressionArmed = false;

	TestEqual(TEXT("Past the cooldown, basis expired: Accepted"),
		EvaluateResetGate(Base), EVehicleResetGateResult::Accepted);

	{
		FVehicleResetGateInput Input = Base;
		Input.bHasPreviousReset = false;
		Input.SimulationTimeSeconds = 0.0;
		Input.LastResetSimulationTimeSeconds = 0.0;
		TestEqual(TEXT("The first reset is not subject to a cooldown"),
			EvaluateResetGate(Input), EVehicleResetGateResult::Accepted);
	}
	{
		FVehicleResetGateInput Input = Base;
		Input.SimulationTimeSeconds = 5.5;
		TestEqual(TEXT("Inside the cooldown: CoolingDown"),
			EvaluateResetGate(Input), EVehicleResetGateResult::CoolingDown);
	}
	{
		FVehicleResetGateInput Input = Base;
		Input.SimulationTimeSeconds = 6.0;
		TestEqual(TEXT("Exactly at the cooldown boundary: Accepted"),
			EvaluateResetGate(Input), EVehicleResetGateResult::Accepted);
	}
	{
		FVehicleResetGateInput Input = Base;
		Input.bContactSuppressionArmed = true;
		TestEqual(TEXT("Past the cooldown but the basis is armed: SuppressionArmed"),
			EvaluateResetGate(Input), EVehicleResetGateResult::SuppressionArmed);
	}
	{
		FVehicleResetGateInput Input = Base;
		Input.bContactSuppressionArmed = true;
		Input.bCaptureEnabled = false;
		TestEqual(TEXT("An armed flag is ignored while capture is disabled (the detector is not running)"),
			EvaluateResetGate(Input), EVehicleResetGateResult::Accepted);
	}
	{
		FVehicleResetGateInput Input = Base;
		Input.SimulationTimeSeconds = 5.5;
		Input.bContactSuppressionArmed = true;
		TestEqual(TEXT("Cooling down and armed: CoolingDown is reported first"),
			EvaluateResetGate(Input), EVehicleResetGateResult::CoolingDown);
	}
	{
		FVehicleResetGateInput Input = Base;
		Input.SimulationTimeSeconds = NaN;
		TestEqual(TEXT("A non-finite clock: ClockUnusable"),
			EvaluateResetGate(Input), EVehicleResetGateResult::ClockUnusable);
	}
	{
		FVehicleResetGateInput Input = Base;
		Input.CooldownSeconds = NaN;
		TestEqual(TEXT("A non-finite cooldown: ClockUnusable"),
			EvaluateResetGate(Input), EVehicleResetGateResult::ClockUnusable);
	}
	{
		FVehicleResetGateInput Input = Base;
		Input.LastResetSimulationTimeSeconds = NaN;
		TestEqual(TEXT("A non-finite last-reset time after a reset: ClockUnusable"),
			EvaluateResetGate(Input), EVehicleResetGateResult::ClockUnusable);
	}
	{
		FVehicleResetGateInput Input = Base;
		Input.bHasPreviousReset = false;
		Input.LastResetSimulationTimeSeconds = NaN;
		TestEqual(TEXT("The last-reset time is not read before the first reset"),
			EvaluateResetGate(Input), EVehicleResetGateResult::Accepted);
	}

	TestEqual(TEXT("LexResetGateResult names SuppressionArmed"),
		FString(LexResetGateResult(EVehicleResetGateResult::SuppressionArmed)), FString(TEXT("SuppressionArmed")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleResetStormTest,
	"RacingSim.Vehicle.ResetStormCannotReachCeiling",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleResetStormTest::RunTest(const FString& Parameters)
{
	using namespace RacingSim::Vehicle;
	using namespace RacingSimResetGateSpecPrivate;

	const FVehicleFailureThresholds Thresholds;
	constexpr double StormSeconds = 30.0;
	const double PawnCooldownSeconds = ResolveEffectiveResetCooldownSeconds(
		GetDefault<ARacingVehiclePawn>()->ResetCooldownSeconds,
		Thresholds.MaxContactSuppressionSeconds,
		GetDefault<ARacingVehiclePawn>()->TelemetrySampleRateHz);

	auto Steady60 = [](int32, int32) { return 1.0 / 60.0; };
	auto Steady144 = [](int32, int32) { return 1.0 / 144.0; };

	// Hitches placed where they hurt: the first frame after every reset is longer than
	// the whole suppression budget, and a smaller hitch lands every seventh frame.
	auto Hitchy60 = [](const int32 FrameIndex, const int32 FramesSinceReset)
	{
		if (FramesSinceReset == 0)
		{
			return 0.6;
		}
		return (FrameIndex % 7 == 0) ? 0.1 : 1.0 / 60.0;
	};

	auto ExpectBounded = [this](const TCHAR* Label, const FResetStormResult& Result, const int32 MinResets)
	{
		AddInfo(FString::Printf(TEXT("%s: resets=%d captures=%d maxCarried=%d whileArmed=%d"),
			Label, Result.Resets, Result.Captures, Result.MaxCarriedEvaluations, Result.ResetsWhileArmed));
		TestTrue(FString::Printf(TEXT("%s: the driver got at least %d resets (the gate is not simply refusing everything)"), Label, MinResets),
			Result.Resets >= MinResets);
		TestFalse(FString::Printf(TEXT("%s: the carried count never reaches the %d-evaluation ceiling"), Label, ResetStormCeilingEvaluations),
			Result.bCeilingReached);
		TestTrue(FString::Printf(TEXT("%s: carried count stays below the ceiling (max %d)"), Label, Result.MaxCarriedEvaluations),
			Result.MaxCarriedEvaluations < ResetStormCeilingEvaluations);
		TestEqual(FString::Printf(TEXT("%s: no reset re-arms a basis that is still armed"), Label),
			Result.ResetsWhileArmed, 0);
		TestFalse(FString::Printf(TEXT("%s: no evaluation raises InvalidContact"), Label),
			Result.bRaisedInvalidContact);
		TestFalse(FString::Printf(TEXT("%s: no evaluation raises any failure"), Label),
			Result.bRaisedAnyFailure);
	};

	// -- The full gate at the pawn's effective cooldown. --
	// 30 s at a ~1 s cooldown is ~28 resets; 20 leaves room for the post-reset expiry
	// wait without letting a gate that refuses everything pass.
	ExpectBounded(TEXT("Full gate, 60 Hz steady"),
		RunResetStorm(Thresholds, 60.0f, PawnCooldownSeconds, /*bUseArmedGate*/ true, StormSeconds, Steady60), 20);
	ExpectBounded(TEXT("Full gate, 120 Hz capture on 144 Hz frames"),
		RunResetStorm(Thresholds, 120.0f, PawnCooldownSeconds, true, StormSeconds, Steady144), 20);
	ExpectBounded(TEXT("Full gate, 60 Hz with hitches"),
		RunResetStorm(Thresholds, 60.0f, PawnCooldownSeconds, true, StormSeconds, Hitchy60), 15);

	// -- The time cooldown ALONE at exactly the minimum, steady capture. --
	// Pins the derivation in VehicleResetMath.h: under steady capture the minimum by
	// itself already lets every basis expire before the next reset.
	const double Minimum60 = ComputeMinimumResetCooldownSeconds(Thresholds.MaxContactSuppressionSeconds, 60.0f);
	const double Minimum120 = ComputeMinimumResetCooldownSeconds(Thresholds.MaxContactSuppressionSeconds, 120.0f);
	ExpectBounded(TEXT("Minimum cooldown only, 60 Hz steady"),
		RunResetStorm(Thresholds, 60.0f, Minimum60, /*bUseArmedGate*/ false, StormSeconds, Steady60), 40);
	ExpectBounded(TEXT("Minimum cooldown only, 120 Hz capture on 144 Hz frames"),
		RunResetStorm(Thresholds, 120.0f, Minimum120, false, StormSeconds, Steady144), 40);

	// -- Controls: each shows the test can fail. --
	{
		// A cooldown shorter than the budget with no armed check: every reset lands on a
		// still-armed basis and the carried count walks to the ceiling. This is the storm
		// VEH-007's ACCEPTED COST note describes.
		const FResetStormResult Result = RunResetStorm(
			Thresholds, 60.0f, /*CooldownSeconds*/ 20.0 / 60.0, false, StormSeconds, Steady60);
		AddInfo(FString::Printf(TEXT("Control, 20-capture cooldown ungated: resets=%d maxCarried=%d whileArmed=%d"),
			Result.Resets, Result.MaxCarriedEvaluations, Result.ResetsWhileArmed));
		TestTrue(TEXT("Control: a cooldown shorter than the budget, without the armed check, reaches the ceiling"),
			Result.bCeilingReached);
	}
	{
		// The time cooldown alone is NOT enough under hitches: the long first frame after
		// each reset passes the cooldown on the very capture that starts the budget, so
		// every reset re-arms an armed basis and adds one to the carried count. Run long
		// enough (240+ cycles) to reach the ceiling -- this is why the gate also refuses
		// while the basis is armed.
		const FResetStormResult Result = RunResetStorm(
			Thresholds, 60.0f, Minimum60, /*bUseArmedGate*/ false, /*DurationSeconds*/ 200.0, Hitchy60);
		AddInfo(FString::Printf(TEXT("Control, minimum cooldown ungated with hitches: resets=%d maxCarried=%d whileArmed=%d"),
			Result.Resets, Result.MaxCarriedEvaluations, Result.ResetsWhileArmed));
		TestTrue(TEXT("Control: under hitches the time cooldown alone re-arms armed bases"),
			Result.ResetsWhileArmed > 0);
		TestTrue(TEXT("Control: under hitches the time cooldown alone reaches the ceiling"),
			Result.bCeilingReached);
	}

	return true;
}
