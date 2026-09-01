// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VehicleCameraMath.generated.h"

/**
 * VEH-005: the camera tunables a driving pawn needs, as plain data.
 *
 * Assembled by UVehicleCameraDataAsset::GetSettings() and consumed by
 * ARacingVehiclePawn -- the same split VEH-004 used for FVehicleFailureThresholds,
 * for the same reason: CORE-003's EnforceRanges resolves flat numeric UPROPERTYs by
 * name, so the validated asset keeps them flat and this struct is what the pawn and
 * the pure math below actually pass around.
 */
USTRUCT()
struct RACINGSIM_API FVehicleCameraSettings
{
	GENERATED_BODY()

	/** Spring arm length, CENTIMETRES. */
	float ArmLengthCm = 600.0f;

	/** Socket height above the chassis root, CENTIMETRES. */
	float SocketHeightCm = 250.0f;

	/** Socket forward offset from the chassis root, CENTIMETRES. Negative sits behind. */
	float SocketForwardOffsetCm = 0.0f;

	/** Arm pitch, DEGREES. Negative looks down at the car. */
	float CameraPitchDegrees = -10.0f;

	/** USpringArmComponent::CameraLagSpeed. 0 disables position lag. */
	float CameraLagSpeed = 10.0f;

	/** USpringArmComponent::CameraRotationLagSpeed. 0 disables rotation lag. */
	float CameraRotationLagSpeed = 10.0f;

	/** UCameraComponent::FieldOfView at zero speed, DEGREES. */
	float BaseFieldOfViewDegrees = 90.0f;

	/** Additional FOV added at or above SpeedForMaxFovBoostCms, DEGREES. */
	float MaxFieldOfViewBoostDegrees = 10.0f;

	/** Forward speed at which the FOV boost reaches its maximum, CENTIMETRES PER SECOND. */
	float SpeedForMaxFovBoostCms = 3000.0f;
};

namespace RacingSim::Vehicle
{
	/**
	 * A simple speed-vs-FOV widening curve, factored out as a pure function per
	 * VEH-005's acceptance criteria: camera math worth testing must be reachable
	 * without an actor, mirroring MapCommandToChaosInput's precedent.
	 *
	 * STABILITY, NOT REALISM: this is the one piece of camera "feel" VEH-005 owns.
	 * SpeedForMaxBoostCms is a divisor -- guarded because a validated-but-zero (or a
	 * caller-supplied non-finite) value would otherwise turn every Tick's FOV into
	 * NaN, and a NaN field of view is exactly the class of defect CLAUDE.md forbids
	 * reaching any layer of this project. A non-finite or negative SpeedCms (e.g. a
	 * zero-length velocity read before the first physics step) is clamped to 0
	 * rather than propagated.
	 *
	 * @return BaseFieldOfViewDegrees at rest, rising to BaseFieldOfViewDegrees +
	 *         MaxFieldOfViewBoostDegrees at or above SpeedForMaxBoostCms. Note the
	 *         clamp-to-0 direction: an INFINITE SpeedCms is non-finite, so it also
	 *         returns the unboosted BASE FOV, not the maximum boosted one -- "clamped to
	 *         0" means the input is treated as stationary, not as unboundedly fast.
	 *         code-reviewer VEH-005 LOW-1.
	 */
	RACINGSIM_API float ComputeSpeedAdjustedFieldOfViewDegrees(
		float BaseFieldOfViewDegrees,
		float MaxFieldOfViewBoostDegrees,
		float SpeedCms,
		float SpeedForMaxBoostCms);

	/**
	 * code-reviewer VEH-005 MEDIUM-A: the actual runtime safety net for the FOV this
	 * module applies to UCameraComponent::FieldOfView.
	 *
	 * UCameraComponent::FieldOfView's own metadata (Camera/CameraComponent.h) declares
	 * ClampMin=0.001/ClampMax=360.0 -- editor-ENFORCING bounds -- but [5, 170] (the range
	 * UVehicleCameraDataAsset::BaseFieldOfViewDegrees authors against) is only UIMin/UIMax,
	 * a slider hint with no runtime effect. Nothing upstream of ARacingVehiclePawn::Tick's
	 * assignment clamps the computed base+boost sum, so a misauthored asset (an
	 * out-of-relationship base/boost pair that UVehicleCameraDataAsset::Validate can only
	 * WARN about, never correct -- there is no single safe split) can reach the camera at
	 * well over 180 degrees, which is a negative-tangent, degenerate projection matrix, not
	 * a merely-too-wide one.
	 *
	 * @return FovDegrees clamped to [5, 170] -- the same design ceiling authoring already
	 *         targets -- with a non-finite input treated as the module's own 90-degree
	 *         default rather than propagated.
	 */
	RACINGSIM_API float ClampFieldOfViewForApplyDegrees(float FovDegrees);
}
