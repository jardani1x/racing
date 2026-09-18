// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RaceDirector.generated.h"

class APawn;
class ATrackDefinitionActor;
class URaceLapTracker;
class URaceResultRecorder;
class URaceRulesetDataAsset;
class URaceStateMachine;
struct FRacingHudRaceInputs;

/**
 * RACE-005: the per-level owner of one race session.
 *
 * Before this class, every race object existed and was tested, but nothing created them
 * for a level: URaceStateMachine, URaceLapTracker and URaceResultRecorder were assembled
 * by hand in each spec. The director is the one place that does it in a running game.
 *
 * WHAT IT OWNS. The session's state machine, its single lap tracker (the recorder's
 * primary) and the result recorder, all created with the director as outer and held as
 * transient UPROPERTYs. It follows ONE competitor as a plain APawn: it does not include
 * Vehicle/ or UI/, so the race layer stays independent of both. The composition root
 * (Game/, ARacingGameMode) is what hands it a pawn.
 *
 * SETUP IS LAZY AND RUNS ONCE, from the first RegisterCompetitor() or from BeginPlay(),
 * whichever comes first. LoadMap logs the local player in -- and so spawns and registers
 * the pawn -- before world BeginPlay, so setup cannot wait for BeginPlay. A failed setup
 * is cached with its reason and logged once; it is not retried per frame.
 *
 * TRACK RESOLUTION. The explicit Track property if set; otherwise ONE TActorIterator
 * pass at setup, which must find exactly one ATrackDefinitionActor. Zero or several is a
 * setup error naming the count -- picking "the first" of two tracks would race whichever
 * the iterator happened to return. There is no search after setup.
 *
 * TICK (TG_PostPhysics, so the pawn has already moved this frame):
 *   1. PollAutoTransitions() -- Countdown -> Racing on the monotonic clock;
 *   2. project the pawn onto the centerline with FindNearestCenterlinePointNear around
 *      the last distance, in a window capped below a quarter lap so it can never degrade
 *      to the global (wrong-leg) search;
 *   3. URaceLapTracker::Advance -- which accepts samples outside Racing but only runs
 *      lap logic while Racing, so the grid-to-green roll-up is tracked, not timed;
 *   4. when Racing and GetLapsCompleted() >= the ruleset's LapsToFinish, FinishRace()
 *      and ShowResults(). The recorder freezes the result on the Finished entry.
 * No allocation, no actor search, no asset load on this path.
 *
 * AUTHORITY. Standalone, one process per stream session (Docs/01-Architecture.md). A
 * networked director is out of scope; the state machine's own authority check still
 * applies to every transition.
 */
UCLASS()
class RACINGSIM_API ARaceDirector : public AActor
{
	GENERATED_BODY()

public:
	ARaceDirector();

	/** RulesetId given to the transient ruleset created when Ruleset is null. */
	static const FName DefaultRulesetId;

	/**
	 * The circuit this session runs on. Optional: when null, setup resolves the single
	 * ATrackDefinitionActor in the world (see the class comment). Set it when a level
	 * holds more than one track.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Director")
	TObjectPtr<ATrackDefinitionActor> Track;

	/**
	 * The session's rules. Optional: when null, setup creates a transient ruleset with the
	 * C++ defaults and RulesetId DefaultRulesetId, and warns once. That is a graybox
	 * default at the composition root, not a substitute for an authored asset: a result
	 * raced on it records Ruleset.Graybox.Default, so it is never mistaken for one raced
	 * on an authored ruleset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Director")
	TObjectPtr<URaceRulesetDataAsset> Ruleset;

	/**
	 * Half-width, cm, of the centerline window searched around the last known distance.
	 *
	 * 2000 cm per tick is 1200 m/s of headroom at 60 Hz, far beyond any car here, while
	 * staying clear of a hairpin's other leg. Capped at setup to just below a quarter of
	 * the lap (GetEffectiveSearchWindowCm): a window of half the lap or more silently
	 * falls back to the global search, which is the wrong-leg snapping the windowed query
	 * exists to prevent.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Director", meta = (ClampMin = "100.0", UIMin = "100.0", UIMax = "10000.0", ForceUnits = "cm"))
	double ProgressSearchWindowCm = 2000.0;

	/**
	 * Start the session (PreRace -> Countdown) automatically once the director has begun
	 * play AND has a competitor. Attempted once; a refusal is logged once with its reason
	 * and the session stays in PreRace. Tests that drive the session by hand turn it off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Director")
	bool bAutoStartSession = true;

	/**
	 * Create the race objects and bind them to the track. IDEMPOTENT: runs once; later
	 * calls return the cached outcome.
	 *
	 * @param OutReason the setup error on failure; untouched on success.
	 * @return true when the state machine, tracker and recorder exist and are configured.
	 */
	bool EnsureSessionSetup(FString& OutReason);

	/**
	 * Follow Pawn as this session's competitor, seeding the lap tracker at GridDistanceCm
	 * (the arc length of the grid slot it was placed on -- ATrackDefinitionActor::
	 * GetGridSlotPose returns it). One competitor: a second call replaces the first.
	 *
	 * Runs setup if it has not run. With bAutoStartSession and play already begun, also
	 * attempts StartSession().
	 *
	 * @return false when Pawn is null, the distance is negative (the grid sentinel), setup
	 *         failed, or the session has left PreRace; OutReason says which.
	 */
	bool RegisterCompetitor(APawn* Pawn, double GridDistanceCm, FString& OutReason);

	/**
	 * PreRace -> Countdown, gated on URaceResultRecorder::CanStartSession. On refusal the
	 * state is left in PreRace.
	 *
	 * @return true only when this call applied BeginCountdown.
	 */
	bool StartSession(FString& OutReason);

	/**
	 * The HUD's race inputs for the followed competitor. Wraps
	 * URaceFunctionLibrary::GatherHudRaceInputs with competitor count 1. No allocation.
	 *
	 * @return false (and default inputs) before a successful setup.
	 */
	bool GatherHudRaceInputs(FRacingHudRaceInputs& OutInputs) const;

	/** The window actually used by Tick; ProgressSearchWindowCm capped below a quarter lap. 0 before setup. */
	double GetEffectiveSearchWindowCm() const { return EffectiveSearchWindowCm; }

	/** Last centerline distance the competitor was projected to, cm; negative before the first seed. */
	double GetLastCompetitorDistanceCm() const { return LastDistanceCm; }

	bool IsSessionSetUp() const { return bSetupSucceeded; }
	bool HasSetupBeenAttempted() const { return bSetupAttempted; }
	const FString& GetSetupError() const { return SetupError; }

	URaceStateMachine* GetStateMachine() const { return StateMachine; }
	URaceLapTracker* GetLapTracker() const { return LapTracker; }
	URaceResultRecorder* GetResultRecorder() const { return ResultRecorder; }

	/** The track in use: the explicit Track, or the one setup resolved. */
	ATrackDefinitionActor* GetTrack() const { return Track; }

	/** The ruleset in use: the authored Ruleset, or the transient default. */
	URaceRulesetDataAsset* GetActiveRuleset() const { return ActiveRuleset; }

	APawn* GetCompetitor() const { return Competitor.Get(); }

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor interface

private:
	/** Setup body; EnsureSessionSetup caches its outcome. */
	bool RunSessionSetup(FString& OutReason);

	/** One StartSession attempt under bAutoStartSession, once play has begun and a competitor exists. */
	void TryAutoStart();

	UPROPERTY(Transient)
	TObjectPtr<URaceRulesetDataAsset> ActiveRuleset;

	UPROPERTY(Transient)
	TObjectPtr<URaceStateMachine> StateMachine;

	UPROPERTY(Transient)
	TObjectPtr<URaceLapTracker> LapTracker;

	UPROPERTY(Transient)
	TObjectPtr<URaceResultRecorder> ResultRecorder;

	/** Weak: the director follows the pawn, it does not keep a destroyed one alive. */
	TWeakObjectPtr<APawn> Competitor;

	double LastDistanceCm = -1.0;
	double EffectiveSearchWindowCm = 0.0;

	bool bSetupAttempted = false;
	bool bSetupSucceeded = false;
	FString SetupError;

	bool bAutoStartAttempted = false;
};
