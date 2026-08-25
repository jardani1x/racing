// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/RacingVehiclePawn.h"

#include "Components/BoxComponent.h"
#include "Core/RacingSimLog.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "Vehicle/PrototypeVehicleWheel.h"
#include "Vehicle/VehicleInputComponent.h"
#include "Vehicle/VehicleInputConfig.h"
#include "Vehicle/VehicleInputTypes.h"
#include "Vehicle/VehicleTuneDataAsset.h"

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
	// SetUpdatedComponent, not a direct UpdatedComponent assignment: the setter also
	// adds a tick prerequisite from ChassisCollision onto this component
	// (UMovementComponent::SetUpdatedComponent), which is half of what makes the
	// "input before physics" ordering below actually hold rather than merely being
	// probably-fine because both are TG_PrePhysics.
	VehicleMovementComponent->SetUpdatedComponent(ChassisCollision);

	VehicleInputComp = CreateDefaultSubobject<UVehicleInputComponent>(TEXT("VehicleInputComponent"));
	// The other half: an actor's Tick and its components' TickComponent calls have no
	// defined relative order within the same tick group (TG_PrePhysics for both, per
	// VehicleInputComponent.h). Without this prerequisite, ARacingVehiclePawn::Tick
	// could run before VehicleInputComp::TickComponent on a given frame and read a
	// stale command -- not incorrect input, but a possible extra frame of latency
	// that would vary run to run.
	AddTickPrerequisiteComponent(VehicleInputComp);
}

void ARacingVehiclePawn::BeginPlay()
{
	Super::BeginPlay();

	ApplyChassisAsset();
}

void ARacingVehiclePawn::ApplyChassisAsset()
{
	if (bChassisApplied)
	{
		// The guard the header promises: BeginPlay is the only caller today, but a
		// second call (e.g. a future hot-reload or re-initialisation path) must not
		// re-run SetNum/RecreatePhysicsState mid-session.
		return;
	}

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

	// VEH-003, deliberately BEFORE RecreatePhysicsState(): that call is what pushes the
	// whole configuration into Chaos, so a tune written after it would sit in the
	// component and not in the simulation until something else recreated the state.
	ApplyTuneAsset();

	VehicleMovementComponent->RecreatePhysicsState();

	bChassisApplied = true;
}

void ARacingVehiclePawn::ApplyTuneAsset()
{
	if (TuneAsset == nullptr)
	{
		// Reported, never substituted. A silent fallback to Chaos' own engine/transmission
		// defaults would give a car that drives -- badly, and unlike the authored tune --
		// with nothing in the session saying which numbers were used. Same policy as the
		// null-chassis path above.
		UE_LOG(LogRacingVehicle, Error,
			TEXT("ARacingVehiclePawn '%s' has no TuneAsset; Chaos' built-in engine, transmission and steering defaults remain in place and no authored tune is applied."),
			*GetNameSafe(this));
		return;
	}

	const RacingSim::Validation::FRacingValidationResult TuneValidation = TuneAsset->ValidateReadOnly();
	for (const RacingSim::Validation::FRacingValidationIssue& Issue : TuneValidation.Issues)
	{
		// Report-only, matching the chassis path: this pawn does not own the asset and
		// must not mutate a tune a designer will later open and find changed.
		UE_LOG(LogRacingVehicle, Warning,
			TEXT("ARacingVehiclePawn '%s': tune validation issue on '%s': %s"),
			*GetNameSafe(this), *Issue.PropertyName.ToString(), *Issue.Message);
	}

	// -- Engine. Nm, rpm, and a NORMALISED [0,1] curve Chaos multiplies MaxTorque by.
	FVehicleEngineConfig& Engine = VehicleMovementComponent->EngineSetup;
	Engine.TorqueCurve = TuneAsset->NormalisedTorqueCurve;
	Engine.MaxTorque = TuneAsset->MaxTorqueNm;
	Engine.MaxRPM = TuneAsset->MaxRpm;
	Engine.EngineIdleRPM = TuneAsset->IdleRpm;
	Engine.EngineBrakeEffect = TuneAsset->EngineBrakeEffect;
	Engine.EngineRevUpMOI = TuneAsset->EngineRevUpMoi;
	Engine.EngineRevDownRate = TuneAsset->EngineRevDownRate;

	// -- Transmission. All dimensionless except GearChangeTime (seconds).
	//
	// bUseAutomaticGears is NOT written here: it is the chassis asset's, is already set
	// above, and is cross-checked against VEH-001's ETransmissionInputMode at possession.
	// Two writers for one Chaos field is exactly what this ticket's asset split avoids.
	FVehicleTransmissionConfig& Transmission = VehicleMovementComponent->TransmissionSetup;
	Transmission.ForwardGearRatios = TuneAsset->ForwardGearRatios;
	Transmission.ReverseGearRatios = TuneAsset->ReverseGearRatios;
	Transmission.FinalRatio = TuneAsset->FinalDriveRatio;
	Transmission.ChangeUpRPM = TuneAsset->ChangeUpRpm;
	Transmission.ChangeDownRPM = TuneAsset->ChangeDownRpm;
	Transmission.GearChangeTime = TuneAsset->GearChangeTimeSeconds;
	Transmission.TransmissionEfficiency = TuneAsset->TransmissionEfficiency;

	// -- Differential. NOT written here, and that is the ticket's decision, not an
	// omission: Chaos' FVehicleDifferentialConfig has exactly two fields and VEH-002's
	// chassis asset owns both (written ~30 lines above). VEH-003's contribution to the
	// differential is this comment and the topology cross-check the chassis asset already
	// performs. See UVehicleTuneDataAsset's header.

	// -- Steering. The second project-enum-to-Chaos-enum mapping this pawn owns.
	// Enumerator-for-enumerator rather than a static_cast, for the same reason as
	// EVehicleDrivetrainLayout above: two independently-versioned enums must not be
	// assumed to share a numeric layout.
	FVehicleSteeringConfig& Steering = VehicleMovementComponent->SteeringSetup;
	switch (TuneAsset->SteeringModel)
	{
	case EVehicleSteeringModel::SingleAngle:
		Steering.SteeringType = ESteeringType::SingleAngle;
		break;
	case EVehicleSteeringModel::Ackermann:
		Steering.SteeringType = ESteeringType::Ackermann;
		break;
	case EVehicleSteeringModel::AngleRatio:
	default:
		Steering.SteeringType = ESteeringType::AngleRatio;
		break;
	}
	Steering.AngleRatio = TuneAsset->OuterInnerAngleRatio;

	// The single-authority rule for speed-sensitive steering, enforced rather than
	// documented. Chaos samples SteeringCurve with CmSToMPH(ForwardSpeed) and its own
	// InitDefaults authors a curve falling to 0.3 by 120 mph; VEH-001's input layer
	// applies a second scale in km/h. Left alone the two MULTIPLY, and neither asset
	// reads as if it were doing so.
	if (TuneAsset->SteerSpeedAuthority == EVehicleSteerSpeedAuthority::ChaosCurve)
	{
		Steering.SteeringCurve = TuneAsset->SteerScaleBySpeedMphCurve;

		if (InputConfigAsset != nullptr && InputConfigAsset->SteerSpeedScaleMode != ESteerSpeedScaleMode::Off)
		{
			UE_LOG(LogRacingVehicle, Warning,
				TEXT("ARacingVehiclePawn '%s': TuneAsset '%s' claims ChaosCurve steer-speed authority but InputConfigAsset '%s' SteerSpeedScaleMode is %s -- the two scales MULTIPLY. Set the input config to Off, or set the tune to InputLayer."),
				*GetNameSafe(this), *GetNameSafe(TuneAsset), *GetNameSafe(InputConfigAsset),
				*UEnum::GetValueAsString(InputConfigAsset->SteerSpeedScaleMode));
		}
	}
	else
	{
		// InputLayer authority: overwrite Chaos' copy with a FLAT UNITY curve so the
		// engine default cannot apply underneath VEH-001's km/h curve. Two keys, because
		// a single-key curve is a constant only by accident of extrapolation. The domain
		// is mph and the upper key is far past any speed this prototype reaches.
		if (FRichCurve* UnityCurve = Steering.SteeringCurve.GetRichCurve())
		{
			UnityCurve->Reset();
			UnityCurve->AddKey(0.0f, 1.0f);
			UnityCurve->AddKey(1000.0f, 1.0f);
		}
	}

	// -- Brakes and suspension are NOT written. Chaos reads MaxBrakeTorque,
	// MaxHandBrakeTorque, SpringRate, SpringPreload, SuspensionDampingRatio,
	// RollbarScaling, SuspensionMaxRaise/Drop and WheelLoadRatio from the wheel CLASS
	// DEFAULT OBJECT at SetupVehicle time -- an instance write lands too late and a CDO
	// write is process-global. So the asset declares them and this proves they agree.
	// See PrototypeVehicleWheel.h.
	const RacingSim::Validation::FRacingValidationResult TuneWheelMatch =
		RacingSim::Vehicle::ValidateTuneAgainstWheelClasses(
			TuneAsset, UPrototypeFrontWheel::StaticClass(), UPrototypeRearWheel::StaticClass());
	for (const RacingSim::Validation::FRacingValidationIssue& Issue : TuneWheelMatch.Issues)
	{
		UE_LOG(LogRacingVehicle, Error,
			TEXT("ARacingVehiclePawn '%s': %s"), *GetNameSafe(this), *Issue.Message);
	}
}

void ARacingVehiclePawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (VehicleInputComp == nullptr)
	{
		return;
	}

	VehicleInputComp->Config = InputConfigAsset;

	// VehicleChassisDataAsset.h's documented promise: "Must agree with the input
	// config's ETransmissionInputMode ... ARacingVehiclePawn checks the two agree at
	// possession and warns by name." Two independent sources of "manual" exist --
	// the config's TransmissionMode (gates whether GearRequest is ever produced) and
	// the chassis's bUseAutomaticGears (gates bManualTransmission in
	// ApplyInputCommand) -- and a mismatch gives shift keys that silently do nothing.
	if (ChassisAsset != nullptr && InputConfigAsset != nullptr)
	{
		const bool bConfigIsManual = InputConfigAsset->TransmissionMode == ETransmissionInputMode::Manual;
		const bool bChassisIsManual = !ChassisAsset->bUseAutomaticGears;
		if (bConfigIsManual != bChassisIsManual)
		{
			UE_LOG(LogRacingVehicle, Warning,
				TEXT("ARacingVehiclePawn '%s': InputConfigAsset '%s' TransmissionMode (%s) disagrees with ChassisAsset '%s' bUseAutomaticGears (%s) -- shift keys will not match the gearbox."),
				*GetNameSafe(this), *GetNameSafe(InputConfigAsset),
				*UEnum::GetValueAsString(InputConfigAsset->TransmissionMode),
				*GetNameSafe(ChassisAsset), ChassisAsset->bUseAutomaticGears ? TEXT("true") : TEXT("false"));
		}
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(NewController))
	{
		VehicleInputComp->InitialiseForController(PlayerController, InitialInputDeviceType);
	}
	// A non-player controller (AI, or none) legitimately has no local player; the
	// input component logs that at Verbose and keeps producing the safe standing-
	// still command, per its own documented contract.
}

void ARacingVehiclePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// The missing half of VEH-001's own documented contract (VehicleInputComponent.h:
	// "Bind the configured UInputAction assets to this component's handlers. Call
	// from the pawn's SetupPlayerInputComponent."). Without this call PossessedBy
	// still pushes the mapping context and configures the processor -- so the logs
	// read as if everything is wired -- but no UInputAction is ever bound to a
	// handler, PendingSample never leaves zero, and GetCommand() returns the default
	// coasting command forever. Caught in code review before merge, not by any test:
	// this project's harness cannot spawn a possessed pawn to exercise it directly.
	if (VehicleInputComp == nullptr)
	{
		return;
	}

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		VehicleInputComp->BindActions(EnhancedInput);
	}
	else
	{
		UE_LOG(LogRacingVehicle, Error,
			TEXT("ARacingVehiclePawn '%s': PlayerInputComponent is not a UEnhancedInputComponent; no input is bound. Check the project's DefaultInputComponentClass."),
			*GetNameSafe(this));
	}
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
