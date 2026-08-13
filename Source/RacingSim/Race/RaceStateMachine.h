// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/RacingSimTypes.h"
#include "Race/RaceClock.h"
#include "RaceStateMachine.generated.h"

class URaceRulesetDataAsset;

/**
 * RACE-001: the authoritative race state machine and clock owner.
 *
 * ===========================================================================
 * Why a plain UObject, and not an Actor, a GameState or a Subsystem
 * ===========================================================================
 *
 * Docs/01-Architecture.md names ARaceDirector as the eventual owner of "the
 * authoritative state machine and session orchestration". That is still the plan;
 * this class is what ARaceDirector will *contain*, not a replacement for it. The
 * split is deliberate and the reasons are, in order of weight:
 *
 *   1. TESTABILITY DECIDES IT. Gate B wants transition and timing correctness
 *      proven, and the project's automation gate is `RunFilter Smoke` under
 *      -nullrhi in a commandlet (Docs/Environment.md). An AActor or AGameStateBase
 *      needs a UWorld to spawn into; a UWorldSubsystem needs a world to exist at
 *      all; a UGameInstanceSubsystem needs a game instance. Every one of those
 *      turns "assert the transition graph" into "stand up a world first", which is
 *      slower, flakier, and quietly makes the failure mode of the *test harness*
 *      indistinguishable from a failure of the *rule*. NewObject into the transient
 *      package needs nothing, so a red test means the rule broke.
 *
 *   2. IT HAS NOTHING TO PLACE, DRAW OR REPLICATE. No transform, no components, no
 *      visual. An Actor would be paying for a scene presence it never uses.
 *
 *   3. IT MUST NOT OWN ITS OWN LIFETIME. A subsystem's lifetime is the engine's --
 *      it survives level transitions and is a singleton per world/instance. Race
 *      authority should die with the session that created it, and the restart path
 *      should be able to construct a fresh one if it ever needs to. Ownership by
 *      ARaceDirector (or by the game mode in the interim) says that; a subsystem
 *      says the opposite.
 *
 *   4. NO TICK. The clock subtracts timestamps (see RaceClock.h), so this class has
 *      no Tick and no FTimerHandle. AActor's whole tick/lifecycle apparatus would be
 *      dead weight, and -- more usefully -- there is no timer that can fire after a
 *      restart, no delegate the engine bound on our behalf, and nothing to
 *      invalidate on teardown. The stale-timer class of bug is designed out rather
 *      than defended against.
 *
 * The cost, stated plainly: a UObject is not garbage-collection-rooted by itself.
 * The owner MUST hold it in a UPROPERTY or it will be collected mid-race. That is
 * the trade for the four points above and it is on ARaceDirector to honour it.
 *
 * ===========================================================================
 * Authority
 * ===========================================================================
 *
 * There is exactly one of these per session and it lives on the server (CLAUDE.md:
 * "Use a monotonic server-side time source for lap timing"). Nothing client-side
 * interpolates, predicts or guesses race time: the HUD reads this object, and under
 * Pixel Streaming the HUD *is* on the server, which is why this project can afford
 * to be this strict. Create() refuses to build one on a net client; the check is at
 * construction, not per call, so the read path stays free of world queries.
 *
 * ===========================================================================
 * What this class deliberately does not know
 * ===========================================================================
 *
 * Checkpoints, laps, sectors, splines, tracks, vehicles, positions. RACE-001 is
 * track-agnostic by ticket. RACE-002 attaches lap validation to this clock and this
 * state; TRACK-002 owns gates. If a future edit makes this file include a track
 * header, the layering has gone wrong.
 */

/**
 * A request to change state. Named for the *event*, not for the destination:
 * Restart is one transition with one meaning, and it happens to land in PreRace
 * from five different places. An enum of destinations would have made
 * "Racing -> PreRace" and "Results -> PreRace" look like the same edge when only
 * one of them is a mid-race abandon.
 */
UENUM(BlueprintType)
enum class ERaceTransition : uint8
{
	/** PreRace -> Countdown. Lights out sequence begins; drive input stays locked. */
	BeginCountdown	UMETA(DisplayName = "Begin countdown"),

	/** Countdown -> Racing. Green. Drive input releases and the race clock starts. */
	StartRace		UMETA(DisplayName = "Start race"),

	/** Racing -> Finished. Result freezes here, once. */
	FinishRace		UMETA(DisplayName = "Finish race"),

	/** Finished -> Results. Presentation only; the clock is already frozen. */
	ShowResults		UMETA(DisplayName = "Show results"),

	/**
	 * Anything -> PreRace, with the clocks re-zeroed and the session id bumped.
	 *
	 * Legal from every state on purpose. A restart that were only legal from
	 * Results would leave a player stuck in Racing after a failure, and every caller
	 * would grow its own ad-hoc reset path -- which is precisely how partial state
	 * survives a "restart". One edge, one implementation, one place to audit.
	 */
	Restart			UMETA(DisplayName = "Restart")
};

/**
 * Outcome of a transition request.
 *
 * Applied and Redundant are BOTH successes, and keeping them apart is the whole of
 * Gate B's idempotency clause. "Calling the same transition twice produces no
 * additional effect" is not "the second call is an error": callers legitimately
 * race (a UI button and an auto-advance poll can both fire StartRace in the same
 * frame). What must not happen is a second entry action or a second broadcast.
 */
UENUM(BlueprintType)
enum class ERaceTransitionResult : uint8
{
	/** State changed. Entry actions ran exactly once. Observers were notified once. */
	Applied					UMETA(DisplayName = "Applied"),

	/**
	 * The transition's post-condition already holds -- the machine is already in the
	 * state this transition exists to reach. Nothing ran, nothing broadcast, no
	 * error, no log above Verbose. This is what idempotency looks like to a caller.
	 *
	 * Redundancy is decided by DESTINATION, not by the edge table: BeginCountdown
	 * while already in Countdown is Redundant even though (Countdown, BeginCountdown)
	 * is not an edge of the graph. The alternative -- rejecting it as illegal --
	 * would make an ordinary double-click on a UI button emit a warning, and a
	 * warning that fires during correct operation is a warning everyone learns to
	 * ignore. Illegality is reserved for requests that ask for something that cannot
	 * happen from here, such as FinishRace before the race started.
	 */
	Redundant				UMETA(DisplayName = "Redundant (post-condition already holds)"),

	/** Not an edge in the authored graph, e.g. FinishRace from PreRace. Logged, state unchanged. */
	RejectedIllegal			UMETA(DisplayName = "Rejected (illegal transition)"),

	/** The enum value was outside the authored range -- a cast from a bad integer. Logged, state unchanged. */
	RejectedOutOfRange		UMETA(DisplayName = "Rejected (value out of range)"),

	/**
	 * A transition was requested from inside an OnRaceStateChanged broadcast.
	 * Rejected rather than queued: re-entrant transitions produce an observer-order-
	 * dependent final state, which is the opposite of the determinism Gate B asks
	 * for. See the note on OnRaceStateChanged.
	 */
	RejectedReentrant		UMETA(DisplayName = "Rejected (re-entrant)")
};

/**
 * Fired once per *applied* transition, after the new state and both clocks are
 * committed, so an observer always reads a consistent object.
 *
 * Never fired for Redundant or Rejected results. That is the guarantee consumers
 * actually need: a listener that counts finishes must count one finish however many
 * times FinishRace() is called.
 *
 * NATIVE multicast, not DYNAMIC/BlueprintAssignable, and this is a real trade:
 *   + C++ observers can bind lambdas, which is what makes broadcast-count and
 *     re-entrancy testable at all without inventing a UObject listener class;
 *   + no reflection dispatch on the notify path;
 *   - UMG cannot bind it directly. That is acceptable because Docs/01-Architecture.md
 *     already mandates "a small explicit view-model layer" between the widgets and
 *     race truth, and UI-001 owns that layer. If UI-001 finds it needs a
 *     BlueprintAssignable event, it belongs on the view model -- not here, where it
 *     would let a widget subscribe directly to race truth and bypass the view model
 *     the architecture doc requires.
 *
 * SessionId is included so an observer can tell "the race restarted" apart from
 * "time went backwards" without holding a reference to this object.
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnRaceStateChanged, ERaceState /*OldState*/, ERaceState /*NewState*/, int32 /*SessionId*/);

UCLASS(BlueprintType)
class RACINGSIM_API URaceStateMachine : public UObject
{
	GENERATED_BODY()

public:
	// =======================================================================
	// Construction
	// =======================================================================

	/**
	 * Create a state machine owned by Owner.
	 *
	 * @param Owner    Outer for the new object. The caller MUST also keep it alive
	 *                 in a UPROPERTY -- a UObject is not GC-rooted by its outer.
	 * @param InRuleset Optional. With one, Countdown expires automatically after
	 *                 URaceRulesetDataAsset::CountdownSeconds and PollAutoTransitions()
	 *                 advances to Racing. Without one, Countdown is manual and only
	 *                 an explicit StartRace() leaves it -- see HasAutomaticCountdown().
	 * @return nullptr if Owner is null or resolves to a net client (authority check).
	 */
	static URaceStateMachine* Create(UObject* Owner, URaceRulesetDataAsset* InRuleset = nullptr);

	/**
	 * Test/dedicated seam: as Create(), but with a substituted monotonic time source.
	 *
	 * Exists so clock behaviour can be driven deterministically -- a dropped frame,
	 * a 4-second hitch, a source that steps backwards -- rather than by sleeping and
	 * hoping. Production code calls Create().
	 */
	static URaceStateMachine* CreateWithTimeSource(UObject* Owner, URaceRulesetDataAsset* InRuleset, FRaceTimeSourceFn InTimeSource);

	/**
	 * True when this context may own race truth: not a net client.
	 *
	 * A null world (commandlet, automation, CDO) counts as authoritative -- there is
	 * no client to be, and failing closed would make every automation test unable to
	 * construct the object it is testing.
	 */
	static bool HasRaceAuthority(const UObject* Context);

	// =======================================================================
	// The authored transition graph
	// =======================================================================

	/**
	 * THE single source of truth for what is legal. Static and side-effect free, so
	 * the graph can be tested exhaustively without an instance, and so no transition
	 * path can grow a second opinion about legality.
	 *
	 * @param OutTo destination state; only written when the function returns true.
	 * @return true if (From, Transition) is an edge of the authored graph.
	 */
	static bool GetTransitionTarget(ERaceState From, ERaceTransition Transition, ERaceState& OutTo);

	/**
	 * The state a transition exists to reach, regardless of where it is requested
	 * from. Total over the five declared transitions.
	 *
	 * This is what makes idempotency uniform: RequestTransition returns Redundant
	 * whenever the machine is already here. See ERaceTransitionResult::Redundant.
	 *
	 * @param OutDestination written only when the function returns true.
	 * @return false if Transition is not a declared value.
	 */
	static bool GetTransitionDestination(ERaceTransition Transition, ERaceState& OutDestination);

	/** Every declared ERaceState, in lifecycle order. Kept in sync with the enum by an automation test. */
	static TConstArrayView<ERaceState> AllStates();

	/** Every declared ERaceTransition. Kept in sync with the enum by an automation test. */
	static TConstArrayView<ERaceTransition> AllTransitions();

	// =======================================================================
	// Transitions
	// =======================================================================

	/**
	 * Request a transition. The only way this object's state ever changes.
	 *
	 * Allocation-free on the accepted and redundant paths (an enum compare, a table
	 * lookup and two doubles). The rejected path formats a log string, which is
	 * acceptable because a rejection is a bug being reported, not a per-frame event.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|State")
	ERaceTransitionResult RequestTransition(ERaceTransition Transition);

	UFUNCTION(BlueprintCallable, Category = "Race|State")
	ERaceTransitionResult BeginCountdown() { return RequestTransition(ERaceTransition::BeginCountdown); }

	UFUNCTION(BlueprintCallable, Category = "Race|State")
	ERaceTransitionResult StartRace() { return RequestTransition(ERaceTransition::StartRace); }

	UFUNCTION(BlueprintCallable, Category = "Race|State")
	ERaceTransitionResult FinishRace() { return RequestTransition(ERaceTransition::FinishRace); }

	UFUNCTION(BlueprintCallable, Category = "Race|State")
	ERaceTransitionResult ShowResults() { return RequestTransition(ERaceTransition::ShowResults); }

	/** Re-zeroes both clocks, bumps the session id and returns to PreRace. Legal from every state. */
	UFUNCTION(BlueprintCallable, Category = "Race|State")
	ERaceTransitionResult Restart() { return RequestTransition(ERaceTransition::Restart); }

	/**
	 * Advance any transition the clock alone can justify -- today, Countdown ->
	 * Racing once the ruleset's countdown has elapsed.
	 *
	 * Call from the owner's Tick. Cheap and honest about it: one enum compare, one
	 * null check and one double subtract in the common case, no allocation, no actor
	 * search, no asset load. It is a poll rather than an FTimerManager timer on
	 * purpose -- a timer set at Countdown entry outlives a restart, and firing a
	 * stale "go green" into a re-zeroed session is exactly the reset-awards-progress
	 * failure Gate B names.
	 *
	 * @return true if a transition was applied by this call.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|State")
	bool PollAutoTransitions();

	// =======================================================================
	// Reads
	// =======================================================================

	UFUNCTION(BlueprintPure, Category = "Race|State")
	ERaceState GetRaceState() const { return CurrentState; }

	/**
	 * Increments on every applied Restart. Starts at 0.
	 *
	 * Anything caching race data across frames should carry this and discard its
	 * cache when it changes -- that is how a consumer distinguishes a legitimate
	 * re-zero from a clock fault, and how a queued async result from the previous
	 * run is identified as stale instead of being applied to the new one.
	 */
	UFUNCTION(BlueprintPure, Category = "Race|State")
	int32 GetSessionId() const { return SessionId; }

	/**
	 * Race time in SECONDS. 0.0 until Racing is entered; frozen from Finished on.
	 * Never decreases within a session. Formatting belongs to UI/.
	 *
	 * Not const: it advances the clock's monotonic ratchet. Game thread only.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Timing")
	double GetRaceElapsedSeconds();

	/** The last value GetRaceElapsedSeconds() returned, without taking a new reading. */
	UFUNCTION(BlueprintPure, Category = "Race|Timing")
	double PeekRaceElapsedSeconds() const { return RaceClock.Peek(); }

	UFUNCTION(BlueprintPure, Category = "Race|Timing")
	bool IsRaceClockRunning() const { return RaceClock.IsRunning(); }

	/** True when a ruleset is configured and Countdown therefore expires on its own. */
	UFUNCTION(BlueprintPure, Category = "Race|Timing")
	bool HasAutomaticCountdown() const { return Ruleset != nullptr; }

	/**
	 * Seconds left on the countdown, clamped to >= 0.
	 *
	 * Returns 0.0 outside Countdown, and 0.0 when there is no ruleset -- in the
	 * latter case the countdown is manual, so pair any UI use with
	 * HasAutomaticCountdown() rather than reading 0.0 as "go". A sentinel like -1.0
	 * was rejected: a magic number that means "no answer" gets displayed as a magic
	 * number the first time somebody forgets to check it.
	 *
	 * Not const: shares the monotonic ratchet. Game thread only.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Timing")
	double GetCountdownRemainingSeconds();

	/**
	 * Returns a non-const pointer only because UnrealHeaderTool rejects a
	 * `const UObject*` UFUNCTION return type. The ruleset is read-only to every
	 * consumer of this class; nothing here mutates it.
	 */
	UFUNCTION(BlueprintPure, Category = "Race|Ruleset")
	URaceRulesetDataAsset* GetRuleset() const { return Ruleset; }

	// =======================================================================
	// Observation
	// =======================================================================

	/** See FOnRaceStateChanged. Fired once per applied transition, never for redundant or rejected ones. */
	FOnRaceStateChanged OnRaceStateChanged;

	//~ Begin UObject interface
	virtual void BeginDestroy() override;
	//~ End UObject interface

private:
	/** Runs entry actions for NewState and broadcasts. Assumes legality is already decided. */
	void CommitTransition(ERaceState NewState, ERaceTransition Transition, double NowSeconds);

	/** Current monotonic reading. One call per transition so both clocks are stamped with the same instant. */
	double Now() const;

	UPROPERTY(VisibleAnywhere, Category = "Race|State")
	ERaceState CurrentState = ERaceState::PreRace;

	UPROPERTY(VisibleAnywhere, Category = "Race|State")
	int32 SessionId = 0;

	/** Optional. Held as a UPROPERTY so an assigned asset cannot be collected mid-session. */
	UPROPERTY(VisibleAnywhere, Category = "Race|Ruleset")
	TObjectPtr<URaceRulesetDataAsset> Ruleset;

	/** Race time. Runs only in Racing; frozen once on entry to Finished. */
	FRaceClock RaceClock;

	/**
	 * Countdown time. A second clock rather than a reused one, because the race
	 * clock's contract is "0.0 until green" and borrowing it for the countdown would
	 * make a HUD reading race time during Countdown display a lap time that had not
	 * started.
	 */
	FRaceClock CountdownClock;

	/** Injected at construction. Never null; Create() installs the platform source. */
	FRaceTimeSourceFn TimeSource = nullptr;

	/** Re-entrancy guard for OnRaceStateChanged. See ERaceTransitionResult::RejectedReentrant. */
	bool bIsBroadcasting = false;
};
