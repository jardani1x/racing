// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingSimValidation.h"
#include "Misc/AutomationTest.h"
#include "Vehicle/VehicleFailureDetection.h"
#include "Vehicle/VehicleFailureThresholdsDataAsset.h"
#include "Vehicle/VehicleInputTypes.h"

#include <limits>

using namespace RacingSim::Validation;

/**
 * VEH-004: the failure detector and its thresholds asset.
 *
 * ---------------------------------------------------------------------------
 * Why no ARacingVehiclePawn and no live Chaos component appears in this file
 * ---------------------------------------------------------------------------
 *
 * The same harness limitation VEH-002's VehicleChassisSpec.cpp and VEH-003's
 * VehicleTuneSpec.cpp both record, derived from engine source in Docs/Environment.md:
 * FEngineLoop::PreInit runs every SmokeFilter test (LaunchEngineLoop.cpp:4376) BEFORE
 * UEngine::Init registers the typed-element types (UnrealEngine.cpp:2399), and
 * UActorComponent::PostInitProperties creates an editor element for every non-template
 * component (ActorComponent.cpp:588) -- which checkf's on an unregistered type and
 * kills the process with NO index.json at all. The gate then reports nothing rather
 * than a failure, which is strictly worse than a red test.
 *
 * This is precisely the constraint the whole VEH-004 design answers. The detector is a
 * free function over plain snapshots, so every branch below is reachable here with
 * synthetic data and no world, no actor and no physics solver. What is NOT covered is
 * ARacingVehiclePawn::CaptureAndEvaluateTelemetry -- the adapter that feeds it real
 * Chaos output. That is the same standing gap VEH-002 recorded for ApplyChassisAsset
 * and VEH-003 for ApplyTuneAsset; it is unchanged in size by this ticket and is left
 * named rather than quietly dropped. VEH-006 owns closing it with a driving test.
 *
 * ---------------------------------------------------------------------------
 * What "coverage" means here
 * ---------------------------------------------------------------------------
 *
 * Every flag must be shown to FIRE on the corrupt input AND to stay silent on the
 * clean one. A test that only proves the clean case passes would go green against a
 * detector whose body had been deleted, which is the failure mode that makes a
 * detector worse than nothing: green evidence for an unarmed guard.
 */

namespace
{
	/**
	 * A snapshot describing a car doing something completely ordinary: 100 km/h in a
	 * straight line, four wheels on the ground, suspension mid-travel.
	 *
	 * Named with a VEH-004-specific prefix. Anonymous-namespace helpers have internal
	 * linkage, so a name collision with another spec in this module links fine in a
	 * normal build and becomes a C2084 redefinition under Unity Build -- the exact bug
	 * VEH-003 shipped as HasIssueFor/HasTuneIssueFor. Existing names in this module
	 * include HasIssueFor, HasTuneIssueFor, SetCurveKeys, MakeCircle, BuildCircle and
	 * StateName; none of the four below collides.
	 */
	FVehicleTelemetrySnapshot MakeHealthyFailureSnapshot(const double TimestampSeconds, const int64 CaptureIndex)
	{
		FVehicleTelemetrySnapshot Snapshot;
		Snapshot.bIsValid = true;
		Snapshot.TimestampSeconds = TimestampSeconds;
		Snapshot.CaptureIndex = CaptureIndex;
		Snapshot.FrameDeltaSeconds = 1.0f / 60.0f;

		// 100 km/h == 2777.78 cm/s. Travelling along +X, which is forward in Unreal.
		Snapshot.VelocityCms = FVector(2777.78, 0.0, 0.0);
		Snapshot.ForwardSpeedCms = 2777.78f;
		Snapshot.LocationCm = FVector(TimestampSeconds * 2777.78, 0.0, 0.0);
		Snapshot.AngularVelocityDegreesPerSecond = FVector(0.0, 0.0, 5.0);
		Snapshot.EngineRpm = 4200.0f;
		Snapshot.GearIndex = 3;
		Snapshot.TargetGearIndex = 3;

		Snapshot.NumWheels = MaxVehicleTelemetryWheels;
		for (int32 WheelIndex = 0; WheelIndex < MaxVehicleTelemetryWheels; ++WheelIndex)
		{
			FVehicleWheelTelemetry& Wheel = Snapshot.Wheels[WheelIndex];
			Wheel.bInContact = true;
			Wheel.NormalisedSuspensionLength = 0.5f;
			Wheel.SuspensionOffsetCm = -2.0f;
			Wheel.SpringForceN = 3500.0f;
			Wheel.SlipAngleDegrees = 1.5f;
			Wheel.SlipMagnitudeCms = 10.0f;
			Wheel.DriveTorqueNm = 120.0f;
			Wheel.BrakeTorqueNm = 0.0f;
			// Contact just under the body, comfortably inside MaxContactDistanceCm.
			Wheel.ContactPointCm = Snapshot.LocationCm + FVector(0.0, 0.0, -40.0);
			Wheel.bHasContactMaterial = true;
		}

		return Snapshot;
	}

	/** Advance a healthy snapshot by one 60 Hz step, moving it the distance its own velocity explains. */
	FVehicleTelemetrySnapshot AdvanceHealthyFailureSnapshot(
		const FVehicleTelemetrySnapshot& Previous,
		const double StepSeconds)
	{
		FVehicleTelemetrySnapshot Next = Previous;
		Next.TimestampSeconds = Previous.TimestampSeconds + StepSeconds;
		Next.CaptureIndex = Previous.CaptureIndex + 1;
		Next.FrameDeltaSeconds = static_cast<float>(StepSeconds);
		Next.LocationCm = Previous.LocationCm + Previous.VelocityCms * StepSeconds;

		for (int32 WheelIndex = 0; WheelIndex < Next.NumWheels; ++WheelIndex)
		{
			Next.Wheels[WheelIndex].ContactPointCm = Next.LocationCm + FVector(0.0, 0.0, -40.0);
		}

		return Next;
	}

	/** Run one evaluation against default thresholds with a fresh detector state. */
	FVehicleFailureReport EvaluateFailurePair(
		const FVehicleTelemetrySnapshot& Previous,
		const FVehicleTelemetrySnapshot& Current)
	{
		FVehicleFailureDetectorState State;
		return RacingSim::Vehicle::EvaluateVehicleFailures(
			Previous, Current, FVehicleFailureThresholds(), State);
	}

	bool HasFailureThresholdIssueFor(const FRacingValidationResult& Result, const FName PropertyName)
	{
		return Result.Issues.ContainsByPredicate(
			[PropertyName](const FRacingValidationIssue& Issue) { return Issue.PropertyName == PropertyName; });
	}
}

// ---------------------------------------------------------------------------
// The clean path. If this is not silent, nothing below means anything.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleFailureCleanTest,
	"RacingSim.Vehicle.FailureDetectionCleanState",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleFailureCleanTest::RunTest(const FString& Parameters)
{
	const double Step = 1.0 / 60.0;

	FVehicleTelemetrySnapshot Previous = MakeHealthyFailureSnapshot(10.0, 1);
	FVehicleFailureDetectorState State;

	// Ten ordinary steps. Any of them reporting a fault means an envelope is set where
	// ordinary racing crosses it, which would make the detector useless in practice.
	for (int32 StepIndex = 0; StepIndex < 10; ++StepIndex)
	{
		const FVehicleTelemetrySnapshot Current = AdvanceHealthyFailureSnapshot(Previous, Step);

		const FVehicleFailureReport Report = RacingSim::Vehicle::EvaluateVehicleFailures(
			Previous, Current, FVehicleFailureThresholds(), State);

		TestFalse(
			FString::Printf(TEXT("Step %d of ordinary driving reports no failure (%s: %s)"),
				StepIndex,
				*RacingSim::Vehicle::DescribeVehicleFailureFlags(Report.Flags),
				*Report.Reason),
			Report.HasAnyFailure());

		TestTrue(TEXT("A clean report allocates no reason string"), Report.Reason.IsEmpty());

		Previous = Current;
	}

	// An INVALID current snapshot is "no sample was taken", not a fault. Reporting one
	// would make every frame before the movement component exists look like a crash.
	const FVehicleTelemetrySnapshot NoSample;
	TestFalse(TEXT("An invalid snapshot is not a failure"),
		EvaluateFailurePair(Previous, NoSample).HasAnyFailure());

	// A FIRST sample, with no valid previous, must not be judged against zeroes -- a
	// car legitimately starting at (30000, 0, 0) would otherwise read as a teleport
	// from the origin on its very first frame.
	const FVehicleTelemetrySnapshot FirstSample = MakeHealthyFailureSnapshot(500.0, 1);
	const FVehicleFailureReport FirstReport = EvaluateFailurePair(FVehicleTelemetrySnapshot(), FirstSample);
	TestFalse(TEXT("The first sample is not compared against a default-constructed previous"),
		FirstReport.HasAnyFailure());

	TestEqual(TEXT("DescribeVehicleFailureFlags names the empty mask"),
		RacingSim::Vehicle::DescribeVehicleFailureFlags(0), FString(TEXT("None")));

	return true;
}

// ---------------------------------------------------------------------------
// NaN and infinity. The Gate C headline requirement.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleFailureNonFiniteTest,
	"RacingSim.Vehicle.FailureDetectionNonFinite",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleFailureNonFiniteTest::RunTest(const FString& Parameters)
{
	const float Nan = std::numeric_limits<float>::quiet_NaN();
	const float Inf = std::numeric_limits<float>::infinity();

	const FVehicleTelemetrySnapshot Base = MakeHealthyFailureSnapshot(10.0, 1);

	// Each corruption is applied ALONE to an otherwise healthy snapshot, so a pass
	// cannot be produced by some other field's failure.
	{
		FVehicleTelemetrySnapshot Corrupt = AdvanceHealthyFailureSnapshot(Base, 1.0 / 60.0);
		Corrupt.VelocityCms.Y = Nan;
		const FVehicleFailureReport Report = EvaluateFailurePair(Base, Corrupt);
		TestTrue(TEXT("A NaN velocity component raises NonFiniteState"),
			Report.Has(EVehicleFailureFlag::NonFiniteState));
		TestFalse(TEXT("A failing report carries a reason"), Report.Reason.IsEmpty());
	}

	{
		FVehicleTelemetrySnapshot Corrupt = AdvanceHealthyFailureSnapshot(Base, 1.0 / 60.0);
		Corrupt.LocationCm.Z = Inf;
		TestTrue(TEXT("An infinite location component raises NonFiniteState"),
			EvaluateFailurePair(Base, Corrupt).Has(EVehicleFailureFlag::NonFiniteState));
	}

	{
		FVehicleTelemetrySnapshot Corrupt = AdvanceHealthyFailureSnapshot(Base, 1.0 / 60.0);
		Corrupt.EngineRpm = Nan;
		TestTrue(TEXT("A NaN engine speed raises NonFiniteState"),
			EvaluateFailurePair(Base, Corrupt).Has(EVehicleFailureFlag::NonFiniteState));
	}

	{
		// A wheel-only corruption. This is the case a chassis-only finiteness check
		// would miss entirely, and per-wheel state is where a diverging suspension
		// solver shows up first.
		FVehicleTelemetrySnapshot Corrupt = AdvanceHealthyFailureSnapshot(Base, 1.0 / 60.0);
		Corrupt.Wheels[2].SpringForceN = Nan;

		const FVehicleFailureReport Report = EvaluateFailurePair(Base, Corrupt);
		TestTrue(TEXT("A NaN spring force on one wheel raises NonFiniteState"),
			Report.Has(EVehicleFailureFlag::NonFiniteState));
		TestTrue(TEXT("...and also names the wheel via UnstableWheelState"),
			Report.Has(EVehicleFailureFlag::UnstableWheelState));
		TestTrue(TEXT("...naming which wheel, which is the whole diagnostic value"),
			Report.Reason.Contains(TEXT("wheel 2")));
	}

	{
		// THE ORDERING GUARANTEE. NaN compares false against every bound, so a
		// "speed > limit" test passes silently on the most corrupt state a solver can
		// produce. If the finiteness check did not run first and independently, this
		// snapshot would be reported entirely clean -- green evidence for a destroyed
		// car. This assertion is the one that would catch that regression.
		FVehicleTelemetrySnapshot Corrupt = AdvanceHealthyFailureSnapshot(Base, 1.0 / 60.0);
		Corrupt.VelocityCms = FVector(Nan, Nan, Nan);
		Corrupt.ForwardSpeedCms = Nan;

		const FVehicleFailureReport Report = EvaluateFailurePair(Base, Corrupt);
		TestTrue(TEXT("An all-NaN velocity is reported despite failing every numeric comparison"),
			Report.HasAnyFailure());
		TestTrue(TEXT("...specifically as NonFiniteState"),
			Report.Has(EVehicleFailureFlag::NonFiniteState));
	}

	return true;
}

// ---------------------------------------------------------------------------
// Runaway energy and tunnelling.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleFailureEnergyTest,
	"RacingSim.Vehicle.FailureDetectionRunawayAndTunnelling",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleFailureEnergyTest::RunTest(const FString& Parameters)
{
	const FVehicleFailureThresholds Thresholds;
	const double Step = 1.0 / 60.0;
	const FVehicleTelemetrySnapshot Base = MakeHealthyFailureSnapshot(10.0, 1);

	{
		// Chaos' classic explosion signature: the body acquires an absurd velocity in
		// one step. Position is moved to match, so this is a pure speed test rather
		// than an accidental tunnelling one.
		FVehicleTelemetrySnapshot Exploded = AdvanceHealthyFailureSnapshot(Base, Step);
		Exploded.VelocityCms = FVector(Thresholds.MaxSpeedCms * 10.0f, 0.0, 0.0);
		Exploded.ForwardSpeedCms = Thresholds.MaxSpeedCms * 10.0f;
		Exploded.LocationCm = Base.LocationCm + Exploded.VelocityCms * Step;

		TestTrue(TEXT("A speed ten times the envelope raises RunawayEnergy"),
			EvaluateFailurePair(Base, Exploded).Has(EVehicleFailureFlag::RunawayEnergy));
	}

	{
		FVehicleTelemetrySnapshot Spinning = AdvanceHealthyFailureSnapshot(Base, Step);
		Spinning.AngularVelocityDegreesPerSecond = FVector(0.0, 0.0, Thresholds.MaxAngularSpeedDegreesPerSecond * 3.0f);
		TestTrue(TEXT("An angular speed past the envelope raises RunawayEnergy"),
			EvaluateFailurePair(Base, Spinning).Has(EVehicleFailureFlag::RunawayEnergy));
	}

	{
		FVehicleTelemetrySnapshot Revving = AdvanceHealthyFailureSnapshot(Base, Step);
		Revving.EngineRpm = Thresholds.MaxEngineRpm * 2.0f;
		TestTrue(TEXT("An engine speed past the envelope raises RunawayEnergy"),
			EvaluateFailurePair(Base, Revving).Has(EVehicleFailureFlag::RunawayEnergy));
	}

	{
		// A hard but LEGITIMATE acceleration must stay silent. 0 -> 100 km/h in a
		// sixtieth of a second is not legitimate; 5 m/s^2 over one step is.
		FVehicleTelemetrySnapshot Accelerating = AdvanceHealthyFailureSnapshot(Base, Step);
		const double AddedCms = 500.0 * Step; // 500 cm/s^2, about half a g.
		Accelerating.VelocityCms = Base.VelocityCms + FVector(AddedCms, 0.0, 0.0);
		Accelerating.ForwardSpeedCms = static_cast<float>(Accelerating.VelocityCms.X);
		Accelerating.LocationCm = Base.LocationCm + Accelerating.VelocityCms * Step;

		TestFalse(TEXT("Half a g of acceleration is not a failure"),
			EvaluateFailurePair(Base, Accelerating).HasAnyFailure());
	}

	{
		// The teleport. Velocity unchanged and entirely plausible, but the body has
		// moved a kilometre in one 60 Hz step -- which is what passing through geometry
		// looks like from outside the solver.
		FVehicleTelemetrySnapshot Teleported = AdvanceHealthyFailureSnapshot(Base, Step);
		Teleported.LocationCm = Base.LocationCm + FVector(100000.0, 0.0, 0.0);

		const FVehicleFailureReport Report = EvaluateFailurePair(Base, Teleported);
		TestTrue(TEXT("A 1 km jump in one step raises Tunnelling"),
			Report.Has(EVehicleFailureFlag::Tunnelling));
	}

	{
		// FRAME-RATE INDEPENDENCE. The same WALL-CLOCK event -- a car travelling at its
		// own recorded velocity for 1/30 s -- must be clean at 30 Hz just as it is at
		// 60 Hz. A fixed distance bound would fire here and not there, which would make
		// every performance change look like a physics regression.
		const FVehicleTelemetrySnapshot SlowFrame = AdvanceHealthyFailureSnapshot(Base, 1.0 / 30.0);
		TestFalse(TEXT("A legitimate 30 Hz step is not tunnelling"),
			EvaluateFailurePair(Base, SlowFrame).HasAnyFailure());

		const FVehicleTelemetrySnapshot FastFrame = AdvanceHealthyFailureSnapshot(Base, 1.0 / 144.0);
		TestFalse(TEXT("A legitimate 144 Hz step is not tunnelling"),
			EvaluateFailurePair(Base, FastFrame).HasAnyFailure());

		// ...and a real teleport is still caught at the FASTEST rate, where the
		// velocity-explained allowance is smallest and a naive implementation is most
		// likely to have compensated by widening it into uselessness.
		FVehicleTelemetrySnapshot FastTeleport = AdvanceHealthyFailureSnapshot(Base, 1.0 / 144.0);
		FastTeleport.LocationCm = Base.LocationCm + FVector(100000.0, 0.0, 0.0);
		TestTrue(TEXT("A teleport is still caught at 144 Hz"),
			EvaluateFailurePair(Base, FastTeleport).Has(EVehicleFailureFlag::Tunnelling));
	}

	{
		// A HITCH is not a fault. A step longer than MaxAnalysisStepSeconds suppresses
		// the extrapolating checks: extrapolating a velocity across a second says
		// nothing useful, and reporting it would manufacture a fault the simulation did
		// not commit. This is a deliberate blind spot and it is asserted, not assumed.
		FVehicleTelemetrySnapshot AfterHitch = AdvanceHealthyFailureSnapshot(Base, 2.0);
		AfterHitch.LocationCm = Base.LocationCm + FVector(100000.0, 0.0, 0.0);

		const FVehicleFailureReport Report = EvaluateFailurePair(Base, AfterHitch);
		TestFalse(TEXT("A 2 s hitch suppresses tunnelling analysis rather than reporting it"),
			Report.Has(EVehicleFailureFlag::Tunnelling));
		TestFalse(TEXT("...and a long step is not itself a TimeAnomaly"),
			Report.Has(EVehicleFailureFlag::TimeAnomaly));
	}

	{
		// A BACKWARDS clock is. Every rate below it would be negative or garbage.
		FVehicleTelemetrySnapshot Rewound = AdvanceHealthyFailureSnapshot(Base, Step);
		Rewound.TimestampSeconds = Base.TimestampSeconds - 1.0;

		TestTrue(TEXT("A backwards timestamp raises TimeAnomaly"),
			EvaluateFailurePair(Base, Rewound).Has(EVehicleFailureFlag::TimeAnomaly));
	}

	return true;
}

// ---------------------------------------------------------------------------
// Wheel state, contacts, and the two ACCUMULATING detectors.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleFailureWheelStateTest,
	"RacingSim.Vehicle.FailureDetectionWheelState",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleFailureWheelStateTest::RunTest(const FString& Parameters)
{
	const FVehicleFailureThresholds Thresholds;
	const double Step = 1.0 / 60.0;
	const FVehicleTelemetrySnapshot Base = MakeHealthyFailureSnapshot(10.0, 1);

	{
		FVehicleTelemetrySnapshot Diverged = AdvanceHealthyFailureSnapshot(Base, Step);
		Diverged.Wheels[1].NormalisedSuspensionLength = 3.5f;
		TestTrue(TEXT("A suspension length above 1 raises UnstableWheelState"),
			EvaluateFailurePair(Base, Diverged).Has(EVehicleFailureFlag::UnstableWheelState));

		Diverged.Wheels[1].NormalisedSuspensionLength = -2.0f;
		TestTrue(TEXT("A negative suspension length raises UnstableWheelState"),
			EvaluateFailurePair(Base, Diverged).Has(EVehicleFailureFlag::UnstableWheelState));
	}

	{
		// Fully extended and fully compressed are LEGAL readings, not faults. Chaos
		// initialises NormalizedSuspensionLength to 1.0 for a wheel with no contact,
		// so a detector that fired on 1.0 would report every airborne wheel.
		FVehicleTelemetrySnapshot AtLimits = AdvanceHealthyFailureSnapshot(Base, Step);
		AtLimits.Wheels[0].NormalisedSuspensionLength = 1.0f;
		AtLimits.Wheels[1].NormalisedSuspensionLength = 0.0f;
		// Wheel 1 is fully compressed but UNLOADED, which must not read as penetration.
		AtLimits.Wheels[1].SpringForceN = 0.0f;

		TestFalse(TEXT("Suspension exactly at either travel limit is not a fault"),
			EvaluateFailurePair(Base, AtLimits).HasAnyFailure());
	}

	{
		// A contact reported a kilometre from the body. The collision query has
		// returned nonsense; the suspension solver has not.
		FVehicleTelemetrySnapshot BadContact = AdvanceHealthyFailureSnapshot(Base, Step);
		BadContact.Wheels[3].ContactPointCm = BadContact.LocationCm + FVector(0.0, 0.0, -50000.0);

		const FVehicleFailureReport Report = EvaluateFailurePair(Base, BadContact);
		TestTrue(TEXT("A contact point far outside the chassis raises InvalidContact"),
			Report.Has(EVehicleFailureFlag::InvalidContact));
		TestFalse(TEXT("...and is NOT conflated with UnstableWheelState"),
			Report.Has(EVehicleFailureFlag::UnstableWheelState));
	}

	{
		// AIRBORNE, accumulating. One airborne frame is a jump; five seconds of them is
		// a car that has left the world.
		FVehicleFailureDetectorState State;
		FVehicleTelemetrySnapshot Previous = Base;

		bool bFiredEarly = false;
		bool bFiredEventually = false;

		// 6 seconds at 60 Hz, past the 5 s default threshold.
		for (int32 StepIndex = 0; StepIndex < 360; ++StepIndex)
		{
			FVehicleTelemetrySnapshot Current = AdvanceHealthyFailureSnapshot(Previous, Step);
			for (int32 WheelIndex = 0; WheelIndex < Current.NumWheels; ++WheelIndex)
			{
				Current.Wheels[WheelIndex].bInContact = false;
				Current.Wheels[WheelIndex].NormalisedSuspensionLength = 1.0f;
				Current.Wheels[WheelIndex].SpringForceN = 0.0f;
			}

			const FVehicleFailureReport Report =
				RacingSim::Vehicle::EvaluateVehicleFailures(Previous, Current, Thresholds, State);

			// Below the threshold, silence. A detector that fires at 1 s would report
			// every kerb hop.
			if (StepIndex < 120 && Report.Has(EVehicleFailureFlag::UnstableWheelState))
			{
				bFiredEarly = true;
			}
			if (Report.Has(EVehicleFailureFlag::UnstableWheelState))
			{
				bFiredEventually = true;
			}

			Previous = Current;
		}

		TestFalse(TEXT("Two seconds airborne is not yet a failure"), bFiredEarly);
		TestTrue(TEXT("Six seconds airborne raises UnstableWheelState"), bFiredEventually);

		// Touching down clears the accumulator outright -- airborne time is CONTINUOUS,
		// so a car bouncing over kerbs must not accumulate its way into a fault one
		// hop at a time.
		//
		// The wheels are put back IN CONTACT explicitly. AdvanceHealthyFailureSnapshot
		// copies its argument, and `Previous` is the last airborne frame, so a bare
		// Advance() call produces another airborne snapshot -- which is what this
		// assertion caught on VEH-004's first Smoke run (it reported 6.02 s of
		// accumulated airborne time instead of 0). The test was wrong, not the detector.
		FVehicleTelemetrySnapshot Landed = AdvanceHealthyFailureSnapshot(Previous, Step);
		for (int32 WheelIndex = 0; WheelIndex < Landed.NumWheels; ++WheelIndex)
		{
			Landed.Wheels[WheelIndex].bInContact = true;
			Landed.Wheels[WheelIndex].NormalisedSuspensionLength = 0.5f;
			Landed.Wheels[WheelIndex].SpringForceN = 3500.0f;
			Landed.Wheels[WheelIndex].ContactPointCm = Landed.LocationCm + FVector(0.0, 0.0, -40.0);
		}

		RacingSim::Vehicle::EvaluateVehicleFailures(Previous, Landed, Thresholds, State);
		TestEqual(TEXT("Landing clears the airborne accumulator"), State.AirborneSeconds, 0.0f);
	}

	{
		// PERSISTENT PENETRATION, accumulating. Fully compressed AND under load, held.
		FVehicleFailureDetectorState State;
		FVehicleTelemetrySnapshot Previous = Base;

		bool bFiredEarly = false;
		bool bFiredEventually = false;

		// 3 seconds at 60 Hz, past the 2 s default threshold.
		for (int32 StepIndex = 0; StepIndex < 180; ++StepIndex)
		{
			FVehicleTelemetrySnapshot Current = AdvanceHealthyFailureSnapshot(Previous, Step);
			Current.Wheels[0].bInContact = true;
			Current.Wheels[0].NormalisedSuspensionLength = 0.0f;
			Current.Wheels[0].SpringForceN = 50000.0f;

			const FVehicleFailureReport Report =
				RacingSim::Vehicle::EvaluateVehicleFailures(Previous, Current, Thresholds, State);

			// Under a second is a kerb strike, and reporting it would drown the real
			// signal. This is the assertion that makes "persistent" mean something.
			if (StepIndex < 50 && Report.Has(EVehicleFailureFlag::PersistentPenetration))
			{
				bFiredEarly = true;
			}
			if (Report.Has(EVehicleFailureFlag::PersistentPenetration))
			{
				bFiredEventually = true;
			}

			Previous = Current;
		}

		TestFalse(TEXT("A sub-second kerb strike is not persistent penetration"), bFiredEarly);
		TestTrue(TEXT("Three seconds pinned under load raises PersistentPenetration"), bFiredEventually);

		// FRAME-RATE INDEPENDENCE of the accumulator: the same three wall-clock seconds
		// at 30 Hz must also fire, in far fewer iterations. A per-tick counter would
		// need twice as many and would silently fail here.
		FVehicleFailureDetectorState SlowState;
		FVehicleTelemetrySnapshot SlowPrevious = Base;
		bool bFiredAtThirtyHz = false;

		for (int32 StepIndex = 0; StepIndex < 90; ++StepIndex)
		{
			FVehicleTelemetrySnapshot Current = AdvanceHealthyFailureSnapshot(SlowPrevious, 1.0 / 30.0);
			Current.Wheels[0].bInContact = true;
			Current.Wheels[0].NormalisedSuspensionLength = 0.0f;
			Current.Wheels[0].SpringForceN = 50000.0f;

			if (RacingSim::Vehicle::EvaluateVehicleFailures(SlowPrevious, Current, Thresholds, SlowState)
				.Has(EVehicleFailureFlag::PersistentPenetration))
			{
				bFiredAtThirtyHz = true;
			}
			SlowPrevious = Current;
		}

		TestTrue(TEXT("The same wall-clock penetration is detected at 30 Hz"), bFiredAtThirtyHz);
	}

	{
		// A stale input correction surfaces as a failure flag, so one report answers
		// "did the solver break, or did the browser go quiet?"
		FVehicleTelemetrySnapshot Stale = AdvanceHealthyFailureSnapshot(Base, Step);
		Stale.InputCorrections = static_cast<uint8>(EVehicleInputCorrection::StaleSample);

		const FVehicleFailureReport Report = EvaluateFailurePair(Base, Stale);
		TestTrue(TEXT("A StaleSample correction raises StaleInput"),
			Report.Has(EVehicleFailureFlag::StaleInput));
		TestFalse(TEXT("...and is not mistaken for a physics fault"),
			Report.Has(EVehicleFailureFlag::NonFiniteState)
				|| Report.Has(EVehicleFailureFlag::RunawayEnergy));
	}

	return true;
}

// ---------------------------------------------------------------------------
// The thresholds DataAsset: ranges, relationships, and the anti-drift pin.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleFailureThresholdsTest,
	"RacingSim.Vehicle.FailureThresholdsValidation",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleFailureThresholdsTest::RunTest(const FString& Parameters)
{
	UVehicleFailureThresholdsDataAsset* Asset =
		NewObject<UVehicleFailureThresholdsDataAsset>(GetTransientPackage());

	TestTrue(TEXT("A default thresholds asset validates cleanly"),
		Asset->ValidateReadOnly().IsClean());

	// CORE-003's other direction: the declared table and the UPROPERTY metadata must
	// agree, and a NEWLY ADDED clamped property that was forgotten in the table must
	// fail here. That second direction is the one that actually causes the bug.
	const FRacingValidationResult MetadataCheck = VerifyRangesMatchMetadata(
		UVehicleFailureThresholdsDataAsset::StaticClass(),
		UVehicleFailureThresholdsDataAsset::StaticRanges());
	TestEqual(TEXT("Declared ranges and UPROPERTY metadata agree in both directions"),
		MetadataCheck.NumFailed(), 0);
	TestTrue(TEXT("...with no reported mismatch at all"), MetadataCheck.IsClean());

	// -- Range enforcement, including the non-finite path.
	{
		UVehicleFailureThresholdsDataAsset* Broken =
			NewObject<UVehicleFailureThresholdsDataAsset>(GetTransientPackage());
		Broken->MaxSpeedCms = std::numeric_limits<float>::quiet_NaN();
		Broken->MaxAirborneSeconds = -5.0f;

		const FRacingValidationResult Result = Broken->Validate(true);
		TestTrue(TEXT("A NaN speed envelope is reported"),
			HasFailureThresholdIssueFor(Result, TEXT("MaxSpeedCms")));
		TestTrue(TEXT("A negative airborne threshold is reported"),
			HasFailureThresholdIssueFor(Result, TEXT("MaxAirborneSeconds")));
		TestTrue(TEXT("Correction replaces the NaN with a finite value"),
			FMath::IsFinite(Broken->MaxSpeedCms));
	}

	// -- The ReplacementValue case, which is the interesting one.
	{
		// MaxAnalysisStepSeconds' MINIMUM IS NOT ITS SAFE END: a 0.02 s window declines
		// to judge nearly every step, silently disarming tunnelling and acceleration
		// detection. Clamping to the bound would take an obviously broken config and
		// make it a quietly useless one -- the exact hazard
		// FRacingPropertyRange::ReplacementValue exists for.
		UVehicleFailureThresholdsDataAsset* Broken =
			NewObject<UVehicleFailureThresholdsDataAsset>(GetTransientPackage());
		Broken->MaxAnalysisStepSeconds = -1.0f;

		Broken->Validate(true);
		TestEqual(TEXT("A broken analysis window is replaced with the default, not clamped to the minimum"),
			Broken->MaxAnalysisStepSeconds, 0.5f);
	}

	// -- Relationships no per-field clamp can express.
	{
		UVehicleFailureThresholdsDataAsset* Overlapping =
			NewObject<UVehicleFailureThresholdsDataAsset>(GetTransientPackage());
		// Individually legal, collectively meaningless: a fully-compressed wheel would
		// also read as an out-of-range suspension length, so both detectors fire on the
		// same value and neither report means what it says.
		Overlapping->PenetrationSuspensionLengthFraction = 0.2f;
		Overlapping->SuspensionLengthTolerance = 0.1f;

		TestTrue(TEXT("An overlapping penetration/tolerance pair is reported"),
			HasFailureThresholdIssueFor(
				Overlapping->ValidateReadOnly(), TEXT("PenetrationSuspensionLengthFraction")));
	}

	{
		UVehicleFailureThresholdsDataAsset* TooShort =
			NewObject<UVehicleFailureThresholdsDataAsset>(GetTransientPackage());
		// A penetration threshold shorter than one analysable step can be crossed by a
		// single step, which is a kerb strike. "Persistent" would mean nothing.
		TooShort->PenetrationPersistSeconds = 0.1f;
		TooShort->MaxAnalysisStepSeconds = 0.5f;

		TestTrue(TEXT("A penetration threshold below one analysis step is reported"),
			HasFailureThresholdIssueFor(TooShort->ValidateReadOnly(), TEXT("PenetrationPersistSeconds")));
	}

	{
		UVehicleFailureThresholdsDataAsset* TooShort =
			NewObject<UVehicleFailureThresholdsDataAsset>(GetTransientPackage());
		TooShort->MaxAirborneSeconds = 0.2f;
		TooShort->MaxAnalysisStepSeconds = 0.5f;

		TestTrue(TEXT("An airborne threshold below one analysis step is reported"),
			HasFailureThresholdIssueFor(TooShort->ValidateReadOnly(), TEXT("MaxAirborneSeconds")));
	}

	// -- ValidateReadOnly must not mutate.
	{
		UVehicleFailureThresholdsDataAsset* Broken =
			NewObject<UVehicleFailureThresholdsDataAsset>(GetTransientPackage());
		Broken->MaxSpeedCms = -1.0f;

		Broken->ValidateReadOnly();
		TestEqual(TEXT("ValidateReadOnly reports without writing"), Broken->MaxSpeedCms, -1.0f);
	}

	return true;
}

// ---------------------------------------------------------------------------
// The anti-drift pin between the asset's defaults and the POD's defaults.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleFailureThresholdDefaultsTest,
	"RacingSim.Vehicle.FailureThresholdDefaultsMatchAsset",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleFailureThresholdDefaultsTest::RunTest(const FString& Parameters)
{
	// TWO INDEPENDENT COPIES OF THE SAME NUMBERS EXIST, and the duplication is
	// structural rather than lazy: CORE-003's EnforceRanges is reflection-driven and
	// only reaches FLAT numeric UPROPERTYs, so the asset cannot simply embed the POD.
	// This test is what makes the duplication safe, in the same spirit as
	// VerifyRangesMatchMetadata -- and it fails when a field is added to one side and
	// forgotten on the other, which is the direction that causes the bug.
	//
	// A pawn with no FailureThresholdsAsset uses the POD defaults; a pawn with one uses
	// the asset's. If the two disagree, the detector's behaviour depends on whether
	// someone happened to assign an asset, which is exactly the kind of divergence that
	// makes a failure report unreproducible.
	const UVehicleFailureThresholdsDataAsset* Cdo =
		GetDefault<UVehicleFailureThresholdsDataAsset>();
	TestNotNull(TEXT("The thresholds CDO exists"), Cdo);

	if (Cdo == nullptr)
	{
		return false;
	}

	const FVehicleFailureThresholds FromAsset = Cdo->GetThresholds();
	const FVehicleFailureThresholds FromPod;

	TestEqual(TEXT("MaxSpeedCms"), FromAsset.MaxSpeedCms, FromPod.MaxSpeedCms);
	TestEqual(TEXT("MaxAngularSpeedDegreesPerSecond"),
		FromAsset.MaxAngularSpeedDegreesPerSecond, FromPod.MaxAngularSpeedDegreesPerSecond);
	TestEqual(TEXT("MaxEngineRpm"), FromAsset.MaxEngineRpm, FromPod.MaxEngineRpm);
	TestEqual(TEXT("MaxAccelerationCmsPerSecondSquared"),
		FromAsset.MaxAccelerationCmsPerSecondSquared, FromPod.MaxAccelerationCmsPerSecondSquared);
	TestEqual(TEXT("TunnelVelocityFactor"), FromAsset.TunnelVelocityFactor, FromPod.TunnelVelocityFactor);
	TestEqual(TEXT("TunnelToleranceCm"), FromAsset.TunnelToleranceCm, FromPod.TunnelToleranceCm);
	TestEqual(TEXT("MaxAnalysisStepSeconds"), FromAsset.MaxAnalysisStepSeconds, FromPod.MaxAnalysisStepSeconds);
	TestEqual(TEXT("SuspensionLengthTolerance"),
		FromAsset.SuspensionLengthTolerance, FromPod.SuspensionLengthTolerance);
	TestEqual(TEXT("MaxAirborneSeconds"), FromAsset.MaxAirborneSeconds, FromPod.MaxAirborneSeconds);
	TestEqual(TEXT("MaxContactDistanceCm"), FromAsset.MaxContactDistanceCm, FromPod.MaxContactDistanceCm);
	TestEqual(TEXT("PenetrationSuspensionLengthFraction"),
		FromAsset.PenetrationSuspensionLengthFraction, FromPod.PenetrationSuspensionLengthFraction);
	TestEqual(TEXT("PenetrationSpringForceN"), FromAsset.PenetrationSpringForceN, FromPod.PenetrationSpringForceN);
	TestEqual(TEXT("PenetrationPersistSeconds"),
		FromAsset.PenetrationPersistSeconds, FromPod.PenetrationPersistSeconds);

	// The count guard. TestEqual above cannot notice a field that exists on neither
	// side of this list; sizeof can notice one that was added to the POD and never
	// compared. If this fails, add the new field to the comparisons above and update
	// the expected size -- do not simply widen the number.
	TestEqual(TEXT("FVehicleFailureThresholds still has exactly 13 float fields; add new ones to the comparison above"),
		static_cast<int32>(sizeof(FVehicleFailureThresholds) / sizeof(float)), 13);

	return true;
}
