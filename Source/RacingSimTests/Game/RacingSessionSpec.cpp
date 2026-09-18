// Copyright RacingSim. All Rights Reserved.

#include "Game/RacingSessionTestTypes.h"

#include "Game/RacingGameMode.h"
#include "Game/RacingGrayboxGround.h"
#include "Game/RacingPlayerController.h"
#include "Race/RaceDirector.h"
#include "Race/RaceResult.h"
#include "Race/RaceStateMachine.h"
#include "Race/TrackCenterline.h"
#include "Race/TrackDefinitionActor.h"
#include "UI/RacingHudWidget.h"
#include "Vehicle/RacingVehiclePawn.h"

#include "Blueprint/UserWidget.h"
#include "Components/BoxComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameMapsSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Tests/AutomationCommon.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_EDITOR
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "WidgetBlueprint.h"
#endif

/**
 * RACE-005: the composed session -- game mode, director, pawn, controller and HUD -- in a
 * test world with a REAL ULocalPlayer.
 *
 * The world is built the way LoadMap builds one, in the order that matters: the track
 * exists before play begins (so the director's BeginPlay resolves it), the world-settings
 * game mode is pinned to ARacingSessionTestGameMode (zero-second countdown, explicit
 * transient car assets), and the player logs in through ULocalPlayer::SpawnPlayActor --
 * Login, ReceivedPlayer, PostLogin and RestartPlayer, the engine's own path.
 *
 * Two login orders are covered:
 *   - ESessionLogin::AfterBeginPlay (most tests): the pawn's BeginPlay has run by the time
 *     the game mode publishes its car spec -- the immediate branch of
 *     ARacingGameMode::OnCompetitorPlaced.
 *   - ESessionLogin::BeforeBeginPlay (RacingSim.Game.Session.LoadMapOrder): UEngine::LoadMap's
 *     order -- SetGameMode, InitializeActorsForPlay, login, then world BeginPlay -- so setup
 *     runs inside RestartPlayer, auto-start runs from the director's BeginPlay and the car
 *     spec is published from StartPlay.
 *
 * The test track undulates in Z, so the ground check can tell the lowest centerline point
 * from the line's average height.
 *
 * ProductFilter: these spawn real actors (Docs/Environment.md).
 */

namespace RacingSessionSpecPrivate
{
	constexpr double SessionSpecTrackRadiusCm = 10000.0;
	constexpr int32 SessionSpecSplinePoints = 12;
	constexpr double SessionSpecToleranceCm = 1.0;
	constexpr float SessionSpecTickSeconds = 1.0f / 60.0f;

	/** Centerline height swing, cm: Z = amplitude * sin(angle), so the line averages Z 0. */
	constexpr double SessionSpecTrackZAmplitudeCm = 400.0;

	enum class ESessionLogin : uint8
	{
		AfterBeginPlay,
		BeforeBeginPlay,
	};

	const TCHAR* const Graybox_MapPackagePath = TEXT("/Game/Tracks/Prototype/Maps/L_Meridian_Graybox");
	const TCHAR* const RacingGameModePath = TEXT("/Script/RacingSim.RacingGameMode");

	/** Owns the test world and the composed session in it. */
	struct FSessionWorld
	{
		FTestWorldWrapper WorldWrapper;
		UWorld* World = nullptr;
		ATrackDefinitionActor* Track = nullptr;
		ARacingSessionTestGameMode* GameMode = nullptr;
		ULocalPlayer* LocalPlayer = nullptr;
		ARacingPlayerController* Controller = nullptr;

		~FSessionWorld()
		{
			// DestroyTestWorld ends play and shuts the game instance down, which removes the
			// local player.
			if (WorldWrapper.GetTestWorld() != nullptr)
			{
				WorldWrapper.DestroyTestWorld(/*bForceGarbageCollect*/ false);
			}
		}

		/**
		 * BeforeBeginPlay stops after login with the world not yet playing; FinishBeginPlay()
		 * then runs world BeginPlay, and with it ARacingGameMode::StartPlay.
		 */
		bool Create(FAutomationTestBase& Test, const ESessionLogin Login = ESessionLogin::AfterBeginPlay)
		{
			if (GEngine == nullptr)
			{
				Test.AddError(TEXT("GEngine is null; no world can be built."));
				return false;
			}

			if (!WorldWrapper.CreateTestWorld(EWorldType::Game) || WorldWrapper.GetTestWorld() == nullptr)
			{
				WorldWrapper.ForwardErrorMessages(&Test);
				Test.AddError(TEXT("FTestWorldWrapper::CreateTestWorld(EWorldType::Game) failed."));
				return false;
			}
			World = WorldWrapper.GetTestWorld();

			// Before play: the director resolves its track in BeginPlay.
			if (!SpawnTrack(Test))
			{
				return false;
			}

			AWorldSettings* WorldSettings = World->GetWorldSettings();
			if (WorldSettings == nullptr)
			{
				Test.AddError(TEXT("The test world has no world settings."));
				return false;
			}
			WorldSettings->DefaultGameMode = ARacingSessionTestGameMode::StaticClass();

			if (Login == ESessionLogin::AfterBeginPlay)
			{
				if (!WorldWrapper.BeginPlayInTestWorld())
				{
					WorldWrapper.ForwardErrorMessages(&Test);
					Test.AddError(TEXT("FTestWorldWrapper::BeginPlayInTestWorld failed."));
					return false;
				}
			}
			else
			{
				// UEngine::LoadMap's order, and BeginPlayInTestWorld's first two steps: the game
				// mode spawns (and with it the director), then actors initialise. World
				// BeginPlay waits for FinishBeginPlay.
				const FURL URL;
				World->SetGameMode(URL);
				World->InitializeActorsForPlay(URL);
			}

			GameMode = Cast<ARacingSessionTestGameMode>(World->GetAuthGameMode());
			if (GameMode == nullptr)
			{
				Test.AddError(FString::Printf(TEXT("The world's game mode is %s, not ARacingSessionTestGameMode."),
					*GetNameSafe(World->GetAuthGameMode())));
				return false;
			}

			// No Enhanced Input assets exist yet (RACE-005 "Deliberately excluded"), so the
			// spawned car has no input config and says so, once each. Declared exactly: a
			// second car, or a silenced fault, fails the test.
			Test.AddExpectedMessagePlain(
				TEXT("has no UVehicleInputConfigDataAsset"),
				ELogVerbosity::Warning,
				EAutomationExpectedMessageFlags::Contains,
				1);
			Test.AddExpectedMessagePlain(
				TEXT("input initialisation failed with 'EVehicleInputInitResult::NoConfig'"),
				ELogVerbosity::Error,
				EAutomationExpectedMessageFlags::Contains,
				1);

			return LogInLocalPlayer(Test);
		}

		/** World BeginPlay for a BeforeBeginPlay session: every actor's BeginPlay, then StartPlay. */
		bool FinishBeginPlay(FAutomationTestBase& Test)
		{
			if (World == nullptr || World->HasBegunPlay())
			{
				Test.AddError(TEXT("FinishBeginPlay needs a world that has not begun play."));
				return false;
			}
			World->BeginPlay();
			return Test.TestTrue(TEXT("The world has begun play"), World->HasBegunPlay());
		}

	private:
		bool SpawnTrack(FAutomationTestBase& Test)
		{
			Track = World->SpawnActorDeferred<ATrackDefinitionActor>(ATrackDefinitionActor::StaticClass(), FTransform::Identity);
			if (Track == nullptr)
			{
				Test.AddError(TEXT("The track actor failed to spawn."));
				return false;
			}
			Track->SetFlags(RF_Transient);

			if (USplineComponent* Spline = Track->GetCenterlineSpline())
			{
				TArray<FVector> Points;
				for (int32 Index = 0; Index < SessionSpecSplinePoints; ++Index)
				{
					const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(Index) / static_cast<double>(SessionSpecSplinePoints);
					Points.Add(FVector(
						SessionSpecTrackRadiusCm * FMath::Cos(Angle),
						SessionSpecTrackRadiusCm * FMath::Sin(Angle),
						SessionSpecTrackZAmplitudeCm * FMath::Sin(Angle)));
				}
				Spline->SetClosedLoop(true, /*bUpdateSpline*/ false);
				Spline->SetSplinePoints(Points, ESplineCoordinateSpace::Local, /*bUpdateSpline*/ true);
			}
			Track->TrackId = FName(TEXT("Track.Test.Session"));
			// ATrackDefinitionActor::Validate requires sector 1 to start at the line.
			Track->SectorStartDistancesCm = { 0.0 };
			Track->FinishSpawning(FTransform::Identity);
			Track->RebuildTrackData();

			FString Reason;
			if (!Track->GetCachedValidation(Reason))
			{
				Test.AddError(FString::Printf(TEXT("The test track failed validation: %s"), *Reason));
				return false;
			}
			return true;
		}

		/** UGameInstance::CreateLocalPlayer's path, minus the viewport the test world lacks. */
		bool LogInLocalPlayer(FAutomationTestBase& Test)
		{
			UGameInstance* GameInstance = World->GetGameInstance();
			if (GameInstance == nullptr)
			{
				Test.AddError(TEXT("The test world has no game instance."));
				return false;
			}

			UClass* LocalPlayerClass = GEngine->LocalPlayerClass != nullptr ? GEngine->LocalPlayerClass.Get() : ULocalPlayer::StaticClass();
			LocalPlayer = NewObject<ULocalPlayer>(GEngine, LocalPlayerClass);
			if (GameInstance->AddLocalPlayer(LocalPlayer, FPlatformMisc::GetPlatformUserForUserIndex(0)) == INDEX_NONE)
			{
				Test.AddError(TEXT("UGameInstance::AddLocalPlayer refused the local player."));
				return false;
			}

			FString Error;
			if (!LocalPlayer->SpawnPlayActor(FString(), Error, World))
			{
				Test.AddError(FString::Printf(TEXT("ULocalPlayer::SpawnPlayActor failed: %s"), *Error));
				return false;
			}

			Controller = Cast<ARacingPlayerController>(LocalPlayer->PlayerController);
			if (Controller == nullptr)
			{
				Test.AddError(FString::Printf(TEXT("The local player's controller is %s, not ARacingPlayerController."),
					*GetNameSafe(LocalPlayer->PlayerController)));
				return false;
			}
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSessionCompositionTest,
	"RacingSim.Game.Session.Composition",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FRacingSessionCompositionTest::RunTest(const FString& Parameters)
{
	using namespace RacingSessionSpecPrivate;

	FSessionWorld Session;
	if (!Session.Create(*this))
	{
		return false;
	}
	UWorld* World = Session.World;
	ATrackDefinitionActor* Track = Session.Track;

	// -- Director ---------------------------------------------------------------
	int32 DirectorCount = 0;
	for (TActorIterator<ARaceDirector> It(World); It; ++It)
	{
		++DirectorCount;
	}
	TestEqual(TEXT("Exactly one race director exists"), DirectorCount, 1);

	ARaceDirector* Director = Session.GameMode->GetRaceDirector();
	if (!TestNotNull(TEXT("The game mode holds the director"), Director))
	{
		return false;
	}
	TestTrue(TEXT("The director's session is set up"), Director->IsSessionSetUp());
	TestTrue(TEXT("The director resolved the world's track"), Director->GetTrack() == Track);
	TestNotNull(TEXT("The director holds the state machine"), Director->GetStateMachine());
	TestNotNull(TEXT("The director holds the lap tracker"), Director->GetLapTracker());
	TestNotNull(TEXT("The director holds the result recorder"), Director->GetResultRecorder());
	TestTrue(TEXT("The director races the game mode's ruleset"), Director->GetActiveRuleset() == Session.GameMode->Ruleset);
	if (Director->GetStateMachine() == nullptr)
	{
		return false;
	}

	// -- Ground ---------------------------------------------------------------
	const FTrackCenterline& Centerline = Track->GetCenterline();
	double LowestCenterlineZ = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < Centerline.NumSamples(); ++Index)
	{
		const double DistanceCm = Centerline.GetLengthCm() * static_cast<double>(Index) / static_cast<double>(Centerline.NumSamples());
		LowestCenterlineZ = FMath::Min(LowestCenterlineZ, Centerline.GetLocationAtDistanceCm(DistanceCm).Z);
	}

	ARacingGrayboxGround* Ground = Session.GameMode->GetGrayboxGround();
	if (TestNotNull(TEXT("The game mode spawned the graybox ground"), Ground)
		&& TestNotNull(TEXT("The ground has its collision box"), Ground->GetCollisionBox()))
	{
		TestEqual(TEXT("The ground's collision profile is BlockAll"),
			Ground->GetCollisionBox()->GetCollisionProfileName(), UCollisionProfile::BlockAll_ProfileName);
		TestTrue(FString::Printf(TEXT("Precondition: the track undulates (lowest centerline Z %.1f cm)"), LowestCenterlineZ),
			LowestCenterlineZ < -0.9 * SessionSpecTrackZAmplitudeCm);
		TestEqual(TEXT("The ground's top face is at the lowest centerline Z"), Ground->GetTopZCm(), LowestCenterlineZ, SessionSpecToleranceCm);
	}

	// -- Pawn -----------------------------------------------------------------
	// Before any world tick: the car has not moved from where RestartPlayer put it.
	ARacingVehiclePawn* Pawn = Cast<ARacingVehiclePawn>(Session.Controller->GetPawn());
	if (!TestNotNull(TEXT("The controller possesses an ARacingVehiclePawn"), Pawn))
	{
		return false;
	}
	TestTrue(TEXT("The pawn's chassis is applied"), Pawn->IsChassisApplied());
	TestTrue(TEXT("The pawn's controller is the local player's"), Pawn->GetController() == Session.Controller);
	TestTrue(TEXT("The controller's Player is the local player"), Session.Controller->Player == Session.LocalPlayer);
	TestTrue(TEXT("The controller is local"), Session.Controller->IsLocalController());
	TestTrue(TEXT("The director follows the possessed pawn"), Director->GetCompetitor() == Pawn);

	// Placement hands the recorder the car and the input device.
	if (const URaceResultRecorder* Recorder = Director->GetResultRecorder())
	{
		TestTrue(TEXT("Precondition: the pawn names a real input device"), Pawn->InitialInputDeviceType != ERacingInputDeviceType::Unknown);
		TestEqual(TEXT("The recorder holds the pawn's input device"), Recorder->GetInputDeviceType(), Pawn->InitialInputDeviceType);
		TestTrue(TEXT("The pawn published its car spec to the recorder (immediate branch)"), Recorder->GetCarSpecVersion().IsPopulated());
	}

	double GridDistanceCm = ATrackDefinitionActor::InvalidDistanceCm;
	const FVector GridLocation = Track->GetGridSlotPose(Session.GameMode->GridSlotIndex, GridDistanceCm).GetLocation();
	const FVector PawnLocation = Pawn->GetActorLocation();
	TestTrue(TEXT("The grid slot exists"), GridDistanceCm >= 0.0);
	TestTrue(
		FString::Printf(TEXT("The pawn is within %.1f cm (XY) of the grid slot pose (off by %.3f cm)"),
			SessionSpecToleranceCm, FVector::Dist2D(PawnLocation, GridLocation)),
		FVector::Dist2D(PawnLocation, GridLocation) <= SessionSpecToleranceCm);
	TestEqual(TEXT("The pawn is raised GridSpawnHeightCm above the grid slot pose"),
		PawnLocation.Z - GridLocation.Z, Session.GameMode->GridSpawnHeightCm, SessionSpecToleranceCm);

	// -- HUD --------------------------------------------------------------------
	URacingHudWidget* Hud = Session.Controller->GetHudWidget();
	if (!TestNotNull(TEXT("The controller created its HUD widget"), Hud))
	{
		return false;
	}
	TestTrue(TEXT("The HUD is owned by the local player"), Hud->GetOwningLocalPlayer() == Session.LocalPlayer);

	// -- One frame ------------------------------------------------------------
	URaceStateMachine* StateMachine = Director->GetStateMachine();
	TestEqual(TEXT("Auto-start put the registered session in Countdown"), StateMachine->GetRaceState(), ERaceState::Countdown);

	const int32 TextUpdatesBefore = Hud->GetTextUpdateCountForTest();
	if (!TestTrue(TEXT("The world ticks"), Session.WorldWrapper.TickTestWorld(SessionSpecTickSeconds)))
	{
		return false;
	}
	TestEqual(TEXT("A zero-second countdown goes green on the first director tick"), StateMachine->GetRaceState(), ERaceState::Racing);
	TestTrue(
		FString::Printf(TEXT("The controller's tick applied the HUD (text updates %d -> %d)"), TextUpdatesBefore, Hud->GetTextUpdateCountForTest()),
		Hud->GetTextUpdateCountForTest() > TextUpdatesBefore);

	// The controller ticks before physics and the director after (see ARacingPlayerController's
	// class comment), so the frame above showed Countdown; an explicit update now shows Racing.
	Session.Controller->UpdateHud();
	TestEqual(TEXT("The HUD view model carries the director's race state"),
		Session.Controller->GetLastViewModelForTest().RaceState, ERaceState::Racing);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSessionLoadMapOrderTest,
	"RacingSim.Game.Session.LoadMapOrder",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FRacingSessionLoadMapOrderTest::RunTest(const FString& Parameters)
{
	using namespace RacingSessionSpecPrivate;

	FSessionWorld Session;
	if (!Session.Create(*this, ESessionLogin::BeforeBeginPlay))
	{
		return false;
	}

	ARaceDirector* Director = Session.GameMode->GetRaceDirector();
	if (!TestNotNull(TEXT("The game mode spawned the director before login"), Director))
	{
		return false;
	}

	// -- After login, before world BeginPlay ----------------------------------
	TestFalse(TEXT("Precondition: the world has not begun play"), Session.World->HasBegunPlay());
	TestTrue(TEXT("Setup ran inside RestartPlayer and succeeded"), Director->IsSessionSetUp());
	TestTrue(TEXT("The director resolved the world's track"), Director->GetTrack() == Session.Track);

	ARacingVehiclePawn* Pawn = Cast<ARacingVehiclePawn>(Session.Controller->GetPawn());
	if (!TestNotNull(TEXT("The controller possesses an ARacingVehiclePawn"), Pawn)
		|| !TestNotNull(TEXT("The director holds the state machine"), Director->GetStateMachine())
		|| !TestNotNull(TEXT("The director holds the result recorder"), Director->GetResultRecorder()))
	{
		return false;
	}
	TestTrue(TEXT("The director follows the pawn"), Director->GetCompetitor() == Pawn);
	TestFalse(TEXT("Precondition: the pawn has not begun play"), Pawn->HasActorBegunPlay());
	TestEqual(TEXT("Nothing auto-starts before play begins"), Director->GetStateMachine()->GetRaceState(), ERaceState::PreRace);

	const URaceResultRecorder* Recorder = Director->GetResultRecorder();
	TestEqual(TEXT("The input device is set at placement"), Recorder->GetInputDeviceType(), Pawn->InitialInputDeviceType);
	TestFalse(TEXT("The car spec waits for the pawn's BeginPlay (pending branch)"), Recorder->GetCarSpecVersion().IsPopulated());

	// -- World BeginPlay --------------------------------------------------------
	if (!Session.FinishBeginPlay(*this))
	{
		return false;
	}
	TestTrue(TEXT("The pawn began play"), Pawn->HasActorBegunPlay());
	TestTrue(TEXT("The pawn's chassis is applied"), Pawn->IsChassisApplied());
	TestEqual(TEXT("The director's BeginPlay auto-started the session"), Director->GetStateMachine()->GetRaceState(), ERaceState::Countdown);
	TestTrue(TEXT("StartPlay published the pending car spec"), Recorder->GetCarSpecVersion().IsPopulated());
	TestNotNull(TEXT("StartPlay spawned the graybox ground"), Session.GameMode->GetGrayboxGround());

	if (!TestTrue(TEXT("The world ticks"), Session.WorldWrapper.TickTestWorld(SessionSpecTickSeconds)))
	{
		return false;
	}
	TestEqual(TEXT("A zero-second countdown goes green on the first tick"), Director->GetStateMachine()->GetRaceState(), ERaceState::Racing);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSessionHudPlayerContextTest,
	"RacingSim.Game.Session.HudPlayerContext",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FRacingSessionHudPlayerContextTest::RunTest(const FString& Parameters)
{
	using namespace RacingSessionSpecPrivate;

	FSessionWorld Session;
	if (!Session.Create(*this))
	{
		return false;
	}

	// UI-002 M2: NativeOnInitialized only runs for a widget with a player context, which no
	// earlier test world had.
	URacingHudProbeWidget* Probe = CreateWidget<URacingHudProbeWidget>(Session.Controller, URacingHudProbeWidget::StaticClass());
	if (!TestNotNull(TEXT("CreateWidget with the controller as owner returns the probe"), Probe))
	{
		return false;
	}

	TestTrue(TEXT("NativeOnInitialized ran"), Probe->bNativeOnInitializedRan);
	TestTrue(
		FString::Printf(TEXT("The probe saw URacingHudWidget's text bindings (%d)"), Probe->TextBindingCountAtInit),
		Probe->TextBindingCountAtInit > 0);

	FString NullNames;
	for (const FName& Name : Probe->NullTextBindingsAtInit)
	{
		NullNames += Name.ToString() + TEXT(" ");
	}
	TestTrue(
		FString::Printf(TEXT("Every text binding was set inside NativeOnInitialized (null: %s)"), NullNames.IsEmpty() ? TEXT("none") : *NullNames),
		Probe->NullTextBindingsAtInit.IsEmpty());
	TestTrue(TEXT("The owning player inside NativeOnInitialized was the real local player"),
		Probe->OwningLocalPlayerAtInit.Get() == Session.LocalPlayer);

	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSessionBlueprintEmptyTreeFallbackTest,
	"RacingSim.Game.Session.BlueprintEmptyTreeFallback",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FRacingSessionBlueprintEmptyTreeFallbackTest::RunTest(const FString& Parameters)
{
	using namespace RacingSessionSpecPrivate;

	FSessionWorld Session;
	if (!Session.Create(*this))
	{
		return false;
	}

	// A transient Widget Blueprint subclass whose designer tree is empty: the case where
	// URacingHudWidget::InitializeNativeClassData does not run and Initialize() must build
	// the default tree itself.
	const FName BlueprintName = MakeUniqueObjectName(GetTransientPackage(), UWidgetBlueprint::StaticClass(), FName(TEXT("WBP_RacingHudEmptyTreeTest")));
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		URacingHudWidget::StaticClass(),
		GetTransientPackage(),
		BlueprintName,
		BPTYPE_Normal,
		UWidgetBlueprint::StaticClass(),
		UWidgetBlueprintGeneratedClass::StaticClass());
	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);
	if (!TestNotNull(TEXT("A Widget Blueprint subclass of URacingHudWidget is created"), WidgetBlueprint))
	{
		return false;
	}
	TestTrue(TEXT("Precondition: the Blueprint's designer tree is empty"),
		WidgetBlueprint->WidgetTree == nullptr || WidgetBlueprint->WidgetTree->RootWidget == nullptr);

	FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
	UClass* GeneratedClass = WidgetBlueprint->GeneratedClass;
	TestTrue(TEXT("The Blueprint compiles without error"), WidgetBlueprint->Status != BS_Error);

	URacingHudWidget* Widget = nullptr;
	if (TestNotNull(TEXT("The Blueprint has a generated class"), GeneratedClass)
		&& TestTrue(TEXT("The generated class is a URacingHudWidget"), GeneratedClass->IsChildOf(URacingHudWidget::StaticClass())))
	{
		Widget = CreateWidget<URacingHudWidget>(Session.Controller, GeneratedClass);
		if (TestNotNull(TEXT("CreateWidget with a player context returns the Blueprint widget"), Widget))
		{
			TestTrue(TEXT("The Blueprint widget is owned by the local player"), Widget->GetOwningLocalPlayer() == Session.LocalPlayer);
			TestNotNull(TEXT("The default tree was built: the root widget exists"), Widget->GetRootWidget());
			TestNotNull(TEXT("The default tree was built: SpeedText is bound"), Widget->SpeedText.Get());
		}
	}

	// The Blueprint lives in the transient package; drop it so no later test iterating
	// URacingHudWidget subclasses finds it.
	if (Widget != nullptr)
	{
		Widget->MarkAsGarbage();
	}
	WidgetBlueprint->ClearFlags(RF_Public | RF_Standalone);
	WidgetBlueprint->MarkAsGarbage();
	if (GeneratedClass != nullptr)
	{
		GeneratedClass->ClearFlags(RF_Public | RF_Standalone);
		GeneratedClass->MarkAsGarbage();
	}

	return true;
}
#endif // WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSessionDefaultConfigTest,
	"RacingSim.Game.Session.DefaultConfig",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FRacingSessionDefaultConfigTest::RunTest(const FString& Parameters)
{
	using namespace RacingSessionSpecPrivate;

	// GetGameDefaultMap returns the long package name, not the object path the ini holds.
	TestEqual(TEXT("GameDefaultMap is the graybox map"), UGameMapsSettings::GetGameDefaultMap(), FString(Graybox_MapPackagePath));
#if WITH_EDITORONLY_DATA
	TestEqual(TEXT("EditorStartupMap is the graybox map"),
		GetDefault<UGameMapsSettings>()->EditorStartupMap.GetLongPackageName(), FString(Graybox_MapPackagePath));
#endif
	TestEqual(TEXT("GlobalDefaultGameMode is ARacingGameMode"), UGameMapsSettings::GetGlobalDefaultGameMode(), FString(RacingGameModePath));
	TestTrue(TEXT("The GlobalDefaultGameMode path resolves to ARacingGameMode"),
		FSoftClassPath(RacingGameModePath).TryLoadClass<AGameModeBase>() == ARacingGameMode::StaticClass());

	// The map must not override the global game mode, or the session would never compose.
	// Loaded as TrackPrototypeLevelSpec loads it: the package only, no map open.
	if (!TestTrue(TEXT("The graybox map package exists"), FPackageName::DoesPackageExist(Graybox_MapPackagePath)))
	{
		return false;
	}
	TStrongObjectPtr<UPackage> Package(LoadPackage(nullptr, Graybox_MapPackagePath, LOAD_None));
	if (!TestNotNull(TEXT("LoadPackage returns the graybox map"), Package.Get()))
	{
		return false;
	}
	UWorld* MapWorld = UWorld::FindWorldInPackage(Package.Get());
	if (!TestNotNull(TEXT("The package holds a UWorld"), MapWorld) || !TestNotNull(TEXT("The world has a persistent level"), MapWorld->PersistentLevel.Get()))
	{
		return false;
	}
	const AWorldSettings* MapSettings = MapWorld->PersistentLevel->GetWorldSettings(/*bChecked*/ false);
	if (!TestNotNull(TEXT("The graybox map has world settings"), MapSettings))
	{
		return false;
	}
	TestNull(TEXT("The graybox map's DefaultGameMode is unset, so the global ARacingGameMode applies"), MapSettings->DefaultGameMode.Get());

	return true;
}
