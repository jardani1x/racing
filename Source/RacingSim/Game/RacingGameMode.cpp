// Copyright RacingSim. All Rights Reserved.

#include "Game/RacingGameMode.h"

#include "Core/RacingSimLog.h"
#include "Game/RacingGrayboxGround.h"
#include "Game/RacingPlayerController.h"
#include "Race/RaceDirector.h"
#include "Race/RaceResult.h"
#include "Race/TrackCenterline.h"
#include "Race/TrackDefinitionActor.h"
#include "Vehicle/RacingVehiclePawn.h"
#include "Vehicle/VehicleChassisDataAsset.h"
#include "Vehicle/VehicleTuneDataAsset.h"

#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"

ARacingGameMode::ARacingGameMode()
{
	DefaultPawnClass = ARacingVehiclePawn::StaticClass();
	PlayerControllerClass = ARacingPlayerController::StaticClass();
	DirectorClass = ARaceDirector::StaticClass();
	// HUDClass stays the engine default: the race HUD is a UMG widget the controller owns.
}

void ARacingGameMode::PreInitializeComponents()
{
	Super::PreInitializeComponents();

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// Before any login (see the class comment). Deferred so the ruleset is in place
	// before the director can run its setup.
	UClass* ClassToSpawn = DirectorClass != nullptr ? DirectorClass.Get() : ARaceDirector::StaticClass();
	RaceDirector = World->SpawnActorDeferred<ARaceDirector>(
		ClassToSpawn, FTransform::Identity, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (RaceDirector == nullptr)
	{
		UE_LOG(LogRacingCore, Error, TEXT("ARacingGameMode: failed to spawn the race director; no race session will run."));
		return;
	}

	if (Ruleset != nullptr)
	{
		RaceDirector->Ruleset = Ruleset;
	}
	UGameplayStatics::FinishSpawningActor(RaceDirector, FTransform::Identity);
}

void ARacingGameMode::StartPlay()
{
	// Runs world BeginPlay: every actor's BeginPlay, the pawn's included.
	Super::StartPlay();

	if (RaceDirector != nullptr)
	{
		FString Reason;
		if (RaceDirector->EnsureSessionSetup(Reason))
		{
			EnsureGrayboxGround(RaceDirector->GetTrack());
		}
	}

	if (ARacingVehiclePawn* Vehicle = PendingCarSpecPawn.Get())
	{
		PendingCarSpecPawn.Reset();
		if (RaceDirector != nullptr)
		{
			Vehicle->PublishCarSpecVersionTo(RaceDirector->GetResultRecorder());
		}
	}
}

void ARacingGameMode::RestartPlayer(AController* NewPlayer)
{
	if (NewPlayer == nullptr || NewPlayer->IsPendingKillPending())
	{
		return;
	}

	FString Reason = TEXT("no race director was spawned");
	ATrackDefinitionActor* Track = nullptr;
	if (RaceDirector != nullptr && RaceDirector->EnsureSessionSetup(Reason))
	{
		Track = RaceDirector->GetTrack();
	}

	double GridDistanceCm = ATrackDefinitionActor::InvalidDistanceCm;
	FTransform Pose = FTransform::Identity;
	if (IsValid(Track))
	{
		Pose = Track->GetGridSlotPose(GridSlotIndex, GridDistanceCm);
		if (GridDistanceCm < 0.0)
		{
			Reason = FString::Printf(TEXT("grid slot %d does not exist on track '%s'"), GridSlotIndex, *Track->GetName());
		}
	}

	if (!IsValid(Track) || GridDistanceCm < 0.0)
	{
		UE_LOG(LogRacingCore, Error,
			TEXT("ARacingGameMode: cannot place the car on the grid (%s); falling back to a PlayerStart. The car will not be raced."),
			*Reason);
		Super::RestartPlayer(NewPlayer);
		return;
	}

	// Ground before the car, so the car never exists above nothing.
	EnsureGrayboxGround(Track);

	Pose.SetScale3D(FVector::OneVector);
	Pose.AddToTranslation(FVector(0.0, 0.0, GridSpawnHeightCm));
	RestartPlayerAtTransform(NewPlayer, Pose);

	if (APawn* Pawn = NewPlayer->GetPawn())
	{
		OnCompetitorPlaced(Pawn, GridDistanceCm);
	}
}

APawn* ARacingGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
	UWorld* World = GetWorld();
	if (World == nullptr || PawnClass == nullptr || !PawnClass->IsChildOf(ARacingVehiclePawn::StaticClass()))
	{
		return Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayer, SpawnTransform);
	}

	// Deferred: ARacingVehiclePawn::BeginPlay refuses a null chassis, so the assets must be
	// in place between construction and FinishSpawningActor. Transient, as the engine's own
	// default pawn spawn is: a pawn is session state, never saved with the level.
	FActorSpawnParameters SpawnParams;
	SpawnParams.Instigator = GetInstigator();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bDeferConstruction = true;
	ARacingVehiclePawn* Vehicle = World->SpawnActor<ARacingVehiclePawn>(PawnClass, SpawnTransform, SpawnParams);
	if (Vehicle == nullptr)
	{
		UE_LOG(LogRacingCore, Error, TEXT("ARacingGameMode: failed to spawn vehicle pawn %s."), *PawnClass->GetName());
		return nullptr;
	}

	const bool bNeedsChassis = (Vehicle->ChassisAsset == nullptr);
	const bool bNeedsTune = (Vehicle->TuneAsset == nullptr);
	if (bNeedsChassis && DefaultChassisAsset == nullptr && TransientChassisAsset == nullptr)
	{
		TransientChassisAsset = NewObject<UVehicleChassisDataAsset>(this, TEXT("GrayboxDefaultChassis"), RF_Transient);
		UE_LOG(LogRacingCore, Warning,
			TEXT("ARacingGameMode: no chassis asset on %s or DefaultChassisAsset; using a transient chassis with C++ defaults (graybox only)."),
			*PawnClass->GetName());
	}
	if (bNeedsTune && DefaultTuneAsset == nullptr && TransientTuneAsset == nullptr)
	{
		TransientTuneAsset = NewObject<UVehicleTuneDataAsset>(this, TEXT("GrayboxDefaultTune"), RF_Transient);
		UE_LOG(LogRacingCore, Warning,
			TEXT("ARacingGameMode: no tune asset on %s or DefaultTuneAsset; using a transient tune with C++ defaults (graybox only)."),
			*PawnClass->GetName());
	}

	if (bNeedsChassis)
	{
		Vehicle->ChassisAsset = DefaultChassisAsset != nullptr ? DefaultChassisAsset.Get() : TransientChassisAsset.Get();
	}
	if (bNeedsTune)
	{
		Vehicle->TuneAsset = DefaultTuneAsset != nullptr ? DefaultTuneAsset.Get() : TransientTuneAsset.Get();
	}

	UGameplayStatics::FinishSpawningActor(Vehicle, SpawnTransform);
	return Vehicle;
}

void ARacingGameMode::EnsureGrayboxGround(const ATrackDefinitionActor* Track)
{
	UWorld* World = GetWorld();
	if (!bSpawnGrayboxGround || GrayboxGround != nullptr || World == nullptr || !IsValid(Track))
	{
		return;
	}

	const FTrackCenterline& Centerline = Track->GetCenterline();
	if (!Centerline.IsValid())
	{
		return;
	}

	// Once, at session start. The baked centerline is in world space.
	const int32 SampleCount = Centerline.NumSamples();
	const double LengthCm = Centerline.GetLengthCm();
	FBox2D Bounds(ForceInit);
	double LowestZ = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		const FVector Location = Centerline.GetLocationAtDistanceCm(LengthCm * static_cast<double>(Index) / static_cast<double>(SampleCount));
		Bounds += FVector2D(Location.X, Location.Y);
		LowestZ = FMath::Min(LowestZ, Location.Z);
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	GrayboxGround = World->SpawnActor<ARacingGrayboxGround>(ARacingGrayboxGround::StaticClass(), FTransform::Identity, Params);
	if (GrayboxGround == nullptr)
	{
		UE_LOG(LogRacingCore, Error, TEXT("ARacingGameMode: failed to spawn the graybox ground; the car has nothing to drive on."));
		return;
	}

	GrayboxGround->ConfigureSurface(Bounds, LowestZ, GrayboxGroundMarginCm);
}

void ARacingGameMode::OnCompetitorPlaced(APawn* Pawn, const double GridDistanceCm)
{
	if (RaceDirector == nullptr)
	{
		return;
	}

	if (ARacingVehiclePawn* Vehicle = Cast<ARacingVehiclePawn>(Pawn))
	{
		if (URaceResultRecorder* Recorder = RaceDirector->GetResultRecorder())
		{
			Recorder->SetInputDeviceType(Vehicle->InitialInputDeviceType);

			// The pawn knows which tune it applied only after its BeginPlay. On LoadMap the
			// player is placed before world BeginPlay, so the publish waits for StartPlay.
			if (Vehicle->HasActorBegunPlay())
			{
				Vehicle->PublishCarSpecVersionTo(Recorder);
			}
			else
			{
				PendingCarSpecPawn = Vehicle;
			}
		}
	}

	FString Reason;
	if (!RaceDirector->RegisterCompetitor(Pawn, GridDistanceCm, Reason))
	{
		UE_LOG(LogRacingCore, Error, TEXT("ARacingGameMode: the race director refused %s: %s"), *GetNameSafe(Pawn), *Reason);
	}
}
