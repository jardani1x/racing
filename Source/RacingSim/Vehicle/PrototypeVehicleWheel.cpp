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
	// Must equal UVehicleChassisDataAsset::MaxSteerAngleDegrees's default -- see the
	// same cross-check reasoning as radius/width, now extended to this field.
	MaxSteerAngle = 40.0f;

	bAffectedBySteering = true;
	bAffectedByHandbrake = false;

	// AxleType, not bAffectedByEngine, is what makes ARacingVehiclePawn's
	// DifferentialSetup.DifferentialType mapping (RacingVehiclePawn.cpp) actually
	// drive anything. Per ChaosVehicleWheel.h: "If left undefined then the
	// bAffectedByEngine value is used, if defined then bAffectedByEngine is ignored
	// and the differential setup on the vehicle defines which wheels get power." An
	// earlier version of this file set only bAffectedByEngine, which left AxleType
	// at its Undefined default -- the differential's DifferentialType was computed
	// correctly and then silently had no effect, so DrivetrainLayout::FrontWheelDrive
	// or AllWheelDrive produced exactly the same (rear-only) car as RearWheelDrive.
	AxleType = EAxleType::Front;
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

	// See UPrototypeFrontWheel's constructor: AxleType is what the differential
	// actually reads. bAffectedByEngine is still set true so the CDO is a coherent,
	// drivable car even in a hypothetical context where AxleType were ignored (e.g.
	// direct construction outside SetupVehicle), but it is not the authoritative
	// path under normal play.
	AxleType = EAxleType::Rear;
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

		// MaxSteerAngleDegrees is the mechanical lock -- geometry, not tune (the
		// asset's own header) -- and Chaos reads it from the steered wheel's CDO the
		// same way it reads radius/width, so it is subject to the identical
		// CDO-vs-instance constraint and gets the same cross-check.
		if (!FMath::IsNearlyEqual(FrontCdo->MaxSteerAngle, Chassis->MaxSteerAngleDegrees))
		{
			AddFailure(TEXT("MaxSteerAngleDegrees"), FString::Printf(
				TEXT("Chassis declares MaxSteerAngleDegrees=%.2f but %s's MaxSteerAngle is %.2f"),
				Chassis->MaxSteerAngleDegrees, *FrontWheel->GetName(), FrontCdo->MaxSteerAngle));
		}

		return Result;
	}
}
