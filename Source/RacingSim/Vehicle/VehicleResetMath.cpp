// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/VehicleResetMath.h"

double RacingSim::Vehicle::ResolveGroundCorrectedResetZCm(
	const double SeedZCm,
	const double GroundClearanceCm,
	const bool bTraceHit,
	const double TraceHitZCm)
{
	if (!FMath::IsFinite(SeedZCm))
	{
		// No sane fallback exists if even the seed is broken; 0 matches the engine's
		// own world-origin convention rather than propagating a NaN into a teleport.
		return 0.0;
	}

	if (!bTraceHit || !FMath::IsFinite(GroundClearanceCm) || !FMath::IsFinite(TraceHitZCm))
	{
		return SeedZCm;
	}

	return TraceHitZCm + GroundClearanceCm;
}

bool RacingSim::Vehicle::IsResetDistanceAtOrBeforeQuery(
	const double QueryDistanceCm,
	const double ResetDistanceCm,
	const double TrackLengthCm,
	const double MaxBackwardGapCm)
{
	if (!FMath::IsFinite(QueryDistanceCm)
		|| !FMath::IsFinite(ResetDistanceCm)
		|| !FMath::IsFinite(TrackLengthCm)
		|| !FMath::IsFinite(MaxBackwardGapCm)
		|| TrackLengthCm <= 0.0
		|| MaxBackwardGapCm < 0.0)
	{
		return false;
	}

	// Wrap both distances into [0, TrackLengthCm) before differencing, since a closed
	// loop makes raw subtraction meaningless once either distance has lapped.
	const double WrappedQueryCm = FMath::Fmod(FMath::Fmod(QueryDistanceCm, TrackLengthCm) + TrackLengthCm, TrackLengthCm);
	const double WrappedResetCm = FMath::Fmod(FMath::Fmod(ResetDistanceCm, TrackLengthCm) + TrackLengthCm, TrackLengthCm);

	// Backward gap along the loop from Reset to Query, i.e. how far behind the query
	// the reset point sits. 0 means they coincide; TrackLengthCm would mean a full
	// lap behind, which IsResetDistanceAtOrBeforeQuery's caller never wants to accept.
	const double BackwardGapCm = FMath::Fmod(WrappedQueryCm - WrappedResetCm + TrackLengthCm, TrackLengthCm);

	return BackwardGapCm <= MaxBackwardGapCm;
}

bool RacingSim::Vehicle::IsResetSampleValid(
	const int32 SampleIndex,
	const double SampleDistanceCm,
	const double InvalidDistanceCm)
{
	return SampleIndex != INDEX_NONE && SampleDistanceCm != InvalidDistanceCm;
}
