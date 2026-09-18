// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/VehicleResetMath.h"

#include "Vehicle/VehicleFailureDetection.h"

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

double RacingSim::Vehicle::ComputeMinimumResetCooldownSeconds(
	const float MaxContactSuppressionSeconds,
	const float TelemetrySampleRateHz)
{
	const double BudgetSeconds = (FMath::IsFinite(MaxContactSuppressionSeconds) && MaxContactSuppressionSeconds > 0.0f)
		? static_cast<double>(MaxContactSuppressionSeconds)
		: 0.0;

	if (!FMath::IsFinite(TelemetrySampleRateHz) || TelemetrySampleRateHz <= 0.0f)
	{
		// Capture disabled: the detector never arms, so only the budget is meaningful.
		return BudgetSeconds;
	}

	const double CaptureIntervalSeconds = 1.0 / static_cast<double>(TelemetrySampleRateHz);
	return BudgetSeconds + static_cast<double>(GetMinContactSuppressionEvaluations() + 1) * CaptureIntervalSeconds;
}

double RacingSim::Vehicle::ResolveEffectiveResetCooldownSeconds(
	const float AuthoredCooldownSeconds,
	const float MaxContactSuppressionSeconds,
	const float TelemetrySampleRateHz)
{
	const double MinimumSeconds = ComputeMinimumResetCooldownSeconds(MaxContactSuppressionSeconds, TelemetrySampleRateHz);
	if (!FMath::IsFinite(AuthoredCooldownSeconds))
	{
		return MinimumSeconds;
	}
	return FMath::Max(static_cast<double>(AuthoredCooldownSeconds), MinimumSeconds);
}

RacingSim::Vehicle::EVehicleResetGateResult RacingSim::Vehicle::EvaluateResetGate(const FVehicleResetGateInput& Input)
{
	if (!FMath::IsFinite(Input.SimulationTimeSeconds)
		|| !FMath::IsFinite(Input.CooldownSeconds)
		|| (Input.bHasPreviousReset && !FMath::IsFinite(Input.LastResetSimulationTimeSeconds)))
	{
		return EVehicleResetGateResult::ClockUnusable;
	}

	if (Input.bHasPreviousReset
		&& Input.SimulationTimeSeconds - Input.LastResetSimulationTimeSeconds < Input.CooldownSeconds)
	{
		return EVehicleResetGateResult::CoolingDown;
	}

	if (Input.bCaptureEnabled && Input.bContactSuppressionArmed)
	{
		return EVehicleResetGateResult::SuppressionArmed;
	}

	return EVehicleResetGateResult::Accepted;
}

const TCHAR* RacingSim::Vehicle::LexResetGateResult(const EVehicleResetGateResult Result)
{
	switch (Result)
	{
	case EVehicleResetGateResult::Accepted:         return TEXT("Accepted");
	case EVehicleResetGateResult::CoolingDown:      return TEXT("CoolingDown");
	case EVehicleResetGateResult::SuppressionArmed: return TEXT("SuppressionArmed");
	case EVehicleResetGateResult::ClockUnusable:    return TEXT("ClockUnusable");
	}
	return TEXT("Unknown");
}
