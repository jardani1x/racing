// Copyright RacingSim. All Rights Reserved.

#include "Race/RaceDirector.h"
#include "Race/RaceLapTracker.h"
#include "Race/RaceResult.h"
#include "Race/RaceRulesetDataAsset.h"
#include "Race/RaceStateMachine.h"
#include "Race/TrackCenterline.h"
#include "Race/TrackDefinitionActor.h"

#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include <limits>

/**
 * RACE-005: ARaceDirector on its own, without the game mode.
 *
 * Race/ only: the competitor is a plain APawn moved with SetActorLocation, the same stand-in
 * the director itself sees (it never includes Vehicle/). The world never begins play, so the
 * director's BeginPlay and tick registration do not run; each test calls Tick directly,
 * which is the whole per-frame path.
 *
 * ProductFilter, not Smoke: these spawn real actors (Docs/Environment.md).
 */

namespace RaceDirectorSpecPrivate
{
	// RaceResultSpec's proven 100 m radius, clear of any minimum corner radius.
	constexpr double DirectorSpecTrackRadiusCm = 10000.0;
	constexpr int32 DirectorSpecSplinePoints = 12;
	constexpr int32 DirectorSpecLapsToFinish = 2;

	/** Steps per lap when driving the pawn. Far below the search window per step. */
	constexpr int32 DirectorSpecStepsPerLap = 200;

	ATrackDefinitionActor* SpawnTestTrack(FAutomationTestBase& Test, UWorld* World, const TCHAR* TrackId)
	{
		// Deferred, so construction bakes the authored circle rather than the default spline.
		ATrackDefinitionActor* Track = World->SpawnActorDeferred<ATrackDefinitionActor>(
			ATrackDefinitionActor::StaticClass(), FTransform::Identity);
		if (Track == nullptr)
		{
			Test.AddError(TEXT("The track actor failed to spawn."));
			return nullptr;
		}
		Track->SetFlags(RF_Transient);

		if (USplineComponent* Spline = Track->GetCenterlineSpline())
		{
			TArray<FVector> Points;
			for (int32 Index = 0; Index < DirectorSpecSplinePoints; ++Index)
			{
				const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(Index) / static_cast<double>(DirectorSpecSplinePoints);
				Points.Add(FVector(DirectorSpecTrackRadiusCm * FMath::Cos(Angle), DirectorSpecTrackRadiusCm * FMath::Sin(Angle), 0.0));
			}
			Spline->SetClosedLoop(true, /*bUpdateSpline*/ false);
			Spline->SetSplinePoints(Points, ESplineCoordinateSpace::Local, /*bUpdateSpline*/ true);
		}
		Track->TrackId = FName(TrackId);
		// ATrackDefinitionActor::Validate requires sector 1 to start at the line.
		Track->SectorStartDistancesCm = { 0.0 };
		Track->FinishSpawning(FTransform::Identity);
		Track->RebuildTrackData();

		FString Reason;
		if (!Track->GetCachedValidation(Reason))
		{
			Test.AddError(FString::Printf(TEXT("The test track failed validation: %s"), *Reason));
			return nullptr;
		}
		return Track;
	}

	URaceRulesetDataAsset* MakeTestRuleset(UObject* Outer)
	{
		URaceRulesetDataAsset* Ruleset = NewObject<URaceRulesetDataAsset>(Outer, NAME_None, RF_Transient);
		Ruleset->RulesetId = FName(TEXT("Ruleset.Test.Director"));
		Ruleset->CountdownSeconds = 0.0;
		Ruleset->LapsToFinish = DirectorSpecLapsToFinish;
		return Ruleset;
	}

	/** Deferred, so the ruleset and auto-start flag are in place before anything can run setup. */
	ARaceDirector* SpawnTestDirector(FAutomationTestBase& Test, UWorld* World)
	{
		ARaceDirector* Director = World->SpawnActorDeferred<ARaceDirector>(ARaceDirector::StaticClass(), FTransform::Identity);
		if (Director == nullptr)
		{
			Test.AddError(TEXT("The race director failed to spawn."));
			return nullptr;
		}
		Director->SetFlags(RF_Transient);
		Director->Ruleset = MakeTestRuleset(Director);
		Director->bAutoStartSession = false;
		UGameplayStatics::FinishSpawningActor(Director, FTransform::Identity);
		return Director;
	}

	APawn* SpawnStandInPawn(FAutomationTestBase& Test, UWorld* World, const FTransform& Pose)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		APawn* Pawn = World->SpawnActor<APawn>(APawn::StaticClass(), Pose, Params);
		if (Pawn == nullptr)
		{
			Test.AddError(TEXT("The stand-in pawn failed to spawn."));
			return nullptr;
		}

		// A bare APawn has no root component, so SetActorLocation would silently do nothing
		// and the "car" would never leave the origin.
		USceneComponent* Root = NewObject<USceneComponent>(Pawn, TEXT("StandInRoot"), RF_Transient);
		Pawn->SetRootComponent(Root);
		Root->RegisterComponent();
		Pawn->SetActorTransform(Pose);
		return Pawn;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceDirectorLifecycleTest,
	"RacingSim.Race.Director.Lifecycle",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FRaceDirectorLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace RaceDirectorSpecPrivate;

	FTestWorldWrapper WorldWrapper;
	if (!WorldWrapper.CreateTestWorld(EWorldType::Game) || WorldWrapper.GetTestWorld() == nullptr)
	{
		AddError(TEXT("FTestWorldWrapper::CreateTestWorld(EWorldType::Game) failed."));
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();

	ATrackDefinitionActor* Track = SpawnTestTrack(*this, World, TEXT("Track.Test.DirectorLifecycle"));
	ARaceDirector* Director = SpawnTestDirector(*this, World);
	if (Track == nullptr || Director == nullptr)
	{
		return false;
	}

	// -- Setup ----------------------------------------------------------------
	FString Reason;
	if (!TestTrue(TEXT("EnsureSessionSetup succeeds with exactly one track in the world"), Director->EnsureSessionSetup(Reason)))
	{
		AddInfo(FString::Printf(TEXT("Setup error: %s"), *Reason));
		return false;
	}
	TestTrue(TEXT("Setup resolved the world's one track"), Director->GetTrack() == Track);
	TestTrue(TEXT("The authored ruleset is the active one"), Director->GetActiveRuleset() == Director->Ruleset);
	TestNotNull(TEXT("Setup created the state machine"), Director->GetStateMachine());
	TestNotNull(TEXT("Setup created the lap tracker"), Director->GetLapTracker());
	TestNotNull(TEXT("Setup created the result recorder"), Director->GetResultRecorder());
	if (Director->GetStateMachine() == nullptr || Director->GetLapTracker() == nullptr || Director->GetResultRecorder() == nullptr)
	{
		return false;
	}

	const double LengthCm = Track->GetTrackLengthCm();
	TestTrue(TEXT("The effective search window is positive"), Director->GetEffectiveSearchWindowCm() > 0.0);
	TestTrue(TEXT("The effective search window is capped at 0.24 of the lap"),
		Director->GetEffectiveSearchWindowCm() <= 0.24 * LengthCm);
	TestTrue(TEXT("A second EnsureSessionSetup returns the cached success"), Director->EnsureSessionSetup(Reason));

	// -- Register on the pole slot -----------------------------------------------
	double GridDistanceCm = ATrackDefinitionActor::InvalidDistanceCm;
	const FTransform GridPose = Track->GetGridSlotPose(0, GridDistanceCm);
	if (!TestTrue(TEXT("Grid slot 0 exists"), GridDistanceCm >= 0.0))
	{
		return false;
	}
	APawn* Pawn = SpawnStandInPawn(*this, World, GridPose);
	if (Pawn == nullptr)
	{
		return false;
	}

	TestFalse(TEXT("RegisterCompetitor refuses the grid sentinel distance"),
		Director->RegisterCompetitor(Pawn, ATrackDefinitionActor::InvalidDistanceCm, Reason));
	TestTrue(TEXT("RegisterCompetitor accepts the pawn at its grid distance"), Director->RegisterCompetitor(Pawn, GridDistanceCm, Reason));
	TestTrue(TEXT("The director follows the registered pawn"), Director->GetCompetitor() == Pawn);
	TestEqual(TEXT("Progress is seeded at the grid distance"), Director->GetLastCompetitorDistanceCm(), GridDistanceCm);

	URaceStateMachine* StateMachine = Director->GetStateMachine();
	TestEqual(TEXT("Auto-start off: the session waits in PreRace"), StateMachine->GetRaceState(), ERaceState::PreRace);

	// -- Start, green -------------------------------------------------------------
	if (!TestTrue(TEXT("StartSession applies BeginCountdown"), Director->StartSession(Reason)))
	{
		AddInfo(FString::Printf(TEXT("StartSession refused: %s"), *Reason));
		return false;
	}
	TestEqual(TEXT("StartSession moves PreRace -> Countdown"), StateMachine->GetRaceState(), ERaceState::Countdown);

	Director->Tick(0.016f);
	TestEqual(TEXT("A zero-second countdown goes green on the first tick"), StateMachine->GetRaceState(), ERaceState::Racing);

	// A mid-race registration would reseed progress past the teleport guard.
	const double DistanceBeforeReRegisterCm = Director->GetLastCompetitorDistanceCm();
	FString ReRegisterReason;
	TestFalse(TEXT("RegisterCompetitor is refused once the session is Racing"),
		Director->RegisterCompetitor(Pawn, GridDistanceCm + 100.0, ReRegisterReason));
	TestTrue(FString::Printf(TEXT("The refusal names PreRace (reason: %s)"), *ReRegisterReason), ReRegisterReason.Contains(TEXT("PreRace")));
	TestEqual(TEXT("The refused registration did not reseed progress"), Director->GetLastCompetitorDistanceCm(), DistanceBeforeReRegisterCm);

	// -- Drive until the flag ---------------------------------------------------
	// From the grid (behind the line) through LapsToFinish full laps: the first line
	// crossing opens lap 1, so the flag falls LapsToFinish laps later. Cap with two laps
	// of margin so a director that never finishes fails here rather than looping.
	const double StepCm = LengthCm / static_cast<double>(DirectorSpecStepsPerLap);
	const int32 MaxSteps = DirectorSpecStepsPerLap * (DirectorSpecLapsToFinish + 2);
	const FTrackCenterline& Centerline = Track->GetCenterline();
	double DistanceCm = GridDistanceCm;
	int32 Steps = 0;
	for (; Steps < MaxSteps && StateMachine->GetRaceState() != ERaceState::Results; ++Steps)
	{
		DistanceCm += StepCm;
		const FVector Target = Centerline.GetLocationAtDistanceCm(FMath::Fmod(DistanceCm, LengthCm));
		Pawn->SetActorLocation(Target);
		if (Steps == 0 && !TestTrue(TEXT("The stand-in pawn follows SetActorLocation"), Pawn->GetActorLocation().Equals(Target, 0.1)))
		{
			return false;
		}
		Director->Tick(0.016f);
	}

	TestEqual(TEXT("The session reaches Results within the step cap"), StateMachine->GetRaceState(), ERaceState::Results);
	AddInfo(FString::Printf(TEXT("Results after %d steps of %.1f cm (lap %.1f cm)."), Steps, StepCm, LengthCm));
	TestEqual(TEXT("The lap tracker completed LapsToFinish laps"), Director->GetLapTracker()->GetLapsCompleted(), DirectorSpecLapsToFinish);

	const URaceResultRecorder* Recorder = Director->GetResultRecorder();
	TestTrue(TEXT("The recorder froze the result"), Recorder->HasFrozenResult());
	TestEqual(TEXT("The frozen result records LapsToFinish laps"), Recorder->GetFrozenResult().LapsCompleted, DirectorSpecLapsToFinish);

	// Further ticks after the flag change nothing.
	Director->Tick(0.016f);
	TestEqual(TEXT("A tick after Results stays in Results"), StateMachine->GetRaceState(), ERaceState::Results);

	// -- Ambiguous track ------------------------------------------------------
	// A second track makes the world ambiguous for a director with no explicit Track.
	ATrackDefinitionActor* SecondTrack = SpawnTestTrack(*this, World, TEXT("Track.Test.DirectorLifecycle.Second"));
	ARaceDirector* SecondDirector = SpawnTestDirector(*this, World);
	if (SecondTrack == nullptr || SecondDirector == nullptr)
	{
		return false;
	}
	AddExpectedMessagePlain(
		TEXT("Expected exactly one ATrackDefinitionActor in the world, found 2"),
		ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains,
		1);
	FString AmbiguousReason;
	TestFalse(TEXT("Setup refuses a world with two tracks"), SecondDirector->EnsureSessionSetup(AmbiguousReason));
	TestTrue(TEXT("The refusal names the count"), AmbiguousReason.Contains(TEXT("found 2")));
	TestNull(TEXT("A refused setup creates no state machine"), SecondDirector->GetStateMachine());
	FString CachedReason;
	TestFalse(TEXT("A refused setup is cached, not retried"), SecondDirector->EnsureSessionSetup(CachedReason));
	TestEqual(TEXT("The cached refusal repeats the reason"), CachedReason, AmbiguousReason);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceDirectorDestroyedTrackTest,
	"RacingSim.Race.Director.DestroyedTrackRefusesSession",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FRaceDirectorDestroyedTrackTest::RunTest(const FString& Parameters)
{
	using namespace RaceDirectorSpecPrivate;

	FTestWorldWrapper WorldWrapper;
	if (!WorldWrapper.CreateTestWorld(EWorldType::Game) || WorldWrapper.GetTestWorld() == nullptr)
	{
		AddError(TEXT("FTestWorldWrapper::CreateTestWorld(EWorldType::Game) failed."));
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();

	ATrackDefinitionActor* Track = SpawnTestTrack(*this, World, TEXT("Track.Test.DirectorDestroyed"));
	ARaceDirector* Director = SpawnTestDirector(*this, World);
	if (Track == nullptr || Director == nullptr)
	{
		return false;
	}

	FString Reason;
	if (!TestTrue(TEXT("EnsureSessionSetup succeeds"), Director->EnsureSessionSetup(Reason)))
	{
		return false;
	}

	double GridDistanceCm = ATrackDefinitionActor::InvalidDistanceCm;
	const FTransform GridPose = Track->GetGridSlotPose(0, GridDistanceCm);
	APawn* Pawn = SpawnStandInPawn(*this, World, GridPose);
	if (Pawn == nullptr || !TestTrue(TEXT("RegisterCompetitor succeeds"), Director->RegisterCompetitor(Pawn, GridDistanceCm, Reason)))
	{
		return false;
	}

	URaceResultRecorder* Recorder = Director->GetResultRecorder();
	FString PreconditionReason;
	if (!TestTrue(TEXT("Precondition: the session could start before the track is destroyed"), Recorder->CanStartSession(PreconditionReason)))
	{
		AddInfo(FString::Printf(TEXT("CanStartSession refused: %s"), *PreconditionReason));
		return false;
	}

	TestTrue(TEXT("DestroyActor succeeds"), World->DestroyActor(Track));

	FString RefusalReason;
	TestFalse(TEXT("CanStartSession refuses once the held track is destroyed"), Recorder->CanStartSession(RefusalReason));
	TestTrue(FString::Printf(TEXT("The refusal names the destroyed track (reason: %s)"), *RefusalReason),
		RefusalReason.Contains(TEXT("destroyed")));

	AddExpectedMessagePlain(
		TEXT("session start refused"),
		ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains,
		1);
	FString StartReason;
	TestFalse(TEXT("StartSession refuses"), Director->StartSession(StartReason));
	TestEqual(TEXT("StartSession reports the recorder's reason"), StartReason, RefusalReason);
	TestEqual(TEXT("The refused session stays in PreRace"), Director->GetStateMachine()->GetRaceState(), ERaceState::PreRace);

	// The per-frame path tolerates the destroyed track: no crash, no transition.
	Director->Tick(0.016f);
	TestEqual(TEXT("A tick with the track destroyed stays in PreRace"), Director->GetStateMachine()->GetRaceState(), ERaceState::PreRace);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceDirectorRefusesInvalidConfigurationTest,
	"RacingSim.Race.Director.RefusesInvalidConfiguration",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FRaceDirectorRefusesInvalidConfigurationTest::RunTest(const FString& Parameters)
{
	using namespace RaceDirectorSpecPrivate;

	FTestWorldWrapper WorldWrapper;
	if (!WorldWrapper.CreateTestWorld(EWorldType::Game) || WorldWrapper.GetTestWorld() == nullptr)
	{
		AddError(TEXT("FTestWorldWrapper::CreateTestWorld(EWorldType::Game) failed."));
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();

	ATrackDefinitionActor* Track = SpawnTestTrack(*this, World, TEXT("Track.Test.DirectorInvalidConfig"));
	if (Track == nullptr)
	{
		return false;
	}

	// ClampMin guards only the editor; both values below are reachable from C++ or config.
	struct FCase
	{
		const TCHAR* Name;
		int32 LapsToFinish;
		double SearchWindowCm;
		const TCHAR* ReasonMustContain;
	};
	const FCase Cases[] = {
		{ TEXT("LapsToFinish 0"), 0, 2000.0, TEXT("LapsToFinish") },
		{ TEXT("LapsToFinish -1"), -1, 2000.0, TEXT("LapsToFinish") },
		{ TEXT("search window 0"), DirectorSpecLapsToFinish, 0.0, TEXT("ProgressSearchWindowCm") },
		{ TEXT("search window negative"), DirectorSpecLapsToFinish, -50.0, TEXT("ProgressSearchWindowCm") },
		{ TEXT("search window NaN"), DirectorSpecLapsToFinish, std::numeric_limits<double>::quiet_NaN(), TEXT("ProgressSearchWindowCm") },
	};

	AddExpectedMessagePlain(
		TEXT("race session setup failed"),
		ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains,
		UE_ARRAY_COUNT(Cases));

	for (const FCase& Case : Cases)
	{
		ARaceDirector* Director = SpawnTestDirector(*this, World);
		if (Director == nullptr)
		{
			return false;
		}
		Director->Track = Track;
		Director->Ruleset->LapsToFinish = Case.LapsToFinish;
		Director->ProgressSearchWindowCm = Case.SearchWindowCm;

		FString Reason;
		TestFalse(FString::Printf(TEXT("%s: EnsureSessionSetup refuses"), Case.Name), Director->EnsureSessionSetup(Reason));
		TestTrue(FString::Printf(TEXT("%s: the reason names %s (reason: %s)"), Case.Name, Case.ReasonMustContain, *Reason),
			Reason.Contains(Case.ReasonMustContain));
		TestFalse(FString::Printf(TEXT("%s: the session is not set up"), Case.Name), Director->IsSessionSetUp());
		TestNull(FString::Printf(TEXT("%s: no state machine was published"), Case.Name), Director->GetStateMachine());

		World->DestroyActor(Director);
	}

	return true;
}
