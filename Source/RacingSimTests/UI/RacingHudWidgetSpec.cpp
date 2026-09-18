// Copyright RacingSim. All Rights Reserved.

#include "UI/HudSpecRig.h"
#include "UI/RacingHudFormat.h"
#include "UI/RacingHudWidget.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

/**
 * UI-002: URacingHudWidget, the native in-race HUD.
 *
 * Tree        -- CreateWidget against a transient test world (never added to a viewport)
 *                builds the documented default tree, every text is readable at the
 *                stream floor, and a tree that already has a root (the Widget Blueprint
 *                path) is left alone.
 * Apply       -- every text is the format library's output for the same field, every
 *                conditional widget follows its flag both ways, and ApplyViewModel does
 *                not churn: same view model 0 updates, a sub-quantum speed change 0, a
 *                one-field change exactly 1.
 * RaceIntegration -- UI-001's procedural race stack (UI/HudSpecRig.h) through PreRace,
 *                Countdown, Racing, a lap close, Finished, Results and Restart, running
 *                gather -> BuildHudViewModel -> ApplyViewModel at each stage and comparing
 *                the widget's texts with the format functions applied to the race objects'
 *                own getters.
 *
 * ProductFilter, not Smoke: a UWorld carries a world-settings actor, and the smoke window
 * cannot construct a non-template actor (Docs/Environment.md). Game thread only.
 */

namespace HudWidgetSpecPrivate
{
	using Format = URacingHudFormatLibrary;

	/** Owns a transient game world and one HUD widget created against it. */
	struct FHudWidgetSpecWorld
	{
		FTestWorldWrapper WorldWrapper;
		// Declared after the wrapper so it is released first.
		TStrongObjectPtr<URacingHudWidget> Widget;

		bool Create(FAutomationTestBase& Test)
		{
			if (!WorldWrapper.CreateTestWorld(EWorldType::Game) || WorldWrapper.GetTestWorld() == nullptr)
			{
				Test.AddError(TEXT("FTestWorldWrapper::CreateTestWorld(EWorldType::Game) failed."));
				return false;
			}

			Widget.Reset(CreateWidget<URacingHudWidget>(WorldWrapper.GetTestWorld()));
			return Test.TestNotNull(TEXT("CreateWidget<URacingHudWidget> returns a widget"), Widget.Get());
		}

		~FHudWidgetSpecWorld()
		{
			Widget.Reset();
			if (WorldWrapper.GetTestWorld() != nullptr)
			{
				WorldWrapper.DestroyTestWorld(/*bForceGarbageCollect*/ false);
			}
		}
	};

	FString TextOf(const UTextBlock* Block)
	{
		return Block != nullptr ? Block->GetText().ToString() : FString(TEXT("<null>"));
	}

	bool IsShown(const UWidget* Target)
	{
		if (Target == nullptr)
		{
			return false;
		}
		const ESlateVisibility Visibility = Target->GetVisibility();
		return Visibility != ESlateVisibility::Collapsed && Visibility != ESlateVisibility::Hidden;
	}

	/** A view model with every field set to something displayable and every flag on. */
	FRacingHudViewModel HudWidgetFullViewModel()
	{
		FRacingHudViewModel VM;
		VM.bHasRaceState = true;
		VM.RaceState = ERaceState::Racing;
		VM.bVehicleDataFresh = true;
		VM.Speed = 187.4;
		VM.SpeedUnit = ERacingSpeedDisplayUnit::KilometresPerHour;
		VM.EngineRPM = 7123.4f;
		VM.GearIndex = 5;
		VM.CurrentLapNumber = 3;
		VM.LapsCompleted = 2;
		VM.bLapInProgress = true;
		VM.CurrentLapElapsedSeconds = 41.2345;
		VM.bCurrentLapInvalid = true;
		VM.bHasLastLap = true;
		VM.LastLapSeconds = 83.456;
		VM.bHasBestLap = true;
		VM.BestLapSeconds = 82.001;
		VM.bHasDelta = true;
		VM.DeltaToBestSeconds = -0.321;
		VM.bShowPosition = true;
		VM.RacePosition = 2;
		VM.CompetitorCount = 8;
		VM.bShowCountdown = true;
		VM.CountdownWholeSeconds = 3;
		VM.bShowResults = true;
		VM.bResultAvailable = true;
		VM.ResultFinalTimeSeconds = 250.75;
		VM.ResultValidity = ERacingRunValidity::Valid;
		VM.ResultLapsCompleted = 3;
		VM.ResultValidLapsCompleted = 2;
		VM.bResultHasBestLap = true;
		// Distinct from BestLapSeconds, so a result field wired to the live best lap fails.
		VM.ResultBestLapSeconds = 81.777;
		return VM;
	}

	/** Every text on the widget equals the format library's output for the same VM field. */
	void HudWidgetExpectTexts(FAutomationTestBase& Test, const FString& Stage, const URacingHudWidget& W, const FRacingHudViewModel& VM)
	{
		auto Expect = [&Test, &Stage](const TCHAR* What, const UTextBlock* Block, const FText& Expected)
		{
			Test.TestEqual(FString::Printf(TEXT("%s: %s is the format library's"), *Stage, What), TextOf(Block), Expected.ToString());
		};

		Expect(TEXT("speed"), W.SpeedText.Get(), Format::FormatSpeed(VM.Speed, VM.bVehicleDataFresh));
		Expect(TEXT("speed unit"), W.SpeedUnitText.Get(), Format::FormatSpeedUnit(VM.SpeedUnit));
		Expect(TEXT("RPM"), W.RpmText.Get(), Format::FormatRPM(VM.EngineRPM, VM.bVehicleDataFresh));
		Expect(TEXT("gear"), W.GearText.Get(), Format::FormatGear(VM.GearIndex, VM.bVehicleDataFresh));
		Expect(TEXT("lap counter"), W.LapCounterText.Get(), Format::FormatLapCounter(VM.CurrentLapNumber));
		Expect(TEXT("current lap"), W.CurrentLapTimeText.Get(), Format::FormatLapTime(VM.CurrentLapElapsedSeconds, VM.bLapInProgress));
		Expect(TEXT("last lap"), W.LastLapTimeText.Get(), Format::FormatLapTime(VM.LastLapSeconds, VM.bHasLastLap));
		Expect(TEXT("best lap"), W.BestLapTimeText.Get(), Format::FormatLapTime(VM.BestLapSeconds, VM.bHasBestLap));
		Expect(TEXT("delta"), W.DeltaText.Get(), Format::FormatDelta(VM.DeltaToBestSeconds, VM.bHasDelta));
		Expect(TEXT("position"), W.PositionText.Get(), Format::FormatPosition(VM.RacePosition, VM.CompetitorCount, VM.bShowPosition));
		Expect(TEXT("countdown"), W.CountdownText.Get(), Format::FormatCountdown(VM.CountdownWholeSeconds, VM.bShowCountdown));
		Expect(TEXT("result time"), W.ResultTimeText.Get(), Format::FormatLapTime(VM.ResultFinalTimeSeconds, VM.bResultAvailable));
		Expect(TEXT("result validity"), W.ResultValidityText.Get(), Format::FormatRunValidity(VM.ResultValidity));
		Expect(TEXT("result best lap"), W.ResultBestLapText.Get(), Format::FormatLapTime(VM.ResultBestLapSeconds, VM.bResultHasBestLap));
		Expect(TEXT("result laps"), W.ResultLapsText.Get(), Format::FormatResultLaps(VM.ResultLapsCompleted, VM.ResultValidLapsCompleted));
	}
}

// ============================================================================
// Tree
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingHudWidgetTreeTest,
	"RacingSim.UI.HudWidget.Tree",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FRacingHudWidgetTreeTest::RunTest(const FString& Parameters)
{
	using namespace HudWidgetSpecPrivate;

	FHudWidgetSpecWorld Spec;
	if (!Spec.Create(*this))
	{
		return false;
	}
	URacingHudWidget& W = *Spec.Widget;

	if (!TestNotNull(TEXT("The default tree exists"), W.WidgetTree.Get())
		|| !TestNotNull(TEXT("...and has a root"), W.WidgetTree->RootWidget.Get()))
	{
		return false;
	}
	TestEqual(TEXT("The root is HudRoot"), W.WidgetTree->RootWidget->GetFName(), FName(TEXT("HudRoot")));
	TestTrue(TEXT("...an overlay"), W.WidgetTree->RootWidget->IsA<UOverlay>());

	// Every bound widget exists and IS the tree's widget of the documented name.
	const TPair<const TCHAR*, const UWidget*> Bound[] = {
		{ TEXT("SpeedText"), W.SpeedText.Get() },
		{ TEXT("SpeedUnitText"), W.SpeedUnitText.Get() },
		{ TEXT("RpmText"), W.RpmText.Get() },
		{ TEXT("GearText"), W.GearText.Get() },
		{ TEXT("LapCounterText"), W.LapCounterText.Get() },
		{ TEXT("CurrentLapTimeText"), W.CurrentLapTimeText.Get() },
		{ TEXT("LastLapTimeText"), W.LastLapTimeText.Get() },
		{ TEXT("BestLapTimeText"), W.BestLapTimeText.Get() },
		{ TEXT("DeltaText"), W.DeltaText.Get() },
		{ TEXT("InvalidLapMarker"), W.InvalidLapMarker.Get() },
		{ TEXT("PositionText"), W.PositionText.Get() },
		{ TEXT("CountdownText"), W.CountdownText.Get() },
		{ TEXT("ResultsPanel"), W.ResultsPanel.Get() },
		{ TEXT("ResultTimeText"), W.ResultTimeText.Get() },
		{ TEXT("ResultValidityText"), W.ResultValidityText.Get() },
		{ TEXT("ResultBestLapText"), W.ResultBestLapText.Get() },
		{ TEXT("ResultLapsText"), W.ResultLapsText.Get() },
	};
	for (const TPair<const TCHAR*, const UWidget*>& Entry : Bound)
	{
		TestNotNull(FString::Printf(TEXT("%s is bound"), Entry.Key), Entry.Value);
		TestTrue(FString::Printf(TEXT("%s is the tree's widget of that name"), Entry.Key),
			Entry.Value != nullptr && W.WidgetTree->FindWidget(FName(Entry.Key)) == Entry.Value);
	}
	TestNotNull(TEXT("TimingColumn is in the tree"), W.WidgetTree->FindWidget(FName(TEXT("TimingColumn"))));
	TestNotNull(TEXT("VehicleColumn is in the tree"), W.WidgetTree->FindWidget(FName(TEXT("VehicleColumn"))));

	// Result texts live inside the results panel, so hiding it hides them.
	for (const UTextBlock* Child : { W.ResultTimeText.Get(), W.ResultValidityText.Get(), W.ResultBestLapText.Get(), W.ResultLapsText.Get() })
	{
		TestTrue(TEXT("A result text is a child of ResultsPanel"),
			Child != nullptr && W.ResultsPanel != nullptr && Child->GetParent() == W.ResultsPanel.Get());
	}

	// Readability floor: every text block in the tree, not just the bound ones.
	TArray<UWidget*> All;
	W.WidgetTree->GetAllWidgets(All);
	int32 TextBlocks = 0;
	for (const UWidget* Child : All)
	{
		if (const UTextBlock* Text = Cast<UTextBlock>(Child))
		{
			++TextBlocks;
			const int32 Size = static_cast<int32>(Text->GetFont().Size);
			TestTrue(FString::Printf(TEXT("%s font size %d >= %d"), *Text->GetName(), Size, URacingHudWidget::MinReadableFontSize),
				Size >= URacingHudWidget::MinReadableFontSize);
		}
	}
	TestEqual(TEXT("The default tree has 16 text blocks"), TextBlocks, 16);

	// Nothing conditional shows before the first view model.
	TestFalse(TEXT("Initially: countdown hidden"), IsShown(W.CountdownText));
	TestFalse(TEXT("Initially: position hidden"), IsShown(W.PositionText));
	TestFalse(TEXT("Initially: delta hidden"), IsShown(W.DeltaText));
	TestFalse(TEXT("Initially: best lap hidden"), IsShown(W.BestLapTimeText));
	TestFalse(TEXT("Initially: invalid-lap marker hidden"), IsShown(W.InvalidLapMarker));
	TestFalse(TEXT("Initially: results hidden"), IsShown(W.ResultsPanel));
	TestTrue(TEXT("Initially: speed shown"), IsShown(W.SpeedText));
	TestTrue(TEXT("Initially: the HUD takes no input"),
		W.WidgetTree->RootWidget->GetVisibility() == ESlateVisibility::HitTestInvisible);

	// -- A tree that already has a root is not overwritten ---------------------
	{
		UWorld* World = Spec.WorldWrapper.GetTestWorld();
		TStrongObjectPtr<URacingHudWidget> Seeded(NewObject<URacingHudWidget>(World));
		Seeded->WidgetTree = NewObject<UWidgetTree>(Seeded.Get(), TEXT("WidgetTree"), RF_Transient);
		UOverlay* AuthoredRoot = Seeded->WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("AuthoredRoot"));
		Seeded->WidgetTree->RootWidget = AuthoredRoot;

		TestTrue(TEXT("Seeded: Initialize succeeds"), Seeded->Initialize());
		TestTrue(TEXT("Seeded: the authored root is kept"), Seeded->WidgetTree->RootWidget.Get() == AuthoredRoot);
		TestNull(TEXT("Seeded: no default HudRoot was built"), Seeded->WidgetTree->FindWidget(FName(TEXT("HudRoot"))));
		TestNull(TEXT("Seeded: SpeedText unbound (the authored tree has none)"), Seeded->SpeedText.Get());
		TestNull(TEXT("Seeded: ResultsPanel unbound"), Seeded->ResultsPanel.Get());

		// Optional bindings: a layout that omits every widget is still safe to drive.
		Seeded->ApplyViewModel(HudWidgetFullViewModel());
		TestEqual(TEXT("Seeded: Apply with nothing bound makes no text update"), Seeded->GetTextUpdateCountForTest(), 0);
		Seeded.Reset();
	}

	return true;
}

// ============================================================================
// Apply
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingHudWidgetApplyTest,
	"RacingSim.UI.HudWidget.Apply",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FRacingHudWidgetApplyTest::RunTest(const FString& Parameters)
{
	using namespace HudWidgetSpecPrivate;

	FHudWidgetSpecWorld Spec;
	if (!Spec.Create(*this))
	{
		return false;
	}
	URacingHudWidget& W = *Spec.Widget;

	// -- Every flag on ---------------------------------------------------------
	FRacingHudViewModel VM = HudWidgetFullViewModel();
	W.ApplyViewModel(VM);
	HudWidgetExpectTexts(*this, TEXT("All on"), W, VM);
	TestTrue(TEXT("All on: countdown shown"), IsShown(W.CountdownText));
	TestTrue(TEXT("All on: position shown"), IsShown(W.PositionText));
	TestTrue(TEXT("All on: delta shown"), IsShown(W.DeltaText));
	TestTrue(TEXT("All on: best lap shown"), IsShown(W.BestLapTimeText));
	TestTrue(TEXT("All on: invalid-lap marker shown"), IsShown(W.InvalidLapMarker));
	TestTrue(TEXT("All on: results shown"), IsShown(W.ResultsPanel));
	TestTrue(TEXT("All on: shown widgets still take no input"),
		W.CountdownText->GetVisibility() == ESlateVisibility::HitTestInvisible);
	TestEqual(TEXT("All on: the first apply writes all 15 texts"), W.GetTextUpdateCountForTest(), 15);
	TestEqual(TEXT("All on: the first apply writes all 6 visibilities"), W.GetVisibilityUpdateCountForTest(), 6);

	// -- Each flag off, one at a time: only its own widget hides ----------------
	struct FFlagCase
	{
		const TCHAR* Name;
		TFunction<void(FRacingHudViewModel&)> TurnOff;
		TFunction<const UWidget*(const URacingHudWidget&)> Target;
	};
	const FFlagCase Cases[] = {
		{ TEXT("bShowCountdown"), [](FRacingHudViewModel& V) { V.bShowCountdown = false; }, [](const URacingHudWidget& X) -> const UWidget* { return X.CountdownText.Get(); } },
		{ TEXT("bShowPosition"), [](FRacingHudViewModel& V) { V.bShowPosition = false; }, [](const URacingHudWidget& X) -> const UWidget* { return X.PositionText.Get(); } },
		{ TEXT("bHasDelta"), [](FRacingHudViewModel& V) { V.bHasDelta = false; }, [](const URacingHudWidget& X) -> const UWidget* { return X.DeltaText.Get(); } },
		{ TEXT("bHasBestLap"), [](FRacingHudViewModel& V) { V.bHasBestLap = false; }, [](const URacingHudWidget& X) -> const UWidget* { return X.BestLapTimeText.Get(); } },
		{ TEXT("bCurrentLapInvalid"), [](FRacingHudViewModel& V) { V.bCurrentLapInvalid = false; }, [](const URacingHudWidget& X) -> const UWidget* { return X.InvalidLapMarker.Get(); } },
		{ TEXT("bShowResults"), [](FRacingHudViewModel& V) { V.bShowResults = false; }, [](const URacingHudWidget& X) -> const UWidget* { return X.ResultsPanel.Get(); } },
	};
	for (const FFlagCase& Case : Cases)
	{
		FRacingHudViewModel Off = HudWidgetFullViewModel();
		Case.TurnOff(Off);
		const int32 VisibilityBefore = W.GetVisibilityUpdateCountForTest();
		W.ApplyViewModel(Off);
		TestEqual(FString::Printf(TEXT("%s false: exactly 1 visibility update"), Case.Name),
			W.GetVisibilityUpdateCountForTest() - VisibilityBefore, 1);
		TestFalse(FString::Printf(TEXT("%s false: its widget is hidden"), Case.Name), IsShown(Case.Target(W)));
		HudWidgetExpectTexts(*this, FString::Printf(TEXT("%s false"), Case.Name), W, Off);
		for (const FFlagCase& Other : Cases)
		{
			if (&Other != &Case)
			{
				TestTrue(FString::Printf(TEXT("%s false: %s's widget still shown"), Case.Name, Other.Name), IsShown(Other.Target(W)));
			}
		}

		W.ApplyViewModel(HudWidgetFullViewModel());
		TestTrue(FString::Printf(TEXT("%s true again: its widget is shown"), Case.Name), IsShown(Case.Target(W)));
	}

	// -- Stale vehicle data ----------------------------------------------------
	{
		FRacingHudViewModel Stale = HudWidgetFullViewModel();
		Stale.bVehicleDataFresh = false;
		W.ApplyViewModel(Stale);
		HudWidgetExpectTexts(*this, TEXT("Stale"), W, Stale);
		TestNotEqual(TEXT("Stale: the speed text is not the live reading"),
			TextOf(W.SpeedText), Format::FormatSpeed(Stale.Speed, true).ToString());
	}

	// -- No per-frame churn ----------------------------------------------------
	VM = HudWidgetFullViewModel();
	W.ApplyViewModel(VM);
	int32 Before = W.GetTextUpdateCountForTest();
	int32 VisibilityBefore = W.GetVisibilityUpdateCountForTest();
	W.ApplyViewModel(VM);
	TestEqual(TEXT("Churn: the same view model again makes 0 text updates"), W.GetTextUpdateCountForTest() - Before, 0);
	TestEqual(TEXT("Churn: ...and 0 visibility updates"), W.GetVisibilityUpdateCountForTest() - VisibilityBefore, 0);

	// 187.4 and 187.2 both read 187.
	Before = W.GetTextUpdateCountForTest();
	FRacingHudViewModel SubQuantum = VM;
	SubQuantum.Speed = 187.2;
	TestEqual(TEXT("Churn: precondition -- both speeds round to the same whole unit"),
		Format::RoundSpeedForDisplay(SubQuantum.Speed), Format::RoundSpeedForDisplay(VM.Speed));
	W.ApplyViewModel(SubQuantum);
	TestEqual(TEXT("Churn: a speed change inside one whole unit makes 0 text updates"), W.GetTextUpdateCountForTest() - Before, 0);

	Before = W.GetTextUpdateCountForTest();
	FRacingHudViewModel OneField = SubQuantum;
	OneField.GearIndex = 6;
	W.ApplyViewModel(OneField);
	TestEqual(TEXT("Churn: a one-field change makes exactly 1 text update"), W.GetTextUpdateCountForTest() - Before, 1);
	TestEqual(TEXT("Churn: ...and it is the gear"), TextOf(W.GearText), Format::FormatGear(6, true).ToString());

	// A flag-only change moves visibility, not text.
	Before = W.GetTextUpdateCountForTest();
	VisibilityBefore = W.GetVisibilityUpdateCountForTest();
	FRacingHudViewModel FlagOnly = OneField;
	FlagOnly.bCurrentLapInvalid = false;
	W.ApplyViewModel(FlagOnly);
	TestEqual(TEXT("Churn: toggling the invalid-lap flag makes 0 text updates"), W.GetTextUpdateCountForTest() - Before, 0);
	TestEqual(TEXT("Churn: ...and exactly 1 visibility update"), W.GetVisibilityUpdateCountForTest() - VisibilityBefore, 1);
	TestFalse(TEXT("Churn: ...but hides the marker"), IsShown(W.InvalidLapMarker));

	// InvalidateDisplayCache forces a full rewrite.
	W.InvalidateDisplayCache();
	Before = W.GetTextUpdateCountForTest();
	VisibilityBefore = W.GetVisibilityUpdateCountForTest();
	W.ApplyViewModel(FlagOnly);
	TestEqual(TEXT("Churn: after InvalidateDisplayCache all 15 texts are rewritten"), W.GetTextUpdateCountForTest() - Before, 15);
	TestEqual(TEXT("Churn: ...and all 6 visibilities"), W.GetVisibilityUpdateCountForTest() - VisibilityBefore, 6);

	// -- Reachability ----------------------------------------------------------
	for (const TCHAR* Name : { TEXT("ApplyViewModel"), TEXT("InvalidateDisplayCache") })
	{
		const UFunction* Function = URacingHudWidget::StaticClass()->FindFunctionByName(FName(Name));
		TestNotNull(FString::Printf(TEXT("%s is a UFUNCTION"), Name), Function);
		TestTrue(FString::Printf(TEXT("%s is BlueprintCallable"), Name),
			Function != nullptr && Function->HasAllFunctionFlags(FUNC_BlueprintCallable));
	}

	return true;
}

// ============================================================================
// RaceIntegration
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingHudWidgetRaceIntegrationTest,
	"RacingSim.UI.HudWidget.RaceIntegration",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FRacingHudWidgetRaceIntegrationTest::RunTest(const FString& Parameters)
{
	using namespace HudViewModelSpecPrivate;
	using namespace HudWidgetSpecPrivate;

	FHudWidgetSpecWorld Spec;
	if (!Spec.Create(*this))
	{
		return false;
	}
	URacingHudWidget& W = *Spec.Widget;

	FHudSpecRig Rig;
	if (!Rig.Build(*this))
	{
		return false;
	}

	// gather -> build -> apply, as RACE-005's director will each frame.
	auto Frame = [this, &Rig, &W](const TCHAR* Stage)
	{
		FRacingHudRaceInputs Inputs;
		TestTrue(FString::Printf(TEXT("%s: gather succeeds"), Stage), Rig.Gather(Inputs));
		const FRacingHudViewModel VM = HudSpecBuild(Inputs, GHudSpecNowSeconds);
		W.ApplyViewModel(VM);
		HudWidgetExpectTexts(*this, Stage, W, VM);
	};

	// The texts the widget must show, from the race objects' own getters -- not the VM.
	auto ExpectLapTexts = [this, &Rig, &W](const TCHAR* Stage)
	{
		const URaceLapTracker& T = *Rig.Tracker;
		TestEqual(FString::Printf(TEXT("%s: lap counter is the tracker's lap"), Stage),
			TextOf(W.LapCounterText), Format::FormatLapCounter(T.GetCurrentLapNumber()).ToString());
		const FRacingLapTiming& Last = T.PeekLastCompletedLap();
		TestEqual(FString::Printf(TEXT("%s: last lap is the tracker's"), Stage),
			TextOf(W.LastLapTimeText), Format::FormatLapTime(Last.LapDurationSeconds, T.GetLapsCompleted() > 0).ToString());
		const FRacingLapTiming& Best = T.PeekBestValidLap();
		TestEqual(FString::Printf(TEXT("%s: best lap is the tracker's best valid lap"), Stage),
			TextOf(W.BestLapTimeText), Format::FormatLapTime(Best.LapDurationSeconds, T.GetValidLapsCompleted() > 0).ToString());
		TestEqual(FString::Printf(TEXT("%s: best lap shown iff a valid lap exists"), Stage),
			IsShown(W.BestLapTimeText), T.GetValidLapsCompleted() > 0);
	};

	const FString AbsentLap = Format::FormatLapTime(0.0, false).ToString();

	// -- PreRace -----------------------------------------------------------------
	Frame(TEXT("PreRace"));
	ExpectLapTexts(TEXT("PreRace"));
	TestEqual(TEXT("PreRace: lap counter reads the pre-lap form"), TextOf(W.LapCounterText), Format::FormatLapCounter(0).ToString());
	TestFalse(TEXT("PreRace: countdown hidden"), IsShown(W.CountdownText));
	TestFalse(TEXT("PreRace: results hidden"), IsShown(W.ResultsPanel));

	// -- Countdown ---------------------------------------------------------------
	Rig.Seed(-800.0);
	Rig.Machine->BeginCountdown();
	GHudSpecNowSeconds += 1.2;
	TestFalse(TEXT("Countdown: 1.2 s in, the poll does not go green"), Rig.Machine->PollAutoTransitions());
	Frame(TEXT("Countdown"));
	const int32 CountdownWhole = static_cast<int32>(FMath::CeilToDouble(Rig.Machine->PeekCountdownRemainingSeconds()));
	TestEqual(TEXT("Countdown: precondition -- 1.8 s remain, reads 2"), CountdownWhole, 2);
	TestEqual(TEXT("Countdown: text is the state machine's countdown"),
		TextOf(W.CountdownText), Format::FormatCountdown(CountdownWhole, true).ToString());
	TestTrue(TEXT("Countdown: shown"), IsShown(W.CountdownText));
	TestFalse(TEXT("Countdown: results hidden"), IsShown(W.ResultsPanel));

	GHudSpecNowSeconds += HudSpecCountdownSeconds - 1.2;
	Rig.Machine->StartRace();

	// -- Racing, first lap open ---------------------------------------------------
	Rig.Drive(400.0, 8);
	Frame(TEXT("Racing"));
	ExpectLapTexts(TEXT("Racing"));
	TestEqual(TEXT("Racing: lap 1"), Rig.Tracker->GetCurrentLapNumber(), 1);
	TestEqual(TEXT("Racing: current lap is the tracker's running time"),
		TextOf(W.CurrentLapTimeText), Format::FormatLapTime(Rig.Tracker->GetCurrentLapElapsedSeconds(), true).ToString());
	TestFalse(TEXT("Racing: countdown hidden"), IsShown(W.CountdownText));
	TestFalse(TEXT("Racing: results hidden"), IsShown(W.ResultsPanel));
	TestEqual(TEXT("Racing: no last lap yet"), TextOf(W.LastLapTimeText), AbsentLap);

	// -- One lap closed --------------------------------------------------------------
	Rig.Drive(400.0 + Rig.LapLengthCm, HudSpecStepsPerLap);
	Frame(TEXT("Lap closed"));
	ExpectLapTexts(TEXT("Lap closed"));
	TestEqual(TEXT("Lap closed: precondition -- one lap completed"), Rig.Tracker->GetLapsCompleted(), 1);
	TestNotEqual(TEXT("Lap closed: last lap now has a time"), TextOf(W.LastLapTimeText), AbsentLap);
	TestTrue(TEXT("Lap closed: best lap shown"), IsShown(W.BestLapTimeText));
	TestFalse(TEXT("Lap closed: results hidden mid-race"), IsShown(W.ResultsPanel));

	// -- Finished ------------------------------------------------------------------
	Rig.Machine->FinishRace();
	Frame(TEXT("Finished"));
	if (TestTrue(TEXT("Finished: the recorder froze a result"), Rig.Recorder->HasFrozenResult()))
	{
		const FRacingRaceResult& Frozen = Rig.Recorder->GetFrozenResult();
		TestEqual(TEXT("Finished: result time is the frozen result's"),
			TextOf(W.ResultTimeText), Format::FormatLapTime(Frozen.FinalTimeSeconds, true).ToString());
		TestEqual(TEXT("Finished: result validity is the frozen result's"),
			TextOf(W.ResultValidityText), Format::FormatRunValidity(Frozen.GetValidity()).ToString());
		TestEqual(TEXT("Finished: result best lap is the frozen result's"),
			TextOf(W.ResultBestLapText), Format::FormatLapTime(Frozen.BestLap.LapDurationSeconds, Frozen.HasValidLap()).ToString());
		TestEqual(TEXT("Finished: result laps are the frozen result's"),
			TextOf(W.ResultLapsText), Format::FormatResultLaps(Frozen.LapsCompleted, Frozen.ValidLapsCompleted).ToString());
	}
	TestTrue(TEXT("Finished: results shown"), IsShown(W.ResultsPanel));

	// -- Results ---------------------------------------------------------------------
	Rig.Machine->ShowResults();
	Frame(TEXT("Results"));
	TestTrue(TEXT("Results: results still shown"), IsShown(W.ResultsPanel));

	// -- Restart ---------------------------------------------------------------------
	Rig.Machine->Restart();
	Frame(TEXT("Restart"));
	ExpectLapTexts(TEXT("Restart"));
	TestFalse(TEXT("Restart: results hidden"), IsShown(W.ResultsPanel));
	TestEqual(TEXT("Restart: last lap cleared to the absent text"), TextOf(W.LastLapTimeText), AbsentLap);
	TestEqual(TEXT("Restart: best lap cleared to the absent text"), TextOf(W.BestLapTimeText), AbsentLap);
	TestFalse(TEXT("Restart: best lap hidden"), IsShown(W.BestLapTimeText));
	TestEqual(TEXT("Restart: lap counter back to the pre-lap form"), TextOf(W.LapCounterText), Format::FormatLapCounter(0).ToString());

	return true;
}
