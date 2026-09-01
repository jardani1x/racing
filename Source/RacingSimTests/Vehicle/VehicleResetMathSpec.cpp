// Copyright RacingSim. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Vehicle/VehicleResetMath.h"

#include <limits>

/**
 * VEH-005: RacingSim::Vehicle::ResolveGroundCorrectedResetZCm and
 * RacingSim::Vehicle::IsResetDistanceAtOrBeforeQuery.
 *
 * Both are pure functions over plain doubles/bools -- no actor, no UWorld, no
 * trace -- so this suite is Smoke-safe for the same reason
 * VehicleChaosInputMappingSpec.cpp is (VEH-002's own file header). The actual
 * UWorld::LineTraceSingleByChannel call inside ExecuteSafeReset is not
 * reachable here; only the decision made from its result is.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleResetMathTest,
	"RacingSim.Vehicle.ResetMath",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleResetMathTest::RunTest(const FString& Parameters)
{
	using namespace RacingSim::Vehicle;

	// -- ResolveGroundCorrectedResetZCm --

	// A trace hit lifts GroundClearanceCm above the hit, ignoring the seed entirely.
	TestEqual(TEXT("A trace hit returns TraceHitZCm + GroundClearanceCm"),
		ResolveGroundCorrectedResetZCm(/*SeedZCm=*/500.0, /*GroundClearanceCm=*/30.0, /*bTraceHit=*/true, /*TraceHitZCm=*/100.0),
		130.0);

	// A trace miss falls back to the seed's own already-lifted height, unmodified.
	TestEqual(TEXT("A trace miss falls back to SeedZCm exactly"),
		ResolveGroundCorrectedResetZCm(500.0, 30.0, /*bTraceHit=*/false, /*TraceHitZCm=*/9999.0),
		500.0);

	// Non-finite inputs never propagate into a NaN teleport target -- they fall back to
	// the seed height, same as a miss.
	TestTrue(TEXT("A non-finite TraceHitZCm on a hit falls back to a finite result"),
		FMath::IsFinite(ResolveGroundCorrectedResetZCm(500.0, 30.0, true, std::numeric_limits<double>::quiet_NaN())));
	TestTrue(TEXT("A non-finite GroundClearanceCm falls back to a finite result"),
		FMath::IsFinite(ResolveGroundCorrectedResetZCm(500.0, std::numeric_limits<double>::infinity(), true, 100.0)));

	// -- IsResetDistanceAtOrBeforeQuery --

	constexpr double TrackLength = 10000.0;
	constexpr double MaxGap = 500.0;

	// Reset sample sits a small, in-bound gap behind the query -- the ordinary case.
	TestTrue(TEXT("A small in-bound backward gap is accepted"),
		IsResetDistanceAtOrBeforeQuery(/*QueryDistanceCm=*/2000.0, /*ResetDistanceCm=*/1800.0, TrackLength, MaxGap));

	// Exactly at the query distance is a zero gap -- still at-or-before, still accepted.
	TestTrue(TEXT("A zero gap (reset distance equals query distance) is accepted"),
		IsResetDistanceAtOrBeforeQuery(2000.0, 2000.0, TrackLength, MaxGap));

	// A gap larger than MaxBackwardGapCm is rejected.
	TestFalse(TEXT("A backward gap beyond MaxBackwardGapCm is rejected"),
		IsResetDistanceAtOrBeforeQuery(2000.0, 1000.0, TrackLength, MaxGap));

	// The reset distance sitting AHEAD of the query -- the exact bug this function
	// exists to catch -- is rejected, not silently accepted as "close enough".
	TestFalse(TEXT("A reset distance ahead of the query is rejected"),
		IsResetDistanceAtOrBeforeQuery(2000.0, 2500.0, TrackLength, MaxGap));

	// Wrap-around correctness: the query sits just after the start/finish line, and the
	// reset sample is a small in-bound gap behind it on the OTHER side of the wrap.
	TestTrue(TEXT("A small backward gap that wraps across the start/finish line is accepted"),
		IsResetDistanceAtOrBeforeQuery(/*QueryDistanceCm=*/100.0, /*ResetDistanceCm=*/TrackLength - 100.0, TrackLength, MaxGap));

	// The same wrapped reset sample, but far enough behind that it exceeds the bound
	// even accounting for the wrap.
	TestFalse(TEXT("A wrapped gap beyond MaxBackwardGapCm is still rejected"),
		IsResetDistanceAtOrBeforeQuery(100.0, TrackLength - 5000.0, TrackLength, MaxGap));

	// Invalid/non-finite input is rejected outright, per the function's own contract.
	// code-reviewer VEH-005 LOW-3: the MaxBackwardGapCm < 0 branch was the one rejection
	// path this spec never exercised.
	TestFalse(TEXT("A negative MaxBackwardGapCm is rejected"),
		IsResetDistanceAtOrBeforeQuery(2000.0, 1800.0, TrackLength, /*MaxBackwardGapCm=*/-1.0));
	TestFalse(TEXT("A non-positive TrackLengthCm is rejected"),
		IsResetDistanceAtOrBeforeQuery(2000.0, 1800.0, /*TrackLengthCm=*/0.0, MaxGap));
	TestFalse(TEXT("A non-finite QueryDistanceCm is rejected"),
		IsResetDistanceAtOrBeforeQuery(std::numeric_limits<double>::quiet_NaN(), 1800.0, TrackLength, MaxGap));
	TestFalse(TEXT("A non-finite ResetDistanceCm is rejected"),
		IsResetDistanceAtOrBeforeQuery(2000.0, std::numeric_limits<double>::infinity(), TrackLength, MaxGap));

	// -- IsResetSampleValid --
	//
	// Added on code review (VEH-005 HIGH-1): ExecuteSafeReset must not teleport to world
	// origin when ATrackDefinitionActor::GetResetPoseAtOrBeforeDistanceCm returns its own
	// degenerate-track sentinel (unbuilt or empty centerline).
	constexpr double InvalidDistance = -1.0; // mirrors ATrackDefinitionActor::InvalidDistanceCm

	TestTrue(TEXT("A real sample index with a real distance is valid"),
		IsResetSampleValid(/*SampleIndex=*/0, /*SampleDistanceCm=*/0.0, InvalidDistance));
	TestTrue(TEXT("A non-zero real sample is valid"),
		IsResetSampleValid(3, 7500.0, InvalidDistance));
	TestFalse(TEXT("INDEX_NONE alone marks the degenerate-track sentinel"),
		IsResetSampleValid(INDEX_NONE, 500.0, InvalidDistance));
	TestFalse(TEXT("InvalidDistanceCm alone marks the degenerate-track sentinel"),
		IsResetSampleValid(2, InvalidDistance, InvalidDistance));
	TestFalse(TEXT("Both sentinel fields together are rejected"),
		IsResetSampleValid(INDEX_NONE, InvalidDistance, InvalidDistance));

	return true;
}
