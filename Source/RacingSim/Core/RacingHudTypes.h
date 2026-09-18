// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/RacingSimTypes.h"
#include <type_traits>
#include "RacingHudTypes.generated.h"

/**
 * UI-001: the race-side half of the HUD data contract.
 *
 * ===========================================================================
 * Why this lives in Core/
 * ===========================================================================
 *
 * Race/ owns the truth and UI/ must not include a Race/ header to read it (the
 * ERaceState comment in RacingSimTypes.h states that rule). So the hand-off is a
 * struct in the one layer both may depend on: Race/ FILLS it
 * (URaceFunctionLibrary::GatherHudRaceInputs), UI/ READS it
 * (URacingHudViewModelLibrary::BuildHudViewModel), and neither knows the other exists.
 *
 * ===========================================================================
 * Scalars only, and the compiler enforces it
 * ===========================================================================
 *
 * No TArray, FString or FText. CORE-002 finding M-4: a struct that carries a heap
 * container heap-allocates on every copy, and CLAUDE.md forbids per-frame allocation.
 * The static_assert below the struct fails the build if a future edit adds one.
 *
 * ===========================================================================
 * What the numbers mean
 * ===========================================================================
 *
 * Every field is copied from an authoritative getter, not derived. The lap fields
 * follow URaceLapTracker's L9 convention exactly: while lap N is timed,
 * CurrentLapNumber == N and LapsCompleted == N - 1. Time is SECONDS on the race clock.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FRacingHudRaceInputs
{
	GENERATED_BODY()

	// -- State machine ----------------------------------------------------------

	/** False when no state machine was supplied; every other field is then its default. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bHasRaceState = false;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	ERaceState RaceState = ERaceState::PreRace;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 SessionId = 0;

	/** Seconds left on the countdown. 0 outside Countdown. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	double CountdownRemainingSeconds = 0.0;

	/** Race-clock elapsed SECONDS, peeked (a HUD read never ratchets the clock). */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	double RaceElapsedSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bRaceClockFaulted = false;

	// -- Lap tracker (L9 convention) --------------------------------------------

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 CurrentLapNumber = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 LapsCompleted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 ValidLapsCompleted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bLapInProgress = false;

	/** Running time of the lap in progress, SECONDS. 0 with no lap in progress. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	double CurrentLapElapsedSeconds = 0.0;

	/** True once the lap in progress has recorded a fault. It can no longer count. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bCurrentLapInvalid = false;

	/** 0-based sector being timed. INDEX_NONE with no lap in progress. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 CurrentSectorIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 NumSectors = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bHasLastLap = false;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	double LastLapSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bLastLapValid = false;

	/**
	 * Did the last lap's splits account for it, checked against the track's REAL sector
	 * count (RACE-003 finding M3)? False for a lap whose splits were withheld.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bLastLapSectorsConsistent = false;

	/** A VALID best lap exists. Never set from the last lap. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bHasBestLap = false;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	double BestLapSeconds = 0.0;

	/** 0..1 round the current lap. Ranking and HUD only; never authorises a lap. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	float LapProgressFraction = 0.0f;

	/** 1-based position. 0 means unclassified. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 RacePosition = 0;

	/** Cars in the session, including this one. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 CompetitorCount = 0;

	/** No delta source exists yet; always false until one does. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bHasDelta = false;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	double DeltaToBestSeconds = 0.0;

	// -- Result recorder --------------------------------------------------------

	/** A result has been frozen for this session. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bResultAvailable = false;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	double ResultFinalTimeSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	ERacingRunValidity ResultValidity = ERacingRunValidity::Unknown;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 ResultLapsCompleted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 ResultValidLapsCompleted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bResultHasBestLap = false;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	double ResultBestLapSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bResultClockFaulted = false;
};

// CORE-002 M-4, enforced rather than promised: copying this struct must never allocate.
static_assert(std::is_trivially_copyable_v<FRacingHudRaceInputs>,
	"FRacingHudRaceInputs must stay trivially copyable: no TArray/FString/FText members (CORE-002 M-4).");
