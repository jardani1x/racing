// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/VehicleCameraDataAsset.h"

using namespace RacingSim::Validation;

namespace
{
	/**
	 * Report a relationship failure that has no numeric correction.
	 *
	 * VEH-005-SPECIFIC NAME, deliberately, per the Unity Build collision discipline
	 * VehicleFailureThresholdsDataAsset.cpp documents (three prior C2084 incidents).
	 * Existing names in this module: AddFailure, AddChassisValidationFailure,
	 * AddTuneValidationFailure, AddFailureThresholdValidationFailure; this is the fifth
	 * and does not collide with any.
	 */
	void AddCameraValidationFailure(FRacingValidationResult& Result, const FName PropertyName, FString Message)
	{
		FRacingValidationIssue Issue;
		Issue.PropertyName = PropertyName;
		Issue.Action = ERangeAction::Failed;
		Issue.Message = MoveTemp(Message);
		Result.Issues.Add(MoveTemp(Issue));
	}
}

TConstArrayView<FRacingPropertyRange> UVehicleCameraDataAsset::StaticRanges()
{
	// Mirrors the ClampMin/ClampMax metadata on the header's properties, in declaration
	// order -- this table is the enforcement path in a packaged Game build, where
	// WITH_METADATA is 0 (CORE-003, Core/RacingSimValidation.h).
	static const FRacingPropertyRange Ranges[] =
	{
		FRacingPropertyRange::Between(TEXT("ArmLengthCm"), 50.0, 5000.0),
		FRacingPropertyRange::Between(TEXT("SocketHeightCm"), -500.0, 2000.0),
		FRacingPropertyRange::Between(TEXT("SocketForwardOffsetCm"), -2000.0, 2000.0),
		FRacingPropertyRange::Between(TEXT("CameraPitchDegrees"), -90.0, 90.0),

		FRacingPropertyRange::Between(TEXT("CameraLagSpeed"), 0.0, 100.0),
		FRacingPropertyRange::Between(TEXT("CameraRotationLagSpeed"), 0.0, 100.0),

		FRacingPropertyRange::Between(TEXT("BaseFieldOfViewDegrees"), 5.0, 170.0),
		FRacingPropertyRange::Between(TEXT("MaxFieldOfViewBoostDegrees"), 0.0, 90.0),
		FRacingPropertyRange::Between(TEXT("SpeedForMaxFovBoostCms"), 1.0, 100000.0)
	};

	return Ranges;
}

FRacingValidationResult UVehicleCameraDataAsset::Validate(const bool bCorrect)
{
	FRacingValidationResult Result;

	if (bCorrect)
	{
		Result = EnforceRanges(this, StaticRanges());
	}
	else
	{
		// Report-only: run the pass on a duplicate so this object is never written.
		// Same idiom as UVehicleFailureThresholdsDataAsset::Validate.
		UVehicleCameraDataAsset* Scratch = DuplicateObject<UVehicleCameraDataAsset>(this, GetTransientPackage());
		if (Scratch != nullptr)
		{
			Result = EnforceRanges(Scratch, StaticRanges());
		}
	}

	// -- Relationship no per-field clamp can express.
	//
	// Evaluated on THIS object's values (corrected ones under bCorrect, since
	// EnforceRanges has already run), same ordering rule as VEH-004's threshold checks.
	const float EffectiveBase = BaseFieldOfViewDegrees;
	const float EffectiveBoost = MaxFieldOfViewBoostDegrees;

	// Corrected on code review (VEH-005 MEDIUM-A): [5, 170] is this project's design
	// ceiling, not an engine-enforced one -- UCameraComponent::FieldOfView's [5, 170] is
	// UIMin/UIMax (editor slider hint only). A base+boost sum above 170 is NOT silently
	// truncated by the engine; RacingSim::Vehicle::ClampFieldOfViewForApplyDegrees
	// (called from ARacingVehiclePawn::Tick) is what actually keeps the applied FOV
	// in range at runtime, regardless of this check. This check stays here as an
	// authoring-time warning -- catching the mistake at tuning time, not just masking
	// it silently at apply time -- so it belongs here, not in
	// RacingSim::Vehicle::ComputeSpeedAdjustedFieldOfViewDegrees (which is the pure
	// math and must stay agnostic to any one caller's ceiling).
	if (FMath::IsFinite(EffectiveBase)
		&& FMath::IsFinite(EffectiveBoost)
		&& EffectiveBase + EffectiveBoost > 170.0f)
	{
		AddCameraValidationFailure(Result, TEXT("MaxFieldOfViewBoostDegrees"), FString::Printf(
			TEXT("BaseFieldOfViewDegrees (%f) + MaxFieldOfViewBoostDegrees (%f) exceeds this project's 170-degree design ceiling; the applied FOV will be clamped at speed (RacingSim::Vehicle::ClampFieldOfViewForApplyDegrees), not the boost the tuning author intended"),
			EffectiveBase, EffectiveBoost));
	}

	return Result;
}

FRacingValidationResult UVehicleCameraDataAsset::ValidateReadOnly() const
{
	// const_cast is safe here for the structural reason UVehicleFailureThresholdsDataAsset::
	// ValidateReadOnly documents: Validate(false) runs EnforceRanges on a DUPLICATE and the
	// relationship check above it is a pure read.
	return const_cast<UVehicleCameraDataAsset*>(this)->Validate(false);
}

FVehicleCameraSettings UVehicleCameraDataAsset::GetSettings() const
{
	FVehicleCameraSettings Settings;

	Settings.ArmLengthCm = ArmLengthCm;
	Settings.SocketHeightCm = SocketHeightCm;
	Settings.SocketForwardOffsetCm = SocketForwardOffsetCm;
	Settings.CameraPitchDegrees = CameraPitchDegrees;
	Settings.CameraLagSpeed = CameraLagSpeed;
	Settings.CameraRotationLagSpeed = CameraRotationLagSpeed;
	Settings.BaseFieldOfViewDegrees = BaseFieldOfViewDegrees;
	Settings.MaxFieldOfViewBoostDegrees = MaxFieldOfViewBoostDegrees;
	Settings.SpeedForMaxFovBoostCms = SpeedForMaxFovBoostCms;

	return Settings;
}
