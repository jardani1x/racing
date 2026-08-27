// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingSimUnits.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Vehicle/VehicleInputConfig.h"

#include <limits>

using namespace RacingSim::Validation;

/**
 * VEH-001: the input config DataAsset.
 *
 * ---------------------------------------------------------------------------
 * Why NewObject is safe here when it is not safe for a component
 * ---------------------------------------------------------------------------
 *
 * Docs/Environment.md's SmokeFilter restriction is specifically about ACTORS and
 * UActorComponents: UActorComponent::PostInitProperties creates a typed editor
 * element for every non-template component (ActorComponent.cpp:588), and the
 * "Components" element type is not registered until UEngine::Init, which runs after
 * FEngineLoop::PreInit -- where the smoke tests execute.
 *
 * UVehicleInputConfigDataAsset is a UDataAsset. It has no components, so it never
 * reaches that path, and the UObject system itself is fully up during PreInit. That
 * is why these tests can construct real assets at the fast gate while
 * VehicleInputProcessorSpec.cpp deliberately constructs no UObject at all.
 *
 * If this assumption is ever wrong the symptom is unmistakable and is documented in
 * Docs/Environment.md: the process dies on `Assertion failed: RegisteredElementType`
 * and NO index.json is written, so the gate reports nothing rather than a failure.
 */

namespace
{
	/**
	 * An asset that validates clean, except that it has no bindings.
	 *
	 * Bindings are deliberately left out of the "good" fixture, because the
	 * UInputAction/UInputMappingContext .uassets do not exist yet -- CLAUDE.md
	 * forbids authoring binary assets from a worktree. Every test below therefore
	 * asserts on binding-related issues BY NAME rather than on overall cleanliness.
	 */
	UVehicleInputConfigDataAsset* MakeConfig()
	{
		UVehicleInputConfigDataAsset* Config = NewObject<UVehicleInputConfigDataAsset>(GetTransientPackage());

		Config->Profiles.Add(ERacingInputDeviceType::Keyboard, FVehicleInputProfile::MakeKeyboardDefault());
		Config->Profiles.Add(ERacingInputDeviceType::Gamepad, FVehicleInputProfile::MakeGamepadDefault());
		Config->DefaultDeviceType = ERacingInputDeviceType::Keyboard;

		return Config;
	}

	/** True when the result contains an issue naming this property. */
	bool HasIssueFor(const FRacingValidationResult& Result, const FName PropertyName)
	{
		for (const FRacingValidationIssue& Issue : Result.Issues)
		{
			if (Issue.PropertyName == PropertyName)
			{
				return true;
			}
		}

		return false;
	}
}

// ---------------------------------------------------------------------------
// Ranges
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleInputConfigRangesTest,
	"RacingSim.Vehicle.InputConfigRanges",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleInputConfigRangesTest::RunTest(const FString& Parameters)
{
	// CORE-003's central lesson applies verbatim here: ClampMin/ClampMax metadata
	// constrains the Details panel and nothing else, and WITH_METADATA is
	// WITH_EDITORONLY_DATA, so a metadata-driven check would enforce nothing in a
	// packaged Game build while passing every test (automation runs in the editor).
	//
	// These assertions therefore go against HAND-WRITTEN known-bad values, not
	// against the metadata. That matters more for this asset than for
	// URacingSimSettings, because the profile fields are nested in a USTRUCT inside a
	// TMap and CORE-003's reflective pass cannot reach them at all -- see the header
	// of VehicleInputConfig.h, and CORE-003 finding MEDIUM-2.

	const double NaNValue = FMath::Sqrt(-1.0);

	// -- A default profile is valid ------------------------------------------
	{
		FVehicleInputProfile Profile;
		TestTrue(TEXT("A default-constructed profile validates clean"), Profile.ValidateReadOnly(TEXT("Default")).IsClean());
	}

	// -- Each field rejects out-of-range and corrects to something usable ----
	{
		FVehicleInputProfile Profile;
		Profile.SteerDeadZone = 5.0f;          // > 0.9
		Profile.PedalDeadZone = -1.0f;         // < 0.0
		Profile.SteerResponseGamma = 99.0f;    // > 5.0
		Profile.ThrottleRiseRate = -3.0f;      // < 0.0
		Profile.ResetHoldSeconds = 0.0f;       // < 0.05

		const FRacingValidationResult Report = Profile.ValidateReadOnly(TEXT("Bad"));

		TestFalse(TEXT("An out-of-range profile does not validate clean"), Report.IsClean());
		TestTrue(TEXT("SteerDeadZone above its maximum is reported"), HasIssueFor(Report, TEXT("SteerDeadZone")));
		TestTrue(TEXT("PedalDeadZone below its minimum is reported"), HasIssueFor(Report, TEXT("PedalDeadZone")));
		TestTrue(TEXT("SteerResponseGamma above its maximum is reported"), HasIssueFor(Report, TEXT("SteerResponseGamma")));
		TestTrue(TEXT("A negative rate is reported"), HasIssueFor(Report, TEXT("ThrottleRiseRate")));
		TestTrue(TEXT("A zero reset hold is reported"), HasIssueFor(Report, TEXT("ResetHoldSeconds")));

		// ValidateReadOnly must not mutate. A "report" that silently fixed the asset
		// would make the two modes indistinguishable and would corrupt a caller that
		// only wanted to look.
		TestNearlyEqual(TEXT("ValidateReadOnly does not mutate SteerDeadZone"), Profile.SteerDeadZone, 5.0f, 0.0f);

		// Correcting mode does write, and writes something inside the range.
		FVehicleInputProfile Corrected = Profile;
		Corrected.Validate(TEXT("Bad"), /* bCorrect */ true);

		TestTrue(TEXT("Correction brings SteerDeadZone into range"), Corrected.SteerDeadZone >= 0.0f && Corrected.SteerDeadZone <= 0.9f);
		TestTrue(TEXT("Correction brings PedalDeadZone into range"), Corrected.PedalDeadZone >= 0.0f && Corrected.PedalDeadZone <= 0.9f);
		TestTrue(TEXT("Correction brings SteerResponseGamma into range"), Corrected.SteerResponseGamma >= 0.2f && Corrected.SteerResponseGamma <= 5.0f);
		TestTrue(TEXT("Correction brings ThrottleRiseRate into range"), Corrected.ThrottleRiseRate >= 0.0f);

		// The reset hold is the one that must not be corrected to its BOUND. Its
		// minimum of 0.05 s is technically legal but is not its safe value; the
		// declared replacement returns it to the 0.5 s default, so a broken config
		// gets a deliberate hold back rather than a near-instant one.
		TestNearlyEqual(TEXT("A broken reset hold returns to the default, not to the bound"), Corrected.ResetHoldSeconds, 0.5f, 1.0e-6f);

		// And the corrected profile must itself validate clean -- otherwise
		// correction is not a fixed point and a caller could loop forever.
		TestTrue(TEXT("A corrected profile validates clean"), Corrected.ValidateReadOnly(TEXT("Corrected")).IsClean());
	}

	// -- Non-finite fields are REPLACED, not clamped -------------------------
	{
		FVehicleInputProfile Profile;
		Profile.SteerSaturation = NaNValue;
		Profile.PedalResponseGamma = std::numeric_limits<float>::infinity();

		FVehicleInputProfile Corrected = Profile;
		const FRacingValidationResult Report = Corrected.Validate(TEXT("NonFinite"), /* bCorrect */ true);

		TestFalse(TEXT("A non-finite profile does not validate clean"), Report.IsClean());

		// Saturation's minimum of 0.1 is NOT its safe end: 0.1 means "one tenth of
		// stick travel is full lock", so clamping a NaN to the minimum would turn a
		// broken value into a violently oversensitive car. The declared replacement
		// of 1.0 -- use the whole axis -- is the value that cannot surprise anyone.
		// This is CORE-003's ReplacementValue rule, and asserting the exact value is
		// what distinguishes it from a plain clamp.
		TestNearlyEqual(TEXT("A NaN saturation is replaced with 1.0, not clamped to 0.1"), Corrected.SteerSaturation, 1.0f, 1.0e-6f);
		TestNearlyEqual(TEXT("An infinite gamma is replaced with 1.0 (linear)"), Corrected.PedalResponseGamma, 1.0f, 1.0e-6f);

		TestTrue(TEXT("The corrected non-finite profile validates clean"), Corrected.ValidateReadOnly(TEXT("Fixed")).IsClean());
	}

	// -- Cross-field relationships no per-field range can express ------------
	//
	// Both of these are reachable from values that are individually legal, which is
	// exactly why a per-field table is not sufficient: dead zone 0.9 and saturation
	// 0.5 each pass their own bound and together leave an empty usable band, where
	// the rescale would divide by a non-positive width.
	{
		FVehicleInputProfile Profile;
		Profile.SteerDeadZone = 0.9f;
		Profile.SteerSaturation = 0.5f;
		Profile.PedalDeadZone = 0.5f;
		Profile.PedalSaturation = 0.5f;   // equal, not merely inverted

		const FRacingValidationResult Report = Profile.ValidateReadOnly(TEXT("Degenerate"));

		TestTrue(TEXT("Steering saturation below the dead zone is reported"), HasIssueFor(Report, TEXT("SteerSaturation")));
		TestTrue(TEXT("Pedal saturation equal to the dead zone is reported"), HasIssueFor(Report, TEXT("PedalSaturation")));

		FVehicleInputProfile Corrected = Profile;
		Corrected.Validate(TEXT("Degenerate"), /* bCorrect */ true);
		TestTrue(TEXT("Correction restores a usable steering band"), Corrected.SteerSaturation > Corrected.SteerDeadZone);
		TestTrue(TEXT("Correction restores a usable pedal band"), Corrected.PedalSaturation > Corrected.PedalDeadZone);
	}

	// -- The asset's own flat scalars, through CORE-003's reflective pass ----
	{
		UVehicleInputConfigDataAsset* Config = MakeConfig();
		TestNotNull(TEXT("The config fixture was constructed"), Config);

		Config->MaxDeltaSeconds = 0.0f;   // below the 0.001 minimum

		const FRacingValidationResult Report = Config->Validate(/* bCorrect */ false);
		TestTrue(TEXT("A zero MaxDeltaSeconds is reported"), HasIssueFor(Report, TEXT("MaxDeltaSeconds")));

		// Read-only mode duplicates the asset to run the pass, so the original must
		// be untouched. If it were not, "just check it" would silently rewrite content.
		TestNearlyEqual(TEXT("A read-only validation does not mutate the asset"), Config->MaxDeltaSeconds, 0.0f, 0.0f);

		Config->Validate(/* bCorrect */ true);

		// Corrected to the DEFAULT, not to the 0.001 bound: 0.001 s would clamp every
		// ordinary frame and reduce every rate-limited axis to a crawl -- a broken
		// value turned into a silently sluggish car.
		TestNearlyEqual(TEXT("A zero MaxDeltaSeconds returns to the default, not to the bound"), Config->MaxDeltaSeconds, 0.1f, 1.0e-6f);
	}

	// -- StaticRanges must cover every flat clamped property -----------------
	//
	// The regression shape CORE-003 M-5 was really about: someone adds a clamped
	// property, the Details panel constrains it, and nothing constrains a config or
	// DataAsset load. CORE-003's own direction-2 metadata sweep filters on
	// CPF_Config and therefore checks NOTHING for a UDataAsset, so that guard does
	// not protect this class. This is the hand-written stand-in.
	{
		const TConstArrayView<FRacingPropertyRange> Ranges = UVehicleInputConfigDataAsset::StaticRanges();

		const FName Expected[] =
		{
			TEXT("MaxDeltaSeconds"),
			// VEH-004 added InputStaleAfterSeconds, the stuck-input timeout that closes
			// VEH-001 MEDIUM-4. Listing it here is not bookkeeping: this guard is the
			// only thing that would have caught the new clamped property being added to
			// the header and forgotten in StaticRanges, which is exactly the CORE-003
			// M-5 regression shape. It DID catch it -- this test failed on VEH-004's
			// first Smoke run and the count below was wrong, not the property.
			TEXT("InputStaleAfterSeconds"),
			TEXT("FullAuthoritySpeedKph"),
			TEXT("MinAuthoritySpeedKph"),
			TEXT("MinSteerScale")
		};

		TestEqual(TEXT("StaticRanges declares every flat clamped property"), Ranges.Num(), 5);

		for (const FName Name : Expected)
		{
			bool bFound = false;
			for (const FRacingPropertyRange& Range : Ranges)
			{
				if (Range.PropertyName == Name)
				{
					bFound = true;

					// A declared range with no bound at either end is a table entry
					// that enforces nothing, which is worse than an absent one --
					// it looks like coverage.
					TestTrue(*FString::Printf(TEXT("%s declares at least one bound"), *Name.ToString()),
						Range.bHasMin || Range.bHasMax);
					break;
				}
			}

			TestTrue(*FString::Printf(TEXT("StaticRanges covers %s"), *Name.ToString()), bFound);
		}
	}

	// -- The enumerator-count constant tracks the enum ------------------------
	//
	// NumVehicleInputActions is used as an iteration bound; if a slot is appended and
	// the constant is not updated, the new slot is silently never considered.
	{
		const UEnum* Enum = StaticEnum<EVehicleInputAction>();
		TestNotNull(TEXT("EVehicleInputAction is a reflected enum"), Enum);

		if (Enum != nullptr)
		{
			// NumEnums() includes the implicit _MAX entry, hence the -1.
			TestEqual(TEXT("NumVehicleInputActions matches the enum"), Enum->NumEnums() - 1, NumVehicleInputActions);
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Bindings
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleInputConfigBindingsTest,
	"RacingSim.Vehicle.InputConfigBindings",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleInputConfigBindingsTest::RunTest(const FString& Parameters)
{
	// -- An unbound required slot is a validation FAILURE --------------------
	//
	// And it must stay one. VEH-001 ships no UInputAction .uassets (CLAUDE.md forbids
	// authoring binary assets from a worktree), so "this asset does not validate"
	// is the CORRECT current state of the world and is asserted as such. A future
	// change that made a missing binding pass would be indistinguishable from the
	// assets having been authored.
	{
		UVehicleInputConfigDataAsset* Config = MakeConfig();
		TestNotNull(TEXT("The config fixture was constructed"), Config);

		Config->TransmissionMode = ETransmissionInputMode::Automatic;

		const FRacingValidationResult Report = Config->Validate(/* bCorrect */ false);

		TestFalse(TEXT("A config with no bindings does not validate clean"), Report.IsClean());
		TestTrue(TEXT("Missing action bindings are reported"), HasIssueFor(Report, TEXT("ActionBindings")));

		// Bindings are NEVER corrected, even in correcting mode: there is no safe
		// substitute for a missing UInputAction, and inventing one would produce a
		// config that validates clean and cannot drive -- strictly worse, because it
		// comes with a green report.
		Config->Validate(/* bCorrect */ true);
		TestEqual(TEXT("Correction does not invent bindings"), Config->ActionBindings.Num(), 0);
	}

	// -- Which slots are required, and the conditional pair ------------------
	{
		UVehicleInputConfigDataAsset* Config = MakeConfig();
		TestNotNull(TEXT("The config fixture was constructed"), Config);

		TArray<EVehicleInputAction> Required;

		Config->TransmissionMode = ETransmissionInputMode::Automatic;
		Config->GetRequiredActions(Required);

		TestTrue(TEXT("Throttle is always required"), Required.Contains(EVehicleInputAction::Throttle));
		TestTrue(TEXT("Brake is always required"), Required.Contains(EVehicleInputAction::Brake));
		TestTrue(TEXT("Steer is always required"), Required.Contains(EVehicleInputAction::Steer));

		// Handbrake is required because a car that cannot be held on a hill start
		// cannot complete Docs/02-VehiclePhysics.md validation manoeuvre 8; Reset is
		// required because a stuck car with no reset ends the session.
		TestTrue(TEXT("Handbrake is always required"), Required.Contains(EVehicleInputAction::Handbrake));
		TestTrue(TEXT("Reset is always required"), Required.Contains(EVehicleInputAction::Reset));

		TestFalse(TEXT("Shift up is NOT required under an automatic transmission"), Required.Contains(EVehicleInputAction::ShiftUp));
		TestFalse(TEXT("Shift down is NOT required under an automatic transmission"), Required.Contains(EVehicleInputAction::ShiftDown));

		// Never required in either mode: a manual gearbox with an automatic clutch is
		// a normal configuration -- it is what a paddle-shift car is -- so demanding a
		// clutch binding would reject a legitimate setup.
		TestFalse(TEXT("Clutch is never required under an automatic transmission"), Required.Contains(EVehicleInputAction::Clutch));

		Config->TransmissionMode = ETransmissionInputMode::Manual;
		Config->GetRequiredActions(Required);

		// The whole point of carrying the transmission mode on this asset: a manual
		// car whose shift slots are unbound is stuck in first gear, and nothing about
		// the asset would say so.
		TestTrue(TEXT("Shift up becomes required under a manual transmission"), Required.Contains(EVehicleInputAction::ShiftUp));
		TestTrue(TEXT("Shift down becomes required under a manual transmission"), Required.Contains(EVehicleInputAction::ShiftDown));
		TestFalse(TEXT("Clutch is still not required under a manual transmission"), Required.Contains(EVehicleInputAction::Clutch));

		// The manual list must be a strict superset, or switching transmission mode
		// would quietly drop a requirement.
		TestTrue(TEXT("Manual requires strictly more slots than automatic"), Required.Num() == 7);
	}

	// -- A present-but-null binding is reported separately -------------------
	//
	// More dangerous than an absent one, because it LOOKS bound in the Details panel.
	{
		UVehicleInputConfigDataAsset* Config = MakeConfig();
		TestNotNull(TEXT("The config fixture was constructed"), Config);

		Config->ActionBindings.Add(EVehicleInputAction::Throttle, nullptr);

		const FRacingValidationResult Report = Config->Validate(/* bCorrect */ false);

		bool bFoundNullMessage = false;
		for (const FRacingValidationIssue& Issue : Report.Issues)
		{
			if (Issue.Message.Contains(TEXT("null asset reference")))
			{
				bFoundNullMessage = true;
				break;
			}
		}

		TestTrue(TEXT("A present-but-null binding is reported as null, not as absent"), bFoundNullMessage);
	}

	// -- Both shipping devices need a profile --------------------------------
	//
	// Docs/00-ExecutivePlan.md names keyboard AND gamepad. Dropping one is a scope
	// change, not a tuning choice, and it should have to fail a test to happen.
	{
		UVehicleInputConfigDataAsset* Config = MakeConfig();
		TestNotNull(TEXT("The config fixture was constructed"), Config);

		Config->Profiles.Remove(ERacingInputDeviceType::Gamepad);

		const FRacingValidationResult Report = Config->Validate(/* bCorrect */ false);
		TestTrue(TEXT("A missing gamepad profile is reported"), HasIssueFor(Report, TEXT("Profiles")));

		// FindProfile must NOT fall back to another device. A silent substitution
		// would apply keyboard ramp rates to a gamepad and present as "the pad feels
		// laggy", with nothing in any log to say why.
		TestNull(TEXT("FindProfile returns null rather than substituting another device"), Config->FindProfile(ERacingInputDeviceType::Gamepad));
		TestNotNull(TEXT("FindProfile still finds a configured device"), Config->FindProfile(ERacingInputDeviceType::Keyboard));
	}

	// -- A device with a profile but no mapping context ----------------------
	//
	// Its actions are shaped but never triggered: a car that is configured and does
	// not respond, which reads as a physics fault rather than a binding one.
	{
		UVehicleInputConfigDataAsset* Config = MakeConfig();
		TestNotNull(TEXT("The config fixture was constructed"), Config);

		const FRacingValidationResult Report = Config->Validate(/* bCorrect */ false);
		TestTrue(TEXT("A profile with no mapping context is reported"), HasIssueFor(Report, TEXT("MappingContexts")));
	}

	// -- The default device must be drivable at possession -------------------
	{
		UVehicleInputConfigDataAsset* Config = MakeConfig();
		TestNotNull(TEXT("The config fixture was constructed"), Config);

		Config->DefaultDeviceType = ERacingInputDeviceType::Wheel;   // no profile authored

		const FRacingValidationResult Report = Config->Validate(/* bCorrect */ false);
		TestTrue(TEXT("A DefaultDeviceType with no profile is reported"), HasIssueFor(Report, TEXT("DefaultDeviceType")));
	}

	return true;
}

// ---------------------------------------------------------------------------
// Speed-sensitive steering: the curve, and the unit conversion
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleInputUnitsTest,
	"RacingSim.Vehicle.InputUnits",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleInputUnitsTest::RunTest(const FString& Parameters)
{
	// .claude/rules/unreal-source.md requires Unreal-centimetre/SI conversions to be
	// explicit AND tested. Speed-sensitive steering is the only place in this layer
	// with a real unit: speed arrives in cm/s (the project storage unit,
	// Core/RacingSimUnits.h) and the thresholds and curve domain are km/h.
	//
	// A wrong constant here is a silent 3.6x error that would present as "the
	// steering goes light far too early" -- plausible enough to be tuned around
	// rather than fixed.

	constexpr float Tol = 1.0e-4f;

	// -- Off means off -------------------------------------------------------
	{
		UVehicleInputConfigDataAsset* Config = MakeConfig();
		TestNotNull(TEXT("The config fixture was constructed"), Config);

		Config->SteerSpeedScaleMode = ESteerSpeedScaleMode::Off;

		TestNearlyEqual(TEXT("Off gives full authority when stationary"), Config->GetSteerScaleForSpeedCms(0.0), 1.0f, 0.0f);
		TestNearlyEqual(TEXT("Off gives full authority at 300 km/h"), Config->GetSteerScaleForSpeedCms(8333.0), 1.0f, 0.0f);
	}

	// -- Linear, with the conversion pinned against known physical values ----
	{
		UVehicleInputConfigDataAsset* Config = MakeConfig();
		TestNotNull(TEXT("The config fixture was constructed"), Config);

		Config->SteerSpeedScaleMode = ESteerSpeedScaleMode::Linear;
		Config->FullAuthoritySpeedKph = 50.0f;
		Config->MinAuthoritySpeedKph = 250.0f;
		Config->MinSteerScale = 0.5f;

		// Known value: 1 m/s == 3.6 km/h exactly, so 50 km/h == 50/3.6 m/s ==
		// 5000/3.6 cm/s == 1388.88... cm/s. Computed from the physical definition,
		// not from another function in the header under test.
		const double FiftyKphInCms = 50.0 * 100000.0 / 3600.0;
		const double TwoFiftyKphInCms = 250.0 * 100000.0 / 3600.0;
		const double OneFiftyKphInCms = 150.0 * 100000.0 / 3600.0;

		TestNearlyEqual(TEXT("At the full-authority threshold the scale is 1"), Config->GetSteerScaleForSpeedCms(FiftyKphInCms), 1.0f, Tol);
		TestNearlyEqual(TEXT("Below the full-authority threshold the scale is 1"), Config->GetSteerScaleForSpeedCms(0.0), 1.0f, Tol);
		TestNearlyEqual(TEXT("At the min-authority threshold the scale is MinSteerScale"), Config->GetSteerScaleForSpeedCms(TwoFiftyKphInCms), 0.5f, Tol);

		// Halfway between 50 and 250 km/h, so halfway between 1.0 and 0.5.
		TestNearlyEqual(TEXT("Halfway between the thresholds the scale is halfway"), Config->GetSteerScaleForSpeedCms(OneFiftyKphInCms), 0.75f, Tol);

		// Beyond the far threshold must CLAMP, not keep falling to zero or negative.
		TestNearlyEqual(TEXT("Beyond the min-authority threshold the scale clamps"), Config->GetSteerScaleForSpeedCms(TwoFiftyKphInCms * 4.0), 0.5f, Tol);

		// THE conversion assertion. If this file ever used an inline 0.036 with the
		// wrong sign or magnitude, feeding a cm/s value that the CORE-003-tested
		// helper says is 150 km/h would land somewhere other than 0.75.
		TestNearlyEqual(
			TEXT("The cm/s -> km/h conversion agrees with RacingSim::Units"),
			static_cast<float>(RacingSim::Units::CmsToKilometresPerHour(OneFiftyKphInCms)),
			150.0f,
			1.0e-3f);

		// A reversing car has negative forward speed and wants FULL steering
		// authority. The absolute value would be wrong here -- reversing at 40 km/h
		// must not be treated as 40 km/h forward.
		TestNearlyEqual(TEXT("A reversing car keeps full steering authority"), Config->GetSteerScaleForSpeedCms(-TwoFiftyKphInCms), 1.0f, Tol);

		// Non-finite speed must not produce a NaN scale. This function is on the
		// steering path, so a NaN here would make the input layer the SOURCE of the
		// defect VEH-004 exists to detect.
		TestTrue(TEXT("A NaN speed yields a finite scale"), FMath::IsFinite(Config->GetSteerScaleForSpeedCms(FMath::Sqrt(-1.0))));
		TestNearlyEqual(TEXT("A NaN speed is treated as stationary"), Config->GetSteerScaleForSpeedCms(FMath::Sqrt(-1.0)), 1.0f, Tol);
		TestNearlyEqual(TEXT("An infinite speed is treated as stationary"), Config->GetSteerScaleForSpeedCms(std::numeric_limits<double>::infinity()), 1.0f, Tol);

		// Inverted thresholds must not divide by zero. Validate() rejects this, but
		// this function must survive it regardless.
		Config->FullAuthoritySpeedKph = 250.0f;
		Config->MinAuthoritySpeedKph = 250.0f;
		TestNearlyEqual(TEXT("Degenerate thresholds fall back to full authority"), Config->GetSteerScaleForSpeedCms(OneFiftyKphInCms), 1.0f, Tol);

		const FRacingValidationResult Report = Config->Validate(/* bCorrect */ false);
		TestTrue(TEXT("Degenerate linear thresholds are reported"), HasIssueFor(Report, TEXT("MinAuthoritySpeedKph")));
	}

	// -- Curve mode REQUIRES a usable curve, and does not silently fall back -
	{
		UVehicleInputConfigDataAsset* Config = MakeConfig();
		TestNotNull(TEXT("The config fixture was constructed"), Config);

		Config->SteerSpeedScaleMode = ESteerSpeedScaleMode::Curve;

		// Empty curve. This is the case the criteria call out: falling back to Off
		// silently is the difference between a car that is twitchy at 250 km/h and
		// one that spins on the straight, and the author would see "Curve" selected
		// in the asset and believe it.
		const FRacingValidationResult Empty = Config->Validate(/* bCorrect */ false);
		TestTrue(TEXT("Curve mode with no curve is reported"), HasIssueFor(Empty, TEXT("SteerScaleBySpeedKphCurve")));

		// One key is still not a curve.
		FRichCurve* Curve = Config->SteerScaleBySpeedKphCurve.GetRichCurve();
		TestNotNull(TEXT("The runtime curve is available"), Curve);

		if (Curve != nullptr)
		{
			Curve->AddKey(0.0f, 1.0f);
			TestTrue(TEXT("Curve mode with one key is still reported"),
				HasIssueFor(Config->Validate(/* bCorrect */ false), TEXT("SteerScaleBySpeedKphCurve")));

			// Two keys: usable.
			Curve->AddKey(250.0f, 0.4f);
			TestFalse(TEXT("A two-key curve is accepted"),
				HasIssueFor(Config->Validate(/* bCorrect */ false), TEXT("SteerScaleBySpeedKphCurve")));

			// And it is actually evaluated, in km/h. 125 km/h is halfway along a
			// linear-interpolating two-key curve, so ~0.7.
			const double OneTwentyFiveKphInCms = 125.0 * 100000.0 / 3600.0;
			TestNearlyEqual(TEXT("The curve is sampled in km/h, not cm/s"),
				Config->GetSteerScaleForSpeedCms(OneTwentyFiveKphInCms), 0.7f, 0.05f);

			// A curve value outside [0.05, 1] is a steering multiplier that either
			// locks the wheel or AMPLIFIES the driver's own input -- neither is
			// discoverable by looking at the car.
			Curve->AddKey(400.0f, 3.7f);
			TestTrue(TEXT("An out-of-range curve value is reported"),
				HasIssueFor(Config->Validate(/* bCorrect */ false), TEXT("SteerScaleBySpeedKphCurve")));

			// Even unvalidated, evaluation must clamp: a curve can be edited after
			// validation, and this is not the place to discover it.
			const double FourHundredKphInCms = 400.0 * 100000.0 / 3600.0;
			TestTrue(TEXT("An out-of-range curve value is clamped at evaluation"),
				Config->GetSteerScaleForSpeedCms(FourHundredKphInCms) <= 1.0f);

			// A negative-time key: the domain is non-negative road speed, and this
			// curve does not model reversing.
			Curve->Reset();
			Curve->AddKey(-10.0f, 1.0f);
			Curve->AddKey(100.0f, 0.5f);
			TestTrue(TEXT("A negative-speed curve key is reported"),
				HasIssueFor(Config->Validate(/* bCorrect */ false), TEXT("SteerScaleBySpeedKphCurve")));
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// No hard-coded keys
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleInputNoHardcodedKeysTest,
	"RacingSim.Vehicle.InputNoHardcodedKeys",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleInputNoHardcodedKeysTest::RunTest(const FString& Parameters)
{
	// VEH-001's acceptance criteria require rebinding to be an ASSET EDIT and never a
	// recompile. The mechanism is UVehicleInputConfigDataAsset's slot -> UInputAction
	// indirection; this test is what stops that mechanism decaying.
	//
	// It is a source scan and not a review convention for the same reason
	// RacingSim.Build.cs scans for automation macros (TEST-001 N-2): a convention
	// does not survive the first "just for now" hard-coded key, and the resulting
	// build is green.

	const FString VehicleDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("RacingSim"), TEXT("Vehicle"));

	TArray<FString> SourceFiles;
	IFileManager::Get().FindFilesRecursive(SourceFiles, *VehicleDir, TEXT("*.h"), true, false, false);
	IFileManager::Get().FindFilesRecursive(SourceFiles, *VehicleDir, TEXT("*.cpp"), true, false, false);

	// If this ever finds nothing, the test would pass vacuously and stay green
	// forever while enforcing nothing -- the exact decay mode it exists to prevent.
	TestTrue(*FString::Printf(TEXT("Found source files to scan under %s"), *VehicleDir), SourceFiles.Num() > 0);

	// The banned tokens. EKeys:: is the namespace every key literal comes from;
	// FKey( is a direct construction; the two most common accidental forms are both
	// covered. This is not a parser and does not claim to be -- see the limitation
	// note below.
	const TCHAR* BannedTokens[] =
	{
		TEXT("EKeys::"),
		TEXT("FKey("),
		TEXT("FInputChord(")
	};

	int32 FilesScanned = 0;

	for (const FString& File : SourceFiles)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *File))
		{
			AddError(FString::Printf(TEXT("Could not read %s"), *File));
			continue;
		}

		++FilesScanned;

		for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
		{
			const FString Trimmed = Lines[LineIndex].TrimStart();

			// LIMITATION, stated rather than implied: comment lines are skipped by a
			// prefix test, not by a real lexer. That is deliberately weaker than
			// RacingSim.Build.cs's scanner, and the trade is acceptable here because
			// the direction of the weakness is safe for the interesting case -- code
			// is never skipped, only comments are. The reason it is needed at all is
			// that the headers in this directory DISCUSS the ban in prose, and a
			// naive scan would flag its own documentation.
			//
			// What it does not catch: a token on the same line after a trailing
			// comment marker, and a key reached through a typedef or a macro. Neither
			// is a plausible accident; both would be caught in review.
			if (Trimmed.StartsWith(TEXT("//")) || Trimmed.StartsWith(TEXT("*")) || Trimmed.StartsWith(TEXT("/*")))
			{
				continue;
			}

			for (const TCHAR* Token : BannedTokens)
			{
				if (Trimmed.Contains(Token))
				{
					AddError(FString::Printf(
						TEXT("%s(%d): hard-coded input key token '%s'. Rebinding must be an edit to a ")
						TEXT("UInputMappingContext asset, never a recompile -- bind through ")
						TEXT("UVehicleInputConfigDataAsset::ActionBindings instead. Line: %s"),
						*File, LineIndex + 1, Token, *Trimmed));
				}
			}
		}
	}

	TestTrue(TEXT("At least one Vehicle source file was actually read"), FilesScanned > 0);

	return true;
}
