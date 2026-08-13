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
