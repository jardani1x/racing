// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/VehicleFailureThresholdsDataAsset.h"

using namespace RacingSim::Validation;

namespace
{
	/**
	 * Report a relationship failure that has no numeric correction.
	 *
	 * VEH-004-SPECIFIC NAME, deliberately. See VehicleFailureDetection.cpp's
	 * RaiseVehicleFailure for the full reasoning: an anonymous-namespace helper whose
	 * signature matches another file's in the same module is a Unity Build
	 * redefinition (C2084), and this project has shipped that bug three times
	 * (AddFailure, AllFinite, HasIssueFor). The existing names in this module are
	 * AddFailure (VehicleInputConfig.cpp), AddChassisValidationFailure
	 * (VehicleChassisDataAsset.cpp) and AddTuneValidationFailure
	 * (VehicleTuneDataAsset.cpp); this is the fourth and it does not collide with any.
	 */
	void AddFailureThresholdValidationFailure(FRacingValidationResult& Result, const FName PropertyName, FString Message)
	{
		FRacingValidationIssue Issue;
		Issue.PropertyName = PropertyName;
		Issue.Action = ERangeAction::Failed;
		Issue.Message = MoveTemp(Message);
		Result.Issues.Add(MoveTemp(Issue));
	}
}

TConstArrayView<FRacingPropertyRange> UVehicleFailureThresholdsDataAsset::StaticRanges()
{
	// Mirrors the ClampMin/ClampMax metadata on the header's properties, in declaration
	// order. This table -- not the metadata -- is the enforcement path in every build
	// configuration, because WITH_METADATA is WITH_EDITORONLY_DATA and is 0 in a
	// packaged Game build (CORE-003, Core/RacingSimValidation.h).
	static const FRacingPropertyRange Ranges[] =
	{
		FRacingPropertyRange::Between(TEXT("MaxSpeedCms"), 100.0, 1000000.0),
		FRacingPropertyRange::Between(TEXT("MaxAngularSpeedDegreesPerSecond"), 90.0, 100000.0),
		FRacingPropertyRange::Between(TEXT("MaxEngineRpm"), 1000.0, 1000000.0),
		FRacingPropertyRange::Between(TEXT("MaxAccelerationCmsPerSecondSquared"), 100.0, 1000000.0),

		// TunnelVelocityFactor's minimum IS its safe end (a wider allowance detects
		// less, never more), so it clamps normally.
		FRacingPropertyRange::Between(TEXT("TunnelVelocityFactor"), 1.0, 100.0),
		FRacingPropertyRange::Between(TEXT("TunnelToleranceCm"), 0.0, 100000.0),

		// MaxAnalysisStepSeconds' minimum is NOT its safe end: a 0.02 s analysis window
		// declines to judge almost every step, which disarms tunnelling and
		// acceleration detection silently. Correct a broken value back to the authored
		// default instead of to the bound it violated. See the header, and
		// Core/RacingSimValidation.h's explanation of why "clamp to the nearest bound"
		// is not always safe.
		FRacingPropertyRange::Between(TEXT("MaxAnalysisStepSeconds"), 0.02, 5.0).WithReplacement(0.5),

		FRacingPropertyRange::Between(TEXT("SuspensionLengthTolerance"), 0.0, 0.5),
		FRacingPropertyRange::Between(TEXT("MaxAirborneSeconds"), 0.1, 600.0),
		FRacingPropertyRange::Between(TEXT("MaxContactDistanceCm"), 10.0, 100000.0),

		FRacingPropertyRange::Between(TEXT("PenetrationSuspensionLengthFraction"), 0.0, 0.5),
		FRacingPropertyRange::Between(TEXT("PenetrationSpringForceN"), 0.0, 1000000.0),
		FRacingPropertyRange::Between(TEXT("PenetrationPersistSeconds"), 0.05, 600.0)
	};

	return Ranges;
}

FRacingValidationResult UVehicleFailureThresholdsDataAsset::Validate(const bool bCorrect)
{
	FRacingValidationResult Result;

	if (bCorrect)
	{
		Result = EnforceRanges(this, StaticRanges());
	}
	else
	{
		// Report-only: run the pass on a duplicate so this object is never written.
		// Same idiom as UVehicleTuneDataAsset::ValidateReadOnly and the chassis asset's.
		UVehicleFailureThresholdsDataAsset* Scratch = DuplicateObject<UVehicleFailureThresholdsDataAsset>(this, GetTransientPackage());
		if (Scratch != nullptr)
		{
			Result = EnforceRanges(Scratch, StaticRanges());
		}
	}

	// -- Relationships no per-field clamp can express.
	//
	// Evaluated on THIS object's values (corrected ones under bCorrect, since
	// EnforceRanges has already run), so a NaN does not report both a non-finite issue
	// and a spurious relationship failure. Same ordering rule as VEH-001's profile
	// validation and VEH-003's tune validation.
	const float EffectivePenetrationFraction = PenetrationSuspensionLengthFraction;
	const float EffectiveSuspensionTolerance = SuspensionLengthTolerance;

	// A penetration threshold at or above the "outside [0,1]" tolerance means the two
	// detectors overlap: a suspension length that counts as fully compressed would also
	// be inside the band UnstableWheelState treats as a diverged solver. Both would
	// fire on the same value and neither report would mean what it says.
	if (FMath::IsFinite(EffectivePenetrationFraction)
		&& FMath::IsFinite(EffectiveSuspensionTolerance)
		&& EffectivePenetrationFraction >= EffectiveSuspensionTolerance)
	{
		AddFailureThresholdValidationFailure(Result, TEXT("PenetrationSuspensionLengthFraction"), FString::Printf(
			TEXT("PenetrationSuspensionLengthFraction (%f) must be below SuspensionLengthTolerance (%f); otherwise a fully-compressed wheel also reads as an out-of-range suspension length and both detectors fire on the same value"),
			EffectivePenetrationFraction, EffectiveSuspensionTolerance));
	}

	// The penetration timer only advances on steps the detector actually accumulates,
	// and a penetration threshold shorter than a single analysable step can be crossed
	// by one step -- which is a kerb strike, not a penetration. Requiring at least two
	// analysable steps is what makes "persistent" mean something.
	if (FMath::IsFinite(PenetrationPersistSeconds)
		&& FMath::IsFinite(MaxAnalysisStepSeconds)
		&& PenetrationPersistSeconds <= MaxAnalysisStepSeconds)
	{
		AddFailureThresholdValidationFailure(Result, TEXT("PenetrationPersistSeconds"), FString::Printf(
			TEXT("PenetrationPersistSeconds (%f) must exceed MaxAnalysisStepSeconds (%f); otherwise a single long step reports persistent penetration and 'persistent' means nothing"),
			PenetrationPersistSeconds, MaxAnalysisStepSeconds));
	}

	// Airborne time is accumulated from the same steps. A threshold below one step
	// would report every airborne frame, i.e. every jump.
	if (FMath::IsFinite(MaxAirborneSeconds)
		&& FMath::IsFinite(MaxAnalysisStepSeconds)
		&& MaxAirborneSeconds <= MaxAnalysisStepSeconds)
	{
		AddFailureThresholdValidationFailure(Result, TEXT("MaxAirborneSeconds"), FString::Printf(
			TEXT("MaxAirborneSeconds (%f) must exceed MaxAnalysisStepSeconds (%f); otherwise a single airborne step reports unstable wheel state"),
			MaxAirborneSeconds, MaxAnalysisStepSeconds));
	}

	return Result;
}

FRacingValidationResult UVehicleFailureThresholdsDataAsset::ValidateReadOnly() const
{
	// const_cast is safe here and the reason is structural, not a shortcut: Validate(false)
	// runs EnforceRanges on a DUPLICATE and every relationship check below it is a pure
	// read. Same idiom, same justification as UVehicleTuneDataAsset::ValidateReadOnly.
	return const_cast<UVehicleFailureThresholdsDataAsset*>(this)->Validate(false);
}

FVehicleFailureThresholds UVehicleFailureThresholdsDataAsset::GetThresholds() const
{
	FVehicleFailureThresholds Thresholds;

	Thresholds.MaxSpeedCms = MaxSpeedCms;
	Thresholds.MaxAngularSpeedDegreesPerSecond = MaxAngularSpeedDegreesPerSecond;
	Thresholds.MaxEngineRpm = MaxEngineRpm;
	Thresholds.MaxAccelerationCmsPerSecondSquared = MaxAccelerationCmsPerSecondSquared;
	Thresholds.TunnelVelocityFactor = TunnelVelocityFactor;
	Thresholds.TunnelToleranceCm = TunnelToleranceCm;
	Thresholds.MaxAnalysisStepSeconds = MaxAnalysisStepSeconds;
	Thresholds.SuspensionLengthTolerance = SuspensionLengthTolerance;
	Thresholds.MaxAirborneSeconds = MaxAirborneSeconds;
	Thresholds.MaxContactDistanceCm = MaxContactDistanceCm;
	Thresholds.PenetrationSuspensionLengthFraction = PenetrationSuspensionLengthFraction;
	Thresholds.PenetrationSpringForceN = PenetrationSpringForceN;
	Thresholds.PenetrationPersistSeconds = PenetrationPersistSeconds;

	return Thresholds;
}
