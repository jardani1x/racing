// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/RacingSimTypes.h"
#include "Core/RacingTelemetry.h"
#include "Race/TrackCheckpointGate.h"
#include "RaceLapTracker.generated.h"

class ATrackDefinitionActor;
class URaceRulesetDataAsset;
class URaceStateMachine;

/**
 * RACE-002: the ORDER half of lap validation, plus lap/sector timing and progress.
 *
 * ===========================================================================
 * What this class is, in one sentence
 * ===========================================================================
 *
 * It turns a stream of per-gate crossing results (TRACK-002) and a monotonic race
 * clock (RACE-001) into "is this lap valid, how long did it take, and how far round
 * is the car" -- and it is the ONLY place in the project permitted to decide that a
 * lap happened.
 *
 * `.claude/rules/race-tests.md`: "Checkpoint order plus crossing direction authorizes
 * laps; spline distance alone never does." TRACK-002 owns the crossing-direction half
 * (FRacingCheckpointGateSet::EvaluateCrossing). This owns the ORDER half. Neither one
 * alone authorises anything.
 *
 * ===========================================================================
 * Why a plain UObject, again
 * ===========================================================================
 *
 * Same reasoning as URaceStateMachine, which its header sets out at length:
 * testability decides it. The project's automation gate is a commandlet under
 * -nullrhi (Docs/Environment.md), and Docs/Environment.md further records that a
 * SmokeFilter test cannot construct a non-template Actor in this project AT ALL --
 * the typed-element registry is not initialised that early. So a UActorComponent
 * (which Docs/01-Architecture.md originally sketched as `URaceProgressComponent`)
 * would have made every lap-ordering rule testable only through the slower, crash-
 * prone Product gate, which cannot even complete on this machine (see
 * Scripts/Test/Run-AutomationFilter.ps1's header). A UObject with an injected track
 * snapshot needs no world, no actor and no level.
 *
 * The cost is the same one URaceStateMachine pays and it must be honoured the same
 * way: A UOBJECT IS NOT GC-ROOTED BY ITS OUTER. Whoever calls Create() must hold the
 * result in a UPROPERTY.
 *
 * ===========================================================================
 * No timers, no delegates, no tick
 * ===========================================================================
 *
 * `.claude/rules/unreal-source.md` requires delegates, timers and async callbacks to
 * be treated explicitly. The explicit treatment here is that there are none:
 *
 *   - time comes from URaceStateMachine::GetRaceElapsedSeconds(), which subtracts
 *     timestamps rather than accumulating deltas, so there is no timer to leak and
 *     nothing to fire into a restarted session;
 *   - this object subscribes to NOTHING. It does not bind OnRaceStateChanged, because
 *     a lambda bound to a native multicast delegate must be unbound by hand and a
 *     missed unbind is precisely the stale-delegate failure the rules name. Instead
 *     Advance() watches URaceStateMachine::GetSessionId() and re-seeds itself when it
 *     changes -- the pattern that delegate's own doc comment recommends;
 *   - nothing here is called per frame by the engine. The owner drives Advance().
 *
 * RACE-001's finding L3 (Restart from PreRace bumps no session id) is therefore not
 * load-bearing for this class: ResetForNewSession() is the authoritative clear and is
 * idempotent, and the session-id watch is a second, defensive line.
 *
 * ===========================================================================
 * Threading, and TRACK-002 L5
 * ===========================================================================
 *
 * Game thread only, like the state machine it reads. It holds a COPY of the baked
 * gate set taken once at configuration time, so per-step crossing evaluation never
 * calls ATrackDefinitionActor::GetCheckpointGates() -- whose lazy bake asserts
 * IsInGameThread(). That is TRACK-002's finding L5 closed by construction rather than
 * by comment: if lap evaluation ever moves onto a Chaos async/substep context, the
 * data it reads is already world-free and detached from the actor.
 *
 * The copy is also a correctness property, not just a threading one: a track re-baked
 * mid-session (an editor property change, a spline nudge) cannot retroactively change
 * which gates a lap in progress was required to take.
 *
 * ===========================================================================
 * Units
 * ===========================================================================
 *
 * Distances are Unreal centimetres and carry a `Cm` suffix, per CORE-002. Durations
 * are SECONDS as double, taken from the authoritative monotonic clock and never
 * formatted here -- Docs/03-TrackRaceUI.md: "Store raw duration with high precision;
 * format only in UI."
 */

/**
 * Why a lap was rejected. Deliberately finer-grained than Core's ERacingRunValidity.
 *
 * ERacingRunValidity (CORE-002) is the coarse, published outcome a result and a HUD
 * read. This is the diagnosis, and the ticket requires the difference: a skipped gate
 * must name THE GATE, and a reverse finish crossing must not be collapsed into the
 * same bucket as a shortcut. Two different driving mistakes reported by one string is
 * how a driver learns to distrust the invalidation.
 *
 * FRaceLapInvalidity::ToRunValidity() maps this onto the Core enum, so the mapping
 * lives in exactly one place.
 */
UENUM(BlueprintType)
enum class ERaceLapInvalidReason : uint8
{
	/** No fault recorded. A lap that closes with this reason is Valid. */
	None						UMETA(DisplayName = "None"),

	/**
	 * An ordered checkpoint gate was not crossed forward before the next one was, or
	 * before the finish line was reached. FRaceLapInvalidity names the missed gate.
	 */
	MissedCheckpoint			UMETA(DisplayName = "Missed checkpoint"),

	/**
	 * The start/finish gate was crossed against its legal direction.
	 *
	 * A DISTINCT reason rather than a flavour of MissedCheckpoint, per the ticket:
	 * driving backwards over the line and cutting the last corner are different acts,
	 * they are caught by different rules, and CLAUDE.md names reverse finish crossings
	 * on their own.
	 */
	ReverseFinishCrossing		UMETA(DisplayName = "Reverse finish crossing"),

	/**
	 * The vehicle was reset, teleported or respawned during the lap.
	 *
	 * Covers both the announced case (NotifyVehicleReset) and an unannounced position
	 * jump caught by Advance()'s implausible-step guard.
	 */
	VehicleReset				UMETA(DisplayName = "Vehicle reset"),

	/**
	 * The lap has no trustworthy duration: the authoritative clock refused to start
	 * (RACE-001 finding M4), or the lap closed without a complete set of sector splits.
	 *
	 * Reported rather than silently publishing a plausible-looking 0.000.
	 */
	TimingUnavailable			UMETA(DisplayName = "Timing unavailable")
};

/**
 * One lap's fault, with the gate that caused it when a gate caused it.
 *
 * FIRST FAULT WINS, and that is deliberate. A lap that is ruined by a reverse finish
 * crossing will usually then also miss gates, and reporting the second symptom would
 * bury the first cause. Which fault is reported must not depend on how much further
 * the car happened to drive afterwards.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FRaceLapInvalidity
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Race|Lap")
	ERaceLapInvalidReason Reason = ERaceLapInvalidReason::None;

	/** Index of the gate this fault is about, INDEX_NONE when the fault is not about a gate. */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Lap")
	int32 GateIndex = INDEX_NONE;

	/** Stable id of that gate, NAME_None when the fault is not about a gate. */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Lap")
	FName GateId;

	/** True when nothing has invalidated the lap. */
	bool IsClean() const { return Reason == ERaceLapInvalidReason::None; }

	/** The coarse published outcome. The one and only place the two vocabularies are joined. */
	ERacingRunValidity ToRunValidity() const;

	/** Human-readable, for logs and diagnostics. Allocates; not for per-frame use. */
	FString ToDebugString() const;
};

/**
 * What one call to Advance() observed.
 *
 * Returned by value so a caller can react without polling every accessor. The heavy
 * member (ClosedLap, which owns a TArray of sector durations) is only populated when
 * a lap actually closed, so the common per-frame return allocates nothing.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FRaceLapTrackerUpdate
{
	GENERATED_BODY()

	/**
	 * False when no lap logic ran: unconfigured, no state machine, non-finite input,
	 * the session is not Racing, or this was the first sample of the session (which
	 * seeds the previous position and evaluates nothing).
	 *
	 * Separate from "nothing happened", for the same reason
	 * FRacingGateCrossingResult::bEvaluated is.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Lap")
	bool bEvaluated = false;

	/** A lap ended at the finish line during this step, valid or not. */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Lap")
	bool bLapClosed = false;

	/** A VALID lap ended and the valid-lap count incremented. Implies bLapClosed. */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Lap")
	bool bLapCounted = false;

	/** A new lap opened at the finish line during this step. */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Lap")
	bool bLapOpened = false;

	/**
	 * NET ordered-gate progress during this step: +1 per gate satisfied, -1 per gate
	 * un-satisfied by a reverse crossing.
	 *
	 * SIGNED, and it has to be. A spin at a gate produces an alternating forward/reverse
	 * crossing stream, and TRACK-002's own spin case asserts the net of that stream is
	 * exactly one forward pass. Clamping this field at zero (which an earlier draft did)
	 * made the per-step values un-summable: three forward crossings and two rewinds
	 * reported 3, not 1, so a caller accumulating them saw a spin as three gates' worth
	 * of progress. The two layers must derive the same net from the same crossings.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Lap")
	int32 GatesAdvanced = 0;

	/** How many sector boundaries were timed during this step. */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Lap")
	int32 SectorsClosed = 0;

	/**
	 * Plane crossings that missed the gate rectangle (ERacingGateCrossing::OutsideExtent).
	 *
	 * Never authorises anything -- it is the shortcut signature TRACK-002 exposed for
	 * telemetry. Reported so a caller can act on it without re-running the sweep.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Lap")
	int32 NearMissCount = 0;

	/** An unannounced position jump was detected and progress was re-seeded. See Advance(). */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Lap")
	bool bTeleportDetected = false;

	/** The lap that closed during this step. LapNumber == 0 when none did. */
	UPROPERTY(BlueprintReadOnly, Category = "Race|Lap")
	FRacingLapTiming ClosedLap;
};

/**
 * Per-car lap, sector, progress and validity state.
 *
 * One of these per competitor. It holds no opinion about who the car is, what it is
 * driving, or where the world is: it is fed a world position and an arc-length
 * distance, and it answers with laps and times.
 */
UCLASS(BlueprintType)
class RACINGSIM_API URaceLapTracker : public UObject
{
	GENERATED_BODY()

public:
	// =======================================================================
	// Construction and configuration
	// =======================================================================

	/**
	 * Create a tracker owned by Owner and bound to a race state machine.
	 *
	 * @param Owner         Outer for the new object. The caller MUST also keep it alive
	 *                      in a UPROPERTY -- a UObject is not GC-rooted by its outer.
	 * @param InStateMachine the session's authoritative state and clock. Required: this
	 *                      class deliberately owns no clock of its own, so that lap time,
	 *                      sector time and session time cannot disagree.
	 * @param InRuleset     optional. Supplies the reset policy; see
	 *                      URaceRulesetDataAsset::bResetInvalidatesLap. Without one the
	 *                      SAFE default applies (a reset invalidates the lap).
	 * @return nullptr if Owner or InStateMachine is null.
	 */
	static URaceLapTracker* Create(UObject* Owner, URaceStateMachine* InStateMachine, URaceRulesetDataAsset* InRuleset = nullptr);

	/**
	 * Take the track snapshot this tracker will validate laps against.
	 *
	 * COPIES the gate set and the sector table on purpose -- see the class comment's
	 * threading note. Call once per session, not per frame: this is the only allocating
	 * function on the class.
	 *
	 * @param InGates                 baked, ordered gates. Must hold at least
	 *                                MinOrderedGateCount, or there is no order to enforce.
	 * @param InSectorStartDistancesCm ascending sector start distances, [0] exactly 0.
	 *                                May be empty: the tracker then counts laps and
	 *                                records no sector splits.
	 * @param InTrackLengthCm         lap length, cm. Must be finite and positive.
	 * @param OutError                human-readable reason on failure; untouched on success.
	 * @return true when the tracker may be used.
	 */
	bool ConfigureTrack(
		const FRacingCheckpointGateSet& InGates,
		TArrayView<const double> InSectorStartDistancesCm,
		double InTrackLengthCm,
		FString& OutError);

	/**
	 * ConfigureTrack() from a built ATrackDefinitionActor.
	 *
	 * Game thread only: it reaches the actor's lazy bake. This is the ONE call that
	 * touches the actor; everything afterwards runs off the snapshot (TRACK-002 L5).
	 */
	bool ConfigureFromTrack(const ATrackDefinitionActor* Track, FString& OutError);

	/** True once ConfigureTrack()/ConfigureFromTrack() has succeeded. */
	UFUNCTION(BlueprintPure, Category = "Race|Lap")
	bool IsConfigured() const { return Gates.IsValid() && TrackLengthCm > 0.0; }

	/**
	 * Fewest ordered gates a tracker will accept.
	 *
	 * TWO IS WHERE AN ORDER EXISTS AT ALL, and that is all this constant claims. It is
	 * NOT the shortcut-resistance floor: that is ATrackDefinitionActor::MinCheckpointGateCount,
	 * which is 4 and is enforced where track publishability is decided, on the reasoning
	 * its own comment gives. A tracker asked to validate a two-gate set will do so
	 * correctly; whether such a track should be raced on is not this class's call, and
	 * duplicating the policy number here would create a second place for it to drift.
	 */
	static constexpr int32 MinOrderedGateCount = 2;

	// =======================================================================
	// Session lifecycle
	// =======================================================================

	/**
	 * Clear ALL lap, sector, progress and validity state. Idempotent.
	 *
	 * Docs/03-TrackRaceUI.md, Restart: "Perform a complete state reset without reusing
	 * stale timers, delegates, input, or checkpoint state." The gate-crossed flags are
	 * cleared element by element rather than by reallocating the array, so a restart
	 * allocates nothing and the array cannot silently change length underneath a caller.
	 *
	 * Does NOT clear the track snapshot: a restart re-races the same circuit. Call
	 * ConfigureTrack() again to change tracks.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Lap")
	void ResetForNewSession();

	/**
	 * Seed the previous position/distance without evaluating anything.
	 *
	 * REQUIRED BEFORE THE FIRST EVALUATED STEP, and Advance() will do it implicitly on
	 * its first call if nobody else has. The reason is not tidiness: a crossing test is
	 * a segment from the previous position to the current one, and an unseeded previous
	 * position is the world origin -- so the first segment of the session would sweep
	 * from (0,0,0) to the grid and register every gate between them.
	 *
	 * At race start the caller should seed from ATrackDefinitionActor::GetGridSlotPose(),
	 * whose distance output exists precisely because a transform alone cannot give the
	 * arc length (TRACK-001 H2).
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Lap")
	void SeedProgress(const FVector& WorldLocationCm, double CenterlineDistanceCm);

	/**
	 * One evaluation step for this car: previous position -> this position.
	 *
	 * Docs/03-TrackRaceUI.md's algorithm, in order:
	 *   1. every gate is tested against the motion SEGMENT (never a volume overlap, so
	 *      speed and hitch length cannot tunnel through a gate);
	 *   2. crossings are processed in the order they PHYSICALLY happened -- ascending
	 *      CrossingAlpha -- not in gate-index order;
	 *   3. only the expected gate advances progress;
	 *   4. a skipped or out-of-order gate invalidates the lap and names the gate;
	 *   5. the finish gate counts a lap only after every ordered gate and a legal
	 *      forward crossing;
	 *   6. arc-length distance supplies continuous progress and sector splits, and
	 *      NEVER authorises a lap.
	 *
	 * Allocation-free in the steady state. Crossing results are collected into an inline
	 * buffer; only a lap CLOSE allocates, once, for that lap's sector array.
	 *
	 * @param WorldLocationCm      the car's position now, centimetres.
	 * @param CenterlineDistanceCm its arc-length position now, centimetres. Progress and
	 *                             sector splits only. Pass the result of
	 *                             FindNearestCenterlinePointNear seeded from the previous
	 *                             step (or from a reset/grid distance), never a global
	 *                             search -- see ATrackDefinitionActor's warning about
	 *                             hairpins.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Lap")
	FRaceLapTrackerUpdate Advance(const FVector& WorldLocationCm, double CenterlineDistanceCm);

	/**
	 * The vehicle was reset/teleported to a safe pose. Re-seeds progress; applies the
	 * ruleset's reset policy to the lap in progress.
	 *
	 * WHY THE CALLER MUST TELL US. A reset is a discontinuity in position, and the
	 * crossing test is a segment between two positions. Without this call the next
	 * Advance() would sweep a segment from where the car went off to where it was put
	 * back, and that segment can cross gates the car never drove through -- forwards or
	 * backwards. Docs/03-TrackRaceUI.md's own functional-test list names it: "reset
	 * chooses a safe legal pose and prevents immediate duplicate gate triggers."
	 *
	 * @param ResetWorldLocationCm    where the car was placed.
	 * @param ResetSampleDistanceCm   the arc length of that pose, from
	 *                                ATrackDefinitionActor::GetResetSampleDistanceCm() or
	 *                                GetResetPoseAtOrBeforeDistanceCm()'s out-parameter.
	 *                                TRACK-001 added those accessors for exactly this
	 *                                call: after a teleport the previous progress hint is
	 *                                by definition stale, and recovering a distance with
	 *                                the GLOBAL nearest-point search is what
	 *                                RacingSim.Race.CenterlineAmbiguity proves snaps to
	 *                                the wrong leg of a hairpin.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Lap")
	void NotifyVehicleReset(const FVector& ResetWorldLocationCm, double ResetSampleDistanceCm);

	// =======================================================================
	// Reads
	// =======================================================================

	/** Laps completed VALIDLY this session. This is the number a result publishes. */
	UFUNCTION(BlueprintPure, Category = "Race|Lap")
	int32 GetValidLapsCompleted() const { return ValidLapsCompleted; }

	/** Laps that ended at the line this session, valid or not. Ranking and HUD only. */
	UFUNCTION(BlueprintPure, Category = "Race|Lap")
	int32 GetLapsCompleted() const { return LapsCompleted; }

	/** 1-based ordinal of the lap in progress, 0 before the first line crossing. */
	UFUNCTION(BlueprintPure, Category = "Race|Lap")
	int32 GetCurrentLapNumber() const { return CurrentLapNumber; }

	/** True once the car has crossed the line forwards and a lap is being timed. */
	UFUNCTION(BlueprintPure, Category = "Race|Lap")
	bool IsLapInProgress() const { return bLapInProgress; }

	/**
	 * The gate this car must cross next. INDEX_NONE when unconfigured.
	 *
	 * Docs/03-TrackRaceUI.md's `ExpectedCheckpointId`, as an index. Before the first
	 * line crossing it is the start/finish gate, which is what opens lap 1.
	 */
	UFUNCTION(BlueprintPure, Category = "Race|Lap")
	int32 GetExpectedGateIndex() const { return ExpectedGateIndex; }

	/** Has this gate been crossed forward during the lap in progress? False for a bad index. */
	UFUNCTION(BlueprintPure, Category = "Race|Lap")
	bool IsGateSatisfied(int32 GateIndex) const;

	/** The fault recorded against the lap in progress, if any. */
	UFUNCTION(BlueprintPure, Category = "Race|Lap")
	FRaceLapInvalidity GetCurrentLapInvalidity() const { return CurrentLapInvalidity; }

	/** Sector index the car is timing, 0-based. INDEX_NONE when no lap is in progress. */
	UFUNCTION(BlueprintPure, Category = "Race|Lap")
	int32 GetCurrentSectorIndex() const { return bLapInProgress ? CurrentSectorIndex : INDEX_NONE; }

	/** Number of sectors in the track snapshot. Zero when the track authored none. */
	UFUNCTION(BlueprintPure, Category = "Race|Lap")
	int32 GetNumSectors() const { return SectorStartDistancesCm.Num(); }

	/**
	 * The lap in progress: running duration and the sector splits closed so far.
	 *
	 * Validity is Pending until the lap closes, per FRacingLapTiming's contract. Const,
	 * and reads the clock's high-water mark rather than sampling it, so a HUD repaint
	 * cannot advance race time.
	 */
	UFUNCTION(BlueprintPure, Category = "Race|Lap")
	FRacingLapTiming GetCurrentLapTiming() const;

	/** The most recently closed lap, valid or not. LapNumber == 0 when none has closed. */
	UFUNCTION(BlueprintPure, Category = "Race|Lap")
	FRacingLapTiming GetLastCompletedLap() const { return LastCompletedLap; }

	/** The fastest VALID lap this session. LapNumber == 0 when there is none. */
	UFUNCTION(BlueprintPure, Category = "Race|Lap")
	FRacingLapTiming GetBestValidLap() const { return BestValidLap; }

	/**
	 * The view-model frame a HUD reads: lap, last gate, arc-length distance, fraction.
	 *
	 * CORE-002's FRacingProgressSample, filled from this object's own state. Widgets stay
	 * passive and read this; nothing downstream may derive a lap from SplineDistanceCm,
	 * which the struct's own comment forbids in as many words.
	 */
	UFUNCTION(BlueprintPure, Category = "Race|Lap")
	FRacingProgressSample GetProgressSample() const;

	/**
	 * Session-level validity, as far as THIS ticket can decide it.
	 *
	 * Pending normally. InvalidIncomplete once a timing fault has been observed --
	 * which is RACE-001's finding M4 closed: a race clock that refused to start would
	 * otherwise freeze a silent 0.000 with nothing marking the run.
	 *
	 * The terminal Valid outcome, penalties and the published result are RACE-003's;
	 * this deliberately does not invent them.
	 */
	UFUNCTION(BlueprintPure, Category = "Race|Lap")
	ERacingRunValidity GetRunValidity() const { return RunValidity; }

	/** Arc-length distance of the last accepted sample, cm. Progress/ranking only. */
	UFUNCTION(BlueprintPure, Category = "Race|Lap")
	double GetProgressDistanceCm() const { return PreviousDistanceCm; }

	/** The snapshot this tracker validates against. Empty until configured. */
	const FRacingCheckpointGateSet& GetGates() const { return Gates; }

	/** Lap length of the snapshot, cm. */
	double GetTrackLengthCm() const { return TrackLengthCm; }

	/**
	 * ARC-LENGTH travel, cm, above which one step is treated as an unannounced teleport
	 * rather than as driving. A quarter of a lap.
	 *
	 * DERIVED, NOT AUTHORED. No tunable was added for it, so there is no number to get
	 * wrong per track and no range table to keep in sync (CORE-003 C3-1..C3-3 are not
	 * triggered by this ticket).
	 *
	 * WHY A QUARTER AND NOT A HALF. The wrapped signed delta this is compared against
	 * cannot represent more than half a lap -- at exactly half, "went forward almost a
	 * lap" and "went backward almost a lap" are the same number -- so a bound AT the
	 * representation's breakdown point can never fire. A quarter sits at half the
	 * breakdown point, which leaves margin instead of sitting on a cliff, and is still
	 * far beyond driving: on a 3 km circuit that is 750 m in one evaluation step, versus
	 * the 139 cm a 60 Hz tick covers at 300 km/h.
	 *
	 * RESIDUAL, STATED RATHER THAN HIDDEN: a SMALL unannounced jump -- a respawn 25 m up
	 * the road -- is under this bound and is not caught. It cannot be: a threshold tight
	 * enough to catch it would fire on a legitimate one-second hitch. NotifyVehicleReset()
	 * is the contract for moving a car; this is defence in depth behind it, not a
	 * replacement for it.
	 */
	double GetImplausibleArcStepCm() const { return TrackLengthCm * 0.25; }

	/**
	 * STRAIGHT-LINE step length, cm, above which a step is treated as a teleport. Half a
	 * lap.
	 *
	 * A second test on a different quantity, because neither alone is sufficient and the
	 * failure is not symmetric. The arc test cannot see a jump that moves a car a long way
	 * through SPACE without moving it along the ROUTE -- respawning on the far side of a
	 * hairpin, or the opposite leg of a parallel straight. The chord test cannot see a
	 * jump along the racing line on a compact circuit: on a circular track the largest
	 * possible chord is the diameter, 2R, while a quarter lap is πR/2 -- so a car
	 * teleported to the exact opposite side of the circle moves only 2R ≈ 0.32 of the lap
	 * in a straight line and would pass a chord-only test at any threshold that does not
	 * also reject normal driving. That is not hypothetical: it is what this project's own
	 * circular test fixture does, and it is how the chord-only version of this guard was
	 * caught.
	 */
	double GetImplausibleChordStepCm() const { return TrackLengthCm * 0.5; }

private:
	/** Apply a fault to the lap in progress. First fault wins; see FRaceLapInvalidity. */
	void MarkLapInvalid(ERaceLapInvalidReason Reason, int32 GateIndex, FName GateId);

	/** Open a new lap at TimeSeconds. Clears gate flags and sector splits. */
	void OpenLap(double TimeSeconds);

	/** Close the lap in progress at TimeSeconds and fill OutTiming. Returns true if the lap was valid. */
	bool CloseLap(double TimeSeconds, FRacingLapTiming& OutTiming);

	/** Lowest-indexed ordered gate not yet crossed this lap, or INDEX_NONE when all are. */
	int32 FindFirstMissedGateIndex() const;

	/** Record the sample as "where the car was" without evaluating crossings. */
	void AcceptSample(const FVector& WorldLocationCm, double CenterlineDistanceCm);

	/** Shortest signed arc-length delta from A to B on a closed lap, in (-L/2, +L/2]. */
	double SignedDistanceDeltaCm(double FromCm, double ToCm) const;

	/** Forward-only arc-length distance from A to B, in [0, L). */
	double ForwardDistanceDeltaCm(double FromCm, double ToCm) const;

	/** Observe the state machine's clock-fault latch and record it once. */
	void ObserveClockFault();

	// -- Bound at construction ---------------------------------------------

	/** The session's authoritative state and clock. Never null after Create(). */
	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	TObjectPtr<URaceStateMachine> StateMachine;

	/** Optional. Held as a UPROPERTY so an assigned asset cannot be collected mid-session. */
	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	TObjectPtr<URaceRulesetDataAsset> Ruleset;

	// -- Track snapshot, taken once at configuration ------------------------

	/** Copy of the baked gate set. See the class comment for why it is a copy. */
	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	FRacingCheckpointGateSet Gates;

	/** Copy of the authored sector starts, ascending, [0] == 0. Empty means "no sector timing". */
	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	TArray<double> SectorStartDistancesCm;

	/** Lap length, cm. */
	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	double TrackLengthCm = 0.0;

	// -- Per-session state --------------------------------------------------

	/**
	 * One flag per gate, cleared on every lap open. Sized once, at configuration.
	 *
	 * Deliberately NOT a UPROPERTY: it holds no object reference, nothing serialises it,
	 * and TArray<bool> is the one container shape whose reflection support is worth not
	 * betting a build on. Everything a HUD needs is published by IsGateSatisfied().
	 */
	TArray<bool> GateSatisfied;

	/** Race-clock time at which each sector of the lap in progress started. [0] is the lap open. */
	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	TArray<double> SectorStartTimesSeconds;

	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	int32 ValidLapsCompleted = 0;

	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	int32 LapsCompleted = 0;

	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	int32 CurrentLapNumber = 0;

	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	int32 ExpectedGateIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	int32 CurrentSectorIndex = 0;

	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	bool bLapInProgress = false;

	/** Race-clock time the lap in progress opened at, seconds. */
	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	double LapOpenTimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	FRaceLapInvalidity CurrentLapInvalidity;

	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	FRacingLapTiming LastCompletedLap;

	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	FRacingLapTiming BestValidLap;

	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	ERacingRunValidity RunValidity = ERacingRunValidity::Pending;

	// -- Sampling state -----------------------------------------------------

	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	FVector PreviousWorldLocationCm = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	double PreviousDistanceCm = 0.0;

	/** Race-clock reading at the previous accepted sample. Interpolation endpoint. */
	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	double PreviousTimeSeconds = 0.0;

	/** False until the first sample. See SeedProgress(). */
	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	bool bHasPreviousSample = false;

	/** Last session id seen from the state machine. See the class comment. */
	UPROPERTY(VisibleAnywhere, Category = "Race|Lap")
	int32 ObservedSessionId = 0;
};
