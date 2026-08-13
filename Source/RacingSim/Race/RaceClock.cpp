// Copyright RacingSim. All Rights Reserved.

#include "Race/RaceClock.h"

#include "Core/RacingSimLog.h"
#include "HAL/PlatformTime.h"

namespace RacingSim::Race
{
	double PlatformMonotonicSeconds()
	{
		// Windows: QueryPerformanceCounter, monotonic and unaffected by wall-clock
		// changes, NTP steps or DST. Not affected by time dilation or by any engine
		// frame-time clamp, which is the whole reason it is used instead of
		// UWorld::GetTimeSeconds() or an accumulated DeltaTime.
		//
		// Caveat kept here rather than assumed away: the *epoch* is arbitrary and
		// differs per process, so a reading may never be serialised, compared across
		// processes, or written into a result. Only differences leave this file.
		return FPlatformTime::Seconds();
	}
}

void FRaceClock::Reset()
{
	StartTimestampSeconds = 0.0;
	FrozenElapsedSeconds = 0.0;
	HighWaterElapsedSeconds = 0.0;
	bRunning = false;
	bHasStarted = false;
}

bool FRaceClock::Start(double NowSeconds)
{
	if (bRunning)
	{
		// Idempotent. Restarting from a new timestamp would silently discard the
		// time already accrued, which is the exact "double start loses the first
		// second of the lap" bug Gate B's idempotency clause exists to prevent.
		return false;
	}

	if (!FMath::IsFinite(NowSeconds))
	{
		// A NaN here propagates into every lap time ever compared against it and
		// makes all comparisons false, which reads as "no lap was ever fastest"
		// rather than as an error. Refuse loudly instead.
		UE_LOG(LogRacingRace, Error, TEXT("FRaceClock::Start rejected a non-finite timestamp; clock not started."));
		return false;
	}

	StartTimestampSeconds = NowSeconds;
	FrozenElapsedSeconds = 0.0;
	bRunning = true;
	bHasStarted = true;
	return true;
}

bool FRaceClock::Stop(double NowSeconds)
{
	if (!bRunning)
	{
		// Idempotent, and load-bearing: ERaceState::Finished must freeze the result
		// exactly once no matter how the finish path is reached.
		return false;
	}

	const double RawElapsed = FMath::IsFinite(NowSeconds) ? (NowSeconds - StartTimestampSeconds) : HighWaterElapsedSeconds;

	// Freeze at the ratcheted value, not the raw subtraction: if the source ever
	// stepped backwards while running, the frozen result must be the largest time
	// the runner was ever told they had, never a smaller one produced at the finish.
	FrozenElapsedSeconds = FMath::Max(HighWaterElapsedSeconds, FMath::Max(0.0, RawElapsed));
	HighWaterElapsedSeconds = FrozenElapsedSeconds;
	bRunning = false;
	return true;
}

double FRaceClock::Sample(double NowSeconds)
{
	if (!bHasStarted)
	{
		return 0.0;
	}

	double RawElapsed = FrozenElapsedSeconds;
	if (bRunning)
	{
		RawElapsed = FMath::IsFinite(NowSeconds) ? (NowSeconds - StartTimestampSeconds) : HighWaterElapsedSeconds;
	}

	// Two clamps, not one. Max(0.0, ...) handles a source that reports a reading
	// earlier than the start stamp; the high-water Max handles a source that steps
	// backwards mid-run. Neither is expected on Windows; both are cheap, branch-free
	// enough for a read path, and turn "we assume the platform is monotonic" into
	// "the value we return is monotonic".
	HighWaterElapsedSeconds = FMath::Max(HighWaterElapsedSeconds, FMath::Max(0.0, RawElapsed));
	return HighWaterElapsedSeconds;
}
