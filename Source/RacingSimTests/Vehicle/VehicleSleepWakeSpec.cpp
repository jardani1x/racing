// Copyright RacingSim. All Rights Reserved.

#include "VehicleManoeuvreFixture.h"

#include "ChaosWheeledVehicleMovementComponent.h"
#include "Misc/AutomationTest.h"

/**
 * VEH-010: a car the physics solver has parked must still drive away when the driver
 * asks it to.
 *
 * Found by RACE-006's end-to-end driver reset test, which could not get the car to move
 * at all after a reset: full throttle, a correctly shaped command of 1.000 reaching the
 * movement component, and a car that did not travel one centimetre in three seconds.
 *
 * The cause is a gap between two pieces of Chaos that both assume a skeletal mesh.
 * UChaosVehicleMovementComponent::ProcessSleeping wakes a vehicle whose driver is
 * pressing something, but it does so through WakeAllEnabledRigidBodies(), which walks
 * GetSkeletalMesh()->Bodies. This pawn's chassis is a UBoxComponent, so that helper
 * reaches nothing. The solver still parks the body on its own once the car is genuinely
 * at rest, and FChaosVehicleManagerAsyncCallback::OnPreSimulate_Internal then returns
 * before Simulate() because the handle is no longer Dynamic -- so no wheel force, no
 * engine torque, and no way back out.
 *
 * ARacingVehiclePawn::WakeChassisForInput closes it. This test is the regression: it
 * parks the car by leaving it alone, then drives.
 *
 * ProductFilter: real world, real physics (Docs/Environment.md).
 */
namespace VehicleSleepWakeSpecPrivate
{
	/** Neutral steps allowed for the solver to park a resting car. 10 s at 60 Hz. */
	constexpr int32 MaxIdleSteps = 600;

	/** Throttle steps after the park. The car needs seconds, not frames, to leave a standstill. */
	constexpr int32 DriveSteps = 180;

	/** Forward speed that counts as "the car actually drove away", cm/s. */
	constexpr double MovingSpeedCms = 100.0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleSleepWakeTest,
	"RacingSim.Vehicle.WakesFromSleepOnThrottle",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FVehicleSleepWakeTest::RunTest(const FString& Parameters)
{
	using namespace VehicleSleepWakeSpecPrivate;

	FVehicleManoeuvreFixture Fixture;
	if (!Fixture.Setup(*this, /*bEnableTelemetry*/ false))
	{
		return false;
	}

	// Park the car the way the product does. Measured, not assumed: idling a settled car
	// for 600 steps does not park it, and neither does writing zero into both velocities
	// -- the first two versions of this test tried exactly that and the body stayed awake.
	// What parks it is the physics-state rebuild inside ResetVehicle()
	// (UChaosVehicleMovementComponent::ResetVehicleState calls OnDestroyPhysicsState then
	// OnCreatePhysicsState, and the destroy half recreates the chassis body), which is the
	// call ARacingVehiclePawn::ExecuteSafeReset makes on every driver reset. So this test
	// makes the same call, on the movement component, with no track and no race involved.
	//
	// VEH-011, and this is load-bearing rather than incidental: ExecuteSafeReset now
	// RE-APPLIES the NeverSleep pin after ResetVehicle(), so a car reset through the pawn
	// no longer parks at all -- RacingSim.Vehicle.ResetRestoresSleepPin asserts exactly
	// that. Going through the movement component directly is what still leaves this car
	// parkable, and it is why VEH-011 deliberately put the re-apply on the pawn's reset
	// path rather than in a tick or a physics callback. Do NOT "simplify" this to
	// Pawn->ExecuteSafeReset: the car would never park, the TestFalse below would fail,
	// and if it were relaxed instead this test would silently stop proving that
	// WakeChassisForInput does anything.
	UChaosWheeledVehicleMovementComponent* Movement = Fixture.GetMovement();
	if (!TestNotNull(TEXT("The fixture exposes a movement component"), Movement))
	{
		return false;
	}
	Movement->ResetVehicle();

	int32 IdleSteps = 0;
	while (IdleSteps < MaxIdleSteps && Fixture.IsChassisAwake())
	{
		if (!Fixture.Drive(FVehicleInputRawSample(), 1))
		{
			return false;
		}
		++IdleSteps;
	}

	// The precondition, checked. A "it drove away" assertion proves nothing about waking
	// if the body was never parked, and this is the half of the test that goes stale
	// first: an engine upgrade that changes the solver's sleep policy must fail here,
	// loudly, rather than leave a test that silently stops testing anything.
	if (!TestFalse(FString::Printf(TEXT("The solver parked the resting car within %d idle steps (took %d)"),
			MaxIdleSteps, IdleSteps), Fixture.IsChassisAwake()))
	{
		Fixture.LogDrivetrain(TEXT("never parked"));
		return false;
	}

	const FVector ParkedLocationCm = Fixture.GetLocation();
	if (!Fixture.Drive(FVehicleManoeuvreFixture::ThrottleSample(1.0, 0.0), DriveSteps))
	{
		return false;
	}
	Fixture.LogDrivetrain(TEXT("after throttle from parked"));

	TestTrue(TEXT("Throttle woke the parked chassis"), Fixture.IsChassisAwake());
	TestTrue(FString::Printf(TEXT("The parked car drove away under throttle (%.2f cm/s, needs > %.1f)"),
			Fixture.GetForwardSpeedCms(), MovingSpeedCms),
		Fixture.GetForwardSpeedCms() > MovingSpeedCms);
	const double MovedCm = FVector::Dist2D(Fixture.GetLocation(), ParkedLocationCm);
	TestTrue(FString::Printf(TEXT("The parked car actually moved (%.2f cm)"), MovedCm), MovedCm > 100.0);

	TestFalse(TEXT("Every Drive step ticked"), Fixture.HasTickFailure());
	return true;
}
