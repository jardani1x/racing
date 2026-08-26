// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "ChaosVehicleWheel.h"
#include "Core/RacingSimValidation.h"
#include "CoreMinimal.h"
#include "PrototypeVehicleWheel.generated.h"

class UVehicleChassisDataAsset;
class UVehicleTuneDataAsset;

/**
 * VEH-002: the prototype's wheels.
 *
 * ---------------------------------------------------------------------------
 * Why wheel geometry is a CLASS and not a field on the chassis DataAsset
 * ---------------------------------------------------------------------------
 *
 * This is the least obvious decision in VEH-002 and the obvious alternative is
 * actively wrong, so it is recorded in full.
 *
 * UChaosWheeledVehicleMovementComponent::SetupVehicle builds the entire physics wheel
 * and suspension configuration from
 *
 *     UChaosVehicleWheel* Wheel = WheelSetups[WheelIdx].WheelClass.GetDefaultObject();
 *
 * (ChaosWheeledVehicleMovementComponent.cpp) -- the CLASS DEFAULT OBJECT. The
 * per-instance wheels that CreateWheels() allocates with NewObject are created in the
 * same call and are used for gameplay queries and animation, but the values that reach
 * Chaos::FSimpleWheelSim and Chaos::FSimpleSuspensionSim come from the CDO.
 *
 * So "read wheel radius from the DataAsset and write it onto the wheel" has exactly
 * two implementations, and both are defects:
 *
 *   - write to Wheels[i] (the instance): the write lands after SetupVehicle has
 *     already copied the CDO, so the physics silently keeps the old radius while every
 *     inspector, log line and Blueprint getter reports the new one. The car's contact
 *     patch and its stated geometry disagree, permanently, with nothing to point at;
 *   - write to the CDO: the CDO is process-global. Two pawns with different chassis
 *     assets would fight over one radius, the last one spawned would win, and the
 *     mutation would persist into the editor session and be saved into unrelated
 *     assets. This is the worse of the two.
 *
 * Therefore: RADIUS, WIDTH, MASS, STEER AUTHORITY, BRAKE/HANDBRAKE/ENGINE FLAGS and
 * SUSPENSION live on these classes, authored in the constructor. PLACEMENT lives on
 * the chassis DataAsset, because FChaosWheelSetup::AdditionalOffset genuinely is
 * per-wheel and per-instance.
 *
 * The DataAsset still DECLARES the wheel size, as the contract it expects, and
 * ValidateChassisAgainstWheelClasses() proves the two agree. A disagreement is a
 * validation failure, never a silent write. That way the asset stays the single place
 * an author reads the geometry from, without the asset being able to lie about it.
 *
 * ---------------------------------------------------------------------------
 * The VEH-002 placeholders are GONE. VEH-003 owns these values now.
 * ---------------------------------------------------------------------------
 *
 * This section previously read "spring rate, preload, damping ratio, brake torque and
 * handbrake torque are VEH-003's ... marked in the constructor and must be replaced,
 * not extended, by VEH-003". VEH-003 replaced them.
 *
 * They are now read from RacingSim::Vehicle::PrototypeTuneDefaults
 * (Vehicle/VehicleTuneDataAsset.h), the same constexpr block UVehicleTuneDataAsset's own
 * property defaults read. That is a deliberate improvement on the radius/width pattern
 * below, which duplicates its literal in two files and relies on a cross-check to notice
 * drift: here the two CANNOT start out disagreeing, because there is one definition.
 *
 * The cross-check still exists and still matters --
 * ValidateTuneAgainstWheelClasses() -- because a tune ASSET INSTANCE can be edited after
 * construction and the wheel class cannot follow it. Validate, never write.
 *
 * ---------------------------------------------------------------------------
 * Units
 * ---------------------------------------------------------------------------
 *
 *   WheelRadius / WheelWidth / SuspensionMaxRaise / SuspensionMaxDrop  CENTIMETRES
 *   WheelMass                                                          KILOGRAMS
 *   MaxSteerAngle                                                      DEGREES
 *   MaxBrakeTorque / MaxHandBrakeTorque                                NEWTON-METRES
 *   SpringRate / SpringPreload                                         NEWTONS PER METRE
 *   SuspensionDampingRatio / WheelLoadRatio / RollbarScaling           DIMENSIONLESS
 *
 * Note that SpringRate is N/m while every distance in this project is centimetres.
 * That is Chaos' unit, not ours, and it is one of the few places the two disagree.
 */

/**
 * Shared prototype wheel behaviour. Never used directly -- the pawn references the
 * front and rear subclasses, which is what makes the axle a type rather than a flag
 * someone can forget to set.
 */
UCLASS(Abstract)
class RACINGSIM_API UPrototypeVehicleWheel : public UChaosVehicleWheel
{
	GENERATED_BODY()

public:
	UPrototypeVehicleWheel(const FObjectInitializer& ObjectInitializer);
};

/** Front axle: steered, braked, not handbraked. */
UCLASS()
class RACINGSIM_API UPrototypeFrontWheel : public UPrototypeVehicleWheel
{
	GENERATED_BODY()

public:
	UPrototypeFrontWheel(const FObjectInitializer& ObjectInitializer);
};

/** Rear axle: driven under the Phase 1 RWD default, braked and handbraked, not steered. */
UCLASS()
class RACINGSIM_API UPrototypeRearWheel : public UPrototypeVehicleWheel
{
	GENERATED_BODY()

public:
	UPrototypeRearWheel(const FObjectInitializer& ObjectInitializer);
};

namespace RacingSim::Vehicle
{
	/**
	 * Prove the chassis asset's declared wheel geometry matches the wheel classes that
	 * will actually reach Chaos.
	 *
	 * This exists because the DataAsset cannot own the geometry (see the file header)
	 * but must still be readable as the truth about the car. Without this check the
	 * asset would be documentation that nothing enforces -- which is the same failure
	 * CORE-003 exists to prevent one layer up, where UPROPERTY metadata looked like an
	 * invariant and was not.
	 *
	 * Declared here rather than on UVehicleChassisDataAsset deliberately: the asset
	 * carries no ChaosVehicles include, so a Phase 2 replacement of Chaos changes this
	 * function and nothing else. See VehicleChassisDataAsset.h.
	 *
	 * @param Chassis      asset to check. Null is reported as a failure, never a crash.
	 * @param FrontWheel   front wheel class. Null is reported as a failure.
	 * @param RearWheel    rear wheel class. Null is reported as a failure.
	 * @return             an empty result when the asset and the classes agree.
	 */
	RACINGSIM_API RacingSim::Validation::FRacingValidationResult ValidateChassisAgainstWheelClasses(
		const UVehicleChassisDataAsset* Chassis,
		TSubclassOf<UChaosVehicleWheel> FrontWheel,
		TSubclassOf<UChaosVehicleWheel> RearWheel);

	/**
	 * VEH-003: prove the tune asset's declared suspension and brake values match the
	 * wheel classes that will actually reach Chaos.
	 *
	 * The tune asset and the wheel classes now share ONE constexpr definition of these
	 * values (RacingSim::Vehicle::PrototypeTuneDefaults), so a freshly constructed pair
	 * always agrees. This exists for the case that definition cannot cover: an asset
	 * instance edited by a designer, which the wheel class has no way to follow. A
	 * disagreement is a validation failure naming the field, never a silent CDO write --
	 * see the file header for why writing is not an option.
	 *
	 * Declared here rather than on UVehicleTuneDataAsset for the same reason its chassis
	 * sibling is: the asset carries no ChaosVehicles include, so a Phase 2 replacement of
	 * Chaos changes this function and nothing else.
	 *
	 * @param Tune         asset to check. Null is reported as a failure, never a crash.
	 * @param FrontWheel   front wheel class. Null is reported as a failure.
	 * @param RearWheel    rear wheel class. Null is reported as a failure.
	 * @return             an empty result when the asset and the classes agree.
	 */
	RACINGSIM_API RacingSim::Validation::FRacingValidationResult ValidateTuneAgainstWheelClasses(
		const UVehicleTuneDataAsset* Tune,
		TSubclassOf<UChaosVehicleWheel> FrontWheel,
		TSubclassOf<UChaosVehicleWheel> RearWheel);
}
