// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingSimValidation.h"
#include "Misc/AutomationTest.h"
#include "Vehicle/PrototypeVehicleWheel.h"
#include "Vehicle/VehicleTuneDataAsset.h"

// Included explicitly rather than relied on transitively: this file names both directly.
#include <initializer_list>
#include <limits>

using namespace RacingSim::Validation;

/**
 * VEH-003: the prototype tune DataAsset, its validation, and its cross-check against
 * the wheel class default objects.
 *
 * ---------------------------------------------------------------------------
 * Why no ARacingVehiclePawn appears in this file
 * ---------------------------------------------------------------------------
 *
 * Same constraint VEH-002's VehicleChassisSpec.cpp records at length and
 * Docs/Environment.md derives from engine source: FEngineLoop::PreInit runs every
 * SmokeFilter test before UEngine::Init has registered the typed-element types, so a
 * SmokeFilter test that constructs any non-template Actor or UActorComponent kills the
 * process outright and produces NO index.json -- the gate then reports nothing at all
 * rather than a failure.
 *
 * Every fixture here is either a UDataAsset (no components) or a UChaosVehicleWheel CDO
 * reached through TSubclassOf::GetDefaultObject(), which was constructed once at module
 * load and does not re-run PostInitProperties. Both are safe, and both are the exact
 * mechanisms VEH-002 already proved safe at this gate.
 *
 * ARacingVehiclePawn::ApplyTuneAsset() is therefore NOT covered here. What it does is
 * assignment plus one enumerator-for-enumerator switch; what it cannot be tested
 * against without a spawnable world is that Chaos consumed the values. That is the same
 * gap VEH-002 recorded for ApplyChassisAsset, it is unchanged in size by this ticket,
 * and it is left named rather than quietly dropped.
 */

namespace
{
	/** A default tune: every value as constructed, which is the state an author starts from. */
	UVehicleTuneDataAsset* MakeDefaultTune()
	{
		return NewObject<UVehicleTuneDataAsset>(GetTransientPackage());
	}

	/**
	 * True when the result names an issue against this property.
	 *
	 * Named `HasTuneIssueFor`, not the shorter `HasIssueFor` this ticket originally used
	 * -- fixed on code review (VEH-003 MEDIUM-1). VehicleInputConfigSpec.cpp already
	 * declares an identically-signatured `HasIssueFor` in its own file-anonymous
	 * namespace, in the same RacingSimTests module. Both compile fine individually
	 * (internal linkage), but a Unity Build concatenates .cpp files into one translation
	 * unit and two same-named definitions become a genuine redefinition (C2084) --
	 * exactly the class of bug this same ticket already fixed once, for
	 * AllFinite/AllTuneValuesFinite, in the RUNTIME module. This module was not yet at
	 * the file count where UBT switches it to Unity Build, so the collision was latent
	 * rather than a build failure -- see Docs/Tickets.md's VEH-003 section for the
	 * measurement.
	 */
	bool HasTuneIssueFor(const FRacingValidationResult& Result, const FName PropertyName)
	{
		return Result.Issues.ContainsByPredicate(
			[PropertyName](const FRacingValidationIssue& Issue) { return Issue.PropertyName == PropertyName; });
	}

	/** Replace a curve's keys wholesale, so a test can author a deliberately broken one. */
	void SetCurveKeys(FRuntimeFloatCurve& Curve, const std::initializer_list<TPair<float, float>> Keys)
	{
		FRichCurve* Rich = Curve.GetRichCurve();
		check(Rich != nullptr);
		Rich->Reset();
		for (const TPair<float, float>& Key : Keys)
		{
			Rich->AddKey(Key.Key, Key.Value);
		}
	}
}

// ---------------------------------------------------------------------------
// Defaults, derived accessors and the content version
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleTuneDefaultsTest,
	"RacingSim.Vehicle.TuneDefaults",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleTuneDefaultsTest::RunTest(const FString& Parameters)
{
	UVehicleTuneDataAsset* Tune = MakeDefaultTune();

	// The constructor-authored curve. A FRuntimeFloatCurve has no keys unless something
	// adds them, and this asset's own validation requires two -- so a freshly created
	// tune failing its own validation is a real, easy regression.
	const FRichCurve* TorqueCurve = Tune->NormalisedTorqueCurve.GetRichCurveConst();
	TestNotNull(TEXT("The default torque curve exists"), TorqueCurve);
	TestTrue(TEXT("The default torque curve has at least two keys"),
		TorqueCurve != nullptr && TorqueCurve->GetNumKeys() >= 2);

	TestEqual(TEXT("The default torque curve peaks at 1.0 (normalised, as authored)"),
		Tune->GetPeakNormalisedTorque(), 1.0f);
	// Deliberately NOT Tune->MaxTorqueNm * Tune->GetPeakNormalisedTorque() -- that was
	// this ticket's original, code-review-caught defect (VEH-003 HIGH-1). Chaos
	// re-normalises the curve to ITS OWN peak before scaling by MaxTorqueNm, so the
	// delivered peak is always exactly MaxTorqueNm, independent of the authored curve's
	// peak. See RacingSim.Vehicle.TunePeakTorqueIndependentOfCurvePeak below for the
	// case that actually falsifies the old, wrong formula.
	TestEqual(TEXT("Peak torque equals MaxTorqueNm (Chaos re-normalises the curve to its own peak)"),
		Tune->GetPeakTorqueNm(), Tune->MaxTorqueNm);

	// The steer curve is authored even though the default authority ignores it: an
	// author who flips the enum must not find an empty curve they never touched.
	const FRichCurve* SteerCurve = Tune->SteerScaleBySpeedMphCurve.GetRichCurveConst();
	TestTrue(TEXT("The default mph steer curve has at least two keys"),
		SteerCurve != nullptr && SteerCurve->GetNumKeys() >= 2);

	// Overall ratio: 1-based, as a driver counts gears, deliberately not the array index.
	TestEqual(TEXT("Gear 1's overall ratio is first gear times the final drive"),
		Tune->GetOverallRatioForGear(1), Tune->ForwardGearRatios[0] * Tune->FinalDriveRatio);
	TestEqual(TEXT("Gear 0 is out of range and returns 0 rather than asserting"),
		Tune->GetOverallRatioForGear(0), 0.0f);
	TestEqual(TEXT("A gear past the top is out of range and returns 0"),
		Tune->GetOverallRatioForGear(Tune->ForwardGearRatios.Num() + 1), 0.0f);

	// Overall ratios must fall as gears rise -- the driver-visible consequence of the
	// strictly-decreasing rule Validate() enforces on the raw ratios.
	for (int32 Gear = 2; Gear <= Tune->ForwardGearRatios.Num(); ++Gear)
	{
		TestTrue(FString::Printf(TEXT("Overall ratio in gear %d is shorter than in gear %d"), Gear, Gear - 1),
			Tune->GetOverallRatioForGear(Gear) < Tune->GetOverallRatioForGear(Gear - 1));
	}

	// -- Content version: the CORE-002 hole this ticket closes ------------------
	const FRacingContentVersion Version = Tune->GetContentVersion();
	TestTrue(TEXT("A default tune's content version is populated"), Version.IsPopulated());
	TestEqual(TEXT("The content version carries the TuneId"), Version.AssetId, Tune->TuneId);

	// A retune must change the hash, or a lap time cannot name the tune it was set on.
	{
		UVehicleTuneDataAsset* Retuned = MakeDefaultTune();
		const uint32 Before = Retuned->ComputeContentHash();
		Retuned->MaxTorqueNm += 1.0f;
		TestNotEqual(TEXT("Changing a scalar changes the content hash"), Retuned->ComputeContentHash(), Before);
	}
	{
		// The curve is the easy thing to forget in a hash. Assert it explicitly.
		UVehicleTuneDataAsset* Retuned = MakeDefaultTune();
		const uint32 Before = Retuned->ComputeContentHash();
		SetCurveKeys(Retuned->NormalisedTorqueCurve, {{1000.0f, 0.5f}, {6000.0f, 1.0f}});
		TestNotEqual(TEXT("Retuning only the torque curve changes the content hash"),
			Retuned->ComputeContentHash(), Before);
	}
	{
		UVehicleTuneDataAsset* A = MakeDefaultTune();
		UVehicleTuneDataAsset* B = MakeDefaultTune();
		TestEqual(TEXT("Two identical tunes hash identically"), A->ComputeContentHash(), B->ComputeContentHash());
	}

	return true;
}

// ---------------------------------------------------------------------------
// Peak torque must not depend on the authored curve's own peak -- Chaos
// re-normalises the curve internally (VEH-003 HIGH-1, code review)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleTunePeakTorqueIndependentOfCurvePeakTest,
	"RacingSim.Vehicle.TunePeakTorqueIndependentOfCurvePeak",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleTunePeakTorqueIndependentOfCurvePeakTest::RunTest(const FString& Parameters)
{
	// A curve peaking well below 1.0 is legal (validation only requires a non-zero
	// peak). This is the case that falsifies the old, incorrect
	// `MaxTorqueNm * GetPeakNormalisedTorque()` formula: that formula would have
	// returned 0.5 * MaxTorqueNm here, but Chaos's FillEngineSetup divides the curve
	// by its own peak before scaling by MaxTorqueNm, so the delivered peak is always
	// exactly MaxTorqueNm regardless of the authored curve's own peak value.
	UVehicleTuneDataAsset* LowPeak = MakeDefaultTune();
	SetCurveKeys(LowPeak->NormalisedTorqueCurve, {{1000.0f, 0.2f}, {4000.0f, 0.5f}, {7000.0f, 0.1f}});

	TestEqual(TEXT("The authored curve peak is 0.5, not 1.0"),
		LowPeak->GetPeakNormalisedTorque(), 0.5f);
	TestEqual(TEXT("Delivered peak torque is still exactly MaxTorqueNm, not MaxTorqueNm * 0.5"),
		LowPeak->GetPeakTorqueNm(), LowPeak->MaxTorqueNm);

	// An unusable curve (peak 0) is the one case where GetPeakTorqueNm() must NOT
	// return MaxTorqueNm -- Chaos's re-normalisation divides by zero and delivers no
	// torque at all in that case.
	UVehicleTuneDataAsset* ZeroPeak = MakeDefaultTune();
	SetCurveKeys(ZeroPeak->NormalisedTorqueCurve, {{1000.0f, 0.0f}, {7000.0f, 0.0f}});
	TestEqual(TEXT("An all-zero curve yields zero delivered peak torque, not MaxTorqueNm"),
		ZeroPeak->GetPeakTorqueNm(), 0.0f);

	return true;
}

// ---------------------------------------------------------------------------
// Ranges: the table must mirror the metadata in BOTH directions
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleTuneRangesTest,
	"RacingSim.Vehicle.TuneRanges",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleTuneRangesTest::RunTest(const FString& Parameters)
{
	// CORE-003's lesson, unchanged: WITH_METADATA is WITH_EDITORONLY_DATA, so
	// ClampMin/ClampMax is 0 in a packaged Game build and cannot be the enforcement
	// path. StaticRanges() is. This assertion is the only thing that keeps the
	// hand-written table from rotting when a clamped property is added -- and the
	// "added but not tabled" direction is the one that actually happens.
	const TConstArrayView<FRacingPropertyRange> Ranges = UVehicleTuneDataAsset::StaticRanges();
	TestTrue(TEXT("The tune range table is not empty"), Ranges.Num() > 0);

	const FRacingValidationResult Match =
		VerifyRangesMatchMetadata(UVehicleTuneDataAsset::StaticClass(), Ranges);
	for (const FRacingValidationIssue& Issue : Match.Issues)
	{
		AddError(FString::Printf(TEXT("StaticRanges disagrees with UPROPERTY metadata on '%s': %s"),
			*Issue.PropertyName.ToString(), *Issue.Message));
	}
	TestTrue(TEXT("StaticRanges and the UPROPERTY clamp metadata agree in both directions"),
		Match.IsClean());

	// -- Out-of-range values are reported, and corrected only when asked --------
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->MaxTorqueNm = 999999.0f;     // above ClampMax
		Tune->IdleRpm = -50.0f;            // below ClampMin
		Tune->FrontDampingRatio = 0.0f;    // below ClampMin: an undamped spring

		const FRacingValidationResult Report = Tune->ValidateReadOnly();
		TestFalse(TEXT("An out-of-range tune does not validate clean"), Report.IsClean());
		TestTrue(TEXT("MaxTorqueNm above its maximum is reported"), HasTuneIssueFor(Report, TEXT("MaxTorqueNm")));
		TestTrue(TEXT("A negative IdleRpm is reported"), HasTuneIssueFor(Report, TEXT("IdleRpm")));
		TestTrue(TEXT("A zero damping ratio is reported"), HasTuneIssueFor(Report, TEXT("FrontDampingRatio")));

		// ValidateReadOnly must not mutate. A report that silently repaired the asset
		// would make the corrected/uncorrected distinction meaningless.
		TestEqual(TEXT("ValidateReadOnly leaves MaxTorqueNm untouched"), Tune->MaxTorqueNm, 999999.0f);
		TestEqual(TEXT("ValidateReadOnly leaves IdleRpm untouched"), Tune->IdleRpm, -50.0f);

		// Validate(true) is the one that writes.
		Tune->Validate(/*bCorrect*/ true);
		TestTrue(TEXT("Correction brings MaxTorqueNm back within its declared range"),
			Tune->MaxTorqueNm >= 50.0f && Tune->MaxTorqueNm <= 2000.0f);
		TestTrue(TEXT("Correction brings IdleRpm back within its declared range"),
			Tune->IdleRpm >= 200.0f && Tune->IdleRpm <= 5000.0f);
		TestTrue(TEXT("Correction brings FrontDampingRatio back within its declared range"),
			Tune->FrontDampingRatio >= 0.05f && Tune->FrontDampingRatio <= 1.5f);
	}

	// -- Non-finite values are caught, and reported ONCE -----------------------
	//
	// The specific defect AllFinite() exists to prevent: every comparison against a NaN
	// is false, so an unguarded relationship check reports a NaN redline as
	// "ChangeUpRpm exceeds MaxRpm" -- one defect, two contradictory issues, and the
	// second one names a property the author never touched.
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->MaxRpm = FMath::Sqrt(-1.0f);

		const FRacingValidationResult Report = Tune->ValidateReadOnly();
		TestFalse(TEXT("A NaN redline does not validate clean"), Report.IsClean());
		TestTrue(TEXT("A NaN redline is reported against MaxRpm"), HasTuneIssueFor(Report, TEXT("MaxRpm")));
		TestFalse(TEXT("A NaN redline does not also produce a spurious ChangeUpRpm relationship issue"),
			HasTuneIssueFor(Report, TEXT("ChangeUpRpm")));
	}
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->MaxTorqueNm = std::numeric_limits<float>::infinity();

		const FRacingValidationResult Report = Tune->ValidateReadOnly();
		TestFalse(TEXT("An infinite MaxTorqueNm does not validate clean"), Report.IsClean());
		TestTrue(TEXT("An infinite MaxTorqueNm is reported"), HasTuneIssueFor(Report, TEXT("MaxTorqueNm")));

		Tune->Validate(/*bCorrect*/ true);
		TestTrue(TEXT("Correcting an infinity yields a finite MaxTorqueNm"), FMath::IsFinite(Tune->MaxTorqueNm));
	}

	return true;
}

// ---------------------------------------------------------------------------
// Relationships: what no per-field clamp can express
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleTuneRelationshipsTest,
	"RacingSim.Vehicle.TuneRelationships",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleTuneRelationshipsTest::RunTest(const FString& Parameters)
{
	// A clean default tune must report nothing. If this ever fails, every other
	// assertion below is measuring the wrong thing.
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		const FRacingValidationResult Result = Tune->ValidateReadOnly();
		for (const FRacingValidationIssue& Issue : Result.Issues)
		{
			AddError(FString::Printf(TEXT("Default tune reported '%s': %s"),
				*Issue.PropertyName.ToString(), *Issue.Message));
		}
		TestTrue(TEXT("A clean default tune validates with no issues"), Result.IsClean());
	}

	// 1. TuneId None -- FRacingSimVersionStamp::IsPublishable() would refuse the stamp.
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->TuneId = NAME_None;
		const FRacingValidationResult Result = Tune->ValidateReadOnly();
		TestTrue(TEXT("An unnamed tune is reported"), HasTuneIssueFor(Result, TEXT("TuneId")));
		TestFalse(TEXT("An unnamed tune's content version is not publishable"),
			Tune->GetContentVersion().IsPopulated());
	}

	// 2. Redline at or below idle.
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->IdleRpm = 4000.0f;
		Tune->MaxRpm = 3000.0f;
		Tune->ChangeUpRpm = 2900.0f;
		Tune->ChangeDownRpm = 2000.0f;
		TestTrue(TEXT("A redline below idle is reported"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("MaxRpm")));
	}

	// 3. Shift points that hunt: up at or below down.
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->ChangeUpRpm = 3000.0f;
		Tune->ChangeDownRpm = 3000.0f;
		TestTrue(TEXT("Equal shift points are reported -- the gearbox would hunt"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("ChangeUpRpm")));
	}

	// 4. Upshift point past the redline: the gearbox never upshifts.
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->ChangeUpRpm = Tune->MaxRpm + 500.0f;
		TestTrue(TEXT("An upshift point above the redline is reported"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("ChangeUpRpm")));
	}

	// 5. Downshift point below idle: the gearbox never downshifts.
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->IdleRpm = 2000.0f;
		Tune->ChangeDownRpm = 1000.0f;
		TestTrue(TEXT("A downshift point below idle is reported"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("ChangeDownRpm")));
	}

	// 6. Gear ratios that are not strictly decreasing. Individually plausible,
	//    collectively impossible: the car accelerates HARDER after an upshift.
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->ForwardGearRatios = {3.35f, 2.18f, 2.40f, 1.19f};
		TestTrue(TEXT("A non-decreasing forward gear set is reported"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("ForwardGearRatios")));
	}
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->ForwardGearRatios.Empty();
		TestTrue(TEXT("An empty forward gear set is reported"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("ForwardGearRatios")));
	}
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->ForwardGearRatios = {3.35f, FMath::Sqrt(-1.0f), 1.57f};
		TestTrue(TEXT("A non-finite gear ratio is reported"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("ForwardGearRatios")));
	}

	// 7. Reverse ratios are POSITIVE magnitudes -- Chaos applies the sign. A negative
	//    entry is an author encoding the direction twice.
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->ReverseGearRatios = {-2.9f};
		TestTrue(TEXT("A negative reverse ratio is reported"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("ReverseGearRatios")));
	}
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->ReverseGearRatios.Empty();
		TestTrue(TEXT("An empty reverse gear set is reported"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("ReverseGearRatios")));
	}

	// 8. Rear brake bias: locks the rear axle first and spins the car under braking.
	//    Reported, never corrected -- a deliberate rear bias is a legitimate tune.
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->RearBrakeTorqueNm = Tune->FrontBrakeTorqueNm + 100.0f;

		const FRacingValidationResult Report = Tune->ValidateReadOnly();
		TestTrue(TEXT("A rear brake bias is reported"), HasTuneIssueFor(Report, TEXT("RearBrakeTorqueNm")));

		const float BeforeRear = Tune->RearBrakeTorqueNm;
		Tune->Validate(/*bCorrect*/ true);
		TestEqual(TEXT("A rear brake bias is NOT silently corrected -- there is no single safe value"),
			Tune->RearBrakeTorqueNm, BeforeRear);
	}

	// 9. A steering ratio the selected model ignores. Chaos reads AngleRatio only under
	//    ESteeringType::AngleRatio, so an author tuning it under Ackermann is tuning
	//    nothing at all and has no way to see that.
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->SteeringModel = EVehicleSteeringModel::Ackermann;
		Tune->OuterInnerAngleRatio = 0.45f;
		TestTrue(TEXT("A non-default angle ratio under a model that ignores it is reported"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("OuterInnerAngleRatio")));
	}
	{
		// The same model with the ratio left alone is clean: this reports an author's
		// wasted edit, not the model choice itself.
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->SteeringModel = EVehicleSteeringModel::Ackermann;
		TestFalse(TEXT("Ackermann with an untouched ratio is not reported"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("OuterInnerAngleRatio")));
	}

	// 10. The mph steer curve is validated only when it is the selected authority.
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		SetCurveKeys(Tune->SteerScaleBySpeedMphCurve, {{0.0f, 1.0f}});   // one key
		TestFalse(TEXT("A broken mph curve is ignored while the input layer holds authority"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("SteerScaleBySpeedMphCurve")));

		Tune->SteerSpeedAuthority = EVehicleSteerSpeedAuthority::ChaosCurve;
		TestTrue(TEXT("The same curve is reported once Chaos holds authority"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("SteerScaleBySpeedMphCurve")));
	}
	{
		// A curve value outside [0.05, 1] either locks the wheel or multiplies the
		// driver's input -- neither is discoverable by looking at the car.
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->SteerSpeedAuthority = EVehicleSteerSpeedAuthority::ChaosCurve;
		SetCurveKeys(Tune->SteerScaleBySpeedMphCurve, {{0.0f, 1.0f}, {100.0f, 3.7f}});
		TestTrue(TEXT("A steer scale above 1 is reported"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("SteerScaleBySpeedMphCurve")));
	}
	{
		// Negative domain: mph is a speed, not a signed axis.
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->SteerSpeedAuthority = EVehicleSteerSpeedAuthority::ChaosCurve;
		SetCurveKeys(Tune->SteerScaleBySpeedMphCurve, {{-10.0f, 1.0f}, {100.0f, 0.5f}});
		TestTrue(TEXT("A negative mph key is reported"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("SteerScaleBySpeedMphCurve")));
	}

	// 11. Torque curve failures. The all-zero case is the one no per-key check sees:
	//     every key legally in [0,1], and Chaos multiplies MaxTorque by all of them, so
	//     the car reports 420 Nm and produces none.
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		SetCurveKeys(Tune->NormalisedTorqueCurve, {{1000.0f, 0.0f}, {7000.0f, 0.0f}});
		TestTrue(TEXT("An all-zero torque curve is reported"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("NormalisedTorqueCurve")));
		TestEqual(TEXT("An all-zero torque curve yields zero peak torque"), Tune->GetPeakTorqueNm(), 0.0f);
	}
	{
		// Un-normalised: an author entering Nm into a [0,1] curve multiplies torque by
		// 420 and gets a car that is not merely fast but non-physical.
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		SetCurveKeys(Tune->NormalisedTorqueCurve, {{1000.0f, 250.0f}, {7000.0f, 420.0f}});
		TestTrue(TEXT("A torque curve authored in Nm rather than normalised is reported"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("NormalisedTorqueCurve")));
	}
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		SetCurveKeys(Tune->NormalisedTorqueCurve, {{5000.0f, 1.0f}});
		TestTrue(TEXT("A single-key torque curve is reported"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("NormalisedTorqueCurve")));
	}
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		SetCurveKeys(Tune->NormalisedTorqueCurve, {{-100.0f, 0.5f}, {6000.0f, 1.0f}});
		TestTrue(TEXT("A negative rpm key is reported"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("NormalisedTorqueCurve")));
	}
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		SetCurveKeys(Tune->NormalisedTorqueCurve, {{1000.0f, FMath::Sqrt(-1.0f)}, {6000.0f, 1.0f}});
		TestTrue(TEXT("A non-finite torque curve key is reported"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("NormalisedTorqueCurve")));
		// Corrected on code review, repair cycle 2 (VEH-003 HIGH-1 re-opened): the
		// PREVIOUS version of this test asserted the peak was 1.0 here, reasoning that
		// FRichCurve::GetValueRange's Max-based fold "ignores" the NaN key. That is true
		// only when the NaN key happens not to be the operand FMath::Max compares first
		// -- Max(finite, NaN) returns the finite operand, so key ORDER silently decided
		// whether this curve was treated as usable. GetPeakNormalisedTorque() no longer
		// uses GetValueRange for exactly this reason: it now iterates keys explicitly and
		// reports the curve as wholly unusable (0.0) the moment ANY key is non-finite,
		// independent of order. The contract is genuinely "never propagate a NaN" now,
		// not "usually, unless the NaN key comes first".
		TestEqual(TEXT("A curve with a non-finite key reports zero peak, not the other key's value"),
			Tune->GetPeakNormalisedTorque(), 0.0f);
		TestEqual(TEXT("GetPeakTorqueNm is zero (unusable), not MaxTorqueNm, for a curve with a non-finite key"),
			Tune->GetPeakTorqueNm(), 0.0f);
	}
	{
		// The order-dependence check itself: the SAME two keys, NaN placed SECOND
		// instead of first. Both orderings must report unusable -- proving the fix is
		// not merely "moved the bug to depend on the other ordering".
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		SetCurveKeys(Tune->NormalisedTorqueCurve, {{1000.0f, 1.0f}, {6000.0f, FMath::Sqrt(-1.0f)}});
		TestEqual(TEXT("A non-finite key reports zero peak regardless of its position in the curve"),
			Tune->GetPeakNormalisedTorque(), 0.0f);
	}

	// 12. Effectively no suspension travel: every kerb strike goes straight into the
	//     chassis, which VEH-004 would later see as an instability with no obvious cause.
	//     Reached through ValidateReadOnly() (the individual clamps permit 0.5 cm each,
	//     and it is only their SUM that is the defect -- comment corrected, code review
	//     VEH-003 LOW-2, this was never a Validate(true) call).
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->SuspensionMaxRaiseCm = 0.5f;
		Tune->SuspensionMaxDropCm = 0.5f;
		TestTrue(TEXT("Effectively zero total suspension travel is reported"),
			HasTuneIssueFor(Tune->ValidateReadOnly(), TEXT("SuspensionMaxDropCm")));
	}

	return true;
}

// ---------------------------------------------------------------------------
// The cross-check: Chaos reads the WHEEL CLASS, not the asset
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleTuneWheelClassMatchTest,
	"RacingSim.Vehicle.TuneWheelClassMatch",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleTuneWheelClassMatchTest::RunTest(const FString& Parameters)
{
	const TSubclassOf<UChaosVehicleWheel> FrontClass = UPrototypeFrontWheel::StaticClass();
	const TSubclassOf<UChaosVehicleWheel> RearClass = UPrototypeRearWheel::StaticClass();

	// The property this ticket's single-definition change buys: the wheel constructors
	// and the asset's property defaults both read PrototypeTuneDefaults, so a freshly
	// constructed pair CANNOT start out disagreeing. VEH-002's radius/width pattern
	// duplicated its literals and relied on this check to notice drift; here there is
	// nothing to drift.
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		const FRacingValidationResult Result =
			RacingSim::Vehicle::ValidateTuneAgainstWheelClasses(Tune, FrontClass, RearClass);
		for (const FRacingValidationIssue& Issue : Result.Issues)
		{
			AddError(FString::Printf(TEXT("Default tune disagrees with the wheel classes: %s"), *Issue.Message));
		}
		TestTrue(TEXT("A default tune agrees with the wheel class default objects"), Result.IsClean());
	}

	// The case the shared constexpr cannot cover, and the reason this function exists:
	// an asset INSTANCE edited after construction. The wheel class cannot follow it,
	// because Chaos reads WheelSetups[i].WheelClass.GetDefaultObject() at SetupVehicle
	// time -- an instance write lands too late and a CDO write is process-global.
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->FrontSpringRateNPerM += 10.0f;
		TestTrue(TEXT("An edited front spring rate is reported against the wheel class"),
			HasTuneIssueFor(RacingSim::Vehicle::ValidateTuneAgainstWheelClasses(Tune, FrontClass, RearClass),
				TEXT("FrontSpringRateNPerM")));
	}
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->RearBrakeTorqueNm += 250.0f;
		TestTrue(TEXT("An edited rear brake torque is reported against the wheel class"),
			HasTuneIssueFor(RacingSim::Vehicle::ValidateTuneAgainstWheelClasses(Tune, FrontClass, RearClass),
				TEXT("RearBrakeTorqueNm")));
	}
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->HandbrakeTorqueNm += 100.0f;
		TestTrue(TEXT("An edited handbrake torque is reported against the rear wheel class"),
			HasTuneIssueFor(RacingSim::Vehicle::ValidateTuneAgainstWheelClasses(Tune, FrontClass, RearClass),
				TEXT("HandbrakeTorqueNm")));
	}
	{
		// Axle-independent values are checked against BOTH wheels, so a subclass that
		// diverged from the shared base would not be invisible.
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->WheelLoadRatio = 0.9f;
		const FRacingValidationResult Result =
			RacingSim::Vehicle::ValidateTuneAgainstWheelClasses(Tune, FrontClass, RearClass);
		TestTrue(TEXT("An edited wheel load ratio is reported"), HasTuneIssueFor(Result, TEXT("WheelLoadRatio")));
		TestEqual(TEXT("It is reported once per wheel class, not once overall"), Result.Issues.Num(), 2);
	}

	// The validate-never-write contract. If this function ever "helpfully" wrote to the
	// CDO it would silently retune every vehicle in the process.
	{
		UVehicleTuneDataAsset* Tune = MakeDefaultTune();
		Tune->FrontSpringRateNPerM = 500.0f;
		RacingSim::Vehicle::ValidateTuneAgainstWheelClasses(Tune, FrontClass, RearClass);

		TestEqual(TEXT("The cross-check does not write the asset onto the wheel CDO"),
			FrontClass.GetDefaultObject()->SpringRate,
			RacingSim::Vehicle::PrototypeTuneDefaults::FrontSpringRateNPerM);
		TestEqual(TEXT("The cross-check does not repair the asset either"),
			Tune->FrontSpringRateNPerM, 500.0f);
	}

	// Null arguments are reported, never a crash: this runs from BeginPlay on content
	// a designer assembled.
	{
		TestFalse(TEXT("A null tune is reported"),
			RacingSim::Vehicle::ValidateTuneAgainstWheelClasses(nullptr, FrontClass, RearClass).IsClean());
		TestFalse(TEXT("A null front wheel class is reported"),
			RacingSim::Vehicle::ValidateTuneAgainstWheelClasses(MakeDefaultTune(), nullptr, RearClass).IsClean());
		TestFalse(TEXT("A null rear wheel class is reported"),
			RacingSim::Vehicle::ValidateTuneAgainstWheelClasses(MakeDefaultTune(), FrontClass, nullptr).IsClean());
	}

	// The front axle carries no handbrake. That is a topology fact the tune must not be
	// able to contradict by implication.
	TestTrue(TEXT("The front wheel class carries no handbrake torque"),
		FMath::IsNearlyZero(FrontClass.GetDefaultObject()->MaxHandBrakeTorque));
	TestTrue(TEXT("The rear wheel class carries the handbrake torque"),
		FrontClass.GetDefaultObject()->MaxHandBrakeTorque < RearClass.GetDefaultObject()->MaxHandBrakeTorque);

	// The front brake bias, asserted on the objects Chaos actually reads rather than on
	// the asset that merely declares it.
	TestTrue(TEXT("The wheel classes carry a front brake bias"),
		FrontClass.GetDefaultObject()->MaxBrakeTorque > RearClass.GetDefaultObject()->MaxBrakeTorque);

	return true;
}
