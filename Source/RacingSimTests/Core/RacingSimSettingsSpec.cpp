// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingSimSettings.h"
#include "Core/RacingSimTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

/**
 * CORE-002: settings defaults.
 *
 * These defaults are a contract, not a convenience. Two of them are safety
 * properties rather than preferences:
 *
 *   DefaultAssistPreset == Raw -- if assist plumbing is ever missed, a run is
 *   recorded as having had no assists. That understates the help the driver got,
 *   which makes a wrong record look suspicious rather than flattering.
 *
 *   PhysicsPolicyVersion == 0 -- no physics policy exists yet (VEH-002 owns it),
 *   and 0 is what makes FRacingSimVersionStamp::IsPublishable() refuse to publish.
 *   A "helpful" default of 1 would silently make unversioned laps publishable.
 *
 * If a future ticket changes either, it must change this test in the same commit
 * and say why.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimSettingsDefaultsTest,
	"RacingSim.Core.SettingsDefaults",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimSettingsDefaultsTest::RunTest(const FString& Parameters)
{
	const URacingSimSettings* Settings = GetDefault<URacingSimSettings>();
	if (!TestNotNull(TEXT("URacingSimSettings CDO exists"), Settings))
	{
		// Everything below dereferences it; stop rather than crash the harness.
		return false;
	}

	// Get() must return the same object the engine hands out, not a copy.
	TestTrue(TEXT("Get() returns the CDO"), &URacingSimSettings::Get() == Settings);

	// -- Registration in Project Settings -----------------------------------
	// A settings class that fails to register is invisible in the editor but
	// still readable from C++, so nothing else would notice.
	TestEqual(TEXT("Settings category is Game"), Settings->GetCategoryName(), FName(TEXT("Game")));
	TestEqual(TEXT("Settings container is Project"), Settings->GetContainerName(), FName(TEXT("Project")));
	TestEqual(TEXT("Settings section is RacingSim"), Settings->GetSectionName(), FName(TEXT("RacingSim")));

	// -- Config plumbing ----------------------------------------------------
	// UCLASS(config=Game, defaultconfig) means edits land in DefaultGame.ini and
	// are reviewable in a diff. If someone drops `config` from a UPROPERTY, the
	// value silently stops persisting; assert on the metadata rather than trust it.
	TestEqual(
		TEXT("Settings are stored in the Game config hierarchy"),
		URacingSimSettings::StaticClass()->ClassConfigName,
		FName(TEXT("Game")));
	TestTrue(
		TEXT("Settings use defaultconfig"),
		URacingSimSettings::StaticClass()->HasAnyClassFlags(CLASS_DefaultConfig));

	{
		static const TCHAR* ConfigProperties[] = {
			TEXT("DefaultSpeedDisplayUnit"),
			TEXT("DefaultDistanceDisplayUnit"),
			TEXT("LapTimeFractionalDigits"),
			TEXT("BuildIdScheme"),
			TEXT("BuildChannel"),
			TEXT("ExplicitBuildId"),
			TEXT("PhysicsPolicyVersion"),
			TEXT("DefaultAssistPreset"),
			TEXT("TelemetrySampleRateHz"),
			TEXT("TelemetryStaleAfterSeconds")
		};

		for (const TCHAR* PropertyName : ConfigProperties)
		{
			const FProperty* Property = URacingSimSettings::StaticClass()->FindPropertyByName(FName(PropertyName));
			if (TestNotNull(*FString::Printf(TEXT("Property exists: %s"), PropertyName), Property))
			{
				TestTrue(
					*FString::Printf(TEXT("Property is config-backed: %s"), PropertyName),
					Property->HasAnyPropertyFlags(CPF_Config));
			}
		}
	}

	// -- Units defaults -----------------------------------------------------
	TestEqual(
		TEXT("Default speed display unit is km/h"),
		Settings->DefaultSpeedDisplayUnit,
		ERacingSpeedDisplayUnit::KilometresPerHour);
	TestEqual(
		TEXT("Default distance display unit is metric"),
		Settings->DefaultDistanceDisplayUnit,
		ERacingDistanceDisplayUnit::Metric);
	TestEqual(TEXT("Lap times show 3 fractional digits"), Settings->LapTimeFractionalDigits, 3);
	TestTrue(
		TEXT("Lap time fractional digits stay in the documented 0..3 range"),
		Settings->LapTimeFractionalDigits >= 0 && Settings->LapTimeFractionalDigits <= 3);

	// -- Versioning defaults ------------------------------------------------
	TestEqual(TEXT("Build ID scheme defaults to Derived"), Settings->BuildIdScheme, ERacingBuildIdScheme::Derived);
	TestEqual(TEXT("Build channel defaults to dev"), Settings->BuildChannel, FString(TEXT("dev")));
	TestTrue(TEXT("No CI build ID is stamped locally"), Settings->ExplicitBuildId.IsEmpty());

	// Safety defaults -- see the class comment above.
	TestEqual(TEXT("Physics policy version is 0 until VEH-002 sets one"), Settings->PhysicsPolicyVersion, 0);
	TestEqual(TEXT("Default assist preset is Raw"), Settings->DefaultAssistPreset, ERacingAssistPreset::Raw);

	// -- Telemetry defaults -------------------------------------------------
	TestEqual(TEXT("Telemetry samples at 30 Hz"), Settings->TelemetrySampleRateHz, 30.0f);
	TestTrue(
		TEXT("Telemetry sample rate is a sane positive rate"),
		Settings->TelemetrySampleRateHz > 0.0f && Settings->TelemetrySampleRateHz <= 240.0f);
	TestNearlyEqual(TEXT("Telemetry goes stale after 0.5 s"), Settings->TelemetryStaleAfterSeconds, 0.5, 0.0);

	return true;
}
