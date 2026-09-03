// Copyright RacingSim. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"

#include "Vehicle/RacingVehiclePawn.h"
#include "Vehicle/VehicleFailureDetection.h"
#include "Vehicle/VehicleTelemetryTypes.h"

#include "Vehicle/VehicleManoeuvreFixture.h"

#include "RacingSimTestsLog.h"

/**
 * VEH-006 soak: thirty minutes of simulated driving in one continuous world.
 *
 * ---------------------------------------------------------------------------
 * What this test is for, and what it deliberately is not
 * ---------------------------------------------------------------------------
 *
 * The manoeuvre specs in this directory each drive for a few seconds and assert a
 * NUMBER -- a final speed, a stopping distance, a yaw sign. This one asserts nothing
 * about vehicle behaviour at all. It asserts that behaviour does not DRIFT: that the
 * same car, stepped a hundred thousand times without a teardown in between, still
 * produces finite telemetry, still reports no failure, still sits somewhere sane, and
 * has not leaked its way through the process's memory in the meantime.
 *
 * Those are four different classes of slow fault and none of them is visible in a
 * three-second test:
 *
 *   - a solver that accumulates error until a value goes non-finite;
 *   - a detector or a physics path that degrades into a permanent fault state;
 *   - a car that walks away from where it was driven, i.e. an integration drift;
 *   - a per-tick allocation that nothing ever frees.
 *
 * CLAUDE.md's coding rules name the last one directly ("avoid per-frame allocations"),
 * and a per-frame allocation is exactly the kind of defect that a short test cannot see
 * and a long session cannot miss.
 *
 * ---------------------------------------------------------------------------
 * Why it is not on the normal gate
 * ---------------------------------------------------------------------------
 *
 * StressFilter, not ProductFilter, and it has its own script
 * (Scripts/Test/Run-Soak.ps1) and its own report directory. VEH-006's own design section
 * settled this before implementation: "a 30-minute soak on the per-ticket gate makes the
 * gate unusable". The per-ticket gate must stay fast enough that nobody is tempted to
 * skip it, and this test is the one thing in the suite that cannot be made fast -- its
 * whole value is the duration.
 *
 * StressFilter also keeps it out of RunFilter Product, which matters for a second
 * reason: Run-AutomationFilter.ps1's header records that a full Product collection run
 * cannot complete on this machine, so the Product gate is named with -TestNames. A soak
 * hiding inside that list would make every ordinary gate run take half an hour.
 *
 * ---------------------------------------------------------------------------
 * Units
 * ---------------------------------------------------------------------------
 *
 * SIMULATED time is seconds of stepped world time -- StepSeconds multiplied by the step
 * count, exact by construction. WALL-CLOCK time is FPlatformTime::Seconds and is
 * reported but never asserted on: it is a property of this machine, not of the code.
 * Distances are centimetres. Memory is bytes internally and reported in mebibytes.
 */

namespace
{
	/**
	 * The fixed step. Matches FVehicleManoeuvreFixture::DefaultStepSeconds and every
	 * other manoeuvre spec, so a fault that appears here can be reproduced by a short
	 * test without changing the integration rate first.
	 */
	constexpr float SoakStepSeconds = 1.0f / 60.0f;

	/**
	 * Simulated duration, seconds. 1800 s == 30 minutes, which is the ticket's own
	 * figure ("at least 30 minutes of simulated time at a fixed step").
	 */
	constexpr double SoakSimulatedSeconds = 1800.0;

	/**
	 * 108,000 steps. Derived, not restated, so the two numbers cannot disagree.
	 *
	 * Rounded UP, and that is not a nicety. SoakStepSeconds is a float, and 1.0f/60.0f is
	 * 0.0166666667... -- fractionally ABOVE the exact 1/60 -- so the quotient here is
	 * 107999.99, and truncating it yields 107,999 steps covering 1799.98 s. That is short
	 * of the required 1800 s, and it failed the first soak run:
	 *
	 *   Expected 'The soak covered at least 1800.0 s of simulated time, got 1800.0 s'
	 *
	 * Both sides printed as 1800.0 because the message rounded to one decimal while the
	 * comparison did not, which is why the assertion below now prints four. Adding one step
	 * costs 1/60 s of simulated time and guarantees the run always covers the requirement.
	 */
	constexpr int32 SoakSteps =
		static_cast<int32>(SoakSimulatedSeconds / static_cast<double>(SoakStepSeconds)) + 1;

	/**
	 * How often the soak stops to inspect telemetry, in steps. 600 == every 10 simulated
	 * seconds, so 180 inspections across the run.
	 *
	 * NOT every step, and the reason is that inspecting every step would make this test
	 * measure the inspection rather than the driving: reading the snapshot and formatting
	 * a failure message a hundred thousand times costs more than the physics does. Ten
	 * seconds is short enough that a fault is localised to a known ten-second window and
	 * long enough that the check is free relative to the work.
	 *
	 * A fault that appears and clears entirely inside one window would be missed by the
	 * sampling -- but not by the test: ARacingVehiclePawn logs any failure at Error
	 * severity the moment its flags change (edge-triggered, see the pawn's capture
	 * function), and the automation framework fails a test on any unexpected Error. The
	 * sampled checks below are the diagnostic; the pawn's own logging is the net.
	 */
	constexpr int32 SoakInspectionIntervalSteps = 600;

	/**
	 * Position bound, centimetres from the origin.
	 *
	 * The fixture's ground slab is 200 m square (GroundHalfSizeCm = 10000), so a car
	 * outside this bound is heading for the edge, at which point the suspension traces
	 * find nothing, the car falls, and every later assertion in this test would be
	 * measuring a fall rather than a drive. 8000 cm leaves 20 m of slab in every
	 * direction as headroom.
	 *
	 * The manoeuvre below is a constant-radius turn precisely so this bound is meaningful:
	 * a straight-line soak would leave the slab in under a minute, and shortening the
	 * soak to fit the slab would defeat its purpose.
	 */
	constexpr double SoakPositionBoundCm = 8000.0;

	/**
	 * Throttle held for the whole soak. Part throttle, not full.
	 *
	 * Full throttle against full lock spirals outward as speed rises, and the radius it
	 * settles at is a property of the tune -- so a tune change would move the car off the
	 * slab and this test would fail for a reason that has nothing to do with what it
	 * measures. A modest throttle keeps the steady-state radius well inside the bound
	 * with margin to spare, and the run reports the maximum distance actually reached so
	 * the margin is a measured number rather than a hope.
	 */
	constexpr double SoakThrottle = 0.3;

	/** Full right lock, held. Positive is right, matching FVehicleInputCommand's sign. */
	constexpr double SoakSteer = 1.0;

	/**
	 * Memory-growth ceiling, bytes. 64 MiB across 108,000 steps.
	 *
	 * MEASURED AS PROCESS-RESIDENT MEMORY (FPlatformMemory::GetStats().UsedPhysical),
	 * which is noisy: it includes every other subsystem in the editor process, and the
	 * allocator is free to hold freed pages rather than return them. So this is a
	 * CEILING, chosen to catch the fault class that matters -- a genuine per-tick leak --
	 * and not a tight bound on allocator behaviour. At 108,000 steps, 64 MiB is about 620
	 * bytes per step: a per-tick allocation of any realistic size blows straight through
	 * it, while ordinary allocator slack does not come close.
	 *
	 * Both endpoints are taken after a full, purging garbage collection, so UObject
	 * churn that a GC would have reclaimed anyway is not counted as a leak.
	 */
	constexpr uint64 SoakMemoryCeilingBytes = 64ull * 1024ull * 1024ull;

	/** Bytes to mebibytes, for reporting only. */
	double ToMebibytes(const uint64 Bytes)
	{
		return static_cast<double>(Bytes) / (1024.0 * 1024.0);
	}

	/** Signed bytes to mebibytes, so a run whose resident memory FELL reports a negative. */
	double SignedToMebibytes(const int64 Bytes)
	{
		return static_cast<double>(Bytes) / (1024.0 * 1024.0);
	}

	/**
	 * Resident memory after a purging GC.
	 *
	 * CollectGarbage(RF_NoFlags, true) is the full blocking collect-and-purge; without
	 * the purge the objects are marked unreachable but their memory is not yet returned,
	 * so a before/after pair taken without it measures GC scheduling rather than growth.
	 */
	uint64 CollectAndMeasureResidentBytes()
	{
		CollectGarbage(RF_NoFlags, /*bFullPurge*/ true);
		return FPlatformMemory::GetStats().UsedPhysical;
	}
}

/**
 * The soak itself.
 *
 * Telemetry is ON (the fixture defaults it off), because half of what this test watches
 * -- finite samples and a silent failure detector -- only exists when the pawn is
 * capturing. No AddExpectedError: nothing here is supposed to log an error, and an
 * unexpected one failing the test is the point.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleSoakTest,
	"RacingSim.Vehicle.Soak.ThirtyMinuteDrive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::StressFilter)

bool FRacingSimVehicleSoakTest::RunTest(const FString&)
{
	FVehicleManoeuvreFixture Fixture;
	if (!Fixture.Setup(*this, /*bEnableTelemetry*/ true))
	{
		return false;
	}

	ARacingVehiclePawn* Pawn = Fixture.GetPawn();

	const uint64 ResidentBeforeBytes = CollectAndMeasureResidentBytes();
	const double WallClockStartSeconds = FPlatformTime::Seconds();

	const FVehicleInputRawSample Sample =
		FVehicleManoeuvreFixture::ThrottleSample(SoakThrottle, SoakSteer);

	double MaxDistanceCm = 0.0;
	int32 InspectionCount = 0;
	int32 StepsCompleted = 0;

	// Driven in inspection-sized blocks rather than one call, so a fault is reported with
	// the step index it appeared at. A single Drive(SoakSteps) would report only that
	// something went wrong somewhere in half an hour.
	for (int32 BlockStart = 0; BlockStart < SoakSteps; BlockStart += SoakInspectionIntervalSteps)
	{
		const int32 BlockSteps = FMath::Min(SoakInspectionIntervalSteps, SoakSteps - BlockStart);
		Fixture.Drive(Sample, BlockSteps, SoakStepSeconds);
		StepsCompleted += BlockSteps;

		const double SimulatedSecondsSoFar = static_cast<double>(StepsCompleted) * SoakStepSeconds;

		const FVehicleTelemetrySnapshot Snapshot = Pawn->GetLastTelemetrySnapshot();
		if (!Snapshot.bIsValid)
		{
			AddError(FString::Printf(
				TEXT("No telemetry sample had been taken by step %d (t=%.2f s simulated); ")
				TEXT("the pawn stopped capturing mid-soak."),
				StepsCompleted, SimulatedSecondsSoFar));
			return false;
		}

		if (!Snapshot.IsFinite())
		{
			AddError(FString::Printf(
				TEXT("Telemetry went non-finite by step %d (t=%.2f s simulated): ")
				TEXT("location (%f,%f,%f), speed %f cm/s."),
				StepsCompleted, SimulatedSecondsSoFar,
				Snapshot.LocationCm.X, Snapshot.LocationCm.Y, Snapshot.LocationCm.Z,
				Snapshot.ForwardSpeedCms));
			return false;
		}

		const FVehicleFailureReport& Report = Pawn->GetLastFailureReport();
		if (Report.HasAnyFailure())
		{
			AddError(FString::Printf(
				TEXT("The failure detector reported [%s] by step %d (t=%.2f s simulated): %s"),
				*RacingSim::Vehicle::DescribeVehicleFailureFlags(Report.Flags),
				StepsCompleted, SimulatedSecondsSoFar, *Report.Reason));
			return false;
		}

		const double DistanceCm = Fixture.GetLocation().Size2D();
		MaxDistanceCm = FMath::Max(MaxDistanceCm, DistanceCm);
		if (DistanceCm > SoakPositionBoundCm)
		{
			AddError(FString::Printf(
				TEXT("The car reached %.1f cm from the origin by step %d (t=%.2f s simulated), ")
				TEXT("beyond the %.1f cm bound; it is about to leave the %.0f cm ground slab."),
				DistanceCm, StepsCompleted, SimulatedSecondsSoFar, SoakPositionBoundCm,
				FVehicleManoeuvreFixture::GroundHalfSizeCm));
			return false;
		}

		++InspectionCount;
	}

	const double WallClockSeconds = FPlatformTime::Seconds() - WallClockStartSeconds;
	const uint64 ResidentAfterBytes = CollectAndMeasureResidentBytes();

	// Signed, because resident memory can legitimately FALL across the run when another
	// subsystem releases more than this one allocates. An unsigned subtraction would turn
	// that into an enormous positive delta and fail the test for shrinking.
	const int64 ResidentDeltaBytes =
		static_cast<int64>(ResidentAfterBytes) - static_cast<int64>(ResidentBeforeBytes);

	const double SimulatedSeconds = static_cast<double>(StepsCompleted) * SoakStepSeconds;

	// The ticket requires all three of these reported, not merely asserted on, so they are
	// emitted unconditionally -- including on the passing path, which is the run whose
	// numbers anyone will actually want to compare against next time.
	const FString Summary = FString::Printf(
		TEXT("VEH-006 soak: %d steps at %f s = %.1f s simulated (%.1f min) in %.1f s wall clock ")
		TEXT("(%.0f steps/s, %.1fx real time); %d inspections; max distance %.1f cm of %.1f cm bound; ")
		TEXT("resident memory %.1f -> %.1f MiB, delta %+.1f MiB against a %.1f MiB ceiling."),
		StepsCompleted, SoakStepSeconds, SimulatedSeconds, SimulatedSeconds / 60.0, WallClockSeconds,
		WallClockSeconds > 0.0 ? static_cast<double>(StepsCompleted) / WallClockSeconds : 0.0,
		WallClockSeconds > 0.0 ? SimulatedSeconds / WallClockSeconds : 0.0,
		InspectionCount, MaxDistanceCm, SoakPositionBoundCm,
		ToMebibytes(ResidentBeforeBytes), ToMebibytes(ResidentAfterBytes),
		SignedToMebibytes(ResidentDeltaBytes),
		ToMebibytes(SoakMemoryCeilingBytes));

	// AddInfo, not UE_LOG alone, and the difference is load bearing. A UE_LOG(Display) issued
	// inside a test reaches index.json only when the framework dumps its captured log on a
	// FAILING test; on a passing one it stays in the editor log and never enters the report.
	// The first successful soak run proved it -- the run passed with NON_SUCCESS_COUNT=0 while
	// Run-Soak.ps1 reported "(none -- the soak did not reach its summary line)", so the one
	// artefact the ticket asks for was missing from precisely the run worth keeping. AddInfo
	// writes an Info entry into the test's own entries, which is serialised either way.
	AddInfo(Summary);
	UE_LOG(LogRacingTests, Display, TEXT("%s"), *Summary);

	TestEqual(
		TEXT("The soak ran every step it was asked to"),
		StepsCompleted, SoakSteps);

	TestTrue(
		FString::Printf(
			TEXT("The soak covered at least %.4f s of simulated time, got %.4f s"),
			SoakSimulatedSeconds, SimulatedSeconds),
		SimulatedSeconds + KINDA_SMALL_NUMBER >= SoakSimulatedSeconds);

	TestTrue(
		FString::Printf(
			TEXT("Resident memory grew by %.1f MiB across %d steps, above the %.1f MiB ceiling"),
			SignedToMebibytes(ResidentDeltaBytes), StepsCompleted,
			ToMebibytes(SoakMemoryCeilingBytes)),
		ResidentDeltaBytes <= static_cast<int64>(SoakMemoryCeilingBytes));

	return true;
}
