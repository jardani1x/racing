// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/VehicleInputConfig.h"

#include "Core/RacingSimUnits.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "UObject/UnrealType.h"

using namespace RacingSim::Validation;

namespace
{
	/**
	 * Apply one declared range to one float, by hand.
	 *
	 * This mirrors EnforceRanges' semantics exactly -- non-finite is replaced rather
	 * than clamped, because a NaN has no nearest legal value; out-of-range clamps to
	 * the violated bound -- but reaches a nested struct member, which the reflective
	 * pass cannot. See the header for why that split exists.
	 *
	 * The issue message names the profile, the property, the loaded value and the
	 * corrected value, so a validation report is actionable without opening the asset.
	 */
	void ApplyRange(
		float& Value,
		const FRacingPropertyRange& Range,
		const FString& Context,
		const bool bCorrect,
		FRacingValidationResult& Result)
	{
		const double Loaded = static_cast<double>(Value);

		double Corrected = Loaded;
		ERangeAction Action = ERangeAction::InRange;

		if (!FMath::IsFinite(Loaded))
		{
			// No nearest legal value exists. Prefer the declared replacement, then
			// the minimum (every range in this file has its safe end at the minimum
			// except SteerSaturation/PedalSaturation, which declare replacements).
			Corrected = Range.bHasReplacement ? Range.ReplacementValue
				: (Range.bHasMin ? Range.Min : (Range.bHasMax ? Range.Max : 0.0));
			Action = ERangeAction::ReplacedNonFinite;
		}
		else if (Range.bHasMin && Loaded < Range.Min)
		{
			Corrected = Range.bHasReplacement ? Range.ReplacementValue : Range.Min;
			Action = Range.bHasReplacement ? ERangeAction::ReplacedOutOfRange : ERangeAction::ClampedToMin;
		}
		else if (Range.bHasMax && Loaded > Range.Max)
		{
			Corrected = Range.bHasReplacement ? Range.ReplacementValue : Range.Max;
			Action = Range.bHasReplacement ? ERangeAction::ReplacedOutOfRange : ERangeAction::ClampedToMax;
		}

		if (Action == ERangeAction::InRange)
		{
			return;
		}

		FRacingValidationIssue Issue;
		Issue.PropertyName = Range.PropertyName;
		Issue.Action = Action;
		Issue.LoadedValue = Loaded;
		Issue.CorrectedValue = Corrected;
		Issue.Message = FString::Printf(
			TEXT("%s.%s: %f is out of range [%s, %s]; using %f"),
			*Context,
			*Range.PropertyName.ToString(),
			Loaded,
			Range.bHasMin ? *FString::SanitizeFloat(Range.Min) : TEXT("-inf"),
			Range.bHasMax ? *FString::SanitizeFloat(Range.Max) : TEXT("+inf"),
			Corrected);

		Result.Issues.Add(MoveTemp(Issue));

		if (bCorrect)
		{
			Value = static_cast<float>(Corrected);
		}
	}

	/** Report a problem that has no numeric correction -- a missing binding, a bad curve, a broken relationship. */
	void AddFailure(FRacingValidationResult& Result, const FName PropertyName, FString Message)
	{
		FRacingValidationIssue Issue;
		Issue.PropertyName = PropertyName;
		Issue.Action = ERangeAction::Failed;
		Issue.Message = MoveTemp(Message);
		Result.Issues.Add(MoveTemp(Issue));
	}
}

RacingSim::Validation::FRacingValidationResult FVehicleInputProfile::Validate(const FString& ProfileName, const bool bCorrect)
{
	FRacingValidationResult Result;

	// This table mirrors the ClampMin/ClampMax metadata on the members above, in
	// declaration order, and is the enforcement path in every build configuration --
	// the metadata is editor-only (WITH_METADATA == WITH_EDITORONLY_DATA) and would
	// enforce nothing in a packaged Game build. Same reasoning as CORE-003.
	//
	// The two saturations declare a replacement of 1.0 rather than clamping to their
	// minimum of 0.1, because 0.1 is not their safe end: a saturation of 0.1 means
	// "one tenth of stick travel is full lock", so correcting a NaN to the minimum
	// would turn a broken value into a violently oversensitive car. 1.0 (use the
	// whole axis) is the value that cannot surprise anyone.
	ApplyRange(SteerDeadZone,       FRacingPropertyRange::Between(TEXT("SteerDeadZone"), 0.0, 0.9),          ProfileName, bCorrect, Result);
	ApplyRange(SteerSaturation,     FRacingPropertyRange::Between(TEXT("SteerSaturation"), 0.1, 1.0).WithReplacement(1.0), ProfileName, bCorrect, Result);
	ApplyRange(SteerResponseGamma,  FRacingPropertyRange::Between(TEXT("SteerResponseGamma"), 0.2, 5.0).WithReplacement(1.0), ProfileName, bCorrect, Result);
	ApplyRange(PedalDeadZone,       FRacingPropertyRange::Between(TEXT("PedalDeadZone"), 0.0, 0.9),          ProfileName, bCorrect, Result);
	ApplyRange(PedalSaturation,     FRacingPropertyRange::Between(TEXT("PedalSaturation"), 0.1, 1.0).WithReplacement(1.0), ProfileName, bCorrect, Result);
	ApplyRange(PedalResponseGamma,  FRacingPropertyRange::Between(TEXT("PedalResponseGamma"), 0.2, 5.0).WithReplacement(1.0), ProfileName, bCorrect, Result);
	ApplyRange(ThrottleRiseRate,    FRacingPropertyRange::Between(TEXT("ThrottleRiseRate"), 0.0, 1000.0),    ProfileName, bCorrect, Result);
	ApplyRange(ThrottleFallRate,    FRacingPropertyRange::Between(TEXT("ThrottleFallRate"), 0.0, 1000.0),    ProfileName, bCorrect, Result);
	ApplyRange(BrakeRiseRate,       FRacingPropertyRange::Between(TEXT("BrakeRiseRate"), 0.0, 1000.0),       ProfileName, bCorrect, Result);
	ApplyRange(BrakeFallRate,       FRacingPropertyRange::Between(TEXT("BrakeFallRate"), 0.0, 1000.0),       ProfileName, bCorrect, Result);
	ApplyRange(SteerRate,           FRacingPropertyRange::Between(TEXT("SteerRate"), 0.0, 1000.0),           ProfileName, bCorrect, Result);
	ApplyRange(SteerCentringRate,   FRacingPropertyRange::Between(TEXT("SteerCentringRate"), 0.0, 1000.0),   ProfileName, bCorrect, Result);
	ApplyRange(ResetHoldSeconds,    FRacingPropertyRange::Between(TEXT("ResetHoldSeconds"), 0.05, 10.0).WithReplacement(0.5), ProfileName, bCorrect, Result);

	// Cross-field relationships. No per-field range can express these, and both are
	// reachable from values that are individually legal: dead zone 0.9 with
	// saturation 0.5 passes every bound above and leaves an empty usable band, where
	// the rescale divides by a non-positive width.
	//
	// Checked AFTER the per-field pass so that under bCorrect the relationship is
	// evaluated on corrected values -- otherwise a NaN dead zone would report both a
	// non-finite issue and a spurious relationship failure.
	if (!(SteerSaturation > SteerDeadZone))
	{
		AddFailure(Result, TEXT("SteerSaturation"), FString::Printf(
			TEXT("%s: SteerSaturation (%f) must be greater than SteerDeadZone (%f); the usable steering band is empty"),
			*ProfileName, SteerSaturation, SteerDeadZone));

		if (bCorrect)
		{
			// Restore a usable band rather than leaving a divide-by-zero armed. 1.0
			// is the only value guaranteed to exceed a legal dead zone (max 0.9).
			SteerSaturation = 1.0f;
		}
	}

	if (!(PedalSaturation > PedalDeadZone))
	{
		AddFailure(Result, TEXT("PedalSaturation"), FString::Printf(
			TEXT("%s: PedalSaturation (%f) must be greater than PedalDeadZone (%f); the usable pedal band is empty"),
			*ProfileName, PedalSaturation, PedalDeadZone));

		if (bCorrect)
		{
			PedalSaturation = 1.0f;
		}
	}

	return Result;
}

RacingSim::Validation::FRacingValidationResult FVehicleInputProfile::ValidateReadOnly(const FString& ProfileName) const
{
	FVehicleInputProfile Copy = *this;
	return Copy.Validate(ProfileName, /* bCorrect */ false);
}

FVehicleInputProfile FVehicleInputProfile::MakeKeyboardDefault()
{
	FVehicleInputProfile Profile;

	// A key is 0 or 1, so there is nothing for a dead zone or a saturation to do --
	// setting either would only be able to break it.
	Profile.SteerDeadZone = 0.0f;
	Profile.SteerSaturation = 1.0f;
	Profile.PedalDeadZone = 0.0f;
	Profile.PedalSaturation = 1.0f;

	// Gamma is likewise inert on a step function: 0^g == 0 and 1^g == 1 for every
	// legal gamma. Left at 1.0 to say so, rather than at a value that implies it does
	// something.
	Profile.SteerResponseGamma = 1.0f;
	Profile.PedalResponseGamma = 1.0f;

	// The rate limits are the entire reason a keyboard is driveable. These are
	// starting values chosen for stability, not measured against a car that does not
	// exist yet: 0.25 s to full throttle, 0.15 s to full brake, 0.4 s to full lock,
	// and 0.2 s back to centre. VEH-003 owns the real numbers once there is a car to
	// feel them in; they are recorded here as an envelope, not as a spec.
	Profile.ThrottleRiseRate = 4.0f;
	Profile.ThrottleFallRate = 8.0f;   // release faster than apply
	Profile.BrakeRiseRate = 6.7f;
	Profile.BrakeFallRate = 10.0f;
	Profile.SteerRate = 2.5f;
	Profile.SteerCentringRate = 5.0f;  // self-centring is the half that saves a car

	Profile.ResetHoldSeconds = 0.5f;

	// Both pedals are digital and both can be held at once with no proprioceptive
	// feedback that they are. Brake wins.
	Profile.PedalConflictPolicy = EVehiclePedalConflictPolicy::BrakeOverrides;

	return Profile;
}

FVehicleInputProfile FVehicleInputProfile::MakeGamepadDefault()
{
	FVehicleInputProfile Profile;

	// Small stick dead zone for resting noise; triggers rest cleanly at 0 on every
	// pad this project targets, so the pedal dead zone stays smaller.
	Profile.SteerDeadZone = 0.08f;
	Profile.SteerSaturation = 0.95f;   // reach full lock slightly before the physical stop
	Profile.PedalDeadZone = 0.02f;
	Profile.PedalSaturation = 1.0f;

	// Above 1: finer control near centre, which is what a 2 cm stick controlling a
	// full steering lock needs. The pedals stay linear -- a trigger already has
	// enough travel to modulate.
	Profile.SteerResponseGamma = 1.8f;
	Profile.PedalResponseGamma = 1.0f;

	// Every rate 0 == instant. A trigger IS the ramp; limiting it again would only
	// add lag between the driver's finger and the car. This is the field that makes
	// one profile struct serve both device classes.
	Profile.ThrottleRiseRate = 0.0f;
	Profile.ThrottleFallRate = 0.0f;
	Profile.BrakeRiseRate = 0.0f;
	Profile.BrakeFallRate = 0.0f;
	Profile.SteerRate = 0.0f;
	Profile.SteerCentringRate = 0.0f;

	Profile.ResetHoldSeconds = 0.5f;

	// Independent: left-foot braking with two analog triggers is a legitimate and
	// common technique on a pad, and suppressing it would be taking a control away
	// from a driver who meant it. The keyboard's reason for BrakeOverrides -- no
	// feedback that both are held -- does not apply to a trigger under a finger.
	Profile.PedalConflictPolicy = EVehiclePedalConflictPolicy::Independent;

	return Profile;
}

TConstArrayView<RacingSim::Validation::FRacingPropertyRange> UVehicleInputConfigDataAsset::StaticRanges()
{
	// Flat numeric properties on this class only; the profiles validate themselves.
	// MaxDeltaSeconds declares a replacement because its minimum is not its safe
	// end -- 0.001 s would clamp every ordinary frame and reduce a rate-limited axis
	// to a crawl, so a broken value returns to the default rather than to the bound.
	static const FRacingPropertyRange Ranges[] =
	{
		FRacingPropertyRange::Between(TEXT("MaxDeltaSeconds"), 0.001, 1.0).WithReplacement(0.1),

		// VEH-004. Declares a replacement for the OPPOSITE reason to MaxDeltaSeconds:
		// its minimum (0.0) means "guard disabled", so clamping a broken value to the
		// bound would silently DISARM the stuck-input protection -- the same shape of
		// hazard URacingSimSettings::TelemetryStaleAfterSeconds forced
		// FRacingPropertyRange::ReplacementValue into existence for. A corrupt value
		// therefore returns to the authored 1.0 s default, guard armed.
		//
		// This is intentionally NOT the same policy the processor applies. The processor
		// receives a value from any caller and fails toward "do nothing"; this asset is
		// authored content whose author demonstrably wanted the guard, and restoring
		// their intent is the safe correction here. Both directions are deliberate and
		// both are tested.
		FRacingPropertyRange::Between(TEXT("InputStaleAfterSeconds"), 0.0, 60.0).WithReplacement(1.0),

		FRacingPropertyRange::Between(TEXT("FullAuthoritySpeedKph"), 0.0, 1000.0),
		FRacingPropertyRange::Between(TEXT("MinAuthoritySpeedKph"), 0.0, 1000.0).WithReplacement(250.0),
		FRacingPropertyRange::Between(TEXT("MinSteerScale"), 0.05, 1.0).WithReplacement(1.0)
	};

	return MakeArrayView(Ranges);
}

void UVehicleInputConfigDataAsset::GetRequiredActions(TArray<EVehicleInputAction>& OutActions) const
{
	OutActions.Reset();

	// Always required. Handbrake is on the list because a car that cannot be held on
	// a hill start (Docs/02-VehiclePhysics.md validation manoeuvre 8) cannot complete
	// its own acceptance tests, and Reset is on it because a stuck car with no reset
	// ends the session.
	OutActions.Add(EVehicleInputAction::Throttle);
	OutActions.Add(EVehicleInputAction::Brake);
	OutActions.Add(EVehicleInputAction::Steer);
	OutActions.Add(EVehicleInputAction::Handbrake);
	OutActions.Add(EVehicleInputAction::Reset);

	if (TransmissionMode == ETransmissionInputMode::Manual)
	{
		// Conditional, and this is the point of having the enum here at all: a manual
		// car whose shift slots are unbound is stuck in first gear, and nothing about
		// the asset would say so.
		OutActions.Add(EVehicleInputAction::ShiftUp);
		OutActions.Add(EVehicleInputAction::ShiftDown);
	}

	// Clutch is deliberately never required. A manual transmission with an automatic
	// clutch is a normal configuration (it is what a paddle-shift car is), so
	// demanding a clutch binding would reject a legitimate setup.
}

const FVehicleInputProfile* UVehicleInputConfigDataAsset::FindProfile(const ERacingInputDeviceType DeviceType) const
{
	// No fallback to DefaultDeviceType on purpose. A silent substitution would apply
	// keyboard ramp rates to a gamepad and present as "the pad feels laggy", with
	// nothing in any log to say why. The caller gets null and decides.
	return Profiles.Find(DeviceType);
}

float UVehicleInputConfigDataAsset::GetSteerScaleForSpeedCms(const double SpeedCms) const
{
	if (SteerSpeedScaleMode == ESteerSpeedScaleMode::Off)
	{
		return 1.0f;
	}

	// Non-finite or negative speed -> treat as stationary. This function sits on the
	// steering path, so returning a NaN scale would put a NaN into the command and
	// make the input layer the source of the very defect VEH-004 exists to detect.
	// Reversing is genuinely negative in the project's convention, and a reversing
	// car wants full steering authority, so the absolute value is not what is wanted
	// here -- 0 is.
	const double SafeSpeedCms = (FMath::IsFinite(SpeedCms) && SpeedCms > 0.0) ? SpeedCms : 0.0;

	// THE unit conversion in this layer. cm/s is the project storage unit
	// (Core/RacingSimUnits.h); the thresholds and the curve domain are km/h. Never
	// write 0.036 inline -- RacingSim.Core.Units pins this constant and
	// RacingSim.Vehicle.InputUnits pins that this call site uses it.
	const double SpeedKph = RacingSim::Units::CmsToKilometresPerHour(SafeSpeedCms);

	if (SteerSpeedScaleMode == ESteerSpeedScaleMode::Curve)
	{
		const FRichCurve* Curve = SteerScaleBySpeedKphCurve.GetRichCurveConst();
		if (Curve != nullptr && Curve->GetNumKeys() >= 2)
		{
			// Clamped to the validated value range rather than trusted: Validate()
			// rejects a bad curve, but a curve can be edited after validation and
			// this is not the place to discover it. An out-of-range multiplier here
			// would amplify the driver's own steering input.
			return FMath::Clamp(Curve->Eval(static_cast<float>(SpeedKph), 1.0f), 0.05f, 1.0f);
		}

		// Curve mode with no usable curve. Validate() reports this as a failure; if
		// it is reached anyway, full authority is the answer that cannot spin a car.
		return 1.0f;
	}

	// Linear. Guarded against an inverted or degenerate threshold pair, which
	// Validate() also rejects but which must not divide by zero if it slips through.
	const double Span = static_cast<double>(MinAuthoritySpeedKph) - static_cast<double>(FullAuthoritySpeedKph);
	if (!(Span > 0.0))
	{
		return 1.0f;
	}

	const double Alpha = FMath::Clamp((SpeedKph - static_cast<double>(FullAuthoritySpeedKph)) / Span, 0.0, 1.0);
	const double Scale = FMath::Lerp(1.0, static_cast<double>(MinSteerScale), Alpha);

	return FMath::Clamp(static_cast<float>(Scale), 0.05f, 1.0f);
}

RacingSim::Validation::FRacingValidationResult UVehicleInputConfigDataAsset::Validate(const bool bCorrect)
{
	FRacingValidationResult Result;

	// -- Flat numerics, through the reflective CORE-003 pass ------------------
	//
	// EnforceRanges always corrects; when the caller asked for a report only, run it
	// against a transient duplicate so this asset is not mutated. The issues it
	// returns are identical either way, which is what makes the report honest.
	if (bCorrect)
	{
		Result.Issues.Append(EnforceRanges(this, StaticRanges()).Issues);
	}
	else
	{
		UVehicleInputConfigDataAsset* Probe = DuplicateObject<UVehicleInputConfigDataAsset>(this, GetTransientPackage());
		if (Probe != nullptr)
		{
			Result.Issues.Append(EnforceRanges(Probe, StaticRanges()).Issues);
		}
		else
		{
			AddFailure(Result, TEXT("MaxDeltaSeconds"),
				TEXT("Could not duplicate the asset to run a read-only range pass"));
		}
	}

	// -- Steering threshold relationship -------------------------------------
	if (SteerSpeedScaleMode == ESteerSpeedScaleMode::Linear
		&& !(MinAuthoritySpeedKph > FullAuthoritySpeedKph))
	{
		AddFailure(Result, TEXT("MinAuthoritySpeedKph"), FString::Printf(
			TEXT("MinAuthoritySpeedKph (%f km/h) must exceed FullAuthoritySpeedKph (%f km/h) under Linear speed scaling"),
			MinAuthoritySpeedKph, FullAuthoritySpeedKph));
	}

	// -- The curve, when it is the selected mode ------------------------------
	if (SteerSpeedScaleMode == ESteerSpeedScaleMode::Curve)
	{
		const FRichCurve* Curve = SteerScaleBySpeedKphCurve.GetRichCurveConst();

		if (Curve == nullptr || Curve->GetNumKeys() < 2)
		{
			// Deliberately a failure and NOT a fall back to Off. A silent fallback is
			// the difference between a car that is twitchy at 250 km/h and one that
			// spins on the straight, and an author reading the asset would see
			// "Curve" selected and believe it.
			AddFailure(Result, TEXT("SteerScaleBySpeedKphCurve"), FString::Printf(
				TEXT("SteerSpeedScaleMode is Curve but the curve has %d keys; at least 2 are required"),
				Curve != nullptr ? Curve->GetNumKeys() : 0));
		}
		else
		{
			// Iterating Keys directly rather than through GetKeyHandleIterator: the
			// handle iterator's const GetKey overload returns by value, and the
			// array is the same data with no per-key map lookup.
			for (const FRichCurveKey& Key : Curve->Keys)
			{

				if (!FMath::IsFinite(Key.Time) || !FMath::IsFinite(Key.Value))
				{
					AddFailure(Result, TEXT("SteerScaleBySpeedKphCurve"),
						TEXT("SteerScaleBySpeedKphCurve has a non-finite key time or value"));
					break;
				}

				if (Key.Time < 0.0f)
				{
					// The domain is km/h road speed. A negative time is either an
					// author error or an attempt to encode reversing, which this
					// curve does not model -- GetSteerScaleForSpeedCms floors at 0.
					AddFailure(Result, TEXT("SteerScaleBySpeedKphCurve"), FString::Printf(
						TEXT("SteerScaleBySpeedKphCurve has a key at %f km/h; the domain is non-negative road speed"),
						Key.Time));
					break;
				}

				if (Key.Value < 0.05f || Key.Value > 1.0f)
				{
					AddFailure(Result, TEXT("SteerScaleBySpeedKphCurve"), FString::Printf(
						TEXT("SteerScaleBySpeedKphCurve key at %f km/h has value %f; steering scale must stay within [0.05, 1.0]"),
						Key.Time, Key.Value));
					break;
				}
			}
		}
	}

	// -- Bindings -------------------------------------------------------------
	//
	// Never corrected. There is no safe substitute for a missing UInputAction, and
	// inventing one would produce an asset that validates clean and cannot drive --
	// strictly worse than the failure it replaced, because it comes with a green
	// report. Same reasoning as CORE-003's note on metadata-driven validation.
	TArray<EVehicleInputAction> Required;
	GetRequiredActions(Required);

	for (const EVehicleInputAction Action : Required)
	{
		const TSoftObjectPtr<UInputAction>* Binding = ActionBindings.Find(Action);

		if (Binding == nullptr)
		{
			AddFailure(Result, TEXT("ActionBindings"), FString::Printf(
				TEXT("Required input action slot '%s' has no binding"),
				*UEnum::GetValueAsString(Action)));
		}
		else if (Binding->IsNull())
		{
			// A present-but-null entry is a different and more dangerous failure than
			// an absent one: it looks bound in the Details panel. Reported separately
			// so the message tells the author which of the two they are looking at.
			AddFailure(Result, TEXT("ActionBindings"), FString::Printf(
				TEXT("Required input action slot '%s' is bound to a null asset reference"),
				*UEnum::GetValueAsString(Action)));
		}
	}

	// -- Profiles, and the mapping context each one needs ---------------------
	//
	// Keyboard and Gamepad are both required because Docs/00-ExecutivePlan.md names
	// both as shipping input methods. A build that supports one is a scope change,
	// not a tuning choice, and it should have to fail a test to happen.
	const ERacingInputDeviceType RequiredDevices[] =
	{
		ERacingInputDeviceType::Keyboard,
		ERacingInputDeviceType::Gamepad
	};

	for (const ERacingInputDeviceType Device : RequiredDevices)
	{
		if (!Profiles.Contains(Device))
		{
			AddFailure(Result, TEXT("Profiles"), FString::Printf(
				TEXT("No input profile for required device '%s'"),
				*UEnum::GetValueAsString(Device)));
		}
	}

	for (TPair<ERacingInputDeviceType, FVehicleInputProfile>& Pair : Profiles)
	{
		const FString ProfileName = UEnum::GetValueAsString(Pair.Key);

		if (bCorrect)
		{
			Result.Issues.Append(Pair.Value.Validate(ProfileName, /* bCorrect */ true).Issues);
		}
		else
		{
			Result.Issues.Append(Pair.Value.ValidateReadOnly(ProfileName).Issues);
		}

		// Every device with a profile must also have a mapping context, or its
		// actions are shaped but never triggered -- a car that is configured and does
		// not respond, which reads as a physics fault rather than a binding one.
		const TSoftObjectPtr<UInputMappingContext>* Context = MappingContexts.Find(Pair.Key);

		if (Context == nullptr || Context->IsNull())
		{
			AddFailure(Result, TEXT("MappingContexts"), FString::Printf(
				TEXT("Device '%s' has an input profile but no usable UInputMappingContext"),
				*ProfileName));
		}
	}

	// The default device must be drivable from the moment the pawn is possessed,
	// before any input has revealed what the player is actually holding.
	if (!Profiles.Contains(DefaultDeviceType))
	{
		AddFailure(Result, TEXT("DefaultDeviceType"), FString::Printf(
			TEXT("DefaultDeviceType is '%s', which has no input profile"),
			*UEnum::GetValueAsString(DefaultDeviceType)));
	}

	return Result;
}
