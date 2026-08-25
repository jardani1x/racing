// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/RacingVehiclePawn.h"

#include "Components/BoxComponent.h"
#include "Core/RacingSimLog.h"
#include "GameFramework/PlayerController.h"
#include "Vehicle/PrototypeVehicleWheel.h"
#include "Vehicle/VehicleInputComponent.h"
#include "Vehicle/VehicleInputConfig.h"
#include "Vehicle/VehicleInputTypes.h"

ARacingVehiclePawn::ARacingVehiclePawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	// TG_PrePhysics, matching VehicleInputComponent's own tick group (VEH-001) --
	// the mapped command must reach the movement component before Chaos steps, or
	// every input is applied one frame late. See VehicleInputComponent.cpp's
	// constructor comment for the latency argument; this pawn's Tick is the second
	// half of the same requirement.
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	// Primitive collision, per CLAUDE.md's "original unbranded prototype car": no
	// authored mesh, so no license-ledger entry is owed by this ticket. Root
	// component, so the wheels (owned by the movement component) and any later
	// visual mesh attach beneath it.
	ChassisCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("ChassisCollision"));
	SetRootComponent(ChassisCollision);
	ChassisCollision->SetCollisionProfileName(TEXT("Vehicle"));
	ChassisCollision->SetSimulatePhysics(true);
	ChassisCollision->SetNotifyRigidBodyCollision(true);

	VehicleMovementComponent = CreateDefaultSubobject<UChaosWheeledVehicleMovementComponent>(TEXT("VehicleMovementComponent"));
	VehicleMovementComponent->SetIsReplicated(false);
	VehicleMovementComponent->UpdatedComponent = ChassisCollision;

	VehicleInputComp = CreateDefaultSubobject<UVehicleInputComponent>(TEXT("VehicleInputComponent"));
}

void ARacingVehiclePawn::BeginPlay()
{
	Super::BeginPlay();

	ApplyChassisAsset();
}

void ARacingVehiclePawn::ApplyChassisAsset()
{
	if (ChassisAsset == nullptr)
	{
		// A pawn with no chassis is a content mistake, not a crash: it stands on
		// ChassisCollision's default box, cannot drive (WheelSetups stays empty, so
		// Chaos has nothing to simulate), and says so loudly. Matches VEH-001's
		// established policy for an unconfigured input component.
		UE_LOG(LogRacingVehicle, Error,
			TEXT("ARacingVehiclePawn '%s' has no ChassisAsset; the vehicle will not be drivable."),
			*GetNameSafe(this));
		return;
	}

	const RacingSim::Validation::FRacingValidationResult ValidationResult = ChassisAsset->ValidateReadOnly();
	if (!ValidationResult.Issues.IsEmpty())
	{
		// Reported, not corrected: this pawn does not own the asset and must not
		// silently mutate a chassis that a designer will later inspect and find
		// changed underneath them. Matches VEH-002's own DataAsset's ValidateReadOnly
		// contract -- report-only, never a hidden write.
		for (const RacingSim::Validation::FRacingValidationIssue& Issue : ValidationResult.Issues)
		{
			UE_LOG(LogRacingVehicle, Warning,
				TEXT("ARacingVehiclePawn '%s': chassis validation issue on '%s': %s"),
				*GetNameSafe(this), *Issue.PropertyName.ToString(), *Issue.Message);
		}
	}

	ChassisCollision->SetBoxExtent(ChassisAsset->GetChassisHalfExtentCm());

	VehicleMovementComponent->Mass = ChassisAsset->GetMassKilograms();
	VehicleMovementComponent->CenterOfMassOverride = ChassisAsset->GetCentreOfMassOffsetCm();
	VehicleMovementComponent->bEnableCenterOfMassOverride = true;

	VehicleMovementComponent->DragCoefficient = ChassisAsset->DragCoefficient;
	VehicleMovementComponent->DownforceCoefficient = ChassisAsset->DownforceCoefficient;
	// Chaos::Cm2ToM2 is applied internally by FillAerodynamicsSetup; DragArea is
	// authored in cm^2 here to match every other distance in this project.
	VehicleMovementComponent->DragArea = ChassisAsset->FrontalAreaCm2;

	VehicleMovementComponent->TransmissionSetup.bUseAutomaticGears = ChassisAsset->bUseAutomaticGears;

	// The one mapping this pawn exists to own: the project's EVehicleDrivetrainLayout
	// onto Chaos' EVehicleDifferential. Enumerator-for-enumerator, deliberately not a
	// static_cast -- see VehicleChassisDataAsset.h for why the project keeps its own
	// enum, and TRACK-002/CORE-002's precedent against relying on numeric layout
	// matching between two independently-versioned enums.
	switch (ChassisAsset->DrivetrainLayout)
	{
	case EVehicleDrivetrainLayout::FrontWheelDrive:
		VehicleMovementComponent->DifferentialSetup.DifferentialType = EVehicleDifferential::FrontWheelDrive;
		break;
	case EVehicleDrivetrainLayout::AllWheelDrive:
		VehicleMovementComponent->DifferentialSetup.DifferentialType = EVehicleDifferential::AllWheelDrive;
		break;
	case EVehicleDrivetrainLayout::RearWheelDrive:
	default:
		VehicleMovementComponent->DifferentialSetup.DifferentialType = EVehicleDifferential::RearWheelDrive;
		break;
	}
	VehicleMovementComponent->DifferentialSetup.FrontRearSplit = ChassisAsset->FrontRearTorqueSplit;

	// WheelSetups: index order fixed by EVehicleWheelIndex (FrontLeft, FrontRight,
	// RearLeft, RearRight), which GetWheelOffsetCm and this loop both honour.
	VehicleMovementComponent->WheelSetups.SetNum(NumPrototypeVehicleWheels);
	for (int32 WheelIndex = 0; WheelIndex < NumPrototypeVehicleWheels; ++WheelIndex)
	{
		const EVehicleWheelIndex Corner = static_cast<EVehicleWheelIndex>(WheelIndex);
		FChaosWheelSetup& Setup = VehicleMovementComponent->WheelSetups[WheelIndex];
		Setup.WheelClass = UVehicleChassisDataAsset::IsFrontWheel(Corner)
			? TSubclassOf<UChaosVehicleWheel>(UPrototypeFrontWheel::StaticClass())
			: TSubclassOf<UChaosVehicleWheel>(UPrototypeRearWheel::StaticClass());
		Setup.AdditionalOffset = ChassisAsset->GetWheelOffsetCm(Corner);

		// UChaosWheeledVehicleMovementComponent::CanCreateVehicle refuses to create
		// the vehicle if any WheelSetup.BoneName is NAME_None, even on a boneless
		// blockout body: LocateBoneOffset ignores the name for a non-skeletal
		// UpdatedComponent and falls back to AdditionalOffset regardless, so this
		// name is never resolved against real geometry -- it exists only to satisfy
		// the engine's precondition. A placeholder rather than a real bone.
		Setup.BoneName = *FString::Printf(TEXT("Wheel_%d_Placeholder"), WheelIndex);
	}

	// The DataAsset's declared geometry and the wheel classes' CDO values must agree
	// -- see PrototypeVehicleWheel.h for why the asset cannot own this directly.
	const RacingSim::Validation::FRacingValidationResult WheelMatch =
		RacingSim::Vehicle::ValidateChassisAgainstWheelClasses(
			ChassisAsset, UPrototypeFrontWheel::StaticClass(), UPrototypeRearWheel::StaticClass());
	for (const RacingSim::Validation::FRacingValidationIssue& Issue : WheelMatch.Issues)
	{
		UE_LOG(LogRacingVehicle, Error,
			TEXT("ARacingVehiclePawn '%s': %s"), *GetNameSafe(this), *Issue.Message);
	}

	VehicleMovementComponent->RecreatePhysicsState();

	bChassisApplied = true;
}

void ARacingVehiclePawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (VehicleInputComp == nullptr)
	{
		return;
	}

	VehicleInputComp->Config = InputConfigAsset;

	if (APlayerController* PlayerController = Cast<APlayerController>(NewController))
	{
		VehicleInputComp->InitialiseForController(PlayerController, InitialInputDeviceType);
	}
	// A non-player controller (AI, or none) legitimately has no local player; the
	// input component logs that at Verbose and keeps producing the safe standing-
	// still command, per its own documented contract.
}

void ARacingVehiclePawn::UnPossessed()
{
	Super::UnPossessed();

	if (VehicleInputComp != nullptr)
	{
		VehicleInputComp->NotifyVehicleReset();
	}
}

void ARacingVehiclePawn::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bChassisApplied || VehicleInputComp == nullptr || VehicleMovementComponent == nullptr)
	{
		return;
	}

	// Speed feedback for VEH-001's speed-sensitive steering, before the command that
	// consumes it is read this same frame -- GetForwardSpeed() reflects last frame's
	// physics step, which is the freshest value available at TG_PrePhysics.
	VehicleInputComp->SetVehicleSpeedCms(VehicleMovementComponent->GetForwardSpeed());

	ApplyInputCommand(VehicleInputComp->GetCommand());
}

void ARacingVehiclePawn::ApplyInputCommand(const FVehicleInputCommand& Command)
{
	// All the mapping decisions -- steer sign, the handbrake threshold, the
	// manual-only gear gate, the non-finite refusal, the stated clutch omission --
	// live in RacingSim::Vehicle::MapCommandToChaosInput, a pure function reachable
	// at the Smoke gate with no actor or component. This call is the only place that
	// function's result crosses into Chaos.
	const bool bManualTransmission = !VehicleMovementComponent->TransmissionSetup.bUseAutomaticGears;
	const FVehicleChaosInput ChaosInput = RacingSim::Vehicle::MapCommandToChaosInput(Command, bManualTransmission);

	VehicleMovementComponent->SetThrottleInput(ChaosInput.Throttle);
	VehicleMovementComponent->SetBrakeInput(ChaosInput.Brake);
	VehicleMovementComponent->SetSteeringInput(ChaosInput.Steering);
	VehicleMovementComponent->SetHandbrakeInput(ChaosInput.bHandbrake);
	VehicleMovementComponent->SetChangeUpInput(ChaosInput.bChangeUp);
	VehicleMovementComponent->SetChangeDownInput(ChaosInput.bChangeDown);

	// Reset is VEH-005's scope (the reset POSE); this pawn deliberately does not act
	// on Command.bResetRequested. VEH-001's input-side reset (NotifyVehicleReset)
	// only clears input smoothing state, and is called from UnPossessed/an external
	// reset trigger, not from a one-shot flag read every Tick.
}
