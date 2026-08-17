// Copyright RacingSim. All Rights Reserved.

#include "Race/TrackCenterline.h"

namespace TrackCenterlinePrivate
{
	/** Tolerance for "this distance is the origin", in centimetres. Far below any authored precision. */
	constexpr double DistanceEpsilonCm = 1.0e-4;

	/**
	 * A closed loop whose first and last sample are this close is almost certainly
	 * an authoring mistake -- the wrap segment would be degenerate. 1 cm.
	 */
	constexpr double MinWrapSegmentLengthCm = 1.0;

	bool IsFiniteVector(const FVector& V)
	{
		return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
	}
}

bool FTrackCenterline::Build(
	TArrayView<const FVector> InLocations,
	TArrayView<const double> InDistancesCm,
	const double InTotalLengthCm,
	const bool bInClosedLoop,
	FString& OutError)
{
	using namespace TrackCenterlinePrivate;

	// Validate everything BEFORE touching member state, so a rejected build leaves
	// a previously good centerline intact rather than half-overwritten. The commit
	// at the bottom is the only mutation.
	if (InLocations.Num() != InDistancesCm.Num())
	{
		OutError = FString::Printf(
			TEXT("Location count (%d) and distance count (%d) differ."),
			InLocations.Num(), InDistancesCm.Num());
		return false;
	}

	const int32 MinSamples = bInClosedLoop ? 3 : 2;
	if (InLocations.Num() < MinSamples)
	{
		OutError = FString::Printf(
			TEXT("%s centerline needs at least %d samples, got %d."),
			bInClosedLoop ? TEXT("Closed") : TEXT("Open"), MinSamples, InLocations.Num());
		return false;
	}

	if (!FMath::IsFinite(InTotalLengthCm) || InTotalLengthCm <= 0.0)
	{
		OutError = FString::Printf(TEXT("TotalLengthCm must be finite and positive, got %f."), InTotalLengthCm);
		return false;
	}

	if (!FMath::IsNearlyZero(InDistancesCm[0], DistanceEpsilonCm))
	{
		// The distance origin IS the start/finish line by definition (see
		// ATrackDefinitionActor). A non-zero first sample would silently offset
		// every progress value on the track.
		OutError = FString::Printf(TEXT("First sample distance must be 0, got %f."), InDistancesCm[0]);
		return false;
	}

	for (int32 Index = 0; Index < InLocations.Num(); ++Index)
	{
		if (!IsFiniteVector(InLocations[Index]))
		{
			OutError = FString::Printf(TEXT("Sample %d location is not finite."), Index);
			return false;
		}

		if (!FMath::IsFinite(InDistancesCm[Index]))
		{
			OutError = FString::Printf(TEXT("Sample %d distance is not finite."), Index);
			return false;
		}

		if (Index > 0 && InDistancesCm[Index] <= InDistancesCm[Index - 1])
		{
			OutError = FString::Printf(
				TEXT("Sample distances must strictly increase; sample %d (%f) <= sample %d (%f)."),
				Index, InDistancesCm[Index], Index - 1, InDistancesCm[Index - 1]);
			return false;
		}
	}

	const double LastDistanceCm = InDistancesCm[InDistancesCm.Num() - 1];

	if (bInClosedLoop)
	{
		if (InTotalLengthCm <= LastDistanceCm)
		{
			OutError = FString::Printf(
				TEXT("Closed loop TotalLengthCm (%f) must exceed the last sample distance (%f); "
					 "the difference is the wrapping segment."),
				InTotalLengthCm, LastDistanceCm);
			return false;
		}

		if (FVector::Distance(InLocations[0], InLocations[InLocations.Num() - 1]) < MinWrapSegmentLengthCm)
		{
			// Repeating the start point as the final sample is the classic closed-loop
			// baking bug: it produces a zero-length wrap segment whose direction is
			// undefined, right on the start/finish line where it does the most damage.
			OutError = TEXT("Closed loop repeats its first sample as its last. The wrap segment closes the loop; "
							"do not duplicate the origin.");
			return false;
		}
	}
	else if (!FMath::IsNearlyEqual(InTotalLengthCm, LastDistanceCm, DistanceEpsilonCm))
	{
		OutError = FString::Printf(
			TEXT("Open centerline TotalLengthCm (%f) must equal the last sample distance (%f)."),
			InTotalLengthCm, LastDistanceCm);
		return false;
	}

	// Commit.
	SampleLocations.Reset(InLocations.Num());
	SampleLocations.Append(InLocations.GetData(), InLocations.Num());
	SampleDistancesCm.Reset(InDistancesCm.Num());
	SampleDistancesCm.Append(InDistancesCm.GetData(), InDistancesCm.Num());
	TotalLengthCm = InTotalLengthCm;
	bClosedLoop = bInClosedLoop;
	return true;
}

bool FTrackCenterline::BuildFromPolyline(TArrayView<const FVector> InLocations, const bool bInClosedLoop, FString& OutError)
{
	using namespace TrackCenterlinePrivate;

	const int32 MinSamples = bInClosedLoop ? 3 : 2;
	if (InLocations.Num() < MinSamples)
	{
		OutError = FString::Printf(
			TEXT("%s centerline needs at least %d samples, got %d."),
			bInClosedLoop ? TEXT("Closed") : TEXT("Open"), MinSamples, InLocations.Num());
		return false;
	}

	for (int32 Index = 0; Index < InLocations.Num(); ++Index)
	{
		if (!IsFiniteVector(InLocations[Index]))
		{
			OutError = FString::Printf(TEXT("Sample %d location is not finite."), Index);
			return false;
		}
	}

	TArray<double> Distances;
	Distances.Reserve(InLocations.Num());
	Distances.Add(0.0);

	double Running = 0.0;
	for (int32 Index = 1; Index < InLocations.Num(); ++Index)
	{
		Running += FVector::Distance(InLocations[Index - 1], InLocations[Index]);
		Distances.Add(Running);
	}

	double Total = Running;
	if (bInClosedLoop)
	{
		Total += FVector::Distance(InLocations[InLocations.Num() - 1], InLocations[0]);
	}

	return Build(InLocations, Distances, Total, bInClosedLoop, OutError);
}

void FTrackCenterline::Reset()
{
	SampleLocations.Reset();
	SampleDistancesCm.Reset();
	TotalLengthCm = 0.0;
	bClosedLoop = false;
}

int32 FTrackCenterline::NumSegments() const
{
	if (SampleLocations.Num() < 2)
	{
		return 0;
	}

	return bClosedLoop ? SampleLocations.Num() : SampleLocations.Num() - 1;
}

double FTrackCenterline::GetSampleSpacingCm() const
{
	const int32 Segments = NumSegments();
	return Segments > 0 ? TotalLengthCm / static_cast<double>(Segments) : 0.0;
}

int32 FTrackCenterline::GetSegmentEndSampleIndex(const int32 SegmentIndex) const
{
	const int32 Next = SegmentIndex + 1;
	return (bClosedLoop && Next >= SampleLocations.Num()) ? 0 : Next;
}

double FTrackCenterline::GetSegmentLengthCm(const int32 SegmentIndex) const
{
	if (!SampleDistancesCm.IsValidIndex(SegmentIndex))
	{
		return 0.0;
	}

	// The final segment of a closed loop runs from the last sample back to the
	// origin, so its length is whatever arc length remains -- not a difference
	// between two entries in the table.
	if (SegmentIndex == SampleDistancesCm.Num() - 1)
	{
		return bClosedLoop ? (TotalLengthCm - SampleDistancesCm[SegmentIndex]) : 0.0;
	}

	return SampleDistancesCm[SegmentIndex + 1] - SampleDistancesCm[SegmentIndex];
}

double FTrackCenterline::WrapDistanceCm(const double DistanceCm) const
{
	if (!IsValid() || !FMath::IsFinite(DistanceCm))
	{
		// A non-finite progress value is a bug upstream, but returning NaN from here
		// would spread it into every transform and ranking that touches the result.
		// Clamp to the origin and let the caller's own validation catch the source.
		return 0.0;
	}

	if (!bClosedLoop)
	{
		return FMath::Clamp(DistanceCm, 0.0, TotalLengthCm);
	}

	// Fmod rather than a loop: a caller passing forty laps of distance (a teleport,
	// or an uninitialised value) must cost the same as a caller passing a sane one.
	double Wrapped = FMath::Fmod(DistanceCm, TotalLengthCm);
	if (Wrapped < 0.0)
	{
		Wrapped += TotalLengthCm;
	}

	// A tiny negative input can round to exactly TotalLengthCm after the addition
	// above, which would escape the [0, L) domain the whole API promises.
	if (Wrapped >= TotalLengthCm || Wrapped < 0.0)
	{
		Wrapped = 0.0;
	}

	return Wrapped;
}

double FTrackCenterline::GetSignedDistanceDeltaCm(const double FromCm, const double ToCm) const
{
	if (!IsValid() || !FMath::IsFinite(FromCm) || !FMath::IsFinite(ToCm))
	{
		return 0.0;
	}

	if (!bClosedLoop)
	{
		return FMath::Clamp(ToCm, 0.0, TotalLengthCm) - FMath::Clamp(FromCm, 0.0, TotalLengthCm);
	}

	const double HalfLengthCm = TotalLengthCm * 0.5;
	double Delta = WrapDistanceCm(ToCm) - WrapDistanceCm(FromCm);

	if (Delta > HalfLengthCm)
	{
		Delta -= TotalLengthCm;
	}
	else if (Delta <= -HalfLengthCm)
	{
		// <= rather than <, so that exactly half a lap always reports +L/2 and this
		// function stays a function instead of depending on argument order.
		Delta += TotalLengthCm;
	}

	return Delta;
}

int32 FTrackCenterline::FindSegmentIndex(const double WrappedDistanceCm) const
{
	// Last index whose sample distance is <= WrappedDistanceCm.
	int32 Low = 0;
	int32 High = SampleDistancesCm.Num() - 1;
	while (Low < High)
	{
		const int32 Mid = (Low + High + 1) / 2;
		if (SampleDistancesCm[Mid] <= WrappedDistanceCm)
		{
			Low = Mid;
		}
		else
		{
			High = Mid - 1;
		}
	}

	// An open centerline queried at exactly its total length lands on the final
	// SAMPLE, which is not the start of any SEGMENT.
	return FMath::Clamp(Low, 0, NumSegments() - 1);
}

FVector FTrackCenterline::GetLocationAtDistanceCm(const double DistanceCm) const
{
	if (!IsValid())
	{
		return FVector::ZeroVector;
	}

	const double Wrapped = WrapDistanceCm(DistanceCm);
	const int32 Segment = FindSegmentIndex(Wrapped);
	const double SegmentLengthCm = GetSegmentLengthCm(Segment);

	double Alpha = 0.0;
	if (SegmentLengthCm > 0.0)
	{
		Alpha = FMath::Clamp((Wrapped - SampleDistancesCm[Segment]) / SegmentLengthCm, 0.0, 1.0);
	}

	return FMath::Lerp(SampleLocations[Segment], SampleLocations[GetSegmentEndSampleIndex(Segment)], Alpha);
}

FVector FTrackCenterline::GetForwardAtDistanceCm(const double DistanceCm) const
{
	if (!IsValid())
	{
		return FVector::ForwardVector;
	}

	const int32 Segment = FindSegmentIndex(WrapDistanceCm(DistanceCm));
	const FVector Delta = SampleLocations[GetSegmentEndSampleIndex(Segment)] - SampleLocations[Segment];
	return Delta.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::ForwardVector);
}

FTransform FTrackCenterline::GetTransformAtDistanceCm(const double DistanceCm) const
{
	const FVector Location = GetLocationAtDistanceCm(DistanceCm);
	const FVector Forward = GetForwardAtDistanceCm(DistanceCm);

	// MakeFromXZ keeps X exactly on the direction of travel and orthogonalises Z
	// towards world up, which is what a car on a banked or climbing section wants.
	// If the track were ever exactly vertical the two inputs are parallel and the
	// roll becomes arbitrary; a circuit centerline cannot be, and the alternative
	// (silently substituting an axis) would hide a genuinely broken spline.
	const FRotator Rotation = FRotationMatrix::MakeFromXZ(Forward, FVector::UpVector).Rotator();
	return FTransform(Rotation, Location, FVector::OneVector);
}

FTrackCenterlineQuery FTrackCenterline::ProjectOntoSegments(
	const FVector& WorldLocationCm,
	const int32 FirstSegment,
	const int32 NumSegmentsToTest) const
{
	FTrackCenterlineQuery Result;

	const int32 SegmentCount = NumSegments();
	if (SegmentCount <= 0 || !TrackCenterlinePrivate::IsFiniteVector(WorldLocationCm) || NumSegmentsToTest <= 0)
	{
		return Result;
	}

	double BestSquaredDistance = TNumericLimits<double>::Max();
	int32 BestSegment = INDEX_NONE;
	double BestAlpha = 0.0;
	FVector BestPoint = FVector::ZeroVector;

	for (int32 Offset = 0; Offset < NumSegmentsToTest; ++Offset)
	{
		int32 Segment = FirstSegment + Offset;
		if (bClosedLoop)
		{
			Segment %= SegmentCount;
		}
		else if (Segment >= SegmentCount)
		{
			break;
		}

		const FVector& Start = SampleLocations[Segment];
		const FVector& End = SampleLocations[GetSegmentEndSampleIndex(Segment)];
		const FVector Along = End - Start;
		const double AlongSquared = Along.SizeSquared();

		double Alpha = 0.0;
		if (AlongSquared > UE_DOUBLE_SMALL_NUMBER)
		{
			Alpha = FMath::Clamp(FVector::DotProduct(WorldLocationCm - Start, Along) / AlongSquared, 0.0, 1.0);
		}

		const FVector Point = Start + Along * Alpha;
		const double SquaredDistance = FVector::DistSquared(WorldLocationCm, Point);
		if (SquaredDistance < BestSquaredDistance)
		{
			BestSquaredDistance = SquaredDistance;
			BestSegment = Segment;
			BestAlpha = Alpha;
			BestPoint = Point;
		}
	}

	if (BestSegment == INDEX_NONE)
	{
		return Result;
	}

	const FVector Along = SampleLocations[GetSegmentEndSampleIndex(BestSegment)] - SampleLocations[BestSegment];
	const FVector Forward = Along.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::ForwardVector);

	// Unreal is left-handed with Z up, so the right-hand side of the direction of
	// travel is Up x Forward. Verified rather than remembered: Forward = +X gives
	// Up x Forward = (0,0,1) x (1,0,0) = (0,1,0) = +Y, which is Unreal's right.
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
	const FVector ToPoint = WorldLocationCm - BestPoint;

	Result.bValid = true;
	Result.DistanceAlongCm = WrapDistanceCm(SampleDistancesCm[BestSegment] + BestAlpha * GetSegmentLengthCm(BestSegment));
	Result.Location = BestPoint;
	Result.Forward = Forward;
	Result.LateralOffsetCm = FVector::DotProduct(ToPoint, Right);
	Result.DistanceToCenterlineCm = FMath::Sqrt(BestSquaredDistance);
	return Result;
}

FTrackCenterlineQuery FTrackCenterline::FindNearest(const FVector& WorldLocationCm) const
{
	return ProjectOntoSegments(WorldLocationCm, 0, NumSegments());
}

FTrackCenterlineQuery FTrackCenterline::FindNearestNear(
	const FVector& WorldLocationCm,
	const double HintDistanceCm,
	const double SearchWindowCm) const
{
	const int32 SegmentCount = NumSegments();
	if (SegmentCount <= 0)
	{
		return FTrackCenterlineQuery();
	}

	// Losing precision is recoverable; reporting "not on the track" is not. Every
	// unusable hint therefore degrades to the global search rather than failing.
	const bool bHintUsable =
		FMath::IsFinite(HintDistanceCm)
		&& FMath::IsFinite(SearchWindowCm)
		&& SearchWindowCm > 0.0
		&& (SearchWindowCm * 2.0) < TotalLengthCm;

	if (!bHintUsable)
	{
		return FindNearest(WorldLocationCm);
	}

	const double WindowStartCm = WrapDistanceCm(HintDistanceCm - SearchWindowCm);
	const int32 FirstSegment = FindSegmentIndex(WindowStartCm);

	// Walk forward until the window is covered. Counting segments rather than
	// dividing by the nominal spacing keeps this correct for a non-uniformly
	// sampled centerline, which Build() permits.
	const double SpanCm = SearchWindowCm * 2.0;
	double CoveredCm = GetSegmentLengthCm(FirstSegment);
	int32 SegmentsToTest = 1;
	while (CoveredCm < SpanCm && SegmentsToTest < SegmentCount)
	{
		int32 Segment = FirstSegment + SegmentsToTest;
		if (bClosedLoop)
		{
			Segment %= SegmentCount;
		}
		else if (Segment >= SegmentCount)
		{
			break;
		}

		CoveredCm += GetSegmentLengthCm(Segment);
		++SegmentsToTest;
	}

	// One extra segment of margin: the window edge almost never lands on a sample
	// boundary, and the segment it lands inside is only partially covered.
	SegmentsToTest = FMath::Min(SegmentsToTest + 1, SegmentCount);

	return ProjectOntoSegments(WorldLocationCm, FirstSegment, SegmentsToTest);
}
