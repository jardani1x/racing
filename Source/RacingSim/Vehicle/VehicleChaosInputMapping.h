// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VehicleChaosInputMapping.generated.h"

struct FVehicleInputCommand;

/**
 * VEH-002: the mapped axes Chaos actually receives, as plain data.
 *
 * A pure output type so `MapCommandToChaosInput` can be tested with no Actor, no
 * `UActorComponent` and no movement component -- reachable at the `Smoke` gate, per
 * the same testability-first precedent VEH-001 established for
 * `FVehicleInputProcessor`. `ARacingVehiclePawn::Tick` calls this once and then only
 * forwards the result to `UChaosVehicleMovementComponent`'s setters; it makes no
 * decisions of its own.
 */
USTRUCT()
struct RACINGSIM_API FVehicleChaosInput
{
	GENERATED_BODY()

	/** [0,1]. */
	float Throttle = 0.0f;

	/** [0,1]. */
	float Brake = 0.0f;

	/** [-1,1]. Positive is right, preserved unchanged from FVehicleInputCommand::Steer. */
	float Steering = 0.0f;

	/** Chaos exposes a bool, not an analog input, so the progressive handbrake is thresholded -- see the .cpp for the value. */
	bool bHandbrake = false;

	/** Only ever true under a manual transmission; see MapCommandToChaosInput. */
	bool bChangeUp = false;

	/** Only ever true under a manual transmission. */
	bool bChangeDown = false;
};

namespace RacingSim::Vehicle
{
	/**
	 * Maps VEH-001's normalised, dimensionless FVehicleInputCommand onto the axes
	 * UChaosVehicleMovementComponent's setters expect.
	 *
	 * A non-finite or out-of-range Command is refused entirely and the safe coasting
	 * input (every field default-constructed) is returned instead -- this function is
	 * the last place a defect in an upstream command can be caught before it reaches
	 * Chaos, per CLAUDE.md's rule that a NaN steering angle inside the physics engine
	 * is not an acceptable outcome of any layer above it.
	 *
	 * Chaos exposes no clutch axis in UE 5.8.1's UChaosVehicleMovementComponent
	 * public API (the manual transmission's internal simulation manages it), so
	 * FVehicleInputCommand::Clutch is deliberately not mapped -- stated here rather
	 * than silently dropped.
	 *
	 * @param Command             VEH-001's command for this frame.
	 * @param bManualTransmission Gates GearRequest onto bChangeUp/bChangeDown. Under
	 *                            an automatic gearbox Chaos ignores manual shift
	 *                            input, but the function still declines to set it so
	 *                            a test does not have to reason about what Chaos does
	 *                            with a value it should never receive.
	 */
	RACINGSIM_API FVehicleChaosInput MapCommandToChaosInput(
		const FVehicleInputCommand& Command,
		bool bManualTransmission);
}
