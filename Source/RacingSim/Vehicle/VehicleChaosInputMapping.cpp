// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/VehicleChaosInputMapping.h"

#include "Vehicle/VehicleInputTypes.h"

namespace RacingSim::Vehicle
{
	FVehicleChaosInput MapCommandToChaosInput(
		const FVehicleInputCommand& Command,
		const bool bManualTransmission)
	{
		// The single boundary check: a Command that fails this is refused wholesale
		// and the safe coasting input (every field default-constructed) is returned.
		// See the header -- this is the last place a defect in an upstream command
		// can be caught before it reaches Chaos.
		if (!Command.IsFiniteAndInRange())
		{
			return FVehicleChaosInput();
		}

		FVehicleChaosInput Result;
		Result.Throttle = Command.Throttle;
		Result.Brake = Command.Brake;
		Result.Steering = Command.Steer;

		// Analog [0,1] to Chaos' bool, at the midpoint -- documented here because
		// there is nowhere else the threshold could live. A wheel-mounted analog
		// handbrake at exactly 0.5 engages; VEH-003 owns whether Phase 2 wants a
		// progressive handbrake torque instead of this on/off gate.
		constexpr float HandbrakeEngageThreshold = 0.5f;
		Result.bHandbrake = Command.Handbrake >= HandbrakeEngageThreshold;

		// GearRequest is only meaningful under a manual transmission -- Chaos ignores
		// SetChangeUpInput/SetChangeDownInput under an automatic gearbox regardless,
		// but this function declines to set them so a test of this function alone
		// never has to reason about what Chaos does with a value it should not have
		// received.
		if (bManualTransmission)
		{
			Result.bChangeUp = (Command.GearRequest == EVehicleGearRequest::ShiftUp);
			Result.bChangeDown = (Command.GearRequest == EVehicleGearRequest::ShiftDown);
		}

		// Command.Clutch is deliberately not mapped -- see the header.

		return Result;
	}
}
