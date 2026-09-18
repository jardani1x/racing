// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/RacingHudTypes.h"
#include "Core/RacingSimTypes.h"
#include "Race/RaceResult.h"
#include "Race/TrackCenterline.h"
#include "RaceFunctionLibrary.generated.h"

class ATrackDefinitionActor;
class URaceLapTracker;
class URaceResultRecorder;
class URaceStateMachine;

/**
 * UI-001: Blueprint and UMG access to Race/ contracts, and the race side of the HUD
 * data contract.
 *
 * Closes two findings of the same class as CORE-002's M-3 (see
 * URacingTelemetryFunctionLibrary):
 *
 *   TRACK-001 L3 -- FTrackCenterline and FTrackCenterlineQuery queries are plain C++.
 *   RACE-003 M2  -- FRacingRaceResult's read surface is plain C++.
 *
 * Every wrapper forwards to the member it names; RacingSim.Race.FunctionLibrary asserts
 * they agree. Track-taking wrappers return a safe default (0) for a null track, because a
 * widget bound before the level finishes loading will ask.
 *
 * The HUD gatherer lives here, not in UI/, because it has to include Race/ headers and
 * UI/ may not. It writes FRacingHudRaceInputs, a Core/ struct; UI/ builds the view model
 * from that without ever naming a Race/ type.
 */
UCLASS()
class RACINGSIM_API URaceFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// =======================================================================
	// Frozen result (RACE-003 M2)
	// =======================================================================

	UFUNCTION(BlueprintPure, Category = "Race|Result")
	static ERacingRunValidity GetResultValidity(const FRacingRaceResult& Result);

	UFUNCTION(BlueprintPure, Category = "Race|Result")
	static bool ResultHasValidLap(const FRacingRaceResult& Result);

	/** FRacingRaceResult::IsSubmittable. Allocates the reason; call once at results, not per frame. */
	UFUNCTION(BlueprintPure, Category = "Race|Result")
	static bool IsResultSubmittable(const FRacingRaceResult& Result, FString& OutReason);

	/** FRacingRaceResult::MakeSubmissionQueryString. False and an empty query when not submittable. */
	UFUNCTION(BlueprintPure, Category = "Race|Result")
	static bool MakeResultSubmissionQueryString(const FRacingRaceResult& Result, FString& OutQuery, FString& OutReason);

	/** Diagnostics only. Allocates. */
	UFUNCTION(BlueprintPure, Category = "Race|Result")
	static FString ResultToString(const FRacingRaceResult& Result);

	// =======================================================================
	// Centerline struct (TRACK-001 L3)
	// =======================================================================

	UFUNCTION(BlueprintPure, Category = "Race|Track")
	static bool IsCenterlineValid(const FTrackCenterline& Centerline);

	/** Lap length, cm. */
	UFUNCTION(BlueprintPure, Category = "Race|Track")
	static double GetCenterlineLengthCm(const FTrackCenterline& Centerline);

	/** FTrackCenterline::WrapDistanceCm, cm. */
	UFUNCTION(BlueprintPure, Category = "Race|Track")
	static double WrapCenterlineDistanceCm(const FTrackCenterline& Centerline, double DistanceCm);

	/** FTrackCenterline::GetSignedDistanceDeltaCm: shortest signed arc-length delta From -> To, cm. */
	UFUNCTION(BlueprintPure, Category = "Race|Track")
	static double GetCenterlineSignedDistanceDeltaCm(const FTrackCenterline& Centerline, double FromCm, double ToCm);

	/**
	 * 0..1 round the lap at DistanceCm, wrapped. 0 for an invalid centerline or a
	 * non-finite distance. Progress and HUD only: it never authorises a lap.
	 */
	UFUNCTION(BlueprintPure, Category = "Race|Track")
	static float GetCenterlineLapProgressFraction(const FTrackCenterline& Centerline, double DistanceCm);

	/**
	 * Signed lateral offset from the centerline, cm, with its validity returned rather
	 * than folded into a 0 that looks like "dead centre" (TRACK-002's L7 hazard).
	 * bOutValid is false, and the offset 0, when the query failed or the sideways axis
	 * could not be derived.
	 */
	UFUNCTION(BlueprintPure, Category = "Race|Track")
	static double GetQueryLateralOffsetCm(const FTrackCenterlineQuery& Query, bool& bOutValid);

	// =======================================================================
	// Track actor (TRACK-001 L3). Null track: 0.
	// =======================================================================

	UFUNCTION(BlueprintPure, Category = "Race|Track")
	static double GetTrackLengthCm(const ATrackDefinitionActor* Track);

	UFUNCTION(BlueprintPure, Category = "Race|Track")
	static double GetTrackLengthMetres(const ATrackDefinitionActor* Track);

	UFUNCTION(BlueprintPure, Category = "Race|Track")
	static float GetTrackLapProgressFraction(const ATrackDefinitionActor* Track, double DistanceCm);

	UFUNCTION(BlueprintPure, Category = "Race|Track")
	static double WrapTrackDistanceCm(const ATrackDefinitionActor* Track, double DistanceCm);

	UFUNCTION(BlueprintPure, Category = "Race|Track")
	static double GetTrackSignedDistanceDeltaCm(const ATrackDefinitionActor* Track, double FromCm, double ToCm);

	// =======================================================================
	// HUD data contract (UI-001)
	// =======================================================================

	/**
	 * Read the authoritative race stack into the HUD's input struct. Once per HUD update.
	 *
	 * Copies, never derives: every field comes from a getter on the object that owns it,
	 * and the lap fields keep URaceLapTracker's L9 convention. Best lap comes only from
	 * the tracker's best VALID lap and never falls back to the last lap.
	 *
	 * NO PER-FRAME ALLOCATION (CORE-002 M-4): uses GetCurrentLapElapsedSeconds() and the
	 * const& Peek reads, never GetCurrentLapTiming() or the by-value lap getters, which
	 * copy split arrays. The output is trivially copyable.
	 *
	 * BlueprintCallable, not Pure: GetCountdownRemainingSeconds() samples the countdown
	 * clock, and a Pure node would re-run that once per connected output pin.
	 *
	 * @param StateMachine    Required. Null resets OutInputs and returns false.
	 * @param LapTracker      Optional. Null leaves the lap fields at their defaults.
	 * @param ResultRecorder  Optional. Null leaves the result fields at their defaults.
	 * @param CompetitorCount Cars in the session including this one; negative reads as 0.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|HUD")
	static bool GatherHudRaceInputs(
		URaceStateMachine* StateMachine,
		const URaceLapTracker* LapTracker,
		const URaceResultRecorder* ResultRecorder,
		int32 CompetitorCount,
		FRacingHudRaceInputs& OutInputs);
};
