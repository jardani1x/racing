// Copyright RacingSim. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Vehicle/VehicleManoeuvreFixture.h"

#include "RacingSimTestsLog.h"

/**
 * VEH-006: the car is actually driveable.
 *
 * ---------------------------------------------------------------------------
 * What this suite proves that nothing before it proved
 * ---------------------------------------------------------------------------
 *
 * VEH-001 through VEH-005 tested pure functions: input shaping, chassis derivation,
 * tune application, telemetry evaluation over a hand-built snapshot, reset arithmetic.
 * Every one of those specs would still pass if the shaped command were never handed to
 * Chaos at all, or handed to it with throttle and brake swapped. The chain
 *
 *     raw device sample
 *       -> UVehicleInputComponent::PendingSample
 *       -> FVehicleInputProcessor::Tick
 *       -> FVehicleInputCommand
 *       -> RacingSim::Vehicle::MapCommandToChaosInput
 *       -> UChaosWheeledVehicleMovementComponent::SetThrottleInput/...
 *       -> the solver
 *       -> the car moves
 *
 * has never been executed end to end. These four tests execute it, and they assert on
 * the far end of it -- displacement, speed and yaw rate of a rigid body -- rather than
 * on any intermediate value, because an assertion on an intermediate value is an
 * assertion the wiring can be broken beneath.
 *
 * ---------------------------------------------------------------------------
 * Why the thresholds are loose, and why that is correct rather than lazy
 * ---------------------------------------------------------------------------
 *
 * These are SIGN AND ORDER-OF-MAGNITUDE assertions, not golden values. A vehicle-dynamics
 * suite that pinned "3 s of full throttle gives 812.4 cm/s" would fail on every future
 * tune change, engine upgrade and solver-iteration tweak -- and the natural repair for
 * such a failure is to edit the expected number, which converts the suite into a record
 * of what the code currently does rather than a test of what it should do.
 *
 * What is asserted instead is the set of properties that must hold for ANY drivable
 * configuration: throttle moves the car forward, brake removes speed, right steering
 * yaws right, and halving the timestep does not change where the car ends up. Those are
 * the four ways this wiring can be wrong, and none of them is tune-dependent.
 *
 * Every test logs the numbers it measured at Display severity, so a run that fails
 * carries the evidence needed to tell "the wiring is broken" from "the threshold was
 * badly chosen" without a second instrumented run.
 *
 * ---------------------------------------------------------------------------
 * Filter phase
 * ---------------------------------------------------------------------------
 *
 * ProductFilter, deliberately. These tests build actors and a world, which is impossible
 * at the Smoke gate: FEngineLoop::PreInit runs SmokeFilter tests before
 * RegisterEngineElements() (see Docs/Environment.md and VehicleInputProcessor.h's file
 * comment), so a smoke-phase actor kills the process with no index.json at all.
 */

namespace VehicleManoeuvrePrivate
{
	/** The fixed step everything here replays at, except the frame-rate independence test. */
	constexpr float StepSeconds = 1.0f / 60.0f;

	/** 3 s of throttle. Long enough to leave the noise of the first few suspension frames behind. */
	constexpr int32 AccelerateSteps = 180;

	/** 2 s of brake. Long enough to stop a car that has had 3 s of throttle. */
	constexpr int32 BrakeSteps = 120;

	/** 2 s of steering. Long enough for a steady-state yaw rate to establish. */
	constexpr int32 SteerSteps = 120;

	/**
	 * Minimum forward speed after AccelerateSteps of full throttle, cm/s.
	 *
	 * 100 cm/s is 3.6 km/h -- walking pace. Any car that is receiving throttle at all
	 * beats it by an order of magnitude after three seconds; a car whose throttle is not
	 * reaching the solver sits at 0. The gap between those two outcomes is what is being
	 * measured, so the threshold sits in the middle of nothing.
	 */
	constexpr double MinAcceleratedSpeedCms = 100.0;

	/** Minimum forward displacement over the same window, cm. Same reasoning as the speed floor. */
	constexpr double MinAcceleratedDistanceCm = 100.0;

	/**
	 * A braked car must end below this fraction of its entry speed.
	 *
	 * Not "must reach zero": a stopped car may still report a few cm/s of suspension-driven
	 * noise, and asserting exact zero would make the test a coin flip on the final frame.
	 * Half of entry speed cannot be reached by coasting drag alone in 2 s, so the threshold
	 * still distinguishes braking from not braking.
	 */
	constexpr double BrakedSpeedFraction = 0.5;

	/** Minimum |yaw rate| under full lock, degrees/s. A straight-running car reports ~0. */
	constexpr double MinYawRateDegreesPerSecond = 1.0;

	/**
	 * Allowed disagreement between the 1/60 s and 1/120 s runs, as a fraction of the
	 * 1/60 s distance.
	 *
	 * NOT zero, and it cannot be. Chaos integrates on a fixed internal substep and
	 * different frame steps land on different substep boundaries, so two runs of the same
	 * manoeuvre are near-identical rather than bit-identical. 25 % is comfortably tighter
	 * than the failure this guards against: a system that scaled per-FRAME instead of
	 * per-SECOND would show the 1/120 run travelling about twice as far, i.e. 100 % apart.
	 */
	constexpr double FrameRateTolerance = 0.25;
}

/**
 * Full throttle from rest moves the car forward.
 *
 * The first test in this project to prove that input reaches the solver at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleManoeuvreAccelerationTest,
	"RacingSim.Vehicle.Manoeuvre.Acceleration",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FVehicleManoeuvreAccelerationTest::RunTest(const FString& Parameters)
{
	using namespace VehicleManoeuvrePrivate;

	FVehicleManoeuvreFixture Fixture;
	if (!Fixture.Setup(*this))
	{
		return false;
	}

	const FVector StartLocation = Fixture.GetLocation();
	const double StartSpeedCms = Fixture.GetForwardSpeedCms();

	Fixture.Drive(FVehicleManoeuvreFixture::ThrottleSample(1.0), AccelerateSteps, StepSeconds);

	const FVector EndLocation = Fixture.GetLocation();
	const double EndSpeedCms = Fixture.GetForwardSpeedCms();
	const double ForwardDistanceCm = EndLocation.X - StartLocation.X;

	const FVehicleInputCommand FinalCommand = Fixture.GetLastCommand();

	UE_LOG(LogRacingTests, Display,
		TEXT("Acceleration: %.2f s of full throttle. Speed %.2f -> %.2f cm/s, forward travel %.2f cm, lateral drift %.2f cm."),
		AccelerateSteps * StepSeconds, StartSpeedCms, EndSpeedCms, ForwardDistanceCm, EndLocation.Y - StartLocation.Y);

	// Splits "the input layer produced nothing" from "the input layer produced a throttle
	// that the solver ignored" -- two failures with identical symptoms at the car.
	UE_LOG(LogRacingTests, Display,
		TEXT("Acceleration: shaped command throttle %.3f, brake %.3f, steer %.3f; end Z %.2f cm, target gear %d, current gear %d, engine %.1f rpm."),
		FinalCommand.Throttle, FinalCommand.Brake, FinalCommand.Steer,
		EndLocation.Z, Fixture.GetMovement()->GetTargetGear(),
		Fixture.GetMovement()->GetCurrentGear(), Fixture.GetMovement()->GetEngineRotationSpeed());

	// The physics thread's own per-wheel view, which the two lines above cannot give: the
	// game-thread gear reported there disagreed with the physics transmission's actual gear
	// during this ticket's investigation, so game-thread state alone is not evidence. See
	// FVehicleManoeuvreFixture::LogDrivetrain for what each combination of DriveTorque,
	// BrakeTorque and AngularVelocity rules in and out.
	Fixture.LogDrivetrain(TEXT("after full throttle"));

	TestFalse(TEXT("The car's position is finite after accelerating"), EndLocation.ContainsNaN());

	TestTrue(
		FString::Printf(TEXT("Full throttle reached at least %.0f cm/s (measured %.2f cm/s)"),
			MinAcceleratedSpeedCms, EndSpeedCms),
		EndSpeedCms > MinAcceleratedSpeedCms);

	TestTrue(
		FString::Printf(TEXT("Full throttle moved the car at least %.0f cm forward (measured %.2f cm)"),
			MinAcceleratedDistanceCm, ForwardDistanceCm),
		ForwardDistanceCm > MinAcceleratedDistanceCm);

	Fixture.Teardown();
	return true;
}

/**
 * Braking removes speed the throttle put in.
 *
 * Runs the acceleration manoeuvre first so the brake has something to act on; asserting
 * that a stationary car stays stationary under brake would pass against a brake input
 * that was never wired up at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleManoeuvreBrakingTest,
	"RacingSim.Vehicle.Manoeuvre.Braking",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FVehicleManoeuvreBrakingTest::RunTest(const FString& Parameters)
{
	using namespace VehicleManoeuvrePrivate;

	FVehicleManoeuvreFixture Fixture;
	if (!Fixture.Setup(*this))
	{
		return false;
	}

	Fixture.Drive(FVehicleManoeuvreFixture::ThrottleSample(1.0), AccelerateSteps, StepSeconds);
	const double EntrySpeedCms = Fixture.GetForwardSpeedCms();

	// A guard, not an assertion about braking: if the car never got moving, a "braking
	// worked" result below would be meaningless rather than merely wrong.
	if (!TestTrue(
			FString::Printf(TEXT("The car was moving before the brake was applied (%.2f cm/s)"), EntrySpeedCms),
			EntrySpeedCms > MinAcceleratedSpeedCms))
	{
		Fixture.Teardown();
		return false;
	}

	Fixture.Drive(FVehicleManoeuvreFixture::BrakeSample(1.0), BrakeSteps, StepSeconds);
	const double ExitSpeedCms = Fixture.GetForwardSpeedCms();

	UE_LOG(LogRacingTests, Display,
		TEXT("Braking: %.2f s of full brake from %.2f cm/s left %.2f cm/s (%.1f%% of entry)."),
		BrakeSteps * StepSeconds, EntrySpeedCms, ExitSpeedCms,
		EntrySpeedCms != 0.0 ? 100.0 * ExitSpeedCms / EntrySpeedCms : 0.0);

	TestTrue(
		FString::Printf(TEXT("Braking cut speed below %.0f%% of entry (%.2f -> %.2f cm/s)"),
			100.0 * BrakedSpeedFraction, EntrySpeedCms, ExitSpeedCms),
		ExitSpeedCms < EntrySpeedCms * BrakedSpeedFraction);

	// The car must not be driven BACKWARDS by the brake. Chaos routes brake and reverse
	// through separate inputs, and a mapping bug that sent brake to the reverse throttle
	// would still satisfy the speed-reduction assertion above while being badly wrong.
	TestTrue(
		FString::Printf(TEXT("Braking did not reverse the car (%.2f cm/s)"), ExitSpeedCms),
		ExitSpeedCms > -MinAcceleratedSpeedCms);

	Fixture.Teardown();
	return true;
}

/**
 * Right steering yaws the car right.
 *
 * A sign test, and worth its own test because the sign is carried through three
 * conversions on the way to the solver -- device to FVehicleInputRawSample::Steer,
 * through FVehicleInputCommand's documented "positive is RIGHT" convention, then through
 * MapCommandToChaosInput -- and an even number of sign errors is invisible everywhere
 * except at the car.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleManoeuvreSteeringTest,
	"RacingSim.Vehicle.Manoeuvre.Steering",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FVehicleManoeuvreSteeringTest::RunTest(const FString& Parameters)
{
	using namespace VehicleManoeuvrePrivate;

	FVehicleManoeuvreFixture Fixture;
	if (!Fixture.Setup(*this))
	{
		return false;
	}

	// Build speed first. A stationary car under full lock produces essentially no yaw,
	// because yaw comes from lateral tyre force and lateral tyre force needs motion.
	Fixture.Drive(FVehicleManoeuvreFixture::ThrottleSample(1.0), AccelerateSteps, StepSeconds);

	const double StraightYawRate = Fixture.GetYawRateDegreesPerSecond();
	const FVector StraightLocation = Fixture.GetLocation();

	Fixture.Drive(FVehicleManoeuvreFixture::ThrottleSample(1.0, /*Steer*/ 1.0), SteerSteps, StepSeconds);

	const double RightYawRate = Fixture.GetYawRateDegreesPerSecond();
	const double LateralTravelCm = Fixture.GetLocation().Y - StraightLocation.Y;

	UE_LOG(LogRacingTests, Display,
		TEXT("Steering: yaw rate %.3f deg/s straight -> %.3f deg/s under full right lock; lateral travel %.2f cm."),
		StraightYawRate, RightYawRate, LateralTravelCm);

	TestTrue(
		FString::Printf(TEXT("Full right lock produced at least %.1f deg/s of yaw (measured %.3f deg/s)"),
			MinYawRateDegreesPerSecond, RightYawRate),
		RightYawRate > MinYawRateDegreesPerSecond);

	// Positive Y is right in Unreal's left-handed frame, so a right turn must displace the
	// car to positive Y. This catches a sign error that yaw rate alone would miss if the
	// yaw convention itself were the thing inverted.
	TestTrue(
		FString::Printf(TEXT("Full right lock moved the car to the right (+Y), measured %.2f cm"), LateralTravelCm),
		LateralTravelCm > 0.0);

	Fixture.Teardown();
	return true;
}

/**
 * The car goes the same distance whether it is simulated at 60 or 120 steps per second.
 *
 * CLAUDE.md's "keep gameplay independent from frame rate" rule, measured rather than
 * asserted in a comment. This is the test that catches a per-frame quantity that should
 * have been per-second: such a bug is invisible on a machine holding a steady frame rate
 * and decisive on a Pixel Streaming client whose frame rate is set by a browser and a
 * network this project does not control.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleManoeuvreFrameRateIndependenceTest,
	"RacingSim.Vehicle.Manoeuvre.FrameRateIndependence",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FVehicleManoeuvreFrameRateIndependenceTest::RunTest(const FString& Parameters)
{
	using namespace VehicleManoeuvrePrivate;

	// Same SIMULATED duration in both runs, reached with different step sizes. That is the
	// whole design of the test: identical sim time, different frame counts.
	const double SimulatedSeconds = AccelerateSteps * StepSeconds;

	double DistanceCm[2] = { 0.0, 0.0 };
	double SpeedCms[2] = { 0.0, 0.0 };
	const float StepSizes[2] = { StepSeconds, StepSeconds * 0.5f };
	const int32 StepCounts[2] = { AccelerateSteps, AccelerateSteps * 2 };

	for (int32 Run = 0; Run < 2; ++Run)
	{
		// A fresh fixture per run rather than a reset of one fixture: reset correctness is
		// VEH-005's subject and has its own spec, and depending on it here would let this
		// test fail for a reason that has nothing to do with frame rate.
		FVehicleManoeuvreFixture Fixture;
		if (!Fixture.Setup(*this))
		{
			return false;
		}

		const FVector StartLocation = Fixture.GetLocation();
		Fixture.Drive(FVehicleManoeuvreFixture::ThrottleSample(1.0), StepCounts[Run], StepSizes[Run]);

		DistanceCm[Run] = Fixture.GetLocation().X - StartLocation.X;
		SpeedCms[Run] = Fixture.GetForwardSpeedCms();

		UE_LOG(LogRacingTests, Display,
			TEXT("Frame-rate independence run %d: %d steps of %.5f s (%.2f s simulated) -> %.2f cm at %.2f cm/s."),
			Run, StepCounts[Run], StepSizes[Run], StepCounts[Run] * StepSizes[Run], DistanceCm[Run], SpeedCms[Run]);
	}

	if (!TestTrue(
			FString::Printf(TEXT("The 1/60 s reference run moved the car (%.2f cm)"), DistanceCm[0]),
			DistanceCm[0] > MinAcceleratedDistanceCm))
	{
		return false;
	}

	const double DifferenceCm = FMath::Abs(DistanceCm[1] - DistanceCm[0]);
	const double RelativeDifference = DifferenceCm / DistanceCm[0];

	UE_LOG(LogRacingTests, Display,
		TEXT("Frame-rate independence: %.2f s simulated both ways, %.2f cm vs %.2f cm, %.1f%% apart (tolerance %.0f%%)."),
		SimulatedSeconds, DistanceCm[0], DistanceCm[1], 100.0 * RelativeDifference, 100.0 * FrameRateTolerance);

	TestTrue(
		FString::Printf(
			TEXT("Halving the timestep changed distance by less than %.0f%% (%.2f cm vs %.2f cm, %.1f%% apart)"),
			100.0 * FrameRateTolerance, DistanceCm[0], DistanceCm[1], 100.0 * RelativeDifference),
		RelativeDifference < FrameRateTolerance);

	return true;
}
