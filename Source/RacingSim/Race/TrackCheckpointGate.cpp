// Copyright RacingSim. All Rights Reserved.

#include "Race/TrackCheckpointGate.h"

namespace TrackCheckpointGatePrivate
{
	/** Tolerance for "this gate sits on the start/finish origin", centimetres. */
	constexpr double DistanceEpsilonCm = 1.0e-4;

	bool IsFiniteVector(const FVector& V)
	{
		return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
	}

	/**
	 * Minimum |Up x Forward| for a gate's sideways axis to be usable.
	 *
	 * Same quantity and same threshold as TrackCenterline.cpp's MinLateralAxisLength,
	 * and deliberately the same value: a gate's RightAxis and a centerline query's
	 * lateral axis are the SAME axis, so if the two disagreed about when it is
	 * degenerate a gate could be built on geometry the query then refuses to measure
	 * against. Duplicated rather than exported because it is an implementation
	 * threshold on both sides, not a published contract; this comment is the link.
	 */
	constexpr double MinLateralAxisLength = 1.0e-6;
}

// ===========================================================================
// FRacingCheckpointGate
// ===========================================================================

FTransform FRacingCheckpointGate::GetTransform() const
{
	// Built from the baked axes rather than from MakeFromXZ, so the transform and the
	// crossing test can never describe different rectangles. The axes are orthonormal by
	// construction in Build().
	const FMatrix Basis(PlaneNormal, RightAxis, UpAxis, FVector::ZeroVector);
	return FTransform(Basis.ToQuat(), Location, FVector::OneVector);
}

bool FRacingCheckpointGate::IsDirectionLegal(const ERacingGateCrossing Crossing) const
{
	switch (Crossing)
	{
	case ERacingGateCrossing::Forward:
		return LegalDirection == ERacingGateDirection::Forward
			|| LegalDirection == ERacingGateDirection::Bidirectional;

	case ERacingGateCrossing::Reverse:
		return LegalDirection == ERacingGateDirection::Reverse
			|| LegalDirection == ERacingGateDirection::Bidirectional;

	default:
		// None and OutsideExtent are not crossings of the gate, so they cannot be legal
		// ones. Returning false here rather than falling through to a permissive default
		// is the difference between "did not go through the gate" and "went through it
		// legally".
		return false;
	}
}

// ===========================================================================
// FRacingCheckpointGateSet: bake
// ===========================================================================

bool FRacingCheckpointGateSet::Build(
	TArrayView<const FRacingCheckpointGateSpec> InSpecs,
	const FTrackCenterline& Centerline,
	const double MinCurveRadiusCm,
	FString& OutError)
{
	using namespace TrackCheckpointGatePrivate;

	// Validate first, commit last: a rejected rebuild must leave the previous set
	// intact, not half-overwritten with a mixture of old and new gates.
	if (!Centerline.IsValid())
	{
		OutError = TEXT("Checkpoint gates cannot be baked against an unbuilt centerline.");
		return false;
	}

	if (InSpecs.Num() < 1)
	{
		OutError = TEXT("A track needs at least one checkpoint gate: the start/finish gate at distance 0.");
		return false;
	}

	const double LengthCm = Centerline.GetLengthCm();

	if (!FMath::IsFinite(MinCurveRadiusCm) || MinCurveRadiusCm <= 0.0)
	{
		OutError = FString::Printf(
			TEXT("MinCurveRadiusCm must be finite and positive, got %f. It converts the centerline's "
				 "maximum segment length into the gate placement tolerance; without it every gate "
				 "width check would be vacuous."),
			MinCurveRadiusCm);
		return false;
	}

	// TRACK-001 L2 and the polyline counter-case, both discharged here.
	//
	// The bake uses the MAXIMUM segment length, not the mean, because a mean cannot
	// bound a worst case: one long segment among four hundred short ones barely moves
	// the average and completely determines the error. Turned into a distance via the
	// sagitta at the tightest authored radius, it is the amount by which a gate's plane
	// can sit inside the true curve -- systematically, always in the same direction.
	const double NewMaxSegmentLengthCm = Centerline.GetMaxSegmentLengthCm();
	const double NewPlacementToleranceCm = Centerline.GetSagittaBoundCm(MinCurveRadiusCm);

	TArray<FRacingCheckpointGate> NewGates;
	NewGates.Reserve(InSpecs.Num());

	for (int32 Index = 0; Index < InSpecs.Num(); ++Index)
	{
		const FRacingCheckpointGateSpec& Spec = InSpecs[Index];

		if (Spec.GateId.IsNone())
		{
			OutError = FString::Printf(TEXT("Checkpoint gate %d has no GateId. An unnamed gate cannot be reported on a result."), Index);
			return false;
		}

		for (int32 Earlier = 0; Earlier < Index; ++Earlier)
		{
			if (InSpecs[Earlier].GateId == Spec.GateId)
			{
				OutError = FString::Printf(
					TEXT("Checkpoint gates %d and %d share the id '%s'. Ids identify gates in results and "
						 "diagnostics, so duplicates make an out-of-order lap unattributable."),
					Earlier, Index, *Spec.GateId.ToString());
				return false;
			}
		}

		if (!FMath::IsFinite(Spec.DistanceAlongCm))
		{
			OutError = FString::Printf(TEXT("Checkpoint gate %d ('%s') has a non-finite DistanceAlongCm."), Index, *Spec.GateId.ToString());
			return false;
		}

		if (Index == 0)
		{
			// Gate 0 IS the start/finish gate, pinned to the distance origin exactly as
			// SectorStartDistancesCm[0] is. See FRacingCheckpointGateSpec::DistanceAlongCm.
			if (!FMath::IsNearlyZero(Spec.DistanceAlongCm, DistanceEpsilonCm))
			{
				OutError = FString::Printf(
					TEXT("Checkpoint gate 0 ('%s') must sit at distance 0 -- it is the start/finish gate, and "
						 "the centerline's distance origin IS the start/finish line. Got %f cm."),
					*Spec.GateId.ToString(), Spec.DistanceAlongCm);
				return false;
			}
		}
		else if (Spec.DistanceAlongCm <= InSpecs[Index - 1].DistanceAlongCm)
		{
			OutError = FString::Printf(
				TEXT("Checkpoint gate distances must strictly increase; gate %d ('%s') at %f cm is not past "
					 "gate %d at %f cm. The set defines the ORDER checkpoints must be taken in."),
				Index, *Spec.GateId.ToString(), Spec.DistanceAlongCm,
				Index - 1, InSpecs[Index - 1].DistanceAlongCm);
			return false;
		}

		if (Spec.DistanceAlongCm >= LengthCm)
		{
			OutError = FString::Printf(
				TEXT("Checkpoint gate %d ('%s') is at %f cm, at or past the lap length %f cm. Distance %f cm "
					 "would wrap onto gate 0."),
				Index, *Spec.GateId.ToString(), Spec.DistanceAlongCm, LengthCm, Spec.DistanceAlongCm);
			return false;
		}

		if (Index > 0)
		{
			// Two gates inside one polyline segment share an identical plane normal and
			// nearly identical location, which makes a crossing between them undecidable:
			// one motion segment crosses both planes at almost the same alpha and the
			// order they are reported in becomes a floating-point coin flip. The MAXIMUM
			// segment length is the right separation floor for exactly that reason.
			const double SeparationCm = Spec.DistanceAlongCm - InSpecs[Index - 1].DistanceAlongCm;
			if (SeparationCm <= NewMaxSegmentLengthCm)
			{
				OutError = FString::Printf(
					TEXT("Checkpoint gates %d and %d are %f cm apart, within one centerline segment (%f cm). "
						 "Two gates inside one segment share a plane normal and cannot be ordered reliably; "
						 "move them apart or bake the centerline finer."),
					Index - 1, Index, SeparationCm, NewMaxSegmentLengthCm);
				return false;
			}
		}

		if (!FMath::IsFinite(Spec.HalfWidthCm) || Spec.HalfWidthCm <= 0.0)
		{
			OutError = FString::Printf(
				TEXT("Checkpoint gate %d ('%s') HalfWidthCm must be finite and positive, got %f."),
				Index, *Spec.GateId.ToString(), Spec.HalfWidthCm);
			return false;
		}

		if (!FMath::IsFinite(Spec.HalfHeightCm) || Spec.HalfHeightCm <= 0.0)
		{
			OutError = FString::Printf(
				TEXT("Checkpoint gate %d ('%s') HalfHeightCm must be finite and positive, got %f."),
				Index, *Spec.GateId.ToString(), Spec.HalfHeightCm);
			return false;
		}

		if (Spec.HalfWidthCm <= NewPlacementToleranceCm)
		{
			// The polyline bias is one-directional, so it does not average out: a gate
			// this narrow has its entire margin consumed by the model's own systematic
			// inward error before a car is anywhere near it.
			OutError = FString::Printf(
				TEXT("Checkpoint gate %d ('%s') is %f cm half-wide but the baked centerline's placement "
					 "tolerance is %f cm (max segment %f cm at a %f cm minimum radius). A gate narrower than "
					 "the model's own placement error cannot be crossed reliably."),
				Index, *Spec.GateId.ToString(), Spec.HalfWidthCm, NewPlacementToleranceCm,
				NewMaxSegmentLengthCm, MinCurveRadiusCm);
			return false;
		}

		// -- Geometry, derived entirely from the centerline ------------------
		const FVector Location = Centerline.GetLocationAtDistanceCm(Spec.DistanceAlongCm);
		const FVector Forward = Centerline.GetForwardAtDistanceCm(Spec.DistanceAlongCm);

		if (!IsFiniteVector(Location) || !IsFiniteVector(Forward))
		{
			OutError = FString::Printf(
				TEXT("Checkpoint gate %d ('%s') derived non-finite geometry from the centerline at %f cm."),
				Index, *Spec.GateId.ToString(), Spec.DistanceAlongCm);
			return false;
		}

		// TRACK-001 L7, applied at BAKE time rather than at query time. The centerline
		// query now flags a degenerate sideways axis instead of reporting a false zero;
		// a gate must go further and refuse to exist, because a gate whose width has no
		// direction is not a bounded rectangle at all.
		const FVector RightUnnormalised = FVector::CrossProduct(FVector::UpVector, Forward);
		const double RightLength = RightUnnormalised.Size();
		if (RightLength <= MinLateralAxisLength)
		{
			OutError = FString::Printf(
				TEXT("Checkpoint gate %d ('%s') sits on an effectively vertical centerline segment at %f cm, "
					 "where the sideways axis (Up x Forward) is undefined; the gate would have width but no "
					 "direction to measure it in."),
				Index, *Spec.GateId.ToString(), Spec.DistanceAlongCm);
			return false;
		}

		const FVector RightAxis = RightUnnormalised / RightLength;

		FRacingCheckpointGate Gate;
		Gate.GateId = Spec.GateId;
		Gate.Ordinal = Index;
		Gate.DistanceAlongCm = Spec.DistanceAlongCm;
		Gate.HalfWidthCm = Spec.HalfWidthCm;
		Gate.HalfHeightCm = Spec.HalfHeightCm;
		Gate.LegalDirection = Spec.LegalDirection;
		Gate.Location = Location;
		Gate.PlaneNormal = Forward;
		Gate.RightAxis = RightAxis;

		// Completed from the other two rather than taken as world up, so the three axes
		// are orthonormal by construction and a banked gate leans with the road instead
		// of shearing across it.
		Gate.UpAxis = FVector::CrossProduct(Forward, RightAxis).GetSafeNormal();

		// See FRacingCheckpointGate::RelevanceRadiusCm. Four times the rectangle's own
		// diagonal: enough that going wide through runoff still registers as a near-miss,
		// bounded enough that the same infinite plane met on the far side of a closed
		// circuit does not.
		Gate.RelevanceRadiusCm = 4.0 * FMath::Sqrt(
			Spec.HalfWidthCm * Spec.HalfWidthCm + Spec.HalfHeightCm * Spec.HalfHeightCm);

		NewGates.Add(Gate);
	}

	// M1 (code-reviewer, TRACK-002 pass 1), fixed at RACE-002: THE SEPARATION INVARIANT
	// MUST ALSO HOLD ACROSS THE LOOP CLOSURE.
	//
	// The per-gate check above compares each gate with InSpecs[Index - 1], which leaves
	// exactly one pair unchecked on a closed circuit: the LAST gate and gate 0. Nothing
	// stopped an authored last gate from sitting one centimetre before the line -- the
	// near-identical-plane case the check exists to prevent, at the one place on the
	// track where getting the order wrong costs a lap rather than a checkpoint.
	//
	// Gate 0 is pinned to distance 0 (validated above), so the wrap gap is simply the
	// distance from the last gate to the end of the lap. Only meaningful on a closed
	// loop: an open centerline has no wrap, and its last gate is followed by nothing.
	if (Centerline.IsClosedLoop() && NewGates.Num() > 1)
	{
		const double WrapSeparationCm = LengthCm - NewGates.Last().DistanceAlongCm;
		if (WrapSeparationCm <= NewMaxSegmentLengthCm)
		{
			OutError = FString::Printf(
				TEXT("Checkpoint gate %d ('%s') at %f cm is %f cm before the start/finish gate across the loop "
					 "closure, within one centerline segment (%f cm). Two gates inside one segment share a plane "
					 "normal and cannot be ordered reliably, and this pair straddles the lap boundary; move the "
					 "gate back or bake the centerline finer."),
				NewGates.Num() - 1, *NewGates.Last().GateId.ToString(), NewGates.Last().DistanceAlongCm,
				WrapSeparationCm, NewMaxSegmentLengthCm);
			return false;
		}
	}

	// Commit.
	Gates = MoveTemp(NewGates);
	MaxSegmentLengthCm = NewMaxSegmentLengthCm;
	PlacementToleranceCm = NewPlacementToleranceCm;
	return true;
}

void FRacingCheckpointGateSet::Reset()
{
	Gates.Reset();
	MaxSegmentLengthCm = 0.0;
	PlacementToleranceCm = 0.0;
}

const FRacingCheckpointGate* FRacingCheckpointGateSet::GetGate(const int32 GateIndex) const
{
	return Gates.IsValidIndex(GateIndex) ? &Gates[GateIndex] : nullptr;
}

int32 FRacingCheckpointGateSet::FindGateIndexById(const FName GateId) const
{
	for (int32 Index = 0; Index < Gates.Num(); ++Index)
	{
		if (Gates[Index].GateId == GateId)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

int32 FRacingCheckpointGateSet::GetNextGateIndex(const int32 GateIndex) const
{
	if (!Gates.IsValidIndex(GateIndex))
	{
		return INDEX_NONE;
	}

	return (GateIndex + 1) % Gates.Num();
}

// ===========================================================================
// FRacingCheckpointGateSet: crossing
// ===========================================================================

FRacingGateCrossingResult FRacingCheckpointGateSet::EvaluateCrossing(
	const int32 GateIndex,
	const FVector& FromWorldCm,
	const FVector& ToWorldCm) const
{
	using namespace TrackCheckpointGatePrivate;

	FRacingGateCrossingResult Result;

	const FRacingCheckpointGate* Gate = GetGate(GateIndex);
	if (!Gate || !IsFiniteVector(FromWorldCm) || !IsFiniteVector(ToWorldCm))
	{
		// bEvaluated stays false: "I could not look" must not be reported as "nothing
		// crossed". A NaN position after a physics blow-up is exactly when a silent
		// None would be most misleading.
		return Result;
	}

	Result.bEvaluated = true;
	Result.GateIndex = GateIndex;
	Result.GateId = Gate->GateId;

	const double FromCm = Gate->GetSignedPlaneDistanceCm(FromWorldCm);
	const double ToCm = Gate->GetSignedPlaneDistanceCm(ToWorldCm);
	Result.SignedDistanceFromCm = FromCm;
	Result.SignedDistanceToCm = ToCm;

	// THE SIGN RULE, and it is asymmetric on purpose.
	//
	//   forward  <=>  FromCm <  0  <=  ToCm
	//   reverse  <=>  ToCm   <  0  <=  FromCm
	//
	// Zero belongs to the AHEAD side in both cases, which makes the plane a half-open
	// boundary and gives three properties that a symmetric rule does not:
	//
	//   1. A car resting exactly on the plane -- stalled on the line, or nudged there by
	//      a collision -- is "past" it. It cannot re-trigger by sitting still, because a
	//      motion from 0 to 0 satisfies neither test.
	//   2. A car spinning ON the line produces a strictly alternating Forward, Reverse,
	//      Forward... sequence with no repeats and no dropped transitions, so RACE-002's
	//      ordering logic sees a net of exactly one crossing. `.claude/rules/race-tests.md`
	//      requires spins at a gate not to double-trigger; this is where that is decided.
	//   3. Motion entirely WITHIN the plane (both distances exactly 0, i.e. driving along
	//      the line) is None rather than a crossing, which is the physically correct
	//      answer and the one a symmetric `<=` rule gets wrong.
	const bool bForward = (FromCm < 0.0) && (ToCm >= 0.0);
	const bool bReverse = (ToCm < 0.0) && (FromCm >= 0.0);

	if (!bForward && !bReverse)
	{
		Result.Crossing = ERacingGateCrossing::None;
		return Result;
	}

	// Denominator cannot be zero here: one of the two values is strictly negative and
	// the other is >= 0, so they differ. Asserted by construction rather than by a
	// guard that would silently pick an arbitrary alpha.
	const double Denominator = FromCm - ToCm;
	const double Alpha = FMath::Clamp(FromCm / Denominator, 0.0, 1.0);

	const FVector CrossingPoint = FMath::Lerp(FromWorldCm, ToWorldCm, Alpha);
	const double LateralCm = Gate->GetLateralOffsetCm(CrossingPoint);
	const double VerticalCm = Gate->GetVerticalOffsetCm(CrossingPoint);

	// THE PLANE IS INFINITE AND THE TRACK IS A LOOP. On any closed circuit each gate's
	// plane is met a second time somewhere else -- on a circular test track, two
	// diameters away on the far side. Reporting that as OutsideExtent would mean a clean
	// lap generated a phantom event on every gate, and a caller reading DidCrossPlane()
	// would see activity on gates the car was nowhere near. Beyond the relevance radius
	// this is not a near-miss of THIS gate; it is a different part of the circuit.
	//
	// Found by running, not by inspection: the first version of this function had no such
	// bound and RacingSim.Race.GateCurvedTrack reported nine crossings for a four-gate
	// lap, the extra five being far-side plane hits up to 200 m off centre.
	if (FMath::Square(LateralCm) + FMath::Square(VerticalCm) > FMath::Square(Gate->RelevanceRadiusCm))
	{
		// The crossing fields are left at their defaults so that Crossing == None always
		// means CrossingAlpha == 0, rather than sometimes carrying an alpha for a
		// crossing the caller is being told did not happen.
		Result.Crossing = ERacingGateCrossing::None;
		return Result;
	}

	Result.CrossingAlpha = Alpha;
	Result.CrossingPoint = CrossingPoint;
	Result.CrossingLateralOffsetCm = LateralCm;
	Result.CrossingVerticalOffsetCm = VerticalCm;

	// The extent test is applied to the INTERSECTION POINT, not to either endpoint.
	// Testing an endpoint would let a car that passed the gate two lanes wide count as
	// having gone through it merely because it ended up back on the racing line, and
	// would reject a car that crossed the middle of the gate at 300 km/h because neither
	// endpoint was anywhere near it.
	const bool bWithinExtent =
		FMath::Abs(LateralCm) <= Gate->HalfWidthCm && FMath::Abs(VerticalCm) <= Gate->HalfHeightCm;

	if (!bWithinExtent)
	{
		Result.Crossing = ERacingGateCrossing::OutsideExtent;
		Result.bMatchesLegalDirection = false;
		return Result;
	}

	Result.Crossing = bForward ? ERacingGateCrossing::Forward : ERacingGateCrossing::Reverse;
	Result.bMatchesLegalDirection = Gate->IsDirectionLegal(Result.Crossing);
	return Result;
}

int32 FRacingCheckpointGateSet::EvaluateCrossings(
	const FVector& FromWorldCm,
	const FVector& ToWorldCm,
	TFunctionRef<void(const FRacingGateCrossingResult&)> Visitor) const
{
	int32 CrossedCount = 0;

	for (int32 Index = 0; Index < Gates.Num(); ++Index)
	{
		const FRacingGateCrossingResult Result = EvaluateCrossing(Index, FromWorldCm, ToWorldCm);
		if (Result.DidCrossPlane())
		{
			++CrossedCount;
			Visitor(Result);
		}
	}

	return CrossedCount;
}

int32 FRacingCheckpointGateSet::FindFirstCrossing(
	const FVector& FromWorldCm,
	const FVector& ToWorldCm,
	FRacingGateCrossingResult& OutResult) const
{
	OutResult = FRacingGateCrossingResult();

	int32 BestIndex = INDEX_NONE;
	double BestAlpha = TNumericLimits<double>::Max();

	for (int32 Index = 0; Index < Gates.Num(); ++Index)
	{
		const FRacingGateCrossingResult Result = EvaluateCrossing(Index, FromWorldCm, ToWorldCm);
		if (!Result.DidCrossPlane())
		{
			continue;
		}

		// Strictly less than, so a tie keeps the LOWER gate index. A tie means two gates
		// were met at the same point of the motion, which Build() already prevents by
		// requiring more than one segment between gates -- but the tiebreak is defined
		// anyway, because "deterministic" is the property callers rely on and leaving it
		// to iteration order makes it accidental rather than guaranteed.
		if (Result.CrossingAlpha < BestAlpha)
		{
			BestAlpha = Result.CrossingAlpha;
			BestIndex = Index;
			OutResult = Result;
		}
	}

	return BestIndex;
}
