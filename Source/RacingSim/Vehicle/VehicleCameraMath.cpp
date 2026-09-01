// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/VehicleCameraMath.h"

float RacingSim::Vehicle::ComputeSpeedAdjustedFieldOfViewDegrees(
	const float BaseFieldOfViewDegrees,
	const float MaxFieldOfViewBoostDegrees,
	const float SpeedCms,
	const float SpeedForMaxBoostCms)
{
	if (!FMath::IsFinite(BaseFieldOfViewDegrees))
	{
		// Nothing sane to return: the caller passed a broken base value. 90 degrees
		// matches FVehicleCameraSettings' own default rather than propagating a NaN.
		return 90.0f;
	}

	if (!FMath::IsFinite(MaxFieldOfViewBoostDegrees)
		|| !FMath::IsFinite(SpeedForMaxBoostCms)
		|| SpeedForMaxBoostCms <= 0.0f)
	{
		// SpeedForMaxBoostCms is the divisor below. A zero, negative or non-finite
		// value has no safe curve to fall back to, so the boost is disabled entirely
		// rather than dividing by it -- the same "no single safe value" reasoning
		// UVehicleFailureThresholdsDataAsset uses for its relationship checks.
		return BaseFieldOfViewDegrees;
	}

	// A zero-length or not-yet-valid velocity read (e.g. before the first physics
	// step) arrives here as 0 or non-finite; both clamp to stationary rather than
	// propagating.
	const float ClampedSpeedCms = FMath::IsFinite(SpeedCms) ? FMath::Max(SpeedCms, 0.0f) : 0.0f;

	const float Alpha = FMath::Clamp(ClampedSpeedCms / SpeedForMaxBoostCms, 0.0f, 1.0f);

	return BaseFieldOfViewDegrees + MaxFieldOfViewBoostDegrees * Alpha;
}

float RacingSim::Vehicle::ClampFieldOfViewForApplyDegrees(const float FovDegrees)
{
	if (!FMath::IsFinite(FovDegrees))
	{
		// Matches ComputeSpeedAdjustedFieldOfViewDegrees' own non-finite fallback above.
		return 90.0f;
	}

	return FMath::Clamp(FovDegrees, 5.0f, 170.0f);
}
