// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/RacingVehiclePawn.h"

#include "Components/BoxComponent.h"
#include "Core/RacingSimLog.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
// VEH-004: the project's first Vehicle/ -> Race/ dependency, confined to this .cpp.
// The direction is justified in ARacingVehiclePawn::PublishCarSpecVersionTo's header
// comment -- only the pawn knows whether a tune was APPLIED as opposed to referenced,
// so a Race-side pull could not be correct.
#include "Race/RaceResult.h"
#include "Vehicle/PrototypeVehicleWheel.h"
#include "Vehicle/VehicleFailureThresholdsDataAsset.h"
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
		VehicleInputComp->NotifyVehicleReset();
	}

	// The telemetry half of the same event. Unpossession is a discontinuity: the next
	// possession's first sample must not be compared against a snapshot from before it,
	// or a car that sat unpossessed for a minute reports a time anomaly and a teleport.
	NotifyTelemetryDiscontinuity();
}

void ARacingVehiclePawn::NotifyTelemetryDiscontinuity()
{
	// Deliberately does NOT clear LastSnapshot or LastFailureReport: those are the
	// record of what happened, and a consumer inspecting why a car was reset must still
	// be able to read the sample that preceded it. What is cleared is the COMPARISON
	// BASIS and the accumulators, which is what would otherwise manufacture a fault.
	PreviousSnapshot = FVehicleTelemetrySnapshot();
	FailureState.Reset();
	LoggedFailureFlags = 0;
	NextCaptureTimeSeconds = 0.0;
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
	if (!(TelemetrySampleRateHz > 0.0f) || !FMath::IsFinite(TelemetrySampleRateHz))
	{
		// 0 (or a corrupt value) disables capture. Explicitly, and without arming
		// anything: a telemetry system that decides its own rate when misconfigured is
		// a telemetry system that costs frame time nobody budgeted for.
		return;
	}

	// FPlatformTime::Seconds() -- the same monotonic source RACE-001 uses for lap
	// timing and VEH-001's component passes to the processor. Deliberately NOT world
	// time, which a pause or a time dilation moves, and deliberately not accumulated
	// from DeltaSeconds, which drifts.
	const double NowSeconds = FPlatformTime::Seconds();

	if (NowSeconds < NextCaptureTimeSeconds)
	{
		return;
	}

	const double IntervalSeconds = 1.0 / static_cast<double>(TelemetrySampleRateHz);

	// Re-based on NOW rather than advanced by one interval from the last due time. The
	// alternative accumulates a backlog after a hitch and then fires every frame to
	// "catch up", which is a burst of capture cost at precisely the moment the frame is
	// already late. Telemetry must never be the reason a hitch gets worse.
	NextCaptureTimeSeconds = NowSeconds + IntervalSeconds;

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
	CaptureInput.FrameDeltaSeconds = DeltaSeconds;
	CaptureInput.CaptureIndex = CaptureIndex;

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

	// Reset is VEH-005's scope (the reset POSE); this pawn deliberately does not act
	// on Command.bResetRequested. VEH-001's input-side reset (NotifyVehicleReset)
	// only clears input smoothing state, and is called from UnPossessed/an external
	// reset trigger, not from a one-shot flag read every Tick.
	//
	// VEH-004 NOTE FOR VEH-005: whatever acts on bResetRequested must also call
	// NotifyTelemetryDiscontinuity(), or the deliberate reposition will be detected and
	// reported as tunnelling. That is the detector working correctly on the wrong
	// event, and it is the one integration obligation this ticket hands forward.
	return ChaosInput;
}
