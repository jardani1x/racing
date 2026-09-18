// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/RacingHudTypes.h"
#include "GameFramework/PlayerController.h"
#include "UI/RacingHudViewModel.h"
#include "RacingPlayerController.generated.h"

class ARaceDirector;
class URacingHudWidget;

/**
 * RACE-005: the local player's controller. Owns the race HUD and feeds it every frame.
 *
 * ONE PATH TO THE SCREEN: director race inputs + pawn telemetry sample
 *   -> URacingHudViewModelLibrary::BuildHudViewModelInto -> URacingHudWidget::ApplyViewModel.
 * The controller holds no race truth; it copies what the director and pawn already publish.
 *
 * TICK ORDER. The controller ticks in the default group (TG_PrePhysics) and the director in
 * TG_PostPhysics, so the race inputs the HUD shows are the ones the director computed last
 * frame: at most one frame late, which no displayed field can resolve. Lap timing itself is
 * on the monotonic race clock and is unaffected.
 *
 * The widget is created in ReceivedPlayer (a local player exists) and added to the viewport
 * only when the world has one, so a headless automation world still builds and feeds it.
 */
UCLASS()
class RACINGSIM_API ARacingPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ARacingPlayerController();

	/** HUD widget to create. A Blueprint subclass of URacingHudWidget supplies the styled layout. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Racing|HUD")
	TSubclassOf<URacingHudWidget> HudWidgetClass;

	/** Build this frame's view model and apply it to the HUD. Called from Tick; public for automation. */
	void UpdateHud();

	URacingHudWidget* GetHudWidget() const { return HudWidget; }

	/** The view model most recently applied. Automation only. */
	const FRacingHudViewModel& GetLastViewModelForTest() const { return ViewModel; }

	//~ Begin APlayerController interface
	virtual void ReceivedPlayer() override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End APlayerController interface

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void EnsureHudWidget();

	/** The director, resolved once through ARacingGameMode. Null on clients and non-racing modes. */
	ARaceDirector* ResolveDirector();

	UPROPERTY(Transient)
	TObjectPtr<URacingHudWidget> HudWidget;

	TWeakObjectPtr<ARaceDirector> CachedDirector;
	bool bDirectorResolved = false;

	/** Reused every frame, so the per-frame path allocates nothing new. */
	FRacingHudRaceInputs RaceInputs;
	FRacingHudViewModel ViewModel;
};
