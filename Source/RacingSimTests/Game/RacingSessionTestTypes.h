// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "Game/RacingGameMode.h"
#include "Race/RaceRulesetDataAsset.h"
#include "UI/RacingHudWidget.h"
#include "UObject/UnrealType.h"
#include "Vehicle/VehicleChassisDataAsset.h"
#include "Vehicle/VehicleTuneDataAsset.h"
#include "RacingSessionTestTypes.generated.h"

/**
 * RACE-005 automation only. UI-002 M2 residual: records what NativeOnInitialized saw, which
 * only a widget with a real player context ever reaches.
 */
UCLASS(NotBlueprintable, Transient, HideDropdown)
class URacingHudProbeWidget : public URacingHudWidget
{
	GENERATED_BODY()

public:
	bool bNativeOnInitializedRan = false;

	/** UTextBlock bindings declared on URacingHudWidget, counted inside NativeOnInitialized. */
	int32 TextBindingCountAtInit = 0;

	/** Names of those bindings that were still null inside NativeOnInitialized. */
	TArray<FName> NullTextBindingsAtInit;

	TWeakObjectPtr<ULocalPlayer> OwningLocalPlayerAtInit;

protected:
	virtual void NativeOnInitialized() override
	{
		Super::NativeOnInitialized();

		bNativeOnInitializedRan = true;
		OwningLocalPlayerAtInit = GetOwningLocalPlayer();

		// By reflection, so a binding added to URacingHudWidget later is covered without
		// editing this probe.
		for (TFieldIterator<FObjectProperty> It(URacingHudWidget::StaticClass()); It; ++It)
		{
			if (It->PropertyClass == nullptr || !It->PropertyClass->IsChildOf(UTextBlock::StaticClass()))
			{
				continue;
			}
			++TextBindingCountAtInit;
			if (It->GetObjectPropertyValue_InContainer(this) == nullptr)
			{
				NullTextBindingsAtInit.Add(It->GetFName());
			}
		}
	}
};

/**
 * RACE-005 automation only. ARacingGameMode with a zero-second countdown and explicit
 * transient car assets, set in the constructor because the director runs its setup when
 * the game mode spawns it -- before a test could hand it anything. Explicit assets keep the
 * graybox-default warnings out of the composition test.
 */
UCLASS(NotBlueprintable, Transient, HideDropdown, NotPlaceable)
class ARacingSessionTestGameMode : public ARacingGameMode
{
	GENERATED_BODY()

public:
	static constexpr int32 TestLapsToFinish = 1;

	ARacingSessionTestGameMode()
	{
		URaceRulesetDataAsset* TestRuleset = CreateDefaultSubobject<URaceRulesetDataAsset>(TEXT("TestRuleset"));
		TestRuleset->RulesetId = FName(TEXT("Ruleset.Test.Session"));
		TestRuleset->CountdownSeconds = 0.0;
		TestRuleset->LapsToFinish = TestLapsToFinish;
		Ruleset = TestRuleset;

		DefaultChassisAsset = CreateDefaultSubobject<UVehicleChassisDataAsset>(TEXT("TestChassis"));
		DefaultTuneAsset = CreateDefaultSubobject<UVehicleTuneDataAsset>(TEXT("TestTune"));
	}
};
