// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingSimSettings.h"
#include "Core/RacingSimValidation.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/UnrealType.h"

namespace RacingSimValidationSpecPrivate
{
	/** The ini section these settings live in; the same string a CI -ini: override uses. */
	static const TCHAR* SettingsSection = TEXT("/Script/RacingSim.RacingSimSettings");

	/**
	 * IEEE-754 binary64 bit patterns for a quiet NaN and +infinity.
	 *
	 * Built from bits rather than arithmetic on purpose. `TNumericLimits<double>::Max() * 2.0`
	 * is constant arithmetic that overflows, which MSVC reports as C4756 -- and
	 * CLAUDE.md forbids suppressing warnings. `FMath::Sqrt(-1.0)` is worse: under
	 * a fast floating-point model a compiler is entitled to assume operands are
	 * finite, so the NaN it is supposed to produce may be optimised away and the
	 * test would silently stop testing anything.
	 */
	static constexpr uint64 QuietNaNBits = 0x7FF8000000000000ull;
	static constexpr uint64 PositiveInfinityBits = 0x7FF0000000000000ull;

	static double MakeIeee754Double(const uint64 Bits)
	{
		double Value = 0.0;
		static_assert(sizeof(Value) == sizeof(Bits), "binary64 double assumed");
		FMemory::Memcpy(&Value, &Bits, sizeof(Value));
		return Value;
	}

	/**
	 * Restores every settings field these tests touch, whatever happens.
	 *
	 * The settings CDO is process-global. Restoring on scope exit (including an
	 * early return) is the difference between a test and a landmine for whatever
	 * runs next. Nothing here calls SaveConfig(), so no ini file is written.
	 */
	struct FScopedSettingsOverride
	{
		FScopedSettingsOverride()
			: Settings(GetMutableDefault<URacingSimSettings>())
			, SavedLapTimeFractionalDigits(Settings->LapTimeFractionalDigits)
			, SavedPhysicsPolicyVersion(Settings->PhysicsPolicyVersion)
			, SavedTelemetrySampleRateHz(Settings->TelemetrySampleRateHz)
			, SavedTelemetryStaleAfterSeconds(Settings->TelemetryStaleAfterSeconds)
		{
		}

		~FScopedSettingsOverride()
		{
			Settings->LapTimeFractionalDigits = SavedLapTimeFractionalDigits;
			Settings->PhysicsPolicyVersion = SavedPhysicsPolicyVersion;
			Settings->TelemetrySampleRateHz = SavedTelemetrySampleRateHz;
			Settings->TelemetryStaleAfterSeconds = SavedTelemetryStaleAfterSeconds;
		}

		URacingSimSettings* Settings;

	private:
		int32 SavedLapTimeFractionalDigits;
		int32 SavedPhysicsPolicyVersion;
		float SavedTelemetrySampleRateHz;
		double SavedTelemetryStaleAfterSeconds;
	};

	/**
	 * Sets one key in the live Game config hierarchy and puts it back exactly as
	 * it was -- including removing it again if it did not exist, which is the
	 * normal case for this project.
	 *
	 * This is what makes the ini test a real test rather than a direct field
	 * write: the value travels through GConfig and UObject::LoadConfig, the same
	 * path an `-ini:Game:[/Script/RacingSim.RacingSimSettings]:Key=Value`
	 * command-line override takes.
	 */
	struct FScopedConfigValue
	{
		FScopedConfigValue(const TCHAR* InKey, const TCHAR* Value)
			: Key(InKey)
		{
			bExisted = GConfig != nullptr && GConfig->GetString(SettingsSection, InKey, SavedValue, GGameIni);
			if (GConfig != nullptr)
			{
				GConfig->SetString(SettingsSection, InKey, Value, GGameIni);
			}
		}

		~FScopedConfigValue()
		{
			if (GConfig == nullptr)
			{
				return;
			}
			if (bExisted)
			{
				GConfig->SetString(SettingsSection, *Key, *SavedValue, GGameIni);
			}
			else
			{
				GConfig->RemoveKey(SettingsSection, *Key, GGameIni);
			}
		}

	private:
		FString Key;
		FString SavedValue;
		bool bExisted = false;
	};
}

/**
 * CORE-003: the declared range table and the UPROPERTY metadata agree.
 *
 * This is the test that makes the duplication in
 * URacingSimSettings::GetValidatedPropertyRanges() safe. The runtime clamp cannot
 * read ClampMin/ClampMax metadata, because metadata is compiled out of non-editor
 * targets (WITH_METADATA == WITH_EDITORONLY_DATA, CoreMiscDefines.h:31) -- so the
 * ranges are declared twice, and this asserts they never diverge.
 *
 * The second direction is the one that matters: a NEW config property with a
 * clamp in its metadata but no entry in the table fails here. That is the exact
 * shape of CORE-002 finding M-5, which is what this ticket exists to close.
 *
 * Includes negative controls. A consistency check that cannot fail proves
 * nothing, and this one has two ways to be silently vacuous (no metadata in the
 * build, or an empty table), so both are asserted against directly.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimSettingsRangeMetadataTest,
	"RacingSim.Core.SettingsRangeMetadata",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimSettingsRangeMetadataTest::RunTest(const FString& Parameters)
{
	using namespace RacingSim::Validation;

	// Guard against a vacuous pass. If this build had no property metadata,
	// VerifyRangesMatchMetadata would return clean while checking nothing.
	TestTrue(TEXT("This build carries property metadata, so the check below is not vacuous"), WITH_METADATA != 0);

	const TConstArrayView<FRacingPropertyRange> Ranges = URacingSimSettings::GetValidatedPropertyRanges();
	TestTrue(TEXT("The range table is not empty"), Ranges.Num() > 0);

	// The four clamped config properties CORE-002 shipped. Named explicitly so
	// that deleting one from the table is a failure here as well as in the
	// metadata sweep below.
	{
		static const TCHAR* ExpectedProperties[] = {
			TEXT("LapTimeFractionalDigits"),
			TEXT("PhysicsPolicyVersion"),
			TEXT("TelemetrySampleRateHz"),
			TEXT("TelemetryStaleAfterSeconds")
		};

		for (const TCHAR* Expected : ExpectedProperties)
		{
			const FName Name(Expected);
			const bool bPresent = Ranges.ContainsByPredicate(
				[Name](const FRacingPropertyRange& Range) { return Range.PropertyName == Name; });
			TestTrue(*FString::Printf(TEXT("Range table covers %s"), Expected), bPresent);
		}
	}

	// -- The real assertion --------------------------------------------------
	{
		const FRacingValidationResult Result =
			VerifyRangesMatchMetadata(URacingSimSettings::StaticClass(), Ranges);
		TestTrue(
			*FString::Printf(TEXT("Range table matches UPROPERTY metadata in both directions. Issues:\n%s"), *Result.ToString()),
			Result.IsClean());
	}

	// -- Negative control 1: a wrong bound must be detected -------------------
	// LapTimeFractionalDigits is ClampMax = "3". Claim 5 and the check must object.
	{
		TArray<FRacingPropertyRange> Wrong(Ranges.GetData(), Ranges.Num());
		for (FRacingPropertyRange& Range : Wrong)
		{
			if (Range.PropertyName == FName(TEXT("LapTimeFractionalDigits")))
			{
				Range.Max = 5.0;
			}
		}

		const FRacingValidationResult Result = VerifyRangesMatchMetadata(URacingSimSettings::StaticClass(), Wrong);
		TestTrue(TEXT("A range table whose maximum disagrees with ClampMax is rejected"), Result.NumFailed() > 0);
	}

	// -- Negative control 2: a missing entry must be detected -----------------
	// This is the M-5 shape itself: the property is clamped in the Details panel
	// and absent from the table, so an -ini: override would bypass the range.
	{
		TArray<FRacingPropertyRange> Incomplete(Ranges.GetData(), Ranges.Num());
		Incomplete.RemoveAll(
			[](const FRacingPropertyRange& Range) { return Range.PropertyName == FName(TEXT("TelemetrySampleRateHz")); });

		const FRacingValidationResult Result = VerifyRangesMatchMetadata(URacingSimSettings::StaticClass(), Incomplete);
		TestTrue(
			TEXT("A clamped config property missing from the range table is rejected (the M-5 shape)"),
			Result.NumFailed() > 0);
	}

	// -- Negative control 3: an unrelated bound omission must be detected -----
	// TelemetrySampleRateHz has a ClampMax. Dropping it from the table means an
	// ini could set 1e6 and nothing would clamp it.
	{
		TArray<FRacingPropertyRange> Loosened(Ranges.GetData(), Ranges.Num());
		for (FRacingPropertyRange& Range : Loosened)
		{
			if (Range.PropertyName == FName(TEXT("TelemetrySampleRateHz")))
			{
				Range.bHasMax = false;
			}
		}

		const FRacingValidationResult Result = VerifyRangesMatchMetadata(URacingSimSettings::StaticClass(), Loosened);
		TestTrue(TEXT("A table that drops a declared ClampMax is rejected"), Result.NumFailed() > 0);
	}

	return true;
}

/**
 * CORE-003: the reflection-driven range pass itself.
 *
 * Uses EnforceRanges directly rather than URacingSimSettings::ValidateConfiguredRanges
 * so these cases assert on the returned result instead of on log text.
 * (RacingSim.Core.SettingsIniOverrideClamp covers the logging path.)
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimRangeEnforcementTest,
	"RacingSim.Core.RangeEnforcement",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimRangeEnforcementTest::RunTest(const FString& Parameters)
{
	using namespace RacingSim::Validation;
	using namespace RacingSimValidationSpecPrivate;

	FScopedSettingsOverride Override;
	URacingSimSettings* Settings = Override.Settings;
	const TConstArrayView<FRacingPropertyRange> Ranges = URacingSimSettings::GetValidatedPropertyRanges();

	// -- In-range values are left completely alone ---------------------------
	// A clamp pass that rewrites correct values is a clamp pass that will one day
	// quietly change a tuned number.
	{
		Settings->LapTimeFractionalDigits = 2;
		Settings->PhysicsPolicyVersion = 4;
		Settings->TelemetrySampleRateHz = 30.0f;
		Settings->TelemetryStaleAfterSeconds = 0.5;

		const FRacingValidationResult Result = EnforceRanges(Settings, Ranges);
		TestTrue(*FString::Printf(TEXT("An in-range configuration is clean. Issues:\n%s"), *Result.ToString()), Result.IsClean());
		TestEqual(TEXT("An in-range int is untouched"), Settings->LapTimeFractionalDigits, 2);
		TestEqual(TEXT("An in-range float is untouched"), Settings->TelemetrySampleRateHz, 30.0f);
	}

	// -- Above ClampMax: integer ---------------------------------------------
	// The ticket's own example: LapTimeFractionalDigits = 99.
	{
		Settings->LapTimeFractionalDigits = 99;

		const FRacingValidationResult Result = EnforceRanges(Settings, Ranges);
		TestEqual(TEXT("LapTimeFractionalDigits = 99 is clamped to the declared maximum"), Settings->LapTimeFractionalDigits, 3);
		TestTrue(TEXT("...and the pass reports it"), Result.WasCorrected(TEXT("LapTimeFractionalDigits")));
		TestEqual(TEXT("...as exactly one correction"), Result.NumCorrected(), 1);
		TestEqual(TEXT("...with no range-table failures"), Result.NumFailed(), 0);
	}

	// -- Below ClampMin: integer ---------------------------------------------
	// A negative PhysicsPolicyVersion clamps to 0, which is "no policy
	// established" -- the value that makes IsPublishable() refuse. Corrections
	// degrade toward unpublishable, never toward plausible.
	{
		Settings->LapTimeFractionalDigits = 3;
		Settings->PhysicsPolicyVersion = -7;

		const FRacingValidationResult Result = EnforceRanges(Settings, Ranges);
		TestEqual(TEXT("A negative PhysicsPolicyVersion is clamped to 0"), Settings->PhysicsPolicyVersion, 0);
		TestTrue(TEXT("...and the pass reports it"), Result.WasCorrected(TEXT("PhysicsPolicyVersion")));
	}

	// -- Above ClampMax: float -----------------------------------------------
	// The ticket's other example: TelemetrySampleRateHz = 1e6.
	{
		Settings->PhysicsPolicyVersion = 0;
		Settings->TelemetrySampleRateHz = 1.0e6f;

		const FRacingValidationResult Result = EnforceRanges(Settings, Ranges);
		TestEqual(TEXT("TelemetrySampleRateHz = 1e6 is clamped to 240"), Settings->TelemetrySampleRateHz, 240.0f);
		TestTrue(TEXT("...and the pass reports it"), Result.WasCorrected(TEXT("TelemetrySampleRateHz")));
	}

	// -- Below ClampMin: float -----------------------------------------------
	{
		Settings->TelemetrySampleRateHz = -1.0f;

		const FRacingValidationResult Result = EnforceRanges(Settings, Ranges);
		TestEqual(TEXT("A negative telemetry sample rate is clamped to 0 (sampling off)"), Settings->TelemetrySampleRateHz, 0.0f);
		TestTrue(TEXT("...and the pass reports it"), Result.WasCorrected(TEXT("TelemetrySampleRateHz")));
	}

	// -- Non-finite ----------------------------------------------------------
	// The case a naive clamp misses entirely: every comparison against NaN is
	// false, so `if (V < Min)` and `if (V > Max)` both decline to act and the NaN
	// survives into the HUD's staleness arithmetic, where it makes every frame
	// compare as not-stale.
	{
		Settings->TelemetrySampleRateHz = 30.0f;
		Settings->TelemetryStaleAfterSeconds = MakeIeee754Double(QuietNaNBits);
		TestTrue(TEXT("Precondition: the test value really is NaN"), FMath::IsNaN(Settings->TelemetryStaleAfterSeconds));

		const FRacingValidationResult Result = EnforceRanges(Settings, Ranges);
		TestFalse(TEXT("A NaN does not survive validation"), FMath::IsNaN(Settings->TelemetryStaleAfterSeconds));
		TestEqual(TEXT("A NaN is replaced by the safe end of the range"), Settings->TelemetryStaleAfterSeconds, 0.0);
		TestTrue(TEXT("...and the pass reports it"), Result.WasCorrected(TEXT("TelemetryStaleAfterSeconds")));
	}
	{
		Settings->TelemetryStaleAfterSeconds = MakeIeee754Double(PositiveInfinityBits);
		TestFalse(TEXT("Precondition: the test value really is infinite"), FMath::IsFinite(Settings->TelemetryStaleAfterSeconds));

		const FRacingValidationResult Result = EnforceRanges(Settings, Ranges);
		TestTrue(TEXT("An infinity does not survive validation"), FMath::IsFinite(Settings->TelemetryStaleAfterSeconds));
		TestTrue(TEXT("...and the pass reports it"), Result.WasCorrected(TEXT("TelemetryStaleAfterSeconds")));
	}

	// -- A property with only a minimum is not given a phantom maximum --------
	// TelemetryStaleAfterSeconds carries UIMax = 5.0 and no ClampMax. UIMax is a
	// slider hint; treating it as a bound would destroy a legal value.
	{
		Settings->TelemetryStaleAfterSeconds = 10.0;

		const FRacingValidationResult Result = EnforceRanges(Settings, Ranges);
		TestEqual(TEXT("UIMax is not treated as a hard maximum"), Settings->TelemetryStaleAfterSeconds, 10.0);
		TestTrue(TEXT("...and nothing is reported"), Result.IsClean());
	}

	// -- Range-table errors fail loudly, they do not skip silently ------------
	// Each of these is a programming error rather than bad user data, so each
	// must surface as a Failed issue. A pass that quietly ignores a range it
	// cannot apply is indistinguishable from no validation at all.
	{
		const FRacingValidationResult Result = EnforceRanges(nullptr, Ranges);
		TestEqual(TEXT("A null object is reported, not crashed on"), Result.NumFailed(), 1);
	}
	{
		const FRacingPropertyRange Missing[] = {
			FRacingPropertyRange::Between(TEXT("ThisPropertyDoesNotExist"), 0.0, 1.0)
		};
		const FRacingValidationResult Result = EnforceRanges(Settings, Missing);
		TestEqual(TEXT("A range naming a nonexistent property is reported"), Result.NumFailed(), 1);
	}
	{
		// BuildChannel is an FStrProperty; a numeric range cannot apply.
		const FRacingPropertyRange NonNumeric[] = {
			FRacingPropertyRange::Between(TEXT("BuildChannel"), 0.0, 1.0)
		};
		const FRacingValidationResult Result = EnforceRanges(Settings, NonNumeric);
		TestEqual(TEXT("A range on a non-numeric property is reported"), Result.NumFailed(), 1);
	}
	{
		// DefaultSpeedDisplayUnit is an enum; a numeric range over enumerators is
		// not meaningful and must not silently "work".
		const FRacingPropertyRange EnumRange[] = {
			FRacingPropertyRange::Between(TEXT("DefaultSpeedDisplayUnit"), 0.0, 1.0)
		};
		const FRacingValidationResult Result = EnforceRanges(Settings, EnumRange);
		TestEqual(TEXT("A range on an enum property is reported"), Result.NumFailed(), 1);
	}

	return true;
}

/**
 * CORE-003: an out-of-range value arriving from config is clamped.
 *
 * This is the acceptance criterion in its literal form, and it is deliberately
 * the slow way round: the values travel through GConfig and
 * UObject::LoadConfig/PostReloadConfig, which is the same path
 *
 *   -ini:Game:[/Script/RacingSim.RacingSimSettings]:LapTimeFractionalDigits=99
 *
 * takes. Writing the fields directly (as RacingSim.Core.RangeEnforcement does)
 * proves the clamp arithmetic; only this proves the hook is actually wired to a
 * config load, which is the half of M-5 that was missing.
 *
 * Two distinct properties, one integer and one float, because the pass takes a
 * different branch for each.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimSettingsIniOverrideClampTest,
	"RacingSim.Core.SettingsIniOverrideClamp",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimSettingsIniOverrideClampTest::RunTest(const FString& Parameters)
{
	using namespace RacingSimValidationSpecPrivate;

	if (!TestNotNull(TEXT("GConfig exists"), GConfig))
	{
		return false;
	}

	FScopedSettingsOverride Override;
	URacingSimSettings* Settings = Override.Settings;

	// Patterns are distinct per property. AddExpectedMessage keys a TSet by
	// pattern string alone, so two identical patterns would let the second
	// registration silently replace the first and prove nothing (CORE-002
	// finding MEDIUM-3). Matching on the property name rather than the formatted
	// value also keeps the test off %g's exponent-notation threshold.
	AddExpectedMessagePlain(
		TEXT("RacingSim settings failed range validation after PostReloadConfig"),
		ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains,
		1);
	// NOTE the missing 'U'. These messages are built from UClass::GetName(), which
	// returns the reflected name without the C++ prefix -- "RacingSimSettings",
	// not "URacingSimSettings". Caught by this test failing on its first run with
	// the clamp working correctly; the patterns were wrong, not the code.
	AddExpectedMessagePlain(
		TEXT("RacingSimSettings::LapTimeFractionalDigits loaded as 99"),
		ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains,
		1);
	AddExpectedMessagePlain(
		TEXT("RacingSimSettings::TelemetrySampleRateHz loaded as"),
		ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains,
		1);

	{
		const FScopedConfigValue LapDigits(TEXT("LapTimeFractionalDigits"), TEXT("99"));
		const FScopedConfigValue SampleRate(TEXT("TelemetrySampleRateHz"), TEXT("1000000.0"));

		// Deliberately set the live fields to legal values first, so a pass here
		// can only come from the config values actually being loaded.
		Settings->LapTimeFractionalDigits = 1;
		Settings->TelemetrySampleRateHz = 30.0f;

		Settings->ReloadConfig();

		TestEqual(
			TEXT("An out-of-range integer from config is clamped to ClampMax (was 99)"),
			Settings->LapTimeFractionalDigits,
			3);
		TestEqual(
			TEXT("An out-of-range float from config is clamped to ClampMax (was 1e6)"),
			Settings->TelemetrySampleRateHz,
			240.0f);
	}

	// The config keys are restored by FScopedConfigValue above, and the field
	// values by FScopedSettingsOverride below. Nothing is written to disk.
	return true;
}
