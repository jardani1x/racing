// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/RacingSimTypes.h"
#include "UObject/WeakObjectPtr.h"
#include "Vehicle/VehicleInputConfig.h"
#include "Vehicle/VehicleInputTypes.h"

/**
 * VEH-001: every input rule, in a plain struct.
 *
 * ---------------------------------------------------------------------------
 * Why this is NOT a UActorComponent, and why that is the whole design
 * ---------------------------------------------------------------------------
 *
 * Docs/Environment.md records, with an engine-source cause, that a SmokeFilter test
 * in this project cannot construct a non-template Actor or UActorComponent:
 * FEngineLoop::PreInit runs the smoke tests itself (LaunchEngineLoop.cpp:4376),
 * before RegisterEngineElements() runs in UEngine::Init (UnrealEngine.cpp:2399), and
 * UActorComponent::PostInitProperties creates an editor element for every
 * non-template component (ActorComponent.cpp:588) -- which checkf's on an
 * unregistered type and kills the process with no index.json at all.
 *
 * If the dead zone, the rate limiter, the reset hold and the frame-rate clamp lived
 * on the component, none of them could be tested at the Smoke gate. They would need
 * a ProductFilter test, a loaded level, and a pawn that VEH-002 has not built yet --
 * i.e. VEH-001's own correctness would be unverifiable until a later ticket landed.
 *
 * So the component keeps the engine plumbing and this struct keeps the decisions.
 * The rule for anyone extending this: if it can be gotten wrong, it belongs here.
 *
 * ---------------------------------------------------------------------------
 * Threading and lifetime
 * ---------------------------------------------------------------------------
 *
 * Game thread only. It holds mutable smoothing state, so two threads ticking one
 * processor would interleave into nonsense; there is no lock because there is no
 * legitimate second caller. The config is held weakly and re-checked every Tick --
 * an input config is content and can be unloaded, and a raw pointer would outlive it.
 *
 * ---------------------------------------------------------------------------
 * Allocation
 * ---------------------------------------------------------------------------
 *
 * Tick allocates nothing. All state is fixed-size POD, the output is returned by
 * value, and the only pointer chase is the weak-pointer resolve. Configure() may
 * allocate; it runs once at possession.
 */

/**
 * One sample of raw, unshaped device values.
 *
 * This is what Enhanced Input hands over, and it is deliberately hostile-friendly:
 * every field may arrive non-finite or out of range without breaking anything.
 * That is not defensive paranoia, it is the actual threat model -- under Pixel
 * Streaming the axis value originates in a BROWSER, travels over the network as a
 * number chosen by a client this project does not control, and arrives here. The
 * processor is the trust boundary, and it is the only one on the path to Chaos.
 */
struct FVehicleInputRawSample
{
	/** Raw throttle, nominally [0,1]. Digital bindings send exactly 0 or 1. */
	double Throttle = 0.0;

	/** Raw brake, nominally [0,1]. */
	double Brake = 0.0;

	/** Raw steer, nominally [-1,1]. Positive is RIGHT; see FVehicleInputCommand. */
	double Steer = 0.0;

	/** Raw handbrake, nominally [0,1]. */
	double Handbrake = 0.0;

	/** Raw clutch, nominally [0,1], 1 == fully disengaged. Ignored under an automatic transmission. */
	double Clutch = 0.0;

	/** Held state, not an edge. The processor derives the edge, so a missed frame cannot double-shift. */
	bool bShiftUpHeld = false;

	/** Held state, not an edge. */
	bool bShiftDownHeld = false;

	/** Held state. The reset is a HOLD, not a press -- see FVehicleInputProfile::ResetHoldSeconds. */
	bool bResetHeld = false;

	/**
	 * Vehicle forward speed in CENTIMETRES PER SECOND -- the project storage unit,
	 * per Core/RacingSimUnits.h. Consumed only by speed-sensitive steering, and
	 * converted to km/h inside UVehicleInputConfigDataAsset::GetSteerScaleForSpeedCms.
	 *
	 * Signed: negative while reversing. Left at 0 by callers with no vehicle yet
	 * (which is every caller until VEH-002), giving full steering authority.
	 */
	double SpeedCms = 0.0;
};

/**
 * Turns raw device samples into normalised commands. See the file header for why it
 * is a plain struct rather than a component.
 */
class RACINGSIM_API FVehicleInputProcessor
{
public:
	/**
	 * Configure from an asset. Resolves the profile for DeviceType and keeps a weak
	 * reference to the asset for speed-sensitive steering.
	 *
	 * @return false when the asset is null or has no profile for DeviceType. On
	 *         false the processor is left in its safe default state and Tick still
	 *         works -- it produces a linear, unlimited, immediately-centring
	 *         command. That is a deliberate choice over refusing to run: an input
	 *         layer that returns nothing at all is a car that cannot be moved off
	 *         the track, and the caller has already been told the truth by the
	 *         return value.
	 */
	bool ConfigureFromAsset(const UVehicleInputConfigDataAsset* Config, ERacingInputDeviceType DeviceType);

	/**
	 * Configure from a profile directly, with no UObject involved at all.
	 *
	 * This is the form the Smoke tests use, and it is public rather than
	 * test-only because it is also the honest API: nothing about shaping an axis
	 * needs an asset. Speed-sensitive steering is disabled in this form (scale is
	 * always 1), because the thresholds and the curve live on the asset.
	 */
	void Configure(
		const FVehicleInputProfile& Profile,
		ERacingInputDeviceType DeviceType,
		ETransmissionInputMode TransmissionMode,
		float MaxDeltaSeconds);

	/**
	 * Advance one sample.
	 *
	 * @param Raw               device values; may contain NaN, infinity or out-of-range values.
	 * @param DeltaSeconds      time since the previous call. Non-finite, negative and
	 *                          over-long values are clamped to [0, MaxDeltaSeconds]
	 *                          and reported as EVehicleInputCorrection::DeltaClamped.
	 * @param TimestampSeconds  monotonic seconds, recorded on the command for telemetry.
	 *                          Supplied by the caller rather than sampled here so a
	 *                          test can drive time deterministically -- the same
	 *                          reason RACE-001 injects its clock.
	 *
	 * @return a command guaranteed to satisfy FVehicleInputCommand::IsFiniteAndInRange().
	 */
	FVehicleInputCommand Tick(const FVehicleInputRawSample& Raw, double DeltaSeconds, double TimestampSeconds);

	/**
	 * Drop all smoothing and latch state; keep the configuration.
	 *
	 * Must be called whenever the car is teleported, respawned or the session is
	 * restarted. Without it a car that was at full lock and full throttle when it was
	 * reset resumes at full lock and full throttle on the grid, because the rate
	 * limiter's state is a memory of inputs that no longer apply. That is the input
	 * layer's share of the Gate B rule that "reset can never award progress".
	 */
	void ResetState();

	/** Currently selected device. Feeds FRacingSimVersionStamp::InputDeviceType on a result. */
	ERacingInputDeviceType GetDeviceType() const
	{
		return DeviceType;
	}

	/** The profile in force. Exposed for telemetry and tests; never mutated through this. */
	const FVehicleInputProfile& GetProfile() const
	{
		return Profile;
	}

	/** The last command produced. Default-constructed (safe, all-zero) before the first Tick. */
	const FVehicleInputCommand& GetLastCommand() const
	{
		return LastCommand;
	}

private:
	/** Configuration. Default profile is linear/instant, which is the neutral behaviour. */
	FVehicleInputProfile Profile;
	ERacingInputDeviceType DeviceType = ERacingInputDeviceType::Unknown;
	ETransmissionInputMode TransmissionMode = ETransmissionInputMode::Automatic;
	float MaxDeltaSeconds = 0.1f;

	/** Held weakly: input config is content and may be unloaded under a raw pointer. */
	TWeakObjectPtr<const UVehicleInputConfigDataAsset> ConfigAsset;

	/** Rate-limited axis state. These are what make the layer frame-rate dependent if mishandled. */
	float CurrentThrottle = 0.0f;
	float CurrentBrake = 0.0f;
	float CurrentSteer = 0.0f;

	/** Edge detection for the shift slots. */
	bool bShiftUpWasHeld = false;
	bool bShiftDownWasHeld = false;

	/** Reset hold accumulator, seconds, and the latch that makes the request one-shot. */
	float ResetHeldSeconds = 0.0f;
	bool bResetLatched = false;

	FVehicleInputCommand LastCommand;
};
