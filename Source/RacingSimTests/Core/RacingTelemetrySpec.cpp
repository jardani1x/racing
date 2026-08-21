// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingSimTypes.h"
#include "Core/RacingTelemetry.h"
#include "Misc/AutomationTest.h"

/**
 * CORE-002: telemetry contracts.
 *
 * Most of this file is data, but two behaviours carry correctness weight and are
 * tested here rather than at the point of use:
 *
 *   AreSectorsConsistent -- catches a gate that fired twice or out of order, by
 *   checking the sectors against the lap from the same clock.
 *
 *   IsStaleAt -- decides whether the HUD may show a frame. The restart case
 *   (a frame stamped in the future, because the clock was reset underneath a
 *   consumer) is the one that would otherwise leave the previous run's numbers
 *   on screen looking live.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingTelemetryContractTest,
	"RacingSim.Core.Telemetry",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingTelemetryContractTest::RunTest(const FString& Parameters)
{
	// -- Defaults are the "nothing has happened yet" state -------------------
	{
		const FRacingVehicleTelemetrySample Sample;
		TestNearlyEqual(TEXT("Default sample has no timestamp"), Sample.TimestampSeconds, 0.0, 0.0);
		TestEqual(TEXT("Default gear is neutral"), Sample.GearIndex, 0);
		TestEqual(TEXT("Default input device is Unknown"), Sample.InputDeviceType, ERacingInputDeviceType::Unknown);

		const FRacingProgressSample Progress;
		TestEqual(TEXT("No lap before the first valid crossing"), Progress.LapNumber, 0);
		TestEqual(TEXT("No checkpoint crossed yet"), Progress.LastCheckpointIndex, INDEX_NONE);
		TestEqual(TEXT("Not yet classified"), Progress.RacePosition, 0);

		const FRacingLapTiming Lap;
		TestEqual(TEXT("A default lap is Pending"), Lap.Validity, ERacingRunValidity::Pending);
		TestFalse(TEXT("A default lap is not complete"), Lap.IsComplete());

		const FRacingTelemetryFrame Frame;
		TestFalse(TEXT("A default frame has no delta"), Frame.bHasDelta);
	}

	// -- Speed conversions at the sample level -------------------------------
	// Same policy as RacingSim.Core.Units, exercised through the struct the HUD
	// actually reads. Storage stays cm/s; only the accessors convert.
	{
		FRacingVehicleTelemetrySample Sample;
		Sample.ForwardSpeedCms = 5000.0; // 50 m/s == 180 km/h
		TestNearlyEqual(TEXT("5000 cm/s reads 180 km/h"), Sample.GetForwardSpeedKph(), 180.0, 1.0e-9);
		TestNearlyEqual(TEXT("5000 cm/s reads 50 m/s"), Sample.GetForwardSpeedMetresPerSecond(), 50.0, 1.0e-9);
		TestNearlyEqual(TEXT("5000 cm/s reads 111.847 mph"), Sample.GetForwardSpeedMph(), 180000.0 / 1609.344, 1.0e-9);

		// Reversing. The sign must reach the HUD, not be clamped away.
		Sample.ForwardSpeedCms = -300.0;
		TestTrue(TEXT("Reversing reads as negative speed"), Sample.GetForwardSpeedKph() < 0.0);
	}

	// -- Lap completion ------------------------------------------------------
	{
		FRacingLapTiming Lap;
		Lap.LapNumber = 1;
		Lap.LapDurationSeconds = 83.456;
		TestFalse(TEXT("A timed but undecided lap is not complete"), Lap.IsComplete());

		Lap.Validity = ERacingRunValidity::Valid;
		TestTrue(TEXT("A timed, decided lap is complete"), Lap.IsComplete());

		// An invalidated lap is still a completed lap: it happened, it is timed,
		// and the result screen shows it struck through rather than missing.
		Lap.Validity = ERacingRunValidity::InvalidShortcut;
		TestTrue(TEXT("An invalidated lap is still complete"), Lap.IsComplete());

		// Lap 0 is the out lap. It has no lap time by definition.
		FRacingLapTiming OutLap = Lap;
		OutLap.LapNumber = 0;
		TestFalse(TEXT("The out lap is never complete"), OutLap.IsComplete());
	}

	// -- Sector consistency ---------------------------------------------------
	{
		FRacingLapTiming Lap;
		Lap.LapNumber = 1;
		Lap.Validity = ERacingRunValidity::Valid;
		Lap.SectorDurationsSeconds = { 28.100, 31.250, 24.106 };
		Lap.LapDurationSeconds = 83.456;

		TestNearlyEqual(TEXT("Sectors sum to the lap"), Lap.GetSectorTotalSeconds(), 83.456, 1.0e-9);
		TestTrue(TEXT("Consistent sectors pass"), Lap.AreSectorsConsistent());

		// A missed gate: the middle sector was never closed, so two sectors are
		// reported for a three-sector lap and the sum falls short.
		{
			FRacingLapTiming Missed = Lap;
			Missed.SectorDurationsSeconds = { 28.100, 55.356 - 24.106 };
			TestFalse(TEXT("A missed gate is detected"), Missed.AreSectorsConsistent());
		}

		// A double-triggered gate: one sector is zero-length. Caught by the
		// per-sector check even though the total still adds up -- which is the
		// whole reason the per-sector check exists.
		{
			FRacingLapTiming Doubled = Lap;
			Doubled.SectorDurationsSeconds = { 28.100, 0.0, 31.250, 24.106 };
			TestFalse(TEXT("A zero-length sector is rejected"), Doubled.AreSectorsConsistent());
		}

		// Oscillation at a gate producing a negative sector: impossible from a
		// monotonic clock, so it is a bug and must not be smoothed over.
		{
			FRacingLapTiming Negative = Lap;
			Negative.SectorDurationsSeconds = { 28.100, -1.0, 32.250, 24.106 };
			TestFalse(TEXT("A negative sector is rejected"), Negative.AreSectorsConsistent());
		}

		// -- NO SECTORS AT ALL: VACUOUSLY TRUE -------------------------------
		//
		// THIS ASSERTION WAS INVERTED AT RACE-003, deliberately and under an approved
		// ticket change (RACE-002 finding L2, routed to RACE-003's acceptance criteria).
		// It previously read "A lap with no sectors is not consistent", asserting false.
		//
		// That was wrong, and the way it was wrong is instructive: an empty sector table is
		// a LEGAL track configuration -- URaceLapTracker::ConfigureTrack accepts one and
		// RacingSim.Race.LapTrackerConfiguration asserts that such a track still counts
		// laps -- so every lap on a sectorless track reported "inconsistent" to any
		// consumer that checked. The old answer confused two questions: "do the recorded
		// sectors account for the lap" (this function's) and "should there have been
		// sectors at all" (the track's). With nothing recorded, nothing fails to account
		// for the lap.
		{
			FRacingLapTiming NoSectors = Lap;
			NoSectors.SectorDurationsSeconds.Reset();
			TestTrue(TEXT("A lap carrying no splits is VACUOUSLY consistent: nothing fails to add up"),
				NoSectors.AreSectorsConsistent());

			// ...and the stronger question is still available, which is the whole reason
			// the answer above is safe to give. A caller that knows the track authored
			// three sectors gets false for a lap carrying none -- that is
			// SectorDurationsSeconds' "shape 3", splits withheld, and it is a real fault.
			TestFalse(TEXT("...but NOT when the caller states the track authored 3 sectors"),
				NoSectors.AreSectorsConsistent(0.001, 3));
			TestTrue(TEXT("...while a genuinely sectorless track passes the same check with a count of 0"),
				NoSectors.AreSectorsConsistent(0.001, 0));
		}

		// -- ExpectedSectorCount catches a set of the wrong LENGTH that still sums --
		//
		// The case the sum alone cannot see. Splitting one sector into two halves keeps the
		// total exactly right and every split positive, so the value check and the total
		// check both pass; only the count disagrees. Without the parameter a consumer had
		// no way to ask.
		{
			FRacingLapTiming Resplit = Lap;
			Resplit.SectorDurationsSeconds = { 28.100, 15.625, 15.625, 24.106 };
			TestTrue(TEXT("Four splits that still sum to the lap pass the unqualified check"),
				Resplit.AreSectorsConsistent());
			TestFalse(TEXT("...and fail once the caller says the track has three sectors"),
				Resplit.AreSectorsConsistent(0.001, 3));
			TestTrue(TEXT("...while the correct three-split set passes the qualified check"),
				Lap.AreSectorsConsistent(0.001, 3));
		}

		// An incomplete lap is still refused even when the count matches, because the lap
		// duration it would be compared against is not final. The count check must not
		// become a way round the completeness rule.
		{
			FRacingLapTiming RunningWithCount = Lap;
			RunningWithCount.Validity = ERacingRunValidity::Pending;
			TestFalse(TEXT("A running lap is refused even when the split count is right"),
				RunningWithCount.AreSectorsConsistent(0.001, 3));
		}

		// An incomplete lap cannot be consistent -- the lap time is not final yet.
		{
			FRacingLapTiming Running = Lap;
			Running.Validity = ERacingRunValidity::Pending;
			TestFalse(TEXT("A running lap is not checked for consistency"), Running.AreSectorsConsistent());
		}

		// Tolerance: accumulated floating-point noise within 1 ms passes; a real
		// 10 ms discrepancy does not.
		{
			FRacingLapTiming Noisy = Lap;
			Noisy.LapDurationSeconds = 83.456 + 0.0005;
			TestTrue(TEXT("Sub-millisecond noise passes"), Noisy.AreSectorsConsistent());

			Noisy.LapDurationSeconds = 83.456 + 0.010;
			TestFalse(TEXT("A 10 ms discrepancy fails"), Noisy.AreSectorsConsistent());
		}
	}

	// -- Staleness ------------------------------------------------------------
	{
		FRacingTelemetryFrame Frame;
		Frame.TimestampSeconds = 100.0;

		TestFalse(TEXT("A fresh frame is not stale"), Frame.IsStaleAt(100.0, 0.5));
		TestFalse(TEXT("A frame inside the window is not stale"), Frame.IsStaleAt(100.4, 0.5));
		TestFalse(TEXT("Exactly at the window is not stale"), Frame.IsStaleAt(100.5, 0.5));
		TestTrue(TEXT("Past the window is stale"), Frame.IsStaleAt(100.51, 0.5));
		TestTrue(TEXT("A long-dead producer is stale"), Frame.IsStaleAt(1000.0, 0.5));

		// Restart / clock reset: the frame is stamped in the future. Must be
		// treated as stale so the HUD blanks instead of showing the last run.
		TestTrue(TEXT("A frame from the future is stale (clock reset)"), Frame.IsStaleAt(0.0, 0.5));
		TestTrue(TEXT("Even a slightly future frame is stale"), Frame.IsStaleAt(99.999, 0.5));

		// MaxAge <= 0 disables the check, matching the settings convention that
		// 0 means "off" rather than "everything is stale".
		TestFalse(TEXT("Zero max age disables the staleness check"), Frame.IsStaleAt(1000.0, 0.0));
		TestFalse(TEXT("Negative max age disables the staleness check"), Frame.IsStaleAt(1000.0, -1.0));

		// ...but a future frame is still stale even with the check disabled: that
		// is a restart, not a staleness policy question.
		TestTrue(TEXT("A future frame is stale even when the check is disabled"), Frame.IsStaleAt(0.0, 0.0));
	}

	return true;
}
