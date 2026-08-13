// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RacingSimTypes.generated.h"

/**
 * CORE-002 shared enumerations.
 *
 * These are the vocabulary that Vehicle/, Race/, UI/ and Streaming/ all speak.
 * They live in Core/ so no layer has to include another layer's header just to
 * name a concept. Nothing here has behaviour.
 *
 * Stability warning (finding N-3): these are UENUMs, so their *names* are part
 * of the /Script/RacingSim path once a DataAsset or Blueprint references them.
 * Renaming an enumerator later needs a CoreRedirect. Add entries; do not
 * renumber or repurpose them.
 */

/**
 * Which physical control device produced a run.
 *
 * Recorded on every result because a wheel-and-pedals lap and a keyboard lap are
 * not the same athletic event. Used for leaderboard *filtering*, not for
 * comparability (see FRacingSimVersionStamp::IsComparableTo).
 */
UENUM(BlueprintType)
enum class ERacingInputDeviceType : uint8
{
	/** Not yet determined. A result must never be published as Unknown. */
	Unknown			UMETA(DisplayName = "Unknown"),
	Keyboard		UMETA(DisplayName = "Keyboard"),
	Gamepad			UMETA(DisplayName = "Gamepad"),
	Wheel			UMETA(DisplayName = "Wheel and pedals"),
	Touch			UMETA(DisplayName = "Touch"),
	/**
	 * Input arrived over Pixel Streaming and the true device could not be
	 * identified. Kept distinct from Unknown: it is a known-lossy path, not a
	 * missing value. Streaming/ may set this; it may set nothing else on a result.
	 */
	RemoteStreamed	UMETA(DisplayName = "Remote (streamed)")
};

/** Driving-assist bundle in force for a run. Assists change the achievable lap time, so a run records one. */
UENUM(BlueprintType)
enum class ERacingAssistPreset : uint8
{
	/** No assists: no traction control, no stability control, no ABS, no racing line. */
	Raw				UMETA(DisplayName = "Raw"),
	/** ABS only. */
	Assisted		UMETA(DisplayName = "Assisted"),
	/** ABS, traction control and stability control. */
	FullyAssisted	UMETA(DisplayName = "Fully assisted"),
	/** Assist set was authored per-vehicle rather than chosen from a preset; not comparable to any preset. */
	Custom			UMETA(DisplayName = "Custom")
};

/**
 * Whether a completed run counts.
 *
 * Set by Race/ only. UI/ displays it; Streaming/ never writes it. A run starts
 * Pending and moves to exactly one terminal value; Unknown means the state
 * machine never ran and is always a bug, never a display state.
 */
UENUM(BlueprintType)
enum class ERacingRunValidity : uint8
{
	Unknown					UMETA(DisplayName = "Unknown"),
	Pending					UMETA(DisplayName = "Pending"),
	Valid					UMETA(DisplayName = "Valid"),
	/** An ordered checkpoint gate was missed, or a gate order violation was detected. */
	InvalidShortcut			UMETA(DisplayName = "Invalid - shortcut"),
	/** A gate was crossed against its authored direction. */
	InvalidReverseCrossing	UMETA(DisplayName = "Invalid - reverse crossing"),
	/** The vehicle was reset, teleported or respawned during the run. */
	InvalidVehicleReset		UMETA(DisplayName = "Invalid - vehicle reset"),
	/** Penalties accumulated past the ruleset threshold. */
	InvalidPenalty			UMETA(DisplayName = "Invalid - penalty"),
	/** Run did not finish (abandoned, disconnected, session ended). */
	InvalidIncomplete		UMETA(DisplayName = "Invalid - incomplete")
};

/** Why a penalty was issued. Kept coarse on purpose; the ruleset asset owns the thresholds. */
UENUM(BlueprintType)
enum class ERacingPenaltyReason : uint8
{
	None			UMETA(DisplayName = "None"),
	TrackLimits		UMETA(DisplayName = "Track limits"),
	Collision		UMETA(DisplayName = "Collision"),
	Cutting			UMETA(DisplayName = "Corner cutting"),
	FalseStart		UMETA(DisplayName = "False start"),
	Other			UMETA(DisplayName = "Other")
};

/**
 * Where a race session is in its lifecycle.
 *
 * RACE-001. Lives in Core/ rather than Race/ for one concrete reason: UI/ has to
 * read it to drive the HUD (countdown banner, finish banner, results screen) and
 * UI/ must not take a dependency on Race/ to do it. Race/ owns the *transitions*;
 * Core/ owns only the vocabulary.
 *
 * The authored transition graph, and the fact that only it is legal, is enforced
 * by URaceStateMachine (Race/RaceStateMachine.h). Nothing else in the project may
 * assign this enum. Reading it is free; writing it is Race/'s alone.
 *
 *   PreRace --> Countdown --> Racing --> Finished --> Results
 *      ^                                                 |
 *      +---------------- Restart (legal from any) -------+
 *
 * There is deliberately no Unknown/None/Invalid member, unlike ERacingRunValidity.
 * A run's validity genuinely can be "not yet determined"; a state machine's state
 * cannot. PreRace is enumerator 0, so a default-constructed ERaceState is the
 * correct boot state rather than a sentinel that every consumer must special-case.
 * The cost is that an out-of-range cast cannot be distinguished from PreRace by
 * value alone -- which is why URaceStateMachine range-checks at its own boundary
 * instead of relying on a sentinel to do it.
 *
 * Docs/01-Architecture.md draws the graph with Boot/Loading/Grid ahead of
 * Countdown. Those are collapsed into PreRace here: RACE-001 is track-agnostic, so
 * there is no track or car to load or grid onto yet, and modelling three states
 * that no code can currently distinguish would be modelling a fiction. Splitting
 * PreRace later is an additive change (append enumerators, add edges); merging
 * states that shipped separately would not be.
 */
UENUM(BlueprintType)
enum class ERaceState : uint8
{
	/**
	 * Session exists, nothing is timed, drive input is not released.
	 * Clock invariant: reset to zero and not running. This is the only state a
	 * restart may land in, and it is enumerator 0 on purpose.
	 */
	PreRace		UMETA(DisplayName = "Pre-race"),

	/**
	 * 3-2-1-Go. Drive input stays locked. The race clock is still zero and still
	 * not running -- a countdown that fed the race clock would hand every driver a
	 * head start measured in whatever the countdown cost.
	 */
	Countdown	UMETA(DisplayName = "Countdown"),

	/** Drive input released, race clock running. The only state in which race time accrues. */
	Racing		UMETA(DisplayName = "Racing"),

	/**
	 * The result is frozen. Race clock stopped exactly once; further sampling
	 * returns the same duration no matter how long the finish presentation runs.
	 */
	Finished	UMETA(DisplayName = "Finished"),

	/** Final time, validity and restart/exit controls are shown. Clock stays frozen. */
	Results		UMETA(DisplayName = "Results")
};

/** Speed unit shown to the player. Presentation only -- see RacingSimUnits.h; storage is always cm/s. */
UENUM(BlueprintType)
enum class ERacingSpeedDisplayUnit : uint8
{
	KilometresPerHour	UMETA(DisplayName = "km/h"),
	MilesPerHour		UMETA(DisplayName = "mph"),
	MetresPerSecond		UMETA(DisplayName = "m/s")
};

/** Distance unit shown to the player. Presentation only; storage is always centimetres. */
UENUM(BlueprintType)
enum class ERacingDistanceDisplayUnit : uint8
{
	Metric		UMETA(DisplayName = "Metric (m / km)"),
	Imperial	UMETA(DisplayName = "Imperial (ft / mi)")
};

/**
 * How the game build ID string is composed.
 *
 * Two schemes, because CI and a developer machine need different guarantees.
 * Resolved by FRacingSimBuildId::Current().
 */
UENUM(BlueprintType)
enum class ERacingBuildIdScheme : uint8
{
	/**
	 * Derived: "<Channel>-<ProjectVersion>-<EngineVersion>+<EngineChangelist>-<Config>-<TargetType>".
	 * Requires no external input and is always available, but two builds of
	 * different source can share an ID on a developer machine.
	 */
	Derived			UMETA(DisplayName = "Derived from engine and project version"),
	/**
	 * Explicit: whatever CI stamped into RacingSimSettings::ExplicitBuildId.
	 * The only scheme that can be trusted to be unique per build; the only one
	 * valid for a published leaderboard.
	 */
	Explicit		UMETA(DisplayName = "Explicit (stamped by CI)")
};

/** Penalty tally for a run. Durations are seconds; formatting belongs to UI/. */
USTRUCT(BlueprintType)
struct RACINGSIM_API FRacingPenaltySummary
{
	GENERATED_BODY()

	/** How many penalties were issued during the run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Racing|Penalty", meta = (ClampMin = "0"))
	int32 PenaltyCount = 0;

	/** Total time added, in SECONDS. Not milliseconds, not a formatted string. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Racing|Penalty", meta = (ClampMin = "0.0"))
	double TotalPenaltySeconds = 0.0;

	/** The reason that accounts for the most penalty time. Display aid only; the full log lives with the result. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Racing|Penalty")
	ERacingPenaltyReason DominantReason = ERacingPenaltyReason::None;

	bool IsClean() const
	{
		return PenaltyCount == 0 && TotalPenaltySeconds == 0.0;
	}

	bool operator==(const FRacingPenaltySummary& Other) const
	{
		return PenaltyCount == Other.PenaltyCount
			&& TotalPenaltySeconds == Other.TotalPenaltySeconds
			&& DominantReason == Other.DominantReason;
	}

	bool operator!=(const FRacingPenaltySummary& Other) const
	{
		return !(*this == Other);
	}
};
