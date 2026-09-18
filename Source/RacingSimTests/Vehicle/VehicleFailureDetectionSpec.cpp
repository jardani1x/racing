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
	FVehicleTelemetrySnapshot MakeHealthyFailureSnapshot(const double TimeSeconds, const int64 CaptureIndex)
	{
		FVehicleTelemetrySnapshot Snapshot;
		Snapshot.bIsValid = true;

		// BOTH clocks, set to the same value. The detector derives its step from
		// SimulationTimeSeconds and nothing else; TimestampSeconds is carried so these
		// fixtures still look like something a real capture produced, and because a real
		// capture's two clocks do coincide when the game runs in real time. A test that
		// needs the two to diverge sets them apart explicitly.
		Snapshot.TimestampSeconds = TimeSeconds;
		Snapshot.SimulationTimeSeconds = TimeSeconds;
		Snapshot.CaptureIndex = CaptureIndex;
		Snapshot.FrameDeltaSeconds = 1.0f / 60.0f;

		// 100 km/h == 2777.78 cm/s. Travelling along +X, which is forward in Unreal.
		Snapshot.VelocityCms = FVector(2777.78, 0.0, 0.0);
		Snapshot.ForwardSpeedCms = 2777.78f;
		Snapshot.LocationCm = FVector(TimeSeconds * 2777.78, 0.0, 0.0);
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
		Next.SimulationTimeSeconds = Previous.SimulationTimeSeconds + StepSeconds;
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
		// The ACCELERATION branch of the runaway envelope, asserted POSITIVELY.
		//
		// This is the branch commit 62134d0 rewrote to divide by SimulationTimeSeconds
		// instead of by measured wall-clock time, and it is the one RunawayEnergy
		// comparison that no other case in this test reaches: delete the comparison
		// entirely and the speed, angular-speed and engine-rpm cases above all stay
		// green, as does the half-a-g negative below. It is asserted separately, and the
		// lurch is deliberately kept inside every OTHER envelope, so that acceleration is
		// the only comparison that can raise the flag.
		FVehicleTelemetrySnapshot Lurching = AdvanceHealthyFailureSnapshot(Base, Step);
		const double LurchAddedCms = 200.0; // 200 cm/s across 1/60 s == 12,000 cm/s^2.
		Lurching.VelocityCms = Base.VelocityCms + FVector(LurchAddedCms, 0.0, 0.0);
		Lurching.ForwardSpeedCms = static_cast<float>(Lurching.VelocityCms.X);
		Lurching.LocationCm = Base.LocationCm + Lurching.VelocityCms * Step;

		// 2777.78 + 200 == 2977.78 cm/s against a 15,000 cm/s envelope, the angular speed
		// and engine rpm are untouched, and the position agrees with the new velocity so
		// Tunnelling cannot fire either. Asserted rather than asserted-by-comment: if a
		// future threshold change makes the lurch trip the speed envelope instead, this
		// line goes red and says so, rather than letting the acceleration case pass for
		// the wrong reason.
		TestTrue(TEXT("The lurch stays inside the SPEED envelope, so only acceleration can fire"),
			Lurching.GetSpeedMagnitudeCms() < Thresholds.MaxSpeedCms);

		const FVehicleFailureReport LurchReport = EvaluateFailurePair(Base, Lurching);
		TestTrue(TEXT("12,000 cm/s^2 past an 8,000 cm/s^2 envelope raises RunawayEnergy"),
			LurchReport.Has(EVehicleFailureFlag::RunawayEnergy));
		TestTrue(TEXT("...and the reason names the acceleration envelope, not another one"),
			LurchReport.Reason.Contains(TEXT("cm/s^2")));
	}

	{
		// The same lurch, with the WALL clock stretched so that wall-clock arithmetic
		// would MISS it. The VEH-004 regression below has one sign; this is the other.
		//
		// Dividing an honest 200 cm/s by a two-second wall-clock step yields 100 cm/s^2,
		// comfortably inside the envelope, so a detector still reading TimestampSeconds
		// would stay silent on a genuine runaway. Simulated time still says 1/60 s, and
		// the flag must still be raised. Together with the negative case below, this pins
		// the clock choice from both directions: the wall clock must not manufacture a
		// fault, and it must not conceal one.
		FVehicleTelemetrySnapshot SlowWallClockLurch = AdvanceHealthyFailureSnapshot(Base, Step);
		SlowWallClockLurch.TimestampSeconds = Base.TimestampSeconds + 2.0;
		SlowWallClockLurch.VelocityCms = Base.VelocityCms + FVector(200.0, 0.0, 0.0);
		SlowWallClockLurch.ForwardSpeedCms = static_cast<float>(SlowWallClockLurch.VelocityCms.X);
		SlowWallClockLurch.LocationCm = Base.LocationCm + SlowWallClockLurch.VelocityCms * Step;

		TestTrue(TEXT("A runaway acceleration is still caught when the WALL clock ran SLOWER than the simulation"),
			EvaluateFailurePair(Base, SlowWallClockLurch).Has(EVehicleFailureFlag::RunawayEnergy));
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
		// THE VEH-004 REGRESSION. Wall-clock time and simulated time disagree, and the
		// detector must follow the simulated one.
		//
		// The car does exactly what its own velocity says over a real 1/60 s step, so it
		// is healthy. The wall clock advances by a microsecond instead -- which is what a
		// fixed-step test loop, a paused editor, a breakpoint or a dilated time source
		// all look like. VEH-004 divided the honest motion by that microsecond and
		// reported a car accelerating at roughly 17,000 g.
		FVehicleTelemetrySnapshot FastWallClock = AdvanceHealthyFailureSnapshot(Base, Step);
		FastWallClock.TimestampSeconds = Base.TimestampSeconds + 0.000001;

		TestFalse(TEXT("A healthy step is still healthy when the WALL clock ran faster than the simulation"),
			EvaluateFailurePair(Base, FastWallClock).HasAnyFailure());

		// And the converse: a genuine teleport is still caught when the wall clock is the
		// thing that looks normal. Catching it here is what proves the wall clock is not
		// merely ignored on the easy side of the test.
		FVehicleTelemetrySnapshot SlowWallClockTeleport = AdvanceHealthyFailureSnapshot(Base, Step);
		SlowWallClockTeleport.TimestampSeconds = Base.TimestampSeconds + 1.0;
		SlowWallClockTeleport.LocationCm = Base.LocationCm + FVector(100000.0, 0.0, 0.0);

		TestTrue(TEXT("A 1 km jump in one SIMULATED step still raises Tunnelling when the wall clock lagged"),
			EvaluateFailurePair(Base, SlowWallClockTeleport).Has(EVehicleFailureFlag::Tunnelling));
	}

	{
		// A BACKWARDS clock is. Every rate below it would be negative or garbage.
		// Rewinding the SIMULATION clock specifically: that is the one the detector
		// divides by, so it is the one whose monotonicity has to be defended.
		FVehicleTelemetrySnapshot Rewound = AdvanceHealthyFailureSnapshot(Base, Step);
		Rewound.SimulationTimeSeconds = Base.SimulationTimeSeconds - 1.0;

		TestTrue(TEXT("A backwards simulation clock raises TimeAnomaly"),
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
	TestEqual(TEXT("MaxContactSuppressionSeconds"),
		FromAsset.MaxContactSuppressionSeconds, FromPod.MaxContactSuppressionSeconds);
	TestEqual(TEXT("PenetrationSuspensionLengthFraction"),
		FromAsset.PenetrationSuspensionLengthFraction, FromPod.PenetrationSuspensionLengthFraction);
	TestEqual(TEXT("PenetrationSpringForceN"), FromAsset.PenetrationSpringForceN, FromPod.PenetrationSpringForceN);
	TestEqual(TEXT("PenetrationPersistSeconds"),
		FromAsset.PenetrationPersistSeconds, FromPod.PenetrationPersistSeconds);

	// The count guard. TestEqual above cannot notice a field that exists on neither
	// side of this list; sizeof can notice one that was added to the POD and never
	// compared. If this fails, add the new field to the comparisons above and update
	// the expected size -- do not simply widen the number.
	TestEqual(TEXT("FVehicleFailureThresholds still has exactly 14 float fields; add new ones to the comparison above"),
		static_cast<int32>(sizeof(FVehicleFailureThresholds) / sizeof(float)), 14);

	return true;
}

// ---------------------------------------------------------------------------
// Discontinuity suppression: what an announced teleport must and must not silence.
// ---------------------------------------------------------------------------

/**
 * VEH-006. FVehicleFailureDetectorState::NotifyDiscontinuity exists so a deliberate pose
 * change -- VEH-005's safe reset, a respawn, a grid placement -- is not reported as the
 * solver having lost the car. Its whole contract is a matter of scope and duration, and
 * both halves are easy to get wrong in opposite directions:
 *
 *   TOO NARROW  and the reset it was called for still raises. That is what shipped:
 *               Reset() dropped the accumulators and nothing else, so the next evaluation
 *               compared straight across the teleport. Caught end-to-end by
 *               RacingSim.Vehicle.Manoeuvre.FailureDetectorCatchesUnannouncedTeleport;
 *               pinned here, where it needs no world and runs at the Smoke gate.
 *
 *   TOO WIDE    and a car that is genuinely broken goes unreported because somebody reset
 *               it. A suppression that outlives one evaluation, or that swallows a NaN,
 *               is a detector that can be silenced by the very event most likely to break
 *               the car.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleFailureDiscontinuityTest,
	"RacingSim.Vehicle.FailureDetectionDiscontinuity",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleFailureDiscontinuityTest::RunTest(const FString& Parameters)
{
	constexpr double Step = 1.0 / 60.0;
	const FVehicleTelemetrySnapshot Base = MakeHealthyFailureSnapshot(10.0, 1);

	// The fault: 1 km in one 60 Hz step, with the wheel contacts left where the car used
	// to be. That is the exact shape a TeleportPhysics produces -- a post-teleport pose
	// beside pre-teleport physics-thread wheel data -- and it raises BOTH a pair check
	// (Tunnelling) and a single-snapshot geometry check (InvalidContact), which is why it
	// is the right fixture for testing what suppression covers.
	FVehicleTelemetrySnapshot Teleported = AdvanceHealthyFailureSnapshot(Base, Step);
	Teleported.LocationCm = Base.LocationCm + FVector(100000.0, 0.0, 0.0);
	for (int32 WheelIndex = 0; WheelIndex < Teleported.NumWheels; ++WheelIndex)
	{
		Teleported.Wheels[WheelIndex].ContactPointCm = Base.LocationCm + FVector(0.0, 0.0, -40.0);
	}

	{
		// Unannounced. Both flags, or the test below proves nothing.
		const FVehicleFailureReport Report = EvaluateFailurePair(Base, Teleported);
		TestTrue(TEXT("An unannounced teleport raises Tunnelling"),
			Report.Has(EVehicleFailureFlag::Tunnelling));
		TestTrue(TEXT("An unannounced teleport with stale wheel contacts raises InvalidContact"),
			Report.Has(EVehicleFailureFlag::InvalidContact));
	}

	{
		// Announced. Same two snapshots, same thresholds, one call in between.
		FVehicleFailureDetectorState State;
		State.NotifyDiscontinuity();

		const FVehicleFailureReport Report = RacingSim::Vehicle::EvaluateVehicleFailures(
			Base, Teleported, FVehicleFailureThresholds(), State);

		TestFalse(
			FString::Printf(TEXT("An announced discontinuity raises nothing, got [%s]"),
				*RacingSim::Vehicle::DescribeVehicleFailureFlags(Report.Flags)),
			Report.HasAnyFailure());

		// Suppressing the pair comparison must also suppress the STEP that comparison
		// produces. A report claiming a measured step across a discontinuity would hand
		// every downstream rate a denominator spanning a teleport.
		TestEqual(TEXT("No step is measured across a discontinuity"), Report.MeasuredStepSeconds, 0.0f);

		// -- DURATION: exactly one evaluation, not "until the car looks fine again". --
		//
		// Re-run the identical fault on the SAME state object. The latch was consumed
		// above, so this must raise -- otherwise a single reset would blind the detector
		// for the rest of the session.
		const FVehicleFailureReport Second = RacingSim::Vehicle::EvaluateVehicleFailures(
			Base, Teleported, FVehicleFailureThresholds(), State);
		TestTrue(TEXT("The suppression lasts ONE evaluation and no longer"),
			Second.Has(EVehicleFailureFlag::Tunnelling));
	}

	{
		// -- THE TAIL: the contact geometry catches up LATER than the pose. --
		//
		// Measured on the real pipeline, not assumed. Driving one step at a time after an
		// announced reset, the chassis pose moves at step 0 and the wheel contacts do not
		// move until step 1, because the game thread sets the transform synchronously
		// while Chaos marshals wheel output back a frame later. So the evaluation AFTER
		// the straddling one still carries pre-teleport contacts, and a suppression that
		// ended after one evaluation reported Error-severity InvalidContact on a car
		// sitting still at its reset pose.
		//
		// The location overload exists for that tail, and it is self-terminating rather
		// than counted: see FVehicleFailureDetectorState::PreDiscontinuityLocationCm.
		FVehicleFailureDetectorState State;
		State.NotifyDiscontinuity(Base.LocationCm);

		const FVehicleFailureReport Straddling = RacingSim::Vehicle::EvaluateVehicleFailures(
			Base, Teleported, FVehicleFailureThresholds(), State);
		TestFalse(
			FString::Printf(TEXT("The straddling evaluation is silent, got [%s]"),
				*RacingSim::Vehicle::DescribeVehicleFailureFlags(Straddling.Flags)),
			Straddling.HasAnyFailure());

		// The capture right after the teleport, taken from the measured pipeline, reports
		// every wheel OUT of contact with a zeroed contact point:
		//
		//     cap=241 loc=(-2500,4330,70) [w0 c=0 pt=(0,0,0)] ... [w3 c=0 pt=(0,0,0)]
		//
		// This step exists to pin that a snapshot carrying NO contact evidence must not
		// expire the basis. An earlier version of this rule expired it whenever nothing
		// matched, which threw the basis away here and let the stale capture that follows
		// through as Error-severity InvalidContact.
		FVehicleTelemetrySnapshot NoContact = AdvanceHealthyFailureSnapshot(Teleported, Step);
		NoContact.LocationCm = Teleported.LocationCm;
		for (int32 WheelIndex = 0; WheelIndex < NoContact.NumWheels; ++WheelIndex)
		{
			NoContact.Wheels[WheelIndex].bInContact = false;
			NoContact.Wheels[WheelIndex].ContactPointCm = FVector::ZeroVector;
		}

		const FVehicleFailureReport Untouched = RacingSim::Vehicle::EvaluateVehicleFailures(
			Teleported, NoContact, FVehicleFailureThresholds(), State);
		TestFalse(
			FString::Printf(TEXT("A capture with no contacts at all is not a fault, got [%s]"),
				*RacingSim::Vehicle::DescribeVehicleFailureFlags(Untouched.Flags)),
			Untouched.HasAnyFailure());

		// One capture later. The car has not moved -- both snapshots are at the teleported
		// pose, so no pair check has anything to say -- but the wheels are STILL reporting
		// the contacts they had before the teleport.
		FVehicleTelemetrySnapshot StillStale = AdvanceHealthyFailureSnapshot(NoContact, Step);
		StillStale.LocationCm = Teleported.LocationCm;
		for (int32 WheelIndex = 0; WheelIndex < StillStale.NumWheels; ++WheelIndex)
		{
			StillStale.Wheels[WheelIndex].bInContact = true;
			StillStale.Wheels[WheelIndex].ContactPointCm = Base.LocationCm + FVector(0.0, 0.0, -40.0);
		}

		const FVehicleFailureReport Tail = RacingSim::Vehicle::EvaluateVehicleFailures(
			NoContact, StillStale, FVehicleFailureThresholds(), State);
		TestFalse(
			FString::Printf(TEXT("Contacts still describing the pose the car LEFT are not a fault, got [%s]"),
				*RacingSim::Vehicle::DescribeVehicleFailureFlags(Tail.Flags)),
			Tail.HasAnyFailure());

		// And the frame the physics output catches up. Nothing raised, and -- the point of
		// this step -- the basis is dropped, because no wheel matches it any more.
		FVehicleTelemetrySnapshot CaughtUp = AdvanceHealthyFailureSnapshot(StillStale, Step);
		CaughtUp.LocationCm = Teleported.LocationCm;
		for (int32 WheelIndex = 0; WheelIndex < CaughtUp.NumWheels; ++WheelIndex)
		{
			CaughtUp.Wheels[WheelIndex].ContactPointCm = Teleported.LocationCm + FVector(0.0, 0.0, -40.0);
		}

		const FVehicleFailureReport Fresh = RacingSim::Vehicle::EvaluateVehicleFailures(
			StillStale, CaughtUp, FVehicleFailureThresholds(), State);
		TestFalse(
			FString::Printf(TEXT("A car whose wheel output caught up reports nothing, got [%s]"),
				*RacingSim::Vehicle::DescribeVehicleFailureFlags(Fresh.Flags)),
			Fresh.HasAnyFailure());

		// -- DURATION, the half a counter cannot express. --
		//
		// Feed the stale contacts back in. They are now a GENUINE impossible reading --
		// the physics output demonstrably caught up one evaluation ago -- and the detector
		// must say so. A suppression held open for the rest of the session would not.
		FVehicleTelemetrySnapshot StaleAgain = AdvanceHealthyFailureSnapshot(CaughtUp, Step);
		StaleAgain.LocationCm = Teleported.LocationCm;
		for (int32 WheelIndex = 0; WheelIndex < StaleAgain.NumWheels; ++WheelIndex)
		{
			StaleAgain.Wheels[WheelIndex].ContactPointCm = Base.LocationCm + FVector(0.0, 0.0, -40.0);
		}

		const FVehicleFailureReport Resumed = RacingSim::Vehicle::EvaluateVehicleFailures(
			CaughtUp, StaleAgain, FVehicleFailureThresholds(), State);
		TestTrue(TEXT("The contact suppression ends when the wheel output catches up"),
			Resumed.Has(EVehicleFailureFlag::InvalidContact));
	}

	{
		// -- SCOPE: a NaN is still a NaN. --
		//
		// A reset is the likeliest moment for the solver to produce corrupt state, so the
		// one class of fault that must survive suppression is the one that says the state
		// is not a number at all. Suppressing it would hide the failure exactly when it is
		// most diagnosable.
		FVehicleTelemetrySnapshot Corrupt = AdvanceHealthyFailureSnapshot(Base, Step);
		Corrupt.VelocityCms.Y = std::numeric_limits<double>::quiet_NaN();

		FVehicleFailureDetectorState State;
		State.NotifyDiscontinuity();

		const FVehicleFailureReport Report = RacingSim::Vehicle::EvaluateVehicleFailures(
			Base, Corrupt, FVehicleFailureThresholds(), State);

		TestTrue(TEXT("A discontinuity does NOT suppress NonFiniteState"),
			Report.Has(EVehicleFailureFlag::NonFiniteState));
	}

	{
		// Reset() must NOT arm the suppression. The two are different requests -- "this
		// history is meaningless" versus "the next sample is not comparable" -- and a
		// Reset() that silently suppressed would make every accumulator clear a blind spot.
		FVehicleFailureDetectorState State;
		State.Reset();

		const FVehicleFailureReport Report = RacingSim::Vehicle::EvaluateVehicleFailures(
			Base, Teleported, FVehicleFailureThresholds(), State);

		TestTrue(TEXT("Reset() alone does not suppress anything"),
			Report.Has(EVehicleFailureFlag::Tunnelling));
	}

	return true;
}

/**
 * VEH-006 repair cycle 1. The contact-suppression basis must TERMINATE, including in the
 * two cases where the rule that normally ends it can never fire.
 *
 * FVehicleFailureDetectorState::PreDiscontinuityLocationCm is dropped by the first
 * evaluation that sees contact from somewhere other than the pose the car left. That is
 * the right rule whenever contact comes back, and it has no answer at all when it does
 * not:
 *
 *   NO CONTACT EVER   a reset that leaves the car airborne, inverted, wedged, or below
 *                     the world produces no fresh contact evidence for the rest of the
 *                     session.
 *
 *   A SHORT RESET     a reset that moved the car less than MaxContactDistanceCm produces
 *                     only contacts that still match the stale basis, so none of them
 *                     ever counts as fresh.
 *
 * In both, the pre-repair detector kept the basis armed indefinitely and went on
 * swallowing genuine InvalidContact near that one pose -- and those are exactly the
 * situations the reset path exists to recover from, so it went deaf where it mattered
 * most. FVehicleFailureThresholds::MaxContactSuppressionSeconds is the backstop, and
 * CASE 1 and CASE 2 prove it fires.
 *
 * Repair cycle 2 then found that a seconds budget is not a bound on its own, because it
 * assumes a usable clock. CASE 4 through CASE 7 cover the four ways that assumption
 * fails, and each of them goes RED against the cycle-1 detector:
 *
 *   A LONG FRAME      one step longer than the whole budget expires the basis on the
 *                     evaluation that still needs it. Bounded by an evaluation FLOOR.
 *
 *   A STOPPED CLOCK   SuppressedForSeconds is pinned at 0.0 for ever, so the budget
 *                     never expires anything. Bounded by an evaluation CEILING.
 *
 *   A NON-FINITE      the clock cannot bound anything, and the same evaluation already
 *   CLOCK             reports NonFiniteState. The basis is held rather than dropped, so
 *                     stale geometry cannot stack a second, misleading flag beside it.
 *
 *   A BACKWARDS       the clock was re-based under the detector. The stamp is dead but
 *   CLOCK             the basis is not, so the stamp is renewed and the count -- which a
 *                     clock event cannot rewind -- keeps doing the bounding.
 *
 *   REPEATED          Reset() zeroes the evaluation count and NotifyDiscontinuity calls
 *   ANNOUNCEMENTS     it, so a caller re-announcing at a wedged car would rewind the
 *                     only bound that still works. The count is carried across the
 *                     Reset() instead, and the ceiling still lands.
 *
 *   RE-ANNOUNCED      VEH-007, spec S-M1: carrying is right for the ceiling and wrong for
 *   PAST THE FLOOR    the floor, because each announcement starts a new stale tail. The
 *                     floor now reads a per-arm count that every announcement restarts
 *                     (CASE 9).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleFailureSuppressionBoundTest,
	"RacingSim.Vehicle.FailureDetectionSuppressionBound",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleFailureSuppressionBoundTest::RunTest(const FString& Parameters)
{
	constexpr double Step = 1.0 / 60.0;
	const FVehicleFailureThresholds Thresholds;
	const FVehicleTelemetrySnapshot Base = MakeHealthyFailureSnapshot(10.0, 1);

	// Past the budget, and DERIVED from it -- worth being honest about, because it means
	// this number pins nothing on its own. A budget ten times longer would still be
	// cleared by a loop computed from it, so "the flag went false" is not by itself
	// evidence that suppression is bounded anywhere useful. Two other things do that work:
	// the behavioural probe below, which asserts the detector actually starts reporting
	// again, and the below-budget case, which pins the budget from underneath so a
	// detector that suppressed nothing at all would fail. The two spare steps absorb the
	// rounding and the structural evaluation floor.
	const int32 StepsPastBudget = FMath::CeilToInt32(
		static_cast<double>(Thresholds.MaxContactSuppressionSeconds) / Step) + 2;

	// The evaluation ceiling, PINNED BY HAND because it cannot be referenced.
	// GVehicleFailureMaxContactSuppressionEvaluations lives in the anonymous namespace of
	// VehicleFailureDetection.cpp -- it is structural rather than tunable, so it is not on
	// FVehicleFailureThresholds and is not visible from this file. Hard-coding it is the
	// deliberate choice: if the constant moves, CASE 5 goes RED and names the mismatch,
	// whereas a bound discovered by looping until the basis drops would silently re-test
	// whatever the new value happened to be.
	constexpr int32 CeilingEvaluations = 240;

	// The reset these cases model: a nudge off a wall, not a teleport across the map.
	const double ShortResetCm = static_cast<double>(Thresholds.MaxContactDistanceCm) * 0.25;
	const FVector RestLocationCm = Base.LocationCm + FVector(0.0, ShortResetCm, 0.0);

	// The probe, and its geometry is the whole point of it.
	//
	// To tell "suppressed" from "reporting", a contact has to be simultaneously INSIDE the
	// bound measured from the old basis -- so bContactPredatesDiscontinuity holds and
	// suppression can swallow it -- and OUTSIDE the bound measured from the car, so that
	// once suppression ends there is something left to report. A point outside both is
	// raised either way; a point inside both is raised in neither; both would pass against
	// a detector that had stopped checking contacts entirely. The triangle inequality opens
	// the gap: the car sits ShortResetCm from the basis, so a point placed
	// 0.99 * MaxContactDistanceCm from the basis on the OPPOSITE side is
	// 0.99 * MaxContactDistanceCm + ShortResetCm from the car and clears the bound.
	const FVector ProbeContactCm = Base.LocationCm
		+ FVector(0.0, -0.99 * static_cast<double>(Thresholds.MaxContactDistanceCm), 0.0);

	// Pinned, and honest about what the pins are worth. Both distances above are fixed
	// FRACTIONS of MaxContactDistanceCm -- 0.99 of it from the basis, 0.99 + 0.25 of it
	// from the car -- so the two inequalities below hold for EVERY positive value of that
	// threshold and cannot catch a change to it. They are not tautologies about nothing:
	// they catch either derived constant being replaced by a literal, which is the
	// realistic way this geometry gets broken. But the earlier claim that they guard
	// against a threshold change was wrong, and a pin nobody can rely on is worse than no
	// pin at all.
	//
	// The assumption that is NOT structural is positivity. At zero or negative the gap
	// collapses, every distance comparison inverts or degenerates, and every case below
	// would assert nothing while staying green -- so that is asserted first and directly.
	TestTrue(TEXT("MaxContactDistanceCm is positive, which is the only thing making this a gap"),
		static_cast<double>(Thresholds.MaxContactDistanceCm) > 0.0);
	TestTrue(TEXT("The probe contact is inside the bound measured from the pre-reset basis"),
		FVector::Dist(Base.LocationCm, ProbeContactCm)
			<= static_cast<double>(Thresholds.MaxContactDistanceCm));
	TestTrue(TEXT("The probe contact is outside the bound measured from where the car ends up"),
		FVector::Dist(RestLocationCm, ProbeContactCm)
			> static_cast<double>(Thresholds.MaxContactDistanceCm));

	// Stationary, deliberately. These cases are about the CONTACT half of the snapshot; a
	// car that kept moving would drag its own contacts out of the basis within about twenty
	// steps at the fixture's 100 km/h, ending the basis by the fresh-contact rule and
	// leaving a case that looks like it tests the budget but does not.
	auto MakeStalledSnapshot = [&Base, &RestLocationCm](
		const double SimTimeSeconds, const int64 CaptureIndex, const FVector& ContactCm)
	{
		FVehicleTelemetrySnapshot Snapshot = Base;
		Snapshot.TimestampSeconds = SimTimeSeconds;
		Snapshot.SimulationTimeSeconds = SimTimeSeconds;
		Snapshot.CaptureIndex = CaptureIndex;
		Snapshot.VelocityCms = FVector::ZeroVector;
		Snapshot.ForwardSpeedCms = 0.0f;
		Snapshot.LocationCm = RestLocationCm;

		for (int32 WheelIndex = 0; WheelIndex < Snapshot.NumWheels; ++WheelIndex)
		{
			Snapshot.Wheels[WheelIndex].bInContact = true;
			Snapshot.Wheels[WheelIndex].ContactPointCm = ContactCm;
		}

		return Snapshot;
	};

	{
		// -- CASE 1: contact never returns. --
		//
		// The car is reset and then stays off the ground: every wheel out of contact, for
		// good. bAnyWheelReportsFreshContact is false on every one of these evaluations, so
		// the fresh-contact rule cannot end anything, and only the time budget can.
		FVehicleFailureDetectorState State;
		State.NotifyDiscontinuity(Base.LocationCm);

		FVehicleTelemetrySnapshot Previous = Base;
		for (int32 StepIndex = 0; StepIndex < StepsPastBudget; ++StepIndex)
		{
			FVehicleTelemetrySnapshot Airborne = AdvanceHealthyFailureSnapshot(Previous, Step);
			for (int32 WheelIndex = 0; WheelIndex < Airborne.NumWheels; ++WheelIndex)
			{
				Airborne.Wheels[WheelIndex].bInContact = false;
				Airborne.Wheels[WheelIndex].ContactPointCm = FVector::ZeroVector;
			}

			RacingSim::Vehicle::EvaluateVehicleFailures(Previous, Airborne, Thresholds, State);
			Previous = Airborne;
		}

		TestFalse(TEXT("The basis does not survive its budget when contact never returns"),
			State.bHasPreDiscontinuityLocation);

		// The behaviour the flag stands for, asserted separately: a genuinely impossible
		// contact NEAR THE OLD POSE -- the one the stale basis would have swallowed -- is
		// raised again. Placed at the old pose deliberately; anywhere else would pass even
		// with the basis still armed and would prove nothing.
		FVehicleTelemetrySnapshot BadContact = AdvanceHealthyFailureSnapshot(Previous, Step);
		for (int32 WheelIndex = 0; WheelIndex < BadContact.NumWheels; ++WheelIndex)
		{
			BadContact.Wheels[WheelIndex].bInContact = true;
			BadContact.Wheels[WheelIndex].ContactPointCm = Base.LocationCm + FVector(0.0, 0.0, -40.0);
		}

		const FVehicleFailureReport Report = RacingSim::Vehicle::EvaluateVehicleFailures(
			Previous, BadContact, Thresholds, State);
		TestTrue(TEXT("An impossible contact at the pre-reset pose is raised once the budget expires"),
			Report.Has(EVehicleFailureFlag::InvalidContact));
	}

	{
		// -- CASE 2: the reset moved the car LESS than MaxContactDistanceCm. --
		//
		// A nudge off a wall, not a teleport across the map. Every contact the car produces
		// afterwards is within MaxContactDistanceCm of the pose it left, so
		// bContactPredatesDiscontinuity is true for all of them for ever and no contact is
		// ever fresh. This is the case that hid best: nothing looks wrong, the car is
		// driving normally, and the detector has quietly stopped checking contacts.
		//
		// What this case does NOT do, stated because the suite is easier to trust when its
		// gaps are written down: it does not discriminate against the cycle-1 detector. The
		// clock advances normally here, so a detector bounded by time alone expires this
		// basis on exactly the same evaluation. CASE 4 through CASE 7 are the cases that
		// separate the two; this one pins the ordinary path that they do not cover.
		FVehicleFailureDetectorState State;
		State.NotifyDiscontinuity(Base.LocationCm);

		FVehicleTelemetrySnapshot Nudged = AdvanceHealthyFailureSnapshot(Base, Step);
		Nudged.VelocityCms = FVector::ZeroVector;
		Nudged.ForwardSpeedCms = 0.0f;
		Nudged.LocationCm = RestLocationCm;
		for (int32 WheelIndex = 0; WheelIndex < Nudged.NumWheels; ++WheelIndex)
		{
			Nudged.Wheels[WheelIndex].bInContact = true;
			Nudged.Wheels[WheelIndex].ContactPointCm = Nudged.LocationCm + FVector(0.0, 0.0, -40.0);
		}

		// Pinned, not assumed. If MaxContactDistanceCm ever shrinks below the nudge, these
		// contacts start counting as fresh, the fresh-contact rule ends the basis on its
		// own, and this case would go green while testing something else entirely.
		TestTrue(TEXT("The nudged contacts still match the pre-reset basis, so only the budget can end it"),
			FVector::Dist(Base.LocationCm, Nudged.Wheels[0].ContactPointCm)
				<= static_cast<double>(Thresholds.MaxContactDistanceCm));

		FVehicleTelemetrySnapshot Previous = Base;
		FVehicleTelemetrySnapshot Current = Nudged;
		for (int32 StepIndex = 0; StepIndex < StepsPastBudget; ++StepIndex)
		{
			RacingSim::Vehicle::EvaluateVehicleFailures(Previous, Current, Thresholds, State);
			Previous = Current;

			Current = AdvanceHealthyFailureSnapshot(Previous, Step);
			for (int32 WheelIndex = 0; WheelIndex < Current.NumWheels; ++WheelIndex)
			{
				Current.Wheels[WheelIndex].bInContact = true;
				Current.Wheels[WheelIndex].ContactPointCm = Current.LocationCm + FVector(0.0, 0.0, -40.0);
			}
		}

		TestFalse(TEXT("A reset shorter than MaxContactDistanceCm does not arm the basis for ever"),
			State.bHasPreDiscontinuityLocation);

		// And the behaviour the flag stands for, which is the assertion that actually carries
		// this case. The flag going false proves a bool was cleared; only the probe proves
		// the detector went back to reporting the contacts it had been swallowing.
		const FVehicleTelemetrySnapshot Probe = MakeStalledSnapshot(
			Previous.SimulationTimeSeconds + Step, Previous.CaptureIndex + 1, ProbeContactCm);

		const FVehicleFailureReport Report = RacingSim::Vehicle::EvaluateVehicleFailures(
			Previous, Probe, Thresholds, State);
		TestTrue(TEXT("An impossible contact near the pre-reset pose is raised again once a short reset's budget expires"),
			Report.Has(EVehicleFailureFlag::InvalidContact));
	}

	{
		// -- CASE 3: the other direction. Below the budget, suppression is still on. --
		//
		// Without this, every assertion above is also satisfied by a detector that dropped
		// the basis on its very first evaluation and suppressed nothing at all -- which is
		// the bug the suppression was written to fix, so a suite that cannot tell that apart
		// from the fix is not testing the fix. Five evaluations in: 0.083 s of simulated
		// time, well inside the budget, and past the structural evaluation floor, so it is
		// genuinely the budget being pinned here and not the floor.
		//
		// Same caveat as CASE 2: a detector bounded by time alone also passes this. It is
		// here to stop the suite being satisfied by a detector that suppressed nothing at
		// all, not to tell the two bounding schemes apart.
		constexpr int32 StepsBelowBudget = 5;

		TestTrue(TEXT("The below-budget loop, probe included, really is below the budget"),
			static_cast<double>(StepsBelowBudget + 1) * Step
				< static_cast<double>(Thresholds.MaxContactSuppressionSeconds));

		FVehicleFailureDetectorState State;
		State.NotifyDiscontinuity(Base.LocationCm);

		FVehicleTelemetrySnapshot Previous = Base;
		for (int32 StepIndex = 0; StepIndex < StepsBelowBudget; ++StepIndex)
		{
			const FVehicleTelemetrySnapshot Current = MakeStalledSnapshot(
				Base.SimulationTimeSeconds + static_cast<double>(StepIndex + 1) * Step,
				Base.CaptureIndex + StepIndex + 1,
				ProbeContactCm);

			RacingSim::Vehicle::EvaluateVehicleFailures(Previous, Current, Thresholds, State);
			Previous = Current;
		}

		TestTrue(TEXT("Below the budget the basis is still armed"),
			State.bHasPreDiscontinuityLocation);

		const FVehicleTelemetrySnapshot Probe = MakeStalledSnapshot(
			Previous.SimulationTimeSeconds + Step, Previous.CaptureIndex + 1, ProbeContactCm);

		const FVehicleFailureReport Report = RacingSim::Vehicle::EvaluateVehicleFailures(
			Previous, Probe, Thresholds, State);
		TestFalse(TEXT("Below the budget the same probe is still suppressed"),
			Report.Has(EVehicleFailureFlag::InvalidContact));
	}

	{
		// -- CASE 4: a single frame longer than the entire budget. --
		//
		// The case a time budget cannot handle alone, and the reason the basis is bounded by
		// an evaluation count as well. The stale tail this suppression covers is measured in
		// CAPTURES -- two of them -- while the budget is measured in SIMULATED TIME, and a
		// frame can be arbitrarily long: a reset into a cell that then streams in produces a
		// single step longer than the whole budget. A time bound on its own expires the basis
		// on the very evaluation that still needs it, and raises the false InvalidContact
		// this suppression exists to prevent -- at the worst possible moment, right after a
		// recovery, which is when an operator is least able to tell a spurious report from a
		// real one.
		constexpr double LongFrameSeconds = 2.0;
		constexpr int32 StaleTailCaptures = 2;

		TestTrue(TEXT("The long frame really is longer than the whole budget"),
			LongFrameSeconds > static_cast<double>(Thresholds.MaxContactSuppressionSeconds));

		FVehicleFailureDetectorState State;
		State.NotifyDiscontinuity(Base.LocationCm);

		// The evaluation that spans the reset itself. Suppressed by bDiscontinuityPending
		// rather than by the basis, so it proves nothing on its own -- it is here to get the
		// state machine past the straddling evaluation and into the tail, which is where the
		// basis is the only thing still suppressing anything.
		FVehicleTelemetrySnapshot Previous = Base;
		FVehicleTelemetrySnapshot Current = MakeStalledSnapshot(
			Base.SimulationTimeSeconds + Step, Base.CaptureIndex + 1, ProbeContactCm);
		RacingSim::Vehicle::EvaluateVehicleFailures(Previous, Current, Thresholds, State);
		Previous = Current;

		for (int32 TailIndex = 0; TailIndex < StaleTailCaptures; ++TailIndex)
		{
			Current = MakeStalledSnapshot(
				Previous.SimulationTimeSeconds + LongFrameSeconds,
				Previous.CaptureIndex + 1,
				ProbeContactCm);

			const FVehicleFailureReport TailReport = RacingSim::Vehicle::EvaluateVehicleFailures(
				Previous, Current, Thresholds, State);
			TestFalse(
				*FString::Printf(
					TEXT("A frame longer than the budget does not expire the basis on stale capture %d"),
					TailIndex),
				TailReport.Has(EVehicleFailureFlag::InvalidContact));

			Previous = Current;
		}

		// And the floor is a floor, not an exemption. One evaluation past it and the budget
		// -- long since exceeded -- takes effect, so a stopped or crawling clock cannot buy
		// unbounded suppression by arriving in very few, very long steps.
		Current = MakeStalledSnapshot(
			Previous.SimulationTimeSeconds + LongFrameSeconds,
			Previous.CaptureIndex + 1,
			ProbeContactCm);

		const FVehicleFailureReport Report = RacingSim::Vehicle::EvaluateVehicleFailures(
			Previous, Current, Thresholds, State);
		TestTrue(TEXT("Past the evaluation floor the exceeded budget expires the basis after all"),
			Report.Has(EVehicleFailureFlag::InvalidContact));
	}

	{
		// -- CASE 5: the simulated clock stops. --
		//
		// The hole a seconds budget cannot see at all, and the reason there is an evaluation
		// ceiling. If SimulationTimeSeconds stops advancing -- a paused or single-stepped
		// simulation, a stalled physics thread, a clock a caller has pinned -- then
		// SuppressedForSeconds is 0.0 on every evaluation for ever. It is never negative, so
		// nothing re-stamps; it never exceeds the budget, so nothing expires. A detector
		// bounded by time alone then swallows genuine InvalidContact near that one pose for
		// the whole remaining session and never says why. That is the same unbounded
		// suppression CASE 1 and CASE 2 exist to close, reached through the clock.
		//
		// This case is RED against the cycle-1 detector: there the basis is still armed at
		// evaluation 240 and the probe is still swallowed.
		FVehicleFailureDetectorState State;
		State.NotifyDiscontinuity(Base.LocationCm);

		// Every snapshot carries the SAME simulated time. CaptureIndex still advances, so
		// these are genuinely distinct captures rather than one sample delivered repeatedly
		// -- the detector is being told the clock stopped, not that nothing arrived.
		const double StoppedSimSeconds = Base.SimulationTimeSeconds;

		FVehicleTelemetrySnapshot Previous = Base;
		bool bReportedBeforeTheCeiling = false;
		for (int32 EvaluationIndex = 1; EvaluationIndex < CeilingEvaluations; ++EvaluationIndex)
		{
			const FVehicleTelemetrySnapshot Current = MakeStalledSnapshot(
				StoppedSimSeconds, Base.CaptureIndex + EvaluationIndex, ProbeContactCm);

			const FVehicleFailureReport StepReport = RacingSim::Vehicle::EvaluateVehicleFailures(
				Previous, Current, Thresholds, State);
			bReportedBeforeTheCeiling |= StepReport.Has(EVehicleFailureFlag::InvalidContact);

			Previous = Current;
		}

		// Accumulated rather than asserted per step, so a break reports once with a name
		// that can be read instead of 239 times with one that cannot.
		TestFalse(TEXT("A stopped clock keeps the probe suppressed on every evaluation below the ceiling"),
			bReportedBeforeTheCeiling);
		TestTrue(TEXT("One evaluation below the ceiling a stopped clock still has the basis armed"),
			State.bHasPreDiscontinuityLocation);

		const FVehicleTelemetrySnapshot Probe = MakeStalledSnapshot(
			StoppedSimSeconds, Base.CaptureIndex + CeilingEvaluations, ProbeContactCm);

		const FVehicleFailureReport Report = RacingSim::Vehicle::EvaluateVehicleFailures(
			Previous, Probe, Thresholds, State);
		TestTrue(TEXT("At the ceiling evaluation a stopped clock stops buying suppression"),
			Report.Has(EVehicleFailureFlag::InvalidContact));
		TestFalse(TEXT("...and the basis is dropped with it, so the ceiling is not re-paid every evaluation"),
			State.bHasPreDiscontinuityLocation);
	}

	{
		// -- CASE 6: the simulated clock goes non-finite. --
		//
		// A NaN clock cannot bound anything, so the basis is HELD and the ceiling is left to
		// be its bound. What the holding is for is the point of the case: the same
		// evaluation already raises NonFiniteState, which names exactly what is wrong, and
		// dropping the basis here as well would stack a second InvalidContact beside it out
		// of contact geometry that is merely stale. Two flags, one of them misleading, at
		// the moment an operator most needs the report to be readable.
		//
		// This case is RED against the cycle-1 detector, which dropped the basis on a
		// non-finite clock and produced exactly that doubled report.
		FVehicleFailureDetectorState State;
		State.NotifyDiscontinuity(Base.LocationCm);

		// Evaluation 1, the straddling one. Suppressed by bDiscontinuityPending rather than
		// by the basis, so the corrupt frame below lands on an evaluation where the basis is
		// the only thing still suppressing anything.
		FVehicleTelemetrySnapshot Previous = Base;
		const FVehicleTelemetrySnapshot Straddle = MakeStalledSnapshot(
			Base.SimulationTimeSeconds + Step, Base.CaptureIndex + 1, ProbeContactCm);
		RacingSim::Vehicle::EvaluateVehicleFailures(Previous, Straddle, Thresholds, State);
		Previous = Straddle;

		FVehicleTelemetrySnapshot Corrupt = MakeStalledSnapshot(
			Previous.SimulationTimeSeconds + Step, Previous.CaptureIndex + 1, ProbeContactCm);
		Corrupt.SimulationTimeSeconds = std::numeric_limits<double>::quiet_NaN();

		const FVehicleFailureReport Report = RacingSim::Vehicle::EvaluateVehicleFailures(
			Previous, Corrupt, Thresholds, State);
		TestTrue(TEXT("A non-finite simulated clock is reported as NonFiniteState"),
			Report.Has(EVehicleFailureFlag::NonFiniteState));
		TestFalse(TEXT("...and does not also raise a stale InvalidContact beside it"),
			Report.Has(EVehicleFailureFlag::InvalidContact));
		TestTrue(TEXT("...because the basis is held rather than dropped, leaving the ceiling as its bound"),
			State.bHasPreDiscontinuityLocation);
	}

	{
		// -- CASE 7: the simulated clock steps backwards. --
		//
		// The clock only runs backwards when it has been re-based under the detector: a
		// replay scrub, a rewind, a fresh session reusing a live state. The STAMP from the
		// old timeline is dead and cannot bound anything. The BASIS is not what went wrong,
		// and it is still exactly as young as its evaluation count says it is, so expiring
		// it on a clock event is the same early-expiry bug the evaluation floor exists to
		// prevent -- a rewind landing inside the stale tail would raise the false
		// InvalidContact this suppression was written to stop. Re-stamping instead keeps the
		// suppression and hands the bounding job to the count, which a clock event cannot
		// rewind. CASE 5 is what stops that from becoming unbounded.
		//
		// This case is RED against the cycle-1 detector on its very first backwards step,
		// which expired the basis on the negative delta.
		constexpr int32 BackwardsSteps = 10;
		constexpr double BackwardsStepSeconds = 5.0;

		// Pinned, and this one is not a tautology: if a single rewind were SMALLER than the
		// budget the case would still pass while proving much less, because nothing would
		// have been rewound past the point where the budget could have expired anything.
		TestTrue(TEXT("Each backwards step is larger than the whole suppression budget"),
			BackwardsStepSeconds > static_cast<double>(Thresholds.MaxContactSuppressionSeconds));

		FVehicleFailureDetectorState State;
		State.NotifyDiscontinuity(Base.LocationCm);

		FVehicleTelemetrySnapshot Previous = Base;
		FVehicleTelemetrySnapshot Current = MakeStalledSnapshot(
			Base.SimulationTimeSeconds + Step, Base.CaptureIndex + 1, ProbeContactCm);
		RacingSim::Vehicle::EvaluateVehicleFailures(Previous, Current, Thresholds, State);
		Previous = Current;

		bool bReportedWhileRewinding = false;
		for (int32 StepIndex = 0; StepIndex < BackwardsSteps; ++StepIndex)
		{
			// CaptureIndex still advances. TIME is what runs backwards, not the recording; a
			// capture counter that rewound as well would be a different fault entirely and
			// would end the basis by a different rule.
			Current = MakeStalledSnapshot(
				Previous.SimulationTimeSeconds - BackwardsStepSeconds,
				Previous.CaptureIndex + 1,
				ProbeContactCm);

			const FVehicleFailureReport StepReport = RacingSim::Vehicle::EvaluateVehicleFailures(
				Previous, Current, Thresholds, State);
			bReportedWhileRewinding |= StepReport.Has(EVehicleFailureFlag::InvalidContact);

			Previous = Current;
		}

		TestFalse(TEXT("A backwards clock does not expire the basis into a false contact report"),
			bReportedWhileRewinding);
		TestTrue(TEXT("A backwards clock leaves the basis armed, re-stamped rather than expired"),
			State.bHasPreDiscontinuityLocation);

		// And it is the re-stamping holding it, not some other bound that merely has not run
		// out yet. Well below the ceiling, so CASE 5 is not what this case is re-testing.
		TestTrue(TEXT("The rewind stayed far below the evaluation ceiling"),
			State.PreDiscontinuityEvaluations < CeilingEvaluations);

		const FVehicleTelemetrySnapshot Probe = MakeStalledSnapshot(
			Previous.SimulationTimeSeconds - BackwardsStepSeconds,
			Previous.CaptureIndex + 1,
			ProbeContactCm);

		const FVehicleFailureReport Report = RacingSim::Vehicle::EvaluateVehicleFailures(
			Previous, Probe, Thresholds, State);
		TestFalse(TEXT("...and the probe it had been suppressing stays suppressed"),
			Report.Has(EVehicleFailureFlag::InvalidContact));
	}

	{
		// -- CASE 8: the same discontinuity announced over and over. --
		//
		// The hole the evaluation bound opens in itself, and the only behavioural cover the
		// repair cycle 3 carry has. NotifyDiscontinuity() calls Reset(), and Reset() zeroes
		// PreDiscontinuityEvaluations -- correct for a session restart, wrong here. A caller
		// that re-announces while the car is still wedged would rewind the count on every
		// announcement, so neither the floor nor the ceiling could ever be reached and the
		// basis would stay armed for the rest of the session. That is CASE 5's unbounded
		// suppression reached through the front door rather than through a stopped clock,
		// and carrying the count across the Reset() is what closes it.
		//
		// Nothing else in the suite covers this. Every other case announces exactly once,
		// so a detector that rewound the count on re-announcement would pass all of them.
		//
		// The clock is stopped for the same reason CASE 5 stops it: with the time budget
		// unable to expire anything, the evaluation count is the only bound left, so what
		// this case measures is the count and nothing else.
		//
		// The re-announcement carries the SAME pose deliberately. The motivating caller is
		// an auto-recover firing repeatedly at a car that is not moving, so the pose it
		// leaves behind is the pose it left behind last time; it also keeps the probe
		// geometry above valid across every announcement, which a moving basis would not.
		constexpr int32 EvaluationsPerAnnouncement = 3;

		FVehicleFailureDetectorState State;
		State.NotifyDiscontinuity(Base.LocationCm);

		const double StoppedSimSeconds = Base.SimulationTimeSeconds;

		FVehicleTelemetrySnapshot Previous = Base;
		int32 Announcements = 1;
		bool bEveryAnnouncementLanded = true;
		bool bReportedBeforeTheCeiling = false;
		for (int32 EvaluationIndex = 1; EvaluationIndex < CeilingEvaluations; ++EvaluationIndex)
		{
			const FVehicleTelemetrySnapshot Current = MakeStalledSnapshot(
				StoppedSimSeconds, Base.CaptureIndex + EvaluationIndex, ProbeContactCm);

			const FVehicleFailureReport StepReport = RacingSim::Vehicle::EvaluateVehicleFailures(
				Previous, Current, Thresholds, State);
			bReportedBeforeTheCeiling |= StepReport.Has(EVehicleFailureFlag::InvalidContact);

			Previous = Current;

			// Announced AFTER the evaluation, so the last announcement follows evaluation
			// 237: evaluation 238 consumes the one-evaluation bDiscontinuityPending latch and
			// evaluation 239 is suppressed by the BASIS alone. The latch is spent before the
			// ceiling probe, so the ceiling assertion below tests the count rather than the
			// latch. Any spacing of two or more keeps that true.
			if (EvaluationIndex % EvaluationsPerAnnouncement == 0)
			{
				State.NotifyDiscontinuity(Base.LocationCm);
				++Announcements;

				// Each announcement must actually LAND. A NotifyDiscontinuity that did nothing
				// while a basis was already armed would still let the count reach the ceiling,
				// and every other assertion in this case would pass as a copy of CASE 5. An
				// evaluation has consumed the latch and stamped the arm time since the last
				// announcement, so a no-op leaves the latch clear and the stamp set, and a real
				// announcement re-arms the one and clears the other. Accumulated so a break
				// reports once rather than eighty times.
				bEveryAnnouncementLanded &= State.bDiscontinuityPending
					&& !State.bHasPreDiscontinuityArmTime
					&& State.bHasPreDiscontinuityLocation;
			}
		}

		// Pinned so the case cannot quietly degenerate into a second copy of CASE 5: if the
		// loop ever stops re-announcing, this is what says so.
		TestTrue(TEXT("The basis really was re-announced many times over the run"),
			Announcements > 2);
		TestTrue(TEXT("Every re-announcement re-armed the latch and cleared the arm time"),
			bEveryAnnouncementLanded);

		// The direct statement of the carry, and the assertion that goes RED against a
		// detector that rewinds on re-announcement -- there the count is bounded by
		// EvaluationsPerAnnouncement and never approaches the ceiling.
		TestEqual(TEXT("Re-announcement carries the evaluation count rather than rewinding it"),
			State.PreDiscontinuityEvaluations, CeilingEvaluations - 1);

		TestFalse(TEXT("Re-announcement keeps the probe suppressed on every evaluation below the ceiling"),
			bReportedBeforeTheCeiling);
		TestTrue(TEXT("One evaluation below the ceiling the re-announced basis is still armed"),
			State.bHasPreDiscontinuityLocation);

		const FVehicleTelemetrySnapshot Probe = MakeStalledSnapshot(
			StoppedSimSeconds, Base.CaptureIndex + CeilingEvaluations, ProbeContactCm);

		const FVehicleFailureReport Report = RacingSim::Vehicle::EvaluateVehicleFailures(
			Previous, Probe, Thresholds, State);
		TestTrue(TEXT("Re-announcing cannot buy suppression past the evaluation ceiling"),
			Report.Has(EVehicleFailureFlag::InvalidContact));
		TestFalse(TEXT("...and the basis is dropped at the ceiling however many announcements paid into it"),
			State.bHasPreDiscontinuityLocation);
	}

	{
		// -- CASE 9: a re-announcement after the carried count has passed the floor. --
		//
		// VEH-007, spec S-M1. CASE 8's carry is right for the ceiling and was wrong for the
		// floor: while one counter did both jobs, a basis re-announced after that counter
		// had climbed past GVehicleFailureMinContactSuppressionEvaluations started its NEW
		// stale tail already past the floor. A single long frame inside that tail -- CASE 4's
		// hazard, reached through a second announcement instead of a first -- then expired
		// the basis and raised a false Error-level InvalidContact on a stationary car.
		//
		// This is CASE 4 run a second time on a state that has already been announced and
		// evaluated past the floor. It is RED against the single-counter detector on the
		// first stale capture.
		constexpr int32 EvaluationsBeforeReannouncement = 10;
		constexpr int32 FloorEvaluations = 3;
		constexpr double LongFrameSeconds = 2.0;
		constexpr int32 StaleTailCaptures = 2;

		TestTrue(TEXT("CASE 9's long frame really is longer than the whole budget"),
			LongFrameSeconds > static_cast<double>(Thresholds.MaxContactSuppressionSeconds));

		FVehicleFailureDetectorState State;
		State.NotifyDiscontinuity(Base.LocationCm);

		// Clock stopped for the first announcement, so neither the budget nor fresh contact
		// can end the basis and the count climbs past the floor while it stays armed.
		const double StoppedSimSeconds = Base.SimulationTimeSeconds;
		FVehicleTelemetrySnapshot Previous = Base;
		for (int32 EvaluationIndex = 1; EvaluationIndex <= EvaluationsBeforeReannouncement; ++EvaluationIndex)
		{
			const FVehicleTelemetrySnapshot Current = MakeStalledSnapshot(
				StoppedSimSeconds, Base.CaptureIndex + EvaluationIndex, ProbeContactCm);
			RacingSim::Vehicle::EvaluateVehicleFailures(Previous, Current, Thresholds, State);
			Previous = Current;
		}

		// Pinned: without these the case could pass with the count still below the floor,
		// which is not the state S-M1 describes.
		TestTrue(TEXT("Before re-announcement the basis is still armed"),
			State.bHasPreDiscontinuityLocation);
		TestTrue(TEXT("Before re-announcement the carried count is past the evaluation floor"),
			State.PreDiscontinuityEvaluations > FloorEvaluations);

		State.NotifyDiscontinuity(Base.LocationCm);

		TestEqual(TEXT("Re-announcement carries the ceiling count"),
			State.PreDiscontinuityEvaluations, EvaluationsBeforeReannouncement);
		TestEqual(TEXT("Re-announcement restarts the floor count"),
			State.PreDiscontinuityArmEvaluations, 0);

		// The straddling evaluation, suppressed by the latch. It also stamps the arm time,
		// so every capture after it is measured against a live time budget.
		FVehicleTelemetrySnapshot Current = MakeStalledSnapshot(
			Previous.SimulationTimeSeconds + Step, Previous.CaptureIndex + 1, ProbeContactCm);
		RacingSim::Vehicle::EvaluateVehicleFailures(Previous, Current, Thresholds, State);
		Previous = Current;

		for (int32 TailIndex = 0; TailIndex < StaleTailCaptures; ++TailIndex)
		{
			Current = MakeStalledSnapshot(
				Previous.SimulationTimeSeconds + LongFrameSeconds,
				Previous.CaptureIndex + 1,
				ProbeContactCm);

			const FVehicleFailureReport TailReport = RacingSim::Vehicle::EvaluateVehicleFailures(
				Previous, Current, Thresholds, State);
			TestFalse(
				*FString::Printf(
					TEXT("A re-announced basis is not expired by a long frame on stale capture %d"),
					TailIndex),
				TailReport.Has(EVehicleFailureFlag::InvalidContact));

			Previous = Current;
		}

		// And the restarted floor is still a floor: one evaluation past it the long-exceeded
		// budget takes effect, exactly as in CASE 4.
		Current = MakeStalledSnapshot(
			Previous.SimulationTimeSeconds + LongFrameSeconds,
			Previous.CaptureIndex + 1,
			ProbeContactCm);

		const FVehicleFailureReport Report = RacingSim::Vehicle::EvaluateVehicleFailures(
			Previous, Current, Thresholds, State);
		TestTrue(TEXT("Past the restarted floor the exceeded budget expires the re-announced basis"),
			Report.Has(EVehicleFailureFlag::InvalidContact));
		TestFalse(TEXT("...and the basis is dropped with it"),
			State.bHasPreDiscontinuityLocation);
	}

	{
		// -- Reset() clears an armed announcement. --
		//
		// Reset() is documented as dropping ALL accumulated history, and an armed
		// one-evaluation suppression is history: without this, a caller using Reset() for a
		// session restart carried a suppression across it and lost its first evaluation.
		// NotifyDiscontinuity() calls Reset() FIRST and re-arms afterwards, which the two
		// cases above already rely on, so clearing here cannot disarm an announcement.
		FVehicleTelemetrySnapshot Teleported = AdvanceHealthyFailureSnapshot(Base, Step);
		Teleported.LocationCm = Base.LocationCm + FVector(100000.0, 0.0, 0.0);

		FVehicleFailureDetectorState State;
		State.NotifyDiscontinuity();
		State.Reset();

		const FVehicleFailureReport Report = RacingSim::Vehicle::EvaluateVehicleFailures(
			Base, Teleported, Thresholds, State);
		TestTrue(TEXT("Reset() after NotifyDiscontinuity() clears the pending suppression"),
			Report.Has(EVehicleFailureFlag::Tunnelling));
	}

	return true;
}
