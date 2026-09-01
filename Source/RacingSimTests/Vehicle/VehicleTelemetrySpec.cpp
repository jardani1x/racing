// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingSimBuildId.h"
#include "Core/RacingSimUnits.h"
#include "Misc/AutomationTest.h"
#include "Vehicle/VehicleInputConfig.h"
#include "Vehicle/VehicleInputProcessor.h"
#include "Vehicle/VehicleTelemetryTypes.h"
#include "Vehicle/VehicleTuneDataAsset.h"

#include <limits>

/**
 * VEH-004: the telemetry snapshot contract, the car-spec-version decision, and the
 * VEH-001 input seams routed forward into this ticket.
 *
 * ---------------------------------------------------------------------------
 * Why no ARacingVehiclePawn and no live movement component appears here
 * ---------------------------------------------------------------------------
 *
 * The documented harness limitation (Docs/Environment.md, derived from engine source;
 * restated in VehicleChassisSpec.cpp and VehicleTuneSpec.cpp): a SmokeFilter test runs
 * inside FEngineLoop::PreInit, before RegisterEngineElements(), so constructing any
 * non-template Actor or UActorComponent kills the process and produces NO index.json.
 *
 * Consequently RacingSim::Vehicle::CaptureVehicleTelemetry is NOT exercised against a
 * real UChaosWheeledVehicleMovementComponent here -- it takes one, so it cannot be. Its
 * null-movement contract is tested, and the STRUCTS and DECISIONS it produces are
 * tested directly. That is the same shape of gap VEH-002 recorded for ApplyChassisAsset
 * and VEH-003 for ApplyTuneAsset, and it is named rather than papered over: closing it
 * needs a spawnable world, which this project does not have, and a graybox level with
 * collision, which TRACK-002 L2 says does not exist either. VEH-006 owns both.
 */

namespace
{
	/** A config with a keyboard profile, built in code so no .uasset is needed. */
	UVehicleInputConfigDataAsset* MakeTelemetryInputConfig()
	{
		return NewObject<UVehicleInputConfigDataAsset>(GetTransientPackage());
	}

	/** A tune asset as constructed -- the state an author starts from, and a valid one. */
	UVehicleTuneDataAsset* MakeTelemetryTune()
	{
		return NewObject<UVehicleTuneDataAsset>(GetTransientPackage());
	}
}

// ---------------------------------------------------------------------------
// The snapshot contract itself.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleTelemetryContractTest,
	"RacingSim.Vehicle.TelemetrySnapshotContract",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleTelemetryContractTest::RunTest(const FString& Parameters)
{
	// -- A default snapshot is "no sample", not "a stationary car".
	//
	// This distinction is load-bearing and is the reason bIsValid exists at all: a car
	// parked at the world origin produces a completely legitimate all-zero reading, so
	// an all-zero snapshot cannot be its own absence sentinel.
	const FVehicleTelemetrySnapshot Default;
	TestFalse(TEXT("A default-constructed snapshot is not valid"), Default.bIsValid);
	TestEqual(TEXT("A default snapshot carries the current schema version"),
		Default.SchemaVersion, VehicleTelemetrySchemaVersion);
	TestFalse(TEXT("A default snapshot has no car spec version"),
		Default.CarSpecVersion.IsPopulated());
	TestEqual(TEXT("A default snapshot reports no wheels"), Default.NumWheels, 0);
	TestTrue(TEXT("A default snapshot is finite"), Default.IsFinite());

	// -- Bounds checking, which must never assert.
	//
	// Chaos' own GetWheelState(int) indexes its array with NO bounds check
	// (ChaosWheeledVehicleMovementComponent.h:716-719), so this project's mirror of it
	// deliberately does not inherit that behaviour: telemetry is a diagnostic, and a
	// diagnostic that crashes the process it is diagnosing is worthless.
	FVehicleTelemetrySnapshot Snapshot;
	Snapshot.bIsValid = true;
	Snapshot.NumWheels = 2;
	Snapshot.Wheels[0].bInContact = true;
	Snapshot.Wheels[1].bInContact = false;

	TestTrue(TEXT("GetWheel returns a populated entry in range"), Snapshot.GetWheel(0).bInContact);
	TestFalse(TEXT("GetWheel(-1) returns a safe default rather than asserting"),
		Snapshot.GetWheel(-1).bInContact);
	TestFalse(TEXT("GetWheel past NumWheels returns a safe default"),
		Snapshot.GetWheel(3).bInContact);
	TestFalse(TEXT("GetWheel past the array capacity returns a safe default"),
		Snapshot.GetWheel(999).bInContact);
	TestEqual(TEXT("CountWheelsInContact counts only populated wheels"),
		Snapshot.CountWheelsInContact(), 1);

	// -- Finiteness covers wheels, not just the chassis.
	Snapshot.Wheels[1].SlipAngleDegrees = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("A NaN in a POPULATED wheel makes the snapshot non-finite"), Snapshot.IsFinite());

	Snapshot.Wheels[1].SlipAngleDegrees = 0.0f;
	Snapshot.Wheels[3].SpringForceN = std::numeric_limits<float>::infinity();
	TestTrue(TEXT("A value in an UNPOPULATED wheel slot is ignored, since it must not be read"),
		Snapshot.IsFinite());

	// -- Units, at the one boundary this struct owns.
	//
	// 2777.777... cm/s is exactly 100 km/h. Asserted against an independently known
	// value through RacingSim::Units, never an inline 0.036.
	Snapshot.Wheels[3].SpringForceN = 0.0f;
	Snapshot.ForwardSpeedCms = 2777.7778f;
	TestTrue(TEXT("cm/s -> km/h goes through Core/RacingSimUnits and matches a known value"),
		FMath::IsNearlyEqual(Snapshot.GetForwardSpeedKph(), 100.0, 0.001));
	TestEqual(TEXT("...and is the same function Core/RacingSimUnits exposes"),
		Snapshot.GetForwardSpeedKph(),
		RacingSim::Units::CmsToKilometresPerHour(Snapshot.ForwardSpeedCms));

	// Speed MAGNITUDE comes from the velocity vector, not the forward axis. A car
	// sliding sideways has a small forward speed and a large actual speed, and the
	// runaway-energy detector must judge the second.
	Snapshot.VelocityCms = FVector(3.0, 4.0, 0.0);
	TestTrue(TEXT("Speed magnitude is the vector length, not the forward component"),
		FMath::IsNearlyEqual(Snapshot.GetSpeedMagnitudeCms(), 5.0, KINDA_SMALL_NUMBER));

	// -- The one-directional bridge to CORE-002's HUD contract.
	Snapshot.TimestampSeconds = 42.5;
	Snapshot.LocationCm = FVector(1.0, 2.0, 3.0);
	Snapshot.EngineRpm = 5500.0f;
	Snapshot.GearIndex = 4;
	Snapshot.ChaosInput.Throttle = 0.75f;
	Snapshot.ChaosInput.Brake = 0.0f;
	Snapshot.ChaosInput.Steering = -0.5f;
	Snapshot.ChaosInput.bHandbrake = true;
	Snapshot.InputDeviceType = ERacingInputDeviceType::Gamepad;

	const FRacingVehicleTelemetrySample Sample = Snapshot.ToRacingVehicleSample();
	TestEqual(TEXT("Bridge carries the timestamp"), Sample.TimestampSeconds, 42.5);
	TestEqual(TEXT("Bridge carries the gear"), Sample.GearIndex, 4);
	TestEqual(TEXT("Bridge carries the engine speed"), Sample.EngineRPM, 5500.0f);
	TestEqual(TEXT("Bridge carries the throttle Chaos received"), Sample.ThrottleInput, 0.75f);
	TestEqual(TEXT("Bridge preserves the steering SIGN (negative is left)"), Sample.SteerInput, -0.5f);
	TestEqual(TEXT("Bridge carries the device type"), Sample.InputDeviceType, ERacingInputDeviceType::Gamepad);
	// Chaos takes a bool handbrake and the HUD contract stores a float; the analog
	// value the driver applied is not recoverable, so 1.0 is the honest widening.
	TestEqual(TEXT("An engaged bool handbrake widens to 1.0, not to an invented lever position"),
		Sample.HandbrakeInput, 1.0f);

	// -- The null-movement capture contract. The one branch of CaptureVehicleTelemetry
	// reachable without a live component, and the one that must never crash.
	FVehicleTelemetryCaptureInput NullCapture;
	NullCapture.TimestampSeconds = 99.0;
	NullCapture.CaptureIndex = 7;

	const FVehicleTelemetrySnapshot FromNull = RacingSim::Vehicle::CaptureVehicleTelemetry(NullCapture);
	TestFalse(TEXT("Capturing with a null movement component yields an INVALID snapshot, not a crash"),
		FromNull.bIsValid);

	return true;
}

// ---------------------------------------------------------------------------
// CarSpecVersion: CORE-002's hole, and the decision that closes it.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleCarSpecVersionTest,
	"RacingSim.Vehicle.TelemetryCarSpecVersion",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleCarSpecVersionTest::RunTest(const FString& Parameters)
{
	UVehicleTuneDataAsset* Tune = MakeTelemetryTune();

	// -- The happy path: a tune that was actually applied names the car.
	const FRacingContentVersion Applied = RacingSim::Vehicle::ResolveCarSpecVersion(Tune, true);
	TestTrue(TEXT("An APPLIED tune yields a populated car spec version"), Applied.IsPopulated());
	TestEqual(TEXT("...identified by the tune's own id"), Applied.AssetId, Tune->TuneId);
	TestEqual(TEXT("...and matching the asset's own GetContentVersion"),
		Applied.ContentHash, Tune->GetContentVersion().ContentHash);

	// -- THE POINT OF THE FUNCTION. A tune that was REFERENCED but never APPLIED must
	// not be published.
	//
	// Two real paths reach this: a pawn with a TuneAsset but no ChassisAsset never
	// reaches ApplyTuneAsset() at all (ApplyChassisAsset returns first), and a tune with
	// an unusable torque curve has its engine write refused. In both, the car did not
	// run that tune. A result naming it would pass IsPublishable() and describe a car
	// nobody drove -- which is exactly what URaceResultRecorder::SetCarSpecVersion's own
	// header warns about.
	const FRacingContentVersion Unapplied = RacingSim::Vehicle::ResolveCarSpecVersion(Tune, false);
	TestFalse(TEXT("A REFERENCED-but-unapplied tune yields an UNPOPULATED version"),
		Unapplied.IsPopulated());

	// -- No tune at all.
	TestFalse(TEXT("A null tune yields an unpopulated version"),
		RacingSim::Vehicle::ResolveCarSpecVersion(nullptr, true).IsPopulated());
	TestFalse(TEXT("A null tune that was also not applied yields an unpopulated version"),
		RacingSim::Vehicle::ResolveCarSpecVersion(nullptr, false).IsPopulated());

	// -- An UNNAMED tune cannot be published either, applied or not: an unpopulated
	// AssetId is what FRacingContentVersion::IsPopulated refuses, and a hash alone
	// cannot identify a car to a human reading a results table.
	Tune->TuneId = NAME_None;
	TestFalse(TEXT("A tune with no TuneId yields an unpopulated version even when applied"),
		RacingSim::Vehicle::ResolveCarSpecVersion(Tune, true).IsPopulated());

	// -- And the stamp CORE-002 built refuses exactly that shape.
	FRacingSimVersionStamp Stamp;
	Stamp.CarSpecVersion = RacingSim::Vehicle::ResolveCarSpecVersion(Tune, true);
	TestFalse(TEXT("An unpopulated car spec version keeps the stamp unpublishable"),
		Stamp.CarSpecVersion.IsPopulated());

	return true;
}

// ---------------------------------------------------------------------------
// VEH-001 MEDIUM-2 / MEDIUM-3: the ConfigureFromAsset seam, untested for two tickets.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleConfigureFromAssetTest,
	"RacingSim.Vehicle.InputConfigureFromAsset",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleConfigureFromAssetTest::RunTest(const FString& Parameters)
{
	// VEH-001 MEDIUM-2 and MEDIUM-3 were open across VEH-002 and VEH-003 because "every
	// processor test uses Configure(profile,...), never ConfigureFromAsset" -- so a
	// regression deleting the asset read entirely would have passed all nine of
	// VEH-001's tests. UVehicleInputConfigDataAsset is a UDataAsset and constructs
	// safely at this gate; there was never a harness reason for the gap.

	// -- The null-asset contract.
	{
		FVehicleInputProcessor Processor;
		const bool bConfigured = Processor.ConfigureFromAsset(nullptr, ERacingInputDeviceType::Gamepad);

		TestFalse(TEXT("ConfigureFromAsset(null) returns false"), bConfigured);
		// The device is recorded ANYWAY. It is a fact about the world; whether this
		// project has a profile for it is a fact about the content, and recording
		// Unknown would put a wrong value on the result's version stamp.
		TestEqual(TEXT("...but the DEVICE is still recorded on failure"),
			Processor.GetDeviceType(), ERacingInputDeviceType::Gamepad);
		TestEqual(TEXT("...and safe defaults are restored"), Processor.GetMaxDeltaSeconds(), 0.1f);
		TestEqual(TEXT("...with the stale guard disarmed"), Processor.GetInputStaleAfterSeconds(), 0.0f);
		TestEqual(TEXT("...and an automatic transmission assumed"),
			Processor.GetTransmissionMode(), ETransmissionInputMode::Automatic);
	}

	// -- The asset-sourced values actually arrive.
	{
		UVehicleInputConfigDataAsset* Config = MakeTelemetryInputConfig();
		Config->TransmissionMode = ETransmissionInputMode::Manual;
		Config->MaxDeltaSeconds = 0.25f;
		Config->InputStaleAfterSeconds = 3.5f;

		FVehicleInputProcessor Processor;
		Processor.ConfigureFromAsset(Config, Config->DefaultDeviceType);

		TestEqual(TEXT("TransmissionMode is sourced from the asset"),
			Processor.GetTransmissionMode(), ETransmissionInputMode::Manual);
		TestEqual(TEXT("MaxDeltaSeconds is sourced from the asset"),
			Processor.GetMaxDeltaSeconds(), 0.25f);
		TestEqual(TEXT("InputStaleAfterSeconds is sourced from the asset"),
			Processor.GetInputStaleAfterSeconds(), 3.5f);
	}

	// -- The [0.001, 1.0] guard on MaxDeltaSeconds, which exists because nothing
	// guarantees Validate() ran on a given asset and a 0 would freeze every
	// rate-limited axis permanently.
	{
		UVehicleInputConfigDataAsset* Config = MakeTelemetryInputConfig();
		Config->MaxDeltaSeconds = 0.0f;

		FVehicleInputProcessor Processor;
		Processor.ConfigureFromAsset(Config, Config->DefaultDeviceType);
		TestEqual(TEXT("A zero MaxDeltaSeconds is guarded up to 0.001"),
			Processor.GetMaxDeltaSeconds(), 0.001f);

		Config->MaxDeltaSeconds = 50.0f;
		Processor.ConfigureFromAsset(Config, Config->DefaultDeviceType);
		TestEqual(TEXT("An enormous MaxDeltaSeconds is guarded down to 1.0"),
			Processor.GetMaxDeltaSeconds(), 1.0f);
	}

	// -- The stale timeout's guard is ASYMMETRIC to that one, deliberately: a broken
	// value DISABLES the guard rather than arming it on an invented timescale. This
	// mechanism can zero a driver's throttle, so it must fail toward doing nothing.
	{
		UVehicleInputConfigDataAsset* Config = MakeTelemetryInputConfig();
		Config->InputStaleAfterSeconds = std::numeric_limits<float>::quiet_NaN();

		FVehicleInputProcessor Processor;
		Processor.ConfigureFromAsset(Config, Config->DefaultDeviceType);
		TestEqual(TEXT("A NaN stale timeout DISABLES the guard rather than arming it"),
			Processor.GetInputStaleAfterSeconds(), 0.0f);

		Config->InputStaleAfterSeconds = -5.0f;
		Processor.ConfigureFromAsset(Config, Config->DefaultDeviceType);
		TestEqual(TEXT("A negative stale timeout also disables the guard"),
			Processor.GetInputStaleAfterSeconds(), 0.0f);
	}

	// -- The neutral-profile fallback and the false return, for a device the config has
	// no profile for. A silent fallback to ANOTHER device's numbers is what this
	// contract exists to prevent (see UVehicleInputConfigDataAsset::FindProfile).
	{
		UVehicleInputConfigDataAsset* Config = MakeTelemetryInputConfig();
		Config->Profiles.Empty();

		FVehicleInputProcessor Processor;
		const bool bConfigured = Processor.ConfigureFromAsset(Config, ERacingInputDeviceType::Gamepad);

		TestFalse(TEXT("A config with no profile for the device returns false"), bConfigured);
		TestEqual(TEXT("...but still records the device"),
			Processor.GetDeviceType(), ERacingInputDeviceType::Gamepad);

		// Neutral shaping: a default-constructed profile, which is linear and instant.
		const FVehicleInputProfile Neutral;
		TestEqual(TEXT("...and falls back to a NEUTRAL profile, not another device's"),
			Processor.GetProfile().SteerDeadZone, Neutral.SteerDeadZone);
		TestEqual(TEXT("...neutral saturation too"),
			Processor.GetProfile().SteerSaturation, Neutral.SteerSaturation);

		// The asset's other values still arrive on the failure path -- the profile is
		// missing, the asset is not.
		TestEqual(TEXT("...while asset-level timing still applies"),
			Processor.GetMaxDeltaSeconds(), FMath::Clamp(Config->MaxDeltaSeconds, 0.001f, 1.0f));
	}

	// -- Configure(profile,...) must CLEAR the asset, or speed-sensitive steering the
	// caller did not ask for silently persists from an earlier ConfigureFromAsset.
	{
		UVehicleInputConfigDataAsset* Config = MakeTelemetryInputConfig();
		Config->InputStaleAfterSeconds = 4.0f;

		FVehicleInputProcessor Processor;
		Processor.ConfigureFromAsset(Config, Config->DefaultDeviceType);
		TestEqual(TEXT("The asset's stale timeout is in force"),
			Processor.GetInputStaleAfterSeconds(), 4.0f);

		const FVehicleInputProfile Profile;
		Processor.Configure(Profile, ERacingInputDeviceType::Keyboard, ETransmissionInputMode::Automatic, 0.1f);
		TestEqual(TEXT("Configure(profile,...) defaults the stale guard back to disabled"),
			Processor.GetInputStaleAfterSeconds(), 0.0f);
	}

	return true;
}

// ---------------------------------------------------------------------------
// VEH-001 MEDIUM-4: the stuck-input timeout. Deferred twice; closed here.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleInputStaleSampleTest,
	"RacingSim.Vehicle.InputStaleSample",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleInputStaleSampleTest::RunTest(const FString& Parameters)
{
	// The profile is instant (no rate limiting) so this test measures the STALENESS
	// rule and not the ramp. The ramp-down behaviour is a separate, deliberate design
	// choice and is asserted at the end.
	FVehicleInputProfile Instant;
	Instant.ThrottleRiseRate = 0.0f;
	Instant.ThrottleFallRate = 0.0f;
	Instant.SteerRate = 0.0f;
	Instant.SteerCentringRate = 0.0f;

	// -- A HELD control is NOT stale. This is the property the whole mechanism turns
	// on: Enhanced Input re-fires Triggered every frame for a held control, so its
	// stamp keeps advancing, and a naive "the value has not changed" implementation
	// would neutralise a driver holding the throttle flat down a straight.
	{
		FVehicleInputProcessor Processor;
		Processor.Configure(Instant, ERacingInputDeviceType::Keyboard, ETransmissionInputMode::Automatic, 0.1f, 1.0f);

		double Now = 100.0;
		for (int32 StepIndex = 0; StepIndex < 300; ++StepIndex)
		{
			Now += 1.0 / 60.0;

			FVehicleInputRawSample Sample;
			Sample.Throttle = 1.0;
			// A live device: the handler stamps the sample every frame.
			Sample.SampleTimestampSeconds = Now;

			const FVehicleInputCommand Command = Processor.Tick(Sample, 1.0 / 60.0, Now);

			TestFalse(
				FString::Printf(TEXT("A continuously re-stamped sample is never stale (step %d)"), StepIndex),
				Command.HasCorrection(EVehicleInputCorrection::StaleSample));
			TestEqual(TEXT("...and full throttle survives"), Command.Throttle, 1.0f);
		}
	}

	// -- A sample whose stamp stops advancing IS stale, after the timeout and not before.
	{
		FVehicleInputProcessor Processor;
		Processor.Configure(Instant, ERacingInputDeviceType::Keyboard, ETransmissionInputMode::Automatic, 0.1f, 1.0f);

		// The last frame a device event arrived.
		const double LastEventTime = 100.0;

		FVehicleInputRawSample Latched;
		Latched.Throttle = 1.0;
		Latched.Steer = 0.8;
		Latched.bShiftUpHeld = true;
		Latched.bResetHeld = true;
		Latched.SampleTimestampSeconds = LastEventTime;

		// Half a second later: not yet stale. The timeout is deliberately long, because
		// a false positive is a car that mysteriously lifts off mid-corner.
		const FVehicleInputCommand Fresh = Processor.Tick(Latched, 1.0 / 60.0, LastEventTime + 0.5);
		TestFalse(TEXT("Half a second of silence is not yet stale"),
			Fresh.HasCorrection(EVehicleInputCorrection::StaleSample));
		TestEqual(TEXT("...and the latched throttle still applies"), Fresh.Throttle, 1.0f);

		// Two seconds later: stale. This is the Pixel Streaming disconnect, the
		// backgrounded tab, the lost focus.
		const FVehicleInputCommand Stale = Processor.Tick(Latched, 1.0 / 60.0, LastEventTime + 2.0);
		TestTrue(TEXT("Two seconds of silence is stale"),
			Stale.HasCorrection(EVehicleInputCorrection::StaleSample));
		TestEqual(TEXT("...throttle is neutralised"), Stale.Throttle, 0.0f);
		TestEqual(TEXT("...steering is neutralised"), Stale.Steer, 0.0f);
		TestEqual(TEXT("...brake stays zero rather than being slammed on"), Stale.Brake, 0.0f);
		// A stale "shift up held" must not fire a phantom edge. The held flags are no
		// longer force-cleared during a stale gap (see the processor's own comment on
		// why clearing them was backwards: it force-cleared bResetLatched too, which
		// would have RE-ARMED a second reset for a player who never released the key).
		// No edge fires here because bShiftUpHeld was already true on the previous
		// (not-yet-stale) tick, so the rising-edge detector correctly sees no change --
		// the same reason a key held continuously across an ordinary frame never
		// double-fires.
		//
		// The still-held reset, however, does NOT keep accumulating through the stale
		// gap -- RESOLVED by VEH-005 (see VehicleInputProcessor.cpp's own TRADE-OFF
		// comment). Letting ResetHeldSeconds accumulate unattended during a dead
		// connection would let a hold that was still short of the threshold complete on
		// its own mid-outage, resetting the car for a driver who never finished
		// pressing. So only the one Fresh (not-yet-stale) tick above contributes:
		// 1*(1/60)/0.5, not the two ticks a naive "ignore staleness" implementation
		// would have accumulated.
		TestEqual(TEXT("...no phantom gear request is produced"),
			Stale.GearRequest, EVehicleGearRequest::None);
		TestFalse(TEXT("...and no reset FIRES yet (progress is below the hold threshold)"),
			Stale.bResetRequested);
		TestTrue(TEXT("...and the still-held reset FREEZES during the stale gap, not accumulating further"),
			FMath::IsNearlyEqual(Stale.ResetHoldProgress, 1.0f * (1.0f / 60.0f) / 0.5f, 1.0e-4f));

		// The output guarantee still holds under the neutralisation path.
		TestTrue(TEXT("A neutralised command is still finite and in range"),
			Stale.IsFiniteAndInRange());
	}

	// -- IDLE COASTING IS NOT STALENESS (code review, VEH-004 HIGH-1). A player who has
	// released every control -- coasting down a straight, waiting on the grid, pad on
	// the table with nothing held -- produces no Enhanced Input events at all, so the
	// sample's timestamp genuinely stops advancing exactly as it would during a real
	// disconnect. The previous version of this mechanism could not tell the two apart
	// and raised StaleSample (and an Error-level "VEH-004 failure detected" log) on
	// every ordinary coast. The fix: staleness is only reported when the STALE SAMPLE
	// ITSELF still names a non-zero demand or a held control -- i.e. when there is
	// something that would actually stay dangerously latched if left alone. A sample
	// that was already neutral when the events stopped has nothing to protect against.
	{
		FVehicleInputProcessor Processor;
		Processor.Configure(Instant, ERacingInputDeviceType::Keyboard, ETransmissionInputMode::Automatic, 0.1f, 1.0f);

		const double LastEventTime = 100.0;

		FVehicleInputRawSample Neutral;
		// Every field left at its default: 0.0 for the axes, false for the held flags --
		// exactly what Enhanced Input's own Completed handlers already wrote once the
		// driver let go of everything.
		Neutral.SampleTimestampSeconds = LastEventTime;

		const FVehicleInputCommand Command = Processor.Tick(Neutral, 1.0 / 60.0, LastEventTime + 5.0);
		TestFalse(TEXT("An already-neutral sample past the timeout is NOT reported stale"),
			Command.HasCorrection(EVehicleInputCorrection::StaleSample));
		TestEqual(TEXT("...and stays a clean, coasting command"), Command.Throttle, 0.0f);
	}

	// -- The guard is OPT-IN FROM BOTH ENDS. A mechanism that can zero a driver's
	// throttle must not arm itself by accident.
	{
		// No timeout configured: never stale, however old the sample.
		FVehicleInputProcessor Processor;
		Processor.Configure(Instant, ERacingInputDeviceType::Keyboard, ETransmissionInputMode::Automatic, 0.1f, 0.0f);

		FVehicleInputRawSample Ancient;
		Ancient.Throttle = 1.0;
		Ancient.SampleTimestampSeconds = 1.0;

		const FVehicleInputCommand Command = Processor.Tick(Ancient, 1.0 / 60.0, 100000.0);
		TestFalse(TEXT("With no timeout configured, nothing is ever stale"),
			Command.HasCorrection(EVehicleInputCorrection::StaleSample));
		TestEqual(TEXT("...and the throttle is untouched"), Command.Throttle, 1.0f);
	}

	{
		// A sample that was NEVER stamped (timestamp 0) is not treated as infinitely
		// old. Otherwise a test harness, or any caller that does not stamp, would have
		// every command it ever produced neutralised -- which is a far worse failure
		// than the one being guarded against.
		FVehicleInputProcessor Processor;
		Processor.Configure(Instant, ERacingInputDeviceType::Keyboard, ETransmissionInputMode::Automatic, 0.1f, 1.0f);

		FVehicleInputRawSample Unstamped;
		Unstamped.Throttle = 1.0;
		Unstamped.SampleTimestampSeconds = 0.0;

		const FVehicleInputCommand Command = Processor.Tick(Unstamped, 1.0 / 60.0, 100000.0);
		TestFalse(TEXT("An unstamped sample is not treated as infinitely old"),
			Command.HasCorrection(EVehicleInputCorrection::StaleSample));
		TestEqual(TEXT("...and its value is honoured"), Command.Throttle, 1.0f);
	}

	// -- RAMP-DOWN, not snap-to-zero. Neutralisation sets zero TARGETS and lets the
	// rate limiter unwind them: a lost connection at 200 km/h that instantly centred
	// the wheel and lifted off would be its own loss of control.
	{
		FVehicleInputProfile Ramped;
		Ramped.ThrottleFallRate = 2.0f;   // half a second from full to zero
		Ramped.SteerCentringRate = 2.0f;
		Ramped.SteerRate = 2.0f;
		Ramped.ThrottleRiseRate = 0.0f;

		FVehicleInputProcessor Processor;
		Processor.Configure(Ramped, ERacingInputDeviceType::Keyboard, ETransmissionInputMode::Automatic, 0.1f, 1.0f);

		double Now = 500.0;

		// Establish full throttle with a live device.
		FVehicleInputRawSample Live;
		Live.Throttle = 1.0;
		Live.SampleTimestampSeconds = Now;
		Processor.Tick(Live, 0.1f, Now);

		// Now the connection dies. The stamp stops advancing; wall time does not.
		Now += 2.0;
		const FVehicleInputCommand FirstStale = Processor.Tick(Live, 0.05, Now);

		TestTrue(TEXT("The first stale frame is flagged"),
			FirstStale.HasCorrection(EVehicleInputCorrection::StaleSample));
		TestTrue(TEXT("...and the throttle RAMPS DOWN rather than snapping to zero"),
			FirstStale.Throttle > 0.0f && FirstStale.Throttle < 1.0f);
	}

	return true;
}
