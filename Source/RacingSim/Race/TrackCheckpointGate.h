// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Race/TrackCenterline.h"
#include "TrackCheckpointGate.generated.h"

/**
 * TRACK-002: what a checkpoint gate IS, and whether a given crossing satisfies it.
 *
 * ===========================================================================
 * Scope: this file decides direction, it does NOT decide laps
 * ===========================================================================
 *
 * `.claude/rules/race-tests.md`: "Checkpoint order plus crossing direction authorizes
 * laps; spline distance alone never does." This ticket owns the CROSSING DIRECTION
 * half of that sentence. RACE-002 owns the ORDER half and consumes the per-gate
 * results below to build lap validity.
 *
 * So there is deliberately nothing here that counts laps, holds a timer, tracks an
 * expected-gate cursor, or references ERaceState. A future edit that adds "has this
 * car completed a lap" to this file is a design error, not a convenience -- the same
 * warning ATrackDefinitionActor carries, for the same reason.
 *
 * ===========================================================================
 * Why a plane test over a motion segment, and not a trigger volume
 * ===========================================================================
 *
 * A UBoxComponent overlap answers "was the car inside the box during a tick". At 300
 * km/h a car covers 139 cm per 60 Hz tick and over 800 cm in a 100 ms hitch, so a gate
 * thin enough to be a line is a gate an overlap can step straight over. Docs/03-
 * TrackRaceUI.md's algorithm is explicit that "a checkpoint trigger evaluates the
 * vehicle's PREVIOUS/CURRENT position against its plane", and that is what this is: a
 * segment-versus-plane intersection, which cannot be tunnelled through regardless of
 * speed or tick length, and which yields the direction for free from the sign change.
 *
 * The extent test is then applied to the INTERSECTION POINT rather than to either
 * endpoint, so "went past the gate but around the outside" is distinguishable from
 * "went through it" -- see ERacingGateCrossing::OutsideExtent.
 *
 * ===========================================================================
 * Units
 * ===========================================================================
 *
 * Unreal centimetres throughout, per CORE-002 (Core/RacingSimUnits.h); every distance
 * member and parameter carries a `Cm` suffix. Arc-length positions are the same
 * distance domain as GetGridSlotDistanceCm/GetResetSampleDistanceCm, i.e. measured
 * from the start/finish origin along FTrackCenterline.
 *
 * ===========================================================================
 * Originality
 * ===========================================================================
 *
 * Nothing here encodes any circuit. Gates are positions along whatever centerline is
 * authored, and the fallback generator spaces them by a generic rule.
 */

/**
 * Which way a gate may legally be crossed.
 *
 * "Legal" is a property of the GATE, not of the crossing: the crossing reports what
 * physically happened and this says what was permitted, so a caller can always tell a
 * reverse crossing from a rejected one. CLAUDE.md requires reverse finish crossings to
 * be rejected; it does not permit them to be invisible.
 */
UENUM(BlueprintType)
enum class ERacingGateDirection : uint8
{
	/** Legal only in the direction of increasing arc length. The normal case, including start/finish. */
	Forward,

	/**
	 * Legal only against the direction of travel.
	 *
	 * Not a curiosity: a pit-lane or service-road gate authored on the main centerline
	 * is crossed backwards relative to lap progress. Kept as a first-class value so such
	 * a gate is expressed rather than faked by inverting the geometry, which would make
	 * every debug draw point the wrong way.
	 */
	Reverse,

	/** Legal in either direction. For gates used purely to observe passage. */
	Bidirectional
};

/**
 * What a single motion segment did to a single gate.
 *
 * Note that Forward/Reverse describe PHYSICS, not permission. A reverse crossing of a
 * Forward-only gate reports Reverse with bMatchesLegalDirection == false; it never
 * reports None. Collapsing an illegal crossing to "nothing happened" is exactly the
 * failure mode the ticket forbids, because RACE-002 must be able to invalidate a lap
 * on the reverse crossing it can then see.
 */
UENUM(BlueprintType)
enum class ERacingGateCrossing : uint8
{
	/** The motion did not pass through the gate's plane. Both endpoints on the same side, or motion within the plane. */
	None,

	/** Passed through the plane, inside the gate's width and height, along increasing arc length. */
	Forward,

	/** Passed through the plane, inside the gate's width and height, against increasing arc length. */
	Reverse,

	/**
	 * Passed NEAR the gate, through its plane, but outside its width or height.
	 *
	 * Distinct from None on purpose. The plane is infinite; the gate is not. A car
	 * driving round the outside of a gate, over a barrier, or through a runoff area
	 * still crosses the plane, and a caller that saw None could not tell that from
	 * "did not reach the gate at all". It is also the signature of a shortcut, which
	 * RACE-002 needs to be able to act on.
	 *
	 * "Near" is bounded by FRacingCheckpointGate::RelevanceRadiusCm. A plane crossing
	 * farther away than that is somewhere else on the circuit -- the same infinite plane
	 * met on the far side of the loop -- and is reported as None, not as a near-miss.
	 */
	OutsideExtent
};

/**
 * One baked checkpoint gate: a bounded rectangle standing across the track at a fixed
 * arc-length distance, with a legal crossing direction.
 *
 * The plane normal is the centerline's direction of travel at DistanceAlongCm, so
 * "forward" for a gate is by construction the same forward that increases arc length.
 * There is no separately authored orientation, for the same reason TRACK-001 refused a
 * separately authored start/finish distance: two sources for one fact is two chances to
 * disagree, and the disagreement would be a gate that rejects legal laps.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FRacingCheckpointGate
{
	GENERATED_BODY()

	/** Stable identifier, e.g. "Gate.StartFinish". Written into diagnostics; never the array index. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	FName GateId;

	/**
	 * Position in the ordered sequence, 0 first. Equal to the index in the owning set.
	 *
	 * Carried on the gate as well as implied by the array so that a gate passed by value
	 * to RACE-002 still knows where it sits in the order without its container.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	int32 Ordinal = 0;

	/** Arc length from the start/finish origin, cm. Same domain as GetGridSlotDistanceCm. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	double DistanceAlongCm = 0.0;

	/** Half the gate's width, cm, measured sideways from the centerline along RightAxis. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	double HalfWidthCm = 0.0;

	/**
	 * Half the gate's height, cm, measured along UpAxis from the centerline.
	 *
	 * Finite rather than infinite so that a car launched over the gate is reported as
	 * OutsideExtent instead of as a clean crossing. On a graybox with jumps that is not
	 * hypothetical.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	double HalfHeightCm = 0.0;

	/** Which crossing direction this gate permits. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	ERacingGateDirection LegalDirection = ERacingGateDirection::Forward;

	/** World-space centre of the gate: the centerline point at DistanceAlongCm. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	FVector Location = FVector::ZeroVector;

	/** Unit plane normal, pointing along increasing arc length. This is what makes a crossing "forward". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	FVector PlaneNormal = FVector::ForwardVector;

	/** Unit sideways axis, positive to the RIGHT of the direction of travel (Up x Forward), matching FTrackCenterlineQuery. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	FVector RightAxis = FVector::RightVector;

	/** Unit vertical axis of the gate, orthogonal to the other two. Not world up on a banked section. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	FVector UpAxis = FVector::UpVector;

	/**
	 * How far from the gate's centre, cm, a plane crossing is still ABOUT this gate.
	 *
	 * THE GATE'S PLANE IS INFINITE AND THE TRACK IS A LOOP, so on any closed circuit
	 * every gate's plane is met a second time somewhere else entirely -- on a circular
	 * test track, on the far side, two diameters away. Without this radius a single
	 * clean lap reports every gate twice: once through the rectangle and once as an
	 * OutsideExtent crossing hundreds of metres away, and a caller reading
	 * DidCrossPlane() sees phantom activity on gates the car was nowhere near.
	 *
	 * Beyond this radius a plane crossing is reported as None: it is not a near-miss of
	 * this gate, it is a different part of the circuit. Inside it but outside the
	 * rectangle it stays OutsideExtent, which is the genuinely interesting case -- a car
	 * that went round the gate through runoff.
	 *
	 * Derived, not authored: four times the gate's own diagonal. Four rather than one so
	 * that going wide through runoff still registers as a near-miss (a 900x500 cm gate
	 * gives a ~41 m radius), and bounded rather than unlimited so the far side of a
	 * circuit cannot masquerade as one.
	 *
	 * RESIDUAL RISK, STATED RATHER THAN HIDDEN: where two parts of a circuit pass within
	 * this radius of each other -- a pit lane beside the main straight -- a crossing on
	 * the wrong leg is still reported as OutsideExtent. That is safe, because only
	 * IsThroughGate() authorises anything and OutsideExtent never does; but a caller
	 * counting near-misses for telemetry must know it. A crossing on the wrong leg
	 * WITHIN the rectangle would be a false positive, and that needs the two legs to pass
	 * within HalfWidthCm of each other, i.e. closer than the gate is wide.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	double RelevanceRadiusCm = 0.0;

	/** Signed distance from the gate plane, cm. Positive is ahead of the gate in the direction of travel. */
	double GetSignedPlaneDistanceCm(const FVector& WorldLocationCm) const
	{
		return FVector::DotProduct(WorldLocationCm - Location, PlaneNormal);
	}

	/** Sideways offset of a world point from the gate centre, cm. Positive is to the right of travel. */
	double GetLateralOffsetCm(const FVector& WorldLocationCm) const
	{
		return FVector::DotProduct(WorldLocationCm - Location, RightAxis);
	}

	/** Vertical offset of a world point from the gate centre along the gate's own up axis, cm. */
	double GetVerticalOffsetCm(const FVector& WorldLocationCm) const
	{
		return FVector::DotProduct(WorldLocationCm - Location, UpAxis);
	}

	/** True when a world point lies within the gate's rectangle, ignoring which side of the plane it is on. */
	bool IsWithinExtent(const FVector& WorldLocationCm) const
	{
		return FMath::Abs(GetLateralOffsetCm(WorldLocationCm)) <= HalfWidthCm
			&& FMath::Abs(GetVerticalOffsetCm(WorldLocationCm)) <= HalfHeightCm;
	}

	/** Pose of the gate: X along the direction of travel, Y right, Z the gate's up. Scale is identity. */
	FTransform GetTransform() const;

	/** True when this direction is one the gate permits. */
	bool IsDirectionLegal(ERacingGateCrossing Crossing) const;
};

/**
 * The result of testing one motion segment against one gate.
 *
 * Returned by value because a caller that wants the direction almost always also wants
 * where in the tick it happened (for interpolating a line-crossing time later) and how
 * far off centre it was.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FRacingGateCrossingResult
{
	GENERATED_BODY()

	/**
	 * False when no test was performed at all: the gate index was out of range, the set
	 * was unbuilt, or an endpoint was non-finite.
	 *
	 * Separate from `Crossing == None` on purpose. "I looked and nothing crossed" and
	 * "I could not look" are different facts, and conflating them is how a broken
	 * checkpoint system reports a clean race.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	bool bEvaluated = false;

	/** What physically happened. Not what was permitted -- see bMatchesLegalDirection. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	ERacingGateCrossing Crossing = ERacingGateCrossing::None;

	/**
	 * True when Crossing is Forward or Reverse AND matches the gate's LegalDirection.
	 *
	 * Always false for None and OutsideExtent: neither is a crossing of the gate, so
	 * neither can be a legal one.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	bool bMatchesLegalDirection = false;

	/** Index of the gate tested, INDEX_NONE when bEvaluated is false. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	int32 GateIndex = INDEX_NONE;

	/** Id of the gate tested, NAME_None when bEvaluated is false. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	FName GateId;

	/** Signed plane distance of the segment's start, cm. Negative is behind the gate. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	double SignedDistanceFromCm = 0.0;

	/** Signed plane distance of the segment's end, cm. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	double SignedDistanceToCm = 0.0;

	/**
	 * Where along the motion segment the plane was crossed, in [0, 1]. Zero when no
	 * crossing occurred.
	 *
	 * This is the hook RACE-002 needs to interpolate a sub-tick crossing TIME from the
	 * monotonic clock rather than attributing the whole tick to the crossing. Recorded
	 * here because it is free at the moment of the intersection and unrecoverable
	 * afterwards.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	double CrossingAlpha = 0.0;

	/** World-space point where the segment met the plane. Meaningless unless the plane was crossed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	FVector CrossingPoint = FVector::ZeroVector;

	/** Sideways offset of CrossingPoint from the gate centre, cm. Positive right of travel. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	double CrossingLateralOffsetCm = 0.0;

	/** Vertical offset of CrossingPoint from the gate centre along the gate's up axis, cm. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	double CrossingVerticalOffsetCm = 0.0;

	/** True for Forward or Reverse, i.e. the car actually went through the gate rectangle. */
	bool IsThroughGate() const
	{
		return Crossing == ERacingGateCrossing::Forward || Crossing == ERacingGateCrossing::Reverse;
	}

	/** True when the plane was crossed at all, including outside the gate's extent. */
	bool DidCrossPlane() const
	{
		return IsThroughGate() || Crossing == ERacingGateCrossing::OutsideExtent;
	}
};

/**
 * How a gate's arc-length position is authored, before it is baked against a centerline.
 *
 * Authored separately from FRacingCheckpointGate so that the world-space plane is never
 * hand-entered: it is always derived from the centerline, which is what guarantees a
 * gate stands square across the track and faces the direction that increases arc
 * length. The grid slots follow the same authored-parameters/generated-geometry split,
 * and for the same stated reason: N hand-placed transforms are N chances to leave one
 * facing backwards, and nothing in a graybox would catch it.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FRacingCheckpointGateSpec
{
	GENERATED_BODY()

	/** Stable identifier. Must be unique within a set and must not be None. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	FName GateId;

	/**
	 * Arc length from the start/finish origin, cm.
	 *
	 * Entry 0 must be exactly 0: gate 0 IS the start/finish gate, pinned to the
	 * centerline's distance origin the same way SectorStartDistancesCm[0] is. TRACK-001
	 * refused to let the start/finish line float to an arbitrary arc length, and letting
	 * the gate that guards it float instead would reintroduce the same wrap-offset
	 * arithmetic at every comparison.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints",
		meta = (ClampMin = "0.0", ForceUnits = "cm"))
	double DistanceAlongCm = 0.0;

	/** Half the gate width, cm. Should comfortably exceed the racing surface's half-width. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints",
		meta = (ClampMin = "1.0", ForceUnits = "cm"))
	double HalfWidthCm = 900.0;

	/** Half the gate height, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints",
		meta = (ClampMin = "1.0", ForceUnits = "cm"))
	double HalfHeightCm = 500.0;

	/** Which way this gate may legally be crossed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	ERacingGateDirection LegalDirection = ERacingGateDirection::Forward;
};

/**
 * An ordered, baked set of checkpoint gates.
 *
 * Ordered by arc length, ascending, starting at 0. The ORDER is published here (it is a
 * property of the track); enforcing that a car visits the gates IN that order is
 * RACE-002's job, and there is deliberately no per-car cursor in this struct.
 *
 * World-free and allocation-free to query, exactly like FTrackCenterline and for the
 * same reason: the project's only automation gate is a commandlet-hosted
 * `Automation RunFilter Smoke` under -nullrhi, so anything that needs a registered
 * component to be testable is effectively untestable here.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FRacingCheckpointGateSet
{
	GENERATED_BODY()

	/**
	 * Bake gate geometry from authored specs against a built centerline.
	 *
	 * Validates everything before mutating anything, so a rejected build leaves a
	 * previously good set intact rather than half-overwritten -- the same commit-at-the-
	 * bottom discipline FTrackCenterline::Build uses.
	 *
	 * @param InSpecs             authored gates, ascending in DistanceAlongCm, [0] at exactly 0.
	 * @param Centerline          a built centerline; gate planes are derived from it.
	 * @param MinCurveRadiusCm    tightest corner radius the authored curve contains, cm. Used to turn
	 *                            the centerline's MAXIMUM segment length into a placement-error bound
	 *                            (FTrackCenterline::GetSagittaBoundCm) that every gate must be wide
	 *                            enough to swallow. Pass the authored value, not a guess.
	 * @param OutError            human-readable reason on failure; untouched on success.
	 * @return true when the set is usable.
	 */
	bool Build(
		TArrayView<const FRacingCheckpointGateSpec> InSpecs,
		const FTrackCenterline& Centerline,
		double MinCurveRadiusCm,
		FString& OutError);

	/** Drop all baked gates. IsValid() is false afterwards. */
	void Reset();

	/** True once Build() has succeeded and the set has not been Reset(). */
	bool IsValid() const { return Gates.Num() > 0; }

	int32 NumGates() const { return Gates.Num(); }

	bool IsValidGateIndex(const int32 GateIndex) const { return Gates.IsValidIndex(GateIndex); }

	/** Gate by index, or nullptr when out of range. Never asserts: a HUD asking for gate 9 of 4 is a bug, not a crash. */
	const FRacingCheckpointGate* GetGate(int32 GateIndex) const;

	/** Index of a gate by id, or INDEX_NONE. Linear over a handful of gates; not for per-frame use. */
	int32 FindGateIndexById(FName GateId) const;

	/**
	 * The next gate in the ordered sequence, wrapping at the end.
	 *
	 * Publishes the ORDER, which is track data. It does not advance anything and does
	 * not know which gate any car is expecting -- that state belongs to RACE-002.
	 */
	int32 GetNextGateIndex(int32 GateIndex) const;

	/** Index of the start/finish gate. Always 0 by construction; named so callers stop hard-coding the literal. */
	static constexpr int32 StartFinishGateIndex = 0;

	/**
	 * The longest centerline segment the gates were baked against, cm.
	 *
	 * Recorded because it, not the mean, is what bounds gate placement error
	 * (TRACK-001 L2). Kept on the set so a validation message or a telemetry frame can
	 * report the bake's real resolution without re-deriving it.
	 */
	double GetMaxSegmentLengthCm() const { return MaxSegmentLengthCm; }

	/**
	 * Upper bound, cm, on how far a baked gate's plane can sit inside the true authored
	 * curve, at the minimum corner radius Build() was given.
	 *
	 * Every gate's HalfWidthCm is required to exceed this, so the systematic inward bias
	 * of the polyline model (TRACK-001's counter-case) cannot consume a gate's whole
	 * margin. This is the number that stops the bias being inherited silently.
	 */
	double GetPlacementToleranceCm() const { return PlacementToleranceCm; }

	/**
	 * Test one motion segment against one gate.
	 *
	 * FromWorldCm/ToWorldCm are the vehicle's position at the previous and current
	 * evaluation -- Docs/03-TrackRaceUI.md's "previous/current position against its
	 * plane". They are NOT required to be one frame apart: a 400 ms hitch or a 300 km/h
	 * straight produces a long segment and the test is unaffected, because it is a
	 * segment/plane intersection rather than a volume overlap.
	 *
	 * Allocation-free. Returns a fully populated result even when nothing crossed.
	 */
	FRacingGateCrossingResult EvaluateCrossing(int32 GateIndex, const FVector& FromWorldCm, const FVector& ToWorldCm) const;

	/**
	 * Test one motion segment against EVERY gate, reporting each crossing to a visitor.
	 *
	 * A single tick can legitimately cross more than one gate -- a long hitch, a
	 * teleport, or two gates close together through a chicane -- and returning only the
	 * first would silently drop the others, which is precisely a skipped-gate bug
	 * manufactured by the reporting layer rather than by the driver.
	 *
	 * Takes a TFunctionRef rather than filling a TArray so that a per-car-per-tick call
	 * allocates nothing (CLAUDE.md forbids per-frame allocation). The visitor is called
	 * only for results where DidCrossPlane() is true, in gate order.
	 *
	 * @return the number of gates whose plane was crossed, including OutsideExtent ones.
	 */
	int32 EvaluateCrossings(
		const FVector& FromWorldCm,
		const FVector& ToWorldCm,
		TFunctionRef<void(const FRacingGateCrossingResult&)> Visitor) const;

	/**
	 * The gate whose plane the segment met EARLIEST (smallest CrossingAlpha).
	 *
	 * Deterministic ordering matters when one tick crosses several gates: processing
	 * them in gate-index order would let a car that crossed gate 3 then gate 0 be
	 * reported the other way round. Earliest-along-the-motion is the order they
	 * physically happened in.
	 *
	 * @param OutResult  the earliest crossing, or a bEvaluated == false result when none.
	 * @return the gate index, or INDEX_NONE.
	 */
	int32 FindFirstCrossing(const FVector& FromWorldCm, const FVector& ToWorldCm, FRacingGateCrossingResult& OutResult) const;

private:
	/** Baked gates, ascending in DistanceAlongCm. Index == Ordinal. */
	UPROPERTY()
	TArray<FRacingCheckpointGate> Gates;

	/** Longest segment of the centerline these gates were baked against, cm. */
	UPROPERTY()
	double MaxSegmentLengthCm = 0.0;

	/** Sagitta bound at the supplied minimum corner radius, cm. See GetPlacementToleranceCm. */
	UPROPERTY()
	double PlacementToleranceCm = 0.0;
};
