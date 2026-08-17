// Copyright RacingSim. All Rights Reserved.

#include "Race/RaceClock.h"
#include "Misc/AutomationTest.h"

// For quiet_NaN/infinity. UE has no portable NaN constant for double, and
// synthesising one arithmetically (0.0/0.0, sqrt(-1)) risks a compiler folding it
// or a floating-point exception, depending on flags.
#include <limits>

/**
 * RACE-001: FRaceClock, tested in isolation.
 *
 * Gate B, verbatim: "timer is monotonic and independent of render frame rate."
 * Those are two claims and they are proved separately here:
 *
 *   independence  -- sampling the same interval 1000 times and 3 times must give
 *                    BIT-IDENTICAL results, and a 4-second hitch must be reported
 *                    as 4 seconds. An accumulator fails both: the first by
 *                    accumulating rounding error per sample, the second because the
 *                    engine clamps DeltaTime and the lost time is simply gone.
 *   monotonicity  -- a deliberately backwards-stepping time source must never
 *                    produce a decreasing reading, and must not be able to shrink a
 *                    frozen result at the finish.
 *
 * No UObject, no world, no GC: this file exercises the arithmetic that everything
 * else rests on, so a failure here is unambiguous.
 *
 * SmokeFilter is mandatory, not stylistic. Docs/Environment.md records that the
 * project's only automation command is `Automation RunFilter Smoke`, and that a
 * test outside that filter was previously written and never ran while the suite
 * reported green.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceClockMonotonicTest,
	"RacingSim.Race.ClockMonotonic",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceClockMonotonicTest::RunTest(const FString& Parameters)
{
	// An arbitrary non-zero epoch. Deliberately not 0.0: a clock that accidentally
	// returned the raw timestamp instead of the difference would still read "0" at
	// the start if the epoch were 0, and the bug would pass.
	constexpr double Epoch = 987654.5;

	// -- A fresh clock reports nothing ---------------------------------------
	{
		FRaceClock Clock;
		TestFalse(TEXT("Fresh clock is not running"), Clock.IsRunning());
		TestFalse(TEXT("Fresh clock has not started"), Clock.HasStarted());
		TestEqual(TEXT("Fresh clock peeks 0"), Clock.Peek(), 0.0, 0.0);
		TestEqual(TEXT("Sampling a never-started clock returns 0 regardless of the reading"),
			Clock.Sample(Epoch + 1000.0), 0.0, 0.0);
	}

	// -- Start and Stop are idempotent ---------------------------------------
	{
		FRaceClock Clock;
		TestTrue(TEXT("First Start starts the clock"), Clock.Start(Epoch));
		TestFalse(TEXT("Second Start is a no-op"), Clock.Start(Epoch + 5.0));

		// The load-bearing assertion: the ignored second Start must not have moved
		// the origin. If it had, five seconds of race time would have vanished.
		TestEqual(TEXT("A doubled Start does not discard elapsed time"),
			Clock.Sample(Epoch + 10.0), 10.0, 0.0);

		TestTrue(TEXT("First Stop stops the clock"), Clock.Stop(Epoch + 10.0));
		TestFalse(TEXT("Second Stop is a no-op"), Clock.Stop(Epoch + 60.0));
		TestEqual(TEXT("A doubled Stop cannot extend a frozen result"),
			Clock.Sample(Epoch + 600.0), 10.0, 0.0);
	}

	// -- Independence of sample count (i.e. of frame rate) -------------------
	{
		constexpr double Interval = 10.0;

		// ~100 fps: 1000 samples across the interval.
		FRaceClock HighRate;
		HighRate.Start(Epoch);
		for (int32 Step = 1; Step <= 1000; ++Step)
		{
			HighRate.Sample(Epoch + (Interval * Step) / 1000.0);
		}
		const double HighRateElapsed = HighRate.Sample(Epoch + Interval);

		// ~0.3 fps with wildly uneven frames, including a repeated timestamp (two
		// samples inside the same frame) and a long gap.
		FRaceClock LowRate;
		LowRate.Start(Epoch);
		LowRate.Sample(Epoch + 0.5);
		LowRate.Sample(Epoch + 0.5);
		LowRate.Sample(Epoch + 9.0);
		const double LowRateElapsed = LowRate.Sample(Epoch + Interval);

		TestEqual(TEXT("Elapsed is exactly the timestamp difference at 1000 samples"),
			HighRateElapsed, Interval, 0.0);
		TestEqual(TEXT("Elapsed is exactly the timestamp difference at 4 samples"),
			LowRateElapsed, Interval, 0.0);

		// Zero tolerance on purpose. "Independent of render frame rate" is a claim
		// about equality, not about being close enough.
		TestEqual(TEXT("Frame rate does not change the measured duration at all"),
			HighRateElapsed, LowRateElapsed, 0.0);
	}

	// -- A hitch is reported, not swallowed ----------------------------------
	{
		FRaceClock Clock;
		Clock.Start(Epoch);
		Clock.Sample(Epoch + 0.016);

		// Four seconds of real time with no sample in between: a shader-compile
		// stall, a GC spike, a stream hiccup. An accumulator fed by a clamped
		// DeltaTime would report ~0.033 s here and the driver would be handed four
		// free seconds.
		//
		// Tolerance is 1e-9, not 0.0: unlike the whole/half-integer offsets above
		// (10.0, 0.5, 9.0 -- all exactly representable in binary, so Epoch + offset
		// round-trips exactly), 4.016 has no exact binary representation. Its own
		// rounding and the rounding of (Epoch + 4.016) - Epoch take different paths
		// and can land a few ULPs apart at Epoch's ~1e6 magnitude (ULP there is
		// ~1.2e-10). This is rounding-path divergence, not a monotonicity or
		// frame-rate defect -- the property under test ("independent of frame rate")
		// is still exact; only "equal to a double literal typed in the test" is not.
		TestEqual(TEXT("A 4-second stall is counted in full"), Clock.Sample(Epoch + 4.016), 4.016, 1e-9);
	}

	// -- Monotonic under a backwards-stepping source -------------------------
	{
		FRaceClock Clock;
		Clock.Start(Epoch);

		const double Readings[] = { Epoch + 1.0, Epoch + 5.0, Epoch + 3.0, Epoch + 4.0, Epoch - 100.0, Epoch + 10.0 };

		double Previous = 0.0;
		for (const double Reading : Readings)
		{
			const double Elapsed = Clock.Sample(Reading);
			TestTrue(
				*FString::Printf(TEXT("Elapsed never decreases (reading %f gave %f after %f)"), Reading, Elapsed, Previous),
				Elapsed >= Previous);
			Previous = Elapsed;
		}

		TestEqual(TEXT("The final forward reading still measures correctly"), Previous, 10.0, 0.0);
	}

	// -- A backwards step cannot shrink the frozen result --------------------
	{
		FRaceClock Clock;
		Clock.Start(Epoch);
		TestEqual(TEXT("Runner was told 42 s"), Clock.Sample(Epoch + 42.0), 42.0, 0.0);

		// The source jumps back, and the finish happens on that bad reading. The
		// frozen result must be the largest time the runner was ever shown, not a
		// flattering 41.
		Clock.Stop(Epoch + 41.0);
		TestEqual(TEXT("Stop freezes at the high-water mark, never below it"),
			Clock.Sample(Epoch + 41.0), 42.0, 0.0);
	}

	// -- Reset is the one legitimate way back to zero ------------------------
	{
		FRaceClock Clock;
		Clock.Start(Epoch);
		Clock.Sample(Epoch + 30.0);
		Clock.Reset();

		TestFalse(TEXT("Reset clock is not running"), Clock.IsRunning());
		TestFalse(TEXT("Reset clock has not started"), Clock.HasStarted());
		TestEqual(TEXT("Reset drops the ratchet to zero"), Clock.Peek(), 0.0, 0.0);
		TestEqual(TEXT("Reset clock reports 0 even far in the future"), Clock.Sample(Epoch + 9000.0), 0.0, 0.0);

		// Reset is idempotent too -- restart paths call it unconditionally.
		Clock.Reset();
		TestEqual(TEXT("Reset twice is still zero"), Clock.Sample(Epoch + 9000.0), 0.0, 0.0);
	}

	// -- Non-finite input is refused, not propagated -------------------------
	{
		// A NaN lap time compares false against everything, so it reads as "nobody
		// was ever fastest" rather than as a fault. Refusing it is the difference
		// between a loud failure and a leaderboard that quietly stops working.
		AddExpectedError(
			TEXT("FRaceClock::Start rejected a non-finite timestamp"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			/*IsRegex=*/false);

		FRaceClock Clock;
		TestFalse(TEXT("Start refuses a NaN timestamp"), Clock.Start(std::numeric_limits<double>::quiet_NaN()));
		TestFalse(TEXT("Refused Start left the clock stopped"), Clock.IsRunning());
		TestFalse(TEXT("Refused Start left the clock unstarted"), Clock.HasStarted());

		Clock.Start(Epoch);
		Clock.Sample(Epoch + 7.0);
		const double AfterBadSample = Clock.Sample(std::numeric_limits<double>::infinity());
		TestEqual(TEXT("A non-finite reading does not corrupt the elapsed time"), AfterBadSample, 7.0, 0.0);
	}

	return true;
}

/**
 * The production time source itself. Kept separate because, unlike everything
 * above, it is the one test here that depends on real hardware behaviour.
 *
 * It deliberately does NOT assert a rate or a duration -- a wall-clock assertion in
 * an automation suite that shares a memory-constrained laptop with a build
 * (BLOCKER-003) is a future intermittent failure. It asserts only the two
 * properties the clock actually relies on: finite, and non-decreasing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacePlatformTimeSourceTest,
	"RacingSim.Race.PlatformTimeSource",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacePlatformTimeSourceTest::RunTest(const FString& Parameters)
{
	double Previous = RacingSim::Race::PlatformMonotonicSeconds();
	TestTrue(TEXT("Platform monotonic time is finite"), FMath::IsFinite(Previous));

	bool bAdvancedAtLeastOnce = false;
	for (int32 Iteration = 0; Iteration < 2000; ++Iteration)
	{
		const double Current = RacingSim::Race::PlatformMonotonicSeconds();
		if (!TestTrue(TEXT("Platform monotonic time never goes backwards"), Current >= Previous))
		{
			return false;
		}
		bAdvancedAtLeastOnce |= (Current > Previous);
		Previous = Current;
	}

	// If this ever fails, the source has no usable resolution on this machine and
	// every lap would time as 0.000 -- worth catching here rather than in a race.
	TestTrue(TEXT("Platform monotonic time advances at all across 2000 reads"), bAdvancedAtLeastOnce);

	return true;
}
