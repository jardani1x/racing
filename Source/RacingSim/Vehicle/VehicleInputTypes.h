// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/RacingSimTypes.h"
#include "VehicleInputTypes.generated.h"

/**
 * VEH-001: the vehicle input contract.
 *
 * ---------------------------------------------------------------------------
 * Units, and why there are none
 * ---------------------------------------------------------------------------
 *
 * Every value in FVehicleInputCommand is NORMALISED AND DIMENSIONLESS. Pedals and
 * the handbrake are [0,1]; steer is [-1,1]. There are deliberately no centimetres,
 * no newtons, no newton-metres and no radians in this struct.
 *
 * That is a scope decision, not an oversight. Turning "steer = 0.4" into a road
 * wheel angle needs a steering ratio and a lock angle, and turning "throttle = 0.4"
 * into a torque needs an engine curve -- both of which live in VEH-003's tune data
 * and are applied by VEH-002's Chaos movement component. If VEH-001 emitted an
 * angle it would be committing to a steering rack that does not exist yet and that
 * it has no way to test, and every later change to that rack would become a change
 * to this contract. Normalised commands are the only thing this layer can be
 * correct about on its own.
 *
 * The one genuine physical quantity that crosses this boundary is vehicle speed,
 * which the speed-sensitive steering rule consumes. It is CENTIMETRES PER SECOND on
 * the way in, because that is the project's storage unit (Core/RacingSimUnits.h),
 * and it is converted to km/h through RacingSim::Units::CmsToKilometresPerHour
 * before it touches a threshold or a curve. That conversion is named at its call
 * site in VehicleInputProcessor.cpp and pinned by RacingSim.Vehicle.InputUnits.
 *
 * ---------------------------------------------------------------------------
 * Coordinate convention
 * ---------------------------------------------------------------------------
 *
 * Unreal is left-handed, Z-up, X-forward, Y-right. Positive SteerInput means
 * steering RIGHT, which is a positive yaw rate about +Z. Negative is left. This is
 * stated here because it is the sign convention VEH-002 must not silently invert;
 * an inverted steering axis is the kind of defect that looks like a physics bug for
 * a week.
 *
 * ---------------------------------------------------------------------------
 * Stability warning
 * ---------------------------------------------------------------------------
 *
 * Per Core/RacingSimTypes.h's note on UENUM stability: once a DataAsset or Blueprint
 * references EVehicleInputAction, the enumerator NAMES are part of the
 * /Script/RacingSim path. Append entries; do not renumber or repurpose them.
 */

/**
 * One bindable control. These are SLOTS, not keys.
 *
 * A slot is what the game asks for; a UInputAction asset bound to it in a
 * UInputMappingContext is what a device actually produces. Nothing in
 * Source/RacingSim/Vehicle/ may name an FKey -- see UVehicleInputConfigDataAsset and
 * RacingSim.Vehicle.InputNoHardcodedKeys, which enforces that by source scan.
 */
UENUM(BlueprintType)
enum class EVehicleInputAction : uint8
{
	/** Analog [0,1]. Gamepad right trigger, or a digital key ramped by the profile. */
	Throttle	UMETA(DisplayName = "Throttle"),

	/** Analog [0,1]. Foundation brake, not the handbrake. */
	Brake		UMETA(DisplayName = "Brake"),

	/** Analog [-1,1]. Positive is right. */
	Steer		UMETA(DisplayName = "Steer"),

	/** Analog [0,1] -- analog rather than a bool because a handbrake is progressive on a wheel and on some pads. */
	Handbrake	UMETA(DisplayName = "Handbrake"),

	/** Analog [0,1], 1 == fully disengaged. Only meaningful under a manual transmission. */
	Clutch		UMETA(DisplayName = "Clutch"),

	/** Digital, edge-triggered. Required only under a manual transmission. */
	ShiftUp		UMETA(DisplayName = "Shift up"),

	/** Digital, edge-triggered. Required only under a manual transmission. */
	ShiftDown	UMETA(DisplayName = "Shift down"),

	/**
	 * Digital, HELD. Not edge-triggered, on purpose: a reset invalidates a lap
	 * (ERacingRunValidity::InvalidVehicleReset), so a single accidental keypress
	 * must not be able to destroy a run. See FVehicleInputProfile::ResetHoldSeconds.
	 */
	Reset		UMETA(DisplayName = "Reset")
};

/** Iteration bound for EVehicleInputAction. Update with the enum; asserted by RacingSim.Vehicle.InputConfig. */
inline constexpr int32 NumVehicleInputActions = 8;

/**
 * What the driver asked the gearbox to do this sample.
 *
 * One-shot: produced on a rising edge and cleared the next Tick, so a consumer that
 * misses a Tick misses the shift rather than shifting twice. Under
 * ETransmissionInputMode::Automatic this is always None and the shift slots are
 * optional.
 */
UENUM(BlueprintType)
enum class EVehicleGearRequest : uint8
{
	None		UMETA(DisplayName = "None"),
	ShiftUp		UMETA(DisplayName = "Shift up"),
	ShiftDown	UMETA(DisplayName = "Shift down")
};

/** Whether the driver shifts, or the drivetrain does. Selected by the tune, consumed here only to decide which slots are required. */
UENUM(BlueprintType)
enum class ETransmissionInputMode : uint8
{
	/** Drivetrain shifts. ShiftUp/ShiftDown/Clutch slots may be unbound. */
	Automatic	UMETA(DisplayName = "Automatic"),
	/** Driver shifts. ShiftUp and ShiftDown become REQUIRED bindings; Validate() rejects a config without them. */
	Manual		UMETA(DisplayName = "Manual")
};

/**
 * What happens when throttle and brake are applied together.
 *
 * This is a real decision and not a preference. On a keyboard both are digital, and
 * left-foot braking is a legitimate technique on a pad -- but a browser client with
 * a stuck key can also send both pinned, and "both pinned" must have a defined,
 * stable meaning rather than whatever the drivetrain happens to do with it.
 */
UENUM(BlueprintType)
enum class EVehiclePedalConflictPolicy : uint8
{
	/** Both pass through untouched. Required for left-foot braking and for trail braking on a wheel. */
	Independent			UMETA(DisplayName = "Independent (allow overlap)"),
	/** Brake wins: any brake input zeroes throttle. The safe default for a digital keyboard. */
	BrakeOverrides		UMETA(DisplayName = "Brake overrides throttle"),
	/** Whichever is larger survives; ties go to the brake. */
	LargerWins			UMETA(DisplayName = "Larger input wins (ties to brake)")
};

/**
 * Why a command differed from the raw sample that produced it.
 *
 * A bitmask rather than a single enum because more than one correction can apply to
 * one sample, and collapsing them would hide the interesting combination -- a
 * non-finite axis on a hitched frame is a different story from either alone.
 *
 * This is telemetry, not control flow: nothing downstream branches on it. It exists
 * so VEH-004 can answer "was the input layer fighting the input?" from a recording,
 * which is not answerable from the corrected values alone.
 */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EVehicleInputCorrection : uint8
{
	None			= 0			UMETA(Hidden),
	/** An incoming axis was NaN or infinite and was replaced with a safe value. */
	NonFinite		= 1 << 0	UMETA(DisplayName = "Non-finite axis"),
	/** An incoming axis was outside its declared domain and was clamped. */
	OutOfRange		= 1 << 1	UMETA(DisplayName = "Out-of-range axis"),
	/** DeltaSeconds was non-finite, negative, or longer than MaxDeltaSeconds, and was clamped. */
	DeltaClamped	= 1 << 2	UMETA(DisplayName = "Delta clamped"),
	/** The pedal-conflict policy suppressed throttle or brake. */
	PedalConflict	= 1 << 3	UMETA(DisplayName = "Pedal conflict resolved")
};
ENUM_CLASS_FLAGS(EVehicleInputCorrection);

/**
 * The normalised command for one sample. THIS is the contract VEH-002/VEH-004/UI-001
 * consume; nothing downstream should ever see a UInputAction or an FKey.
 *
 * Default-constructed is the safe standing-still command: no throttle, no brake, no
 * steer, no request. That matters because a consumer that fails to get a command --
 * component not yet initialised, config missing, controller unpossessed -- gets a
 * coasting car rather than an undefined one.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FVehicleInputCommand
{
	GENERATED_BODY()

	/** [0,1]. Dimensionless demand, NOT a torque. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Input")
	float Throttle = 0.0f;

	/** [0,1]. Foundation brake demand, NOT a pressure or a torque. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Input")
	float Brake = 0.0f;

	/** [-1,1]. Positive is RIGHT (+Z yaw). NOT an angle -- VEH-003's steering ratio makes it one. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Input")
	float Steer = 0.0f;

	/** [0,1]. Progressive; a digital binding produces 0 or 1. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Input")
	float Handbrake = 0.0f;

	/** [0,1], 1 == fully disengaged. Always 0 under ETransmissionInputMode::Automatic. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Input")
	float Clutch = 0.0f;

	/** One-shot, cleared on the next Tick. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Input")
	EVehicleGearRequest GearRequest = EVehicleGearRequest::None;

	/**
	 * One-shot: true for exactly ONE command after the Reset slot has been held for
	 * ResetHoldSeconds, and not again until the slot is released and re-held.
	 *
	 * The one-shot guarantee is load-bearing for Race/: URaceLapTracker's
	 * NotifyVehicleReset is not idempotent within a lap in the way a repeated call
	 * would need it to be, and a held key that fired every frame would spam it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Input")
	bool bResetRequested = false;

	/**
	 * Progress toward the reset hold, [0,1]. Purely so UI-001 can draw a hold-to-reset
	 * ring; no gameplay reads it. 0 when the slot is not held.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Input")
	float ResetHoldProgress = 0.0f;

	/**
	 * Monotonic seconds, from the same class of source RACE-001 uses for lap timing
	 * (FPlatformTime), supplied by the caller rather than sampled here so a test can
	 * drive time deterministically. Recorded because Docs/02-VehiclePhysics.md's
	 * telemetry schema requires a monotonic timestamp on every sample.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Input")
	double TimestampSeconds = 0.0;

	/** The CLAMPED delta actually used to integrate this sample. Differs from the caller's when DeltaClamped is set. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Input")
	float DeltaSeconds = 0.0f;

	/** Which profile produced this command. Feeds FRacingSimVersionStamp's InputDeviceType on a result. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Input")
	ERacingInputDeviceType DeviceType = ERacingInputDeviceType::Unknown;

	/** Bitmask of EVehicleInputCorrection. Telemetry only; see the enum comment. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Input", meta = (Bitmask, BitmaskEnum = "/Script/RacingSim.EVehicleInputCorrection"))
	uint8 Corrections = 0;

	bool WasCorrected() const
	{
		return Corrections != 0;
	}

	bool HasCorrection(const EVehicleInputCorrection Correction) const
	{
		return (Corrections & static_cast<uint8>(Correction)) != 0;
	}

	/**
	 * True when every float is finite and inside its declared domain.
	 *
	 * The processor guarantees this for everything it returns, so a consumer does
	 * not need to call it defensively on a hot path. It exists so tests can assert
	 * the guarantee in one call instead of restating six bounds per case, and so
	 * VEH-002 can check a command it received over a boundary the processor did not
	 * own (a replicated or Pixel-Streamed one, later).
	 */
	bool IsFiniteAndInRange() const
	{
		auto InUnit = [](const float Value)
		{
			return FMath::IsFinite(Value) && Value >= 0.0f && Value <= 1.0f;
		};

		return InUnit(Throttle)
			&& InUnit(Brake)
			&& InUnit(Handbrake)
			&& InUnit(Clutch)
			&& InUnit(ResetHoldProgress)
			&& FMath::IsFinite(Steer) && Steer >= -1.0f && Steer <= 1.0f
			&& FMath::IsFinite(DeltaSeconds) && DeltaSeconds >= 0.0f
			&& FMath::IsFinite(TimestampSeconds);
	}
};
