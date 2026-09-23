// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/RacingVehiclePawn.h"

#include "Camera/CameraComponent.h"
#include "Chaos/ParticleHandle.h"
#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "Core/RacingSimLog.h"
#include "EnhancedInputComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
// VEH-004/VEH-005: the project's Vehicle/ -> Race/ dependencies, confined to this .cpp.
// The direction is justified in ARacingVehiclePawn::PublishCarSpecVersionTo's header
// comment -- only the pawn knows whether a tune was APPLIED as opposed to referenced,
// and only the pawn knows where it actually ended up after a reset, so a Race-side pull
// could not be correct for either.
#include "Race/RaceLapTracker.h"
#include "Race/RaceResult.h"
#include "Race/TrackDefinitionActor.h"
#include "Vehicle/PrototypeVehicleWheel.h"
#include "Vehicle/VehicleCameraDataAsset.h"
#include "Vehicle/VehicleFailureThresholdsDataAsset.h"
#include "Vehicle/VehicleInputComponent.h"
#include "Vehicle/VehicleInputConfig.h"
#include "Vehicle/VehicleInputTypes.h"
#include "Vehicle/VehicleResetMath.h"
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

	// VEH-005: same construction-time pattern VEH-002 used for ChassisCollision --
	// native components, no .uasset, so no license-ledger entry is owed. Root-attached
	// per the ticket; ApplyCameraSettings (called from BeginPlay) is what actually sets
	// the rig geometry from CameraAsset, so the literals here only matter before that
	// first BeginPlay runs (e.g. an editor viewport preview of the placed pawn).
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->TargetArmLength = 600.0f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
}

void ARacingVehiclePawn::BeginPlay()
{
	Super::BeginPlay();

	ApplyChassisAsset();

	// VEH-006: keep the chassis rigid body out of Chaos' sleep state.
	//
	// UChaosVehicleMovementComponent::SetSleeping delegates to WakeAllEnabledRigidBodies and
	// PutAllEnabledRigidBodiesToSleep, and BOTH open with
	// `if (USkeletalMeshComponent* Mesh = GetSkeletalMesh())`
	// (ChaosVehicleMovementComponent.cpp:2058-2090). This pawn is a blockout whose updated
	// component is a UBoxComponent, so GetSkeletalMesh() returns null and neither call does
	// anything at all. The vehicle literally cannot wake its own chassis.
	//
	// Chaos still sleeps the body on its own once it comes to rest, and for a vehicle a sleeping
	// body is fatal rather than merely idle:
	// FChaosVehicleManagerAsyncCallback::OnPreSimulate_Internal returns before
	// FChaosVehicleAsyncInput::Simulate unless Handle->ObjectState() is Dynamic
	// (ChaosVehicleManagerAsyncCallback.cpp:126-129). The entire vehicle simulation -- engine,
	// transmission, suspension, tyres -- stops, the last FChaosVehicleAsyncOutput stays latched
	// so telemetry keeps reporting plausible frozen numbers, and throttle does nothing. It is a
	// one-way door: the car cannot produce the motion that would wake it.
	//
	// That is what the VEH-006 manoeuvre suite was actually measuring. The decisive evidence was
	// per-wheel physics output identical to one decimal place before and after full throttle,
	// with game-thread interpolated throttle 1.000 and the body reporting not awake
	// (Saved/Automation/ReportVEH006Probe6).
	//
	// ESleepType::NeverSleep states the requirement directly, and FRigidBodyHandle_External::
	// SetSleepType wakes an already-sleeping particle when handed it (ParticleHandle.h:3777-3781),
	// so this is safe to call at any point after physics state creation. BeginPlay is after it.
	//
	// REMOVE THIS when the prototype gains a real skeletal mesh with a physics asset, together
	// with p.Vehicle.DisableConstraintSuspension in Config/DefaultEngine.ini: both are the same
	// missing-skeletal-mesh gap in Chaos Vehicles, seen from different sides.
	//
	// KNOWN GAP, code-reviewer RACE-006 repair cycle 2 HIGH-1: this pin does NOT survive a
	// reset. ExecuteSafeReset calls UChaosVehicleMovementComponent::ResetVehicle(), which
	// reaches ResetVehicleState() -> OnDestroyPhysicsState() ->
	// UpdatedComponent->RecreatePhysicsState() (ChaosVehicleMovementComponent.cpp:904, :1922).
	// That destroys and recreates the chassis particle, and the sleep type goes with it.
	// BeginPlay is the only place that applies it, so from the first reset onwards the
	// solver can sleep this car again. WakeChassisForInput covers the driver-facing half of
	// that -- the car still drives away -- and re-applying the pin after ResetVehicle() is
	// tracked separately as VEH-011, because it changes physics state on a path the soak
	// covers and needs its own evidence.
	//
	// Every path that fails to reach SetSleepType is REPORTED, not skipped quietly. The
	// failure this pin prevents is silent by construction: a slept chassis latches its
	// last physics output, so the telemetry keeps reporting the speed the car had when it
	// went to sleep and nothing in FVehicleFailureThresholds can raise a flag for it. If
	// the pin does not get applied, the log line below is the only warning anyone gets.
	bool bSleepPinApplied = false;

	if (ChassisCollision != nullptr)
	{
		if (const FBodyInstance* ChassisBody = ChassisCollision->GetBodyInstance())
		{
			if (FPhysicsActorHandle ChassisActor = ChassisBody->GetPhysicsActor())
			{
				ChassisActor->GetGameThreadAPI().SetSleepType(Chaos::ESleepType::NeverSleep);
				bSleepPinApplied = true;
			}
			else
			{
				UE_LOG(LogRacingVehicle, Warning,
					TEXT("ARacingVehiclePawn '%s' has a chassis body instance with no physics actor at BeginPlay, so the NeverSleep pin was not applied; the chassis may sleep under steady input and latch its last physics output."),
					*GetNameSafe(this));
			}
		}
		else
		{
			UE_LOG(LogRacingVehicle, Warning,
				TEXT("ARacingVehiclePawn '%s' has a chassis collision component with no body instance at BeginPlay, so the NeverSleep pin was not applied; the chassis may sleep under steady input and latch its last physics output."),
				*GetNameSafe(this));
		}
	}
	else
	{
		UE_LOG(LogRacingVehicle, Warning,
			TEXT("ARacingVehiclePawn '%s' has no ChassisCollision at BeginPlay, so the NeverSleep pin was not applied; the chassis may sleep under steady input and latch its last physics output."),
			*GetNameSafe(this));
	}

	UE_LOG(LogRacingVehicle, Verbose,
		TEXT("ARacingVehiclePawn '%s' NeverSleep pin applied: %s."),
		*GetNameSafe(this), bSleepPinApplied ? TEXT("yes") : TEXT("NO"));

	// VEH-004: report the thresholds in force once, at start, rather than leaving a
	// reader to guess which numbers a failure report was judged against. A null asset
	// is the expected configuration today and is NOT an error -- but it must be said,
	// because "the detector used defaults" and "the detector used the asset you edited"
	// look identical in a log otherwise.
	if (FailureThresholdsAsset == nullptr)
	{
		UE_LOG(LogRacingVehicle, Log,
			TEXT("ARacingVehiclePawn '%s': no FailureThresholdsAsset; VEH-004 failure detection uses FVehicleFailureThresholds' built-in defaults."),
			*GetNameSafe(this));
	}
	else
	{
		// Report-only, same policy as the chassis and tune assets: this pawn does not
		// own the asset and must not mutate a threshold set a designer will later open
		// and find changed.
		const RacingSim::Validation::FRacingValidationResult ThresholdValidation =
			FailureThresholdsAsset->ValidateReadOnly();

		for (const RacingSim::Validation::FRacingValidationIssue& Issue : ThresholdValidation.Issues)
		{
			UE_LOG(LogRacingVehicle, Warning,
				TEXT("ARacingVehiclePawn '%s': failure-threshold validation issue on '%s': %s"),
				*GetNameSafe(this), *Issue.PropertyName.ToString(), *Issue.Message);
		}
	}

	// VEH-005: same report-only policy as FailureThresholdsAsset above. A null
	// CameraAsset is the expected configuration today (no .uasset authored, per
	// CLAUDE.md) and falls back to FVehicleCameraSettings' own defaults.
	if (CameraAsset == nullptr)
	{
		UE_LOG(LogRacingVehicle, Log,
			TEXT("ARacingVehiclePawn '%s': no CameraAsset; VEH-005 camera rig uses FVehicleCameraSettings' built-in defaults."),
			*GetNameSafe(this));
	}
	else
	{
		const RacingSim::Validation::FRacingValidationResult CameraValidation = CameraAsset->ValidateReadOnly();

		for (const RacingSim::Validation::FRacingValidationIssue& Issue : CameraValidation.Issues)
		{
			UE_LOG(LogRacingVehicle, Warning,
				TEXT("ARacingVehiclePawn '%s': camera validation issue on '%s': %s"),
				*GetNameSafe(this), *Issue.PropertyName.ToString(), *Issue.Message);
		}
	}

	ApplyCameraSettings();
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
		//
		// Decision, recorded rather than left implicit (code review, VEH-003 MEDIUM-4):
		// ApplyTuneAsset() is deliberately NOT called on this path, so a TuneAsset on a
		// chassis-less pawn is never applied, validated, or cross-checked against the
		// wheel classes. "Tune without a chassis" is not a supported configuration --
		// there is no WheelSetup for the tune's brake/suspension cross-check to run
		// against, and a car with no wheels gains nothing from a tuned engine. If a
		// future ticket needs the two independent, split this early-return so it only
		// skips the chassis-specific work below and calls ApplyTuneAsset() regardless.
		UE_LOG(LogRacingVehicle, Error,
			TEXT("ARacingVehiclePawn '%s' has no ChassisAsset; the vehicle will not be drivable. Any TuneAsset is also not applied."),
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
	if (bTuneApplied)
	{
		// Own guard, not borrowed from the caller's bChassisApplied (code review,
		// VEH-003 MEDIUM-4) -- correct today because ApplyChassisAsset() is this
		// function's only caller, but a function documented as "guarded and
		// idempotent" should not depend on every future caller remembering to guard it.
		return;
	}

	if (TuneAsset == nullptr)
	{
		// Reported, never substituted. The consequence is stated exactly, because it is
		// worse than it reads: returning here skips the mechanical-simulation re-arm
		// below, and bMechanicalSimEnabled has ALREADY latched false during component
		// registration (see the long note at the re-arm). So a tuneless pawn does not fall
		// back to Chaos' engine and transmission defaults -- it has no engine, no
		// transmission and no differential at all, sits at gear 0 and 0.0 rpm, and does not
		// respond to throttle. Anyone reading this line needs to know that, because a car
		// that drives badly and a car that does not drive are different bug reports.
		UE_LOG(LogRacingVehicle, Error,
			TEXT("ARacingVehiclePawn '%s' has no TuneAsset; no authored tune is applied AND mechanical simulation stays latched off, so this pawn has no engine, transmission or differential and will not respond to throttle."),
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

	// -- Engine. Nm, rpm, and a NORMALISED [0,1] curve Chaos re-normalises internally
	// (see UVehicleTuneDataAsset::GetPeakTorqueNm()).
	//
	// The torque curve write is gated on GetPeakNormalisedTorque() > 0 -- fixed on code
	// review (VEH-003 HIGH-2). ValidateReadOnly() above only REPORTS an unusable curve
	// (empty, all-zero, or every key non-finite); nothing previously stopped it reaching
	// Chaos. FVehicleEngineConfig::FillEngineSetup divides the curve by its own peak
	// (Eval(X) / MaxVal) before handing it to the physics solver, so a zero-peak curve is
	// a division by zero that puts NaN into Chaos::FSimpleEngineConfig -- a corrupting
	// solver state, not merely "a slow car". A missing/unusable engine curve is treated
	// the same as a missing TuneAsset: reported by name. NOTE what actually happens
	// downstream, corrected on code review (repair cycle 2, MEDIUM-3): the default
	// EngineSetup.TorqueCurve has no authored keys, and
	// UChaosWheeledVehicleMovementComponent::SetupVehicle disables mechanical
	// simulation entirely for an empty curve (logs its own "no torque curve defined"
	// warning) -- it is not "Chaos' engine defaults" driving the car, it is no
	// mechanical simulation at all, which also means the TRANSMISSION values this
	// function writes below are not applied either.
	//
	// CORRECTED BY VEH-004 (VEH-003 review pass 3, LOW-1): the previous version of this
	// comment also claimed steering was skipped. It is not. FSimpleSteeringSim is added
	// in UChaosWheeledVehicleMovementComponent::SetupVehicle OUTSIDE the
	// bMechanicalSimEnabled guard, so the SteeringSetup written below is applied
	// whether or not the engine curve was usable. An inaccurate comment about which
	// subsystems survive a refusal is exactly the kind of thing that sends the next
	// investigation to the wrong file.
	if (TuneAsset->GetPeakNormalisedTorque() > 0.0f)
	{
		FVehicleEngineConfig& Engine = VehicleMovementComponent->EngineSetup;
		Engine.TorqueCurve = TuneAsset->NormalisedTorqueCurve;
		Engine.MaxTorque = TuneAsset->MaxTorqueNm;
		Engine.MaxRPM = TuneAsset->MaxRpm;
		Engine.EngineIdleRPM = TuneAsset->IdleRpm;
		Engine.EngineBrakeEffect = TuneAsset->EngineBrakeEffect;
		Engine.EngineRevUpMOI = TuneAsset->EngineRevUpMoi;
		Engine.EngineRevDownRate = TuneAsset->EngineRevDownRate;

		// VEH-006: re-arm mechanical simulation. This is NOT belt-and-braces; without it
		// this pawn has no engine, no transmission and no differential for its whole life.
		//
		// UChaosWheeledVehicleMovementComponent::bMechanicalSimEnabled is set true in
		// exactly one place -- the component constructor
		// (ChaosWheeledVehicleMovementComponent.cpp:1135). SetupVehicle sets it FALSE
		// when EngineSetup.TorqueCurve is empty (:1539-1549) and there is no path that
		// ever sets it back. The physics state is created once at component REGISTRATION,
		// which happens during spawn, before BeginPlay -- and at that moment
		// EngineSetup.TorqueCurve is still the empty default, because the tune above has
		// not been written yet. So the flag latches off, and the RecreatePhysicsState()
		// that ApplyChassisAsset() performs immediately after this function returns
		// rebuilds the vehicle with a perfectly good curve into a component that has
		// already decided mechanical simulation is off. The `if (bMechanicalSimEnabled)`
		// guard at :1551 then skips FSimpleEngineSim, FSimpleTransmissionSim and
		// FSimpleDifferentialSim entirely.
		//
		// Symptom this produced, and what it cost to find: full throttle, a correctly
		// shaped input command of 1.000 reaching the movement component, wheels created,
		// rigid body settling correctly onto the ground -- and the car sitting still at
		// gear 0 and 0.0 engine rpm, not even the 950 rpm idle. Three build-and-run
		// cycles. Evidence: Saved/Automation/ReportVEH006Man1..3/index.json and the
		// single LogVehicle line in Saved/Logs/RacingSim.log.
		//
		// EnableMechanicalSim is the supported public setter
		// (ChaosWheeledVehicleMovementComponent.h:768-771). It is called only inside this
		// branch: a tune whose curve was refused above must stay disabled, which is the
		// engine's own correct behaviour for an unusable curve.
		VehicleMovementComponent->EnableMechanicalSim(true);

		// The car-spec-version gate (VEH-004 HIGH-2, see the header comment on
		// ApplyTuneAsset()): only set once the engine has genuinely been written, so a
		// refused tune cannot be named in a submitted race result.
		bTuneEngineApplied = true;
	}
	else
	{
		UE_LOG(LogRacingVehicle, Error,
			TEXT("ARacingVehiclePawn '%s': TuneAsset '%s' has an unusable NormalisedTorqueCurve (empty, all-zero, or containing a non-finite key); refusing to write the engine setup rather than sending a NaN into Chaos. With no torque curve authored, Chaos disables mechanical simulation entirely -- this is not merely a defaulted engine."),
			*GetNameSafe(this), *GetNameSafe(TuneAsset));
	}

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

	// Corrected on code review, repair cycle 2 (VEH-003 MEDIUM-1, was MEDIUM-5): the
	// PREVIOUS fix set FVehicleTransmissionConfig::bUseAutoReverse, reasoning that
	// auto-reverse changes what a brake input does at standstill. That field is a
	// genuine no-op on this component's actual simulation path: SetupVehicle
	// instantiates FSimpleTransmissionSim (ChaosWheeledVehicleMovementComponent.cpp),
	// which never reads Setup().AutoReverse at all -- only the separate modular
	// vehicle path (SimModule/TransmissionModule.cpp) does. The field that actually
	// governs "does a brake input at standstill reverse the car" is
	// UChaosVehicleMovementComponent::bReverseAsBrake, a base-class field, which
	// defaults to true and was previously left untouched. Set explicitly here instead,
	// for the same reason the old (wrong) fix gave: reverse is driver-commanded only,
	// via GearRequest reaching Reverse, not auto-triggered by braking at standstill --
	// a behaviour VEH-001's input contract and RACE-002's reverse-crossing invariants
	// were not written expecting.
	VehicleMovementComponent->bReverseAsBrake = false;

	// FVehicleTransmissionConfig::bUseAutoReverse OWNERSHIP, decided and recorded by
	// VEH-004 (VEH-003 review pass 3, LOW-2, which asked for a decision rather than a
	// write). It is DELIBERATELY LEFT UNSET, at Chaos' own InitDefaults() value.
	//
	// Setting it would be theatre: SetupVehicle instantiates FSimpleTransmissionSim,
	// which never reads Setup().AutoReverse at all -- only the separate, unused modular
	// vehicle path (SimModule/TransmissionModule.cpp) does. Writing an inert field to
	// make an ownership table look complete is how the previous repair cycle produced a
	// no-op fix that read as a real one. The behaviour this project actually cares
	// about ("does braking at standstill reverse the car") is governed by
	// bReverseAsBrake, set immediately above, and that is where the ownership sits. If
	// this project ever adopts the modular vehicle path, this field becomes live and
	// must be set to false there for the same reason bReverseAsBrake is.

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
		// VEH-004, closing VEH-003 review pass 3 MEDIUM-1: the steering curve now gets
		// the same gate the torque curve got at HIGH-2, and for the same three reasons
		// (an empty-curve GetLastKey() assert, an Eval(X)/MaxValue divide-by-peak that
		// puts NaN into Chaos, and a MaxX/NumSamples divide-by-zero on a zero-domain
		// curve). ValidateReadOnly() above already REPORTS all three; reporting is not
		// a gate, which is exactly the lesson HIGH-2 taught on the other curve.
		FString SteerCurveReason;
		if (TuneAsset->IsSteerSpeedCurveUsableByChaos(SteerCurveReason))
		{
			Steering.SteeringCurve = TuneAsset->SteerScaleBySpeedMphCurve;
		}
		else
		{
			// Chaos' own InitDefaults() steering curve is left in place. Unlike the
			// engine case, that is a genuine fallback rather than a disabled subsystem:
			// FSimpleSteeringSim is added at ChaosWheeledVehicleMovementComponent.cpp
			// OUTSIDE the bMechanicalSimEnabled guard, so steering is applied whatever
			// happens to the engine. (That fact also closes VEH-003 pass 3 LOW-1, which
			// found the opposite claim written into the engine-refusal comment below.)
			UE_LOG(LogRacingVehicle, Error,
				TEXT("ARacingVehiclePawn '%s': TuneAsset '%s' claims ChaosCurve steer-speed authority but SteerScaleBySpeedMphCurve is unusable -- %s. Refusing to write it; Chaos' own default steering curve remains in force."),
				*GetNameSafe(this), *GetNameSafe(TuneAsset), *SteerCurveReason);
		}

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

		// The mirror image of the MULTIPLY warning above (code review, VEH-003
		// MEDIUM-3): InputLayer authority plus the input config ALSO disabled is a
		// silent NO-OWNER case, not a MULTIPLY -- Chaos is flattened to unity here and
		// VEH-001 applies nothing under ESteerSpeedScaleMode::Off, so the car has NO
		// speed-sensitive steering at all despite both assets reading as though it does.
		if (InputConfigAsset != nullptr && InputConfigAsset->SteerSpeedScaleMode == ESteerSpeedScaleMode::Off)
		{
			UE_LOG(LogRacingVehicle, Warning,
				TEXT("ARacingVehiclePawn '%s': TuneAsset '%s' claims InputLayer steer-speed authority but InputConfigAsset '%s' SteerSpeedScaleMode is Off -- NEITHER system applies speed-sensitive steering. Set the input config to a non-Off mode, or set the tune to ChaosCurve."),
				*GetNameSafe(this), *GetNameSafe(TuneAsset), *GetNameSafe(InputConfigAsset));
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

	bTuneApplied = true;
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
		// VEH-004, closing VEH-001 LOW-2. The return value used to be a bool this
		// function DISCARDED -- which was the only sane thing to do with it, because
		// `false` meant either "the content is broken" or "this is a remote pawn and
		// everything is fine" and there was no way to tell. Now the two are
		// distinguishable, so they get different verbosities and the fault case names
		// itself.
		const EVehicleInputInitResult InitResult =
			VehicleInputComp->InitialiseForController(PlayerController, InitialInputDeviceType);

		if (UVehicleInputComponent::IsVehicleInputInitFault(InitResult))
		{
			UE_LOG(LogRacingVehicle, Error,
				TEXT("ARacingVehiclePawn '%s': input initialisation failed with '%s'; the car will accept no input beyond the safe standing-still command."),
				*GetNameSafe(this), *UEnum::GetValueAsString(InitResult));
		}
		else if (InitResult == EVehicleInputInitResult::NotLocalPlayer)
		{
			// Expected for an AI-driven, spectated or remote pawn. Verbose, so it does
			// not read as a fault in a shipping log.
			UE_LOG(LogRacingVehicle, Verbose,
				TEXT("ARacingVehiclePawn '%s': controller has no local player; no mapping context pushed (expected for a non-local pawn)."),
				*GetNameSafe(this));
		}
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
		// code-reviewer VEH-005 MEDIUM-B: NotifyVehicleReset() preserves held flags for a
		// future Enhanced Input callback to correct; unpossession has no such callback,
		// so this uses the unpossession-specific counterpart instead.
		VehicleInputComp->NotifyUnpossessed();
	}

	// The telemetry half of the same event. Unpossession is a discontinuity: the next
	// possession's first sample must not be compared against a snapshot from before it,
	// or a car that sat unpossessed for a minute reports a time anomaly and a teleport.
	NotifyTelemetryDiscontinuity();

	// RACE-006: a request belongs to the driver who held reset. Left latched, the next
	// possessor's controller would service a reset nobody on it asked for.
	bResetRequestPending = false;
}

void ARacingVehiclePawn::NotifyTelemetryDiscontinuity()
{
	// Deliberately does NOT clear LastSnapshot or LastFailureReport: those are the
	// record of what happened, and a consumer inspecting why a car was reset must still
	// be able to read the sample that preceded it. What is cleared is the COMPARISON
	// BASIS and the accumulators, which is what would otherwise manufacture a fault.
	//
	// SimulationTimeSeconds is likewise NOT cleared: a reset does not un-happen the time
	// that preceded it, and a clock that jumps backwards is precisely the TimeAnomaly the
	// detector would then raise on the first sample after the reset. Zeroing
	// NextCaptureTimeSeconds is enough to make the next Tick capture, because the
	// simulation clock only ever counts up from zero.
	PreviousSnapshot = FVehicleTelemetrySnapshot();

	// NotifyDiscontinuity, NOT Reset. Reset alone drops the accumulators and nothing else,
	// and clearing PreviousSnapshot above does not survive: CaptureAndEvaluateTelemetry
	// opens its next capture with PreviousSnapshot = LastSnapshot, restoring the basis one
	// line before the detector reads it. So an ANNOUNCED reset still raised Tunnelling on
	// the following capture -- and then InvalidContact, because the contact check compares
	// a post-teleport pose against pre-teleport wheel data. VEH-005's ExecuteSafeReset
	// calls this precisely so neither happens. Both were caught by
	// RacingSim.Vehicle.Manoeuvre.FailureDetectorCatchesUnannouncedTeleport.
	// The LOCATION overload, and LastSnapshot rather than GetActorLocation(): the
	// detector needs the pose the physics thread has already seen, which is the last one
	// captured, not the one this actor was just teleported to. See
	// FVehicleFailureDetectorState::PreDiscontinuityLocationCm for the measurement that
	// made this necessary -- the wheel half of the telemetry keeps arriving from before
	// the teleport for a capture longer than the pose half does.
	if (LastSnapshot.bIsValid)
	{
		FailureState.NotifyDiscontinuity(LastSnapshot.LocationCm);
	}
	else
	{
		FailureState.NotifyDiscontinuity();
	}

	LoggedFailureFlags = 0;
	NextCaptureTimeSeconds = 0.0;
}

double ARacingVehiclePawn::GetMinimumResetClearanceCm() const
{
	// No asset means no geometry to reason from, and 0 is the honest answer: it leaves
	// FMath::Max in ExecuteSafeReset with the track's own lift, which is exactly the
	// behaviour this pawn had before the chassis was consulted at all.
	//
	// Warned rather than checked. A tuneless-but-driveable pawn is a legitimate editor
	// state and a fatal check here would take the whole session down for it; but the
	// degraded path is invisible from the outside -- a reset that lands the car partly
	// through the road looks like a track-lift problem, not a missing asset -- so it has
	// to say so once. Once, not per reset: a reset can be spammed, and a repeating log
	// line during a soak is its own defect.
	if (ChassisAsset == nullptr)
	{
		if (!bWarnedMissingChassisForClearance)
		{
			bWarnedMissingChassisForClearance = true;
			UE_LOG(LogRacingVehicle, Warning,
				TEXT("ARacingVehiclePawn '%s' has no ChassisAsset, so the reset clearance falls back to 0 cm and the reset relies entirely on the track's own lift; a reset may leave the tyres intersecting the road."),
				*GetNameSafe(this));
		}

		return 0.0;
	}

	// DEPENDENCY, stated because it is not visible from this function: the number below
	// is a STATIC geometric height -- hub offset plus tyre radius, the pose the car has
	// with its suspension neither compressed nor extended. It is correct as a starting
	// height only while the tune's spring equilibrium puts the body near that pose. A
	// tune whose springs settle the body significantly lower (SuspensionMaxDropCm, or a
	// spring rate far softer than the sprung mass it carries) makes this an over-lift and
	// turns the reset into a drop; one that settles it higher makes it an under-lift. The
	// chassis asset alone cannot see that, because the springs live in the tune asset.
	// If reset behaviour ever changes after a suspension retune, this is why.

	// The deepest corner, not the average and not the front axle: the car must clear the
	// road at EVERY wheel, and the prototype's front and rear radii differ (34 vs 35 cm).
	double DeepestContactPatchBelowOriginCm = 0.0;

	for (int32 CornerIndex = 0; CornerIndex < NumPrototypeVehicleWheels; ++CornerIndex)
	{
		const EVehicleWheelIndex WheelIndex = static_cast<EVehicleWheelIndex>(CornerIndex);

		// WheelCentreHeightCm is negative (the hubs sit below the actor origin), so
		// negating it gives a depth, and the tyre reaches one radius further down.
		const double ContactPatchBelowOriginCm =
			-static_cast<double>(ChassisAsset->GetWheelOffsetCm(WheelIndex).Z)
			+ static_cast<double>(ChassisAsset->GetWheelRadiusCm(WheelIndex));

		DeepestContactPatchBelowOriginCm =
			FMath::Max(DeepestContactPatchBelowOriginCm, ContactPatchBelowOriginCm);
	}

	// No extra margin. This is the height at which the tyres just touch, which is where a
	// suspension wants to start: lifting further makes the reset a small drop, and a drop
	// is the other way to produce the acceleration spike this number exists to prevent.
	return DeepestContactPatchBelowOriginCm;
}

FVehicleFailureThresholds ARacingVehiclePawn::ResolveFailureThresholds() const
{
	// A null asset is the expected configuration today (VEH-004 authors no .uasset,
	// per CLAUDE.md) and yields the POD defaults -- which are the same numbers the
	// asset defaults to, pinned together by
	// RacingSim.Vehicle.FailureThresholdDefaultsMatchAsset.
	return (FailureThresholdsAsset != nullptr)
		? FailureThresholdsAsset->GetThresholds()
		: FVehicleFailureThresholds();
}

FVehicleCameraSettings ARacingVehiclePawn::ResolveCameraSettings() const
{
	// Same null-fallback shape as ResolveFailureThresholds() above, same reason: no
	// .uasset is authored by this ticket, so a null CameraAsset is the expected
	// configuration today, not an error.
	return (CameraAsset != nullptr)
		? CameraAsset->GetSettings()
		: FVehicleCameraSettings();
}

void ARacingVehiclePawn::ApplyCameraSettings()
{
	if (CameraBoom == nullptr || FollowCamera == nullptr)
	{
		return;
	}

	const FVehicleCameraSettings Settings = ResolveCameraSettings();

	CameraBoom->TargetArmLength = Settings.ArmLengthCm;
	CameraBoom->SocketOffset = FVector(Settings.SocketForwardOffsetCm, 0.0, Settings.SocketHeightCm);
	CameraBoom->SetRelativeRotation(FRotator(Settings.CameraPitchDegrees, 0.0f, 0.0f));

	// 0 disables lag rather than merely slowing it to a crawl -- matches
	// USpringArmComponent's own documented meaning for CameraLagSpeed/
	// CameraRotationLagSpeed of 0, so bEnable* here is redundant with the speed being 0
	// in practice, but explicit rather than relying on that engine behaviour silently.
	CameraBoom->bEnableCameraLag = Settings.CameraLagSpeed > 0.0f;
	CameraBoom->CameraLagSpeed = Settings.CameraLagSpeed;
	CameraBoom->bEnableCameraRotationLag = Settings.CameraRotationLagSpeed > 0.0f;
	CameraBoom->CameraRotationLagSpeed = Settings.CameraRotationLagSpeed;

	// Base FOV only -- the speed-adjusted boost is re-applied every Tick (see Tick()
	// below), since it depends on the car's current speed, not just the authored asset.
	//
	// code-reviewer VEH-005 MEDIUM-A2 (repair cycle 2 re-review): this is the OTHER
	// FieldOfView apply site besides Tick's, and it was left unclamped -- reachable
	// whenever Tick's own clamped assignment never runs (its `!bChassisApplied` early
	// return, LOW-2's accepted freeze), leaving this raw, editor-only-bounded value on
	// the camera permanently rather than for one frame. Same runtime safety net as Tick.
	FollowCamera->FieldOfView = RacingSim::Vehicle::ClampFieldOfViewForApplyDegrees(Settings.BaseFieldOfViewDegrees);
}

bool ARacingVehiclePawn::HasPublishableCarSpecVersion() const
{
	return RacingSim::Vehicle::ResolveCarSpecVersion(TuneAsset, bTuneEngineApplied).IsPopulated();
}

bool ARacingVehiclePawn::PublishCarSpecVersionTo(URaceResultRecorder* Recorder)
{
	if (Recorder == nullptr)
	{
		return false;
	}

	const FRacingContentVersion Version = RacingSim::Vehicle::ResolveCarSpecVersion(TuneAsset, bTuneEngineApplied);

	if (!Version.IsPopulated())
	{
		// Refused, loudly, and NOTHING is written. Leaving the recorder's unpopulated
		// default in place is what makes FRacingSimVersionStamp::IsPublishable() refuse
		// the result -- which is the correct outcome for a run whose car cannot be
		// named. Writing a plausible-looking version here would produce a result that
		// passes every check and describes a car nobody drove.
		UE_LOG(LogRacingVehicle, Warning,
			TEXT("ARacingVehiclePawn '%s': no publishable car spec version (TuneAsset '%s', engine applied: %s); the race result will remain unsubmittable, which is correct rather than a defect."),
			*GetNameSafe(this), *GetNameSafe(TuneAsset), bTuneEngineApplied ? TEXT("true") : TEXT("false"));
		return false;
	}

	Recorder->SetCarSpecVersion(Version);

	UE_LOG(LogRacingVehicle, Log,
		TEXT("ARacingVehiclePawn '%s': published car spec version '%s' to the race result recorder."),
		*GetNameSafe(this), *Version.ToString());

	return true;
}

bool ARacingVehiclePawn::ExecuteSafeReset(
	const ATrackDefinitionActor* Track, URaceLapTracker* LapTracker, const double LastValidProgressDistanceCm)
{
	if (!IsValid(Track))
	{
		// Documented no-op, not a crash and not a guessed pose: there is nothing to
		// reset onto without a track. IsValid(), not a raw null check -- Track can be
		// pending-kill (session restart, level reload) independently of this pawn, per
		// this class's own header comment on why the pointer is parameter-injected
		// rather than stored, code-reviewer MEDIUM-4.
		UE_LOG(LogRacingVehicle, Warning,
			TEXT("ARacingVehiclePawn '%s': ExecuteSafeReset called with a null/invalid Track; no-op."),
			*GetNameSafe(this));
		return false;
	}

	// DELIBERATE SUBSTITUTION, DISCLOSED: the ticket text names
	// GetResetTransformAtOrBeforeDistanceCm + GetResetSampleDistanceCm as two calls.
	// GetResetPoseAtOrBeforeDistanceCm (TrackDefinitionActor.h) is that actor's own
	// documented "prefer this overload at every reset site" -- a single call proven
	// equivalent to the two-call form, and it is what CenterlineAmbiguity's own tests
	// exercise. Using it here instead of the literal two-call combination is exactly
	// the substitution this class's header comment tells code-reviewer to expect.
	int32 ResetSampleIndex = INDEX_NONE;
	double ResetSampleDistanceCm = 0.0;
	const FTransform ResetSeedTransform =
		Track->GetResetPoseAtOrBeforeDistanceCm(LastValidProgressDistanceCm, ResetSampleIndex, ResetSampleDistanceCm);

	// Same documented no-op as a null Track: an unbuilt/invalid-centerline track (no
	// reset samples yet) returns the sentinel pair (INDEX_NONE, InvalidDistanceCm) and
	// FTransform::Identity -- teleporting to that would put the car at world origin and
	// hand the lap tracker a -1.0 "progress" distance, code-reviewer HIGH-1.
	if (!RacingSim::Vehicle::IsResetSampleValid(
			ResetSampleIndex, ResetSampleDistanceCm, ATrackDefinitionActor::InvalidDistanceCm))
	{
		UE_LOG(LogRacingVehicle, Warning,
			TEXT("ARacingVehiclePawn '%s': ExecuteSafeReset's track has no valid reset sample (unbuilt or empty ")
			TEXT("centerline); no-op rather than teleporting to a guessed pose."),
			*GetNameSafe(this));
		return false;
	}

	// RACE-002 M3 precondition, CHECKED rather than assumed. GetResetPoseAtOrBeforeDistanceCm
	// already guarantees this by construction, so this is defence-in-depth against a
	// future caller's bug (e.g. passing a distance ahead of the car's real last-valid
	// progress), not a re-implementation of the actor's own guarantee. A failure here is
	// logged and the reset still proceeds -- the pose came from the actor's own
	// guaranteed-safe accessor, so this check exists to surface a caller bug, not to
	// gate the reset on its own output.
	// Derived from the track's own authored spacing, not a literal -- code-reviewer
	// MEDIUM-3. RebuildResetSamples can produce an effective step up to
	// ResetSampleSpacingCm on a track short enough to hit MaxGeneratedSamples, so 2x
	// stays generous without hard-coding a number that silently drifts from the track.
	const double MaxBackwardGapCm = FMath::Max(2.0 * Track->ResetSampleSpacingCm, 100.0);
	if (!RacingSim::Vehicle::IsResetDistanceAtOrBeforeQuery(
			LastValidProgressDistanceCm, ResetSampleDistanceCm, Track->GetTrackLengthCm(), MaxBackwardGapCm))
	{
		UE_LOG(LogRacingVehicle, Warning,
			TEXT("ARacingVehiclePawn '%s': ExecuteSafeReset's resolved reset distance (%f cm) failed the ")
			TEXT("at-or-before-query defence-in-depth check against the requested distance (%f cm); proceeding ")
			TEXT("with the track's own returned pose regardless, since GetResetPoseAtOrBeforeDistanceCm is the ")
			TEXT("authority, not this check."),
			*GetNameSafe(this), ResetSampleDistanceCm, LastValidProgressDistanceCm);
	}

	// One-shot ground trace, not per-Tick: corrects height for a crested or banked reset
	// point rather than blindly trusting the seed's fixed PoseHeightOffsetCm lift.
	const FVector SeedLocation = ResetSeedTransform.GetLocation();

	// THE MAXIMUM OF TWO NUMBERS THAT ANSWER TWO DIFFERENT QUESTIONS.
	//
	// ATrackDefinitionActor::PoseHeightOffsetCm is a TRACK property -- "how far above the
	// road surface to place a car origin" -- authored once for a circuit, with no
	// knowledge of which car will use it. Its 50 cm default is fine for a car whose
	// origin sits near its axle plane and wrong for one whose wheels hang lower. The
	// prototype chassis is the second kind: WheelCentreHeightCm = -35 with a 35 cm rear
	// radius puts the tyre contact patch 70 cm BELOW the origin, so a 50 cm lift plants
	// the car 20 cm inside the slab. The suspension then throws it out, and the detector
	// is right to call that RunawayEnergy:
	//
	//   speed changed by 272.351990 cm/s over 0.016667 s (16341.118533 cm/s^2)
	//
	// -- 16.7 g, on a car that was supposed to have been placed gently. Taking the larger
	// of the two never lowers a car the track wanted higher, and never plants a car whose
	// own geometry needs more room than the track author assumed.
	const double GroundClearanceCm = FMath::Max(
		Track->PoseHeightOffsetCm, GetMinimumResetClearanceCm());

	FHitResult Hit;
	bool bTraceHit = false;
	double TraceHitZCm = 0.0;
	if (UWorld* World = GetWorld())
	{
		const FVector TraceStart = SeedLocation + FVector(0.0, 0.0, GroundClearanceCm * 2.0);
		const FVector TraceEnd = SeedLocation - FVector(0.0, 0.0, GroundClearanceCm * 10.0);
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(VehicleSafeResetGroundTrace), /*bTraceComplex=*/false, this);
		bTraceHit = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams);
		TraceHitZCm = Hit.ImpactPoint.Z;
	}

	if (!bTraceHit)
	{
		UE_LOG(LogRacingVehicle, Log,
			TEXT("ARacingVehiclePawn '%s': ExecuteSafeReset's ground trace found nothing at reset distance %f cm; ")
			TEXT("falling back to the seed pose's own fixed PoseHeightOffsetCm lift."),
			*GetNameSafe(this), ResetSampleDistanceCm);
	}

	FVector ResetLocation = SeedLocation;
	ResetLocation.Z = RacingSim::Vehicle::ResolveGroundCorrectedResetZCm(SeedLocation.Z, GroundClearanceCm, bTraceHit, TraceHitZCm);
	const FRotator ResetRotation = ResetSeedTransform.Rotator();

	// Teleport BEFORE ResetVehicle(), so Chaos never sees a one-frame velocity spike
	// computed from the position jump.
	SetActorLocationAndRotation(ResetLocation, ResetRotation, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

	if (VehicleMovementComponent != nullptr)
	{
		VehicleMovementComponent->ResetVehicle();
	}

	if (ChassisCollision != nullptr)
	{
		ChassisCollision->SetPhysicsLinearVelocity(FVector::ZeroVector);
		ChassisCollision->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}

	// VEH-004's own stated obligation for whatever acts on a reset: without this, the
	// deliberate reposition above is reported as tunnelling rather than the
	// discontinuity it actually is.
	NotifyTelemetryDiscontinuity();

	if (VehicleInputComp != nullptr)
	{
		// Input-side half of the same discontinuity: drops the rate-limiter/latch state
		// so a car reset at full lock or full throttle does not resume at full lock or
		// full throttle on the grid.
		VehicleInputComp->NotifyVehicleReset();
	}

	if (IsValid(LapTracker))
	{
		// RACE-002's contract: a reset must not itself award progress. Parameter
		// injection, not a stored pointer -- this pawn never keeps LapTracker beyond
		// this call, same pattern as PublishCarSpecVersionTo's Recorder above. IsValid(),
		// not a raw null check, for the same pending-kill reason as Track above.
		LapTracker->NotifyVehicleReset(ResetLocation, ResetSampleDistanceCm);
	}

	// RACE-006: stamp the cooldown on the same simulated clock the detector's
	// suppression budget runs on, and drop any request latched before this reset so it
	// cannot fire a second one the moment the cooldown ends.
	bHasExecutedReset = true;
	LastResetSimulationTimeSeconds = SimulationTimeSeconds;
	bResetRequestPending = false;

	UE_LOG(LogRacingVehicle, Log,
		TEXT("ARacingVehiclePawn '%s': ExecuteSafeReset placed the car at distance %f cm (requested %f cm)."),
		*GetNameSafe(this), ResetSampleDistanceCm, LastValidProgressDistanceCm);
	return true;
}

bool ARacingVehiclePawn::ConsumeResetRequest()
{
	const bool bWasPending = bResetRequestPending;
	bResetRequestPending = false;
	return bWasPending;
}

double ARacingVehiclePawn::GetEffectiveResetCooldownSeconds() const
{
	const FVehicleFailureThresholds Thresholds = ResolveFailureThresholds();
	const double MinimumSeconds = RacingSim::Vehicle::ComputeMinimumResetCooldownSeconds(
		Thresholds.MaxContactSuppressionSeconds, TelemetrySampleRateHz);

	if (!bWarnedResetCooldownBelowMinimum
		&& (!FMath::IsFinite(ResetCooldownSeconds) || static_cast<double>(ResetCooldownSeconds) < MinimumSeconds))
	{
		// Raised, not refused: the minimum is the one value that keeps the detector's
		// ceiling out of reach, and refusing every reset would strand the driver.
		// Warned once per pawn so a reset-heavy session cannot fill the log.
		bWarnedResetCooldownBelowMinimum = true;
		UE_LOG(LogRacingVehicle, Warning,
			TEXT("ARacingVehiclePawn '%s': ResetCooldownSeconds %f is below the minimum %f s the failure detector ")
			TEXT("needs at MaxContactSuppressionSeconds %f and TelemetrySampleRateHz %f; enforcing the minimum."),
			*GetNameSafe(this), ResetCooldownSeconds, MinimumSeconds,
			Thresholds.MaxContactSuppressionSeconds, TelemetrySampleRateHz);
	}

	return RacingSim::Vehicle::ResolveEffectiveResetCooldownSeconds(
		ResetCooldownSeconds, Thresholds.MaxContactSuppressionSeconds, TelemetrySampleRateHz);
}

bool ARacingVehiclePawn::CanAcceptResetRequest(FString& OutReason) const
{
	RacingSim::Vehicle::FVehicleResetGateInput GateInput;
	GateInput.bHasPreviousReset = bHasExecutedReset;
	GateInput.SimulationTimeSeconds = SimulationTimeSeconds;
	GateInput.LastResetSimulationTimeSeconds = LastResetSimulationTimeSeconds;
	GateInput.CooldownSeconds = GetEffectiveResetCooldownSeconds();
	GateInput.bCaptureEnabled = TelemetrySampleRateHz > 0.0f && FMath::IsFinite(TelemetrySampleRateHz);
	GateInput.bContactSuppressionArmed = FailureState.bHasPreDiscontinuityLocation;

	const RacingSim::Vehicle::EVehicleResetGateResult Result = RacingSim::Vehicle::EvaluateResetGate(GateInput);
	if (Result == RacingSim::Vehicle::EVehicleResetGateResult::Accepted)
	{
		OutReason.Reset();
		return true;
	}

	OutReason = FString::Printf(
		TEXT("vehicle reset gate refused: %s (simulated %.3f s since last reset, cooldown %.3f s, suppression %s)"),
		RacingSim::Vehicle::LexResetGateResult(Result),
		bHasExecutedReset ? SimulationTimeSeconds - LastResetSimulationTimeSeconds : -1.0,
		GateInput.CooldownSeconds,
		GateInput.bContactSuppressionArmed ? TEXT("armed") : TEXT("expired"));
	return false;
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

	// VEH-005: FOV is the one camera property that is speed-DEPENDENT rather than a
	// fixed rig setting, so unlike ApplyCameraSettings() (BeginPlay, once) this reruns
	// every Tick. ResolveCameraSettings() is a small POD copy, not an allocation or a
	// search, so this is within the module's no-per-frame-allocation rule.
	if (FollowCamera != nullptr)
	{
		const FVehicleCameraSettings CameraSettings = ResolveCameraSettings();
		const float UnclampedFovDegrees = RacingSim::Vehicle::ComputeSpeedAdjustedFieldOfViewDegrees(
			CameraSettings.BaseFieldOfViewDegrees,
			CameraSettings.MaxFieldOfViewBoostDegrees,
			VehicleMovementComponent->GetForwardSpeed(),
			CameraSettings.SpeedForMaxFovBoostCms);

		// code-reviewer VEH-005 MEDIUM-A: UCameraComponent::FieldOfView has no enforcing
		// runtime ceiling of its own (its [5, 170] is UIMin/UIMax, editor-only) -- this is
		// the actual safety net against a misauthored base+boost sum producing a
		// degenerate, negative-tangent projection matrix.
		FollowCamera->FieldOfView = RacingSim::Vehicle::ClampFieldOfViewForApplyDegrees(UnclampedFovDegrees);
	}

	const FVehicleInputCommand Command = VehicleInputComp->GetCommand();

	// VEH-004: the mapped input is RETURNED rather than re-derived. Calling
	// MapCommandToChaosInput a second time for the telemetry would be cheap but wrong
	// in principle -- the recording must be of the values that were actually pushed,
	// not of a second independent evaluation that a future change could make diverge.
	const FVehicleChaosInput AppliedInput = ApplyInputCommand(Command);

	// LAST in the Tick because AppliedInput does not exist until ApplyInputCommand
	// returns it -- corrected on code review (VEH-004 MEDIUM-1), which found the
	// previous comment's ordering claim backwards. This call happens at TG_PrePhysics,
	// BEFORE Chaos steps this frame, so the chassis/wheel state this snapshot reads is
	// necessarily still last physics step's regardless of where in this Tick the
	// capture runs -- capturing earlier would not have paired input with a fresher
	// physics state, only with a stale AppliedInput. The pairing this snapshot records
	// is honestly this frame's input against last step's physics state, matching
	// GetForwardSpeed()'s own comment above.
	CaptureAndEvaluateTelemetry(AppliedInput, Command, DeltaSeconds);
}

void ARacingVehiclePawn::CaptureAndEvaluateTelemetry(
	const FVehicleChaosInput& AppliedInput,
	const FVehicleInputCommand& Command,
	const float DeltaSeconds)
{
	// RACE-006: the simulated clock advances whether or not capture is enabled -- the
	// reset cooldown runs on it, and a pawn with capture disabled must still be able to
	// finish a cooldown. Only the snapshot and the evaluation below are gated.
	SimulationTimeSeconds += static_cast<double>(DeltaSeconds);

	if (!(TelemetrySampleRateHz > 0.0f) || !FMath::IsFinite(TelemetrySampleRateHz))
	{
		// 0 (or a corrupt value) disables capture. Explicitly, and without arming
		// anything: a telemetry system that decides its own rate when misconfigured is
		// a telemetry system that costs frame time nobody budgeted for.
		return;
	}

	// TWO CLOCKS, deliberately, because they answer two different questions.
	//
	// FPlatformTime::Seconds() -- the same monotonic source RACE-001 uses for lap timing
	// and VEH-001's component passes to the processor -- records WHEN IN REAL TIME the
	// sample was taken, which is what a staleness check needs. Deliberately not world
	// time, which a pause or a time dilation moves.
	//
	// SimulationTimeSeconds accumulates the DeltaSeconds that actually produced the
	// motion, and is the clock every RATE is derived from. Only this one can be divided
	// by: the solver advanced by these deltas and by nothing else. VEH-004 had a single
	// wall clock doing both jobs, which is invisible in a real-time session and wrong
	// everywhere else -- see FVehicleTelemetrySnapshot::SimulationTimeSeconds.
	//
	// Accumulated BEFORE the rate gate below (at the top of this function), so
	// decimating capture never loses time: the clock counts every frame, capture reads
	// it every Nth.
	const double NowSeconds = FPlatformTime::Seconds();

	// Paced on SIMULATED time, so the sample rate means the same thing in a fixed-step
	// test as it does at runtime -- a 60 Hz rate is one sample per simulated 1/60 s
	// either way. Pacing on the wall clock instead would let a fast test loop take one
	// sample per Tick regardless of the configured rate.
	if (SimulationTimeSeconds < NextCaptureTimeSeconds)
	{
		return;
	}

	const double IntervalSeconds = 1.0 / static_cast<double>(TelemetrySampleRateHz);

	// Re-based on NOW rather than advanced by one interval from the last due time. The
	// alternative accumulates a backlog after a hitch and then fires every frame to
	// "catch up", which is a burst of capture cost at precisely the moment the frame is
	// already late. Telemetry must never be the reason a hitch gets worse.
	NextCaptureTimeSeconds = SimulationTimeSeconds + IntervalSeconds;

	++CaptureIndex;

	FVehicleTelemetryCaptureInput CaptureInput;
	CaptureInput.Movement = VehicleMovementComponent;
	CaptureInput.Chassis = ChassisCollision;
	CaptureInput.ChaosInput = AppliedInput;
	CaptureInput.ClutchInput = Command.Clutch;
	CaptureInput.InputCorrections = Command.Corrections;
	CaptureInput.InputDeviceType = Command.DeviceType;
	CaptureInput.CarSpecVersion = RacingSim::Vehicle::ResolveCarSpecVersion(TuneAsset, bTuneEngineApplied);
	CaptureInput.TimestampSeconds = NowSeconds;
	CaptureInput.SimulationTimeSeconds = SimulationTimeSeconds;
	CaptureInput.FrameDeltaSeconds = DeltaSeconds;
	CaptureInput.CaptureIndex = CaptureIndex;

	// Unconditional, including across a discontinuity: the detector, not this function,
	// decides what a discontinuity suppresses, and it holds that latch in
	// FVehicleFailureDetectorState. Duplicating the decision here would give one concept
	// two owners that could disagree.
	PreviousSnapshot = LastSnapshot;
	LastSnapshot = RacingSim::Vehicle::CaptureVehicleTelemetry(CaptureInput);

	LastFailureReport = RacingSim::Vehicle::EvaluateVehicleFailures(
		PreviousSnapshot, LastSnapshot, ResolveFailureThresholds(), FailureState);

	// EDGE-TRIGGERED LOGGING. A persistent fault -- and every fault this detector finds
	// is persistent, because a corrupted solver does not recover -- would otherwise emit
	// one line per capture forever, which is both a frame-time cost and an active
	// obstruction: the line that matters is the FIRST one, and it would be buried under
	// a hundred thousand identical successors. Logging only on a change of flags means
	// the log records when each class of fault began.
	if (LastFailureReport.Flags != LoggedFailureFlags)
	{
		if (LastFailureReport.HasAnyFailure())
		{
			UE_LOG(LogRacingVehicle, Error,
				TEXT("ARacingVehiclePawn '%s' VEH-004 failure detected at capture %lld (t=%f, step=%f s) [%s]: %s"),
				*GetNameSafe(this),
				LastFailureReport.CaptureIndex,
				LastFailureReport.TimestampSeconds,
				LastFailureReport.MeasuredStepSeconds,
				*RacingSim::Vehicle::DescribeVehicleFailureFlags(LastFailureReport.Flags),
				*LastFailureReport.Reason);
		}
		else
		{
			UE_LOG(LogRacingVehicle, Log,
				TEXT("ARacingVehiclePawn '%s': vehicle state returned clean at capture %lld."),
				*GetNameSafe(this), LastFailureReport.CaptureIndex);
		}

		LoggedFailureFlags = LastFailureReport.Flags;
	}
}

FVehicleChaosInput ARacingVehiclePawn::ApplyInputCommand(const FVehicleInputCommand& Command)
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

	WakeChassisForInput(ChaosInput);

	// RACE-006: the one-shot reset request is LATCHED here, not acted on. Executing it
	// needs a Track and a race-progress distance neither this pawn nor VehicleInputComp
	// owns; RacingSim::Game::ServiceDriverResetRequest consumes the latch, asks the race
	// director and CanAcceptResetRequest, and only then calls ExecuteSafeReset. A latch
	// rather than a direct read keeps the request alive across the pawn/controller tick
	// order, and ORing keeps a second raise before servicing from being lost or doubled.
	if (Command.bResetRequested)
	{
		bResetRequestPending = true;
	}
	return ChaosInput;
}

void ARacingVehiclePawn::WakeChassisForInput(const FVehicleChaosInput& ChaosInput)
{
	if (ChassisCollision == nullptr)
	{
		return;
	}

	// Close to, but deliberately NOT identical to, what
	// UChaosVehicleMovementComponent::ProcessSleeping treats as "a control input is
	// pressed" (ChaosVehicleMovementComponent.cpp:1389-1394). Three differences, all
	// intentional:
	//
	//   - The roll, pitch and yaw axes are dropped. This pawn never writes them.
	//   - Steering is compared against the tolerance directly, where Chaos compares the
	//     DELTA against the previous frame's value. Chaos runs every frame on a car that
	//     may be mid-corner; this only ever fires on a car the solver has already parked,
	//     where held lock is as much a request to move as a change of lock is.
	//   - bHandbrake is ADDED. Chaos has no handbrake term at all.
	//
	// Consequence of the last two, worth knowing before trusting the cheap-path argument
	// below: a car parked with the handbrake held, or with steering held off centre, is
	// woken again every time the solver parks it, so it oscillates wake-sleep-wake for as
	// long as the input is held instead of settling. One local car, one wake per park, and
	// the alternative is a handbraked car that can never be released -- but it is not
	// free.
	const bool bDriverAsksForMotion =
		ChaosInput.Throttle >= ChassisWakeInputTolerance
		|| ChaosInput.Brake >= ChassisWakeInputTolerance
		|| FMath::Abs(ChaosInput.Steering) >= ChassisWakeInputTolerance
		|| ChaosInput.bHandbrake;
	if (!bDriverAsksForMotion)
	{
		return;
	}

	// Checked only after the input test above, so a coasting car pays no physics query at
	// all and a car under power pays one query and no write into the physics scene. That
	// is every frame of a normal lap.
	if (ChassisCollision->IsAnyRigidBodyAwake())
	{
		return;
	}

	ChassisCollision->WakeAllRigidBodies();
}
