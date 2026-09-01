// Copyright RacingSim. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Vehicle/VehicleCameraMath.h"

#include <limits>

/**
 * VEH-005: RacingSim::Vehicle::ComputeSpeedAdjustedFieldOfViewDegrees.
 *
 * No Actor, no UActorComponent, no UCameraComponent -- a pure function over plain
 * data, so this suite is safe at the Smoke gate for the same reason
 * VehicleChaosInputMappingSpec.cpp is (VEH-002's own file header).
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleCameraMathTest,
	"RacingSim.Vehicle.CameraMath",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleCameraMathTest::RunTest(const FString& Parameters)
{
	using namespace RacingSim::Vehicle;

	constexpr float Base = 90.0f;
	constexpr float Boost = 10.0f;
	constexpr float SpeedForMaxBoost = 3000.0f;

	// At rest, FOV is exactly the base.
	TestEqual(TEXT("Zero speed yields the base FOV"),
		ComputeSpeedAdjustedFieldOfViewDegrees(Base, Boost, 0.0f, SpeedForMaxBoost), Base);

	// At or above the threshold, FOV is base + boost, and does not overshoot past it.
	TestEqual(TEXT("Speed at the threshold yields base + boost"),
		ComputeSpeedAdjustedFieldOfViewDegrees(Base, Boost, SpeedForMaxBoost, SpeedForMaxBoost), Base + Boost);
	TestEqual(TEXT("Speed well past the threshold does not exceed base + boost"),
		ComputeSpeedAdjustedFieldOfViewDegrees(Base, Boost, SpeedForMaxBoost * 10.0f, SpeedForMaxBoost), Base + Boost);

	// Halfway to the threshold yields roughly half the boost -- a simple linear curve,
	// not a claim of any particular easing.
	TestNearlyEqual(TEXT("Half speed yields roughly half the boost"),
		ComputeSpeedAdjustedFieldOfViewDegrees(Base, Boost, SpeedForMaxBoost * 0.5f, SpeedForMaxBoost),
		Base + Boost * 0.5f, 1.0e-3f);

	// A negative or non-finite speed is clamped to 0 rather than propagated -- a
	// zero-length velocity read before the first physics step must not produce a
	// negative or NaN FOV.
	TestEqual(TEXT("A negative speed is clamped to 0, yielding the base FOV"),
		ComputeSpeedAdjustedFieldOfViewDegrees(Base, Boost, -500.0f, SpeedForMaxBoost), Base);
	TestTrue(TEXT("A NaN speed yields a finite result"),
		FMath::IsFinite(ComputeSpeedAdjustedFieldOfViewDegrees(Base, Boost, std::numeric_limits<float>::quiet_NaN(), SpeedForMaxBoost)));
	TestTrue(TEXT("An infinite speed yields a finite result"),
		FMath::IsFinite(ComputeSpeedAdjustedFieldOfViewDegrees(Base, Boost, std::numeric_limits<float>::infinity(), SpeedForMaxBoost)));

	// SpeedForMaxBoostCms is a divisor: a validated-but-zero or non-finite value must
	// never reach a division, or every Tick's FOV becomes NaN -- exactly the class of
	// defect CLAUDE.md forbids reaching any layer of this project.
	TestTrue(TEXT("A zero SpeedForMaxBoostCms yields a finite result, not NaN from a division by zero"),
		FMath::IsFinite(ComputeSpeedAdjustedFieldOfViewDegrees(Base, Boost, 1500.0f, 0.0f)));
	TestTrue(TEXT("A negative SpeedForMaxBoostCms yields a finite result"),
		FMath::IsFinite(ComputeSpeedAdjustedFieldOfViewDegrees(Base, Boost, 1500.0f, -100.0f)));
	TestTrue(TEXT("A non-finite SpeedForMaxBoostCms yields a finite result"),
		FMath::IsFinite(ComputeSpeedAdjustedFieldOfViewDegrees(Base, Boost, 1500.0f, std::numeric_limits<float>::quiet_NaN())));

	// -- ClampFieldOfViewForApplyDegrees --
	//
	// code-reviewer VEH-005 MEDIUM-A: UCameraComponent::FieldOfView has no enforcing
	// runtime ceiling of its own (its [5, 170] is UIMin/UIMax, editor-only), so this is
	// the actual guard against a misauthored base+boost sum reaching a degenerate,
	// negative-tangent projection matrix.
	TestEqual(TEXT("A value already inside [5, 170] passes through unchanged"),
		ClampFieldOfViewForApplyDegrees(90.0f), 90.0f);
	// code-reviewer VEH-005 LOW-1 (repair cycle 2 re-review): 180, not 190, is the actual
	// degenerate-matrix (tangent-undefined) boundary; 190 is simply a value past this
	// module's [5, 170] design ceiling, same as 250 below -- corrected wording, same value.
	TestEqual(TEXT("A value above 170 is clamped to 170"),
		ClampFieldOfViewForApplyDegrees(250.0f), 170.0f);
	TestEqual(TEXT("A value well past the design ceiling is clamped to 170"),
		ClampFieldOfViewForApplyDegrees(190.0f), 170.0f);
	TestEqual(TEXT("A value below 5 is clamped to 5"),
		ClampFieldOfViewForApplyDegrees(0.5f), 5.0f);
	// The prior assertions only prove clamping from OUTSIDE [5, 170]; these pin that the
	// inclusive endpoints themselves pass through unchanged rather than being nudged
	// inward by an off-by-one in the FMath::Clamp bounds (code-reviewer LOW-1).
	TestEqual(TEXT("The lower bound 5 passes through unchanged"),
		ClampFieldOfViewForApplyDegrees(5.0f), 5.0f);
	TestEqual(TEXT("The upper bound 170 passes through unchanged"),
		ClampFieldOfViewForApplyDegrees(170.0f), 170.0f);
	TestEqual(TEXT("A NaN input falls back to the module's 90-degree default"),
		ClampFieldOfViewForApplyDegrees(std::numeric_limits<float>::quiet_NaN()), 90.0f);
	TestEqual(TEXT("An infinite input falls back to the module's 90-degree default"),
		ClampFieldOfViewForApplyDegrees(std::numeric_limits<float>::infinity()), 90.0f);

	return true;
}
