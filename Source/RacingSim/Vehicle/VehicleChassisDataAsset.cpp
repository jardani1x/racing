// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/VehicleChassisDataAsset.h"

#include "Core/RacingSimUnits.h"

// For the AllFinite guard's parameter type. Included explicitly rather than relied on
// transitively -- a std:: type used without its own header is a build that works until
// an unrelated engine header stops including it.
#include <initializer_list>

using namespace RacingSim::Validation;

namespace
{
	/** Same shape as VehicleInputConfig.cpp's helper: a relationship failure, named by the property an author would edit to fix it. */
	void AddFailure(FRacingValidationResult& Result, const FName PropertyName, FString Message)
	{
		FRacingValidationIssue Issue;
		Issue.PropertyName = PropertyName;
		Issue.Action = ERangeAction::Failed;
		Issue.Message = MoveTemp(Message);
		Result.Issues.Add(MoveTemp(Issue));
	}

	/**
	 * True when every value a relationship check is about to compare is finite.
	 *
	 * Relationship checks run AFTER the range pass, so in the bCorrect path a NaN has
	 * already been replaced. In the report-only path it has not, and `NaN > 0` is
	 * false for every comparison -- which would make a NaN wheelbase report as
	 * "the front axle is behind the rear axle" instead of as a non-finite value the
	 * range pass already named. Guarding keeps one defect producing one issue.
	 */
	bool AllFinite(const std::initializer_list<float> Values)
	{
		for (const float Value : Values)
		{
			if (!FMath::IsFinite(Value))
			{
				return false;
			}
		}
		return true;
	}
}

TConstArrayView<FRacingPropertyRange> UVehicleChassisDataAsset::StaticRanges()
{
	// Mirrors the ClampMin/ClampMax metadata on this class member-for-member. The
	// duplication is deliberate and is CORE-003's whole design: metadata is compiled
	// out when WITH_METADATA is 0, i.e. in the packaged Game build, so the metadata
	// cannot be the enforcement path. See Core/RacingSimValidation.h.
	//
	// Replacements are declared only where a BOUND IS NOT A SAFE VALUE:
	//
	//   MassKg           -- 200 kg on this footprint flips on the first kerb and
	//                       5000 kg cannot be stopped by its own brakes. Neither end
	//                       is safe, so a broken mass returns to the envelope.
	//   ChassisHalf*Cm   -- clamping to the minimum would shrink the collision body
	//                       around the wheels, which is a car whose wheels are outside
	//                       its own chassis. Return to the envelope instead.
	//   *TrackWidthCm    -- same reasoning, in the other axis.
	//   MaxSteerAngle    -- the minimum, 1 degree, is a car that cannot turn; the
	//                       maximum, 70, is one that spins at the first input.
	//   FrontRearTorqueSplit -- 0.0 and 1.0 are both legal AWD tunes, so the bounds
	//                       ARE safe here and no replacement is declared. This is the
	//                       counter-example that keeps the rule from looking automatic.
	static const FRacingPropertyRange Ranges[] =
	{
		FRacingPropertyRange::Between(TEXT("MassKg"), 200.0, 5000.0).WithReplacement(1250.0),

		FRacingPropertyRange::Between(TEXT("CentreOfMassOffsetXCm"), -300.0, 300.0).WithReplacement(0.0),
		FRacingPropertyRange::Between(TEXT("CentreOfMassOffsetYCm"), -150.0, 150.0).WithReplacement(0.0),
		FRacingPropertyRange::Between(TEXT("CentreOfMassOffsetZCm"), -200.0, 200.0).WithReplacement(0.0),

		FRacingPropertyRange::Between(TEXT("ChassisHalfLengthCm"), 50.0, 600.0).WithReplacement(230.0),
		FRacingPropertyRange::Between(TEXT("ChassisHalfWidthCm"), 30.0, 200.0).WithReplacement(95.0),
		FRacingPropertyRange::Between(TEXT("ChassisHalfHeightCm"), 10.0, 150.0).WithReplacement(55.0),

		FRacingPropertyRange::Between(TEXT("FrontAxleOffsetXCm"), -400.0, 400.0).WithReplacement(140.0),
		FRacingPropertyRange::Between(TEXT("RearAxleOffsetXCm"), -400.0, 400.0).WithReplacement(-145.0),
		FRacingPropertyRange::Between(TEXT("FrontTrackWidthCm"), 60.0, 350.0).WithReplacement(158.0),
		FRacingPropertyRange::Between(TEXT("RearTrackWidthCm"), 60.0, 350.0).WithReplacement(156.0),
		FRacingPropertyRange::Between(TEXT("WheelCentreHeightCm"), -250.0, 100.0).WithReplacement(-35.0),

		FRacingPropertyRange::Between(TEXT("FrontWheelRadiusCm"), 10.0, 80.0).WithReplacement(34.0),
		FRacingPropertyRange::Between(TEXT("RearWheelRadiusCm"), 10.0, 80.0).WithReplacement(35.0),
		FRacingPropertyRange::Between(TEXT("FrontWheelWidthCm"), 5.0, 60.0).WithReplacement(24.0),
		FRacingPropertyRange::Between(TEXT("RearWheelWidthCm"), 5.0, 60.0).WithReplacement(28.0),

		FRacingPropertyRange::Between(TEXT("FrontRearTorqueSplit"), 0.0, 1.0),
		FRacingPropertyRange::Between(TEXT("MaxSteerAngleDegrees"), 1.0, 70.0).WithReplacement(40.0),

		FRacingPropertyRange::Between(TEXT("DragCoefficient"), 0.0, 2.0),
		FRacingPropertyRange::Between(TEXT("DownforceCoefficient"), 0.0, 10.0),
		FRacingPropertyRange::Between(TEXT("FrontalAreaCm2"), 0.0, 200000.0)
	};

	return MakeArrayView(Ranges);
}

FVector UVehicleChassisDataAsset::GetWheelOffsetCm(const EVehicleWheelIndex WheelIndex) const
{
	// +X forward, +Y right, +Z up. A front wheel is at +FrontAxleOffsetXCm; a right
	// wheel is at +half track. Getting either sign backwards produces a car that
	// drives and steers the wrong way round a corner, which is why the convention is
	// restated here rather than only in the header.
	const bool bFront = IsFrontWheel(WheelIndex);
	const bool bRight = (WheelIndex == EVehicleWheelIndex::FrontRight)
		|| (WheelIndex == EVehicleWheelIndex::RearRight);

	const float OffsetX = bFront ? FrontAxleOffsetXCm : RearAxleOffsetXCm;
	const float HalfTrack = 0.5f * (bFront ? FrontTrackWidthCm : RearTrackWidthCm);

	return FVector(OffsetX, bRight ? HalfTrack : -HalfTrack, WheelCentreHeightCm);
}

FRacingValidationResult UVehicleChassisDataAsset::ValidateReadOnly() const
{
	// Duplicate rather than const_cast: Validate(false) still runs EnforceRanges,
	// which always writes, and a read-only API that mutates its own asset would be a
	// far worse defect than the duplication costs. Same pattern as VEH-001's config.
	UVehicleChassisDataAsset* Probe =
		DuplicateObject<UVehicleChassisDataAsset>(this, GetTransientPackage());

	if (Probe == nullptr)
	{
		FRacingValidationResult Result;
		AddFailure(Result, TEXT("MassKg"),
			TEXT("Could not duplicate the asset to run a read-only validation pass"));
		return Result;
	}

	return Probe->Validate(/*bCorrect*/ false);
}

FRacingValidationResult UVehicleChassisDataAsset::Validate(const bool bCorrect)
{
	FRacingValidationResult Result;

	// -- Flat numerics, through the reflective CORE-003 pass ------------------
	if (bCorrect)
	{
		Result.Issues.Append(EnforceRanges(this, StaticRanges()).Issues);
	}
	else
	{
		UVehicleChassisDataAsset* Probe =
			DuplicateObject<UVehicleChassisDataAsset>(this, GetTransientPackage());

		if (Probe != nullptr)
		{
			Result.Issues.Append(EnforceRanges(Probe, StaticRanges()).Issues);
		}
		else
		{
			AddFailure(Result, TEXT("MassKg"),
				TEXT("Could not duplicate the asset to run a read-only range pass"));
		}
	}

	// -- Relationships. None of these are correctable; see the header. --------
	//
	// Every one of them is a geometric contradiction rather than a value out of
	// bounds, and no per-field clamp can express any of them. They are the checks
	// that catch an asset assembled from individually-plausible numbers.

	// 1. The front axle must be in front of the rear axle.
	if (AllFinite({FrontAxleOffsetXCm, RearAxleOffsetXCm}) && !(GetWheelbaseCm() > 0.0f))
	{
		AddFailure(Result, TEXT("FrontAxleOffsetXCm"), FString::Printf(
			TEXT("FrontAxleOffsetXCm (%.2f cm) must exceed RearAxleOffsetXCm (%.2f cm); the derived wheelbase is %.2f cm"),
			FrontAxleOffsetXCm, RearAxleOffsetXCm, GetWheelbaseCm()));
	}

	// 2. Each axle's wheels must clear each other.
	//
	// Two wheels at half a track width apart, each half a section width wide, overlap
	// when the track is narrower than the tyre width. That is an asset that produces
	// two interpenetrating wheel sweeps at the same contact point, which Chaos will
	// happily simulate into a car that shakes itself apart -- with nothing anywhere
	// saying the geometry was impossible.
	if (AllFinite({FrontTrackWidthCm, FrontWheelWidthCm}) && FrontTrackWidthCm <= FrontWheelWidthCm)
	{
		AddFailure(Result, TEXT("FrontTrackWidthCm"), FString::Printf(
			TEXT("FrontTrackWidthCm (%.2f cm) must exceed FrontWheelWidthCm (%.2f cm) or the front wheels intersect each other"),
			FrontTrackWidthCm, FrontWheelWidthCm));
	}

	if (AllFinite({RearTrackWidthCm, RearWheelWidthCm}) && RearTrackWidthCm <= RearWheelWidthCm)
	{
		AddFailure(Result, TEXT("RearTrackWidthCm"), FString::Printf(
			TEXT("RearTrackWidthCm (%.2f cm) must exceed RearWheelWidthCm (%.2f cm) or the rear wheels intersect each other"),
			RearTrackWidthCm, RearWheelWidthCm));
	}

	// 3. The wheelbase must clear the wheels themselves.
	if (AllFinite({FrontAxleOffsetXCm, RearAxleOffsetXCm, FrontWheelRadiusCm, RearWheelRadiusCm})
		&& GetWheelbaseCm() > 0.0f
		&& GetWheelbaseCm() <= (FrontWheelRadiusCm + RearWheelRadiusCm))
	{
		AddFailure(Result, TEXT("RearAxleOffsetXCm"), FString::Printf(
			TEXT("Wheelbase (%.2f cm) must exceed the sum of the wheel radii (%.2f cm) or front and rear wheels intersect"),
			GetWheelbaseCm(), FrontWheelRadiusCm + RearWheelRadiusCm));
	}

	// 4. The centre of mass must be inside the collision box.
	//
	// Chaos accepts a centre of mass outside the body without complaint and produces a
	// vehicle that rotates about a point in mid-air. It looks like a suspension bug.
	if (AllFinite({CentreOfMassOffsetXCm, CentreOfMassOffsetYCm, CentreOfMassOffsetZCm,
		ChassisHalfLengthCm, ChassisHalfWidthCm, ChassisHalfHeightCm}))
	{
		if (FMath::Abs(CentreOfMassOffsetXCm) > ChassisHalfLengthCm
			|| FMath::Abs(CentreOfMassOffsetYCm) > ChassisHalfWidthCm
			|| FMath::Abs(CentreOfMassOffsetZCm) > ChassisHalfHeightCm)
		{
			AddFailure(Result, TEXT("CentreOfMassOffsetZCm"), FString::Printf(
				TEXT("Centre of mass (%.2f, %.2f, %.2f) cm lies outside the chassis half-extent (%.2f, %.2f, %.2f) cm"),
				CentreOfMassOffsetXCm, CentreOfMassOffsetYCm, CentreOfMassOffsetZCm,
				ChassisHalfLengthCm, ChassisHalfWidthCm, ChassisHalfHeightCm));
		}
	}

	// 5. The centre of mass must be at or below the wheel centres.
	//
	// A racing car whose mass sits above its wheel centres rolls over under lateral
	// load. This is a HARD failure rather than a warning because it is the single
	// most common way a plausible-looking chassis asset produces a car that is blamed
	// on the tyre model for a week (Docs/02-VehiclePhysics.md: "Keep the first tune
	// simple and stable").
	if (AllFinite({CentreOfMassOffsetZCm, WheelCentreHeightCm})
		&& CentreOfMassOffsetZCm > WheelCentreHeightCm + FMath::Max(FrontWheelRadiusCm, RearWheelRadiusCm))
	{
		AddFailure(Result, TEXT("CentreOfMassOffsetZCm"), FString::Printf(
			TEXT("Centre of mass height (%.2f cm) is above the top of the wheels (%.2f cm); the vehicle will roll over under lateral load"),
			CentreOfMassOffsetZCm,
			WheelCentreHeightCm + FMath::Max(FrontWheelRadiusCm, RearWheelRadiusCm)));
	}

	// 6. The torque split is meaningless unless the drivetrain is AWD.
	//
	// Reported rather than silently ignored: Chaos reads FrontRearSplit only for the
	// 4W case, so an author who sets 0.3 under RWD has changed nothing and has no way
	// to find that out from the car. Not a hard failure -- the asset is drivable --
	// but it must appear in the report.
	if (DrivetrainLayout != EVehicleDrivetrainLayout::AllWheelDrive
		&& FMath::IsFinite(FrontRearTorqueSplit)
		&& !FMath::IsNearlyEqual(FrontRearTorqueSplit, 0.5f))
	{
		AddFailure(Result, TEXT("FrontRearTorqueSplit"), FString::Printf(
			TEXT("FrontRearTorqueSplit is %.3f but DrivetrainLayout is not AllWheelDrive; Chaos ignores the split for this layout"),
			FrontRearTorqueSplit));
	}

	return Result;
}
