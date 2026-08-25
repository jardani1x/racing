// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/PrototypeVehicleWheel.h"

#include "Vehicle/VehicleChassisDataAsset.h"
#include "Vehicle/VehicleTuneDataAsset.h"

using namespace RacingSim::Validation;
namespace TuneDefaults = RacingSim::Vehicle::PrototypeTuneDefaults;

UPrototypeVehicleWheel::UPrototypeVehicleWheel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// VEH-003 REPLACED the VEH-002 placeholders here, as that ticket required ("must be
	// replaced, not extended, by VEH-003"). These now read from the single constexpr
	// definition in VehicleTuneDataAsset.h that UVehicleTuneDataAsset's own property
	// defaults also read, so the wheel class and the asset cannot start out disagreeing.
	// An asset INSTANCE can still be edited away from them, which is what
	// ValidateTuneAgainstWheelClasses() catches -- the same validate-never-write policy
	// VEH-002 established for radius/width (see this file's header for why a runtime CDO
	// write is not an option).
	//
	// Axle-independent values only. Spring rate, preload, damping, rollbar and brake
	// torque differ front to rear and are set by the subclasses below.
	SuspensionMaxRaise = TuneDefaults::SuspensionMaxRaiseCm;
	SuspensionMaxDrop = TuneDefaults::SuspensionMaxDropCm;
	WheelLoadRatio = TuneDefaults::WheelLoadRatio;

	// Neither axle is handbraked or steered by default; the front/rear subclasses
	// override the ones that are.
	bAffectedBySteering = false;
	bAffectedByHandbrake = false;
	bAffectedByEngine = false;
	bAffectedByBrake = true;
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

	// VEH-003 tune, front axle. Softer springs and less anti-roll than the rear, and the
	// larger share of the brake torque -- a front brake bias is what stops the rear axle
	// locking first and spinning the car, and UVehicleTuneDataAsset::Validate() reports a
	// rear-biased tune for exactly that reason.
	SpringRate = TuneDefaults::FrontSpringRateNPerM;
	SpringPreload = TuneDefaults::FrontSpringPreloadN;
	SuspensionDampingRatio = TuneDefaults::FrontDampingRatio;
	RollbarScaling = TuneDefaults::FrontRollbarScaling;
	MaxBrakeTorque = TuneDefaults::FrontBrakeTorqueNm;

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

	// VEH-003 tune, rear axle. Stiffer springs, more damping and more anti-roll than the
	// front (this axle carries the drive), and less brake torque (the front bias).
	SpringRate = TuneDefaults::RearSpringRateNPerM;
	SpringPreload = TuneDefaults::RearSpringPreloadN;
	SuspensionDampingRatio = TuneDefaults::RearDampingRatio;
	RollbarScaling = TuneDefaults::RearRollbarScaling;
	MaxBrakeTorque = TuneDefaults::RearBrakeTorqueNm;

	bAffectedBySteering = false;
	bAffectedByHandbrake = true;
	MaxHandBrakeTorque = TuneDefaults::HandbrakeTorqueNm;

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

	FRacingValidationResult ValidateTuneAgainstWheelClasses(
		const UVehicleTuneDataAsset* Tune,
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

		if (Tune == nullptr)
		{
			AddFailure(TEXT("Tune"), TEXT("Tune asset is null"));
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

		// Every field below is one Chaos reads from the CLASS DEFAULT OBJECT at
		// SetupVehicle time, so the asset can only DECLARE it -- see this file's header.
		// A mismatch means an author edited the asset expecting the car to change and it
		// did not. Named per field so the report says which one.
		auto CheckFloat = [&AddFailure](
			const FName PropertyName, const float Declared, const float CdoValue,
			const TCHAR* WheelName, const TCHAR* CdoFieldName, const TCHAR* Unit)
		{
			if (!FMath::IsNearlyEqual(Declared, CdoValue))
			{
				AddFailure(PropertyName, FString::Printf(
					TEXT("Tune declares %s=%.3f %s but %s's %s is %.3f -- Chaos reads the wheel class, not the asset"),
					*PropertyName.ToString(), Declared, Unit, WheelName, CdoFieldName, CdoValue));
			}
		};

		const FString FrontName = FrontWheel->GetName();
		const FString RearName = RearWheel->GetName();

		CheckFloat(TEXT("FrontSpringRateNPerM"), Tune->FrontSpringRateNPerM, FrontCdo->SpringRate, *FrontName, TEXT("SpringRate"), TEXT("N/m"));
		CheckFloat(TEXT("RearSpringRateNPerM"), Tune->RearSpringRateNPerM, RearCdo->SpringRate, *RearName, TEXT("SpringRate"), TEXT("N/m"));
		CheckFloat(TEXT("FrontSpringPreloadN"), Tune->FrontSpringPreloadN, FrontCdo->SpringPreload, *FrontName, TEXT("SpringPreload"), TEXT("N"));
		CheckFloat(TEXT("RearSpringPreloadN"), Tune->RearSpringPreloadN, RearCdo->SpringPreload, *RearName, TEXT("SpringPreload"), TEXT("N"));
		CheckFloat(TEXT("FrontDampingRatio"), Tune->FrontDampingRatio, FrontCdo->SuspensionDampingRatio, *FrontName, TEXT("SuspensionDampingRatio"), TEXT(""));
		CheckFloat(TEXT("RearDampingRatio"), Tune->RearDampingRatio, RearCdo->SuspensionDampingRatio, *RearName, TEXT("SuspensionDampingRatio"), TEXT(""));
		CheckFloat(TEXT("FrontRollbarScaling"), Tune->FrontRollbarScaling, FrontCdo->RollbarScaling, *FrontName, TEXT("RollbarScaling"), TEXT(""));
		CheckFloat(TEXT("RearRollbarScaling"), Tune->RearRollbarScaling, RearCdo->RollbarScaling, *RearName, TEXT("RollbarScaling"), TEXT(""));

		// Travel and load ratio are axle-independent, so both wheels are checked against
		// the same declared value -- a subclass that diverged would otherwise be invisible.
		CheckFloat(TEXT("SuspensionMaxRaiseCm"), Tune->SuspensionMaxRaiseCm, FrontCdo->SuspensionMaxRaise, *FrontName, TEXT("SuspensionMaxRaise"), TEXT("cm"));
		CheckFloat(TEXT("SuspensionMaxRaiseCm"), Tune->SuspensionMaxRaiseCm, RearCdo->SuspensionMaxRaise, *RearName, TEXT("SuspensionMaxRaise"), TEXT("cm"));
		CheckFloat(TEXT("SuspensionMaxDropCm"), Tune->SuspensionMaxDropCm, FrontCdo->SuspensionMaxDrop, *FrontName, TEXT("SuspensionMaxDrop"), TEXT("cm"));
		CheckFloat(TEXT("SuspensionMaxDropCm"), Tune->SuspensionMaxDropCm, RearCdo->SuspensionMaxDrop, *RearName, TEXT("SuspensionMaxDrop"), TEXT("cm"));
		CheckFloat(TEXT("WheelLoadRatio"), Tune->WheelLoadRatio, FrontCdo->WheelLoadRatio, *FrontName, TEXT("WheelLoadRatio"), TEXT(""));
		CheckFloat(TEXT("WheelLoadRatio"), Tune->WheelLoadRatio, RearCdo->WheelLoadRatio, *RearName, TEXT("WheelLoadRatio"), TEXT(""));

		CheckFloat(TEXT("FrontBrakeTorqueNm"), Tune->FrontBrakeTorqueNm, FrontCdo->MaxBrakeTorque, *FrontName, TEXT("MaxBrakeTorque"), TEXT("Nm"));
		CheckFloat(TEXT("RearBrakeTorqueNm"), Tune->RearBrakeTorqueNm, RearCdo->MaxBrakeTorque, *RearName, TEXT("MaxBrakeTorque"), TEXT("Nm"));
		CheckFloat(TEXT("HandbrakeTorqueNm"), Tune->HandbrakeTorqueNm, RearCdo->MaxHandBrakeTorque, *RearName, TEXT("MaxHandBrakeTorque"), TEXT("Nm"));

		// The front axle carries NO handbrake, which is a topology fact the tune must not
		// be able to contradict by implication: if the front wheel ever gains handbrake
		// torque, the handbrake tune above stops describing the car.
		if (!FMath::IsNearlyZero(FrontCdo->MaxHandBrakeTorque))
		{
			AddFailure(TEXT("HandbrakeTorqueNm"), FString::Printf(
				TEXT("%s's MaxHandBrakeTorque is %.3f Nm; the prototype's handbrake acts on the rear axle only, so HandbrakeTorqueNm no longer describes the car"),
				*FrontName, FrontCdo->MaxHandBrakeTorque));
		}

		return Result;
	}
}
