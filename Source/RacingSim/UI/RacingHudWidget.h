// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/SlateWrapperTypes.h"
#include "UI/RacingHudViewModel.h"
#include "RacingHudWidget.generated.h"

class UPanelWidget;
class UTextBlock;
class UWidget;

/**
 * UI-002: the in-race HUD.
 *
 * ===========================================================================
 * What it does
 * ===========================================================================
 *
 * Shows one FRacingHudViewModel. ApplyViewModel is the only input; every rule about what
 * to show was decided when the view model was built (UI-001), and every string comes from
 * URacingHudFormatLibrary. The widget holds no race, vehicle or world reference, so it
 * cannot drift from race truth and cannot search for it.
 *
 * ===========================================================================
 * Native tree, Blueprint restyle
 * ===========================================================================
 *
 * Used as-is, the native class builds a plain default layout in C++ so the HUD works
 * without any .uasset. It is built in InitializeNativeClassData(), which UUserWidget runs
 * before NativeOnInitialized(), so every bound property is already set when
 * OnInitialized fires. A Widget Blueprint subclass that authors its own tree keeps it:
 * the default is built only when the tree has no root. The Blueprint names its widgets
 * like the BindWidgetOptional properties below to have them driven; any it leaves out are
 * skipped. A Blueprint subclass with an EMPTY tree falls back to building the default in
 * Initialize(), after OnInitialized; its bindings are therefore still null there.
 *
 * Automation covers the tree and bindings, but not OnInitialized ordering: the test world
 * has no local player, so no player context and no OnInitialized (forwarded to RACE-005,
 * which owns putting the HUD on a player's screen).
 *
 * Every widget in the default tree is HitTestInvisible. UI-003 must relax that for the
 * results panel before adding a clickable restart control to it.
 *
 * ===========================================================================
 * No per-frame churn
 * ===========================================================================
 *
 * ApplyViewModel may be called every frame. Each text field is keyed by the integer its
 * text is made from (whole km/h, whole milliseconds, ...), using the formatter's own
 * rounding helpers, and SetText runs only when that key changes. A car holding 100 km/h
 * therefore costs no FText allocation and no Slate invalidation per frame.
 * Visibility is cached the same way. GetTextUpdateCountForTest() and
 * GetVisibilityUpdateCountForTest() expose the SetText and SetVisibility counts so
 * automation can prove it.
 */
UCLASS()
class RACINGSIM_API URacingHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Smallest font size, in Slate font points, the default tree uses. Streamed video is
	 * compressed and often viewed on a laptop; smaller text does not survive the encoder.
	 */
	static constexpr int32 MinReadableFontSize = 18;

	URacingHudWidget(const FObjectInitializer& ObjectInitializer);

	//~ UUserWidget
	virtual bool Initialize() override;
	//~ End UUserWidget

	/**
	 * Show ViewModel. Cheap to call every frame: widgets whose displayed value did not
	 * change are not touched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Racing|HUD")
	void ApplyViewModel(const FRacingHudViewModel& ViewModel);

	/** Forget what is on screen, so the next ApplyViewModel rewrites every bound widget. */
	UFUNCTION(BlueprintCallable, Category = "Racing|HUD")
	void InvalidateDisplayCache();

	/** SetText calls made by ApplyViewModel since construction. Automation only. */
	int32 GetTextUpdateCountForTest() const { return TextUpdateCount; }

	/** SetVisibility calls made by ApplyViewModel since construction. Automation only. */
	int32 GetVisibilityUpdateCountForTest() const { return VisibilityUpdateCount; }

	// -- Bound widgets. Optional: a Blueprint layout may omit any of them. ------

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SpeedText;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SpeedUnitText;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RpmText;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GearText;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LapCounterText;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CurrentLapTimeText;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LastLapTimeText;

	/** Hidden until a valid best lap exists. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BestLapTimeText;

	/** Hidden unless the view model carries a delta. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DeltaText;

	/** Hidden unless the lap in progress has been invalidated. Static text. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> InvalidLapMarker;

	/** Hidden unless position is meaningful (opponents exist and the car is classified). */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PositionText;

	/** Hidden outside the countdown. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CountdownText;

	/** Hidden until a frozen result exists in Finished or Results. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> ResultsPanel;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultTimeText;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultValidityText;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultBestLapText;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultLapsText;

protected:
	//~ UUserWidget
	virtual void InitializeNativeClassData() override;
	//~ End UUserWidget

	/** Build the default layout into WidgetTree and bind the properties above to it. */
	void BuildDefaultTree();

private:
	enum class ETextField : uint8
	{
		Speed,
		SpeedUnit,
		RPM,
		Gear,
		LapCounter,
		CurrentLapTime,
		LastLapTime,
		BestLapTime,
		Delta,
		Countdown,
		Position,
		ResultTime,
		ResultValidity,
		ResultBestLap,
		ResultLaps,
		Count
	};

	enum class EVisibilityField : uint8
	{
		Countdown,
		Position,
		Delta,
		BestLap,
		InvalidLap,
		Results,
		Count
	};

	/** Key no displayed value produces: the first ApplyViewModel after construction or invalidation writes everything. */
	static constexpr int64 UnwrittenKey = MIN_int64;

	/** Visibility cache value meaning "not yet written". */
	static constexpr uint8 UnwrittenVisibility = 0xFF;

	/** SetText on Block with Format() only when Key differs from the last key written to Field. */
	template <typename FormatFunc>
	void UpdateText(UTextBlock* Block, ETextField Field, int64 Key, FormatFunc&& Format);

	void UpdateVisibility(UWidget* Target, EVisibilityField Field, bool bVisible);

	int64 TextKeys[static_cast<int32>(ETextField::Count)];
	uint8 VisibilityKeys[static_cast<int32>(EVisibilityField::Count)];
	int32 TextUpdateCount = 0;
	int32 VisibilityUpdateCount = 0;
};
