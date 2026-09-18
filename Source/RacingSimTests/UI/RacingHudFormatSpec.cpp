// Copyright RacingSim. All Rights Reserved.

#include "UI/RacingHudFormat.h"

#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/AutomationTest.h"
#include "UObject/Class.h"

#include <limits>

/**
 * UI-002: URacingHudFormatLibrary, the only place HUD text is made.
 *
 * Exact strings, because a HUD string that is "nearly right" (1:00.000 printed as 0:60.000,
 * a reverse speed printed as -12) is the bug. The culture block re-runs a sample of them
 * under a comma-decimal culture and requires the same bytes.
 */

namespace HudFormatSpecPrivate
{
	void ExpectText(FAutomationTestBase& Test, const TCHAR* What, const FText& Actual, const TCHAR* Expected)
	{
		Test.TestEqual(What, Actual.ToString(), FString(Expected));
	}

	/** One of each function, at values whose text would change under a comma-decimal culture if it were culture-sensitive. */
	TArray<FString> CultureSample()
	{
		using Format = URacingHudFormatLibrary;
		return {
			Format::FormatLapTime(83.456, true).ToString(),
			Format::FormatDelta(-1.25, true).ToString(),
			Format::FormatSpeed(12345.0, true).ToString(),
			Format::FormatRPM(12345.0f, true).ToString(),
			Format::FormatCountdown(3, true).ToString(),
			Format::FormatPosition(2, 12, true).ToString(),
			Format::FormatLapCounter(1234).ToString(),
			Format::FormatResultLaps(1234, 1000).ToString(),
		};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingHudFormatTest,
	"RacingSim.UI.HudFormat",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingHudFormatTest::RunTest(const FString& Parameters)
{
	using namespace HudFormatSpecPrivate;
	using Format = URacingHudFormatLibrary;

	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const double Inf = std::numeric_limits<double>::infinity();

	// -- Speed ------------------------------------------------------------------
	ExpectText(*this, TEXT("Speed: whole units"), Format::FormatSpeed(187.2, true), TEXT("187"));
	ExpectText(*this, TEXT("Speed: halves round up (99.5 -> 100)"), Format::FormatSpeed(99.5, true), TEXT("100"));
	ExpectText(*this, TEXT("Speed: just under a half rounds down"), Format::FormatSpeed(99.49, true), TEXT("99"));
	ExpectText(*this, TEXT("Speed: reverse shows the magnitude"), Format::FormatSpeed(-12.4, true), TEXT("12"));
	ExpectText(*this, TEXT("Speed: zero"), Format::FormatSpeed(0.0, true), TEXT("0"));
	ExpectText(*this, TEXT("Speed: clamped to the display bound"), Format::FormatSpeed(1.0e12, true), TEXT("9999"));
	ExpectText(*this, TEXT("Speed: stale"), Format::FormatSpeed(187.2, false), TEXT("--"));
	ExpectText(*this, TEXT("Speed: NaN reads as absent"), Format::FormatSpeed(NaN, true), TEXT("--"));
	ExpectText(*this, TEXT("Speed: infinity reads as absent"), Format::FormatSpeed(Inf, true), TEXT("--"));

	ExpectText(*this, TEXT("Unit: km/h"), Format::FormatSpeedUnit(ERacingSpeedDisplayUnit::KilometresPerHour), TEXT("km/h"));
	ExpectText(*this, TEXT("Unit: mph"), Format::FormatSpeedUnit(ERacingSpeedDisplayUnit::MilesPerHour), TEXT("mph"));
	ExpectText(*this, TEXT("Unit: m/s"), Format::FormatSpeedUnit(ERacingSpeedDisplayUnit::MetresPerSecond), TEXT("m/s"));

	// -- RPM --------------------------------------------------------------------
	ExpectText(*this, TEXT("RPM: whole"), Format::FormatRPM(6499.6f, true), TEXT("6500"));
	ExpectText(*this, TEXT("RPM: negative reads 0"), Format::FormatRPM(-50.0f, true), TEXT("0"));
	ExpectText(*this, TEXT("RPM: stale"), Format::FormatRPM(6500.0f, false), TEXT("--"));
	ExpectText(*this, TEXT("RPM: NaN reads as absent"),
		Format::FormatRPM(std::numeric_limits<float>::quiet_NaN(), true), TEXT("--"));

	// -- Gear -------------------------------------------------------------------
	ExpectText(*this, TEXT("Gear: reverse"), Format::FormatGear(-1, true), TEXT("R"));
	ExpectText(*this, TEXT("Gear: any reverse is R"), Format::FormatGear(-3, true), TEXT("R"));
	ExpectText(*this, TEXT("Gear: neutral"), Format::FormatGear(0, true), TEXT("N"));
	ExpectText(*this, TEXT("Gear: 4th"), Format::FormatGear(4, true), TEXT("4"));
	ExpectText(*this, TEXT("Gear: stale"), Format::FormatGear(4, false), TEXT("-"));

	// -- Lap time ---------------------------------------------------------------
	ExpectText(*this, TEXT("Lap: M:SS.mmm"), Format::FormatLapTime(83.456, true), TEXT("1:23.456"));
	ExpectText(*this, TEXT("Lap: under a minute"), Format::FormatLapTime(5.007, true), TEXT("0:05.007"));
	ExpectText(*this, TEXT("Lap: zero"), Format::FormatLapTime(0.0, true), TEXT("0:00.000"));
	ExpectText(*this, TEXT("Lap: rounds to nearest ms with carry (59.9996 -> 1:00.000)"),
		Format::FormatLapTime(59.9996, true), TEXT("1:00.000"));
	ExpectText(*this, TEXT("Lap: 59.9994 stays under the minute"), Format::FormatLapTime(59.9994, true), TEXT("0:59.999"));
	ExpectText(*this, TEXT("Lap: minutes are not wrapped at 60"), Format::FormatLapTime(3725.5, true), TEXT("62:05.500"));
	ExpectText(*this, TEXT("Lap: clamped at 999:59.999"), Format::FormatLapTime(1.0e9, true), TEXT("999:59.999"));
	ExpectText(*this, TEXT("Lap: absent"), Format::FormatLapTime(83.456, false), TEXT("-:--.---"));
	ExpectText(*this, TEXT("Lap: negative reads 0"), Format::FormatLapTime(-3.0, true), TEXT("0:00.000"));
	ExpectText(*this, TEXT("Lap: NaN reads as absent"), Format::FormatLapTime(NaN, true), TEXT("-:--.---"));
	ExpectText(*this, TEXT("Lap: infinity reads as absent"), Format::FormatLapTime(Inf, true), TEXT("-:--.---"));

	// -- Delta ------------------------------------------------------------------
	ExpectText(*this, TEXT("Delta: slower is +"), Format::FormatDelta(0.5, true), TEXT("+0.500"));
	ExpectText(*this, TEXT("Delta: faster is -"), Format::FormatDelta(-1.25, true), TEXT("-1.250"));
	ExpectText(*this, TEXT("Delta: zero is +0.000"), Format::FormatDelta(0.0, true), TEXT("+0.000"));
	ExpectText(*this, TEXT("Delta: negative zero is +0.000"), Format::FormatDelta(-0.0, true), TEXT("+0.000"));
	ExpectText(*this, TEXT("Delta: a tiny negative that rounds to 0 is +0.000"), Format::FormatDelta(-0.0004, true), TEXT("+0.000"));
	ExpectText(*this, TEXT("Delta: clamped +"), Format::FormatDelta(5000.0, true), TEXT("+999.999"));
	ExpectText(*this, TEXT("Delta: clamped -"), Format::FormatDelta(-5000.0, true), TEXT("-999.999"));
	TestTrue(TEXT("Delta: empty when absent"), Format::FormatDelta(0.5, false).IsEmpty());
	TestTrue(TEXT("Delta: empty when non-finite"), Format::FormatDelta(NaN, true).IsEmpty());

	// -- Countdown, lap counter, position, result laps ---------------------------
	ExpectText(*this, TEXT("Countdown: 3"), Format::FormatCountdown(3, true), TEXT("3"));
	ExpectText(*this, TEXT("Countdown: GO at 0"), Format::FormatCountdown(0, true), TEXT("GO"));
	ExpectText(*this, TEXT("Countdown: GO below 0"), Format::FormatCountdown(-1, true), TEXT("GO"));
	ExpectText(*this, TEXT("Countdown: clamped"), Format::FormatCountdown(MAX_int32, true), TEXT("3600"));
	TestTrue(TEXT("Countdown: empty when hidden"), Format::FormatCountdown(3, false).IsEmpty());

	ExpectText(*this, TEXT("Lap counter: LAP 1"), Format::FormatLapCounter(1), TEXT("LAP 1"));
	ExpectText(*this, TEXT("Lap counter: LAP - before the line"), Format::FormatLapCounter(0), TEXT("LAP -"));
	ExpectText(*this, TEXT("Lap counter: negative is LAP -"), Format::FormatLapCounter(-2), TEXT("LAP -"));

	ExpectText(*this, TEXT("Position: P n/m"), Format::FormatPosition(2, 8, true), TEXT("P 2/8"));
	TestTrue(TEXT("Position: empty when hidden"), Format::FormatPosition(2, 8, false).IsEmpty());
	TestTrue(TEXT("Position: empty when unclassified"), Format::FormatPosition(0, 8, true).IsEmpty());

	ExpectText(*this, TEXT("Result laps"), Format::FormatResultLaps(3, 2), TEXT("3 LAPS, 2 VALID"));
	ExpectText(*this, TEXT("Result laps: negative reads 0"), Format::FormatResultLaps(-1, -1), TEXT("0 LAPS, 0 VALID"));

	// -- Validity: one distinct, non-empty label per enum value, by reflection ---
	{
		const UEnum* Enum = StaticEnum<ERacingRunValidity>();
		if (TestNotNull(TEXT("ERacingRunValidity is reflected"), Enum))
		{
			TSet<FString> Seen;
			// NumEnums() includes the generated _MAX entry, which is not a value.
			const int32 Count = Enum->NumEnums() - (Enum->ContainsExistingMax() ? 1 : 0);
			TestTrue(TEXT("Validity: at least the eight known values"), Count >= 8);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const ERacingRunValidity Value = static_cast<ERacingRunValidity>(Enum->GetValueByIndex(Index));
				const FString Label = Format::FormatRunValidity(Value).ToString();
				const FString Name = Enum->GetNameStringByIndex(Index);
				TestFalse(FString::Printf(TEXT("Validity %s: label is non-empty"), *Name), Label.IsEmpty());

				bool bAlreadySeen = false;
				Seen.Add(Label, &bAlreadySeen);
				TestFalse(FString::Printf(TEXT("Validity %s: label '%s' is distinct"), *Name, *Label), bAlreadySeen);
			}
		}

		ExpectText(*this, TEXT("Validity: VALID"), Format::FormatRunValidity(ERacingRunValidity::Valid), TEXT("VALID"));
		ExpectText(*this, TEXT("Validity: shortcut"),
			Format::FormatRunValidity(ERacingRunValidity::InvalidShortcut), TEXT("INVALID - SHORTCUT"));
		ExpectText(*this, TEXT("Validity: wrong way"),
			Format::FormatRunValidity(ERacingRunValidity::InvalidReverseCrossing), TEXT("INVALID - WRONG WAY"));
	}

	// -- Rounding helpers the widget keys on ------------------------------------
	TestEqual(TEXT("RoundSpeedForDisplay: non-finite is -1"), Format::RoundSpeedForDisplay(NaN), -1);
	TestEqual(TEXT("RoundLapTimeMilliseconds: non-finite is -1"), Format::RoundLapTimeMilliseconds(Inf), static_cast<int64>(-1));
	TestEqual(TEXT("RoundLapTimeMilliseconds: 59.9996 is 60000"), Format::RoundLapTimeMilliseconds(59.9996), static_cast<int64>(60000));
	TestEqual(TEXT("RoundDeltaMilliseconds: half away from zero"), Format::RoundDeltaMilliseconds(-0.0025), static_cast<int64>(-3));
	TestEqual(TEXT("RoundDeltaMilliseconds: non-finite is 0"), Format::RoundDeltaMilliseconds(NaN), static_cast<int64>(0));

	// -- Reachability: every formatter is a BlueprintPure UFUNCTION --------------
	for (const TCHAR* Name : {
		TEXT("FormatSpeed"), TEXT("FormatSpeedUnit"), TEXT("FormatRPM"), TEXT("FormatGear"),
		TEXT("FormatLapTime"), TEXT("FormatDelta"), TEXT("FormatCountdown"), TEXT("FormatLapCounter"),
		TEXT("FormatPosition"), TEXT("FormatResultLaps"), TEXT("FormatRunValidity") })
	{
		const UFunction* Function = URacingHudFormatLibrary::StaticClass()->FindFunctionByName(FName(Name));
		if (TestNotNull(FString::Printf(TEXT("%s is a UFUNCTION"), Name), Function))
		{
			TestTrue(FString::Printf(TEXT("%s is BlueprintPure"), Name), Function->HasAllFunctionFlags(FUNC_BlueprintPure));
		}
	}

	// -- Culture invariance ------------------------------------------------------
	{
		FInternationalization& I18N = FInternationalization::Get();
		const FString OriginalCulture = I18N.GetCurrentCulture()->GetName();
		const TArray<FString> Invariant = CultureSample();

		if (!I18N.SetCurrentCulture(TEXT("de-DE")))
		{
			AddError(TEXT("Culture de-DE is not available; the culture-invariance check could not run."));
		}
		else
		{
			// Precondition: the culture really is comma-decimal, or the check below proves nothing.
			const FString CultureNumber = FText::AsNumber(1.5).ToString();
			TestTrue(FString::Printf(TEXT("Precondition: de-DE formats 1.5 with a comma (got '%s')"), *CultureNumber),
				CultureNumber.Contains(TEXT(",")));

			const TArray<FString> UnderCulture = CultureSample();
			TestEqual(TEXT("Culture: same number of samples"), UnderCulture.Num(), Invariant.Num());
			for (int32 Index = 0; Index < FMath::Min(UnderCulture.Num(), Invariant.Num()); ++Index)
			{
				TestEqual(FString::Printf(TEXT("Culture: sample %d is identical under de-DE"), Index),
					UnderCulture[Index], Invariant[Index]);
			}
			ExpectText(*this, TEXT("Culture: lap time keeps its full stop under de-DE"),
				Format::FormatLapTime(83.456, true), TEXT("1:23.456"));
		}

		I18N.SetCurrentCulture(OriginalCulture);
		TestEqual(TEXT("Culture: restored"), I18N.GetCurrentCulture()->GetName(), OriginalCulture);
	}

	return true;
}
