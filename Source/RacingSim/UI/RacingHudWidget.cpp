// Copyright RacingSim. All Rights Reserved.

#include "UI/RacingHudWidget.h"

#include "UI/RacingHudFormat.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace RacingHudWidgetPrivate
{
	// Default-layout font sizes, Slate points. All >= URacingHudWidget::MinReadableFontSize;
	// RacingSim.UI.HudWidget.Tree checks every text block in the tree against that floor.
	constexpr int32 SpeedFontSize = 64;
	constexpr int32 CountdownFontSize = 96;
	constexpr int32 PrimaryFontSize = 32;
	constexpr int32 SecondaryFontSize = 24;
	constexpr int32 LabelFontSize = URacingHudWidget::MinReadableFontSize;

	constexpr float EdgePaddingPx = 32.0f;

	UTextBlock* MakeText(UWidgetTree& Tree, const FName Name, const int32 FontSize,
		const ETextJustify::Type Justify = ETextJustify::Left)
	{
		UTextBlock* Text = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = FontSize;
		Text->SetFont(Font);
		Text->SetJustification(Justify);
		// The HUD never takes input; the streamed player's clicks go to the game.
		Text->SetVisibility(ESlateVisibility::HitTestInvisible);
		return Text;
	}

	UVerticalBox* AddColumn(UWidgetTree& Tree, UOverlay& Root, const FName Name,
		const EHorizontalAlignment HAlign, const EVerticalAlignment VAlign)
	{
		UVerticalBox* Column = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), Name);
		Column->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* ColumnSlot = Root.AddChildToOverlay(Column);
		ColumnSlot->SetHorizontalAlignment(HAlign);
		ColumnSlot->SetVerticalAlignment(VAlign);
		ColumnSlot->SetPadding(FMargin(EdgePaddingPx));
		return Column;
	}

	void AddToColumn(UVerticalBox& Column, UWidget* Child, const EHorizontalAlignment HAlign)
	{
		UVerticalBoxSlot* ChildSlot = Column.AddChildToVerticalBox(Child);
		ChildSlot->SetHorizontalAlignment(HAlign);
	}

	/** Lap-time key: whole milliseconds, or -1 for the absent form (which non-finite also formats as). */
	int64 LapTimeKey(const bool bHasTime, const double Seconds)
	{
		return bHasTime ? URacingHudFormatLibrary::RoundLapTimeMilliseconds(Seconds) : -1;
	}
}

URacingHudWidget::URacingHudWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InvalidateDisplayCache();
}

void URacingHudWidget::InitializeNativeClassData()
{
	Super::InitializeNativeClassData();

	// Native class only: UUserWidget::Initialize calls this before it would create the tree
	// and before NativeOnInitialized, so building here means OnInitialized sees every
	// binding. Created exactly as UUserWidget::Initialize would create it.
	if (WidgetTree == nullptr)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultTree();
	}
}

bool URacingHudWidget::Initialize()
{
	const bool bFirstInitialize = Super::Initialize();

	// Fallback for a Blueprint subclass whose tree is empty (InitializeNativeClassData does
	// not run for Blueprint classes). This runs after OnInitialized, so that subclass sees
	// null bindings there. A Blueprint subclass's authored tree has a root and is left alone.
	if (bFirstInitialize && WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildDefaultTree();
	}
	return bFirstInitialize;
}

void URacingHudWidget::BuildDefaultTree()
{
	using namespace RacingHudWidgetPrivate;

	UWidgetTree& Tree = *WidgetTree;

	UOverlay* Root = Tree.ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("HudRoot"));
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);
	Tree.RootWidget = Root;

	// Timing, top left.
	UVerticalBox* Timing = AddColumn(Tree, *Root, TEXT("TimingColumn"), HAlign_Left, VAlign_Top);
	LapCounterText = MakeText(Tree, TEXT("LapCounterText"), PrimaryFontSize);
	CurrentLapTimeText = MakeText(Tree, TEXT("CurrentLapTimeText"), PrimaryFontSize);
	LastLapTimeText = MakeText(Tree, TEXT("LastLapTimeText"), SecondaryFontSize);
	BestLapTimeText = MakeText(Tree, TEXT("BestLapTimeText"), SecondaryFontSize);
	DeltaText = MakeText(Tree, TEXT("DeltaText"), SecondaryFontSize);
	PositionText = MakeText(Tree, TEXT("PositionText"), PrimaryFontSize);

	UTextBlock* InvalidText = MakeText(Tree, TEXT("InvalidLapMarker"), SecondaryFontSize);
	InvalidText->SetText(FText::AsCultureInvariant(FString(TEXT("LAP INVALID"))));
	InvalidText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.25f, 0.2f)));
	InvalidLapMarker = InvalidText;

	for (UWidget* Child : { static_cast<UWidget*>(PositionText), static_cast<UWidget*>(LapCounterText),
		static_cast<UWidget*>(CurrentLapTimeText), static_cast<UWidget*>(InvalidLapMarker),
		static_cast<UWidget*>(LastLapTimeText), static_cast<UWidget*>(BestLapTimeText),
		static_cast<UWidget*>(DeltaText) })
	{
		AddToColumn(*Timing, Child, HAlign_Left);
	}

	// Vehicle, bottom right.
	UVerticalBox* Vehicle = AddColumn(Tree, *Root, TEXT("VehicleColumn"), HAlign_Right, VAlign_Bottom);
	GearText = MakeText(Tree, TEXT("GearText"), PrimaryFontSize, ETextJustify::Right);
	SpeedText = MakeText(Tree, TEXT("SpeedText"), SpeedFontSize, ETextJustify::Right);
	SpeedUnitText = MakeText(Tree, TEXT("SpeedUnitText"), LabelFontSize, ETextJustify::Right);
	RpmText = MakeText(Tree, TEXT("RpmText"), SecondaryFontSize, ETextJustify::Right);
	for (UWidget* Child : { static_cast<UWidget*>(GearText), static_cast<UWidget*>(SpeedText),
		static_cast<UWidget*>(SpeedUnitText), static_cast<UWidget*>(RpmText) })
	{
		AddToColumn(*Vehicle, Child, HAlign_Right);
	}

	// Countdown, centre.
	CountdownText = MakeText(Tree, TEXT("CountdownText"), CountdownFontSize, ETextJustify::Center);
	UOverlaySlot* CountdownSlot = Root->AddChildToOverlay(CountdownText);
	CountdownSlot->SetHorizontalAlignment(HAlign_Center);
	CountdownSlot->SetVerticalAlignment(VAlign_Center);

	// Results, centre.
	UVerticalBox* Results = AddColumn(Tree, *Root, TEXT("ResultsPanel"), HAlign_Center, VAlign_Center);
	ResultsPanel = Results;
	ResultTimeText = MakeText(Tree, TEXT("ResultTimeText"), PrimaryFontSize, ETextJustify::Center);
	ResultValidityText = MakeText(Tree, TEXT("ResultValidityText"), SecondaryFontSize, ETextJustify::Center);
	ResultBestLapText = MakeText(Tree, TEXT("ResultBestLapText"), SecondaryFontSize, ETextJustify::Center);
	ResultLapsText = MakeText(Tree, TEXT("ResultLapsText"), SecondaryFontSize, ETextJustify::Center);
	for (UWidget* Child : { static_cast<UWidget*>(ResultTimeText), static_cast<UWidget*>(ResultValidityText),
		static_cast<UWidget*>(ResultBestLapText), static_cast<UWidget*>(ResultLapsText) })
	{
		AddToColumn(*Results, Child, HAlign_Center);
	}

	// Nothing conditional shows until a view model says so.
	CountdownText->SetVisibility(ESlateVisibility::Collapsed);
	PositionText->SetVisibility(ESlateVisibility::Collapsed);
	DeltaText->SetVisibility(ESlateVisibility::Collapsed);
	BestLapTimeText->SetVisibility(ESlateVisibility::Collapsed);
	InvalidLapMarker->SetVisibility(ESlateVisibility::Collapsed);
	ResultsPanel->SetVisibility(ESlateVisibility::Collapsed);
}

void URacingHudWidget::InvalidateDisplayCache()
{
	for (int64& Key : TextKeys)
	{
		Key = UnwrittenKey;
	}
	for (uint8& Key : VisibilityKeys)
	{
		Key = UnwrittenVisibility;
	}
}

template <typename FormatFunc>
void URacingHudWidget::UpdateText(UTextBlock* Block, const ETextField Field, const int64 Key, FormatFunc&& Format)
{
	if (Block == nullptr)
	{
		return;
	}

	int64& Written = TextKeys[static_cast<int32>(Field)];
	if (Written == Key)
	{
		return;
	}

	Written = Key;
	Block->SetText(Format());
	++TextUpdateCount;
}

void URacingHudWidget::UpdateVisibility(UWidget* Target, const EVisibilityField Field, const bool bVisible)
{
	if (Target == nullptr)
	{
		return;
	}

	uint8& Written = VisibilityKeys[static_cast<int32>(Field)];
	const uint8 Key = bVisible ? 1 : 0;
	if (Written == Key)
	{
		return;
	}

	Written = Key;
	Target->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	++VisibilityUpdateCount;
}

void URacingHudWidget::ApplyViewModel(const FRacingHudViewModel& VM)
{
	using namespace RacingHudWidgetPrivate;
	using Format = URacingHudFormatLibrary;

	// -- Vehicle ----------------------------------------------------------------
	// Stale keys (-1) cannot collide with a real reading, which is always >= 0; a fresh
	// non-finite reading also rounds to -1 and formats as the same "--".
	const bool bFresh = VM.bVehicleDataFresh;

	UpdateText(SpeedText, ETextField::Speed,
		bFresh ? Format::RoundSpeedForDisplay(VM.Speed) : -1,
		[&VM]() { return Format::FormatSpeed(VM.Speed, VM.bVehicleDataFresh); });

	UpdateText(SpeedUnitText, ETextField::SpeedUnit, static_cast<int64>(VM.SpeedUnit),
		[&VM]() { return Format::FormatSpeedUnit(VM.SpeedUnit); });

	UpdateText(RpmText, ETextField::RPM,
		bFresh ? Format::RoundRPMForDisplay(VM.EngineRPM) : -1,
		[&VM]() { return Format::FormatRPM(VM.EngineRPM, VM.bVehicleDataFresh); });

	// Every reverse gear reads "R", so they share a key. Stale sits below all of them.
	const int64 GearKey = !bFresh ? -2 : (VM.GearIndex < 0 ? -1 : static_cast<int64>(VM.GearIndex));
	UpdateText(GearText, ETextField::Gear, GearKey,
		[&VM]() { return Format::FormatGear(VM.GearIndex, VM.bVehicleDataFresh); });

	// -- Timing -----------------------------------------------------------------
	UpdateText(LapCounterText, ETextField::LapCounter, FMath::Max(0, VM.CurrentLapNumber),
		[&VM]() { return Format::FormatLapCounter(VM.CurrentLapNumber); });

	UpdateText(CurrentLapTimeText, ETextField::CurrentLapTime,
		LapTimeKey(VM.bLapInProgress, VM.CurrentLapElapsedSeconds),
		[&VM]() { return Format::FormatLapTime(VM.CurrentLapElapsedSeconds, VM.bLapInProgress); });

	UpdateText(LastLapTimeText, ETextField::LastLapTime,
		LapTimeKey(VM.bHasLastLap, VM.LastLapSeconds),
		[&VM]() { return Format::FormatLapTime(VM.LastLapSeconds, VM.bHasLastLap); });

	UpdateText(BestLapTimeText, ETextField::BestLapTime,
		LapTimeKey(VM.bHasBestLap, VM.BestLapSeconds),
		[&VM]() { return Format::FormatLapTime(VM.BestLapSeconds, VM.bHasBestLap); });
	UpdateVisibility(BestLapTimeText, EVisibilityField::BestLap, VM.bHasBestLap);

	// Absent (and non-finite, which formats the same) sits above every real delta, which is bounded.
	const bool bDeltaShown = VM.bHasDelta && FMath::IsFinite(VM.DeltaToBestSeconds);
	UpdateText(DeltaText, ETextField::Delta,
		bDeltaShown ? Format::RoundDeltaMilliseconds(VM.DeltaToBestSeconds) : MAX_int64,
		[&VM]() { return Format::FormatDelta(VM.DeltaToBestSeconds, VM.bHasDelta); });
	UpdateVisibility(DeltaText, EVisibilityField::Delta, bDeltaShown);

	UpdateVisibility(InvalidLapMarker, EVisibilityField::InvalidLap, VM.bCurrentLapInvalid);

	// -- Race -------------------------------------------------------------------
	const bool bPositionShown = VM.bShowPosition && VM.RacePosition > 0 && VM.CompetitorCount > 0;
	UpdateText(PositionText, ETextField::Position,
		bPositionShown ? (static_cast<int64>(VM.RacePosition) << 32) | static_cast<int64>(VM.CompetitorCount) : -1,
		[&VM]() { return Format::FormatPosition(VM.RacePosition, VM.CompetitorCount, VM.bShowPosition); });
	UpdateVisibility(PositionText, EVisibilityField::Position, bPositionShown);

	UpdateText(CountdownText, ETextField::Countdown,
		VM.bShowCountdown
			? FMath::Clamp(VM.CountdownWholeSeconds, 0, URacingHudFormatLibrary::MaxDisplayCountdownSeconds)
			: -1,
		[&VM]() { return Format::FormatCountdown(VM.CountdownWholeSeconds, VM.bShowCountdown); });
	UpdateVisibility(CountdownText, EVisibilityField::Countdown, VM.bShowCountdown);

	// -- Results ----------------------------------------------------------------
	// Kept current even while hidden, so the panel is right on the frame it appears.
	UpdateText(ResultTimeText, ETextField::ResultTime,
		LapTimeKey(VM.bResultAvailable, VM.ResultFinalTimeSeconds),
		[&VM]() { return Format::FormatLapTime(VM.ResultFinalTimeSeconds, VM.bResultAvailable); });

	UpdateText(ResultValidityText, ETextField::ResultValidity, static_cast<int64>(VM.ResultValidity),
		[&VM]() { return Format::FormatRunValidity(VM.ResultValidity); });

	UpdateText(ResultBestLapText, ETextField::ResultBestLap,
		LapTimeKey(VM.bResultHasBestLap, VM.ResultBestLapSeconds),
		[&VM]() { return Format::FormatLapTime(VM.ResultBestLapSeconds, VM.bResultHasBestLap); });

	UpdateText(ResultLapsText, ETextField::ResultLaps,
		(static_cast<int64>(FMath::Max(0, VM.ResultLapsCompleted)) << 32)
			| static_cast<int64>(FMath::Max(0, VM.ResultValidLapsCompleted)),
		[&VM]() { return Format::FormatResultLaps(VM.ResultLapsCompleted, VM.ResultValidLapsCompleted); });

	UpdateVisibility(ResultsPanel, EVisibilityField::Results, VM.bShowResults);
}
