// Copyright RacingSim. All Rights Reserved.

#include "Race/RaceDirector.h"

#include "Core/RacingHudTypes.h"
#include "Core/RacingSimLog.h"
#include "Race/RaceFunctionLibrary.h"
#include "Race/RaceLapTracker.h"
#include "Race/RaceResult.h"
#include "Race/RaceRulesetDataAsset.h"
#include "Race/RaceStateMachine.h"
#include "Race/TrackCenterline.h"
#include "Race/TrackDefinitionActor.h"

#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

const FName ARaceDirector::DefaultRulesetId(TEXT("Ruleset.Graybox.Default"));

namespace RaceDirectorPrivate
{
	/**
	 * The effective search window is capped at this fraction of the lap. Below a quarter,
	 * so `Window * 2 < Length / 2` and FindNearestCenterlinePointNear can never take its
	 * `Window * 2 >= Length` global-search fallback, with margin for float error.
	 */
	constexpr double MaxSearchWindowLapFraction = 0.24;
}

ARaceDirector::ARaceDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// After physics: the pawn has moved this frame, so the sample the tracker sees is the
	// car's position now, not last frame's.
	PrimaryActorTick.TickGroup = TG_PostPhysics;
}

bool ARaceDirector::EnsureSessionSetup(FString& OutReason)
{
	if (!bSetupAttempted)
	{
		bSetupAttempted = true;
		bSetupSucceeded = RunSessionSetup(SetupError);
		if (!bSetupSucceeded)
		{
			UE_LOG(LogRacingRace, Error, TEXT("%s: race session setup failed: %s"), *GetName(), *SetupError);
		}
	}

	if (!bSetupSucceeded)
	{
		OutReason = SetupError;
	}
	return bSetupSucceeded;
}

bool ARaceDirector::RunSessionSetup(FString& OutReason)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutReason = TEXT("The director has no world.");
		return false;
	}

	if (Track == nullptr)
	{
		// The one actor search this class makes, at setup, never per frame.
		int32 TrackCount = 0;
		ATrackDefinitionActor* Found = nullptr;
		for (TActorIterator<ATrackDefinitionActor> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				++TrackCount;
				Found = *It;
			}
		}

		if (TrackCount != 1)
		{
			OutReason = FString::Printf(
				TEXT("Expected exactly one ATrackDefinitionActor in the world, found %d. Set ARaceDirector::Track explicitly when a level holds more than one."),
				TrackCount);
			return false;
		}
		Track = Found;
	}
	else if (!IsValid(Track))
	{
		OutReason = TEXT("The director's Track has been destroyed.");
		return false;
	}

	const double TrackLengthCm = Track->GetTrackLengthCm();
	if (!(TrackLengthCm > 0.0))
	{
		OutReason = FString::Printf(TEXT("Track '%s' has no built centerline (length %.1f cm)."), *Track->GetName(), TrackLengthCm);
		return false;
	}

	// ClampMin only guards the editor; C++ and config can still write anything. A window of
	// zero, below, or NaN would silently fall back to the global nearest-point search the
	// window exists to prevent.
	if (!FMath::IsFinite(ProgressSearchWindowCm) || !(ProgressSearchWindowCm > 0.0))
	{
		OutReason = FString::Printf(TEXT("ProgressSearchWindowCm must be a positive finite distance, got %f cm."), ProgressSearchWindowCm);
		return false;
	}

	if (Ruleset != nullptr)
	{
		ActiveRuleset = Ruleset;
	}
	else
	{
		ActiveRuleset = NewObject<URaceRulesetDataAsset>(this, TEXT("GrayboxDefaultRuleset"), RF_Transient);
		ActiveRuleset->RulesetId = DefaultRulesetId;
		UE_LOG(LogRacingRace, Warning,
			TEXT("%s: no Ruleset assigned; racing on a transient default (%s, %d laps). Author a URaceRulesetDataAsset for anything but graybox."),
			*GetName(), *DefaultRulesetId.ToString(), ActiveRuleset->LapsToFinish);
	}

	// Same reason as the window check: a LapsToFinish below one would freeze a zero-lap
	// result on the first Racing tick.
	if (ActiveRuleset->LapsToFinish < 1)
	{
		OutReason = FString::Printf(TEXT("Ruleset '%s' has LapsToFinish %d; a race needs at least one lap."),
			*ActiveRuleset->RulesetId.ToString(), ActiveRuleset->LapsToFinish);
		return false;
	}

	StateMachine = URaceStateMachine::Create(this, ActiveRuleset);
	if (StateMachine == nullptr)
	{
		OutReason = TEXT("URaceStateMachine::Create returned null (no race authority?).");
		return false;
	}

	LapTracker = URaceLapTracker::Create(this, StateMachine, ActiveRuleset);
	if (LapTracker == nullptr)
	{
		OutReason = TEXT("URaceLapTracker::Create returned null.");
		return false;
	}

	FString TrackError;
	if (!LapTracker->ConfigureFromTrack(Track, TrackError))
	{
		OutReason = FString::Printf(TEXT("The lap tracker could not be configured from track '%s': %s"), *Track->GetName(), *TrackError);
		return false;
	}

	ResultRecorder = URaceResultRecorder::Create(this, StateMachine);
	if (ResultRecorder == nullptr)
	{
		OutReason = TEXT("URaceResultRecorder::Create returned null.");
		return false;
	}

	if (!ResultRecorder->RegisterLapTracker(LapTracker))
	{
		OutReason = TEXT("The result recorder refused the lap tracker.");
		return false;
	}
	ResultRecorder->SetTrack(Track);

	EffectiveSearchWindowCm = FMath::Min(ProgressSearchWindowCm, TrackLengthCm * RaceDirectorPrivate::MaxSearchWindowLapFraction);
	return true;
}

bool ARaceDirector::RegisterCompetitor(APawn* Pawn, const double GridDistanceCm, FString& OutReason)
{
	if (!IsValid(Pawn))
	{
		OutReason = TEXT("RegisterCompetitor needs a live pawn.");
		return false;
	}

	if (!(GridDistanceCm >= 0.0))
	{
		// ATrackDefinitionActor::InvalidDistanceCm, an out-of-range grid slot. Seeding the
		// tracker there would start the car's progress somewhere it is not.
		OutReason = FString::Printf(TEXT("Grid distance %.1f cm is not a centerline distance (out-of-range grid slot?)."), GridDistanceCm);
		return false;
	}

	if (!EnsureSessionSetup(OutReason))
	{
		return false;
	}

	// PreRace only. SeedProgress moves the tracker's progress without the teleport guard
	// or a lap invalidation, so a mid-race registration (a respawned pawn placed back on
	// the grid) could complete a shortened lap. A mid-race car move goes through
	// URaceLapTracker::NotifyVehicleReset; restart re-enters PreRace first.
	if (StateMachine->GetRaceState() != ERaceState::PreRace)
	{
		OutReason = FString::Printf(
			TEXT("RegisterCompetitor is only accepted in PreRace; the session is in %s."),
			*UEnum::GetValueAsString(StateMachine->GetRaceState()));
		return false;
	}

	Competitor = Pawn;
	LastDistanceCm = GridDistanceCm;
	LapTracker->SeedProgress(Pawn->GetActorLocation(), GridDistanceCm);

	TryAutoStart();
	return true;
}

bool ARaceDirector::StartSession(FString& OutReason)
{
	if (!EnsureSessionSetup(OutReason))
	{
		return false;
	}

	if (!ResultRecorder->CanStartSession(OutReason))
	{
		UE_LOG(LogRacingRace, Error, TEXT("%s: session start refused: %s"), *GetName(), *OutReason);
		return false;
	}

	if (StateMachine->BeginCountdown() != ERaceTransitionResult::Applied)
	{
		OutReason = FString::Printf(
			TEXT("BeginCountdown was not applied from state %s."),
			*UEnum::GetValueAsString(StateMachine->GetRaceState()));
		return false;
	}

	return true;
}

void ARaceDirector::TryAutoStart()
{
	// Inside BeginPlay HasActorBegunPlay() is still false; IsActorBeginningPlay() covers it.
	const bool bPlaying = HasActorBegunPlay() || IsActorBeginningPlay();
	if (!bAutoStartSession || bAutoStartAttempted || !bPlaying || !Competitor.IsValid() || !bSetupSucceeded)
	{
		return;
	}

	// Once: a refusal is a configuration fault, and retrying it per frame would only
	// repeat the same log line. StartSession has already logged the reason.
	bAutoStartAttempted = true;
	FString Reason;
	StartSession(Reason);
}

void ARaceDirector::BeginPlay()
{
	Super::BeginPlay();

	FString Reason;
	if (EnsureSessionSetup(Reason))
	{
		TryAutoStart();
	}
}

void ARaceDirector::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bSetupSucceeded || StateMachine == nullptr || LapTracker == nullptr)
	{
		return;
	}

	StateMachine->PollAutoTransitions();

	const APawn* Pawn = Competitor.Get();
	if (Pawn == nullptr || !IsValid(Track) || !(LastDistanceCm >= 0.0))
	{
		return;
	}

	const FVector Location = Pawn->GetActorLocation();
	const FTrackCenterlineQuery Query = Track->FindNearestCenterlinePointNear(Location, LastDistanceCm, EffectiveSearchWindowCm);
	if (!Query.bValid)
	{
		return;
	}

	LastDistanceCm = Query.DistanceAlongCm;
	LapTracker->Advance(Location, Query.DistanceAlongCm);

	if (StateMachine->GetRaceState() == ERaceState::Racing
		&& ActiveRuleset != nullptr
		&& LapTracker->GetLapsCompleted() >= ActiveRuleset->LapsToFinish)
	{
		// Finished freezes the result (the recorder listens for it); Results is the
		// presentation state the HUD's results panel keys on.
		StateMachine->FinishRace();
		StateMachine->ShowResults();
	}
}

bool ARaceDirector::GatherHudRaceInputs(FRacingHudRaceInputs& OutInputs) const
{
	if (!bSetupSucceeded)
	{
		OutInputs = FRacingHudRaceInputs();
		return false;
	}

	return URaceFunctionLibrary::GatherHudRaceInputs(StateMachine, LapTracker, ResultRecorder, /*CompetitorCount*/ 1, OutInputs);
}
