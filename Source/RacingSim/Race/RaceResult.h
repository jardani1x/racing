// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/RacingSimBuildId.h"
#include "Core/RacingSimTypes.h"
#include "Core/RacingTelemetry.h"
#include "RaceResult.generated.h"

class ATrackDefinitionActor;
class URaceLapTracker;
class URaceStateMachine;

/**
 * RACE-003: the frozen race result, and the object that freezes it.
 *
 * ===========================================================================
 * What this file is
 * ===========================================================================
 *
 * RACE-001 built the state machine's shape; RACE-002 built per-lap truth. This is what
 * happens at the two transitions neither of them owned:
 *
 *   Finished -> the result is ASSEMBLED ONCE from what those two already computed, and
 *               frozen. Docs/03-TrackRaceUI.md: "Freeze the result once, disable further
 *               lap counting."
 *   Restart  -> every tracker is reset, the frozen result is dropped, and nothing from
 *               the previous session survives into the next one. Same doc: "Perform a
 *               complete state reset without reusing stale timers, delegates, input, or
 *               checkpoint state."
 *
 * ASSEMBLED, NEVER RE-DERIVED. Every number in FRacingRaceResult is a copy of a value
 * some other system is authoritative for: URaceLapTracker for laps and validity,
 * URaceStateMachine's FRaceClock for the final time, ATrackDefinitionActor for the track
 * version and its validity, FRacingSimVersionStamp for the build/engine/assist metadata.
 * Nothing here recomputes a lap, re-times anything, or forms a second opinion. If a
 * future edit makes this file consult a gate, a spline or a crossing, the layering has
 * gone wrong -- the one place permitted to decide a lap happened is URaceLapTracker, and
 * it says so in its own header.
 *
 * ===========================================================================
 * Durations are stored, never formatted
 * ===========================================================================
 *
 * Every time in this file is SECONDS as double from the authoritative monotonic clock.
 * Docs/03-TrackRaceUI.md: "Store raw duration with high precision; format only in UI."
 * There is no FString time anywhere in FRacingRaceResult and there must never be one --
 * ToString() exists for logs and is explicitly not a display path.
 */

/**
 * The frozen outcome of one race session, for one competitor.
 *
 * ===========================================================================
 * The field list is a contract, not a convenience
 * ===========================================================================
 *
 * Docs/03-TrackRaceUI.md's Timing section commits to exactly this list: "Results include
 * track version, car tune version, assist state, validity, and build ID", plus
 * "final/best time, validity, splits" from the Results state. Every one of those has a
 * home below, and the four version-ish ones live inside `Version` rather than being
 * copied out flat, because FRacingSimVersionStamp is CORE-002's published contract for
 * "everything a competitive result must carry to be interpretable later" and splitting it
 * would create a second, drifting copy of the same list.
 *
 * ===========================================================================
 * ONE COMPETITOR, and why that is not a shortcut
 * ===========================================================================
 *
 * This slice has one car. FRacingProgressSample::RacePosition already encodes the same
 * decision from the other side ("0 means not yet classified", because
 * Docs/03-TrackRaceUI.md requires position to be shown only when opponents exist), and
 * URaceResultRecorder can hold several URaceLapTrackers so a restart resets all of them.
 * What it does NOT do is invent a multi-entrant result shape before there is an opponent
 * to put in it: the entrant array, the finishing order and the gap-to-leader are a
 * different data contract with different questions (who is classified? what is a DNF?)
 * and they belong to the ticket that first has two cars. This struct describes the
 * PRIMARY tracker, which is the first one registered.
 *
 * ===========================================================================
 * THE LAP-NUMBER CONVENTION (RACE-002 finding L9, closed here)
 * ===========================================================================
 *
 * Three counters, three different numbers, and conflating any two is a bug. The full
 * statement lives at URaceLapTracker's accessors; the operative part for a consumer of
 * THIS struct is:
 *
 *   THE FIRST FORWARD CROSSING OF THE LINE OPENS LAP 1 AND CLOSES NOTHING. The car
 *   starts on a grid BEHIND the line, so the out-lap is not a lap and has no time.
 *
 * Therefore, while lap N is being timed:
 *
 *     CurrentLapNumberAtFinish == N
 *     LapsCompleted            == N - 1        (ended at the line, valid or not)
 *     ValidLapsCompleted       <= N - 1        (ended clean)
 *
 * A results screen showing "laps" must use LapsCompleted, never
 * CurrentLapNumberAtFinish, or it will credit a lap the driver was still on. A
 * leaderboard must use ValidLapsCompleted and BestLap. UI-001 reads this paragraph
 * rather than deriving a lap count in a widget -- Docs/03-TrackRaceUI.md requires HUD
 * widgets to stay passive.
 *
 * ONE DOCUMENTED CASE BREAKS THE `N - 1` RELATION and a consumer must not assume it
 * cannot: a lap driven entirely outside every gate rectangle is refused a lap boundary at
 * the line (URaceLapTracker::Advance rule 7), so LapsCompleted sits one below the number
 * of physical laps driven and the lap that eventually closes spans more than one physical
 * lap. RACE-003 made that lap INVALID (rule 8, closing RACE-002 finding R2-M1), so it can
 * never reach BestLap -- but it can reach LastLap, and LastLap.LapDurationSeconds is then
 * not one lap's time. That is why LastLap and BestLap are separate fields and why nothing
 * downstream may treat LastLap as a fallback for BestLap.
 *
 * ===========================================================================
 * SECTOR SPLITS (RACE-002 finding M2, decided here)
 * ===========================================================================
 *
 * BestLap and LastLap each carry their own complete FRacingLapTiming, including that
 * lap's full SectorDurationsSeconds array -- that is where "all sector splits" lives. A
 * per-lap ARCHIVE of every lap in the session is deliberately not kept: URaceLapTracker
 * retains exactly the best and the last (RACE-002's design), and building a history here
 * would mean this file storing race data nobody else has, which is the re-derivation this
 * layer exists not to do.
 *
 * TrackSectorCount is the number a consumer needs to interpret an EMPTY split array,
 * which is a legitimate shape and not always an error. FRacingLapTiming::
 * SectorDurationsSeconds documents the three cases; the short version:
 *
 *   TrackSectorCount == 0  ->  a sectorless track. Empty splits are correct and
 *                              AreSectorsConsistent() is vacuously true (finding L2).
 *   TrackSectorCount >  0 and the array is empty  ->  the splits were WITHHELD because
 *                              the set would have been incomplete, and the lap carries
 *                              ERacingRunValidity::InvalidIncomplete to say so.
 *
 * A PARTIAL SET IS NEVER PUBLISHED. To check a lap properly, pass the count:
 * `Result.BestLap.AreSectorsConsistent(0.001, Result.TrackSectorCount)`.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FRacingRaceResult
{
	GENERATED_BODY()

	// =======================================================================
	// Metadata -- the five things Docs/03-TrackRaceUI.md names
	// =======================================================================

	/**
	 * Build ID, engine patch, TRACK VERSION, CAR TUNE VERSION, ruleset version, physics
	 * policy, ASSIST STATE, input device, VALIDITY and penalties.
	 *
	 * Version.Validity is the result's overall ERacingRunValidity. It lives there rather
	 * than as a second top-level field because CORE-002 reserved that exact slot for it
	 * ("Validity -> RACE-002/RACE-003, at run end"), and two homes for one verdict is how
	 * a HUD and a leaderboard end up disagreeing about whether a run counted. Read it
	 * through GetValidity().
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Result")
	FRacingSimVersionStamp Version;

	// =======================================================================
	// Timing. Seconds as double, from the one monotonic clock. Never formatted.
	// =======================================================================

	/**
	 * Total session time at the finish, in SECONDS.
	 *
	 * URaceStateMachine's race clock, frozen by the Finished entry action before this
	 * result was assembled. Not a sum of lap times: a session includes the out-lap and
	 * any time after the last lap boundary, and reconstructing it by addition would give
	 * a different number that looks equally plausible.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Result", meta = (ClampMin = "0.0"))
	double FinalTimeSeconds = 0.0;

	/** The fastest VALID lap of the session, with its splits. LapNumber == 0 when there was none. */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Result")
	FRacingLapTiming BestLap;

	/**
	 * The last lap that ended at the line, valid or not, with its splits. LapNumber == 0
	 * when none did.
	 *
	 * NOT a fallback for BestLap. See the lap-number block above: an invalid lap can span
	 * more than one physical lap, so its duration is not comparable with anything.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Result")
	FRacingLapTiming LastLap;

	// =======================================================================
	// Counters. See THE LAP-NUMBER CONVENTION above before using any of them.
	// =======================================================================

	/** Laps that ended at the line, valid or not. This is the "laps" a results screen shows. */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Result", meta = (ClampMin = "0"))
	int32 LapsCompleted = 0;

	/** Laps that ended clean. This is the number a leaderboard may publish. */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Result", meta = (ClampMin = "0"))
	int32 ValidLapsCompleted = 0;

	/** 1-based ordinal of the lap in progress when the session finished; 0 if the line was never crossed. */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Result", meta = (ClampMin = "0"))
	int32 CurrentLapNumberAtFinish = 0;

	/** How many sectors the TRACK authored. The key to reading an empty split array; see above. */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Result", meta = (ClampMin = "0"))
	int32 TrackSectorCount = 0;

	// =======================================================================
	// Session identity and freeze state
	// =======================================================================

	/**
	 * URaceStateMachine::GetSessionId() at the freeze.
	 *
	 * Carried so a consumer holding a result across a restart can tell "this describes the
	 * run I am looking at" from "this is the previous run's numbers", without holding a
	 * reference to the state machine. The same reason FOnRaceStateChanged carries it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Result")
	int32 SessionId = 0;

	/**
	 * True once this result has been frozen. A default-constructed result is NOT frozen,
	 * and every field in it is meaningless -- check this before reading anything.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Result")
	bool bFrozen = false;

	/**
	 * Was the track validated (through ATrackDefinitionActor's cached validity, TRACK-001
	 * M7) at the moment this result was frozen?
	 *
	 * Recorded on the result rather than re-asked later, because "was this track
	 * publishable WHEN THE RUN HAPPENED" is the question, and an editor edit after the
	 * fact must not retroactively make a bad run good or a good run bad.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Result")
	bool bTrackValidated = false;

	/** Why the track failed validation, empty when it validated. Diagnostics, not display. */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Result")
	FString TrackValidationReason;

	/**
	 * Did the authoritative race clock report a fault during this session
	 * (URaceStateMachine::HasRaceClockFault(), RACE-001 finding M4)?
	 *
	 * Kept as its own flag as well as being folded into Validity, because
	 * ERacingRunValidity has no timing-fault enumerator -- a refused clock maps onto the
	 * coarse InvalidIncomplete, and this is what distinguishes "the driver abandoned" from
	 * "the clock never ran". Adding an enumerator to a CORE-002 vocabulary that UI/,
	 * results and any future leaderboard read, for a case unreachable with the shipped
	 * platform time source, was judged the worse trade -- the same call RACE-002 made and
	 * recorded.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Result")
	bool bRaceClockFaulted = false;

	// =======================================================================
	// Reads
	// =======================================================================

	/** The overall outcome of the run. See Version.Validity for why it lives there. */
	ERacingRunValidity GetValidity() const { return Version.Validity; }

	/** True when a valid lap was set and the run is therefore worth comparing. */
	bool HasValidLap() const { return BestLap.LapNumber > 0 && ValidLapsCompleted > 0; }

	/**
	 * May this result be SUBMITTED to a leaderboard or backend?
	 *
	 * Docs/03-TrackRaceUI.md's functional-test list, verbatim: "result submission rejects
	 * invalid build/track/tune metadata". This is that rejection, and it is deliberately
	 * STRICTER than "may this run be shown to the player":
	 *
	 *   - the result must be frozen. An unfrozen result is a default-constructed struct;
	 *   - the track must have validated at freeze time (bTrackValidated). This is what
	 *     TRACK-001 M7's cache is FOR -- a track that bakes but does not validate is
	 *     unpublishable, and TRACK-002 M4 means a gate-bake failure is one of the ways;
	 *   - the race clock must not have faulted (bRaceClockFaulted). Note this is a
	 *     SEPARATE condition from validity, not a duplicate of it: an ordinarily invalid
	 *     run (a shortcut, a reverse crossing, a reset) has REAL times and a void verdict,
	 *     and a leaderboard may legitimately record it struck through. A clock-faulted run
	 *     has no times at all -- every duration on it is 0.000 -- so there is nothing to
	 *     publish. CORE-002's IsPublishable() cannot make that distinction, because it sees
	 *     only that InvalidIncomplete is a terminal validity;
	 *   - FRacingSimVersionStamp::IsPublishable() must hold, which refuses a
	 *     NON-AUTHORITATIVE build ID (every Derived one, i.e. every developer machine),
	 *     an unpopulated track/car-tune/ruleset version, PhysicsPolicyVersion == 0, an
	 *     Unknown input device, and a non-terminal validity.
	 *
	 * A DEVELOPER RESULT IS STILL A RESULT. This function refusing does not mean the run
	 * did not happen or must not be displayed -- FRacingSimBuildId's own header requires
	 * non-authoritative results to be "labelled as such wherever they are shown". Starting
	 * a session has a different, weaker bar (URaceResultRecorder::CanStartSession), or no
	 * one could race on a developer build at all.
	 *
	 * @param OutReason optional; set to the first failing condition, cleared on success.
	 */
	bool IsSubmittable(FString* OutReason = nullptr) const;

	/**
	 * Compose the URL query string a submission would carry, or refuse.
	 *
	 * TWO THINGS ARE LOAD-BEARING HERE.
	 *
	 * 1. IT REFUSES STRUCTURALLY. On a result that fails IsSubmittable(), OutQuery is left
	 *    EMPTY and this returns false. A version that returned a query string plus an
	 *    advisory bool would be one ignored return value away from submitting a developer
	 *    build's lap to a leaderboard, which is the exact failure the functional test
	 *    names. There is no way to get the string without passing the check.
	 *
	 * 2. EVERY VALUE IS PERCENT-ENCODED (CORE-003 finding C3-4). A build ID legally
	 *    contains '+', which decodes to a SPACE in an
	 *    application/x-www-form-urlencoded query string -- so an unencoded ID arrives as a
	 *    different, colliding string. Track and ruleset identities contain '@' and '#',
	 *    which are worse: '#' truncates the URL at a fragment boundary. Values go through
	 *    FRacingSimBuildId::ToUrlQueryValue()/RacingSim::Url::PercentEncodeQueryValue().
	 *
	 * The key set is deliberately minimal and stable: build, track, car, ruleset, engine,
	 * physics, assists, input, validity, laps, best, final. No backend consumes it yet
	 * (STREAM-001+ owns the network boundary), so this is the format's definition rather
	 * than a client for one -- which is why it is testable today and why the encoding rule
	 * is asserted rather than assumed.
	 *
	 * @param OutQuery  the query string WITHOUT a leading '?' or '&', empty on refusal.
	 * @param OutReason why it was refused, empty on success.
	 */
	bool MakeSubmissionQueryString(FString& OutQuery, FString& OutReason) const;

	/** Single-line form for logs and evidence. Not localised, not a display path. */
	FString ToString() const;
};

/**
 * Freezes the result at Finished and performs the complete reset at Restart.
 *
 * ===========================================================================
 * Why a plain UObject (again), and why not ARaceDirector
 * ===========================================================================
 *
 * Docs/01-Architecture.md names ARaceDirector as the eventual session orchestrator. This
 * is one more thing ARaceDirector will CONTAIN, alongside URaceStateMachine and the
 * URaceLapTrackers -- not a replacement for it. The reasoning is URaceStateMachine's,
 * unchanged and load-bearing: Docs/Environment.md records that a SmokeFilter test in this
 * project cannot construct a non-template Actor at all, so an AActor here would make
 * "does a restart leave stale state" testable only through a gate that cannot complete on
 * the reference machine. A UObject needs no world.
 *
 * The cost is the same one and must be honoured the same way: A UOBJECT IS NOT GC-ROOTED
 * BY ITS OUTER. Whoever calls Create() must hold the result in a UPROPERTY. This object
 * in turn holds its registered trackers in a UPROPERTY array, which discharges that
 * obligation for them.
 *
 * ===========================================================================
 * ONE delegate binding, for the object's whole lifetime
 * ===========================================================================
 *
 * URaceLapTracker deliberately subscribes to nothing and watches the session id instead,
 * and its header explains why: a lambda bound to a native multicast delegate must be
 * unbound by hand, and a missed unbind is the stale-delegate failure
 * `.claude/rules/unreal-source.md` names. This class cannot take that route, because
 * "freeze the result ONCE, at the Finished transition" is a statement about an INSTANT,
 * and a poll answers a frame later or -- if the owner stops polling between Finished and
 * Restart -- never. A result that silently fails to freeze is worse than a delegate.
 *
 * So it binds, exactly once, and the binding is handled three ways rather than one:
 *
 *   1. AddUObject, not a lambda. A UObject-bound delegate is weak: if this object is
 *      collected without anyone unbinding, the invocation list drops it rather than
 *      calling into freed memory. That is the failure mode that cannot be tested for and
 *      therefore must not be possible.
 *   2. The handle is stored and removed explicitly in DetachFromStateMachine(), which
 *      BeginDestroy() calls. Idempotent.
 *   3. THE BINDING IS PER-OBJECT, NOT PER-SESSION. Nothing binds at Countdown and unbinds
 *      at Finished, so a restart cannot accumulate a second subscription. That is the
 *      property that actually matters for "no stale delegate survives a restart", and
 *      GetStateChangeNotificationCount() exists so it can be asserted from outside:
 *      after N restarts, one further transition must raise the count by exactly one.
 *
 * There are no timers and no input bindings anywhere in this class -- the clock is
 * pull-based (RaceClock.h) and input belongs to Vehicle/. That is the whole of the
 * "no stale timer/input binding" requirement, discharged by not having any.
 *
 * ===========================================================================
 * RACE-001 finding L3 is NOT relied upon
 * ===========================================================================
 *
 * `Restart` requested while already in PreRace is Redundant: no transition is applied, no
 * broadcast happens, and the session id does NOT bump. This class must therefore not use
 * "the session id changed" as its restart signal, and it does not -- ClearForNewSession()
 * is driven by the transition INTO PreRace (the only way in) and is additionally exposed
 * publicly and made idempotent, so an owner can force it. The redundant case needs no
 * clearing by definition: to be in PreRace is already to have been cleared.
 */
UCLASS(BlueprintType)
class RACINGSIM_API URaceResultRecorder : public UObject
{
	GENERATED_BODY()

public:
	// =======================================================================
	// Construction
	// =======================================================================

	/**
	 * Create a recorder bound to a session's state machine.
	 *
	 * @param Owner          outer for the new object. The caller MUST also keep it alive
	 *                       in a UPROPERTY.
	 * @param InStateMachine the session's authoritative state and clock. Required: the
	 *                       final time and the freeze instant both come from it, and a
	 *                       recorder with no state machine could only guess at both.
	 * @return nullptr if either argument is null.
	 */
	static URaceResultRecorder* Create(UObject* Owner, URaceStateMachine* InStateMachine);

	// =======================================================================
	// Wiring
	// =======================================================================

	/**
	 * Register a tracker. The FIRST one registered is the PRIMARY: its laps are what the
	 * frozen result describes. Every registered tracker is reset by ClearForNewSession().
	 *
	 * Held in a UPROPERTY array, which is what keeps a registered tracker alive -- see
	 * URaceLapTracker::Create's warning that a UObject is not GC-rooted by its outer.
	 *
	 * @return false for null, or for a tracker already registered (which is a no-op, not
	 *         an error -- a double-register must not produce a second reset call).
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Result")
	bool RegisterLapTracker(URaceLapTracker* Tracker);

	/** Remove a tracker. It is no longer reset by a restart and no longer kept alive by this object. */
	UFUNCTION(BlueprintCallable, Category = "Race|Result")
	bool UnregisterLapTracker(URaceLapTracker* Tracker);

	UFUNCTION(BlueprintPure, Category = "Race|Result")
	int32 GetNumLapTrackers() const { return LapTrackers.Num(); }

	/** The tracker the frozen result describes: the first one registered. Null when none is. */
	UFUNCTION(BlueprintPure, Category = "Race|Result")
	URaceLapTracker* GetPrimaryLapTracker() const;

	/**
	 * The track this session is run on. Optional but strongly preferred: with one, the
	 * track version, sector count and CACHED VALIDITY are read from it at session start
	 * and at the freeze, so a result can never claim a track it was not validated against.
	 *
	 * Read through ATrackDefinitionActor::GetCachedValidation(), never through a live
	 * Validate() -- that is the point of TRACK-001 M7's cache and this is its first
	 * consumer.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Result")
	void SetTrack(ATrackDefinitionActor* InTrack);

	/**
	 * Supply track metadata WITHOUT an ATrackDefinitionActor.
	 *
	 * The level-free path, and it is not only for tests: a future replay, a server-side
	 * validation pass, or a track sourced from something other than a placed actor all
	 * need to state the same four facts. It also happens to be the only path this
	 * project's automation gate can exercise for a non-template actor, which is recorded
	 * rather than hidden (see TrackDefinitionActorSpec's header for why).
	 *
	 * Overwrites anything SetTrack() would have read. Calling both is legal; the actor
	 * wins, because it is re-read at every freeze and this snapshot is not.
	 */
	void SetTrackSnapshot(
		const FRacingContentVersion& InTrackVersion,
		int32 InSectorCount,
		bool bInValidated,
		const FString& InValidationReason);

	/**
	 * The car spec / tune version, which VEH-003 owns and this ticket cannot compute.
	 *
	 * Left unpopulated by default ON PURPOSE, exactly as FRacingSimVersionStamp::
	 * MakeCurrent() leaves it: an unpopulated field is honest and IsSubmittable() refuses
	 * it, whereas a plausible default would produce a result that passes every check while
	 * describing a car nobody drove.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Result")
	void SetCarSpecVersion(const FRacingContentVersion& InCarSpecVersion);

	/** The input device the run was driven with. VEH-001 owns this; Unknown blocks submission. */
	UFUNCTION(BlueprintCallable, Category = "Race|Result")
	void SetInputDeviceType(ERacingInputDeviceType InInputDeviceType);

	// =======================================================================
	// Session lifecycle
	// =======================================================================

	/**
	 * May a session start? Cheap, and deliberately WEAKER than IsSubmittable().
	 *
	 * Requires a validated track and at least one configured tracker. Does NOT require an
	 * authoritative build ID, a populated car version or a known input device -- a
	 * developer build must be able to race, it simply may not publish. Conflating the two
	 * bars would make the project untestable on the machine that develops it.
	 *
	 * Uses the track's CACHED validity, so this is a hash comparison rather than a full
	 * Validate() -- which is what TRACK-001 M7 asked for: "a race director can cheaply
	 * refuse to start a session on an invalid track".
	 */
	bool CanStartSession(FString& OutReason) const;

	/**
	 * Assemble and freeze the result. IDEMPOTENT: the second and later calls do nothing
	 * and return false.
	 *
	 * Called automatically on the transition into ERaceState::Finished. Exposed publicly
	 * as well so an owner without a delegate -- a test, a headless validation pass, a
	 * future ARaceDirector driving this by hand -- can produce the same result, and so
	 * that the freeze-once rule is enforced in ONE place rather than once per caller.
	 *
	 * @return true only if THIS call froze the result.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Result")
	bool FreezeResult();

	/**
	 * The complete reset Docs/03-TrackRaceUI.md's Restart state requires. IDEMPOTENT.
	 *
	 * Calls ResetForNewSession() on EVERY registered tracker, drops the frozen result so a
	 * HUD reading it during the new PreRace/Countdown gets "no result" rather than the
	 * previous run's numbers, and prunes any tracker that has been collected so no stale
	 * reference survives. Track and car metadata are deliberately KEPT: a restart re-races
	 * the same car on the same circuit, and re-deriving that would be the reset throwing
	 * away configuration rather than state.
	 *
	 * Called automatically on the transition into ERaceState::PreRace, which is reachable
	 * only via Restart.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Result")
	void ClearForNewSession();

	// =======================================================================
	// Reads
	// =======================================================================

	UFUNCTION(BlueprintPure, Category = "Race|Result")
	bool HasFrozenResult() const { return Result.bFrozen; }

	/**
	 * The frozen result. Meaningless unless HasFrozenResult(); a default-constructed
	 * FRacingRaceResult is returned before the freeze and after a restart.
	 *
	 * By const reference: it carries two FRacingLapTiming, each owning a TArray, so
	 * returning by value would heap-allocate on every read. CLAUDE.md forbids that on a
	 * path a HUD can touch.
	 */
	const FRacingRaceResult& GetFrozenResult() const { return Result; }

	/**
	 * How many OnRaceStateChanged notifications this object has received.
	 *
	 * For automation, on the same reasoning ATrackDefinitionActor::GetBakeAttemptCount()
	 * records. "No stale delegate binding survives a restart" is otherwise unassertable
	 * from outside a native multicast delegate: bind twice and every notification arrives
	 * twice, which nothing else observes. After N restarts, one further applied transition
	 * must raise this by exactly one.
	 */
	UFUNCTION(BlueprintPure, Category = "Race|Result")
	int32 GetStateChangeNotificationCount() const { return StateChangeNotificationCount; }

	/** True while this object is subscribed to its state machine. */
	UFUNCTION(BlueprintPure, Category = "Race|Result")
	bool IsObservingStateMachine() const { return StateChangedHandle.IsValid(); }

	/** Unsubscribe. Idempotent. Called by BeginDestroy(); call it early to hand a session over. */
	UFUNCTION(BlueprintCallable, Category = "Race|Result")
	void DetachFromStateMachine();

	//~ Begin UObject interface
	virtual void BeginDestroy() override;
	//~ End UObject interface

private:
	/** The one delegate handler. Freezes at Finished, clears at PreRace, ignores the rest. */
	void HandleRaceStateChanged(ERaceState OldState, ERaceState NewState, int32 InSessionId);

	/** Refresh TrackVersion/TrackSectorCount/validity from the track actor, if one is set. */
	void RefreshTrackSnapshot();

	/** Build the result from the authoritative sources. Assembles; derives nothing. */
	FRacingRaceResult AssembleResult() const;

	/** Coarse-grain the tracker's per-lap answers into ONE published ERacingRunValidity. */
	ERacingRunValidity ResolveOverallValidity(const URaceLapTracker* Primary) const;

	/** The session's authoritative state and clock. Never null after Create(). */
	UPROPERTY(VisibleAnywhere, Category = "Race|Result")
	TObjectPtr<URaceStateMachine> StateMachine;

	/** Registered trackers, in registration order. [0] is primary. UPROPERTY: this is their GC root. */
	UPROPERTY(VisibleAnywhere, Category = "Race|Result")
	TArray<TObjectPtr<URaceLapTracker>> LapTrackers;

	/** Optional. Held as a UPROPERTY so a referenced track cannot be collected mid-session. */
	UPROPERTY(VisibleAnywhere, Category = "Race|Result")
	TObjectPtr<ATrackDefinitionActor> Track;

	/** Track identity as it will be written onto a result. */
	UPROPERTY(VisibleAnywhere, Category = "Race|Result")
	FRacingContentVersion TrackVersion;

	UPROPERTY(VisibleAnywhere, Category = "Race|Result")
	int32 TrackSectorCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Race|Result")
	bool bTrackValidated = false;

	UPROPERTY(VisibleAnywhere, Category = "Race|Result")
	FString TrackValidationReason;

	UPROPERTY(VisibleAnywhere, Category = "Race|Result")
	FRacingContentVersion CarSpecVersion;

	UPROPERTY(VisibleAnywhere, Category = "Race|Result")
	ERacingInputDeviceType InputDeviceType = ERacingInputDeviceType::Unknown;

	/** The frozen result. Cleared by ClearForNewSession(). */
	UPROPERTY(VisibleAnywhere, Category = "Race|Result")
	FRacingRaceResult Result;

	/** See GetStateChangeNotificationCount(). */
	UPROPERTY(VisibleAnywhere, Category = "Race|Result")
	int32 StateChangeNotificationCount = 0;

	/**
	 * The one subscription. Not a UPROPERTY -- FDelegateHandle is a plain id, holds no
	 * object reference and is not reflected.
	 */
	FDelegateHandle StateChangedHandle;
};
