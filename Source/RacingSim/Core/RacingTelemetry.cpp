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

bool FRacingLapTiming::AreSectorsConsistent(const double ToleranceSeconds) const
{
	if (!IsComplete() || SectorDurationsSeconds.Num() == 0)
	{
		return false;
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
