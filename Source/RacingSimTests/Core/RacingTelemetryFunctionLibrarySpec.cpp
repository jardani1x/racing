// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingSimBuildId.h"
#include "Core/RacingSimSettings.h"
#include "Core/RacingSimTypes.h"
#include "Core/RacingSimUnits.h"
#include "Core/RacingTelemetry.h"
#include "Core/RacingTelemetryFunctionLibrary.h"
#include "Misc/AutomationTest.h"

#include <limits>

/**
 * UI-001: URacingTelemetryFunctionLibrary, closing CORE-002 finding M-3.
 *
 * Two things are asserted, and only two:
 *
 *   AGREEMENT -- every wrapper returns exactly what the C++ member or RacingSim::Units
 *   function it names returns. The library must not become a second opinion about a rule;
 *   RacingSim.Core.Telemetry and RacingSim.Core.Units remain the tests of the rules.
 *
 *   REACHABILITY -- each wrapper is a real BlueprintPure UFUNCTION. That is the finding:
 *   the members were unreachable from UMG, and a wrapper that compiled without UFUNCTION
 *   would pass every agreement check while closing nothing.
 */

namespace TelemetryFunctionLibrarySpecPrivate
{
	bool TelemetryLibHasPureFunction(FAutomationTestBase& Test, const TCHAR* FunctionName)
	{
		const UFunction* Function = URacingTelemetryFunctionLibrary::StaticClass()->FindFunctionByName(FName(FunctionName));
		const bool bPure = Function != nullptr && Function->HasAnyFunctionFlags(FUNC_BlueprintPure);
		Test.TestTrue(FString::Printf(TEXT("%s is a BlueprintPure UFUNCTION"), FunctionName), bPure);
		return bPure;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingTelemetryFunctionLibraryTest,
	"RacingSim.Core.TelemetryFunctionLibrary",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingTelemetryFunctionLibraryTest::RunTest(const FString& Parameters)
{
	using namespace TelemetryFunctionLibrarySpecPrivate;
	using Lib = URacingTelemetryFunctionLibrary;

	// -- Speed: wrapper == member, for forward, reverse and zero ---------------
	for (const double SpeedCms : { 5000.0, -300.0, 0.0, 12345.678 })
	{
		FRacingVehicleTelemetrySample Sample;
		Sample.ForwardSpeedCms = SpeedCms;

		TestEqual(FString::Printf(TEXT("Kph agrees at %f cm/s"), SpeedCms), Lib::GetForwardSpeedKph(Sample), Sample.GetForwardSpeedKph());
		TestEqual(FString::Printf(TEXT("Mph agrees at %f cm/s"), SpeedCms), Lib::GetForwardSpeedMph(Sample), Sample.GetForwardSpeedMph());
		TestEqual(FString::Printf(TEXT("m/s agrees at %f cm/s"), SpeedCms),
			Lib::GetForwardSpeedMetresPerSecond(Sample), Sample.GetForwardSpeedMetresPerSecond());

		TestEqual(TEXT("Display unit km/h is the km/h accessor"),
			Lib::GetForwardSpeedInDisplayUnit(Sample, ERacingSpeedDisplayUnit::KilometresPerHour), Sample.GetForwardSpeedKph());
		TestEqual(TEXT("Display unit mph is the mph accessor"),
			Lib::GetForwardSpeedInDisplayUnit(Sample, ERacingSpeedDisplayUnit::MilesPerHour), Sample.GetForwardSpeedMph());
		TestEqual(TEXT("Display unit m/s is the m/s accessor"),
			Lib::GetForwardSpeedInDisplayUnit(Sample, ERacingSpeedDisplayUnit::MetresPerSecond), Sample.GetForwardSpeedMetresPerSecond());
	}

	// A corrupt enum value reads as the project default, never as zero speed.
	TestEqual(TEXT("An out-of-range display unit falls back to km/h"),
		Lib::ConvertSpeedCmsToDisplayUnit(5000.0, static_cast<ERacingSpeedDisplayUnit>(200)),
		RacingSim::Units::CmsToKilometresPerHour(5000.0));

	// -- Units: wrapper == RacingSim::Units ------------------------------------
	for (const double Value : { 0.0, 1.0, 98765.4321, -250.0 })
	{
		TestEqual(TEXT("CmToMetres agrees"), Lib::CmToMetres(Value), RacingSim::Units::CmToMetres(Value));
		TestEqual(TEXT("CmToKilometres agrees"), Lib::CmToKilometres(Value), RacingSim::Units::CmToKilometres(Value));
		TestEqual(TEXT("CmsToMetresPerSecond agrees"), Lib::CmsToMetresPerSecond(Value), RacingSim::Units::CmsToMetresPerSecond(Value));
		TestEqual(TEXT("CmsToKilometresPerHour agrees"), Lib::CmsToKilometresPerHour(Value), RacingSim::Units::CmsToKilometresPerHour(Value));
		TestEqual(TEXT("CmsToMilesPerHour agrees"), Lib::CmsToMilesPerHour(Value), RacingSim::Units::CmsToMilesPerHour(Value));
		TestEqual(TEXT("CmsSquaredToG agrees"), Lib::CmsSquaredToG(Value), RacingSim::Units::CmsSquaredToG(Value));
	}

	// -- Staleness: one rule, three ways to ask it -----------------------------
	{
		struct FStaleCase
		{
			double Timestamp;
			double Now;
			double MaxAge;
		};

		const double StaleNaN = std::numeric_limits<double>::quiet_NaN();

		const FStaleCase Cases[] = {
			{ 10.0, 10.0, 0.5 },   // age 0: fresh
			{ 10.0, 10.5, 0.5 },   // age == limit: fresh (strictly greater is stale)
			{ 10.0, 10.6, 0.5 },   // over the limit: stale
			{ 11.0, 10.0, 0.5 },   // stamped in the future (restart): stale
			{ 0.0, 1000.0, 0.0 },  // limit 0 disables the age check
			{ 11.0, 10.0, 0.0 },   // ...but not the future-stamp check
			{ StaleNaN, 10.0, 0.5 },  // non-finite stamp: stale (fail closed)
			{ 10.0, StaleNaN, 0.5 },  // non-finite clock: stale
			{ 10.0, 10.0, StaleNaN }, // non-finite limit: stale, not "check disabled"
		};

		for (const FStaleCase& Case : Cases)
		{
			FRacingTelemetryFrame Frame;
			Frame.TimestampSeconds = Case.Timestamp;

			const bool bMember = Frame.IsStaleAt(Case.Now, Case.MaxAge);
			const FString Label = FString::Printf(TEXT("ts %.2f now %.2f max %.2f"), Case.Timestamp, Case.Now, Case.MaxAge);

			// The agreement checks below would pass with every entry point wrongly "fresh",
			// so pin the non-finite rows to the expected answer independently.
			if (!FMath::IsFinite(Case.Timestamp) || !FMath::IsFinite(Case.Now) || !FMath::IsFinite(Case.MaxAge))
			{
				TestTrue(FString::Printf(TEXT("Non-finite input is stale (%s)"), *Label), bMember);
			}

			TestEqual(FString::Printf(TEXT("IsTelemetryFrameStale == IsStaleAt (%s)"), *Label),
				Lib::IsTelemetryFrameStale(Frame, Case.Now, Case.MaxAge), bMember);
			TestEqual(FString::Printf(TEXT("IsTimestampStale == IsStaleAt (%s)"), *Label),
				Lib::IsTimestampStale(Case.Timestamp, Case.Now, Case.MaxAge), bMember);
			TestEqual(FString::Printf(TEXT("IsTimestampStaleAt == IsStaleAt (%s)"), *Label),
				FRacingTelemetryFrame::IsTimestampStaleAt(Case.Timestamp, Case.Now, Case.MaxAge), bMember);
		}

		TestFalse(TEXT("Age exactly at the limit is fresh"), Lib::IsTimestampStale(10.0, 10.5, 0.5));
		TestTrue(TEXT("A future stamp is stale"), Lib::IsTimestampStale(11.0, 10.0, 0.5));

		TestEqual(TEXT("GetTelemetryStaleAfterSeconds reads the project setting"),
			Lib::GetTelemetryStaleAfterSeconds(), GetDefault<URacingSimSettings>()->TelemetryStaleAfterSeconds);
	}

	// -- Lap timing, including RACE-003 M3 -------------------------------------
	{
		FRacingLapTiming Lap;
		Lap.LapNumber = 2;
		Lap.LapDurationSeconds = 60.0;
		Lap.Validity = ERacingRunValidity::Valid;
		Lap.SectorDurationsSeconds = { 20.0, 20.0, 20.0 };

		TestEqual(TEXT("IsLapComplete agrees"), Lib::IsLapComplete(Lap), Lap.IsComplete());
		TestEqual(TEXT("GetLapSectorTotalSeconds agrees"), Lib::GetLapSectorTotalSeconds(Lap), Lap.GetSectorTotalSeconds());
		TestEqual(TEXT("AreLapSectorsConsistent agrees for three splits on a three-sector track"),
			Lib::AreLapSectorsConsistent(Lap, 3), Lap.AreSectorsConsistent(0.001, 3));
		TestTrue(TEXT("...and that answer is true"), Lib::AreLapSectorsConsistent(Lap, 3));

		// M3: splits withheld. The C++ default (no count) says "consistent"; the wrapper,
		// which has no default for the count, says what a three-sector track needs said.
		FRacingLapTiming Withheld = Lap;
		Withheld.SectorDurationsSeconds.Reset();

		// Precondition, not a behaviour under test: it documents the member's vacuous-true
		// answer so the wrapper assertion that follows is shown to differ from it.
		TestTrue(TEXT("M3 precondition: the count-less member reads a withheld split set as consistent"),
			Withheld.AreSectorsConsistent());
		TestFalse(TEXT("M3: the wrapper, given the track's three sectors, refuses it"),
			Lib::AreLapSectorsConsistent(Withheld, 3));
		TestTrue(TEXT("...while a genuinely sectorless track (count 0) is still consistent"),
			Lib::AreLapSectorsConsistent(Withheld, 0));

		FRacingLapTiming Drifted = Lap;
		Drifted.SectorDurationsSeconds = { 20.0, 20.0, 20.01 };
		TestFalse(TEXT("A 10 ms drift fails at the default 1 ms tolerance"), Lib::AreLapSectorsConsistent(Drifted, 3));
		TestTrue(TEXT("...and passes when the caller widens it"), Lib::AreLapSectorsConsistent(Drifted, 3, 0.02));

		const FRacingLapTiming Running;
		TestEqual(TEXT("IsLapComplete agrees on a default lap"), Lib::IsLapComplete(Running), Running.IsComplete());
	}

	// -- Versions --------------------------------------------------------------
	{
		FRacingContentVersion Empty;
		FRacingContentVersion Populated;
		Populated.AssetId = FName(TEXT("Track.Test.Library"));
		Populated.SchemaVersion = 3;
		Populated.ContentHash = 0x1234ABCD;

		TestEqual(TEXT("IsContentVersionPopulated agrees on an empty version"),
			Lib::IsContentVersionPopulated(Empty), Empty.IsPopulated());
		TestEqual(TEXT("IsContentVersionPopulated agrees on a populated version"),
			Lib::IsContentVersionPopulated(Populated), Populated.IsPopulated());
		TestEqual(TEXT("ContentVersionToString agrees"), Lib::ContentVersionToString(Populated), Populated.ToString());

		const FRacingSimVersionStamp Stamp;
		TestEqual(TEXT("VersionStampToString agrees"), Lib::VersionStampToString(Stamp), Stamp.ToString());
	}

	// -- Reachability: the finding itself --------------------------------------
	for (const TCHAR* Name : {
		TEXT("GetForwardSpeedKph"), TEXT("GetForwardSpeedMph"), TEXT("GetForwardSpeedMetresPerSecond"),
		TEXT("GetForwardSpeedInDisplayUnit"), TEXT("ConvertSpeedCmsToDisplayUnit"),
		TEXT("CmToMetres"), TEXT("CmToKilometres"), TEXT("CmsToMetresPerSecond"),
		TEXT("CmsToKilometresPerHour"), TEXT("CmsToMilesPerHour"), TEXT("CmsSquaredToG"),
		TEXT("IsTelemetryFrameStale"), TEXT("IsTimestampStale"), TEXT("GetTelemetryStaleAfterSeconds"),
		TEXT("IsLapComplete"), TEXT("GetLapSectorTotalSeconds"), TEXT("AreLapSectorsConsistent"),
		TEXT("IsContentVersionPopulated"), TEXT("ContentVersionToString"), TEXT("VersionStampToString") })
	{
		TelemetryLibHasPureFunction(*this, Name);
	}

	return true;
}
