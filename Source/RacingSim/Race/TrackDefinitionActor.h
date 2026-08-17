// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/RacingSimBuildId.h"
#include "Race/TrackCenterline.h"
#include "TrackDefinitionActor.generated.h"

class USplineComponent;

/**
 * TRACK-001: the authoritative, versioned definition of one circuit.
 *
 * ===========================================================================
 * Scope, and what this ticket deliberately does NOT own
 * ===========================================================================
 *
 * Docs/03-TrackRaceUI.md, "Track representation", lists nine things
 * ATrackDefinitionActor owns. TRACK-001 delivers five of them:
 *
 *   [x] a centerline spline with monotonically increasing arc length
 *   [x] sector boundaries
 *   [x] grid and start poses
 *   [x] safe reset poses sampled along the legal route
 *   [x] total length and version hash
 *   [ ] ordered checkpoint IDs                     -> TRACK-002
 *   [ ] start/finish plane and valid crossing dir  -> TRACK-002
 *   [ ] track width / boundary splines             -> TRACK-002 or later
 *   [ ] surface metadata and track-limit zones     -> later
 *
 * THIS CLASS CANNOT AUTHORISE A LAP AND MUST NEVER BE MADE ABLE TO. CLAUDE.md
 * requires ordered checkpoint gates plus a valid crossing direction, and
 * Docs/03-TrackRaceUI.md rule 6 says continuous spline distance "never replaces
 * ordered checkpoint validation". Everything here is progress, ranking and
 * placement. TRACK-002 adds the gates that authorise; RACE-002 adds the rules that
 * validate. A future edit that adds a "crossed the line" query to this file is a
 * design error, not a convenience.
 *
 * ===========================================================================
 * The start/finish origin is the centerline's distance zero, by definition
 * ===========================================================================
 *
 * There is no separately authored start/finish distance, and that is a deliberate
 * constraint rather than an omission. If the line could sit at an arbitrary arc
 * length, then lap progress (`lap * length + distance`, Docs/03-TrackRaceUI.md
 * rule 7) would need an offset-and-rewrap at every comparison, and every such site
 * is a place to get the wrap wrong exactly once, on the start/finish line, where
 * it is hardest to notice and most expensive. Pinning the origin to the line makes
 * the arithmetic trivially correct everywhere. To move the line, move the spline's
 * first point.
 *
 * TRACK-002 will place its finish GATE at this transform; the transform is
 * geometry and belongs here, the crossing rule is policy and belongs there.
 *
 * ===========================================================================
 * Units
 * ===========================================================================
 *
 * Every stored distance is Unreal centimetres per CORE-002 (Core/RacingSimUnits.h)
 * and carries a `Cm` suffix. SI appears only in the explicitly named
 * GetTrackLengthMetres()/GetTrackLengthKilometres() accessors, which exist so that
 * UI, docs and the "3-5 km centerline" target in Docs/03-TrackRaceUI.md are read
 * off one conversion rather than several hand-rolled ones.
 *
 * ===========================================================================
 * Originality
 * ===========================================================================
 *
 * CLAUDE.md forbids copying a real circuit's geometry, name, signage or venue.
 * Nothing in this class encodes any circuit: it is a container whose contents are
 * authored. The grid and reset poses are GENERATED from the centerline by generic
 * rules (a staggered two-column grid, evenly spaced reset samples) rather than
 * traced from anywhere.
 */
UCLASS(Blueprintable, BlueprintType, meta = (DisplayName = "Track Definition"))
class RACINGSIM_API ATrackDefinitionActor : public AActor
{
	GENERATED_BODY()

public:
	ATrackDefinitionActor();

	/**
	 * Layout version of the C++ that reads this actor's authored data. Hand-bumped,
	 * never derived -- the same contract URaceRulesetDataAsset::RulesetSchemaVersion
	 * uses.
	 *
	 * 1 = TRACK-001: centerline spline, sector boundaries, generated grid, generated
	 *     reset samples. No checkpoints.
	 *
	 * Any ticket that adds an authored field MUST bump this AND add the field to
	 * ComputeContentHash(), or two genuinely different tracks will claim to be the
	 * same one on a leaderboard.
	 */
	static constexpr int32 TrackSchemaVersion = 1;

	// =======================================================================
	// Authored data
	// =======================================================================

	/**
	 * Stable identifier written into every result, e.g. "Track.Prototype.NorthLoop".
	 *
	 * A name rather than the level path, for the reason URaceRulesetDataAsset
	 * records: a content move must not invalidate historical results. NAME_None is
	 * rejected by Validate() and by FRacingContentVersion::IsPopulated().
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track")
	FName TrackId;

	/**
	 * Spacing the centerline is baked to, in centimetres. Default 100 cm (1 m).
	 *
	 * This is an accuracy/cost dial and it is part of the content hash, because a
	 * re-bake at a different resolution shifts every progress value on the track by
	 * a small amount and two such runs are not comparable. Chord error against the
	 * true curve is about Spacing^2 / (8 * radius): at 100 cm and a 15 m corner that
	 * is under 1 cm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track",
		meta = (ClampMin = "10.0", UIMin = "25.0", UIMax = "1000.0", ForceUnits = "cm"))
	double CenterlineSampleSpacingCm = 100.0;

	/**
	 * Arc-length distance at which each sector STARTS, in centimetres, ascending.
	 *
	 * Element 0 must be exactly 0 -- sector 0 begins at the start/finish line. The
	 * number of sectors is the number of entries, so three entries is the
	 * conventional three-sector circuit. The last sector runs from its start
	 * distance back around to the line.
	 *
	 * Stored as starts rather than as interior boundaries so that "which sector is
	 * this distance in" is a plain upper-bound search with no special case for the
	 * first and last, and so an empty array is obviously invalid rather than
	 * ambiguously meaning "one sector".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track",
		meta = (ForceUnits = "cm"))
	TArray<double> SectorStartDistancesCm;

	// -- Grid ---------------------------------------------------------------
	// Generated from the centerline rather than authored per slot: a generated slot
	// is guaranteed to sit on the legal route facing the right way, whereas 20
	// hand-placed transforms are 20 chances to leave one facing backwards, and
	// nothing in the graybox would catch it.

	/** Number of starting grid slots. Slot 0 is pole. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track|Grid",
		meta = (ClampMin = "1", UIMin = "1", UIMax = "32"))
	int32 NumGridSlots = 8;

	/** Distance from the start/finish line back to pole position, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track|Grid",
		meta = (ClampMin = "0.0", ForceUnits = "cm"))
	double GridPoleSetbackCm = 800.0;

	/** Additional setback per slot, in centimetres. Rows are one slot apart, staggered left/right. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track|Grid",
		meta = (ClampMin = "100.0", ForceUnits = "cm"))
	double GridSlotSpacingCm = 800.0;

	/** Sideways stagger from the centerline, in centimetres. Slot 0 sits to the LEFT. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track|Grid",
		meta = (ClampMin = "0.0", ForceUnits = "cm"))
	double GridSlotLateralOffsetCm = 200.0;

	/** Height above the centerline for every generated pose, in centimetres. Keeps a spawned car out of the road surface. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track|Grid",
		meta = (ClampMin = "0.0", ForceUnits = "cm"))
	double PoseHeightOffsetCm = 50.0;

	// -- Reset samples ------------------------------------------------------

	/**
	 * Spacing between safe reset poses along the centerline, in centimetres.
	 * Default 2500 cm (25 m).
	 *
	 * Docs/03-TrackRaceUI.md rule 8: "Reset returns the car to the most recent safe
	 * valid sample". Generating them along the centerline is what makes "safe" and
	 * "valid" true by construction -- a sample cannot be off the legal route if the
	 * legal route is where it came from. Closer spacing means a less punishing
	 * reset; it is a tunable, and it is hashed, because two runs with different
	 * reset granularity are not the same competition.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track|Reset",
		meta = (ClampMin = "100.0", UIMin = "500.0", ForceUnits = "cm"))
	double ResetSampleSpacingCm = 2500.0;

	// =======================================================================
	// Queries
	// =======================================================================

	/** The spline the centerline is authored on. Closed loop. Editor-facing; runtime code should use the baked centerline. */
	USplineComponent* GetCenterlineSpline() const { return CenterlineSpline; }

	/** The baked, world-free arc-length model. This is what runtime race code should query. */
	const FTrackCenterline& GetCenterline() const;

	/** Lap distance in Unreal centimetres. */
	double GetTrackLengthCm() const;

	/** Lap distance in SI metres. UNIT BOUNDARY -- cm to m via RacingSim::Units. */
	double GetTrackLengthMetres() const;

	/** Lap distance in kilometres, for display and for the 3-5 km design target. */
	double GetTrackLengthKilometres() const;

	/** World transform of the start/finish line: the centerline at distance zero, facing the direction of travel. */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	FTransform GetStartFinishTransform() const;

	/** Number of sectors, i.e. the number of authored sector starts. */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	int32 GetNumSectors() const;

	/**
	 * Which sector an arc-length distance falls in, or INDEX_NONE if the track is
	 * unbuilt or has no sectors. The distance is wrapped first, so a multi-lap or
	 * negative value answers about the equivalent point on the lap.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	int32 GetSectorIndexAtDistanceCm(double DistanceCm) const;

	/** Arc length at which a sector starts, in centimetres. 0 for sector 0. */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	double GetSectorStartDistanceCm(int32 SectorIndex) const;

	/** Arc length of a sector, in centimetres. The last sector's length runs back to the start/finish origin. */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	double GetSectorLengthCm(int32 SectorIndex) const;

	/** Number of generated grid slots actually available. Matches NumGridSlots once the track is built. */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	int32 GetNumGridSlots() const;

	/** World transform of a grid slot. Slot 0 is pole. Returns identity for an out-of-range index. */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	FTransform GetGridSlotTransform(int32 SlotIndex) const;

	/** Number of generated safe reset poses. */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	int32 GetNumResetSamples() const;

	/** World transform of a reset pose by index. Returns identity for an out-of-range index. */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	FTransform GetResetSampleTransform(int32 SampleIndex) const;

	/**
	 * The most recent safe reset pose at or before an arc-length distance --
	 * Docs/03-TrackRaceUI.md rule 8.
	 *
	 * "At or before" and never "nearest": placing a car at the nearest sample can
	 * put it AHEAD of where it left the road, which awards free distance. This
	 * function can only ever move a car backwards along the route or leave it where
	 * it is. Whether the lap is then invalidated is a ruleset decision and is
	 * RACE-002's, not this class's.
	 *
	 * @param DistanceCm  arc length to search back from; wrapped first.
	 * @param OutIndex    index of the chosen sample, INDEX_NONE when none exists.
	 * @return the pose, or identity when the track has no reset samples.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	FTransform GetResetTransformAtOrBeforeDistanceCm(double DistanceCm, int32& OutIndex) const;

	/**
	 * Project a world location onto the centerline, searching the whole track.
	 *
	 * PROGRESS ONLY. The returned distance ranks cars; it does not and cannot count
	 * a lap. See the class comment.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	FTrackCenterlineQuery FindNearestCenterlinePoint(const FVector& WorldLocationCm) const;

	/**
	 * Project a world location onto the centerline near a previous result.
	 *
	 * Prefer this whenever a previous distance exists. It is not just faster: a
	 * global search snaps to the wrong side of a hairpin or the wrong one of two
	 * parallel straights, and the symptom is a car's progress teleporting half a
	 * lap. See FTrackCenterline::FindNearestNear.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	FTrackCenterlineQuery FindNearestCenterlinePointNear(const FVector& WorldLocationCm, double HintDistanceCm, double SearchWindowCm) const;

	/**
	 * Ranking key from a lap count and an arc-length distance:
	 * `LapsCompleted * TrackLength + DistanceAlongCm`, Docs/03-TrackRaceUI.md rule 7.
	 *
	 * LapsCompleted is an INPUT, supplied by whoever holds lap authority (RACE-002),
	 * precisely so that this function cannot invent one. It is arithmetic over a
	 * number someone else validated.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	double GetRaceProgressCm(int32 LapsCompleted, double DistanceAlongCm) const;

	// =======================================================================
	// Identity and validation
	// =======================================================================

	/**
	 * Identity of this track for FRacingSimVersionStamp::TrackVersion, which
	 * CORE-002 reserved with the comment "populated by TRACK-001".
	 *
	 * Hashes on every call. Call it once when a run starts, never per frame.
	 */
	FRacingContentVersion GetContentVersion() const;

	/**
	 * Hash of the AUTHORED values only: identity, spline geometry, bake resolution,
	 * sectors, grid and reset parameters, and the actor's own transform (which
	 * places the whole circuit in the world).
	 *
	 * Deliberately excludes TrackSchemaVersion, which FRacingContentVersion records
	 * separately -- "the code changed" and "the data changed" are different
	 * questions, as RaceRulesetDataAsset records.
	 *
	 * Also deliberately excludes everything DERIVED (the baked samples, the grid
	 * transforms, the reset poses). Hashing a derivative of an input already in the
	 * hash adds no information and creates a second thing to keep in sync.
	 */
	uint32 ComputeContentHash() const;

	/**
	 * Content validation, callable from automation and from CORE-003's editor pass.
	 *
	 * A plain function rather than an IsDataValid override, for the reason
	 * URaceRulesetDataAsset records: the engine's validation context type lives in
	 * an editor-only module, and a WITH_EDITOR fork could not be exercised by the
	 * commandlet the project's automation gate actually runs in.
	 *
	 * @param OutReason set to a human-readable reason on failure; untouched on success.
	 * @return true when this track may be used for a result that will be published.
	 */
	bool Validate(FString& OutReason) const;

	/**
	 * Re-bake the centerline, grid and reset poses from the authored data.
	 *
	 * Called from OnConstruction, PostLoad, BeginPlay and (in editor) on any
	 * property change, and lazily by the first query if none of those has run --
	 * so a NewObject'd instance in a test behaves like a placed one. Game thread
	 * only; it is not safe to call while another thread is querying.
	 *
	 * @return true when the resulting centerline is usable.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	bool RebuildTrackData();

	/** True once RebuildTrackData() has produced a usable centerline. */
	bool IsTrackDataBuilt() const { return bTrackDataBuilt && BakedCenterline.IsValid(); }

	//~ Begin AActor interface
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostLoad() override;
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End AActor interface

private:
	/** Scene root. The spline is attached to it so the whole circuit moves with the actor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> TrackRoot;

	/** The authored centerline. Closed loop by default; Validate() rejects an open one. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Track", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USplineComponent> CenterlineSpline;

	/** Derived. Rebuilt from CenterlineSpline; never authored, never saved as truth. */
	UPROPERTY(Transient)
	FTrackCenterline BakedCenterline;

	/** Derived. World-space grid poses, index 0 = pole. */
	UPROPERTY(Transient)
	TArray<FTransform> GridSlotTransforms;

	/** Derived. World-space reset poses, ascending in arc length, [0] at distance 0. */
	UPROPERTY(Transient)
	TArray<FTransform> ResetSampleTransforms;

	/** Derived. Arc-length distance of each entry in ResetSampleTransforms. */
	UPROPERTY(Transient)
	TArray<double> ResetSampleDistancesCm;

	UPROPERTY(Transient)
	bool bTrackDataBuilt = false;

	/**
	 * Build on first use if nothing else has.
	 *
	 * const_cast rather than mutable members: the derived data is genuinely derived,
	 * so rebuilding it does not change what this actor MEANS, and marking six
	 * members mutable would weaken const on all of them individually. Game thread
	 * only, like RebuildTrackData itself.
	 */
	void EnsureTrackDataBuilt() const;

	/** Generate GridSlotTransforms from the baked centerline. Requires a valid centerline. */
	void RebuildGridSlots();

	/** Generate ResetSampleTransforms/Distances from the baked centerline. Requires a valid centerline. */
	void RebuildResetSamples();

	/** Lift a centerline transform by PoseHeightOffsetCm and slide it sideways. Shared by grid and reset poses. */
	FTransform MakePoseAtDistance(double DistanceCm, double LateralOffsetCm) const;
};
