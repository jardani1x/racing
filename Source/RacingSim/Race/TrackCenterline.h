// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/RacingSimUnits.h"
#include "TrackCenterline.generated.h"

/**
 * TRACK-001: the baked, world-free arc-length model of a circuit centerline.
 *
 * ===========================================================================
 * Why this is a plain struct and not "just call USplineComponent"
 * ===========================================================================
 *
 *   1. TESTABILITY DECIDES IT, for exactly the reason RACE-001 recorded when it
 *      made URaceStateMachine a UObject rather than an Actor. The project's only
 *      automation gate is `Automation RunFilter Smoke` under -nullrhi in a
 *      commandlet (Docs/Environment.md). A USplineComponent's world-space queries
 *      depend on component registration and a component-to-world transform, so
 *      testing them means standing a world up first, and a harness failure then
 *      looks identical to a rule failure. This struct needs nothing: build it from
 *      an array of FVectors and the answers are checkable against closed-form
 *      geometry.
 *
 *   2. IT PINS THE MODEL THE RACE RULES ARE ALLOWED TO ASSUME. USplineComponent's
 *      FindInputKeyClosestToWorldLocation is an iterative refinement over a
 *      reparametrisation table whose resolution is authored per-spline, so its
 *      accuracy is a content property. Baking to a fixed, recorded sample spacing
 *      makes the accuracy a *code* property that a test can bound, and puts the
 *      spacing into the content hash so a re-bake at a different resolution is
 *      visible on a result rather than silently shifting every progress value.
 *
 *   3. IT IS SELF-CONSISTENT, WHICH MATTERS MORE THAN BEING EXACT. Queries here
 *      operate on the polyline through the baked samples. A point returned by
 *      GetLocationAtDistanceCm therefore sits fractionally inside the true curve
 *      (bounded by the sagitta, roughly Spacing^2 / (8 * radius)); at the default
 *      100 cm spacing and a 15 m minimum radius that is under 1 cm. What the model
 *      guarantees absolutely is that FindNearest and GetLocationAtDistanceCm agree
 *      with each other, so progress is monotonic along the route and never jumps.
 *      TotalLengthCm is kept separately and is the *spline's* authoritative arc
 *      length, so the published track length is not degraded by sampling.
 *
 * ===========================================================================
 * What this is NOT
 * ===========================================================================
 *
 * NOT LAP AUTHORITY. CLAUDE.md and Docs/03-TrackRaceUI.md are explicit that
 * ordered checkpoint gates plus a valid crossing direction authorise a lap, and
 * that "nearest centerline spline distance supplies continuous progress for
 * ranking; it never replaces ordered checkpoint validation". Nothing in this file
 * may be used to count a lap. TRACK-002 owns gates; RACE-002 owns lap validity.
 *
 * NOT CHECKPOINTS. TRACK-001 is deliberately checkpoint-agnostic.
 *
 * UNITS. Every distance here is in Unreal centimetres per the CORE-002 policy in
 * Core/RacingSimUnits.h, and every such member or parameter carries a `Cm` suffix.
 * The only SI values exposed are the explicitly named GetLengthMetres() /
 * GetLengthKilometres() accessors, which exist so UI and documentation never
 * hand-roll a divide by 100.
 */

/**
 * The result of projecting a world location onto the centerline.
 *
 * Returned by value rather than through out-parameters because the caller almost
 * always wants three of these five fields at once (progress, side of the road,
 * how far off-line), and computing them separately would mean projecting three
 * times.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FTrackCenterlineQuery
{
	GENERATED_BODY()

	/** False when the centerline is unbuilt or the query location was non-finite. Every other field is then meaningless. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track")
	bool bValid = false;

	/**
	 * Arc length from the start/finish origin to the projected point, in Unreal
	 * centimetres, always in [0, TotalLengthCm) for a closed loop.
	 *
	 * This is the continuous progress value Docs/03-TrackRaceUI.md rule 7 orders
	 * ranking by (`lap * trackLength + splineDistance`). It is NOT a lap counter
	 * and carries no direction information on its own -- a car facing backwards
	 * produces a perfectly ordinary distance.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track")
	double DistanceAlongCm = 0.0;

	/** The closest point on the centerline itself, in world space (cm). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track")
	FVector Location = FVector::ZeroVector;

	/** Unit direction of travel at that point, i.e. the direction of increasing DistanceAlongCm. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track")
	FVector Forward = FVector::ForwardVector;

	/**
	 * Signed sideways offset from the centerline, in centimetres. Positive is to
	 * the RIGHT of the direction of travel (Unreal is left-handed: Right = Up x
	 * Forward).
	 *
	 * Sign is kept rather than an absolute distance because a track-limits rule
	 * needs to know which side was left, and because RACE-002's "did the car cut
	 * the corner" question is a signed one.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track")
	double LateralOffsetCm = 0.0;

	/**
	 * Unsigned 3D distance from the query location to Location. Differs from
	 * |LateralOffsetCm| whenever the car is above or below the centerline, which
	 * on a circuit with elevation change is always.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track")
	double DistanceToCenterlineCm = 0.0;
};

/**
 * A closed or open centerline, baked to evenly spaced arc-length samples.
 *
 * Build it once (ATrackDefinitionActor does this from its USplineComponent) and
 * query it as often as you like: every query below is allocation-free and touches
 * only two flat arrays.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FTrackCenterline
{
	GENERATED_BODY()

	/**
	 * Build from samples whose arc-length distances are already known.
	 *
	 * This is the path ATrackDefinitionActor uses, because a USplineComponent can
	 * report a true arc length that a chord sum cannot recover.
	 *
	 * @param InLocations       world-space sample positions, cm. >= 3 for a closed loop, >= 2 otherwise.
	 * @param InDistancesCm     arc length of each sample from the origin; strictly increasing, InDistancesCm[0] == 0.
	 * @param InTotalLengthCm   authoritative total arc length. For a closed loop this is the lap distance and
	 *                          must exceed the last sample distance; for an open line it must equal it.
	 * @param bInClosedLoop     true when the last sample joins back to the first.
	 * @param OutError          human-readable reason on failure; untouched on success.
	 * @return true when the centerline is usable. On failure the struct is left Reset(), never half-built.
	 */
	bool Build(
		TArrayView<const FVector> InLocations,
		TArrayView<const double> InDistancesCm,
		double InTotalLengthCm,
		bool bInClosedLoop,
		FString& OutError);

	/**
	 * Build from bare points, deriving arc length as the chord sum through them.
	 *
	 * For tests and for any caller that has no spline. The derived length
	 * under-reports a curve, so this must not be used to publish a track length
	 * unless the points are dense; ATrackDefinitionActor does not use it.
	 */
	bool BuildFromPolyline(TArrayView<const FVector> InLocations, bool bInClosedLoop, FString& OutError);

	/** Drop all baked data. IsValid() is false afterwards. */
	void Reset();

	/** True once Build() has succeeded and the data has not been Reset(). */
	bool IsValid() const { return SampleLocations.Num() >= 2 && TotalLengthCm > 0.0; }

	bool IsClosedLoop() const { return bClosedLoop; }

	int32 NumSamples() const { return SampleLocations.Num(); }

	/** Number of polyline segments: NumSamples() for a closed loop (the last wraps), NumSamples()-1 otherwise. */
	int32 NumSegments() const;

	// -- Length -------------------------------------------------------------
	// UNIT BOUNDARY. Storage is centimetres (CORE-002); the metre and kilometre
	// accessors exist only so presentation code never divides by 100 by hand.

	/** Total arc length, Unreal centimetres. This is the lap distance for a closed loop. */
	double GetLengthCm() const { return TotalLengthCm; }

	/** Total arc length in SI metres. cm -> m, via RacingSim::Units. */
	double GetLengthMetres() const { return RacingSim::Units::CmToMetres(TotalLengthCm); }

	/** Total arc length in kilometres, for display and for the 3-5 km target in Docs/03-TrackRaceUI.md. */
	double GetLengthKilometres() const { return RacingSim::Units::CmToKilometres(TotalLengthCm); }

	/** Spacing the samples were baked at, cm. Recorded because it bounds every query's accuracy. */
	double GetSampleSpacingCm() const;

	// -- Distance-domain helpers -------------------------------------------

	/**
	 * Fold a distance into [0, TotalLengthCm) for a closed loop; clamp it to
	 * [0, TotalLengthCm] for an open one.
	 *
	 * Handles negative and multi-lap inputs. Uses Fmod plus a correction rather
	 * than a while-loop, so a caller passing a distance forty laps out (a teleport,
	 * or an uninitialised value) costs the same as a caller passing a sane one and
	 * cannot hang the game thread.
	 */
	double WrapDistanceCm(double DistanceCm) const;

	/**
	 * Shortest signed distance from FromCm to ToCm around the loop, in
	 * (-TotalLengthCm/2, +TotalLengthCm/2].
	 *
	 * Positive means ToCm is ahead in the direction of travel. This is the only
	 * correct way to compare two progress values across the start/finish origin --
	 * a plain subtraction reports a car that has just crossed the line as having
	 * gone backwards by nearly a whole lap. RACE-002 needs this; it is here because
	 * it is a property of the distance domain, not of the lap rules.
	 *
	 * Exactly half a lap apart is reported as +L/2 by convention, so the result is
	 * a function and not a coin flip.
	 */
	double GetSignedDistanceDeltaCm(double FromCm, double ToCm) const;

	// -- Point queries ------------------------------------------------------

	/** World-space position at an arc-length distance. Input is wrapped/clamped by WrapDistanceCm. */
	FVector GetLocationAtDistanceCm(double DistanceCm) const;

	/**
	 * Unit direction of travel at an arc-length distance.
	 *
	 * Piecewise constant across each segment, so it steps at sample boundaries
	 * rather than varying smoothly. That is acceptable for grid and reset poses,
	 * which sit at or near sample distances, and it is deliberately not smoothed:
	 * a smoothed tangent would disagree with the segment the position came from,
	 * and the model's one hard guarantee is that position and direction describe
	 * the same polyline.
	 */
	FVector GetForwardAtDistanceCm(double DistanceCm) const;

	/**
	 * World transform on the centerline at a distance: location plus a rotation
	 * whose X axis is the direction of travel and whose Z is as close to world up
	 * as the direction allows. Scale is identity.
	 *
	 * Used for the start/finish transform, grid slots and reset poses, so all three
	 * are derived from one definition of "facing along the track" instead of three.
	 */
	FTransform GetTransformAtDistanceCm(double DistanceCm) const;

	/**
	 * Project a world location onto the centerline, searching the whole track.
	 *
	 * O(NumSegments), allocation-free. At the default 100 cm spacing a 4 km circuit
	 * is ~4000 segments of a dot product each -- fine per car per frame, but prefer
	 * FindNearestNear() once a previous result exists, both for cost and because it
	 * is more CORRECT (see below).
	 */
	FTrackCenterlineQuery FindNearest(const FVector& WorldLocationCm) const;

	/**
	 * Project a world location onto the centerline, searching only within
	 * SearchWindowCm either side of HintDistanceCm.
	 *
	 * THIS IS THE CORRECTNESS-CRITICAL OVERLOAD, not merely the fast one. On any
	 * real circuit two parts of the track pass close to each other -- a hairpin
	 * doubling back, a pit straight beside the main straight. A global nearest-point
	 * search will happily snap a car to the wrong one, and the symptom is a
	 * progress value that teleports half a lap and a ranking that flips. Seeding
	 * with the car's last known distance removes the ambiguity.
	 *
	 * The window must be large enough to cover the distance the car can travel
	 * between queries with margin; the caller owns that choice because it depends
	 * on tick rate and top speed. A non-positive window, or a non-finite hint,
	 * falls back to the global search rather than returning nothing -- losing
	 * precision is recoverable, returning "off track" is not.
	 */
	FTrackCenterlineQuery FindNearestNear(const FVector& WorldLocationCm, double HintDistanceCm, double SearchWindowCm) const;

private:
	/** World-space sample positions, cm. */
	UPROPERTY()
	TArray<FVector> SampleLocations;

	/** Arc length of each sample from the origin, cm. Strictly increasing, [0] == 0. */
	UPROPERTY()
	TArray<double> SampleDistancesCm;

	/** Authoritative total arc length, cm. For a closed loop, > SampleDistancesCm.Last(). */
	UPROPERTY()
	double TotalLengthCm = 0.0;

	UPROPERTY()
	bool bClosedLoop = false;

	/** Index of the sample that starts the segment containing DistanceCm. Assumes DistanceCm is already wrapped. */
	int32 FindSegmentIndex(double WrappedDistanceCm) const;

	/** Length of segment SegmentIndex, cm. Handles the wrapping final segment of a closed loop. */
	double GetSegmentLengthCm(int32 SegmentIndex) const;

	/** Index of the sample that ends segment SegmentIndex. */
	int32 GetSegmentEndSampleIndex(int32 SegmentIndex) const;

	/** Shared body of FindNearest / FindNearestNear over an explicit segment index list. */
	FTrackCenterlineQuery ProjectOntoSegments(const FVector& WorldLocationCm, int32 FirstSegment, int32 NumSegmentsToTest) const;
};
