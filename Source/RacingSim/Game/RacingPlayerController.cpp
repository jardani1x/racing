// Copyright RacingSim. All Rights Reserved.

#include "Game/RacingPlayerController.h"

#include "Core/RacingSimLog.h"
#include "Core/RacingSimSettings.h"
#include "Core/RacingTelemetry.h"
#include "Core/RacingTelemetryFunctionLibrary.h"
#include "Game/RacingGameMode.h"
#include "Race/RaceDirector.h"
#include "UI/RacingHudWidget.h"
#include "Vehicle/RacingVehiclePawn.h"
#include "Vehicle/VehicleTelemetryTypes.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

ARacingPlayerController::ARacingPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	HudWidgetClass = URacingHudWidget::StaticClass();
}

void ARacingPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();
	EnsureHudWidget();
}

void ARacingPlayerController::EnsureHudWidget()
{
	if (HudWidget != nullptr || !IsLocalController())
	{
		return;
	}

	UClass* WidgetClass = HudWidgetClass != nullptr ? HudWidgetClass.Get() : URacingHudWidget::StaticClass();
	HudWidget = CreateWidget<URacingHudWidget>(this, WidgetClass);
	if (HudWidget == nullptr)
	{
		UE_LOG(LogRacingCore, Error, TEXT("%s: failed to create HUD widget %s."), *GetName(), *GetNameSafe(WidgetClass));
		return;
	}

	const UWorld* World = GetWorld();
	if (World != nullptr && World->GetGameViewport() != nullptr)
	{
		HudWidget->AddToViewport();
	}
}

ARaceDirector* ARacingPlayerController::ResolveDirector()
{
	if (!bDirectorResolved)
	{
		// Once: the game mode spawns the director before any login and keeps it for the
		// session. GetAuthGameMode is null on a client, which has no race truth to show.
		bDirectorResolved = true;
		if (const UWorld* World = GetWorld())
		{
			if (const ARacingGameMode* GameMode = World->GetAuthGameMode<ARacingGameMode>())
			{
				CachedDirector = GameMode->GetRaceDirector();
			}
		}
	}
	return CachedDirector.Get();
}

void ARacingPlayerController::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateHud();
}

void ARacingPlayerController::UpdateHud()
{
	if (HudWidget == nullptr)
	{
		return;
	}

	if (const ARaceDirector* Director = ResolveDirector())
	{
		Director->GatherHudRaceInputs(RaceInputs);
	}
	else
	{
		RaceInputs = FRacingHudRaceInputs();
	}

	// A default sample has timestamp 0, so it reads as stale and the gauges blank rather than
	// show zeros as if they were live.
	FRacingVehicleTelemetrySample VehicleSample;
	if (const ARacingVehiclePawn* Vehicle = Cast<ARacingVehiclePawn>(GetPawn()))
	{
		VehicleSample = Vehicle->GetLastTelemetrySnapshot().ToRacingVehicleSample();
	}

	// Vehicle samples are stamped on FPlatformTime::Seconds() (Core/RacingTelemetry.h), so
	// "now" must come from the same clock.
	URacingHudViewModelLibrary::BuildHudViewModelInto(
		RaceInputs,
		VehicleSample,
		FPlatformTime::Seconds(),
		URacingTelemetryFunctionLibrary::GetTelemetryStaleAfterSeconds(),
		URacingSimSettings::Get().DefaultSpeedDisplayUnit,
		ViewModel);

	HudWidget->ApplyViewModel(ViewModel);
}

void ARacingPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HudWidget != nullptr)
	{
		HudWidget->RemoveFromParent();
		HudWidget = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}
