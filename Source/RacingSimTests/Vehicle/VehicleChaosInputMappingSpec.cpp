// Copyright RacingSim. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Vehicle/VehicleChaosInputMapping.h"
#include "Vehicle/VehicleInputTypes.h"

#include <limits>

/**
 * VEH-002: RacingSim::Vehicle::MapCommandToChaosInput.
 *
 * No Actor, no UActorComponent, no movement component -- a pure function over plain
 * data, so this suite is safe at the Smoke gate for the same reason
 * VehicleInputProcessorSpec.cpp is (VehicleInputComponent.h's file header).
 */

namespace
{
	FVehicleInputCommand MakeCleanCommand()
	{
		FVehicleInputCommand Command;
		Command.Throttle = 0.6f;
		Command.Brake = 0.0f;
		Command.Steer = 0.4f;
		Command.Handbrake = 0.0f;
		Command.Clutch = 0.0f;
		Command.GearRequest = EVehicleGearRequest::None;
		Command.TimestampSeconds = 1.0;
		Command.DeltaSeconds = 1.0f / 60.0f;
		return Command;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleChaosInputMappingTest,
	"RacingSim.Vehicle.ChaosInputMapping",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleChaosInputMappingTest::RunTest(const FString& Parameters)
{
	using namespace RacingSim::Vehicle;

	// A clean command's throttle/brake/steer pass through unchanged; steer sign is preserved.
	{
		FVehicleInputCommand Command = MakeCleanCommand();
		const FVehicleChaosInput Result = MapCommandToChaosInput(Command, /*bManualTransmission*/ false);
		TestEqual(TEXT("Throttle passes through unchanged"), Result.Throttle, 0.6f);
		TestEqual(TEXT("Steer sign and magnitude are preserved"), Result.Steering, 0.4f);

		FVehicleInputCommand NegativeSteer = MakeCleanCommand();
		NegativeSteer.Steer = -0.7f;
		const FVehicleChaosInput NegativeResult = MapCommandToChaosInput(NegativeSteer, false);
		TestEqual(TEXT("Negative (left) steer is preserved, not flipped"), NegativeResult.Steering, -0.7f);
	}

	// Handbrake threshold: below 0.5 does not engage, at/above 0.5 does.
	{
		FVehicleInputCommand BelowThreshold = MakeCleanCommand();
		BelowThreshold.Handbrake = 0.4f;
		TestFalse(TEXT("Handbrake below the engage threshold stays disengaged"),
			MapCommandToChaosInput(BelowThreshold, false).bHandbrake);

		FVehicleInputCommand AtThreshold = MakeCleanCommand();
		AtThreshold.Handbrake = 0.5f;
		TestTrue(TEXT("Handbrake at the engage threshold engages"),
			MapCommandToChaosInput(AtThreshold, false).bHandbrake);

		FVehicleInputCommand AboveThreshold = MakeCleanCommand();
		AboveThreshold.Handbrake = 1.0f;
		TestTrue(TEXT("Handbrake fully applied engages"),
			MapCommandToChaosInput(AboveThreshold, false).bHandbrake);
	}

	// GearRequest only maps under a manual transmission.
	{
		FVehicleInputCommand ShiftUp = MakeCleanCommand();
		ShiftUp.GearRequest = EVehicleGearRequest::ShiftUp;

		const FVehicleChaosInput UnderManual = MapCommandToChaosInput(ShiftUp, /*bManualTransmission*/ true);
		TestTrue(TEXT("ShiftUp maps to bChangeUp under a manual transmission"), UnderManual.bChangeUp);
		TestFalse(TEXT("ShiftUp does not also set bChangeDown"), UnderManual.bChangeDown);

		const FVehicleChaosInput UnderAutomatic = MapCommandToChaosInput(ShiftUp, /*bManualTransmission*/ false);
		TestFalse(TEXT("ShiftUp is NOT mapped under an automatic transmission"), UnderAutomatic.bChangeUp);
	}

	// Non-finite or out-of-range input is refused wholesale, returning the safe coasting input.
	{
		FVehicleInputCommand NanThrottle = MakeCleanCommand();
		NanThrottle.Throttle = std::numeric_limits<float>::quiet_NaN();
		const FVehicleChaosInput Result = MapCommandToChaosInput(NanThrottle, false);
		TestEqual(TEXT("A NaN command produces zero throttle"), Result.Throttle, 0.0f);
		TestEqual(TEXT("A NaN command produces zero steer too -- the whole command is refused, not per-field"),
			Result.Steering, 0.0f);
		TestFalse(TEXT("A NaN command never engages the handbrake"), Result.bHandbrake);

		FVehicleInputCommand InfiniteSteer = MakeCleanCommand();
		InfiniteSteer.Steer = std::numeric_limits<float>::infinity();
		TestEqual(TEXT("An infinite steer command is refused"),
			MapCommandToChaosInput(InfiniteSteer, false).Steering, 0.0f);

		FVehicleInputCommand OutOfRange = MakeCleanCommand();
		OutOfRange.Throttle = 1.5f;
		TestEqual(TEXT("An out-of-[0,1]-range throttle is refused"),
			MapCommandToChaosInput(OutOfRange, false).Throttle, 0.0f);
	}

	return true;
}
