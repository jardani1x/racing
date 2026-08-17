// Copyright RacingSim. All Rights Reserved.

#include "Race/TrackDefinitionActor.h"

#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Core/RacingSimLog.h"

namespace TrackDefinitionPrivate
{
	/** Tolerance for "this sector starts at the line", centimetres. */
	constexpr double DistanceEpsilonCm = 1.0e-4;

	/**
	 * Upper bound on baked samples and on generated reset poses.
	 *
	 * Not a design limit -- a 5 km track at the 10 cm minimum spacing is 50,000
	 * samples, well inside this. It exists so that a mistyped spacing (0.001 cm) or
	 * a spline whose length has gone wrong cannot turn a rebuild into an
	 * out-of-memory crash on the GPU worker.
	 */
	constexpr int32 MaxGeneratedSamples = 200000;

	/**
	 * Hash one double.
	 *
	 * Everything below funnels through this rather than through GetTypeHash(FVector)
	 * or GetTypeHash(FTransform). Neither of those resolves here (verified by a
	 * failed build, C2665, not assumed), and more importantly hashing a vector's
	 * memory would fold in padding and make the hash depend on the compiler's
	 * layout rather than on the authored numbers.
	 *
	 * Same caveats URaceRulesetDataAsset::ComputeContentHash records: this hashes
	 * the bit pattern, so +0.0 and -0.0 hash differently despite comparing equal,
	 * and a NaN hashes stably but compares unequal to itself. Validate() rejects
	 * non-finite authored values before such a hash can reach a result.
	 */
	uint32 HashDouble(const uint32 Hash, const double Value)
	{
		return HashCombine(Hash, GetTypeHash(Value));
	}

	uint32 HashVector(uint32 Hash, const FVector& Vector)
	{
		Hash = HashDouble(Hash, Vector.X);
		Hash = HashDouble(Hash, Vector.Y);
		return HashDouble(Hash, Vector.Z);
	}

	/** FTransform has no usable GetTypeHash; hash the three components that define it. */
	uint32 HashTransform(uint32 Hash, const FTransform& Transform)
	{
		Hash = HashVector(Hash, Transform.GetLocation());

		// The quaternion rather than the rotator: two rotators can name the same
		// orientation, and the quaternion is what the transform actually stores.
		const FQuat Rotation = Transform.GetRotation();
		Hash = HashDouble(Hash, Rotation.X);
		Hash = HashDouble(Hash, Rotation.Y);
		Hash = HashDouble(Hash, Rotation.Z);
		Hash = HashDouble(Hash, Rotation.W);

		return HashVector(Hash, Transform.GetScale3D());
	}

	/** Last index in a sorted ascending array whose value is <= Value, or INDEX_NONE if the array is empty. */
	int32 FindLastAtOrBefore(const TArray<double>& SortedAscending, const double Value)
	{
		if (SortedAscending.Num() == 0)
		{
			return INDEX_NONE;
		}

		if (Value < SortedAscending[0])
		{
			return INDEX_NONE;
		}

		int32 Low = 0;
		int32 High = SortedAscending.Num() - 1;
		while (Low < High)
		{
			const int32 Mid = (Low + High + 1) / 2;
			if (SortedAscending[Mid] <= Value)
			{
				Low = Mid;
			}
			else
			{
				High = Mid - 1;
			}
		}

		return Low;
	}
}

ATrackDefinitionActor::ATrackDefinitionActor()
{
	// Nothing here ticks. The track is data; the systems that read it tick.
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	TrackRoot = CreateDefaultSubobject<USceneComponent>(TEXT("TrackRoot"));
	RootComponent = TrackRoot;

	CenterlineSpline = CreateDefaultSubobject<USplineComponent>(TEXT("CenterlineSpline"));
	CenterlineSpline->SetupAttachment(TrackRoot);

	// A circuit closes. An open centerline is rejected by Validate(), but defaulting
	// to closed means a freshly placed actor is already the shape it will be.
	CenterlineSpline->SetClosedLoop(true);

	// One sector, starting at the line. Set here rather than as an inline TArray
	// initialiser so the "element 0 is exactly 0" invariant is established in code
	// that a reader of the constructor can see. The prototype circuit authors three.
	SectorStartDistancesCm.Add(0.0);
}

// ===========================================================================
// Lifecycle
// ===========================================================================

void ATrackDefinitionActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildTrackData();
}

void ATrackDefinitionActor::PostLoad()
{
	Super::PostLoad();
	RebuildTrackData();
}

void ATrackDefinitionActor::BeginPlay()
{
	Super::BeginPlay();

	// Rebuild rather than trust: OnConstruction ran before any runtime property
	// override from a construction script or a spawn-time setter could apply.
	RebuildTrackData();

	FString Reason;
	if (!Validate(Reason))
	{
		// Loud, once, at the only moment where it can still be acted on. Not an
		// ensure: a graybox level with a half-authored track must still open.
		UE_LOG(LogRacingRace, Error, TEXT("Track '%s' failed validation at BeginPlay: %s"),
			*TrackId.ToString(), *Reason);
	}
}

#if WITH_EDITOR
void ATrackDefinitionActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Every authored property on this actor feeds the bake, so there is no
	// worthwhile property-name filter here; rebuilding unconditionally is cheaper
	// than a filter that silently misses a new field added by a later ticket.
	RebuildTrackData();
}
#endif

void ATrackDefinitionActor::EnsureTrackDataBuilt() const
{
	if (!bTrackDataBuilt)
	{
		const_cast<ATrackDefinitionActor*>(this)->RebuildTrackData();
	}
}

// ===========================================================================
// Bake
// ===========================================================================

bool ATrackDefinitionActor::RebuildTrackData()
{
	using namespace TrackDefinitionPrivate;

	bTrackDataBuilt = false;
	BakedCenterline.Reset();
	GridSlotTransforms.Reset();
	ResetSampleTransforms.Reset();
	ResetSampleDistancesCm.Reset();

	if (!CenterlineSpline)
	{
		UE_LOG(LogRacingRace, Warning, TEXT("Track '%s' has no centerline spline component."), *TrackId.ToString());
		return false;
	}

	// PRECISION BOUNDARY, and it is the engine's, not ours. USplineComponent's
	// length and distance API is float-based (GetSplineLength returns float;
	// GetLocationAtDistanceAlongSpline takes float), while everything downstream of
	// the bake is double. Float carries ~7 significant digits, so on a 5 km circuit
	// (500,000 cm) the quantum is about 0.03 cm -- three orders of magnitude below
	// the 100 cm sample spacing and irrelevant to any race decision. Recorded rather
	// than hidden, because the widening happens silently and someone will eventually
	// wonder why two "identical" distances differ in the ninth digit.
	const double SplineLengthCm = static_cast<double>(CenterlineSpline->GetSplineLength());
	if (!FMath::IsFinite(SplineLengthCm) || SplineLengthCm <= 0.0)
	{
		UE_LOG(LogRacingRace, Warning, TEXT("Track '%s' centerline has no usable length (%f cm)."),
			*TrackId.ToString(), SplineLengthCm);
		return false;
	}

	double SpacingCm = CenterlineSampleSpacingCm;
	if (!FMath::IsFinite(SpacingCm) || SpacingCm <= 0.0)
	{
		UE_LOG(LogRacingRace, Warning,
			TEXT("Track '%s' CenterlineSampleSpacingCm is %f; falling back to 100 cm for this bake. "
				 "Fix the authored value -- the content hash records what was authored, not what was used."),
			*TrackId.ToString(), SpacingCm);
		SpacingCm = 100.0;
	}

	const bool bClosed = CenterlineSpline->IsClosedLoop();
	const int32 MinSamples = bClosed ? 3 : 2;

	int32 NumSamples = FMath::Max(MinSamples, FMath::CeilToInt32(SplineLengthCm / SpacingCm));
	if (NumSamples > MaxGeneratedSamples)
	{
		UE_LOG(LogRacingRace, Warning,
			TEXT("Track '%s' would bake %d samples at %f cm spacing; clamping to %d. Accuracy is reduced."),
			*TrackId.ToString(), NumSamples, SpacingCm, MaxGeneratedSamples);
		NumSamples = MaxGeneratedSamples;
	}

	// Sample spacing is derived from the sample COUNT, not used directly, so that
	// the samples divide the lap exactly. For a closed loop the final sample must
	// stop short of the origin -- FTrackCenterline's wrapping segment closes the
	// gap, and duplicating the origin would create a zero-length segment right on
	// the start/finish line.
	const double StepCm = bClosed
		? SplineLengthCm / static_cast<double>(NumSamples)
		: SplineLengthCm / static_cast<double>(NumSamples - 1);

	TArray<FVector> Locations;
	TArray<double> DistancesCm;
	Locations.Reserve(NumSamples);
	DistancesCm.Reserve(NumSamples);

	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		const double DistanceCm = (Index == NumSamples - 1 && !bClosed)
			? SplineLengthCm                              // pin the endpoint exactly
			: static_cast<double>(Index) * StepCm;

		// World space: cars are queried in world space, and baking in local space
		// would mean transforming on every query instead of once here.
		Locations.Add(CenterlineSpline->GetLocationAtDistanceAlongSpline(
			static_cast<float>(DistanceCm), ESplineCoordinateSpace::World));
		DistancesCm.Add(DistanceCm);
	}

	FString BuildError;
	if (!BakedCenterline.Build(Locations, DistancesCm, SplineLengthCm, bClosed, BuildError))
	{
		UE_LOG(LogRacingRace, Warning, TEXT("Track '%s' centerline bake failed: %s"), *TrackId.ToString(), *BuildError);
		return false;
	}

	RebuildGridSlots();
	RebuildResetSamples();

	bTrackDataBuilt = true;
	return true;
}

FTransform ATrackDefinitionActor::MakePoseAtDistance(const double DistanceCm, const double LateralOffsetCm) const
{
	FTransform Pose = BakedCenterline.GetTransformAtDistanceCm(DistanceCm);

	// Offset along the POSE's own axes rather than world axes, so a banked or
	// climbing section lifts the car perpendicular to the road instead of burying
	// one side of it.
	const FVector Right = Pose.GetRotation().GetRightVector();
	const FVector Up = Pose.GetRotation().GetUpVector();
	Pose.SetLocation(Pose.GetLocation() + Right * LateralOffsetCm + Up * PoseHeightOffsetCm);
	return Pose;
}

void ATrackDefinitionActor::RebuildGridSlots()
{
	const int32 SlotCount = FMath::Max(0, NumGridSlots);
	GridSlotTransforms.Reserve(SlotCount);

	const double SpacingCm = (FMath::IsFinite(GridSlotSpacingCm) && GridSlotSpacingCm > 0.0) ? GridSlotSpacingCm : 800.0;
	const double SetbackCm = (FMath::IsFinite(GridPoleSetbackCm) && GridPoleSetbackCm >= 0.0) ? GridPoleSetbackCm : 0.0;
	const double LateralCm = (FMath::IsFinite(GridSlotLateralOffsetCm) && GridSlotLateralOffsetCm >= 0.0) ? GridSlotLateralOffsetCm : 0.0;

	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		// Negative distance means "behind the line"; WrapDistanceCm folds it around
		// the loop, which is why the grid does not need to know the track length.
		const double DistanceCm = BakedCenterline.WrapDistanceCm(-(SetbackCm + static_cast<double>(Slot) * SpacingCm));

		// Even slots left, odd slots right. Pole is on the left by convention, and
		// the convention is arbitrary but must be FIXED, or the grid mirrors itself
		// whenever someone reorders this.
		const double LateralOffsetCm = (Slot % 2 == 0) ? -LateralCm : LateralCm;

		GridSlotTransforms.Add(MakePoseAtDistance(DistanceCm, LateralOffsetCm));
	}
}

void ATrackDefinitionActor::RebuildResetSamples()
{
	using namespace TrackDefinitionPrivate;

	const double LengthCm = BakedCenterline.GetLengthCm();
	const double RequestedSpacingCm =
		(FMath::IsFinite(ResetSampleSpacingCm) && ResetSampleSpacingCm > 0.0) ? ResetSampleSpacingCm : 2500.0;

	int32 SampleCount = FMath::Max(1, FMath::FloorToInt32(LengthCm / RequestedSpacingCm));
	SampleCount = FMath::Min(SampleCount, MaxGeneratedSamples);

	// Derive the actual step from the count, so the gap between the last sample and
	// the start/finish line matches every other gap. Using the requested spacing
	// directly would leave a remainder there, i.e. the one place on the track where
	// a reset costs almost twice as much distance -- and it is the place where a
	// reset is most likely to change a result.
	const double StepCm = LengthCm / static_cast<double>(SampleCount);

	ResetSampleTransforms.Reserve(SampleCount);
	ResetSampleDistancesCm.Reserve(SampleCount);

	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		const double DistanceCm = static_cast<double>(Index) * StepCm;
		ResetSampleDistancesCm.Add(DistanceCm);
		ResetSampleTransforms.Add(MakePoseAtDistance(DistanceCm, 0.0));
	}
}

// ===========================================================================
// Queries
// ===========================================================================

const FTrackCenterline& ATrackDefinitionActor::GetCenterline() const
{
	EnsureTrackDataBuilt();
	return BakedCenterline;
}

double ATrackDefinitionActor::GetTrackLengthCm() const
{
	return GetCenterline().GetLengthCm();
}

double ATrackDefinitionActor::GetTrackLengthMetres() const
{
	// UNIT BOUNDARY: storage is Unreal centimetres (CORE-002); this is SI metres.
	return GetCenterline().GetLengthMetres();
}

double ATrackDefinitionActor::GetTrackLengthKilometres() const
{
	// UNIT BOUNDARY: storage is Unreal centimetres (CORE-002); this is kilometres.
	return GetCenterline().GetLengthKilometres();
}

FTransform ATrackDefinitionActor::GetStartFinishTransform() const
{
	return GetCenterline().GetTransformAtDistanceCm(0.0);
}

int32 ATrackDefinitionActor::GetNumSectors() const
{
	return SectorStartDistancesCm.Num();
}

int32 ATrackDefinitionActor::GetSectorIndexAtDistanceCm(const double DistanceCm) const
{
	const FTrackCenterline& Centerline = GetCenterline();
	if (!Centerline.IsValid() || SectorStartDistancesCm.Num() == 0)
	{
		return INDEX_NONE;
	}

	const double WrappedCm = Centerline.WrapDistanceCm(DistanceCm);

	// SectorStartDistancesCm[0] is required to be 0 by Validate(), so a wrapped
	// distance always lands in a sector. FindLastAtOrBefore returning INDEX_NONE
	// here therefore means the data failed validation; report sector 0 rather than
	// an invalid index, because a wrong sector is recoverable and an out-of-range
	// index read by a caller is not.
	const int32 Index = TrackDefinitionPrivate::FindLastAtOrBefore(SectorStartDistancesCm, WrappedCm);
	return Index == INDEX_NONE ? 0 : Index;
}

double ATrackDefinitionActor::GetSectorStartDistanceCm(const int32 SectorIndex) const
{
	return SectorStartDistancesCm.IsValidIndex(SectorIndex) ? SectorStartDistancesCm[SectorIndex] : 0.0;
}

double ATrackDefinitionActor::GetSectorLengthCm(const int32 SectorIndex) const
{
	if (!SectorStartDistancesCm.IsValidIndex(SectorIndex))
	{
		return 0.0;
	}

	if (SectorIndex == SectorStartDistancesCm.Num() - 1)
	{
		// The last sector runs back to the start/finish origin.
		return GetCenterline().GetLengthCm() - SectorStartDistancesCm[SectorIndex];
	}

	return SectorStartDistancesCm[SectorIndex + 1] - SectorStartDistancesCm[SectorIndex];
}

int32 ATrackDefinitionActor::GetNumGridSlots() const
{
	EnsureTrackDataBuilt();
	return GridSlotTransforms.Num();
}

FTransform ATrackDefinitionActor::GetGridSlotTransform(const int32 SlotIndex) const
{
	EnsureTrackDataBuilt();
	return GridSlotTransforms.IsValidIndex(SlotIndex) ? GridSlotTransforms[SlotIndex] : FTransform::Identity;
}

int32 ATrackDefinitionActor::GetNumResetSamples() const
{
	EnsureTrackDataBuilt();
	return ResetSampleTransforms.Num();
}

FTransform ATrackDefinitionActor::GetResetSampleTransform(const int32 SampleIndex) const
{
	EnsureTrackDataBuilt();
	return ResetSampleTransforms.IsValidIndex(SampleIndex) ? ResetSampleTransforms[SampleIndex] : FTransform::Identity;
}

FTransform ATrackDefinitionActor::GetResetTransformAtOrBeforeDistanceCm(const double DistanceCm, int32& OutIndex) const
{
	OutIndex = INDEX_NONE;

	const FTrackCenterline& Centerline = GetCenterline();
	if (!Centerline.IsValid() || ResetSampleTransforms.Num() == 0)
	{
		return FTransform::Identity;
	}

	const double WrappedCm = Centerline.WrapDistanceCm(DistanceCm);

	// Sample 0 sits at distance 0, so every wrapped distance has a predecessor and
	// this cannot fail to find one.
	int32 Index = TrackDefinitionPrivate::FindLastAtOrBefore(ResetSampleDistancesCm, WrappedCm);
	if (Index == INDEX_NONE)
	{
		Index = 0;
	}

	OutIndex = Index;
	return ResetSampleTransforms[Index];
}

FTrackCenterlineQuery ATrackDefinitionActor::FindNearestCenterlinePoint(const FVector& WorldLocationCm) const
{
	return GetCenterline().FindNearest(WorldLocationCm);
}

FTrackCenterlineQuery ATrackDefinitionActor::FindNearestCenterlinePointNear(
	const FVector& WorldLocationCm,
	const double HintDistanceCm,
	const double SearchWindowCm) const
{
	return GetCenterline().FindNearestNear(WorldLocationCm, HintDistanceCm, SearchWindowCm);
}

double ATrackDefinitionActor::GetRaceProgressCm(const int32 LapsCompleted, const double DistanceAlongCm) const
{
	const FTrackCenterline& Centerline = GetCenterline();
	if (!Centerline.IsValid())
	{
		return 0.0;
	}

	// Negative lap counts are clamped rather than trusted: this is a ranking key,
	// and a negative one would sort a bugged car ahead of the field.
	const int32 Laps = FMath::Max(0, LapsCompleted);
	return static_cast<double>(Laps) * Centerline.GetLengthCm() + Centerline.WrapDistanceCm(DistanceAlongCm);
}

// ===========================================================================
// Identity and validation
// ===========================================================================

uint32 ATrackDefinitionActor::ComputeContentHash() const
{
	using namespace TrackDefinitionPrivate;

	uint32 Hash = GetTypeHash(TrackId);
	Hash = HashDouble(Hash, CenterlineSampleSpacingCm);

	// The actor's own transform places the whole circuit, so two otherwise identical
	// tracks at different world origins are different content -- everything baked
	// below is in world space.
	Hash = HashTransform(Hash, GetActorTransform());

	if (CenterlineSpline)
	{
		Hash = HashCombine(Hash, GetTypeHash(static_cast<int32>(CenterlineSpline->IsClosedLoop() ? 1 : 0)));

		const int32 PointCount = CenterlineSpline->GetNumberOfSplinePoints();
		Hash = HashCombine(Hash, GetTypeHash(PointCount));

		for (int32 Point = 0; Point < PointCount; ++Point)
		{
			// Local space: the actor transform is already hashed above, and hashing
			// world positions as well would double-count a move.
			Hash = HashVector(Hash, CenterlineSpline->GetLocationAtSplinePoint(Point, ESplineCoordinateSpace::Local));
			Hash = HashVector(Hash, CenterlineSpline->GetArriveTangentAtSplinePoint(Point, ESplineCoordinateSpace::Local));
			Hash = HashVector(Hash, CenterlineSpline->GetLeaveTangentAtSplinePoint(Point, ESplineCoordinateSpace::Local));

			// Point type changes the curve between the same two positions, so it is
			// geometry, not presentation.
			Hash = HashCombine(Hash, GetTypeHash(static_cast<int32>(CenterlineSpline->GetSplinePointType(Point))));
		}
	}

	Hash = HashCombine(Hash, GetTypeHash(SectorStartDistancesCm.Num()));
	for (const double SectorStartCm : SectorStartDistancesCm)
	{
		Hash = HashDouble(Hash, SectorStartCm);
	}

	Hash = HashCombine(Hash, GetTypeHash(NumGridSlots));
	Hash = HashDouble(Hash, GridPoleSetbackCm);
	Hash = HashDouble(Hash, GridSlotSpacingCm);
	Hash = HashDouble(Hash, GridSlotLateralOffsetCm);
	Hash = HashDouble(Hash, PoseHeightOffsetCm);
	Hash = HashDouble(Hash, ResetSampleSpacingCm);

	// Any authored field added by a later ticket MUST be combined in here, and
	// TrackSchemaVersion MUST be bumped. See the header.
	return Hash;
}

FRacingContentVersion ATrackDefinitionActor::GetContentVersion() const
{
	FRacingContentVersion Version;
	Version.AssetId = TrackId;
	Version.SchemaVersion = TrackSchemaVersion;
	Version.ContentHash = ComputeContentHash();
	return Version;
}

bool ATrackDefinitionActor::Validate(FString& OutReason) const
{
	using namespace TrackDefinitionPrivate;

	if (TrackId.IsNone())
	{
		OutReason = TEXT("TrackId is None. An unnamed track cannot identify itself on a result.");
		return false;
	}

	if (!CenterlineSpline)
	{
		OutReason = TEXT("No centerline spline component.");
		return false;
	}

	if (!CenterlineSpline->IsClosedLoop())
	{
		OutReason = TEXT("Centerline spline is not a closed loop. A circuit must close; "
						 "lap distance arithmetic and grid wrap-around both assume it.");
		return false;
	}

	if (CenterlineSpline->GetNumberOfSplinePoints() < 3)
	{
		OutReason = FString::Printf(
			TEXT("Centerline has %d spline points; a closed loop needs at least 3."),
			CenterlineSpline->GetNumberOfSplinePoints());
		return false;
	}

	if (!FMath::IsFinite(CenterlineSampleSpacingCm) || CenterlineSampleSpacingCm <= 0.0)
	{
		OutReason = FString::Printf(TEXT("CenterlineSampleSpacingCm must be finite and positive, got %f."), CenterlineSampleSpacingCm);
		return false;
	}

	EnsureTrackDataBuilt();

	if (!BakedCenterline.IsValid())
	{
		OutReason = TEXT("Centerline failed to bake. See the LogRacingRace warning for the reason.");
		return false;
	}

	const double LengthCm = BakedCenterline.GetLengthCm();

	if (BakedCenterline.NumSegments() < 3)
	{
		OutReason = FString::Printf(
			TEXT("Centerline baked to only %d segments at %f cm spacing over %f cm; too coarse to query."),
			BakedCenterline.NumSegments(), CenterlineSampleSpacingCm, LengthCm);
		return false;
	}

	// -- Sectors ------------------------------------------------------------

	if (SectorStartDistancesCm.Num() < 1)
	{
		OutReason = TEXT("No sectors. A track needs at least one sector starting at the line.");
		return false;
	}

	if (!FMath::IsNearlyZero(SectorStartDistancesCm[0], DistanceEpsilonCm))
	{
		OutReason = FString::Printf(
			TEXT("SectorStartDistancesCm[0] must be 0 (the start/finish line), got %f."),
			SectorStartDistancesCm[0]);
		return false;
	}

	for (int32 Index = 0; Index < SectorStartDistancesCm.Num(); ++Index)
	{
		const double SectorStartCm = SectorStartDistancesCm[Index];

		if (!FMath::IsFinite(SectorStartCm))
		{
			OutReason = FString::Printf(TEXT("SectorStartDistancesCm[%d] is not finite."), Index);
			return false;
		}

		if (SectorStartCm >= LengthCm)
		{
			OutReason = FString::Printf(
				TEXT("SectorStartDistancesCm[%d] is %f cm, at or past the lap length %f cm."),
				Index, SectorStartCm, LengthCm);
			return false;
		}

		if (Index > 0 && SectorStartCm <= SectorStartDistancesCm[Index - 1])
		{
			OutReason = FString::Printf(
				TEXT("SectorStartDistancesCm must strictly increase; [%d] (%f) <= [%d] (%f)."),
				Index, SectorStartCm, Index - 1, SectorStartDistancesCm[Index - 1]);
			return false;
		}
	}

	// -- Grid ---------------------------------------------------------------

	if (NumGridSlots < 1)
	{
		OutReason = FString::Printf(TEXT("NumGridSlots is %d; a race needs at least one grid slot."), NumGridSlots);
		return false;
	}

	if (!FMath::IsFinite(GridSlotSpacingCm) || GridSlotSpacingCm <= 0.0)
	{
		OutReason = FString::Printf(TEXT("GridSlotSpacingCm must be finite and positive, got %f."), GridSlotSpacingCm);
		return false;
	}

	if (!FMath::IsFinite(GridPoleSetbackCm) || GridPoleSetbackCm < 0.0)
	{
		OutReason = FString::Printf(TEXT("GridPoleSetbackCm must be finite and non-negative, got %f."), GridPoleSetbackCm);
		return false;
	}

	if (!FMath::IsFinite(GridSlotLateralOffsetCm) || GridSlotLateralOffsetCm < 0.0)
	{
		OutReason = FString::Printf(TEXT("GridSlotLateralOffsetCm must be finite and non-negative, got %f."), GridSlotLateralOffsetCm);
		return false;
	}

	if (!FMath::IsFinite(PoseHeightOffsetCm) || PoseHeightOffsetCm < 0.0)
	{
		OutReason = FString::Printf(TEXT("PoseHeightOffsetCm must be finite and non-negative, got %f."), PoseHeightOffsetCm);
		return false;
	}

	const double GridExtentCm = GridPoleSetbackCm + static_cast<double>(NumGridSlots - 1) * GridSlotSpacingCm;
	if (GridExtentCm >= LengthCm)
	{
		// The grid would wrap past the start/finish line and the back of the grid
		// would sit AHEAD of pole. Nothing crashes; the race is simply nonsense.
		OutReason = FString::Printf(
			TEXT("Grid extends %f cm back from the line but the lap is only %f cm; the grid would wrap onto itself."),
			GridExtentCm, LengthCm);
		return false;
	}

	// -- Reset samples ------------------------------------------------------

	if (!FMath::IsFinite(ResetSampleSpacingCm) || ResetSampleSpacingCm <= 0.0)
	{
		OutReason = FString::Printf(TEXT("ResetSampleSpacingCm must be finite and positive, got %f."), ResetSampleSpacingCm);
		return false;
	}

	if (ResetSampleSpacingCm > LengthCm)
	{
		OutReason = FString::Printf(
			TEXT("ResetSampleSpacingCm (%f cm) exceeds the lap length (%f cm); the track would have a single reset pose."),
			ResetSampleSpacingCm, LengthCm);
		return false;
	}

	return true;
}
