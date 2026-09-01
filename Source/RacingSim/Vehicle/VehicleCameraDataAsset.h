// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/RacingSimValidation.h"
#include "Engine/DataAsset.h"
#include "Vehicle/VehicleCameraMath.h"
#include "VehicleCameraDataAsset.generated.h"

/**
 * VEH-005: the authored, validated camera tunables.
 *
 * Same split as UVehicleFailureThresholdsDataAsset (VEH-004): CORE-003's EnforceRanges
 * resolves each declared range by name to a FLAT numeric UPROPERTY, so this class keeps
 * the fields flat and GetSettings() assembles the FVehicleCameraSettings POD that
 * ARacingVehiclePawn and RacingSim::Vehicle::ComputeSpeedAdjustedFieldOfViewDegrees
 * actually consume.
 *
 * No .uasset is authored by this ticket, per CLAUDE.md's binary-asset rule. A pawn with
 * a null CameraAsset uses FVehicleCameraSettings' own defaults and reports it once, the
 * same fallback VEH-004 established for a null failure-thresholds asset.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Vehicle Camera Settings"))
class RACINGSIM_API UVehicleCameraDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** CORE-003 range pass over this class's flat numeric properties. */
	static TConstArrayView<RacingSim::Validation::FRacingPropertyRange> StaticRanges();

	/**
	 * Ranges, then the one relationship no per-field clamp can express.
	 *
	 * @param bCorrect  true to write safe values back over out-of-range numerics. The
	 *                  FOV-ceiling relationship below is never corrected -- there is no
	 *                  single safe split between base and boost, same policy as VEH-004's
	 *                  threshold relationships.
	 */
	RacingSim::Validation::FRacingValidationResult Validate(bool bCorrect);

	/** Const report-only form. Never mutates this asset. */
	RacingSim::Validation::FRacingValidationResult ValidateReadOnly() const;

	/** Assemble the POD the pawn and the pure camera math consume. Call Validate() first. */
	FVehicleCameraSettings GetSettings() const;

	// -- Rig geometry --------------------------------------------------------

	/** Spring arm length, CENTIMETRES. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rig geometry", meta = (ClampMin = "50.0", ClampMax = "5000.0"))
	float ArmLengthCm = 600.0f;

	/** Socket height above the chassis root, CENTIMETRES. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rig geometry", meta = (ClampMin = "-500.0", ClampMax = "2000.0"))
	float SocketHeightCm = 250.0f;

	/** Socket forward offset from the chassis root, CENTIMETRES. Negative sits behind. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rig geometry", meta = (ClampMin = "-2000.0", ClampMax = "2000.0"))
	float SocketForwardOffsetCm = 0.0f;

	/** Arm pitch, DEGREES. Negative looks down at the car. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rig geometry", meta = (ClampMin = "-90.0", ClampMax = "90.0"))
	float CameraPitchDegrees = -10.0f;

	// -- Lag -------------------------------------------------------------------

	/** USpringArmComponent::CameraLagSpeed. 0 disables position lag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lag", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float CameraLagSpeed = 10.0f;

	/** USpringArmComponent::CameraRotationLagSpeed. 0 disables rotation lag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lag", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float CameraRotationLagSpeed = 10.0f;

	// -- Field of view -----------------------------------------------------

	/**
	 * FOV at zero speed, DEGREES.
	 *
	 * [5, 170] is a DESIGN ceiling, not an engine one -- corrected on code review (VEH-005
	 * MEDIUM-A): UCameraComponent::FieldOfView's own metadata (Camera/CameraComponent.h)
	 * is UIMin=5/UIMax=170, an editor slider hint with no runtime effect, not an
	 * enforcing ClampMin/ClampMax (those are 0.001/360.0). Authoring outside [5, 170] is
	 * NOT silently truncated by the engine at apply time; the actual runtime safety net
	 * is RacingSim::Vehicle::ClampFieldOfViewForApplyDegrees, called from
	 * ARacingVehiclePawn::Tick. This property's own ClampMin/ClampMax below still bound
	 * it in the editor and in EnforceRanges (CORE-003), independent of that runtime clamp.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Field of view", meta = (ClampMin = "5.0", ClampMax = "170.0"))
	float BaseFieldOfViewDegrees = 90.0f;

	/** Additional FOV added at or above SpeedForMaxFovBoostCms, DEGREES. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Field of view", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float MaxFieldOfViewBoostDegrees = 10.0f;

	/**
	 * Forward speed at which the FOV boost reaches its maximum, CENTIMETRES PER SECOND.
	 *
	 * ClampMin is 1.0, not 0: RacingSim::Vehicle::ComputeSpeedAdjustedFieldOfViewDegrees
	 * divides by this value, and a validated-but-zero speed threshold is exactly the
	 * "no single safe value" case that function's own header comment documents.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Field of view", meta = (ClampMin = "1.0", ClampMax = "100000.0"))
	float SpeedForMaxFovBoostCms = 3000.0f;
};
