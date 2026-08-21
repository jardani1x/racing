// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingTelemetry.h"

double FRacingLapTiming::GetSectorTotalSeconds() const
{
	double Total = 0.0;
	for (const double SectorSeconds : SectorDurationsSeconds)
	{
		Total += SectorSeconds;
	}
	return Total;
}

bool FRacingLapTiming::AreSectorsConsistent(const double ToleranceSeconds, const int32 ExpectedSectorCount) const
{
	if (!IsComplete())
	{
		// A lap still running has no final duration to check the splits against, so
		// there is nothing to be consistent WITH. Unchanged by RACE-003.
		return false;
	}

	// EXISTENCE, checked only when the caller supplies the track's authored count.
	// Ordered before the vacuous-true branch below on purpose: a caller that DID say
	// "this track has three sectors" must get false for a lap carrying none, which is
	// SectorDurationsSeconds' shape 3 (splits withheld) and is a real fault the lap
	// also reports through Validity.
	if (ExpectedSectorCount != INDEX_NONE && SectorDurationsSeconds.Num() != ExpectedSectorCount)
	{
		return false;
	}

	if (SectorDurationsSeconds.Num() == 0)
	{
		// RACE-002 FINDING L2, CLOSED HERE. Vacuously true: no recorded sector fails to
		// account for the lap when there are no recorded sectors. Reached by a sectorless
		// track (a legal configuration) and, when the caller declined to pass a count, by
		// a lap whose splits were withheld -- which is why the header is emphatic that
		// this function answers "are the splits present consistent", never "are splits
		// present". See the ExpectedSectorCount parameter for the stronger question.
		return true;
	}

	// A negative or zero sector is impossible from a monotonic clock and means a
	// gate fired twice or out of order. Reject it before the sum hides it: two
	// errors of opposite sign would otherwise cancel and pass the total check.
	for (const double SectorSeconds : SectorDurationsSeconds)
	{
		if (!(SectorSeconds > 0.0))
		{
			return false;
		}
	}

	return FMath::Abs(GetSectorTotalSeconds() - LapDurationSeconds) <= ToleranceSeconds;
}

bool FRacingTelemetryFrame::IsStaleAt(const double NowSeconds, const double MaxAgeSeconds) const
{
	// Age on the race clock. Negative age means the frame is stamped in the
	// future, which happens when the clock is restarted underneath a consumer --
	// treated as stale so a restart blanks the HUD instead of leaving the
	// previous run's numbers on screen looking live.
	const double AgeSeconds = NowSeconds - TimestampSeconds;
	if (AgeSeconds < 0.0)
	{
		return true;
	}

	// MaxAgeSeconds <= 0 disables the staleness check entirely (the setting's
	// documented "0 disables" convention), rather than making every frame stale.
	if (MaxAgeSeconds <= 0.0)
	{
		return false;
	}

	return AgeSeconds > MaxAgeSeconds;
}
