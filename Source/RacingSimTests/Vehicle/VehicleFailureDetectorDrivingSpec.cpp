// Copyright RacingSim. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Vehicle/VehicleManoeuvreFixture.h"

#include "RacingSimTestsLog.h"

#include "Vehicle/VehicleFailureDetection.h"
#include "Vehicle/VehicleTelemetryTypes.h"

/**
 * VEH-006: the failure detector, against a car that is actually being simulated.
 *
 * ---------------------------------------------------------------------------
 * Why this suite exists when VehicleFailureDetectionSpec already passes
 * ---------------------------------------------------------------------------
 *
 * VEH-004's spec is thorough and every one of its cases is hand-built: it constructs
 * two FVehicleTelemetrySnapshots, hands them to EvaluateVehicleFailures, and asserts on
 * the report. That proves the DECISION FUNCTION is correct. It cannot prove the detector
 * is correctly WIRED, and it cannot prove the detector is correctly CALIBRATED, because
 * neither the capture path nor a real solver appears in it. Concretely, all of the
 * following would leave that spec green:
 *
 *   - ARacingVehiclePawn never calls CaptureAndEvaluateTelemetry at all;
 *   - it calls it with the snapshots swapped, so every step is measured backwards;
 *   - TelemetrySampleRateHz is misread and capture never fires;
 *   - the thresholds are set so tight that ordinary driving trips them, which makes the
 *     detector useless in exactly the way a detector fails most often -- by crying wolf
 *     until someone stops reading it.
 *
 * So this suite drives the real car and asserts on the report the real pawn produced.
 * Both controls are here on purpose and neither is sufficient alone:
 *
 *   NEGATIVE CONTROL   a healthy car under hard acceleration, hard braking and full
 *                      lock reports NOTHING. A detector that always fires catches
 *                      every fault and is worthless.
 *   POSITIVE CONTROL   a real fault in the real pipeline IS caught. A detector that
 *                      never fires passes the negative control perfectly.
 *
 * ---------------------------------------------------------------------------
 * The bug the negative control found
 * ---------------------------------------------------------------------------
 *
 * VEH-004 derived its analysis step from FVehicleTelemetrySnapshot::TimestampSeconds,
 * which the pawn stamps from FPlatformTime::Seconds() -- a WALL clock -- while every
 * quantity being divided by that step came from stepping Chaos with DeltaSeconds. Sixty
 * simulated frames run in about a millisecond of wall time here, so the detector was
 * dividing entirely honest motion by a step three orders of magnitude too small and
 * would report a car accelerating at thousands of g.
 *
 * That is not a test artefact. The same divergence appears under a hitch, a breakpoint,
 * a paused editor, or any dilated or substepped time -- precisely the conditions a
 * failure detector exists to survive. The fix added
 * FVehicleTelemetrySnapshot::SimulationTimeSeconds and made it the only clock a rate is
 * derived from; this suite is the reason it was found, and is what keeps it fixed.
 */

namespace VehicleFailureDetectorDrivingPrivate
{
	/** 60 Hz, matching ARacingVehiclePawn::TelemetrySampleRateHz's default, so every step captures. */
	constexpr float StepSeconds = 1.0f / 60.0f;

	constexpr int32 AccelerateSteps = 180;	// 3.00 s
	constexpr int32 BrakeSteps = 120;		// 2.00 s
	constexpr int32 SteerSteps = 120;		// 2.00 s

	/**
	 * How far the positive control teleports the chassis in ONE step, CENTIMETRES.
	 *
	 * The tunnelling rule is: distance moved must not exceed
	 * |velocity| * step * TunnelVelocityFactor + TunnelToleranceCm, which for a
	 * STATIONARY car is 0 + 100 cm. 500 cm clears that by five times without being so
	 * large that the car leaves the fixture's 10,000 cm half-size ground -- leaving the
	 * ground would put the wheels in the air and change WHICH fault is being tested.
	 */
	constexpr double TeleportDistanceCm = 500.0;

	/** Describes a report for a log line, including the clean case. */
	FString DescribeReport(const FVehicleFailureReport& Report)
	{
		if (!Report.HasAnyFailure())
		{
			return TEXT("clean");
		}

		return FString::Printf(TEXT("[%s] %s"),
			*RacingSim::Vehicle::DescribeVehicleFailureFlags(Report.Flags), *Report.Reason);
	}
}

/**
 * NEGATIVE CONTROL: hard driving is not a fault.
 *
 * Accelerate to roughly 68 km/h, brake to a stop, then hold full lock. Every one of
 * those is a legitimate thing a driver does and none of them may raise a flag.
 *
 * The assertion is made on ARacingVehiclePawn's own LastFailureReport rather than on a
 * report this test computes, because the point is to test the pawn's wiring, not to
 * re-run the decision function the other spec already covers.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleFailureDetectorHealthyDrivingTest,
	"RacingSim.Vehicle.Manoeuvre.FailureDetectorSilentOnHealthyDriving",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FVehicleFailureDetectorHealthyDrivingTest::RunTest(const FString& Parameters)
{
	using namespace VehicleFailureDetectorDrivingPrivate;

	// bEnableTelemetry TRUE. Every other manoeuvre test opts out, because capture costs
	// frame time it does not need; this one is about capture.
	FVehicleManoeuvreFixture Fixture;
	if (!Fixture.Setup(*this, /*bEnableTelemetry*/ true))
	{
		return false;
	}

	ARacingVehiclePawn* Pawn = Fixture.GetPawn();

	// The settle phase already ran inside Setup with telemetry live, so a fault raised
	// while the car was dropping onto its springs is caught here rather than being
	// attributed to the first manoeuvre.
	const FVehicleFailureReport AfterSettle = Pawn->GetLastFailureReport();
	UE_LOG(LogRacingTests, Display,
		TEXT("FailureDetector: after settle, report %s; capture %lld, sim clock %.4f s."),
		*DescribeReport(AfterSettle), AfterSettle.CaptureIndex,
		Pawn->GetLastTelemetrySnapshot().SimulationTimeSeconds);

	TestFalse(TEXT("Settling onto the suspension is not a failure"), AfterSettle.HasAnyFailure());

	// -- CAPTURE IS ACTUALLY HAPPENING -------------------------------------------
	//
	// Checked before any "no failure" assertion is trusted. A pawn that never captures
	// holds a default report, which reports no failure, and would pass every assertion
	// below while proving nothing whatsoever. This is the difference between "the
	// detector stayed silent" and "the detector was never asked".
	const FVehicleTelemetrySnapshot SettleSnapshot = Pawn->GetLastTelemetrySnapshot();
	TestTrue(TEXT("Telemetry capture produced a valid snapshot during settling"), SettleSnapshot.bIsValid);
	TestTrue(
		FString::Printf(TEXT("Capture ran more than once while settling (index %lld)"), SettleSnapshot.CaptureIndex),
		SettleSnapshot.CaptureIndex > 1);
	TestEqual(TEXT("The snapshot carries the current schema version"),
		SettleSnapshot.SchemaVersion, VehicleTelemetrySchemaVersion);

	const int64 SettleCaptureIndex = SettleSnapshot.CaptureIndex;
	const double SettleSimTimeSeconds = SettleSnapshot.SimulationTimeSeconds;

	// -- Hard acceleration --------------------------------------------------------
	Fixture.Drive(FVehicleManoeuvreFixture::ThrottleSample(1.0), AccelerateSteps, StepSeconds);

	const FVehicleFailureReport AfterThrottle = Pawn->GetLastFailureReport();
	UE_LOG(LogRacingTests, Display,
		TEXT("FailureDetector: after %.2f s full throttle at %.2f cm/s, report %s (step %.5f s)."),
		AccelerateSteps * StepSeconds, Fixture.GetForwardSpeedCms(),
		*DescribeReport(AfterThrottle), AfterThrottle.MeasuredStepSeconds);

	TestFalse(TEXT("Full-throttle acceleration is not a failure"), AfterThrottle.HasAnyFailure());

	// -- Hard braking -------------------------------------------------------------
	Fixture.Drive(FVehicleManoeuvreFixture::BrakeSample(1.0), BrakeSteps, StepSeconds);

	const FVehicleFailureReport AfterBrake = Pawn->GetLastFailureReport();
	UE_LOG(LogRacingTests, Display,
		TEXT("FailureDetector: after %.2f s full brake at %.2f cm/s, report %s."),
		BrakeSteps * StepSeconds, Fixture.GetForwardSpeedCms(), *DescribeReport(AfterBrake));

	TestFalse(TEXT("Full braking is not a failure"), AfterBrake.HasAnyFailure());

	// -- Full lock ----------------------------------------------------------------
	Fixture.Drive(FVehicleManoeuvreFixture::ThrottleSample(1.0, 1.0), SteerSteps, StepSeconds);

	const FVehicleFailureReport AfterSteer = Pawn->GetLastFailureReport();
	UE_LOG(LogRacingTests, Display,
		TEXT("FailureDetector: after %.2f s at full right lock, yaw %.3f deg/s, report %s."),
		SteerSteps * StepSeconds, Fixture.GetYawRateDegreesPerSecond(), *DescribeReport(AfterSteer));

	TestFalse(TEXT("Cornering at full lock is not a failure"), AfterSteer.HasAnyFailure());

	// -- The two clocks, measured --------------------------------------------------
	//
	// The simulated clock must have advanced by the simulated time actually driven, and
	// the capture count must match the 60 Hz rate over that time. Together these say the
	// clock is not merely non-zero but is counting the right thing: a clock stamped from
	// the wall would show microseconds here, and a capture that never fired would show
	// no new indices.
	const FVehicleTelemetrySnapshot FinalSnapshot = Pawn->GetLastTelemetrySnapshot();
	const int32 DrivenSteps = AccelerateSteps + BrakeSteps + SteerSteps;
	const double ExpectedSimSeconds = DrivenSteps * static_cast<double>(StepSeconds);
	const double MeasuredSimSeconds = FinalSnapshot.SimulationTimeSeconds - SettleSimTimeSeconds;
	const int64 MeasuredCaptures = FinalSnapshot.CaptureIndex - SettleCaptureIndex;

	UE_LOG(LogRacingTests, Display,
		TEXT("FailureDetector: %d steps driven; simulation clock advanced %.4f s (expected %.4f s), %lld captures."),
		DrivenSteps, MeasuredSimSeconds, ExpectedSimSeconds, MeasuredCaptures);

	// Two steps of slack each way: capture is decimated against the sample rate, so the
	// last capture can land short of the final tick.
	TestTrue(
		FString::Printf(TEXT("The simulation clock advanced by the time actually driven (%.4f s vs %.4f s expected)"),
			MeasuredSimSeconds, ExpectedSimSeconds),
		FMath::Abs(MeasuredSimSeconds - ExpectedSimSeconds) <= 2.0 * StepSeconds);

	TestTrue(
		FString::Printf(TEXT("Capture ran about once per step at 60 Hz (%lld captures over %d steps)"),
			MeasuredCaptures, DrivenSteps),
		MeasuredCaptures >= DrivenSteps - 2 && MeasuredCaptures <= DrivenSteps + 2);

	Fixture.Teardown();
	return true;
}

/**
 * POSITIVE CONTROL: a real fault in the real pipeline is caught.
 *
 * A teleport is the right fault to inject because it is the one the detector can be
 * shown a genuine instance of without corrupting the solver. Everything else the
 * detector looks for -- non-finite state, runaway energy, a wheel spinning through the
 * floor -- requires either putting Chaos into a state it refuses to enter or reaching
 * past the public API to write nonsense into the snapshot, and the second of those
 * tests nothing but the decision function VehicleFailureDetectionSpec already covers.
 *
 * SetActorLocation with ETeleportType::TeleportPhysics is exactly what a shortcut, a
 * respawn, or a badly written cutscene does to a car, and it is what
 * ARacingVehiclePawn::ExecuteSafeReset does deliberately. The difference is the
 * NotifyTelemetryDiscontinuity call that reset makes and this test withholds -- so what
 * this test really proves is a pair of things:
 *
 *   1. an unannounced discontinuity IS caught, and
 *   2. therefore the announcement ExecuteSafeReset makes is load-bearing, not decorative.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleFailureDetectorTunnellingTest,
	"RacingSim.Vehicle.Manoeuvre.FailureDetectorCatchesUnannouncedTeleport",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FVehicleFailureDetectorTunnellingTest::RunTest(const FString& Parameters)
{
	using namespace VehicleFailureDetectorDrivingPrivate;

	// The detector logs its first raised flag at Error severity, and the automation
	// framework turns any Error logged during a test into a test failure. That is the
	// correct behaviour for both of them and it collides here, where an Error is the
	// expected outcome. Declared rather than suppressed: if the detector stops logging,
	// this test fails on the unmatched expectation, which is the outcome that should
	// follow from a detector that went quiet.
	AddExpectedError(TEXT("VEH-004 failure detected"),
		EAutomationExpectedErrorFlags::Contains, /*Occurrences*/ 0);

	FVehicleManoeuvreFixture Fixture;
	if (!Fixture.Setup(*this, /*bEnableTelemetry*/ true))
	{
		return false;
	}

	ARacingVehiclePawn* Pawn = Fixture.GetPawn();

	// The car must be demonstrably CLEAN before the fault is injected, or a flag raised
	// afterwards proves nothing about the injection.
	const FVehicleFailureReport BeforeTeleport = Pawn->GetLastFailureReport();
	TestFalse(TEXT("The settled car is clean before the fault is injected"),
		BeforeTeleport.HasAnyFailure());
	TestTrue(TEXT("Telemetry is live before the fault is injected"),
		Pawn->GetLastTelemetrySnapshot().bIsValid);

	const FVector StartLocation = Fixture.GetLocation();
	const double StartSpeedCms = Fixture.GetForwardSpeedCms();

	// THE FAULT. Straight along +X, at the same height, so the wheels stay over the
	// ground and the only thing that changed is a position that no velocity explains.
	const FVector TeleportTarget = StartLocation + FVector(TeleportDistanceCm, 0.0, 0.0);
	const bool bTeleported = Pawn->SetActorLocation(
		TeleportTarget, /*bSweep*/ false, /*OutSweepHitResult*/ nullptr, ETeleportType::TeleportPhysics);

	TestTrue(TEXT("The chassis accepted the teleport"), bTeleported);

	// ONE step. The detector is edge-triggered on the capture that follows the jump, and
	// ticking further would let the car drive away from the evidence.
	Fixture.Drive(FVehicleInputRawSample(), 1, StepSeconds);

	const FVehicleFailureReport AfterTeleport = Pawn->GetLastFailureReport();
	const FVehicleTelemetrySnapshot AfterSnapshot = Pawn->GetLastTelemetrySnapshot();

	UE_LOG(LogRacingTests, Display,
		TEXT("FailureDetector: teleported %.2f cm in one %.5f s step at %.2f cm/s; report %s (measured step %.5f s)."),
		TeleportDistanceCm, StepSeconds, StartSpeedCms,
		*DescribeReport(AfterTeleport), AfterTeleport.MeasuredStepSeconds);

	TestTrue(
		FString::Printf(TEXT("An unannounced %.0f cm teleport raises Tunnelling"), TeleportDistanceCm),
		AfterTeleport.Has(EVehicleFailureFlag::Tunnelling));

	TestTrue(TEXT("The raised report carries a non-empty reason"), !AfterTeleport.Reason.IsEmpty());

	TestTrue(
		FString::Printf(TEXT("The report is attributed to a capture after the teleport (index %lld)"),
			AfterTeleport.CaptureIndex),
		AfterTeleport.CaptureIndex > BeforeTeleport.CaptureIndex);

	TestTrue(TEXT("The snapshot behind the report is still finite"), AfterSnapshot.IsFinite());

	// -- The other half of the contract: announcing the discontinuity clears it -------
	//
	// Without this, the test above would also pass against a detector that raises
	// Tunnelling on every capture forever, and against a NotifyTelemetryDiscontinuity
	// that does nothing. It is the same fault, announced, and it must go quiet.
	//
	// IT DID NOT, and that is the defect this half of the test was written to find.
	// NotifyTelemetryDiscontinuity cleared PreviousSnapshot, and CaptureAndEvaluateTelemetry
	// then opened its very next capture with PreviousSnapshot = LastSnapshot -- restoring
	// the basis the notification had just dropped, one line before the detector read it.
	// So an announced reset raised Error-severity Tunnelling on the following capture,
	// every time, and VEH-005's ExecuteSafeReset calls this precisely so it will not.
	// Evidence: Saved/Automation/ReportVEH006Det1, capture 62,
	//   "[Tunnelling,InvalidContact]: moved 500.003126 cm in 0.016667 s".
	// Fixed in the detector, not in the pawn: FVehicleFailureDetectorState::
	// NotifyDiscontinuity arms a one-evaluation latch that EvaluateVehicleFailures
	// consumes. A pawn-side latch was written first and deliberately reverted -- it
	// silenced Tunnelling but not InvalidContact, which straddles the same
	// discontinuity, and it gave one concept two owners that could disagree.
	Pawn->NotifyTelemetryDiscontinuity();

	const FVector SecondTarget = Fixture.GetLocation() + FVector(TeleportDistanceCm, 0.0, 0.0);
	Pawn->SetActorLocation(SecondTarget, /*bSweep*/ false, /*OutSweepHitResult*/ nullptr, ETeleportType::TeleportPhysics);

	// ONE step, matching the unannounced case exactly. The two halves must differ in
	// nothing but the notification, or the comparison proves nothing about it -- and one
	// step is also the step on which the bug above fired, so a regression cannot hide in
	// a capture this test declined to look at.
	Fixture.Drive(FVehicleInputRawSample(), 1, StepSeconds);

	const FVehicleFailureReport AfterAnnounced = Pawn->GetLastFailureReport();
	UE_LOG(LogRacingTests, Display,
		TEXT("FailureDetector: same teleport, announced via NotifyTelemetryDiscontinuity; report %s."),
		*DescribeReport(AfterAnnounced));

	TestFalse(TEXT("An ANNOUNCED discontinuity of the same size raises nothing"),
		AfterAnnounced.HasAnyFailure());

	Fixture.Teardown();
	return true;
}
