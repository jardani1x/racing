// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/VehicleTuneDataAsset.h"

// For the AllTuneValuesFinite guard's parameter type. Included explicitly rather than relied on
// transitively, same reasoning as VehicleChassisDataAsset.cpp.
#include <initializer_list>

using namespace RacingSim::Validation;

namespace
{
	/**
	 * A relationship or curve failure, named by the property an author would edit.
	 *
	 * Named distinctly from VehicleChassisDataAsset.cpp's AddChassisValidationFailure and
	 * VehicleInputConfig.cpp's AddFailure: all three live in a file-anonymous namespace,
	 * and a Unity Build merges these translation units, where two identical signatures are
	 * a genuine duplicate definition (C2084 -- caught by VEH-002's repair cycle 1, not by
	 * anything a reviewer spots by eye).
	 */
	void AddTuneValidationFailure(FRacingValidationResult& Result, const FName PropertyName, FString Message)
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
	 * already been replaced; in the report-only path it has not, and every comparison
	 * against a NaN is false. Without this guard a NaN redline would report as
	 * "ChangeUpRpm exceeds MaxRpm" instead of as the non-finite value the range pass
	 * already named. One defect, one issue.
	 *
	 * Named AllTuneValuesFinite, not AllFinite, for the SAME Unity Build reason recorded on
	 * AddTuneValidationFailure above: VehicleChassisDataAsset.cpp already defines an
	 * identically-signatured AllFinite in its own file-anonymous namespace. Anonymous
	 * namespaces give internal linkage, so a non-unity build links cleanly and hides this --
	 * but a Unity Build concatenates both translation units and the two definitions become a
	 * redefinition (C2084). VEH-003 shipped with this collision and it was caught at the
	 * build gate, not by review; the helper above had the warning and this one did not obey it.
	 */
	bool AllTuneValuesFinite(const std::initializer_list<float> Values)
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

	/**
	 * Shared curve validation, used for both curves this asset owns.
	 *
	 * Both are "a dimensionless multiplier against a non-negative speed-like domain", and
	 * writing the checks twice would guarantee they drift. Deliberately a FAILURE and not
	 * a fall-back, matching VEH-001's policy: a silent fallback to "no curve" is the
	 * difference between a car that is slow and a car that has no torque, and the author
	 * would see an authored curve in the asset and believe it.
	 */
	void ValidateNormalisedCurve(
		FRacingValidationResult& Result,
		const FRuntimeFloatCurve& Curve,
		const FName PropertyName,
		const TCHAR* DomainDescription,
		const float MinValue,
		const float MaxValue,
		const bool bRequireNonZeroPeak)
	{
		const FRichCurve* RichCurve = Curve.GetRichCurveConst();

		if (RichCurve == nullptr || RichCurve->GetNumKeys() < 2)
		{
			AddTuneValidationFailure(Result, PropertyName, FString::Printf(
				TEXT("%s has %d keys; at least 2 are required"),
				*PropertyName.ToString(), RichCurve != nullptr ? RichCurve->GetNumKeys() : 0));
			return;
		}

		float Peak = 0.0f;

		// Iterating Keys directly rather than through GetKeyHandleIterator: the handle
		// iterator's const GetKey overload returns by value and adds a per-key map lookup
		// over the same data. Same choice VEH-001's config validation made.
		for (const FRichCurveKey& Key : RichCurve->Keys)
		{
			if (!FMath::IsFinite(Key.Time) || !FMath::IsFinite(Key.Value))
			{
				AddTuneValidationFailure(Result, PropertyName, FString::Printf(
					TEXT("%s has a non-finite key time or value"), *PropertyName.ToString()));
				return;
			}

			if (Key.Time < 0.0f)
			{
				AddTuneValidationFailure(Result, PropertyName, FString::Printf(
					TEXT("%s has a key at %f; the domain is non-negative %s"),
					*PropertyName.ToString(), Key.Time, DomainDescription));
				return;
			}

			if (Key.Value < MinValue || Key.Value > MaxValue)
			{
				AddTuneValidationFailure(Result, PropertyName, FString::Printf(
					TEXT("%s key at %f has value %f; the range is [%f, %f]"),
					*PropertyName.ToString(), Key.Time, Key.Value, MinValue, MaxValue));
				return;
			}

			Peak = FMath::Max(Peak, Key.Value);
		}

		if (bRequireNonZeroPeak && Peak <= 0.0f)
		{
			// The whole-curve failure a per-key check cannot see: every key legally in
			// [0,1] and every key zero. Chaos multiplies MaxTorque by this, so the car
			// reports 420 Nm and produces none.
			AddTuneValidationFailure(Result, PropertyName, FString::Printf(
				TEXT("%s peaks at %f; every key is zero, so the multiplied result is always zero"),
				*PropertyName.ToString(), Peak));
		}
	}

	/** Hash every key of a curve, so a retune of the curve alone still changes the content hash. */
	uint32 HashCurve(uint32 Hash, const FRuntimeFloatCurve& Curve)
	{
		const FRichCurve* RichCurve = Curve.GetRichCurveConst();
		if (RichCurve == nullptr)
		{
			return Hash;
		}

		for (const FRichCurveKey& Key : RichCurve->Keys)
		{
			Hash = HashCombine(Hash, GetTypeHash(Key.Time));
			Hash = HashCombine(Hash, GetTypeHash(Key.Value));
		}
		return Hash;
	}
}

UVehicleTuneDataAsset::UVehicleTuneDataAsset()
{
	// An ORIGINAL PROTOTYPE ENVELOPE, not a measured engine: a flat-ish mid-range that
	// peaks around 5200 rpm and falls away to the redline. Chosen to be stable and
	// self-consistent, per Docs/02-VehiclePhysics.md ("Use envelopes rather than fake
	// precision until source data is authoritative"), and to satisfy this asset's own
	// validation out of the box -- see the constructor's header comment.
	if (FRichCurve* TorqueCurve = NormalisedTorqueCurve.GetRichCurve())
	{
		TorqueCurve->Reset();
		TorqueCurve->AddKey(950.0f, 0.42f);
		TorqueCurve->AddKey(2000.0f, 0.72f);
		TorqueCurve->AddKey(3500.0f, 0.92f);
		TorqueCurve->AddKey(5200.0f, 1.00f);
		TorqueCurve->AddKey(6800.0f, 0.91f);
		TorqueCurve->AddKey(7800.0f, 0.74f);
	}

	// Authored even though SteerSpeedAuthority defaults to InputLayer (where it is
	// ignored): an asset whose author flips the enum to ChaosCurve must not discover that
	// the curve they never touched has no keys. Domain is MILES PER HOUR -- Chaos'.
	if (FRichCurve* SteerCurve = SteerScaleBySpeedMphCurve.GetRichCurve())
	{
		SteerCurve->Reset();
		SteerCurve->AddKey(0.0f, 1.00f);
		SteerCurve->AddKey(30.0f, 0.85f);
		SteerCurve->AddKey(80.0f, 0.55f);
		SteerCurve->AddKey(160.0f, 0.35f);
	}
}

TConstArrayView<FRacingPropertyRange> UVehicleTuneDataAsset::StaticRanges()
{
	// Mirrors the ClampMin/ClampMax metadata on this class member-for-member. The
	// duplication is CORE-003's design, not an oversight: metadata is compiled out when
	// WITH_METADATA is 0 (i.e. in the packaged Game build), so the metadata cannot be the
	// enforcement path. RacingSim.Vehicle.TuneRanges asserts the two agree in BOTH
	// directions, which is what stops this table rotting when a property is added.
	//
	// Replacements are declared only where A BOUND IS NOT A SAFE VALUE:
	//
	//   MaxTorqueNm         -- 50 Nm cannot move the car, 2000 Nm spins it in every gear.
	//   MaxRpm / IdleRpm    -- neither extreme is a usable engine.
	//   ChangeUp/DownRpm    -- the minimum shift point is below idle; the maximum is past
	//                          any redline. Both produce a gearbox that never behaves.
	//   *SpringRateNPerM    -- 1 N/m is a car resting on its bump stops; 1000 N/m is a
	//                          kerb strike that launches it.
	//   *DampingRatio       -- the minimum is very nearly undamped (oscillates until the
	//                          integrator gives up); the maximum is a solid axle.
	//   Suspension*Cm       -- 0.5 cm of travel is no suspension at all.
	//   *BrakeTorqueNm      -- the minimum cannot stop the car; the maximum locks
	//                          instantly every time.
	//   OuterInnerAngleRatio-- 0.1 is a car that barely turns its outer wheel.
	//   TransmissionEfficiency, FinalDriveRatio, GearChangeTimeSeconds,
	//   EngineBrakeEffect, rollbars, WheelLoadRatio, rev terms -- their bounds ARE
	//   defensible values, so no replacement is declared. That asymmetry is deliberate;
	//   see Core/RacingSimValidation.h on why "clamp to the bound" is not always safe.
	static const FRacingPropertyRange Ranges[] =
	{
		FRacingPropertyRange::Between(TEXT("TuneSchemaVersion"), 1.0, 1000.0).WithReplacement(1.0),

		FRacingPropertyRange::Between(TEXT("MaxTorqueNm"), 50.0, 2000.0).WithReplacement(420.0),
		FRacingPropertyRange::Between(TEXT("MaxRpm"), 1000.0, 20000.0).WithReplacement(7800.0),
		FRacingPropertyRange::Between(TEXT("IdleRpm"), 200.0, 5000.0).WithReplacement(950.0),
		FRacingPropertyRange::Between(TEXT("EngineBrakeEffect"), 0.0, 1.0),
		FRacingPropertyRange::Between(TEXT("EngineRevUpMoi"), 0.01, 100.0),
		FRacingPropertyRange::Between(TEXT("EngineRevDownRate"), 0.01, 2000.0),

		FRacingPropertyRange::Between(TEXT("FinalDriveRatio"), 0.5, 10.0),
		FRacingPropertyRange::Between(TEXT("ChangeUpRpm"), 500.0, 20000.0).WithReplacement(7200.0),
		FRacingPropertyRange::Between(TEXT("ChangeDownRpm"), 300.0, 20000.0).WithReplacement(3200.0),
		FRacingPropertyRange::Between(TEXT("GearChangeTimeSeconds"), 0.0, 3.0),
		FRacingPropertyRange::Between(TEXT("TransmissionEfficiency"), 0.05, 1.0),

		FRacingPropertyRange::Between(TEXT("FrontBrakeTorqueNm"), 100.0, 10000.0)
			.WithReplacement(RacingSim::Vehicle::PrototypeTuneDefaults::FrontBrakeTorqueNm),
		FRacingPropertyRange::Between(TEXT("RearBrakeTorqueNm"), 50.0, 10000.0)
			.WithReplacement(RacingSim::Vehicle::PrototypeTuneDefaults::RearBrakeTorqueNm),
		FRacingPropertyRange::Between(TEXT("HandbrakeTorqueNm"), 100.0, 10000.0)
			.WithReplacement(RacingSim::Vehicle::PrototypeTuneDefaults::HandbrakeTorqueNm),

		FRacingPropertyRange::Between(TEXT("OuterInnerAngleRatio"), 0.1, 1.0).WithReplacement(0.7),

		FRacingPropertyRange::Between(TEXT("FrontSpringRateNPerM"), 1.0, 1000.0)
			.WithReplacement(RacingSim::Vehicle::PrototypeTuneDefaults::FrontSpringRateNPerM),
		FRacingPropertyRange::Between(TEXT("RearSpringRateNPerM"), 1.0, 1000.0)
			.WithReplacement(RacingSim::Vehicle::PrototypeTuneDefaults::RearSpringRateNPerM),
		FRacingPropertyRange::Between(TEXT("FrontSpringPreloadN"), 0.0, 1000.0),
		FRacingPropertyRange::Between(TEXT("RearSpringPreloadN"), 0.0, 1000.0),
		FRacingPropertyRange::Between(TEXT("FrontDampingRatio"), 0.05, 1.5)
			.WithReplacement(RacingSim::Vehicle::PrototypeTuneDefaults::FrontDampingRatio),
		FRacingPropertyRange::Between(TEXT("RearDampingRatio"), 0.05, 1.5)
			.WithReplacement(RacingSim::Vehicle::PrototypeTuneDefaults::RearDampingRatio),
		FRacingPropertyRange::Between(TEXT("SuspensionMaxRaiseCm"), 0.5, 60.0)
			.WithReplacement(RacingSim::Vehicle::PrototypeTuneDefaults::SuspensionMaxRaiseCm),
		FRacingPropertyRange::Between(TEXT("SuspensionMaxDropCm"), 0.5, 60.0)
			.WithReplacement(RacingSim::Vehicle::PrototypeTuneDefaults::SuspensionMaxDropCm),
		FRacingPropertyRange::Between(TEXT("FrontRollbarScaling"), 0.0, 1.0),
		FRacingPropertyRange::Between(TEXT("RearRollbarScaling"), 0.0, 1.0),
		FRacingPropertyRange::Between(TEXT("WheelLoadRatio"), 0.0, 1.0)
	};

	return MakeArrayView(Ranges);
}

float UVehicleTuneDataAsset::GetPeakNormalisedTorque() const
{
	const FRichCurve* Curve = NormalisedTorqueCurve.GetRichCurveConst();
	if (Curve == nullptr || Curve->GetNumKeys() == 0)
	{
		return 0.0f;
	}

	// Iterate keys explicitly rather than FRichCurve::GetValueRange (fixed on code
	// review, VEH-003 repair cycle 2, HIGH-1 not genuinely closed by cycle 1).
	// GetValueRange folds with FMath::Max, and Max(finite, NaN) returns the FINITE
	// operand -- so a curve whose non-finite key is NOT the one FMath::Max happens to
	// compare first can still report a finite, positive peak. That silently defeated
	// this ticket's own ApplyTuneAsset() gate (RacingVehiclePawn.cpp), letting a
	// partially-NaN curve reach Chaos, which is the exact corrupting-solver-state
	// outcome the gate exists to prevent. A single non-finite key anywhere now
	// unconditionally reports the curve as unusable, independent of key order.
	float MaxValue = -MAX_FLT;
	for (int32 KeyIndex = 0; KeyIndex < Curve->GetNumKeys(); ++KeyIndex)
	{
		const FRichCurveKey& Key = Curve->Keys[KeyIndex];
		if (!FMath::IsFinite(Key.Time) || !FMath::IsFinite(Key.Value))
		{
			return 0.0f;
		}
		MaxValue = FMath::Max(MaxValue, Key.Value);
	}
	return FMath::IsFinite(MaxValue) ? MaxValue : 0.0f;
}

float UVehicleTuneDataAsset::GetOverallRatioForGear(const int32 ForwardGear) const
{
	// 1-based, as a driver counts gears -- deliberately not matching the array index, so a
	// caller reading "gear 3" from telemetry cannot be off by one without noticing.
	if (ForwardGear < 1 || ForwardGear > ForwardGearRatios.Num())
	{
		return 0.0f;
	}

	return ForwardGearRatios[ForwardGear - 1] * FinalDriveRatio;
}

uint32 UVehicleTuneDataAsset::ComputeContentHash() const
{
	uint32 Hash = GetTypeHash(TuneId);
	Hash = HashCombine(Hash, GetTypeHash(TuneSchemaVersion));

	Hash = HashCombine(Hash, GetTypeHash(MaxTorqueNm));
	Hash = HashCombine(Hash, GetTypeHash(MaxRpm));
	Hash = HashCombine(Hash, GetTypeHash(IdleRpm));
	Hash = HashCombine(Hash, GetTypeHash(EngineBrakeEffect));
	Hash = HashCombine(Hash, GetTypeHash(EngineRevUpMoi));
	Hash = HashCombine(Hash, GetTypeHash(EngineRevDownRate));
	Hash = HashCurve(Hash, NormalisedTorqueCurve);

	for (const float Ratio : ForwardGearRatios)
	{
		Hash = HashCombine(Hash, GetTypeHash(Ratio));
	}
	for (const float Ratio : ReverseGearRatios)
	{
		Hash = HashCombine(Hash, GetTypeHash(Ratio));
	}
	Hash = HashCombine(Hash, GetTypeHash(FinalDriveRatio));
	Hash = HashCombine(Hash, GetTypeHash(ChangeUpRpm));
	Hash = HashCombine(Hash, GetTypeHash(ChangeDownRpm));
	Hash = HashCombine(Hash, GetTypeHash(GearChangeTimeSeconds));
	Hash = HashCombine(Hash, GetTypeHash(TransmissionEfficiency));

	Hash = HashCombine(Hash, GetTypeHash(FrontBrakeTorqueNm));
	Hash = HashCombine(Hash, GetTypeHash(RearBrakeTorqueNm));
	Hash = HashCombine(Hash, GetTypeHash(HandbrakeTorqueNm));

	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(SteeringModel)));
	Hash = HashCombine(Hash, GetTypeHash(OuterInnerAngleRatio));
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(SteerSpeedAuthority)));
	Hash = HashCurve(Hash, SteerScaleBySpeedMphCurve);

	Hash = HashCombine(Hash, GetTypeHash(FrontSpringRateNPerM));
	Hash = HashCombine(Hash, GetTypeHash(RearSpringRateNPerM));
	Hash = HashCombine(Hash, GetTypeHash(FrontSpringPreloadN));
	Hash = HashCombine(Hash, GetTypeHash(RearSpringPreloadN));
	Hash = HashCombine(Hash, GetTypeHash(FrontDampingRatio));
	Hash = HashCombine(Hash, GetTypeHash(RearDampingRatio));
	Hash = HashCombine(Hash, GetTypeHash(SuspensionMaxRaiseCm));
	Hash = HashCombine(Hash, GetTypeHash(SuspensionMaxDropCm));
	Hash = HashCombine(Hash, GetTypeHash(FrontRollbarScaling));
	Hash = HashCombine(Hash, GetTypeHash(RearRollbarScaling));
	Hash = HashCombine(Hash, GetTypeHash(WheelLoadRatio));

	// Any field added to this class must be combined in here as well, or a retune of it
	// is invisible on a published result.
	return Hash;
}

FRacingContentVersion UVehicleTuneDataAsset::GetContentVersion() const
{
	// Closes the hole CORE-002 opened and named: RacingSimBuildId.h documents
	// FRacingSimVersionStamp::CarSpecVersion as "Populated by VEH-003 from the car spec /
	// tune asset. Empty here by design." Same three-field shape as
	// URaceRulesetDataAsset::GetContentVersion, deliberately, so a consumer treats every
	// content version identically.
	FRacingContentVersion Version;
	Version.AssetId = TuneId;
	Version.SchemaVersion = TuneSchemaVersion;
	Version.ContentHash = ComputeContentHash();
	return Version;
}

FRacingValidationResult UVehicleTuneDataAsset::ValidateReadOnly() const
{
	// Duplicate rather than const_cast: Validate(false) still runs EnforceRanges, which
	// always writes. A read-only API that mutates its own asset would be a far worse
	// defect than the duplication costs. Same pattern as the chassis asset.
	UVehicleTuneDataAsset* Probe = DuplicateObject<UVehicleTuneDataAsset>(this, GetTransientPackage());

	if (Probe == nullptr)
	{
		FRacingValidationResult Result;
		AddTuneValidationFailure(Result, TEXT("TuneId"),
			TEXT("Could not duplicate the asset to run a read-only validation pass"));
		return Result;
	}

	return Probe->Validate(/*bCorrect*/ false);
}

FRacingValidationResult UVehicleTuneDataAsset::Validate(const bool bCorrect)
{
	FRacingValidationResult Result;

	// -- Flat numerics, through the reflective CORE-003 pass ------------------
	if (bCorrect)
	{
		Result.Issues.Append(EnforceRanges(this, StaticRanges()).Issues);
	}
	else
	{
		UVehicleTuneDataAsset* Probe = DuplicateObject<UVehicleTuneDataAsset>(this, GetTransientPackage());
		if (Probe != nullptr)
		{
			Result.Issues.Append(EnforceRanges(Probe, StaticRanges()).Issues);
		}
		else
		{
			AddTuneValidationFailure(Result, TEXT("TuneId"),
				TEXT("Could not duplicate the asset to run a read-only range pass"));
		}
	}

	// -- Identity -------------------------------------------------------------
	if (TuneId.IsNone())
	{
		// FRacingContentVersion::IsPopulated() rejects an unnamed asset, and
		// FRacingSimVersionStamp::IsPublishable() then refuses the whole stamp. Caught
		// here so the failure names the tune rather than surfacing as an unpublishable
		// lap time an hour later.
		AddTuneValidationFailure(Result, TEXT("TuneId"),
			TEXT("TuneId is None; an unnamed tune cannot identify itself on a race result"));
	}

	// -- Engine relationships -------------------------------------------------
	if (AllTuneValuesFinite({IdleRpm, MaxRpm}) && !(MaxRpm > IdleRpm))
	{
		AddTuneValidationFailure(Result, TEXT("MaxRpm"), FString::Printf(
			TEXT("MaxRpm (%.1f rpm) must exceed IdleRpm (%.1f rpm)"), MaxRpm, IdleRpm));
	}

	ValidateNormalisedCurve(Result, NormalisedTorqueCurve, TEXT("NormalisedTorqueCurve"),
		TEXT("engine speed in rpm"), 0.0f, 1.0f, /*bRequireNonZeroPeak*/ true);

	// -- Transmission relationships -------------------------------------------
	if (ForwardGearRatios.Num() == 0)
	{
		AddTuneValidationFailure(Result, TEXT("ForwardGearRatios"),
			TEXT("ForwardGearRatios is empty; the vehicle has no forward gear"));
	}
	else
	{
		for (int32 Index = 0; Index < ForwardGearRatios.Num(); ++Index)
		{
			const float Ratio = ForwardGearRatios[Index];

			if (!FMath::IsFinite(Ratio) || Ratio <= 0.0f)
			{
				AddTuneValidationFailure(Result, TEXT("ForwardGearRatios"), FString::Printf(
					TEXT("ForwardGearRatios[%d] is %f; every forward ratio must be finite and positive"),
					Index, Ratio));
				break;
			}

			// Strictly decreasing, first gear first. A set whose third gear is shorter
			// than its second accelerates HARDER after an upshift, which presents as a
			// tyre or torque defect and is neither.
			if (Index > 0 && !(Ratio < ForwardGearRatios[Index - 1]))
			{
				AddTuneValidationFailure(Result, TEXT("ForwardGearRatios"), FString::Printf(
					TEXT("ForwardGearRatios[%d] (%f) is not shorter than [%d] (%f); forward ratios must strictly decrease, first gear first"),
					Index, Ratio, Index - 1, ForwardGearRatios[Index - 1]));
				break;
			}
		}
	}

	if (ReverseGearRatios.Num() == 0)
	{
		AddTuneValidationFailure(Result, TEXT("ReverseGearRatios"),
			TEXT("ReverseGearRatios is empty; the vehicle cannot reverse, which VEH-005's recovery path assumes it can"));
	}
	else
	{
		for (int32 Index = 0; Index < ReverseGearRatios.Num(); ++Index)
		{
			const float Ratio = ReverseGearRatios[Index];
			if (!FMath::IsFinite(Ratio) || Ratio <= 0.0f)
			{
				// POSITIVE magnitudes: Chaos applies the direction itself. A negative
				// entry here is an author encoding the sign twice.
				AddTuneValidationFailure(Result, TEXT("ReverseGearRatios"), FString::Printf(
					TEXT("ReverseGearRatios[%d] is %f; reverse ratios are positive magnitudes and Chaos applies the sign"),
					Index, Ratio));
				break;
			}
		}
	}

	if (AllTuneValuesFinite({ChangeUpRpm, ChangeDownRpm}) && !(ChangeUpRpm > ChangeDownRpm))
	{
		// Equal or inverted shift points make an automatic gearbox that shifts up and
		// immediately back down, forever, at part throttle.
		AddTuneValidationFailure(Result, TEXT("ChangeUpRpm"), FString::Printf(
			TEXT("ChangeUpRpm (%.1f rpm) must exceed ChangeDownRpm (%.1f rpm) or the gearbox hunts between two gears"),
			ChangeUpRpm, ChangeDownRpm));
	}

	if (AllTuneValuesFinite({ChangeUpRpm, MaxRpm}) && ChangeUpRpm > MaxRpm)
	{
		AddTuneValidationFailure(Result, TEXT("ChangeUpRpm"), FString::Printf(
			TEXT("ChangeUpRpm (%.1f rpm) exceeds MaxRpm (%.1f rpm); the gearbox would never upshift"),
			ChangeUpRpm, MaxRpm));
	}

	if (AllTuneValuesFinite({ChangeDownRpm, IdleRpm}) && ChangeDownRpm < IdleRpm)
	{
		AddTuneValidationFailure(Result, TEXT("ChangeDownRpm"), FString::Printf(
			TEXT("ChangeDownRpm (%.1f rpm) is below IdleRpm (%.1f rpm); the gearbox would never downshift"),
			ChangeDownRpm, IdleRpm));
	}

	// -- Brakes ---------------------------------------------------------------
	if (AllTuneValuesFinite({FrontBrakeTorqueNm, RearBrakeTorqueNm}) && RearBrakeTorqueNm > FrontBrakeTorqueNm)
	{
		// Rear-biased braking locks the rear axle first, which spins the car under
		// straight-line braking. Reported rather than corrected: a deliberate rear bias
		// is a legitimate (if hostile) tune, and this asset does not get to overrule an
		// author -- it gets to make sure they saw it.
		AddTuneValidationFailure(Result, TEXT("RearBrakeTorqueNm"), FString::Printf(
			TEXT("RearBrakeTorqueNm (%.1f Nm) exceeds FrontBrakeTorqueNm (%.1f Nm); a rear brake bias locks the rear axle first and spins the car under braking"),
			RearBrakeTorqueNm, FrontBrakeTorqueNm));
	}

	// -- Steering -------------------------------------------------------------
	if (SteeringModel != EVehicleSteeringModel::AngleRatio
		&& FMath::IsFinite(OuterInnerAngleRatio)
		&& !FMath::IsNearlyEqual(OuterInnerAngleRatio, 0.7f))
	{
		// Chaos reads AngleRatio only under ESteeringType::AngleRatio. Same shape as the
		// chassis asset's FrontRearTorqueSplit-outside-AWD report: an author who changed
		// a number that does nothing must be told, or they will tune against it.
		AddTuneValidationFailure(Result, TEXT("OuterInnerAngleRatio"), FString::Printf(
			TEXT("OuterInnerAngleRatio is %.3f but SteeringModel is not AngleRatio; Chaos ignores the ratio for this model"),
			OuterInnerAngleRatio));
	}

	if (SteerSpeedAuthority == EVehicleSteerSpeedAuthority::ChaosCurve)
	{
		// Only validated when it is the selected authority, exactly as VEH-001 validates
		// its own curve only under ESteerSpeedScaleMode::Curve. Domain is MPH here.
		ValidateNormalisedCurve(Result, SteerScaleBySpeedMphCurve, TEXT("SteerScaleBySpeedMphCurve"),
			TEXT("road speed in mph"), 0.05f, 1.0f, /*bRequireNonZeroPeak*/ true);
	}

	// -- Suspension -----------------------------------------------------------
	if (AllTuneValuesFinite({SuspensionMaxRaiseCm, SuspensionMaxDropCm})
		&& (SuspensionMaxRaiseCm + SuspensionMaxDropCm) <= 1.0f)
	{
		AddTuneValidationFailure(Result, TEXT("SuspensionMaxDropCm"), FString::Printf(
			TEXT("Total suspension travel is %.2f cm; a vehicle with effectively no travel transmits every kerb strike straight into the chassis"),
			SuspensionMaxRaiseCm + SuspensionMaxDropCm));
	}

	return Result;
}
