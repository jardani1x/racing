// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/RacingSimTypes.h"
#include "Core/RacingSimValidation.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataAsset.h"
#include "Vehicle/VehicleInputTypes.h"
#include "VehicleInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * VEH-001: the tuning and the bindings, as data.
 *
 * ---------------------------------------------------------------------------
 * Why a DataAsset and not a config section
 * ---------------------------------------------------------------------------
 *
 * CLAUDE.md permits either ("typed DataAssets or config"). This is a DataAsset
 * because input feel is per-vehicle and per-device: a prototype with a quick rack
 * and a heavy GT car want different steer rates, and a config section is a single
 * global. URacingSimSettings (config) stays the right home for project-wide policy;
 * this is per-car content, and VEH-003's UCarSpecDataAsset will reference one.
 *
 * ---------------------------------------------------------------------------
 * Why CORE-003's EnforceRanges is used for only part of this asset
 * ---------------------------------------------------------------------------
 *
 * RacingSim::Validation::EnforceRanges resolves each declared range BY NAME to an
 * FProperty on the object's own class. That works for this asset's flat scalars and
 * it does not work for a field inside FVehicleInputProfile, which is a nested
 * USTRUCT reached through an FMapProperty -- there is no top-level property named
 * "SteerDeadZone" to resolve, and there are two profiles carrying that name anyway.
 *
 * So the split is deliberate and is stated rather than left to be discovered:
 *
 *   - flat numeric properties on this class go through EnforceRanges, and get the
 *     full CORE-003 treatment (clamp, replacement values, NaN handling, logging);
 *   - FVehicleInputProfile validates its own fields in FVehicleInputProfile::Validate,
 *     which REUSES CORE-003's FRacingPropertyRange/FRacingValidationResult types and
 *     its correction semantics, but applies them directly instead of reflectively.
 *
 * The related known gap is recorded in Docs/Tickets.md as CORE-003 MEDIUM-2: the
 * VerifyRangesMatchMetadata direction-2 sweep filters on CPF_Config, so for a
 * UDataAsset it currently checks nothing in that direction. This asset therefore
 * does NOT get the "someone added a clamped property and forgot the table" guard
 * that URacingSimSettings gets. That is a real, un-closed limitation and it is why
 * RacingSim.Vehicle.InputConfigRanges asserts the profile bounds directly against
 * hand-written known-bad values rather than trusting a metadata cross-check.
 */

/**
 * Per-device-class input shaping.
 *
 * ---------------------------------------------------------------------------
 * Why this is per device and not one global set of numbers
 * ---------------------------------------------------------------------------
 *
 * A keyboard key is a step function: 0 or 1, no in-between, ever. A gamepad trigger
 * is a continuous axis with a noisy resting value. The same numbers cannot serve
 * both, and the failure is not subtle in either direction -- rate-limit an analog
 * trigger and the pedal feels like treacle, do not rate-limit a key and the car
 * snaps to full lock in one frame and is undriveable.
 *
 * Keyed on ERacingInputDeviceType (Core/RacingSimTypes.h) rather than a new enum,
 * so the device recorded on a result (FRacingSimVersionStamp::InputDeviceType) and
 * the device that shaped the input are the same value by construction. Inventing a
 * second device enum here would make "which device set this lap" answerable two
 * ways, which is one way too many.
 *
 * ---------------------------------------------------------------------------
 * Units
 * ---------------------------------------------------------------------------
 *
 * Dead zones, saturations, scales and gammas are DIMENSIONLESS. Rates are
 * NORMALISED UNITS PER SECOND (1/s): a ThrottleRiseRate of 4.0 moves throttle from
 * 0 to 1 in 0.25 s regardless of frame rate. Hold times are SECONDS. Speed
 * thresholds are KILOMETRES PER HOUR -- the only place in this layer with a
 * non-trivial unit, and the conversion from the project's cm/s storage unit happens
 * in VehicleInputProcessor.cpp through RacingSim::Units, never inline.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FVehicleInputProfile
{
	GENERATED_BODY()

	/**
	 * Raw magnitude below which the steering axis reads as centred, [0, 0.9].
	 *
	 * Applied with RESCALING, not subtraction-and-clamp: at a dead zone of 0.1 a raw
	 * 0.1 gives 0 and a raw 1.0 still gives 1.0. A naive subtraction would cost the
	 * driver the top of the axis and no stick would ever reach full lock.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float SteerDeadZone = 0.0f;

	/**
	 * Raw magnitude at which the steering axis reads as fully deflected, (0.1, 1.0].
	 *
	 * Below 1.0 this trades range for reach -- a worn stick that only makes 0.85 can
	 * still get full lock. Must exceed SteerDeadZone or the usable band is empty;
	 * Validate() enforces that as a relationship, which no per-field range can.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float SteerSaturation = 1.0f;

	/**
	 * Response exponent for steering, [0.2, 5.0]. output = input^gamma, sign preserved.
	 *
	 * Greater than 1 gives finer control near centre at the cost of a sharper end of
	 * travel; 1.0 is linear. Exactly 1.0 is the default because a non-linear default
	 * makes every later handling complaint ambiguous between the tune and the curve.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.2", ClampMax = "5.0"))
	float SteerResponseGamma = 1.0f;

	/** Dead zone for throttle/brake/handbrake/clutch, [0, 0.9]. Rescaled, as above. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pedals", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float PedalDeadZone = 0.0f;

	/** Raw magnitude at which a pedal reads as fully applied, (0.1, 1.0]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pedals", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float PedalSaturation = 1.0f;

	/** Response exponent for pedals, [0.2, 5.0]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pedals", meta = (ClampMin = "0.2", ClampMax = "5.0"))
	float PedalResponseGamma = 1.0f;

	/**
	 * Rate limits in NORMALISED UNITS PER SECOND, [0, 1000].
	 *
	 * ZERO MEANS INSTANT, and that is what makes one struct serve both device
	 * classes without a bAnalog flag: a gamepad profile sets every rate to 0 and the
	 * limiter becomes a no-op, a keyboard profile sets real rates and gets ramping.
	 * A boolean plus rates would allow the contradictory state "not rate-limited but
	 * here are my rates", which is a state someone eventually has to interpret.
	 *
	 * Rise and fall are separate because they are not symmetric on any real control:
	 * releasing a pedal should be faster than applying one, and a driver lifting to
	 * catch a slide cannot wait 250 ms for it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float ThrottleRiseRate = 0.0f;

	/** See ThrottleRiseRate. Units 1/s; 0 == instant. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float ThrottleFallRate = 0.0f;

	/** See ThrottleRiseRate. Units 1/s; 0 == instant. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float BrakeRiseRate = 0.0f;

	/** See ThrottleRiseRate. Units 1/s; 0 == instant. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float BrakeFallRate = 0.0f;

	/** Rate toward a larger |steer|, 1/s; 0 == instant. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float SteerRate = 0.0f;

	/**
	 * Rate back toward centre, 1/s; 0 == instant.
	 *
	 * Distinct from SteerRate because self-centring is the half that saves a car. A
	 * keyboard driver who releases the key wants the wheel back NOW; the same driver
	 * turning in wants it to arrive smoothly.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float SteerCentringRate = 0.0f;

	/**
	 * How long the Reset slot must be HELD before a reset is emitted, seconds,
	 * [0.05, 10.0].
	 *
	 * Not zero, and not configurable to zero. A reset sets
	 * ERacingRunValidity::InvalidVehicleReset on the run (Core/RacingSimTypes.h), so
	 * a single mis-keyed press would silently destroy a lap that was otherwise
	 * clean, and the driver would find out at the results screen. The floor of 0.05 s
	 * is deliberately low enough not to be an accessibility problem and high enough
	 * that the hold is a decision.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reset", meta = (ClampMin = "0.05", ClampMax = "10.0"))
	float ResetHoldSeconds = 0.5f;

	/** See EVehiclePedalConflictPolicy. Per-device because a keyboard and a wheel want different answers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pedals")
	EVehiclePedalConflictPolicy PedalConflictPolicy = EVehiclePedalConflictPolicy::BrakeOverrides;

	/**
	 * Validate every field of this profile, plus the two cross-field relationships
	 * (saturation must exceed dead zone, for steering and for pedals).
	 *
	 * @param ProfileName  used only to name the profile in issue messages.
	 * @param bCorrect     when true, out-of-range and non-finite fields are written
	 *                     back with a safe value; when false nothing is mutated and
	 *                     the result is a pure report. Tests use both.
	 */
	RacingSim::Validation::FRacingValidationResult Validate(const FString& ProfileName, bool bCorrect);

	/** Const report-only form. Equivalent to Validate with bCorrect false, on a copy. */
	RacingSim::Validation::FRacingValidationResult ValidateReadOnly(const FString& ProfileName) const;

	/** Sensible starting point for a digital keyboard: real ramp rates, no dead zone, brake wins. */
	static FVehicleInputProfile MakeKeyboardDefault();

	/** Sensible starting point for an analog pad: small dead zones, no rate limiting. */
	static FVehicleInputProfile MakeGamepadDefault();
};

/** How the steering scale is derived from road speed. See UVehicleInputConfigDataAsset. */
UENUM(BlueprintType)
enum class ESteerSpeedScaleMode : uint8
{
	/** No speed sensitivity. Full authority at every speed. */
	Off		UMETA(DisplayName = "Off"),
	/** Linear interpolation from 1.0 at FullAuthoritySpeedKph down to MinSteerScale at MinAuthoritySpeedKph. */
	Linear	UMETA(DisplayName = "Linear ramp"),
	/**
	 * Sampled from SteerScaleBySpeedKphCurve. Selecting this REQUIRES a usable curve;
	 * Validate() fails the asset rather than falling back, because a silent fallback
	 * to Off is the difference between a car that is twitchy at speed and one that
	 * spins on the straight, and the author would have no way to notice.
	 */
	Curve	UMETA(DisplayName = "Curve")
};

/**
 * VEH-001: bindings + tuning for one vehicle's input layer.
 *
 * ---------------------------------------------------------------------------
 * Bindings are SOFT references to assets. Deliberately.
 * ---------------------------------------------------------------------------
 *
 * TSoftObjectPtr rather than a hard UPROPERTY pointer, because a hard reference
 * would pull every UInputAction into memory whenever this asset is touched -- which
 * includes the cook's dependency graph and the streaming path CLAUDE.md's "no
 * synchronous asset loads during a race" rule is about. They are resolved once, at
 * component initialisation, never per frame.
 *
 * ---------------------------------------------------------------------------
 * What VEH-001 does NOT ship, stated plainly
 * ---------------------------------------------------------------------------
 *
 * The UInputMappingContext and UInputAction .uassets themselves do not exist yet.
 * CLAUDE.md forbids editing Unreal binary assets from a worktree and requires a
 * serialized Docs/AssetOwnership.tsv claim, and this ticket is a C++ contract
 * ticket. What VEH-001 owes is the fields, the required-slot rule and the validation
 * that rejects an unbound or null slot -- all of which are here and tested. What it
 * does not owe is content. Until those assets are authored, Validate() on a default
 * asset reports missing bindings, which is the CORRECT answer and is asserted as
 * such by RacingSim.Vehicle.InputConfigBindings.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Vehicle Input Config"))
class RACINGSIM_API UVehicleInputConfigDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * CORE-003 range pass over this class's FLAT numeric properties only. See the
	 * file header for why the profiles are not covered by it.
	 */
	static TConstArrayView<RacingSim::Validation::FRacingPropertyRange> StaticRanges();

	/**
	 * Full validation: flat ranges, every profile, the binding completeness rule, and
	 * the curve.
	 *
	 * @param bCorrect  true to write safe values back over out-of-range numerics.
	 *                  Binding and curve problems are NEVER "corrected" -- there is
	 *                  no safe substitute for a missing UInputAction, and inventing
	 *                  one would produce a config that validates and cannot drive.
	 */
	RacingSim::Validation::FRacingValidationResult Validate(bool bCorrect);

	/** Which slots must be bound given TransmissionMode. Shift slots are required only under Manual. */
	void GetRequiredActions(TArray<EVehicleInputAction>& OutActions) const;

	/** Profile for a device, or nullptr when none is authored. Never falls back silently -- the caller decides. */
	const FVehicleInputProfile* FindProfile(ERacingInputDeviceType DeviceType) const;

	/**
	 * Steering authority scale in [MinSteerScale, 1] for a speed given in
	 * CENTIMETRES PER SECOND (the project storage unit).
	 *
	 * The cm/s -> km/h conversion happens inside, once, through
	 * RacingSim::Units::CmsToKilometresPerHour. Callers must not pre-convert.
	 * A non-finite or negative speed is treated as 0 (stationary, full authority)
	 * rather than propagating: this function is on the steering path and returning a
	 * NaN scale here would put a NaN into the command that VEH-004 would then have
	 * to catch downstream.
	 */
	float GetSteerScaleForSpeedCms(double SpeedCms) const;

	/** Whether the driver shifts. Decides which slots GetRequiredActions demands. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transmission")
	ETransmissionInputMode TransmissionMode = ETransmissionInputMode::Automatic;

	/**
	 * Upper bound on the DeltaSeconds one Tick may integrate, seconds, [0.001, 1.0].
	 *
	 * This is the frame-rate-independence guard, and it guards a real event rather
	 * than a hypothetical one: a Pixel Streaming session that is backgrounded,
	 * hitches on a shader compile, or resumes after the browser tab is restored will
	 * deliver a DeltaSeconds measured in seconds. Without a clamp, one such frame
	 * moves a rate-limited axis across its entire range in a single step -- full lock
	 * from centre -- which is exactly the "frame-rate dependence" failure Gate B
	 * forbids. Clamping costs a slightly slow ramp across a hitch and is recorded on
	 * the command as EVehicleInputCorrection::DeltaClamped rather than hidden.
	 *
	 * The default of 0.1 s is 6 frames at 60 Hz: long enough that ordinary jitter
	 * never trips it, short enough that a real hitch cannot slam a control.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float MaxDeltaSeconds = 0.1f;

	/**
	 * VEH-004: how long a raw device sample may go unrefreshed before every demand is
	 * neutralised, SECONDS. 0 DISABLES the guard.
	 *
	 * Closes VEH-001 MEDIUM-4 ("stuck input at the browser trust boundary"), which
	 * VEH-002 deferred and re-routed here. Enhanced Input reports a held control by
	 * firing Triggered every frame, so a live driver's stamp is always fresh; only a
	 * dead connection, a backgrounded tab or a lost focus stops it advancing. See
	 * EVehicleInputCorrection::StaleSample.
	 *
	 * ONE CORRECTION (re-review, VEH-004 pass 2): this guard only ever ACTS when the
	 * stale sample still names a non-zero demand or a held control. A driver who
	 * released every control before the connection died has nothing to neutralise, and
	 * this timeout never fires for them -- it is not "how long until the input layer
	 * intervenes at all", only "how long a still-latched demand may survive silence".
	 *
	 * The default of 1.0 s is deliberately long. This mechanism zeroes a driver's
	 * throttle, so a false positive is a car that mysteriously lifts off mid-corner --
	 * far worse than a real positive detected 500 ms late, since the car is already
	 * uncontrolled by then either way. One second is ~60 frames of total input silence
	 * at 60 Hz, which no live session produces.
	 *
	 * NOT clamped to a minimum by the processor: a non-finite or negative value
	 * disables the guard rather than arming it on some invented timescale. Failing
	 * toward "do nothing" is the only safe direction for an intervention this strong.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float InputStaleAfterSeconds = 1.0f;

	/** Device assumed before any input has been seen. Must have a profile; Validate() enforces that. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bindings")
	ERacingInputDeviceType DefaultDeviceType = ERacingInputDeviceType::Keyboard;

	/**
	 * One UInputMappingContext per supported device class.
	 *
	 * A map rather than two named fields so that adding Wheel or RemoteStreamed later
	 * is a content change, not a schema change plus a recompile plus a C++ review.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bindings")
	TMap<ERacingInputDeviceType, TSoftObjectPtr<UInputMappingContext>> MappingContexts;

	/**
	 * Slot -> UInputAction. THE indirection that keeps keys out of C++.
	 *
	 * Rebinding a control is an edit to a UInputMappingContext asset. Nothing in
	 * Source/RacingSim/Vehicle/ names an FKey, and RacingSim.Vehicle.InputNoHardcodedKeys
	 * fails the suite if that ever stops being true.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bindings")
	TMap<EVehicleInputAction, TSoftObjectPtr<UInputAction>> ActionBindings;

	/** Per-device shaping. Keyboard and Gamepad are both REQUIRED -- Docs/00-ExecutivePlan.md names both as shipping targets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response")
	TMap<ERacingInputDeviceType, FVehicleInputProfile> Profiles;

	/** See ESteerSpeedScaleMode. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering")
	ESteerSpeedScaleMode SteerSpeedScaleMode = ESteerSpeedScaleMode::Off;

	/** At or below this road speed, steering has full authority (scale 1). KM/H. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float FullAuthoritySpeedKph = 50.0f;

	/** At or above this road speed, steering is scaled by MinSteerScale. KM/H. Must exceed FullAuthoritySpeedKph. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float MinAuthoritySpeedKph = 250.0f;

	/**
	 * Steering scale at and above MinAuthoritySpeedKph, [0.05, 1.0].
	 *
	 * Floored at 0.05 rather than 0: a scale of 0 is a car that cannot be steered at
	 * speed at all, which is not a tune, it is a bug that looks like a tune.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float MinSteerScale = 0.35f;

	/**
	 * Steering scale against road speed in KM/H. Required when SteerSpeedScaleMode
	 * is Curve, ignored otherwise.
	 *
	 * Validation requires at least 2 keys, finite times and values, non-negative
	 * times, and every value within [0.05, 1]. A curve that returns 0 or 3.7 at some
	 * speed is a steering authority multiplier that either locks the wheel or
	 * triples the driver's input, and neither is discoverable by looking at the car.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering")
	FRuntimeFloatCurve SteerScaleBySpeedKphCurve;
};
