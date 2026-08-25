// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/PrototypeVehicleWheel.h"

#include "Vehicle/VehicleChassisDataAsset.h"

using namespace RacingSim::Validation;

UPrototypeVehicleWheel::UPrototypeVehicleWheel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Shared suspension defaults. VEH-003 placeholder -- see the header's "What is a
	// PLACEHOLDER here" section. A car with no suspension travel falls through the
	// floor on the first Tick, so these carry values, not zeros.
	SuspensionMaxRaise = 12.0f;
	SuspensionMaxDrop = 12.0f;
	SuspensionDampingRatio = 0.5f;
	WheelLoadRatio = 0.5f;
	RollbarScaling = 0.15f;
	SpringRate = 60.0f;
	SpringPreload = 50.0f;

	// Neither axle is handbraked or steered by default; the front/rear subclasses
	// override the ones that are.
	bAffectedBySteering = false;
	bAffectedByHandbrake = false;
	bAffectedByEngine = false;
	bAffectedByBrake = true;
	MaxBrakeTorque = 1200.0f;
	MaxHandBrakeTorque = 0.0f;
}

UPrototypeFrontWheel::UPrototypeFrontWheel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Radius/width are the CDO values Chaos reads at SetupVehicle time (see the
	// header). They must equal UVehicleChassisDataAsset's FrontWheelRadiusCm /
	// FrontWheelWidthCm defaults, and ValidateChassisAgainstWheelClasses proves it
	// rather than trusting the two authors to remember.
	WheelRadius = 34.0f;
	WheelWidth = 24.0f;
	WheelMass = 18.0f;

	bAffectedBySteering = true;
	bAffectedByHandbrake = false;
	bAffectedByEngine = false;
}

UPrototypeRearWheel::UPrototypeRearWheel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	WheelRadius = 35.0f;
	WheelWidth = 28.0f;
	WheelMass = 20.0f;

	bAffectedBySteering = false;
	bAffectedByHandbrake = true;
	MaxHandBrakeTorque = 1600.0f;

	// bAffectedByEngine is set true unconditionally: the Phase 1 default drivetrain
	// is rear-wheel drive (EVehicleDrivetrainLayout::RearWheelDrive). ARacingVehiclePawn
	// overrides this per-instance to match the chassis asset's actual layout for
	// front- or all-wheel drive, since the CDO cannot express a per-instance choice
	// (see the header's CDO-vs-instance discussion) -- but the default must itself be
	// a coherent, drivable car, not an inert one.
	bAffectedByEngine = true;
}

namespace RacingSim::Vehicle
{
	FRacingValidationResult ValidateChassisAgainstWheelClasses(
		const UVehicleChassisDataAsset* Chassis,
		TSubclassOf<UChaosVehicleWheel> FrontWheel,
		TSubclassOf<UChaosVehicleWheel> RearWheel)
	{
		FRacingValidationResult Result;

		auto AddFailure = [&Result](const FName PropertyName, FString Message)
		{
			FRacingValidationIssue Issue;
			Issue.PropertyName = PropertyName;
			Issue.Action = ERangeAction::Failed;
			Issue.Message = MoveTemp(Message);
			Result.Issues.Add(MoveTemp(Issue));
		};

		if (Chassis == nullptr)
		{
			AddFailure(TEXT("Chassis"), TEXT("Chassis asset is null"));
			return Result;
		}
		if (FrontWheel == nullptr)
		{
			AddFailure(TEXT("FrontWheel"), TEXT("Front wheel class is null"));
			return Result;
		}
		if (RearWheel == nullptr)
		{
			AddFailure(TEXT("RearWheel"), TEXT("Rear wheel class is null"));
			return Result;
		}

		const UChaosVehicleWheel* FrontCdo = FrontWheel.GetDefaultObject();
		const UChaosVehicleWheel* RearCdo = RearWheel.GetDefaultObject();

		if (!FMath::IsNearlyEqual(FrontCdo->WheelRadius, Chassis->GetWheelRadiusCm(EVehicleWheelIndex::FrontLeft)))
		{
			AddFailure(TEXT("FrontWheelRadiusCm"), FString::Printf(
				TEXT("Chassis declares FrontWheelRadiusCm=%.2f but %s's WheelRadius is %.2f -- Chaos reads the wheel class, not the asset"),
				Chassis->GetWheelRadiusCm(EVehicleWheelIndex::FrontLeft), *FrontWheel->GetName(), FrontCdo->WheelRadius));
		}
		if (!FMath::IsNearlyEqual(FrontCdo->WheelWidth, Chassis->GetWheelWidthCm(EVehicleWheelIndex::FrontLeft)))
		{
			AddFailure(TEXT("FrontWheelWidthCm"), FString::Printf(
				TEXT("Chassis declares FrontWheelWidthCm=%.2f but %s's WheelWidth is %.2f"),
				Chassis->GetWheelWidthCm(EVehicleWheelIndex::FrontLeft), *FrontWheel->GetName(), FrontCdo->WheelWidth));
		}
		if (!FMath::IsNearlyEqual(RearCdo->WheelRadius, Chassis->GetWheelRadiusCm(EVehicleWheelIndex::RearLeft)))
		{
			AddFailure(TEXT("RearWheelRadiusCm"), FString::Printf(
				TEXT("Chassis declares RearWheelRadiusCm=%.2f but %s's WheelRadius is %.2f"),
				Chassis->GetWheelRadiusCm(EVehicleWheelIndex::RearLeft), *RearWheel->GetName(), RearCdo->WheelRadius));
		}
		if (!FMath::IsNearlyEqual(RearCdo->WheelWidth, Chassis->GetWheelWidthCm(EVehicleWheelIndex::RearLeft)))
		{
			AddFailure(TEXT("RearWheelWidthCm"), FString::Printf(
				TEXT("Chassis declares RearWheelWidthCm=%.2f but %s's WheelWidth is %.2f"),
				Chassis->GetWheelWidthCm(EVehicleWheelIndex::RearLeft), *RearWheel->GetName(), RearCdo->WheelWidth));
		}

		return Result;
	}
}
