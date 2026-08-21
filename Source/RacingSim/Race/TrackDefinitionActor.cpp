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

	// TRACK-002, closing TRACK-001 M5: RECORD THE OUTCOME OF *THIS* BAKE SPECIFICALLY.
	//
	// M5's doubt is narrow and real: this function reads USplineComponent geometry, and
	// the engine does not guarantee the component's PostLoad has run before the owning
	// actor's. If the spline were not ready here, the bake would fail -- and nothing
	// downstream would tell you, because a placed actor is re-baked again moments later
	// when the editor initialises the loaded world and re-runs OnConstruction. The track
	// would be perfectly healthy by the time anyone could look at it, and the load-order
	// bug would be invisible until the day the second bake stopped happening.
	//
	// BakeAttemptCount alone cannot answer the question for that reason: by the time a
	// test can observe it, it is legitimately 2. So the load-time result is latched here,
	// at the only moment it is knowable.
	//
	// This is not test scaffolding in runtime code. "Did this placed track bake when the
	// level loaded" is a question a level-validation pass, a race director refusing to
	// start a session, or anyone debugging a broken map has a direct interest in; the
	// shipping game is willing to carry two ints.
	PostLoadBakeAttemptIndex = BakeAttemptCount + 1;
	bPostLoadBakeSucceeded = RebuildTrackData();
}

void ATrackDefinitionActor::BeginPlay()
{
	Super::BeginPlay();

	// Rebuild rather than trust: OnConstruction ran before any runtime property
	// override from a construction script or a spawn-time setter could apply.
	RebuildTrackData();

	// Through the CACHE rather than through Validate() directly, so the load path leaves
	// the cache warm for whoever asks next. A race director deciding whether to go green
	// then pays a content hash, not a second full validation -- which is the whole point
	// of TRACK-001 M7's fix, and it would be a poor one if the first in-project caller
	// bypassed it.
	FString Reason;
	if (!GetCachedValidation(Reason))
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
	// M3: the const_cast below mutates six containers and five scalars. That is only
	// sound because this class is game-thread-only, and "game thread only" was
	// previously asserted by a comment. Assert it in code instead -- a background task
	// calling any query would otherwise corrupt the arrays a racing car is reading.
	check(IsInGameThread());

	// H1: gate on ATTEMPTED, not on SUCCEEDED.
	//
	// Gating on bTrackDataBuilt meant a FAILED bake was retried by every single query.
	// A freshly placed actor with an unauthored spline fails to bake, and that is its
	// normal state for as long as it takes someone to draw a circuit; each retry ran
	// the full sample loop with two heap allocations and emitted another warning. One
	// attempt, then cache the outcome. RebuildTrackData() is the only retry path, and
	// it is called exactly where the spline data can actually have changed.
	if (!bBakeAttempted)
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

	// M3: see EnsureTrackDataBuilt. This mutates every derived container on the actor.
	check(IsInGameThread());

	// H1: record the ATTEMPT before anything can fail, so a failed bake latches and
	// EnsureTrackDataBuilt() stops re-entering here on every query.
	bBakeAttempted = true;
	++BakeAttemptCount;

	bTrackDataBuilt = false;
	BakedCenterline.Reset();
	BakedCheckpointGates.Reset();
	CheckpointGateBakeError.Reset();
	GeneratedGateClampNote.Reset();
	GridSlotTransforms.Reset();
	GridSlotDistancesCm.Reset();
	ResetSampleTransforms.Reset();
	ResetSampleDistancesCm.Reset();
	EffectiveSampleCount = 0;
	EffectiveStepCm = 0.0;

	// H1: one-shot failure logging. Attempts are now bounded by the lifecycle hooks
	// rather than by query volume, but an explicit rebuild loop (a construction script
	// re-running, a tool re-authoring) would still emit one warning per call. Log the
	// first failure and stay quiet until something succeeds, so the reason is visible
	// exactly once and the log stays readable.
	auto LogBakeFailure = [this](const FString& Message)
	{
		if (!bBakeFailureLogged)
		{
			bBakeFailureLogged = true;
			UE_LOG(LogRacingRace, Warning, TEXT("%s (further bake failures for this track are suppressed "
				"until a bake succeeds)"), *Message);
		}
	};

	if (!CenterlineSpline)
	{
		LogBakeFailure(FString::Printf(TEXT("Track '%s' has no centerline spline component."), *TrackId.ToString()));
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
		LogBakeFailure(FString::Printf(TEXT("Track '%s' centerline has no usable length (%f cm)."),
			*TrackId.ToString(), SplineLengthCm));
		return false;
	}

	double SpacingCm = CenterlineSampleSpacingCm;
	if (!FMath::IsFinite(SpacingCm) || SpacingCm <= 0.0)
	{
		UE_LOG(LogRacingRace, Warning,
			TEXT("Track '%s' CenterlineSampleSpacingCm is %f; falling back to 100 cm for this bake. "
				 "Fix the authored value -- Validate() rejects it outright, and the content hash "
				 "records BOTH the authored value and the effective one."),
			*TrackId.ToString(), SpacingCm);
		SpacingCm = 100.0;
	}

	const bool bClosed = CenterlineSpline->IsClosedLoop();
	const int32 MinSamples = bClosed ? 3 : 2;

	// TRACK-002, closing TRACK-001 L4: COMPUTE IN DOUBLE, COMPARE, THEN CAST.
	//
	// This was FMath::Max(MinSamples, FMath::CeilToInt32(SplineLengthCm / SpacingCm)),
	// with a comment claiming the MaxGeneratedSamples clamp below made an absurd spacing
	// safe. The outcome was safe; the MECHANISM was not the documented one. For a spacing
	// small enough that the quotient exceeds 2^31 -- 0.001 cm on a 5 km circuit reaches
	// 5e8, and a mistyped 1e-9 reaches 5e14 -- CeilToInt32 casts an out-of-range double
	// to int32, which is UNDEFINED BEHAVIOUR in C++ and in practice yields an
	// implementation-defined value that is frequently INT32_MIN. The result then survived
	// only because FMath::Max floored it back to MinSamples, i.e. the code degraded to a
	// 3-sample bake by way of an integer wraparound rather than by the clamp its own
	// comment pointed at. A guard nobody can read correctly is a guard that will be
	// "simplified" away.
	//
	// The quotient is now formed and range-checked as a double, where the arithmetic is
	// total: a huge quotient is a huge double, and infinity and NaN are values rather
	// than traps. Only a value already proven to be inside int32 is cast.
	const double RequestedSampleCount = FMath::CeilToDouble(SplineLengthCm / SpacingCm);

	int32 NumSamples;
	if (!FMath::IsFinite(RequestedSampleCount) || RequestedSampleCount > static_cast<double>(MaxGeneratedSamples))
	{
		UE_LOG(LogRacingRace, Warning,
			TEXT("Track '%s' would bake %f samples at %f cm spacing; clamping to %d. Accuracy is reduced, "
				 "and the content hash records the clamped count so the degraded bake is visible on a result."),
			*TrackId.ToString(), RequestedSampleCount, SpacingCm, MaxGeneratedSamples);
		NumSamples = MaxGeneratedSamples;
	}
	else
	{
		// RequestedSampleCount is finite and <= MaxGeneratedSamples here, so the cast is
		// in range by construction. A negative or sub-minimum value (a negative spacing
		// cannot reach this far, but a denormal length could) is floored by FMath::Max.
		NumSamples = FMath::Max(MinSamples, static_cast<int32>(RequestedSampleCount));
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
		LogBakeFailure(FString::Printf(TEXT("Track '%s' centerline bake failed: %s"), *TrackId.ToString(), *BuildError));
		return false;
	}

	// M1: record what the bake ACTUALLY used, not what was authored. ComputeContentHash
	// covers these, because a fallback spacing or a clamped sample count shifts every
	// progress value on the track while leaving the authored field untouched.
	EffectiveSampleCount = NumSamples;
	EffectiveStepCm = StepCm;

	RebuildGridSlots();
	RebuildResetSamples();
	RebuildCheckpointGates();

	bTrackDataBuilt = true;

	// A success re-arms the one-shot failure log, so a track that breaks again after
	// being fixed reports it instead of staying silent forever.
	bBakeFailureLogged = false;
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
	GridSlotDistancesCm.Reserve(SlotCount);

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

		// H2: kept alongside the transform rather than recovered later. Recovering it
		// means a global FindNearest, which is the search CenterlineAmbiguity proves
		// picks the wrong hairpin leg -- and race start is exactly when no hint exists.
		GridSlotDistancesCm.Add(DistanceCm);
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

void ATrackDefinitionActor::MakeGeneratedGateSpecs(TArray<FRacingCheckpointGateSpec>& OutSpecs)
{
	using namespace TrackDefinitionPrivate;

	OutSpecs.Reset();
	GeneratedGateClampNote.Reset();

	const double LengthCm = BakedCenterline.GetLengthCm();
	if (LengthCm <= 0.0)
	{
		return;
	}

	// Clamped rather than trusted, on the same reasoning as the grid's fallbacks: the
	// generator runs during a bake, and a bake must not fail on a value Validate() will
	// reject anyway. MaxGeneratedSamples is reused as the ceiling because it is already
	// this file's "no authored number turns a rebuild into an allocation storm" bound.
	const int32 RequestedGateCount = FMath::Clamp(NumGeneratedCheckpointGates, 1, MaxGeneratedSamples);
	int32 GateCount = RequestedGateCount;

	// A GENERATED SET MUST BE BAKEABLE. FRacingCheckpointGateSet::Build refuses gates
	// closer together than one centerline segment, because two gates inside one segment
	// share a plane normal and cannot be ordered reliably. Nothing stops an authored set
	// from tripping that -- it is an authoring error and should be reported -- but the
	// GENERATOR must not manufacture one, or a track with a coarse bake would silently
	// have no gates at all through no fault of anyone who touched it.
	//
	// Found by running: RacingSim.Race.TrackValidation deliberately bakes at ten laps'
	// worth of spacing to probe an unrelated guard, which floors at three samples and
	// therefore ~L/3 segments. Four evenly spaced gates are then L/4 apart -- closer than
	// one segment -- and the whole track stopped validating.
	//
	// The 2x margin is not decoration: at exactly one segment the separation check is
	// inclusive, and gate distances are computed by repeated multiplication, so a bare
	// >1 target would sit on the boundary and depend on rounding.
	//
	// WHAT THIS CLAMP IS AND IS NOT, restated after finding H1. It keeps the generated set
	// BAKEABLE. It does not, and cannot, keep it RACEABLE -- clamping four gates down to
	// one produces a set that builds cleanly and enforces nothing. Those are two different
	// properties and they used to be conflated here, which is how a one-gate track reached
	// Validate() and passed. Bakeability is this function's business; whether the survivors
	// are enough to run a race on is MinCheckpointGateCount's, enforced in Validate().
	const double MaxSegmentCm = BakedCenterline.GetMaxSegmentLengthCm();
	if (MaxSegmentCm > 0.0)
	{
		const int32 SupportedGates = FMath::FloorToInt32(LengthCm / (MaxSegmentCm * 2.0));
		GateCount = FMath::Clamp(GateCount, 1, FMath::Max(1, SupportedGates));
	}

	// H1 (code-reviewer, TRACK-002 pass 1): THE CLAMP MUST SAY SO.
	//
	// It used to reduce the count in silence. A four-gate request degraded to a
	// single-gate track -- no order to enforce, no shortcut detectable -- with nothing in
	// the log, nothing on the actor and, at the time, nothing in Validate() either. The
	// degraded set is now recorded on the actor (Validate() quotes it and refuses the
	// track below MinCheckpointGateCount) AND logged, because the two answer different
	// questions: the note tells a validation pass what is wrong with THIS track, the log
	// line tells whoever is watching a bake that the bake itself degraded something.
	//
	// THE NOTE IS RECORDED EVERY TIME; THE LOG LINE IS ONE-SHOT. The two are separated
	// deliberately, and the first draft of this fix got it wrong in a way the test run
	// caught, which is worth recording:
	//
	// Logging unconditionally reintroduced, in a new place, the exact defect TRACK-001's
	// finding H1 fixed for bake failures. A freshly placed ATrackDefinitionActor has
	// USplineComponent's default TWO-POINT, 200 cm spline. That bakes SUCCESSFULLY (a
	// closed loop floors at three samples), so LogBakeFailure's suppression never sees it
	// -- and 200 cm of track supports floor(200 / (2 * 66.7)) == 1 gate, so the clamp
	// fires. Every OnConstruction and every PostEditChangeProperty while somebody drags
	// the circuit into shape then emitted a warning. That is the normal state of the actor
	// for as long as it takes to author a track, and `.claude/rules/unreal-source.md`
	// forbids exactly this ("do not ... log noisily").
	//
	// Measured, not assumed: the first Smoke run of this change reported nine suites as
	// succeededWithWarnings, and the message was this one, at a 199.999985 cm lap -- the
	// CDO's default spline, restored by the test fixture's teardown.
	//
	// So the log follows bBakeFailureLogged's established discipline: report the first
	// time a bake clamps, then stay quiet until a bake places the full requested set,
	// which re-arms it. No information is lost, because GeneratedGateClampNote is set
	// unconditionally and Validate() quotes it on every call.
	if (GateCount < RequestedGateCount)
	{
		GeneratedGateClampNote = FString::Printf(
			TEXT("The gate generator was asked for %d gates and could only place %d: the centerline baked to a "
				 "maximum segment of %f cm over a %f cm lap (CenterlineSampleSpacingCm = %f, effective step %f cm), "
				 "and two gates within one segment share a plane normal and cannot be ordered. Bake the centerline "
				 "finer (lower CenterlineSampleSpacingCm) or ask for fewer gates."),
			RequestedGateCount, GateCount, MaxSegmentCm, LengthCm, CenterlineSampleSpacingCm, EffectiveStepCm);

		// R1-M2 (code-reviewer, TRACK-002 repair-cycle re-review), fixed at RACE-002:
		// A TEMPLATE IS NOT A TRACK, so its clamp is not news.
		//
		// The warning baseline grew from 3 suites/4 warnings to 9 suites/13 warnings when
		// this log was added, and every one of the new ones came from the CLASS DEFAULT
		// OBJECT: TrackDefinitionActorSpec's fixture borrows the CDO
		// (GetMutableDefault<ATrackDefinitionActor>()), bakes a circle on it and restores
		// the 200 cm two-point default spline in its destructor -- which supports one
		// gate, so the clamp fires once per suite that uses the fixture. Nothing was
		// wrong, and nine warnings per run that mean nothing is how a real one gets
		// missed.
		//
		// GeneratedGateClampNote is still recorded unconditionally, for templates too, so
		// no information is lost: Validate() quotes it and GetGeneratedGateClampNote()
		// exposes it. Only the LOG LINE is suppressed, and only for an object that can
		// never be raced on.
		// LATCH FIRST, LOG SECOND (restructured at RACE-003, closing RACE-002 finding L6).
		//
		// This used to read `if (!bGateClampLogged && !IsTemplate())`, so a CDO never
		// latched the flag at all -- and the only test fixture this project can build for
		// this actor IS the CDO (see TrackDefinitionActorSpec's header for why: a
		// SmokeFilter test cannot construct a non-template Actor). R1-L3's re-arm
		// behaviour was therefore unobservable in every environment that could test it,
		// which is precisely why it shipped with no assertion.
		//
		// The latch now happens for a template too; only the LOG is suppressed, which is
		// all R1-M2 ever asked for ("a template is not a track, so its clamp is not
		// news"). Nothing else reads this flag, so for a real actor the behaviour is
		// byte-identical and for a template the only change is that
		// IsGeneratedGateClampReportArmed() now tells the truth.
		if (!bGateClampLogged)
		{
			bGateClampLogged = true;

			if (!IsTemplate())
			{
				UE_LOG(LogRacingRace, Warning, TEXT("Track '%s': %s (further clamp reports for this track are "
					"suppressed until a bake places every requested gate)"), *TrackId.ToString(), *GeneratedGateClampNote);
			}
		}
	}
	else
	{
		// Re-armed on a clean bake, so a track that degrades AGAIN after being fixed
		// reports it instead of staying silent forever -- the same reasoning that re-arms
		// bBakeFailureLogged in RebuildTrackData().
		bGateClampLogged = false;
	}

	const double HalfWidthCm = (FMath::IsFinite(GeneratedGateHalfWidthCm) && GeneratedGateHalfWidthCm > 0.0)
		? GeneratedGateHalfWidthCm : 900.0;
	const double HalfHeightCm = (FMath::IsFinite(GeneratedGateHalfHeightCm) && GeneratedGateHalfHeightCm > 0.0)
		? GeneratedGateHalfHeightCm : 500.0;

	const double StepCm = LengthCm / static_cast<double>(GateCount);

	OutSpecs.Reserve(GateCount);
	for (int32 Index = 0; Index < GateCount; ++Index)
	{
		FRacingCheckpointGateSpec Spec;

		// Zero-padded so the ids sort in gate order as text as well as numerically -- a
		// log line reading Gate.10 before Gate.2 is how an ordering bug gets misread as a
		// gate bug.
		Spec.GateId = (Index == 0)
			? FName(TEXT("Gate.StartFinish"))
			: FName(*FString::Printf(TEXT("Gate.%02d"), Index));

		// Index 0 is pinned to EXACTLY 0.0 rather than computed as 0 * StepCm, so the
		// start/finish gate cannot be moved off the distance origin by a rounding step.
		Spec.DistanceAlongCm = (Index == 0) ? 0.0 : static_cast<double>(Index) * StepCm;
		Spec.HalfWidthCm = HalfWidthCm;
		Spec.HalfHeightCm = HalfHeightCm;
		Spec.LegalDirection = ERacingGateDirection::Forward;

		OutSpecs.Add(Spec);
	}
}

void ATrackDefinitionActor::RebuildCheckpointGates()
{
	BakedCheckpointGates.Reset();
	CheckpointGateBakeError.Reset();

	// Cleared here as well as in MakeGeneratedGateSpecs, because the AUTHORED path never
	// calls the generator at all: without this, filling in CheckpointGateSpecs to repair a
	// clamped generated set would leave the old clamp note attached to a track the
	// generator no longer touches.
	GeneratedGateClampNote.Reset();

	// Authored takes precedence; the generator is the floor for a track whose gates
	// nobody has placed yet, which is every track the moment its spline is drawn.
	TArray<FRacingCheckpointGateSpec> Generated;
	const bool bUseAuthored = CheckpointGateSpecs.Num() > 0;
	if (!bUseAuthored)
	{
		MakeGeneratedGateSpecs(Generated);
	}
	else
	{
		// R1-L3 (code-reviewer, TRACK-002 repair-cycle re-review), fixed at RACE-002.
		//
		// bGateClampLogged is re-armed by MakeGeneratedGateSpecs, which the authored path
		// never calls -- so a generated -> authored -> generated round trip left the
		// second clamp unlogged and the header's re-arm contract describing something the
		// code did not do. Re-arming here makes the two agree: the generator is not in
		// play on this path, so there is no clamp to suppress a report of.
		bGateClampLogged = false;
	}

	const TArray<FRacingCheckpointGateSpec>& Specs = bUseAuthored ? CheckpointGateSpecs : Generated;

	FString Error;
	if (!BakedCheckpointGates.Build(Specs, BakedCenterline, MinCornerRadiusCm, Error))
	{
		// Recorded, not logged here. RebuildTrackData's own one-shot failure logging
		// covers the centerline; a gate failure is reported by Validate(), which is where
		// a race director can act on it. Logging per rebuild would flood the editor while
		// someone is dragging a gate into place.
		CheckpointGateBakeError = Error;
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

// -- Checkpoint gates (TRACK-002) -------------------------------------------

const FRacingCheckpointGateSet& ATrackDefinitionActor::GetCheckpointGates() const
{
	EnsureTrackDataBuilt();
	return BakedCheckpointGates;
}

int32 ATrackDefinitionActor::GetNumCheckpointGates() const
{
	return GetCheckpointGates().NumGates();
}

bool ATrackDefinitionActor::GetCheckpointGate(const int32 GateIndex, FRacingCheckpointGate& OutGate) const
{
	const FRacingCheckpointGate* Gate = GetCheckpointGates().GetGate(GateIndex);
	if (!Gate)
	{
		// Default-constructed rather than left untouched: a caller that ignores the
		// return value gets a gate at the origin with zero extent, which crosses nothing,
		// instead of whatever happened to be in its stack slot.
		OutGate = FRacingCheckpointGate();
		return false;
	}

	OutGate = *Gate;
	return true;
}

int32 ATrackDefinitionActor::FindCheckpointGateIndexById(const FName GateId) const
{
	return GetCheckpointGates().FindGateIndexById(GateId);
}

double ATrackDefinitionActor::GetCheckpointGateDistanceCm(const int32 GateIndex) const
{
	const FRacingCheckpointGate* Gate = GetCheckpointGates().GetGate(GateIndex);
	return Gate ? Gate->DistanceAlongCm : InvalidDistanceCm;
}

FTransform ATrackDefinitionActor::GetCheckpointGateTransform(const int32 GateIndex) const
{
	const FRacingCheckpointGate* Gate = GetCheckpointGates().GetGate(GateIndex);
	return Gate ? Gate->GetTransform() : FTransform::Identity;
}

FRacingGateCrossingResult ATrackDefinitionActor::EvaluateGateCrossing(
	const int32 GateIndex,
	const FVector& FromWorldCm,
	const FVector& ToWorldCm) const
{
	return GetCheckpointGates().EvaluateCrossing(GateIndex, FromWorldCm, ToWorldCm);
}

int32 ATrackDefinitionActor::FindFirstGateCrossing(
	const FVector& FromWorldCm,
	const FVector& ToWorldCm,
	FRacingGateCrossingResult& OutResult) const
{
	return GetCheckpointGates().FindFirstCrossing(FromWorldCm, ToWorldCm, OutResult);
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

double ATrackDefinitionActor::GetGridSlotDistanceCm(const int32 SlotIndex) const
{
	EnsureTrackDataBuilt();
	return GridSlotDistancesCm.IsValidIndex(SlotIndex) ? GridSlotDistancesCm[SlotIndex] : InvalidDistanceCm;
}

FTransform ATrackDefinitionActor::GetGridSlotPose(const int32 SlotIndex, double& OutDistanceCm) const
{
	EnsureTrackDataBuilt();

	if (!GridSlotTransforms.IsValidIndex(SlotIndex) || !GridSlotDistancesCm.IsValidIndex(SlotIndex))
	{
		OutDistanceCm = InvalidDistanceCm;
		return FTransform::Identity;
	}

	OutDistanceCm = GridSlotDistancesCm[SlotIndex];
	return GridSlotTransforms[SlotIndex];
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

double ATrackDefinitionActor::GetResetSampleDistanceCm(const int32 SampleIndex) const
{
	EnsureTrackDataBuilt();
	return ResetSampleDistancesCm.IsValidIndex(SampleIndex) ? ResetSampleDistancesCm[SampleIndex] : InvalidDistanceCm;
}

FTransform ATrackDefinitionActor::GetResetTransformAtOrBeforeDistanceCm(const double DistanceCm, int32& OutIndex) const
{
	double UnusedDistanceCm = InvalidDistanceCm;
	return GetResetPoseAtOrBeforeDistanceCm(DistanceCm, OutIndex, UnusedDistanceCm);
}

FTransform ATrackDefinitionActor::GetResetPoseAtOrBeforeDistanceCm(
	const double DistanceCm,
	int32& OutIndex,
	double& OutDistanceCm) const
{
	OutIndex = INDEX_NONE;
	OutDistanceCm = InvalidDistanceCm;

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

	// H2: the arc length of the pose we are about to teleport a car to. The caller's
	// own progress hint is stale by definition after a reset -- this is what it must
	// re-seed FindNearestCenterlinePointNear with.
	OutDistanceCm = ResetSampleDistancesCm.IsValidIndex(Index) ? ResetSampleDistancesCm[Index] : InvalidDistanceCm;
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
	// M2: THREE fallback triggers, and the third is the counter-intuitive one.
	//
	// FindNearestNear degrades to the global search when the hint is non-finite, when
	// the window is non-positive, AND when SearchWindowCm * 2 >= GetTrackLengthCm().
	// The third case is easy to reach by trying to be careful: a caller that widens the
	// window "to be safe" past half a lap silently gets back the wrong-leg-prone global
	// search this overload exists to avoid, with no warning and no visible symptom
	// until a car's progress teleports across a hairpin.
	//
	// Not clamped here on purpose. Silently shrinking an over-wide window would hide a
	// caller's sizing bug behind behaviour it never asked for; the documented contract
	// is that the caller owns the window, so the caller must own getting it right.
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

	// M1: the AUTHORED resolution is not enough on its own.
	//
	// The bake substitutes 100 cm when CenterlineSampleSpacingCm is non-finite or
	// non-positive, and clamps the sample count at MaxGeneratedSamples. Both paths
	// change the effective resolution -- and therefore every progress value on the
	// track -- while leaving the authored field byte-identical, so hashing only the
	// authored field let two genuinely incomparable runs claim the same content hash.
	//
	// Both are hashed, not just the effective pair: the authored field is what a person
	// edited, the effective pair is what the machine actually ran, and a difference in
	// either makes two results incomparable. Requires the bake to have run, hence the
	// EnsureTrackDataBuilt() -- which after H1 costs at most one attempt.
	EnsureTrackDataBuilt();

	Hash = HashDouble(Hash, CenterlineSampleSpacingCm);
	Hash = HashCombine(Hash, GetTypeHash(EffectiveSampleCount));
	Hash = HashDouble(Hash, EffectiveStepCm);

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

	// TRACK-002: gates change which laps COUNT, so two tracks with identical geometry and
	// different gates are emphatically not the same content. Both the authored specs and
	// the generator parameters are hashed, because which of the two paths ran is itself a
	// content decision -- an authored set that happens to coincide with what the
	// generator would have produced is still a different, independently editable track.
	Hash = HashCombine(Hash, GetTypeHash(CheckpointGateSpecs.Num()));
	for (const FRacingCheckpointGateSpec& Spec : CheckpointGateSpecs)
	{
		Hash = HashCombine(Hash, GetTypeHash(Spec.GateId));
		Hash = HashDouble(Hash, Spec.DistanceAlongCm);
		Hash = HashDouble(Hash, Spec.HalfWidthCm);
		Hash = HashDouble(Hash, Spec.HalfHeightCm);

		// The legal direction is the whole point of a gate. A track whose finish line
		// accepts reverse crossings is a different competition from one that does not,
		// and without this the two would publish the same hash.
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Spec.LegalDirection)));
	}

	Hash = HashCombine(Hash, GetTypeHash(NumGeneratedCheckpointGates));
	Hash = HashDouble(Hash, GeneratedGateHalfWidthCm);
	Hash = HashDouble(Hash, GeneratedGateHalfHeightCm);
	Hash = HashDouble(Hash, MinCornerRadiusCm);

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

	// -- Checkpoint gates (TRACK-002) ---------------------------------------
	//
	// CLAUDE.md: "ordered checkpoint gates plus a valid crossing direction" authorise a
	// lap. A track that cannot bake its gates cannot authorise anything, so this is a
	// publishability failure and not a warning -- even though the CENTERLINE bake above
	// succeeded and progress/ranking would work perfectly well without gates. That is
	// exactly the trap: such a track looks entirely healthy in a HUD.

	if (!FMath::IsFinite(MinCornerRadiusCm) || MinCornerRadiusCm <= 0.0)
	{
		OutReason = FString::Printf(TEXT("MinCornerRadiusCm must be finite and positive, got %f."), MinCornerRadiusCm);
		return false;
	}

	// Checked only when the generator is the path actually in use. An authored gate set
	// makes this field inert -- it still feeds the content hash, because which path ran is
	// a content decision, but rejecting a track for a number that changes nothing about
	// its baked gates would be a false failure, and false failures are how a validation
	// pass teaches people to ignore it. The authored path is covered by the baked-count
	// floor below, which is the check that actually matters either way.
	// R1-L2 (code-reviewer, TRACK-002 repair-cycle re-review), fixed at RACE-002: THE
	// FIELD STILL FEEDS THE CONTENT HASH ON THE AUTHORED PATH.
	//
	// Relaxing the check below to "only when the generator is in use" was right -- an
	// inert field must not fail a track -- but it left NumGeneratedCheckpointGates
	// completely unvalidated whenever CheckpointGateSpecs is populated, while
	// ComputeContentHash() still hashes it. An authored track could therefore carry an
	// arbitrary or negative value into the identity two results are compared on. A bare
	// sanity floor keeps the hash covering a meaningful number without reintroducing a
	// false failure: 1, not MinCheckpointGateCount, because on this path the field
	// generates nothing and only has to be a number a generator could have used.
	if (CheckpointGateSpecs.Num() > 0 && NumGeneratedCheckpointGates < 1)
	{
		OutReason = FString::Printf(
			TEXT("NumGeneratedCheckpointGates is %d. The generator is not in use (CheckpointGateSpecs is authored), "
				 "but the field still feeds this track's content hash, so it may not be a value no generator could "
				 "have produced. Set it to at least 1."),
			NumGeneratedCheckpointGates);
		return false;
	}

	if (CheckpointGateSpecs.Num() == 0 && NumGeneratedCheckpointGates < MinCheckpointGateCount)
	{
		OutReason = FString::Printf(
			TEXT("NumGeneratedCheckpointGates is %d and CheckpointGateSpecs is empty, so this track would generate "
				 "fewer than the %d gates required to enforce checkpoint order. Raise it, or author "
				 "CheckpointGateSpecs explicitly."),
			NumGeneratedCheckpointGates, MinCheckpointGateCount);
		return false;
	}

	if (!FMath::IsFinite(GeneratedGateHalfWidthCm) || GeneratedGateHalfWidthCm <= 0.0)
	{
		OutReason = FString::Printf(TEXT("GeneratedGateHalfWidthCm must be finite and positive, got %f."), GeneratedGateHalfWidthCm);
		return false;
	}

	if (!FMath::IsFinite(GeneratedGateHalfHeightCm) || GeneratedGateHalfHeightCm <= 0.0)
	{
		OutReason = FString::Printf(TEXT("GeneratedGateHalfHeightCm must be finite and positive, got %f."), GeneratedGateHalfHeightCm);
		return false;
	}

	if (!BakedCheckpointGates.IsValid())
	{
		// The recorded bake error, not a downstream symptom. "Gate 2 is 40 cm from gate 1,
		// inside one centerline segment" is actionable; "the track has no gates" sends the
		// reader looking at the wrong array.
		OutReason = CheckpointGateBakeError.IsEmpty()
			? TEXT("Checkpoint gates failed to bake, with no recorded reason.")
			: FString::Printf(TEXT("Checkpoint gates failed to bake: %s"), *CheckpointGateBakeError);
		return false;
	}

	// TRACK-002 FINDING M2, CLOSED HERE: KEEP MinCornerRadiusCm IN THE MONOTONIC RANGE.
	//
	// The full derivation is in the field's own comment. In one line: the placement
	// tolerance GetSagittaBoundCm(R) peaks at R == MaxSegmentLengthCm / PI and DECREASES
	// on both sides of it, so below that threshold an author who understates the radius --
	// the direction Author-PrototypeGrayboxLevel.py correctly calls safe, and which is
	// safe everywhere above the threshold -- gets the WEAKEST possible gate-width check
	// instead of the strictest. Refusing the non-monotonic range is what makes "understate
	// it" advice a reader can follow without a caveat.
	//
	// CHECKED AFTER THE BAKE BRANCHES ABOVE, deliberately. A radius small enough to reach
	// this test usually makes the gate bake fail first (the tolerance exceeds the gate
	// half-width), and "gate 2 is narrower than the 1500 cm placement tolerance" is a more
	// actionable message than "your minimum corner radius is in the wrong range". This
	// fires for the case that survives the bake: a coarse centerline whose segments are so
	// long that even a plausible radius lands under S/PI.
	{
		const double MaxSegmentCm = BakedCenterline.GetMaxSegmentLengthCm();
		const double MonotonicFloorCm = MaxSegmentCm / UE_DOUBLE_PI;

		if (MaxSegmentCm > 0.0 && !(MinCornerRadiusCm > MonotonicFloorCm))
		{
			OutReason = FString::Printf(
				TEXT("MinCornerRadiusCm is %f cm, at or below %f cm (the baked centerline's maximum segment of "
					 "%f cm divided by PI). Below that threshold the derived gate placement tolerance stops "
					 "increasing as the radius falls and starts SHRINKING with it, so a smaller authored radius "
					 "produces a WEAKER gate-width check rather than a stricter one -- the opposite of what "
					 "understating the radius is supposed to buy. Raise MinCornerRadiusCm above %f cm, or bake "
					 "the centerline finer (lower CenterlineSampleSpacingCm) so the maximum segment shrinks."),
				MinCornerRadiusCm, MonotonicFloorCm, MaxSegmentCm, MonotonicFloorCm);
			return false;
		}
	}

	// H1 (code-reviewer, TRACK-002 pass 1): A GATE SET THAT CANNOT ENFORCE ORDER IS NOT A
	// PUBLISHABLE TRACK, AND "IT BAKED" IS NOT THE SAME QUESTION.
	//
	// The check above only asks whether the set built. A one-gate set builds perfectly:
	// its geometry is sound, gate 0 sits at distance 0 facing forwards, and every crossing
	// query answers correctly. It is nonetheless unraceable, because with one gate there
	// is no order to be out of and no shortcut is detectable -- the exact property gates
	// exist to provide. See MinCheckpointGateCount for why the floor is 4 and why the
	// coarse bake, not an author, is what produced the one-gate set.
	//
	// Reported AFTER the bake-error branch so a set that failed to build still reports the
	// build reason: "gate 2 is inside one centerline segment of gate 1" is more actionable
	// than "this track has 0 gates".
	if (BakedCheckpointGates.NumGates() < MinCheckpointGateCount)
	{
		OutReason = FString::Printf(
			TEXT("Track baked %d checkpoint gate(s); at least %d are required, because a set this small cannot "
				 "enforce checkpoint order -- CLAUDE.md's \"ordered checkpoint gates plus a valid crossing "
				 "direction\" has no ordering half here, and a car cutting across the circuit would still be "
				 "credited a lap."),
			BakedCheckpointGates.NumGates(), MinCheckpointGateCount);

		if (!GeneratedGateClampNote.IsEmpty())
		{
			// The usual cause, and it is not visible anywhere in the authored data: the
			// author asked for enough gates and the BAKE threw them away.
			OutReason += FString::Printf(TEXT(" %s"), *GeneratedGateClampNote);
		}

		return false;
	}

	{
		// A track with no forward-crossable start/finish gate can never complete a lap,
		// however well everything else validates. Checked here rather than in the gate
		// set's Build() because it is a RACE rule, not a geometry rule: a gate set with a
		// bidirectional or reverse-only gate 0 is perfectly well-formed geometry.
		const FRacingCheckpointGate* StartFinish =
			BakedCheckpointGates.GetGate(FRacingCheckpointGateSet::StartFinishGateIndex);

		if (!StartFinish)
		{
			OutReason = TEXT("No start/finish checkpoint gate.");
			return false;
		}

		if (StartFinish->LegalDirection == ERacingGateDirection::Reverse)
		{
			OutReason = FString::Printf(
				TEXT("Start/finish gate '%s' is Reverse-only, so a forward lap could never be completed."),
				*StartFinish->GateId.ToString());
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

// ===========================================================================
// Cached validity (TRACK-001 M7, extended to cover TRACK-002 M4)
// ===========================================================================

bool ATrackDefinitionActor::GetCachedValidation(FString& OutReason) const
{
	// Same rule and same reason as EnsureTrackDataBuilt()/RebuildTrackData(): the
	// const_cast below writes five members, which is only sound because this class is
	// game-thread-only. Asserted in code rather than promised in a comment.
	check(IsInGameThread());

	// THE KEY. ComputeContentHash() runs EnsureTrackDataBuilt() itself, so this also
	// guarantees there IS a bake to validate -- and it folds in EffectiveSampleCount and
	// EffectiveStepCm, so a re-bake that silently degraded the resolution moves the key
	// even when no authored field changed. That is the case a naive "did anything get
	// edited" cache would miss.
	const uint32 CurrentHash = ComputeContentHash();

	if (!bHasValidationCache || ValidatedContentHash != CurrentHash)
	{
		ATrackDefinitionActor* MutableThis = const_cast<ATrackDefinitionActor*>(this);

		FString Reason;
		const bool bValid = Validate(Reason);

		// Committed together. A half-written cache (new hash, old answer) would be worse
		// than no cache: it would hand out a stale verdict under a key that claims to be
		// current, which is the failure mode a cache exists to make impossible.
		MutableThis->bCachedValidationResult = bValid;
		MutableThis->CachedValidationReason = MoveTemp(Reason);
		MutableThis->ValidatedContentHash = CurrentHash;
		MutableThis->bHasValidationCache = true;
		++MutableThis->ValidationRunCount;

		UE_LOG(LogRacingRace, Verbose,
			TEXT("Track '%s' validation cache miss (hash %08x): %s%s%s"),
			*TrackId.ToString(), CurrentHash, bValid ? TEXT("valid") : TEXT("INVALID"),
			bValid ? TEXT("") : TEXT(" -- "), *CachedValidationReason);
	}

	// Always written, unlike Validate()'s out-parameter, which is left untouched on
	// success. A caller reading a cache must not have to know what was in its FString
	// before it asked.
	OutReason = CachedValidationReason;
	return bCachedValidationResult;
}

bool ATrackDefinitionActor::IsValidatedForRace() const
{
	FString UnusedReason;
	return GetCachedValidation(UnusedReason);
}
