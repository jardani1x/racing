// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/RacingHudTypes.h"
#include "Core/RacingSimTypes.h"
#include "Core/RacingTelemetry.h"
#include <type_traits>
#include "RacingHudViewModel.generated.h"

/**
 * UI-001: the display-ready HUD view model.
 *
 * ===========================================================================
 * What a widget may do with this
 * ===========================================================================
 *
 * Read fields and format them. Every decision a widget would otherwise make -- is the
 * vehicle data fresh, should the countdown show, is position meaningful, which unit is
 * the speed in -- has been made here, once, in C++ that automation covers. A widget
 * that needs a rule not expressed as a field has found a gap in this struct, not a
 * reason to query the world.
 *
 * ===========================================================================
 * Layering
 * ===========================================================================
 *
 * UI/ includes Core/ only. Race truth arrives as FRacingHudRaceInputs, filled by
 * URaceFunctionLibrary::GatherHudRaceInputs; vehicle truth as
 * FRacingVehicleTelemetrySample. This file never names a Race/ type.
 *
 * ===========================================================================
 * No allocation
 * ===========================================================================
 *
 * Scalars only; the static_assert below the struct fails the build otherwise
 * (CORE-002 M-4). Building and copying one per frame costs a memcpy.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FRacingHudViewModel
{
	GENERATED_BODY()

	// -- Session ----------------------------------------------------------------

	/** False when the gatherer had no state machine; the view model is then all defaults. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bHasRaceState = false;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	ERaceState RaceState = ERaceState::PreRace;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 SessionId = 0;

	/** Race-clock elapsed SECONDS. Non-negative. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	double RaceElapsedSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bRaceClockFaulted = false;

	// -- Countdown --------------------------------------------------------------

	/**
	 * True only in ERaceState::Countdown. Can be true with CountdownWholeSeconds == 0: a
	 * manually released countdown, or the frame on which the timer has expired but the
	 * state machine has not yet gone green. Widgets should show "GO"/nothing, not "0".
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bShowCountdown = false;

	/** Seconds left, rounded UP to a whole number: 2.1 s left shows "3". 0 when hidden. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 CountdownWholeSeconds = 0;

	/** Seconds left, unrounded. 0 when hidden. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	double CountdownRemainingSeconds = 0.0;

	// -- Vehicle ----------------------------------------------------------------

	/**
	 * False when the vehicle sample is stale, stamped in the future, or non-finite. Speed,
	 * RPM and gear are then zeroed, so a widget that ignores this flag shows a blank car
	 * rather than the last live numbers frozen on screen.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bVehicleDataFresh = false;

	/** Forward speed in SpeedUnit. Signed: negative means reversing. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	double Speed = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	ERacingSpeedDisplayUnit SpeedUnit = ERacingSpeedDisplayUnit::KilometresPerHour;

	/** Non-negative. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	float EngineRPM = 0.0f;

	/** 0 neutral, negative reverse (Chaos Vehicles convention). */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 GearIndex = 0;

	// -- Laps (URaceLapTracker's L9 convention, passed through) ------------------

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 CurrentLapNumber = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 LapsCompleted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 ValidLapsCompleted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bLapInProgress = false;

	/** Running lap time, SECONDS. 0 with no lap in progress. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	double CurrentLapElapsedSeconds = 0.0;

	/** The lap in progress has already been invalidated. False with no lap in progress. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bCurrentLapInvalid = false;

	/** 1-based sector being timed. 0 with no lap in progress or an out-of-range index. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 CurrentSectorNumber = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 NumSectors = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bHasLastLap = false;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	double LastLapSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bLastLapValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bLastLapSectorsConsistent = false;

	/** A valid best lap exists. Never borrowed from the last lap. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bHasBestLap = false;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	double BestLapSeconds = 0.0;

	/** 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	float LapProgressFraction = 0.0f;

	// -- Position ---------------------------------------------------------------

	/** Position is shown only when opponents exist AND the car is classified. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bShowPosition = false;

	/** 1-based. 0 whenever bShowPosition is false. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 RacePosition = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	int32 CompetitorCount = 0;

	// -- Delta ------------------------------------------------------------------

	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bHasDelta = false;

	/** Negative is faster. 0 unless bHasDelta. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	double DeltaToBestSeconds = 0.0;

	// -- Results ----------------------------------------------------------------

	/** True with a frozen result in Finished or Results. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|HUD")
	bool bShowResults = false;

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

// CORE-002 M-4, enforced rather than promised: building and copying the view model must never allocate.
static_assert(std::is_trivially_copyable_v<FRacingHudViewModel>,
	"FRacingHudViewModel must stay trivially copyable: no TArray/FString/FText members (CORE-002 M-4).");

/** Builds FRacingHudViewModel. Pure functions; no world, no state. */
UCLASS()
class RACINGSIM_API URacingHudViewModelLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Turn race inputs and a vehicle sample into the view model.
	 *
	 * @param Inputs                   From URaceFunctionLibrary::GatherHudRaceInputs.
	 * @param Vehicle                  The latest vehicle telemetry sample.
	 * @param NowSeconds               Current time on the SAME clock as Vehicle.TimestampSeconds.
	 *                                 ARacingVehiclePawn stamps its samples with FPlatformTime::Seconds(),
	 *                                 so pass FPlatformTime::Seconds() for its samples -- NOT
	 *                                 FRacingHudRaceInputs::RaceElapsedSeconds, which is 0 before the
	 *                                 green flag and frozen after the finish. A mismatched clock fails
	 *                                 closed (the sample reads stale and speed/RPM are hidden), never
	 *                                 the other way round, but it blanks the gauges.
	 * @param VehicleStaleAfterSeconds Maximum sample age; URacingSimSettings::TelemetryStaleAfterSeconds
	 *                                 is the project default. <= 0 disables the age check.
	 * @param SpeedUnit                The player's display unit.
	 */
	UFUNCTION(BlueprintPure, Category = "Racing|HUD")
	static FRacingHudViewModel BuildHudViewModel(
		const FRacingHudRaceInputs& Inputs,
		const FRacingVehicleTelemetrySample& Vehicle,
		double NowSeconds,
		double VehicleStaleAfterSeconds,
		ERacingSpeedDisplayUnit SpeedUnit);

	/** BuildHudViewModel into an existing struct. The C++ per-frame path. */
	static void BuildHudViewModelInto(
		const FRacingHudRaceInputs& Inputs,
		const FRacingVehicleTelemetrySample& Vehicle,
		double NowSeconds,
		double VehicleStaleAfterSeconds,
		ERacingSpeedDisplayUnit SpeedUnit,
		FRacingHudViewModel& OutViewModel);

	/** Whole seconds to display for a countdown: rounded up, 0 for <= 0 or non-finite, capped at 3600. */
	UFUNCTION(BlueprintPure, Category = "Racing|HUD")
	static int32 GetCountdownWholeSeconds(double RemainingSeconds);
};
