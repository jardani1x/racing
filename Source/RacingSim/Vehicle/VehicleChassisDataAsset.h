// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/RacingSimValidation.h"
#include "Engine/DataAsset.h"
#include "VehicleChassisDataAsset.generated.h"

/**
 * VEH-002: the prototype chassis, as data.
 *
 * ---------------------------------------------------------------------------
 * What this asset is, and what it deliberately is not
 * ---------------------------------------------------------------------------
 *
 * TOPOLOGY, GEOMETRY AND MASS. How many wheels there are, where they sit, how big
 * they are, what drives them, what the chassis collides with, and where its mass is.
 *
 * It is NOT the tune. Engine torque curve, gear ratios, differential bias, brake
 * torques, steering ratio/curve, spring rates and damping are all VEH-003, and the
 * Epic 2 row in Docs/Tickets.md says so by name. The dividing question is simple: a
 * number that answers "what SHAPE is it" lives here, a number that answers "how FAST
 * is it" lives in VEH-003. If that line is crossed, two tickets own the same field and
 * a handling regression has two places to hide.
 *
 * ---------------------------------------------------------------------------
 * There is no Chaos type in this file, on purpose
 * ---------------------------------------------------------------------------
 *
 * Docs/02-VehiclePhysics.md's Phase 2 clause requires that a later project-owned tyre
 * and suspension layer can replace stock Chaos while "the same UCarSpecDataAsset,
 * input, telemetry and race interfaces" survive. A ChaosVehicles include here would
 * make that promise unkeepable: EVehicleDifferential in the data contract means every
 * consumer of the contract depends on a Chaos enum.
 *
 * So this asset declares EVehicleDrivetrainLayout, and ARacingVehiclePawn is the ONE
 * place that maps it onto Chaos. That is also CLAUDE.md's "keep race/UI/streaming
 * dependent on stable project contracts, not Chaos internals" applied one layer
 * earlier than it strictly demands, which is cheap here and expensive later.
 *
 * ---------------------------------------------------------------------------
 * Units and coordinate convention. Stated once, obeyed everywhere.
 * ---------------------------------------------------------------------------
 *
 *   distance   CENTIMETRES (Unreal units; 1 uu == 1 cm, Core/RacingSimUnits.h)
 *   mass       KILOGRAMS (Chaos' own unit for UChaosVehicleMovementComponent::Mass)
 *   angle      DEGREES
 *   area       SQUARE CENTIMETRES (Chaos converts with Chaos::Cm2ToM2 internally)
 *
 * Unreal is left-handed and Z-up: +X forward, +Y right, +Z up. So a FRONT wheel takes
 * a POSITIVE X offset, a RIGHT-HAND wheel takes a POSITIVE Y offset, and a wheel
 * centre below the actor origin takes a NEGATIVE Z. This matches the steering sign
 * convention published by FVehicleInputCommand (positive steer == right == +Z yaw),
 * and getting either backwards produces a car that drives, badly, for a week before
 * anyone proves which half is inverted.
 *
 * ---------------------------------------------------------------------------
 * Provenance of the numbers
 * ---------------------------------------------------------------------------
 *
 * Every default below is an ORIGINAL PROTOTYPE ENVELOPE invented for this project.
 * No branded vehicle's specification was consulted, inferred or approximated, per
 * CLAUDE.md's non-negotiable decisions and Docs/02-VehiclePhysics.md ("Use envelopes
 * rather than fake precision until source data is authoritative"). They are chosen to
 * be self-consistent and stable, not to resemble anything.
 */

/**
 * Which axles the engine drives.
 *
 * A project enum rather than Chaos' EVehicleDifferential -- see the file header. The
 * enumerator NAMES become part of the /Script/RacingSim path once an asset references
 * them, so append; do not renumber (Core/RacingSimTypes.h's stability note).
 */
UENUM(BlueprintType)
enum class EVehicleDrivetrainLayout : uint8
{
	/** Engine drives the rear axle only. The Phase 1 default; see ARacingVehiclePawn for why. */
	RearWheelDrive	UMETA(DisplayName = "Rear-wheel drive"),
	/** Engine drives the front axle only. */
	FrontWheelDrive	UMETA(DisplayName = "Front-wheel drive"),
	/** Engine drives both axles, split by FrontRearTorqueSplit. */
	AllWheelDrive	UMETA(DisplayName = "All-wheel drive")
};

/** Which corner a wheel is. Index order is fixed and is what GetWheelOffsetCm and the pawn's WheelSetups both use. */
UENUM(BlueprintType)
enum class EVehicleWheelIndex : uint8
{
	FrontLeft	= 0	UMETA(DisplayName = "Front left"),
	FrontRight	= 1	UMETA(DisplayName = "Front right"),
	RearLeft	= 2	UMETA(DisplayName = "Rear left"),
	RearRight	= 3	UMETA(DisplayName = "Rear right")
};

/** Wheel count for Phase 1. Asserted by RacingSim.Vehicle.ChassisGeometry rather than assumed at every loop bound. */
inline constexpr int32 NumPrototypeVehicleWheels = 4;

/**
 * VEH-002: chassis, wheel placement, wheel size and drivetrain topology for one
 * prototype vehicle.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Vehicle Chassis"))
class RACINGSIM_API UVehicleChassisDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** CORE-003 range pass over this class's flat numeric properties. Every clamped scalar here is flat, so unlike VEH-001's input config there is no nested-struct exception. */
	static TConstArrayView<RacingSim::Validation::FRacingPropertyRange> StaticRanges();

	/**
	 * Ranges, then the geometric relationships no per-field clamp can express.
	 *
	 * @param bCorrect  true to write safe values back over out-of-range numerics.
	 *                  RELATIONSHIP failures are never corrected: there is no single
	 *                  safe value for "the front axle is behind the rear axle", and
	 *                  inventing one produces an asset that validates and cannot be
	 *                  driven -- the same policy VEH-001 applies to missing bindings.
	 */
	RacingSim::Validation::FRacingValidationResult Validate(bool bCorrect);

	/** Const report-only form. Equivalent to Validate(false) on a duplicate. */
	RacingSim::Validation::FRacingValidationResult ValidateReadOnly() const;

	/** Half-extents of the chassis collision box, CENTIMETRES. This is what UBoxComponent::SetBoxExtent takes. */
	FVector GetChassisHalfExtentCm() const
	{
		return FVector(ChassisHalfLengthCm, ChassisHalfWidthCm, ChassisHalfHeightCm);
	}

	/** Centre-of-mass offset from the actor origin, CENTIMETRES, in the body frame. */
	FVector GetCentreOfMassOffsetCm() const
	{
		return FVector(CentreOfMassOffsetXCm, CentreOfMassOffsetYCm, CentreOfMassOffsetZCm);
	}

	/**
	 * Wheelbase, CENTIMETRES. DERIVED from the two axle positions rather than authored
	 * alongside them -- an authored wheelbase that disagrees with its own axle offsets
	 * is a contradiction the asset would have to arbitrate, and every arbitration rule
	 * is a place for the two to silently diverge.
	 */
	float GetWheelbaseCm() const
	{
		return FrontAxleOffsetXCm - RearAxleOffsetXCm;
	}

	/**
	 * Wheel centre offset from the actor origin, CENTIMETRES, body frame.
	 *
	 * This is what becomes FChaosWheelSetup::AdditionalOffset. It is the per-wheel
	 * value because Chaos reads wheel RADIUS and WIDTH from the wheel class default
	 * object (see PrototypeVehicleWheel.h) but reads placement from the per-wheel
	 * setup, so placement is the only geometry this asset can legitimately drive.
	 *
	 * Returns the zero vector for an out-of-range index rather than asserting: this is
	 * reachable from Blueprint.
	 */
	UFUNCTION(BlueprintPure, Category = "Vehicle|Chassis")
	FVector GetWheelOffsetCm(EVehicleWheelIndex WheelIndex) const;

	/** True when the wheel is on the front axle. Index-order truth in one place. */
	static bool IsFrontWheel(const EVehicleWheelIndex WheelIndex)
	{
		return WheelIndex == EVehicleWheelIndex::FrontLeft || WheelIndex == EVehicleWheelIndex::FrontRight;
	}

	/** Radius of the wheel on the given axle, CENTIMETRES. */
	float GetWheelRadiusCm(const EVehicleWheelIndex WheelIndex) const
	{
		return IsFrontWheel(WheelIndex) ? FrontWheelRadiusCm : RearWheelRadiusCm;
	}

	/** Width of the wheel on the given axle, CENTIMETRES. */
	float GetWheelWidthCm(const EVehicleWheelIndex WheelIndex) const
	{
		return IsFrontWheel(WheelIndex) ? FrontWheelWidthCm : RearWheelWidthCm;
	}

	/** Kerb mass in SI kilograms, for telemetry and documentation. Storage unit is already kg; this exists so a reader never has to guess. */
	float GetMassKilograms() const
	{
		return MassKg;
	}

	// -- Mass ---------------------------------------------------------------

	/**
	 * Total vehicle mass, KILOGRAMS. Handed to UChaosVehicleMovementComponent::Mass,
	 * which overrides the collision primitive's own computed mass.
	 *
	 * Declares a replacement rather than clamping, because neither bound is safe: 200 kg
	 * on this footprint is a vehicle that flips on the first kerb and 5000 kg is one
	 * that cannot be stopped by its own brakes. A broken value returns to the envelope.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mass", meta = (ClampMin = "200.0", ClampMax = "5000.0"))
	float MassKg = 1250.0f;

	/** Centre of mass offset from the actor origin, CENTIMETRES, +X forward. Negative moves mass rearward. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mass", meta = (ClampMin = "-300.0", ClampMax = "300.0"))
	float CentreOfMassOffsetXCm = -10.0f;

	/** Centre of mass offset, CENTIMETRES, +Y right. Non-zero is a deliberate asymmetry, not a default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mass", meta = (ClampMin = "-150.0", ClampMax = "150.0"))
	float CentreOfMassOffsetYCm = 0.0f;

	/**
	 * Centre of mass offset, CENTIMETRES, +Z up. Negative is BELOW the origin.
	 *
	 * The single most stability-critical number in this asset. A centre of mass at or
	 * above the wheel centres makes a car that rolls over under lateral load, which
	 * presents as "the tyre model is wrong" and is not.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mass", meta = (ClampMin = "-200.0", ClampMax = "200.0"))
	float CentreOfMassOffsetZCm = -25.0f;

	// -- Chassis collision --------------------------------------------------

	/** Half-length of the chassis collision box along +X, CENTIMETRES. Full length is twice this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (ClampMin = "50.0", ClampMax = "600.0"))
	float ChassisHalfLengthCm = 230.0f;

	/** Half-width of the chassis collision box along +Y, CENTIMETRES. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (ClampMin = "30.0", ClampMax = "200.0"))
	float ChassisHalfWidthCm = 95.0f;

	/** Half-height of the chassis collision box along +Z, CENTIMETRES. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (ClampMin = "10.0", ClampMax = "150.0"))
	float ChassisHalfHeightCm = 55.0f;

	// -- Wheel placement ----------------------------------------------------

	/** Front axle centreline, CENTIMETRES along +X from the actor origin. Must exceed RearAxleOffsetXCm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels", meta = (ClampMin = "-400.0", ClampMax = "400.0"))
	float FrontAxleOffsetXCm = 140.0f;

	/** Rear axle centreline, CENTIMETRES along +X. Normally negative. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels", meta = (ClampMin = "-400.0", ClampMax = "400.0"))
	float RearAxleOffsetXCm = -145.0f;

	/** Front track width, CENTIMETRES, measured between wheel centres. Each front wheel sits at +/- half of this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels", meta = (ClampMin = "60.0", ClampMax = "350.0"))
	float FrontTrackWidthCm = 158.0f;

	/** Rear track width, CENTIMETRES. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels", meta = (ClampMin = "60.0", ClampMax = "350.0"))
	float RearTrackWidthCm = 156.0f;

	/** Height of every wheel centre relative to the actor origin, CENTIMETRES. Negative is below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels", meta = (ClampMin = "-250.0", ClampMax = "100.0"))
	float WheelCentreHeightCm = -35.0f;

	// -- Wheel size ---------------------------------------------------------
	//
	// Declared here as the CONTRACT, and validated against the wheel classes rather
	// than written to them. See PrototypeVehicleWheel.h for why writing is not an
	// option and ValidateChassisAgainstWheelClasses for the check.

	/** Front wheel rolling radius, CENTIMETRES. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels", meta = (ClampMin = "10.0", ClampMax = "80.0"))
	float FrontWheelRadiusCm = 34.0f;

	/** Rear wheel rolling radius, CENTIMETRES. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels", meta = (ClampMin = "10.0", ClampMax = "80.0"))
	float RearWheelRadiusCm = 35.0f;

	/** Front tyre section width, CENTIMETRES. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels", meta = (ClampMin = "5.0", ClampMax = "60.0"))
	float FrontWheelWidthCm = 24.0f;

	/** Rear tyre section width, CENTIMETRES. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels", meta = (ClampMin = "5.0", ClampMax = "60.0"))
	float RearWheelWidthCm = 28.0f;

	// -- Drivetrain topology ------------------------------------------------

	/** Which axles the engine drives. Ratios and torque are VEH-003; this is only the topology. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drivetrain")
	EVehicleDrivetrainLayout DrivetrainLayout = EVehicleDrivetrainLayout::RearWheelDrive;

	/**
	 * Front/rear torque split, dimensionless [0,1]. Below 0.5 sends more to the front.
	 *
	 * MEANINGLESS unless DrivetrainLayout is AllWheelDrive -- Chaos ignores it for the
	 * other two -- so Validate() reports a non-default value under RWD/FWD as an issue
	 * rather than letting an author believe they changed something.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drivetrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FrontRearTorqueSplit = 0.5f;

	/**
	 * Whether the gearbox shifts itself.
	 *
	 * Must agree with the input config's ETransmissionInputMode, or the driver has
	 * shift keys that do nothing (or a manual box with no way to change gear).
	 * ARacingVehiclePawn checks the two agree at possession and warns by name.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drivetrain")
	bool bUseAutomaticGears = true;

	// -- Steering geometry --------------------------------------------------

	/**
	 * Maximum road-wheel angle at the steered axle, DEGREES.
	 *
	 * Geometry, not tune: this is the mechanical lock the rack can reach. VEH-003 owns
	 * the steering RATIO and the speed-sensitivity curve that decide how much of it a
	 * given input actually uses.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "1.0", ClampMax = "70.0"))
	float MaxSteerAngleDegrees = 40.0f;

	// -- Aerodynamics -------------------------------------------------------
	//
	// Docs/02-VehiclePhysics.md item 10 asks for "aerodynamic drag and downforce with
	// a documented center of pressure". Chaos applies both at the centre of mass and
	// exposes no centre-of-pressure control, so the documented answer for Phase 1 is
	// "centre of pressure == centre of mass, because the model has no other option".
	// A real aero balance is Phase 2 work and is recorded as such rather than faked.

	/** Chassis drag coefficient, dimensionless. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aerodynamics", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float DragCoefficient = 0.35f;

	/** Chassis downforce coefficient, dimensionless. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aerodynamics", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float DownforceCoefficient = 0.4f;

	/**
	 * Reference frontal area, SQUARE CENTIMETRES.
	 *
	 * Chaos takes cm^2 and converts internally with Chaos::Cm2ToM2
	 * (ChaosVehicleMovementComponent.h, AerodynamicsSetup). Authored in cm^2 rather
	 * than m^2 so the project's "everything stored is Unreal units" rule holds without
	 * a conversion at the boundary; RacingSim.Vehicle.ChassisUnits pins the m^2 value.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aerodynamics", meta = (ClampMin = "0.0", ClampMax = "200000.0"))
	float FrontalAreaCm2 = 20000.0f;
};
