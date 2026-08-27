// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "Containers/StaticArray.h"
#include "CoreMinimal.h"
#include "Core/RacingSimBuildId.h"
#include "Core/RacingSimTypes.h"
#include "Core/RacingSimUnits.h"
#include "Core/RacingTelemetry.h"
#include "Vehicle/VehicleChaosInputMapping.h"
#include "VehicleTelemetryTypes.generated.h"

// Forward-declared at GLOBAL scope, deliberately, and this is not a style preference.
// An elaborated type specifier (`const class UVehicleTuneDataAsset*`) written inside
// `namespace RacingSim::Vehicle` declares a BRAND NEW class in THAT namespace rather
// than referring to the global one -- the two are unrelated types, and the resulting
// C2664 ("Types pointed to are unrelated") appears at the call site rather than at the
// declaration, which makes it read like a caller bug. Caught by the compiler on this
// ticket's first build; recorded so the next person adding a namespaced free function
// here does not rediscover it.
class UChaosWheeledVehicleMovementComponent;
class UPrimitiveComponent;
class UVehicleTuneDataAsset;

/**
 * VEH-004: the physics-facing vehicle telemetry contract.
 *
 * ---------------------------------------------------------------------------
 * Why this exists when Core/RacingTelemetry.h already has a vehicle sample
 * ---------------------------------------------------------------------------
 *
 * FRacingVehicleTelemetrySample (CORE-002) is the HUD-FACING contract. Race/
 * assembles it into an FRacingTelemetryFrame, UI-001 formats it, and it holds exactly
 * what a speedometer and a gear indicator need: speed, RPM, gear, four input axes.
 * It has no per-wheel data, no suspension state, no contact state and no failure
 * information, and it should not grow any -- a HUD that can read a spring force is a
 * HUD that will eventually make a decision with one.
 *
 * FVehicleTelemetrySnapshot is the PHYSICS-FACING contract. It is what
 * Docs/02-VehiclePhysics.md's "Telemetry schema" section asks for, it is what
 * RacingSim::Vehicle::EvaluateVehicleFailures consumes, and it is what a VEH-006 soak
 * run would record. The two are deliberately separate types rather than one type with
 * optional fields, because they have different consumers, different lifetimes and
 * different stability requirements: breaking the HUD contract breaks UI, breaking this
 * one breaks a diagnostic.
 *
 * The relationship is one-directional and lossy on purpose: everything in
 * FRacingVehicleTelemetrySample can be derived from a snapshot, and nothing here is
 * derived from it.
 *
 * ---------------------------------------------------------------------------
 * NO CHAOS TYPE IS DECLARED HERE
 * ---------------------------------------------------------------------------
 *
 * Same Phase 2 rule UVehicleTuneDataAsset follows (see its header). Chaos'
 * FWheelStatus is READ by RacingSim::Vehicle::CaptureVehicleTelemetry and copied field
 * by field into FVehicleWheelTelemetry; it is never stored, never forwarded and never
 * named in this header. Docs/02-VehiclePhysics.md's Phase 2 plan promises that a
 * project-owned tyre/suspension layer can replace stock Chaos while preserving "the
 * same UCarSpecDataAsset, input, telemetry, and race interfaces" -- a telemetry struct
 * embedding a Chaos type would make that promise unkeepable on the first day it was
 * needed.
 *
 * ---------------------------------------------------------------------------
 * Units, stated once and applied everywhere
 * ---------------------------------------------------------------------------
 *
 *   distance        CENTIMETRES (Unreal's unit; Core/RacingSimUnits.h converts)
 *   speed           CENTIMETRES PER SECOND, signed (negative forward speed == reversing)
 *   angular rate    DEGREES PER SECOND
 *   angles          DEGREES
 *   force           NEWTONS
 *   torque          NEWTON-METRES
 *   timestamps      SECONDS, double, monotonic (FPlatformTime class of source)
 *   durations       SECONDS, float
 *   engine speed    RPM (not an Unreal unit; RPM is RPM)
 *
 * Coordinate convention is unchanged from VEH-001: Unreal is left-handed, Z-up,
 * X-forward, Y-right, and positive steering is RIGHT (+Z yaw).
 *
 * ---------------------------------------------------------------------------
 * Allocation and copy cost
 * ---------------------------------------------------------------------------
 *
 * Flat and trivially copyable: no TArray, no pointer, no UObject reference. The wheel
 * array is a fixed-size TStaticArray sized by NumPrototypeVehicleWheels' value rather
 * than a TArray, so a snapshot can be copied into a ring buffer at sample rate without
 * allocating. CLAUDE.md: "Avoid per-frame allocations".
 */

/**
 * Layout version of FVehicleTelemetrySnapshot.
 *
 * HAND-BUMPED when a field is added, removed or reinterpreted -- not when a value
 * changes. Same contract as UVehicleTuneDataAsset::TuneSchemaVersion and
 * ATrackDefinitionActor::TrackSchemaVersion. A recorded snapshot that does not carry
 * its own layout version is unreadable the first time the layout changes, which is the
 * failure this constant exists to prevent.
 */
inline constexpr int32 VehicleTelemetrySchemaVersion = 1;

/**
 * Fixed wheel count for a telemetry snapshot.
 *
 * Deliberately NOT NumPrototypeVehicleWheels (PrototypeVehicleWheel.h): that constant
 * describes the prototype CAR, and this one describes the SNAPSHOT's capacity. They
 * happen to be equal today. Keeping them separate means a future six-wheeled test rig
 * does not silently truncate its telemetry, because the capture reports a
 * WheelCountMismatch rather than dropping wheels on the floor.
 */
inline constexpr int32 MaxVehicleTelemetryWheels = 4;

/**
 * One wheel's simulation state for one sample.
 *
 * Mirrors the subset of Chaos' FWheelStatus (ChaosWheeledVehicleMovementComponent.h:78)
 * that UE 5.8.1 actually publishes, renamed into this project's unit-suffixed
 * convention.
 *
 * WHAT Docs/02-VehiclePhysics.md ASKS FOR AND CHAOS DOES NOT EXPOSE, named rather than
 * silently omitted (all verified by reading FWheelStatus in UE 5.8.1, not assumed):
 *
 *   - per-wheel ANGULAR SPEED: absent from FWheelStatus. FWheelSnapshot carries
 *     WheelAngularVelocity, but that type is only produced by GetSnapshot()/SetSnapshot(),
 *     a save/restore path, not a per-frame output. Reachable in future by widening the
 *     capture to the snapshot API; deliberately not done here because that call is not
 *     free and this ticket has no consumer for the value.
 *   - NORMAL LOAD: absent. SpringForceN is the closest published quantity and is NOT the
 *     same thing (it excludes the unsprung mass and any anti-roll contribution), so it is
 *     recorded under its own name and must not be read as a tyre load.
 *   - LONGITUDINAL SLIP: absent. SlipAngleDegrees (lateral) and SlipMagnitude (a scalar
 *     speed difference, not a normalised slip ratio) are what exist.
 *   - SUSPENSION VELOCITY: absent. Derivable by a consumer from two snapshots and their
 *     timestamps; deliberately not pre-derived here so the snapshot stays a measurement
 *     rather than a mixture of measurements and estimates.
 *   - TCS state: absent (bABSActivated exists; there is no traction-control equivalent).
 *     Docs/02-VehiclePhysics.md item 11 makes ABS/TCS a separate controller anyway.
 *   - SURFACE/MATERIAL IDENTIFIER: FWheelStatus carries a TWeakObjectPtr<UPhysicalMaterial>.
 *     A weak UObject pointer must not go into a struct that is copied into a ring buffer
 *     at sample rate, so the capture records only whether a material was present; naming
 *     the surface needs a material->id table that no ticket has authored yet.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FVehicleWheelTelemetry
{
	GENERATED_BODY()

	/** True when Chaos' suspension raycast found ground for this wheel this step. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	bool bInContact = false;

	/**
	 * Suspension length normalised to its own travel: 0 == fully compressed (on the
	 * bump stop), 1 == fully extended (drooping, wheel unloaded).
	 *
	 * Chaos initialises this to 1.0 for a wheel with no contact (FWheelStatus::Init),
	 * so "1.0 and not in contact" is the airborne resting state, not an error. A value
	 * OUTSIDE [0,1] is what the failure detector treats as unstable wheel state.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	float NormalisedSuspensionLength = 1.0f;

	/** Suspension offset from rest, CENTIMETRES. Signed; Chaos' own sign convention, passed through unaltered. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	float SuspensionOffsetCm = 0.0f;

	/** Spring force at the suspension, NEWTONS. NOT the tyre normal load -- see the struct comment. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	float SpringForceN = 0.0f;

	/** Lateral slip angle, DEGREES: the angle between the wheel's heading and its velocity. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	float SlipAngleDegrees = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	bool bIsSlipping = false;

	/** Speed difference between the wheel surface and the ground, CENTIMETRES PER SECOND. Not a normalised slip ratio. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	float SlipMagnitudeCms = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	bool bIsSkidding = false;

	/** Skid magnitude, CENTIMETRES PER SECOND. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	float SkidMagnitudeCms = 0.0f;

	/** Drive torque currently applied at this wheel, NEWTON-METRES. Signed. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	float DriveTorqueNm = 0.0f;

	/** Brake torque currently applied at this wheel, NEWTON-METRES. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	float BrakeTorqueNm = 0.0f;

	/** ABS engaged at this wheel. Chaos' own flag; VEH-004 does not implement ABS (Docs/02-VehiclePhysics.md item 11). */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	bool bAbsActive = false;

	/** Contact point, WORLD SPACE, CENTIMETRES. Meaningless unless bInContact. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	FVector ContactPointCm = FVector::ZeroVector;

	/** True when Chaos reported a physical material at the contact. The material itself is deliberately not stored -- see the struct comment. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	bool bHasContactMaterial = false;

	/** Every float and vector in this wheel's state is finite. Cheap enough to call per wheel per sample. */
	bool IsFinite() const
	{
		return FMath::IsFinite(NormalisedSuspensionLength)
			&& FMath::IsFinite(SuspensionOffsetCm)
			&& FMath::IsFinite(SpringForceN)
			&& FMath::IsFinite(SlipAngleDegrees)
			&& FMath::IsFinite(SlipMagnitudeCms)
			&& FMath::IsFinite(SkidMagnitudeCms)
			&& FMath::IsFinite(DriveTorqueNm)
			&& FMath::IsFinite(BrakeTorqueNm)
			&& !ContactPointCm.ContainsNaN();
	}
};

/**
 * One complete vehicle-state sample, at one instant, for one car.
 *
 * VERSIONED (SchemaVersion) and IDENTIFIED (CarSpecVersion). Both are load-bearing:
 * without the first a recording is unreadable after the next layout change, and
 * without the second a recording cannot say which car produced it -- which is the same
 * hole FRacingSimVersionStamp::IsPublishable() refuses a race result over.
 *
 * bIsValid distinguishes "a sample with all-zero values" from "no sample was taken".
 * A default-constructed snapshot is the SECOND of those, and every consumer must check
 * it: a stationary car at the origin is a legitimate and completely ordinary reading,
 * so an all-zero snapshot cannot be used as its own absence sentinel.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FVehicleTelemetrySnapshot
{
	GENERATED_BODY()

	/** False on a default-constructed snapshot; true only once a capture has filled it. See the struct comment. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	bool bIsValid = false;

	/** Layout version of this struct; see VehicleTelemetrySchemaVersion. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	int32 SchemaVersion = VehicleTelemetrySchemaVersion;

	/**
	 * Which tune produced this sample. Unpopulated when no tune is in force, or when
	 * the tune was NOT actually applied to the movement component -- see
	 * RacingSim::Vehicle::ResolveCarSpecVersion, which is the one place that decision
	 * is made for both this field and URaceResultRecorder::SetCarSpecVersion.
	 *
	 * Not a UPROPERTY: FRacingContentVersion is a plain struct in Core/, not a USTRUCT,
	 * so it cannot be reflected. Blueprint reads the derived accessors instead.
	 */
	FRacingContentVersion CarSpecVersion;

	/** Monotonic SECONDS. Same class of source as RACE-001's lap clock (FPlatformTime), supplied by the caller. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	double TimestampSeconds = 0.0;

	/**
	 * The DeltaSeconds the game thread observed for the frame this sample was taken on.
	 *
	 * Recorded, not used as a clock: the detector derives elapsed time from consecutive
	 * TimestampSeconds so that it cannot be fooled by a frame that lied about its own
	 * delta. This field is here so a recording can show frame pacing next to the state
	 * it produced, which is what makes a frame-rate-dependence bug visible in telemetry
	 * rather than only in a test.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	float FrameDeltaSeconds = 0.0f;

	/**
	 * Monotonically increasing capture index, starting at 1.
	 *
	 * Docs/02-VehiclePhysics.md's schema asks for "monotonic timestamp and physics
	 * step". Chaos does not publish its internal substep counter through any public
	 * accessor in UE 5.8.1, so this is the CAPTURE index, not the physics step index,
	 * and it is named CaptureIndex rather than PhysicsStep to avoid implying otherwise.
	 * A gap in this sequence means capture was decimated or skipped, which is exactly
	 * what a soak run needs to be able to tell.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	int64 CaptureIndex = 0;

	// -- Chassis rigid-body state ------------------------------------------

	/** World location, CENTIMETRES. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	FVector LocationCm = FVector::ZeroVector;

	/** World rotation. Degrees, Unreal's FRotator convention. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	FRotator Rotation = FRotator::ZeroRotator;

	/** World linear velocity, CENTIMETRES PER SECOND. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	FVector VelocityCms = FVector::ZeroVector;

	/** World angular velocity, DEGREES PER SECOND. Z is yaw rate. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	FVector AngularVelocityDegreesPerSecond = FVector::ZeroVector;

	/** Signed forward speed, CENTIMETRES PER SECOND. Negative means reversing. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	float ForwardSpeedCms = 0.0f;

	// -- Drivetrain --------------------------------------------------------

	/** Engine speed, RPM. 0 when mechanical simulation is disabled. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	float EngineRpm = 0.0f;

	/** Current gear. 0 neutral, negative reverse -- Chaos' convention, passed through. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	int32 GearIndex = 0;

	/** Gear being changed INTO. Differs from GearIndex only during a shift. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	int32 TargetGearIndex = 0;

	// -- Input, as Chaos received it ---------------------------------------

	/**
	 * The MAPPED axes, not the raw command.
	 *
	 * Deliberate: FVehicleInputCommand is what the driver asked for and
	 * FVehicleChaosInput is what the physics actually got, and the interesting bugs
	 * live in the gap (a refused non-finite command, the handbrake threshold, the
	 * manual-shift gate). Recording the command would answer the less useful question.
	 *
	 * UPROPERTY without Blueprint exposure: FVehicleChaosInput is a plain USTRUCT
	 * (VEH-002) and is not BlueprintType, and widening VEH-002's published contract to
	 * suit a telemetry field would be the wrong ticket changing the wrong type. A
	 * USTRUCT cannot declare UFUNCTIONs either, so the accessors below are plain C++;
	 * the Blueprint-facing view of an input axis is CORE-002's
	 * FRacingVehicleTelemetrySample, which ToRacingVehicleSample() produces.
	 */
	UPROPERTY()
	FVehicleChaosInput ChaosInput;

	/** [0,1], as Chaos received it. */
	float GetThrottleInput() const { return ChaosInput.Throttle; }

	/** [0,1], as Chaos received it. */
	float GetBrakeInput() const { return ChaosInput.Brake; }

	/** [-1,1]; positive is RIGHT. */
	float GetSteerInput() const { return ChaosInput.Steering; }

	/** Chaos takes a bool, not the analog handbrake -- see MapCommandToChaosInput's threshold. */
	bool IsHandbrakeEngaged() const { return ChaosInput.bHandbrake; }

	/** Clutch, [0,1]. Carried from the command because Chaos exposes no clutch axis to map it onto (see MapCommandToChaosInput). */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	float ClutchInput = 0.0f;

	/** VEH-001's EVehicleInputCorrection bitmask for the command behind this sample. Telemetry only; nothing branches on it. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry", meta = (Bitmask, BitmaskEnum = "/Script/RacingSim.EVehicleInputCorrection"))
	uint8 InputCorrections = 0;

	/** Which device produced the command behind this sample. */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	ERacingInputDeviceType InputDeviceType = ERacingInputDeviceType::Unknown;

	// -- Per wheel ---------------------------------------------------------

	/**
	 * How many entries of Wheels are populated. May be less than
	 * MaxVehicleTelemetryWheels (an unconfigured vehicle reports 0) and is CLAMPED to
	 * it by the capture, which also reports the truncation.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle|Telemetry")
	int32 NumWheels = 0;

	/**
	 * Fixed-size, so a snapshot copies without allocating. Entries at or beyond
	 * NumWheels are default-constructed and must not be read.
	 *
	 * Not a UPROPERTY: TStaticArray is not a reflected container. Blueprint reads
	 * GetWheel() instead, which also bounds-checks.
	 */
	TStaticArray<FVehicleWheelTelemetry, MaxVehicleTelemetryWheels> Wheels;

	/** Bounds-checked wheel access. Returns a default (all-zero, not-in-contact) entry for an out-of-range index rather than asserting. */
	const FVehicleWheelTelemetry& GetWheel(const int32 Index) const
	{
		static const FVehicleWheelTelemetry Absent;
		return (Index >= 0 && Index < NumWheels && Index < MaxVehicleTelemetryWheels) ? Wheels[Index] : Absent;
	}

	/** How many populated wheels report ground contact. */
	int32 CountWheelsInContact() const
	{
		int32 Count = 0;
		for (int32 Index = 0; Index < NumWheels && Index < MaxVehicleTelemetryWheels; ++Index)
		{
			Count += Wheels[Index].bInContact ? 1 : 0;
		}
		return Count;
	}

	/**
	 * Every float, vector and rotator in this snapshot is finite.
	 *
	 * This is the single check the NonFiniteState detector is built on, and it covers
	 * the wheels too. It is a whole-snapshot predicate rather than a per-field one on
	 * purpose: a NaN anywhere in a rigid body's state has already corrupted the solver
	 * by the time it is observable, so which field it landed in is a diagnostic detail,
	 * not a decision input.
	 */
	bool IsFinite() const
	{
		if (!FMath::IsFinite(TimestampSeconds)
			|| !FMath::IsFinite(FrameDeltaSeconds)
			|| !FMath::IsFinite(ForwardSpeedCms)
			|| !FMath::IsFinite(EngineRpm)
			|| !FMath::IsFinite(ClutchInput)
			|| !FMath::IsFinite(ChaosInput.Throttle)
			|| !FMath::IsFinite(ChaosInput.Brake)
			|| !FMath::IsFinite(ChaosInput.Steering)
			|| LocationCm.ContainsNaN()
			|| VelocityCms.ContainsNaN()
			|| AngularVelocityDegreesPerSecond.ContainsNaN()
			|| Rotation.ContainsNaN())
		{
			return false;
		}

		for (int32 Index = 0; Index < NumWheels && Index < MaxVehicleTelemetryWheels; ++Index)
		{
			if (!Wheels[Index].IsFinite())
			{
				return false;
			}
		}

		return true;
	}

	/** UNIT BOUNDARY: cm/s -> km/h, for display or report only. Never store the result. */
	double GetForwardSpeedKph() const
	{
		return RacingSim::Units::CmsToKilometresPerHour(ForwardSpeedCms);
	}

	/** Speed as a magnitude, CENTIMETRES PER SECOND, from the full velocity vector rather than the forward axis. */
	double GetSpeedMagnitudeCms() const
	{
		return VelocityCms.Size();
	}

	/**
	 * Build the CORE-002 HUD-facing sample from this one.
	 *
	 * The one-directional bridge described in the file header. Lives here rather than
	 * in Core/ because Core must not know about Vehicle/, and the conversion is lossy
	 * in this direction only.
	 */
	FRacingVehicleTelemetrySample ToRacingVehicleSample() const;
};

/**
 * Everything a capture needs that does not come from the movement component.
 *
 * A parameter object rather than eleven arguments, so a caller cannot silently pass
 * the frame delta where the timestamp goes -- the two are both floating-point seconds
 * and a positional mix-up would produce a plausible, wrong recording.
 */
struct FVehicleTelemetryCaptureInput
{
	/**
	 * The Chaos movement component to read. Non-const because
	 * UChaosWheeledVehicleMovementComponent::GetSuspensionOffset(int) is a non-const
	 * virtual in UE 5.8.1 (ChaosWheeledVehicleMovementComponent.h:721) -- every other
	 * accessor this capture uses is const. Null yields an invalid snapshot, never a crash.
	 */
	UChaosWheeledVehicleMovementComponent* Movement = nullptr;

	/**
	 * The simulating chassis primitive, for world transform and rigid-body velocities.
	 *
	 * Read from the PRIMITIVE rather than from Chaos' own FVehicleState, which is
	 * `protected` in UE 5.8.1 (ChaosVehicleMovementComponent.h:1161 opens the access
	 * block that 1283 sits in) and therefore not reachable from a pawn without either
	 * subclassing the component or a const_cast. Neither is worth doing for data the
	 * engine already publishes on the primitive.
	 */
	const UPrimitiveComponent* Chassis = nullptr;

	/** The axes Chaos was actually given this frame. */
	FVehicleChaosInput ChaosInput;

	/** [0,1]; carried from the command because Chaos has no clutch axis to map onto. */
	float ClutchInput = 0.0f;

	/** VEH-001's EVehicleInputCorrection bitmask for the command behind this sample. */
	uint8 InputCorrections = 0;

	ERacingInputDeviceType InputDeviceType = ERacingInputDeviceType::Unknown;

	/** From ResolveCarSpecVersion. Unpopulated is legal and means "no tune is in force". */
	FRacingContentVersion CarSpecVersion;

	/** Monotonic SECONDS, supplied by the caller so a test can drive time deterministically. */
	double TimestampSeconds = 0.0;

	/** The frame's DeltaSeconds, recorded for pacing only. See FVehicleTelemetrySnapshot::FrameDeltaSeconds. */
	float FrameDeltaSeconds = 0.0f;

	/** 1-based, monotonically increasing. See FVehicleTelemetrySnapshot::CaptureIndex. */
	int64 CaptureIndex = 0;
};

namespace RacingSim::Vehicle
{
	/**
	 * Read one snapshot from a live movement component and chassis primitive.
	 *
	 * ALLOCATES NOTHING. Every field is a scalar copy out of Chaos' already-computed
	 * FWheelStatus array, and the snapshot's wheel storage is fixed-size.
	 *
	 * BOUNDED BY GetNumWheels(). UChaosWheeledVehicleMovementComponent::GetWheelState
	 * indexes `WheelStatus[WheelIndex]` with NO bounds check
	 * (ChaosWheeledVehicleMovementComponent.h:716-719), so an out-of-range read there
	 * is a crash rather than a bad number. A vehicle with more wheels than
	 * MaxVehicleTelemetryWheels is TRUNCATED and the truncation is logged once per
	 * call, never silently dropped.
	 *
	 * @return an invalid snapshot (bIsValid == false) when Movement is null. A null
	 *         Chassis is survivable and yields a snapshot with no transform or
	 *         velocities but valid drivetrain and wheel state, because a movement
	 *         component with no updated component is a configuration fault worth
	 *         seeing the rest of the state for.
	 */
	RACINGSIM_API FVehicleTelemetrySnapshot CaptureVehicleTelemetry(const FVehicleTelemetryCaptureInput& Input);

	/**
	 * Decide whether a tune may be published as this car's spec version.
	 *
	 * THE POINT OF THIS FUNCTION IS THE SECOND ARGUMENT. CORE-002's
	 * FRacingSimVersionStamp::CarSpecVersion exists so a lap time can name the car that
	 * set it; a version taken from a tune asset that was REFERENCED but never APPLIED
	 * (no chassis, so ApplyChassisAsset() returned before ApplyTuneAsset(); or an
	 * unusable torque curve, so the engine write was refused) would name a car nobody
	 * drove -- which is exactly the "passes every check while describing a car nobody
	 * drove" failure URaceResultRecorder::SetCarSpecVersion's own header warns about.
	 *
	 * Returns an UNPOPULATED FRacingContentVersion in that case, which
	 * FRacingContentVersion::IsPopulated() and FRacingSimVersionStamp::IsPublishable()
	 * both refuse. Honest and unpublishable beats plausible and wrong.
	 *
	 * Pure, and takes a UDataAsset rather than a pawn, so it is reachable at the Smoke
	 * gate -- the same reason MapCommandToChaosInput is a free function.
	 */
	RACINGSIM_API FRacingContentVersion ResolveCarSpecVersion(
		const UVehicleTuneDataAsset* Tune,
		bool bTuneWasApplied);
}
