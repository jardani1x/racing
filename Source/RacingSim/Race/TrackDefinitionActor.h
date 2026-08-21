// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/RacingSimBuildId.h"
#include "Race/TrackCenterline.h"
#include "Race/TrackCheckpointGate.h"
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
 *   [x] ordered checkpoint IDs                     -> TRACK-002
 *   [x] start/finish plane and valid crossing dir  -> TRACK-002
 *   [ ] track width / boundary splines             -> later
 *   [ ] surface metadata and track-limit zones     -> later
 *
 * THIS CLASS STILL CANNOT AUTHORISE A LAP, AND MUST NEVER BE MADE ABLE TO.
 * TRACK-002 added the ordered gates and the crossing-direction test, which is half
 * of what CLAUDE.md requires -- "ordered checkpoint gates PLUS a valid crossing
 * direction". The other half is ORDER ENFORCEMENT: tracking which gate each car is
 * expected to take next, invalidating a lap on a skipped or out-of-order gate, and
 * counting the lap at the finish line. That state is per-car, not per-track, and it
 * belongs to RACE-002.
 *
 * So: this class can now tell you that a car crossed gate 3 forwards, legally, 40%
 * of the way through the tick. It still cannot tell you whether that completed a
 * lap, and Docs/03-TrackRaceUI.md rule 6's "continuous spline distance never
 * replaces ordered checkpoint validation" is unchanged. A future edit that adds a
 * lap counter, a timer, or an ERaceState reference to this file is a design error.
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
	 * 2 = TRACK-002: ordered checkpoint gates (authored or generated), per-gate legal
	 *     crossing direction, and MinCornerRadiusCm. A version-1 result and a version-2
	 *     result are not comparable even on identical geometry, because the gates decide
	 *     which laps count at all.
	 *
	 * Any ticket that adds an authored field MUST bump this AND add the field to
	 * ComputeContentHash(), or two genuinely different tracks will claim to be the
	 * same one on a leaderboard.
	 */
	static constexpr int32 TrackSchemaVersion = 2;

	/**
	 * Fewest BAKED checkpoint gates a track may have and still be publishable.
	 * Enforced by Validate(); see the "Checkpoint gates" block there.
	 *
	 * WHY A FLOOR EXISTS AT ALL, and why it is not 1 (code-reviewer, TRACK-002 pass 1,
	 * finding H1). Validate()'s only gate-count check used to be
	 * FRacingCheckpointGateSet::IsValid(), i.e. "at least one gate". With exactly one
	 * gate there is no ORDER to enforce: every crossing is a crossing of the only gate,
	 * RACE-002's expected-checkpoint sequence degenerates to a single element, and no
	 * shortcut whatsoever is detectable. Such a track validated green, and the HUD would
	 * have looked perfectly healthy while counting laps for a car that drove across the
	 * infield.
	 *
	 * THAT SET WAS REACHABLE WITHOUT AUTHORING IT. MakeGeneratedGateSpecs() clamps its
	 * own count down to what the baked centerline can separate (two gates inside one
	 * polyline segment share a plane normal and cannot be ordered). At the coarsest bake
	 * Validate() permits -- CenterlineSampleSpacingCm >= LengthCm/3, which floors the bake
	 * at three samples and therefore ~L/3 segments -- that clamp yields
	 * floor(L / (2 * L/3)) == 1. A large MinCornerRadiusCm then shrinks the sagitta below
	 * the gate half-width so the gate bake succeeds, and the whole track validated with a
	 * single gate. So the cause is a COARSE BAKE silently degrading the gate set, not an
	 * author typing 1.
	 *
	 * WHY 4. Two thresholds are in play and they are different numbers:
	 *
	 *   - >= 2 is where an ORDER exists at all (one gate cannot be out of order);
	 *   - >= 4 is where a shortcut across the middle of a circuit becomes detectable,
	 *     which is the whole reason gates exist. With gates at 0 and L/2 only, a car can
	 *     drive to the L/2 gate, turn round across the infield, cross the line forwards
	 *     and be credited a lap half the length of the circuit.
	 *
	 * No finite count forbids every shortcut -- N gates bound the longest undetectable cut
	 * to roughly the chord across one inter-gate arc -- so the floor is a policy choice,
	 * and 4 is chosen because it is the number this class ALREADY documented (see
	 * NumGeneratedCheckpointGates) and ships as the generator default. Raising the floor
	 * to match the documentation removes a contradiction; inventing a third number would
	 * have added one.
	 *
	 * This is deliberately NOT enforced inside FRacingCheckpointGateSet::Build(): a
	 * two-gate set is well-formed GEOMETRY, and Build() owns geometry. "Enough gates to
	 * run a race on" is a RACE rule, and race rules live in Validate() -- the same split
	 * the Reverse-only start/finish check is made on.
	 */
	static constexpr int32 MinCheckpointGateCount = 4;

	// =======================================================================
	// Authored data
	// =======================================================================

	/**
	 * Stable identifier written into every result, e.g. "Track.Prototype.Meridian".
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

	// -- Checkpoint gates (TRACK-002) ---------------------------------------

	/**
	 * Ordered checkpoint gates, ascending in arc length, entry 0 at exactly 0.
	 *
	 * Docs/03-TrackRaceUI.md lists "ordered checkpoint IDs" and "start/finish plane and
	 * valid crossing direction" among the things this actor owns, so they live here
	 * rather than in a side asset: a gate set that can be swapped independently of the
	 * centerline it is measured against is a gate set that can silently stop matching it.
	 *
	 * LEAVE THIS EMPTY TO GET GENERATED GATES. See NumGeneratedCheckpointGates. Both
	 * paths are covered by ComputeContentHash(), so a generated set and an authored set
	 * that happen to coincide are still distinguishable on a result.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints")
	TArray<FRacingCheckpointGateSpec> CheckpointGateSpecs;

	/**
	 * How many evenly spaced gates to generate when CheckpointGateSpecs is empty.
	 * Gate 0 is always the start/finish gate at distance 0.
	 *
	 * Generated rather than required-to-be-authored for the same reason the grid is
	 * generated: a graybox track must be usable the moment its spline exists, and a
	 * generated gate is guaranteed to stand square across the legal route facing the
	 * direction that increases arc length. Hand-authoring is still available and takes
	 * precedence; this is the floor, not the ceiling.
	 *
	 * Four (start/finish plus three) is the smallest count that makes a shortcut across
	 * the middle of a circuit detectable, which is the point of having gates at all --
	 * see MinCheckpointGateCount, which Validate() now enforces on the BAKED set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints",
		meta = (ClampMin = "4", UIMin = "4", UIMax = "32"))
	int32 NumGeneratedCheckpointGates = 4;

	/** Half-width, cm, given to every generated gate. Should comfortably exceed the racing surface's half-width. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints",
		meta = (ClampMin = "1.0", ForceUnits = "cm"))
	double GeneratedGateHalfWidthCm = 900.0;

	/** Half-height, cm, given to every generated gate. Finite so a car launched over the gate is not a clean crossing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints",
		meta = (ClampMin = "1.0", ForceUnits = "cm"))
	double GeneratedGateHalfHeightCm = 500.0;

	/**
	 * Tightest corner radius the authored centerline contains, cm. Default 1500 cm (15 m).
	 *
	 * NOT COSMETIC, AND NOT A GUESS TO BE LEFT AT ITS DEFAULT. This is the number that
	 * converts the baked centerline's MAXIMUM segment length into a real distance: the
	 * sagitta, i.e. how far inside the true authored curve a baked position can sit
	 * (FTrackCenterline::GetSagittaBoundCm). Every gate's half-width must exceed that
	 * bound, so this field sets the strictness of the gate-width check.
	 *
	 * The bias is one-directional -- always inward, never outward -- so it does not
	 * average away over a lap. TRACK-001 recorded that as its strongest counter-case and
	 * required TRACK-002 to assert it rather than inherit it; this field plus
	 * RacingSim.Race.CenterlinePolylineBias is that assertion.
	 *
	 * =======================================================================
	 * WHICH DIRECTION IS SAFE (TRACK-002 finding M2, reconciled at RACE-003)
	 * =======================================================================
	 *
	 * TWO IN-REPO COMMENTS USED TO DISAGREE ABOUT THIS, and both were half right.
	 * Scripts/Content/Author-PrototypeGrayboxLevel.py said understating the radius is the
	 * safe direction because it makes the derived tolerance larger; the paragraph that
	 * used to stand here said understating is "the one input that can let a gate be baked
	 * narrower than the model's own systematic error". They are describing opposite sides
	 * of a function that is NOT MONOTONIC, and neither said so.
	 *
	 * Write S for GetMaxSegmentLengthCm() and R for this field. GetSagittaBoundCm is
	 *
	 *     tolerance(R) = min( R * (1 - cos(min(S/R, PI) / 2)),  S/2 )
	 *
	 * and it has a single interior maximum at R = S/PI, where it equals S/PI:
	 *
	 *   - for R >  S/PI the theta cap is inactive, and tolerance(R) is STRICTLY
	 *     DECREASING in R (to leading order S^2/(8R)). Understating R makes the tolerance
	 *     LARGER and the gate-width check STRICTER. The .py comment is right HERE, and
	 *     this is the region every real track sits in;
	 *   - for R <= S/PI theta is capped at PI, so tolerance(R) collapses to exactly R and
	 *     is STRICTLY INCREASING. Understating R now makes the tolerance SMALLER and the
	 *     check WEAKER -- an author who types a very small radius gets the weakest
	 *     possible placement check, not the strictest. The old paragraph here was right
	 *     about THIS region and wrong to state it unconditionally.
	 *
	 * SO THE GUARD IS RANGE-CONDITIONAL, AND Validate() NOW ENFORCES THE RANGE: it
	 * refuses a track with `MinCornerRadiusCm <= GetMaxSegmentLengthCm() / PI`. Inside the
	 * validated range the function is monotonic, understating is unambiguously the safe
	 * direction, and the .py comment is unconditionally true for any track that validates.
	 *
	 * WHY NOT JUST RAISE ClampMin. Because the threshold is DATA, not a constant: it is
	 * S/PI, and S is whatever the centerline happened to bake to. A static ClampMin high
	 * enough for a coarse bake would reject legitimate tight-radius authoring on a fine
	 * one, and one low enough for a fine bake would not guard a coarse one at all. ClampMin
	 * stays a floor against nonsense; the real check is in Validate(), where the baked
	 * geometry is available. This matters more from RACE-003 on than it did before:
	 * IsValidatedForRace() is the first consumer that treats Validate()'s answer as
	 * load-bearing rather than advisory.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Track|Checkpoints",
		meta = (ClampMin = "1.0", UIMin = "500.0", ForceUnits = "cm"))
	double MinCornerRadiusCm = 1500.0;

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

	// -- Checkpoint gates (TRACK-002) ---------------------------------------

	/**
	 * The baked, ordered gate set. This is the surface RACE-002 consumes.
	 *
	 * Returned by const reference rather than by value: FRacingCheckpointGateSet holds a
	 * TArray, and CLAUDE.md forbids per-frame allocation. A caller evaluating crossings
	 * every tick must bind this once, not copy it.
	 */
	const FRacingCheckpointGateSet& GetCheckpointGates() const;

	/** Number of baked checkpoint gates. */
	UFUNCTION(BlueprintCallable, Category = "Race|Track|Checkpoints")
	int32 GetNumCheckpointGates() const;

	/**
	 * A gate by index. Returns false and leaves OutGate default-constructed when the
	 * index is out of range, rather than returning a plausible-looking gate at the
	 * origin that a caller would then test crossings against.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Track|Checkpoints")
	bool GetCheckpointGate(int32 GateIndex, FRacingCheckpointGate& OutGate) const;

	/** Index of a gate by its stable id, or INDEX_NONE. Not for per-frame use; it is a linear scan. */
	UFUNCTION(BlueprintCallable, Category = "Race|Track|Checkpoints")
	int32 FindCheckpointGateIndexById(FName GateId) const;

	/** Arc-length distance of a gate, cm, or InvalidDistanceCm when out of range. Same sentinel rule as the grid/reset accessors. */
	UFUNCTION(BlueprintCallable, Category = "Race|Track|Checkpoints")
	double GetCheckpointGateDistanceCm(int32 GateIndex) const;

	/** World pose of a gate: X along the direction of travel, Y right, Z the gate's own up. Identity when out of range. */
	UFUNCTION(BlueprintCallable, Category = "Race|Track|Checkpoints")
	FTransform GetCheckpointGateTransform(int32 GateIndex) const;

	/**
	 * Test one motion segment against one gate, and report WHICH WAY it was crossed.
	 *
	 * This is TRACK-002's central query and the crossing-direction half of
	 * `.claude/rules/race-tests.md`'s "checkpoint order plus crossing direction
	 * authorizes laps". A reverse crossing comes back as
	 * ERacingGateCrossing::Reverse with bMatchesLegalDirection == false -- it is
	 * REPORTED, not silently swallowed, so RACE-002 can invalidate the lap and say why.
	 *
	 * FromWorldCm/ToWorldCm are the vehicle's previous and current positions. They need
	 * not be adjacent frames: the test is a segment/plane intersection, so a 300 km/h
	 * pass or a 400 ms hitch cannot tunnel through the gate.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Track|Checkpoints")
	FRacingGateCrossingResult EvaluateGateCrossing(int32 GateIndex, const FVector& FromWorldCm, const FVector& ToWorldCm) const;

	/**
	 * The gate this motion segment met EARLIEST, across every gate on the track.
	 *
	 * For the case a single evaluation step crosses more than one gate -- a hitch, a
	 * teleport, or two gates through a chicane. Ordering by where along the motion each
	 * plane was met is the order they physically happened in; ordering by gate index
	 * would not be.
	 *
	 * @return the gate index, or INDEX_NONE when nothing crossed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Track|Checkpoints")
	int32 FindFirstGateCrossing(const FVector& FromWorldCm, const FVector& ToWorldCm, FRacingGateCrossingResult& OutResult) const;

	/** Number of generated grid slots actually available. Matches NumGridSlots once the track is built. */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	int32 GetNumGridSlots() const;

	/** World transform of a grid slot. Slot 0 is pole. Returns identity for an out-of-range index. */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	FTransform GetGridSlotTransform(int32 SlotIndex) const;

	/**
	 * Arc-length distance of a grid slot along the centerline, in centimetres, or
	 * InvalidDistanceCm for an out-of-range index.
	 *
	 * THIS EXISTS BECAUSE A TRANSFORM ALONE IS NOT ENOUGH AT RACE START. A caller that
	 * only has the transform must recover the distance by calling
	 * FindNearestCenterlinePoint (the GLOBAL search), and this ticket's own
	 * RacingSim.Race.CenterlineAmbiguity test demonstrates the global search snapping
	 * to the wrong leg of a hairpin. Race start is precisely the moment no valid
	 * search hint exists yet, so the ambiguity is not hypothetical: the field is
	 * stationary on a grid that may sit on a section doubling back on itself.
	 *
	 * Use this to seed FindNearestCenterlinePointNear's hint for each car's first
	 * query of the session. See also GetResetSampleDistanceCm for the reset case.
	 *
	 * A negative sentinel rather than 0.0, because 0.0 is a legal arc length (the
	 * start/finish line) and an out-of-range index must not be mistakable for pole.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	double GetGridSlotDistanceCm(int32 SlotIndex) const;

	/**
	 * Grid slot pose and its arc-length distance in one call, so a caller physically
	 * cannot take the transform and forget the hint.
	 *
	 * @param SlotIndex     grid slot; 0 is pole.
	 * @param OutDistanceCm arc length of the slot along the centerline, InvalidDistanceCm when out of range.
	 * @return the pose, or identity when out of range.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	FTransform GetGridSlotPose(int32 SlotIndex, double& OutDistanceCm) const;

	/** Number of generated safe reset poses. */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	int32 GetNumResetSamples() const;

	/** World transform of a reset pose by index. Returns identity for an out-of-range index. */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	FTransform GetResetSampleTransform(int32 SampleIndex) const;

	/**
	 * Arc-length distance of a reset pose along the centerline, in centimetres, or
	 * InvalidDistanceCm for an out-of-range index.
	 *
	 * SAME REASON AS GetGridSlotDistanceCm, and the more urgent of the two. After a
	 * reset or a teleport the car's previous progress hint is by definition stale --
	 * that is what a reset means -- so VEH-005/RACE-002 must re-seed
	 * FindNearestCenterlinePointNear from the pose it just teleported to. Without this
	 * accessor the only way back to a distance is the global search, which is exactly
	 * the search CenterlineAmbiguity proves picks the wrong hairpin leg.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	double GetResetSampleDistanceCm(int32 SampleIndex) const;

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
	 * As GetResetTransformAtOrBeforeDistanceCm, but also returns the chosen sample's
	 * arc-length distance.
	 *
	 * PREFER THIS OVERLOAD AT EVERY RESET SITE. The distance is the search hint the
	 * caller needs for its next FindNearestCenterlinePointNear call, and a reset is
	 * the one moment the caller's existing hint is guaranteed wrong. Recovering it
	 * with the global FindNearestCenterlinePoint instead is the failure
	 * RacingSim.Race.CenterlineAmbiguity demonstrates.
	 *
	 * @param DistanceCm    arc length to search back from; wrapped first.
	 * @param OutIndex      index of the chosen sample, INDEX_NONE when none exists.
	 * @param OutDistanceCm arc length of the chosen sample, InvalidDistanceCm when none exists.
	 * @return the pose, or identity when the track has no reset samples.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	FTransform GetResetPoseAtOrBeforeDistanceCm(double DistanceCm, int32& OutIndex, double& OutDistanceCm) const;

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
	 *
	 * A BIGGER WINDOW IS NOT A SAFER WINDOW. There are THREE fallback triggers, not
	 * two, and the third is counter-intuitive: as well as a non-finite hint and a
	 * non-positive window, a window with `SearchWindowCm * 2 >= GetTrackLengthCm()`
	 * ALSO degrades to the global search -- a window that wide cannot exclude
	 * anything, so there is nothing left to disambiguate. Passing a deliberately
	 * generous window therefore silently reinstates the exact wrong-leg snapping this
	 * function exists to prevent. Keep SearchWindowCm well under half the lap; size it
	 * from tick rate times top speed plus margin, not from caution.
	 *
	 * Callers with no hint at all should seed from GetGridSlotDistanceCm (race start)
	 * or GetResetPoseAtOrBeforeDistanceCm (after a reset) rather than widening the
	 * window until the hint stops mattering.
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
	 * PREFER IsValidatedForRace()/GetCachedValidation() AT A CALL SITE. This function is
	 * the computation; those are the cache. See the block below for why the difference
	 * matters.
	 *
	 * @param OutReason set to a human-readable reason on failure; untouched on success.
	 * @return true when this track may be used for a result that will be published.
	 */
	bool Validate(FString& OutReason) const;

	// =======================================================================
	// Cached validity (TRACK-001 finding M7, extended to cover TRACK-002 M4)
	// =======================================================================
	//
	// WHY A CACHE AND NOT JUST Validate(). M7's complaint is precise: RebuildTrackData()
	// and Validate() disagree BY DESIGN -- the bake substitutes fallbacks (100 cm
	// spacing, 800 cm grid spacing, 2500 cm reset spacing, a clamped sample count) for
	// exactly the values Validate() rejects outright -- so a track can bake
	// "successfully" and still be unpublishable. IsTrackDataBuilt() is therefore a
	// STRICTLY WEAKER claim than "this track may be raced on", and it is the claim a
	// session-start path would naturally reach for. Validate() is the right question and
	// the wrong function to ask repeatedly: it re-runs ~25 branches and formats FStrings
	// on every call.
	//
	// AND IT MUST COVER THE GATE BAKE (TRACK-002 finding M4). RebuildTrackData() returns
	// **true** even when RebuildCheckpointGates() fails -- that function records
	// CheckpointGateBakeError and returns void, deliberately, because a mis-authored gate
	// must not take the centerline (and therefore progress and ranking) down with it. So
	// neither IsTrackDataBuilt() nor DidPostLoadBakeSucceed() can see a gate-bake failure,
	// and gates are what decide which laps count. A cache keyed only on the centerline
	// would miss exactly the case TRACK-002 introduced.
	//
	// This cache does not have that hole, and not by remembering to check: it memoises
	// **Validate() itself**, which already refuses a failed gate bake, a gate set below
	// MinCheckpointGateCount and a Reverse-only start/finish gate. Anything Validate()
	// learns to refuse later is covered the day it is added, with no second list to keep
	// in sync. RacingSim.Race.TrackValidationCache asserts the gate-bake case directly:
	// RebuildTrackData() true, IsValidatedForRace() false.
	//
	// KEYED ON ComputeContentHash(), which is what the ticket asks for and is the only
	// key that is right: it covers every authored field AND the effective bake resolution
	// the bake actually used, so a re-bake at a different spacing, a nudged spline point,
	// a retuned gate width or a changed MinCornerRadiusCm all move the key. The cost of a
	// cache hit is that hash (a walk of the spline points) rather than the full
	// validation. THIS IS A SESSION-START API, NOT A PER-FRAME ONE -- it is cheap enough
	// to call when a race director decides whether to go green and when a result is
	// frozen, and it is not cheap enough for Tick. Nothing in this project calls it per
	// frame and nothing should.

	/**
	 * May a session be started, and a result published, on this track? Cache-backed.
	 *
	 * Re-runs Validate() only when the content hash has moved since the cached answer was
	 * computed. GetValidationRunCount() is how automation proves that.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	bool IsValidatedForRace() const;

	/**
	 * As IsValidatedForRace(), but also yields the cached failure reason.
	 *
	 * @param OutReason the recorded reason, or an empty string when the track validates.
	 *                  Always written, unlike Validate()'s out-parameter -- a caller
	 *                  reading a cache should not have to know whether the previous
	 *                  occupant of its FString was left there by a different call.
	 */
	bool GetCachedValidation(FString& OutReason) const;

	/**
	 * How many times the cache has actually run Validate().
	 *
	 * Exists for automation, on exactly the reasoning GetBakeAttemptCount() records:
	 * "N calls produced one computation" cannot be asserted by timing (that measures the
	 * machine, and this project's automation gate shares a build host), and counting is
	 * the only honest alternative. Does NOT count direct Validate() calls -- it counts
	 * cache misses.
	 */
	int32 GetValidationRunCount() const { return ValidationRunCount; }

	/** True once a validation answer has been cached on this instance. */
	bool HasValidationCache() const { return bHasValidationCache; }

	/** The content hash the cached answer was computed against. Meaningless unless HasValidationCache(). */
	uint32 GetValidatedContentHash() const { return ValidatedContentHash; }

	/**
	 * Re-bake the centerline, grid and reset poses from the authored data.
	 *
	 * Called from OnConstruction, PostLoad, BeginPlay and (in editor) on any
	 * property change, and lazily ONCE by the first query if none of those has run --
	 * so a NewObject'd instance in a test behaves like a placed one. Game thread
	 * only, enforced by check(IsInGameThread()); it is not safe to call while another
	 * thread is querying.
	 *
	 * This is also the ONLY way to retry a bake that failed -- see bBakeAttempted.
	 *
	 * @return true when the resulting centerline is usable.
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|Track")
	bool RebuildTrackData();

	/** True once RebuildTrackData() has produced a usable centerline. */
	bool IsTrackDataBuilt() const { return bTrackDataBuilt && BakedCenterline.IsValid(); }

	/**
	 * True once a bake has been ATTEMPTED, whether or not it succeeded.
	 *
	 * Distinct from IsTrackDataBuilt() on purpose; the difference is the whole point.
	 * See bBakeAttempted.
	 */
	bool HasBakeBeenAttempted() const { return bBakeAttempted; }

	/**
	 * How many times RebuildTrackData() has run on this instance.
	 *
	 * Exists for automation: RacingSim.Race.TrackFailedBakeIsNotRetried asserts that N
	 * queries after a FAILED bake produce exactly one attempt. Counting attempts is the
	 * only honest way to assert that -- timing the queries would measure the machine,
	 * and this project's automation gate shares a build host with other agents.
	 */
	int32 GetBakeAttemptCount() const { return BakeAttemptCount; }

	/**
	 * Whether the bake performed by PostLoad() -- the LOAD-TIME bake -- succeeded.
	 *
	 * False for an instance that was never loaded from a package (a CDO, or an actor
	 * spawned at runtime): see HasPostLoadBakeRun() to tell "did not run" from "ran and
	 * failed".
	 *
	 * WHY THIS IS NOT REDUNDANT WITH IsTrackDataBuilt(). A placed actor is baked more
	 * than once on the way in: PostLoad bakes it, and then the editor initialises the
	 * loaded world, registers components and re-runs OnConstruction, which bakes it
	 * again. A PostLoad bake that FAILED -- because the spline component's own PostLoad
	 * had not run yet, which the engine does not guarantee -- would therefore be repaired
	 * silently, and IsTrackDataBuilt() would be true either way. This flag is the only
	 * thing that distinguishes them, and TRACK-001 review finding M5 is precisely the
	 * question it answers.
	 */
	bool DidPostLoadBakeSucceed() const { return bPostLoadBakeSucceeded; }

	/** True once PostLoad() has run a bake on this instance, whatever its outcome. */
	bool HasPostLoadBakeRun() const { return PostLoadBakeAttemptIndex != INDEX_NONE; }

	/**
	 * Which bake attempt PostLoad()'s was, 1-based, or INDEX_NONE if PostLoad has not run.
	 *
	 * 1 means nothing baked this instance before the load-time bake, which is what makes
	 * DidPostLoadBakeSucceed() a statement about the load path rather than about whatever
	 * happened to run first.
	 */
	int32 GetPostLoadBakeAttemptIndex() const { return PostLoadBakeAttemptIndex; }

	/**
	 * Number of centerline samples the last bake actually produced, and the step it
	 * actually used, in centimetres.
	 *
	 * These are the EFFECTIVE values, which are not always the authored ones: the bake
	 * substitutes 100 cm for a non-finite or non-positive CenterlineSampleSpacingCm and
	 * clamps the sample count at an internal ceiling. ComputeContentHash() covers these
	 * as well as the authored field, because two runs baked at different effective
	 * resolutions are not comparable even when the authored field matches.
	 *
	 * Zero when no bake has succeeded.
	 */
	int32 GetEffectiveSampleCount() const { return EffectiveSampleCount; }
	double GetEffectiveStepCm() const { return EffectiveStepCm; }

	/**
	 * Why the gate GENERATOR produced fewer gates than NumGeneratedCheckpointGates asked
	 * for, or an empty string when it did not reduce the count.
	 *
	 * Non-empty means the baked gate set is NOT the authored intent: the centerline was
	 * baked too coarsely to separate the requested gates, so the generator dropped some.
	 * Validate() quotes this in its failure reason when the surviving count falls below
	 * MinCheckpointGateCount; it is exposed here so a level-validation pass or an
	 * automation test can see the degradation directly rather than by string-matching a
	 * validation message.
	 */
	const FString& GetGeneratedGateClampNote() const { return GeneratedGateClampNote; }

	/**
	 * Is the gate generator's one-shot clamp WARNING armed -- i.e. would the next clamp be
	 * reported rather than suppressed?
	 *
	 * TRACK-002 finding R1-L3 shipped at RACE-002 with no direct assertion, and RACE-002's
	 * own finding L6 routed that gap here. The fix was that the one-shot flag is re-armed
	 * on TWO paths, not one -- by a generated bake that places every requested gate, AND
	 * by any bake that uses AUTHORED specs, because the authored path never calls the
	 * generator and so could never re-arm it. Without the second, a generated -> authored
	 * -> generated round trip swallowed the second clamp report entirely. That is a
	 * behaviour of a private bool, and the only observable it had was a log line the test
	 * fixture (a CDO) is deliberately excluded from emitting -- so it was untestable in
	 * practice. This accessor is the observable.
	 *
	 * Same justification GetBakeAttemptCount() and DidPostLoadBakeSucceed() record: the
	 * shipping game is willing to carry one bool accessor, and "has this track's gate
	 * degradation already been reported" is a question a level-validation pass has a
	 * direct interest in. GeneratedGateClampNote is recorded unconditionally regardless;
	 * this is only about the LOG.
	 */
	bool IsGeneratedGateClampReportArmed() const { return !bGateClampLogged; }

	/**
	 * Returned by the arc-length accessors for an out-of-range index.
	 *
	 * Negative, because every real arc length is in [0, TrackLength) and 0.0 is the
	 * start/finish line -- returning 0.0 for "no such slot" would be indistinguishable
	 * from "this slot is exactly on the line".
	 */
	static constexpr double InvalidDistanceCm = -1.0;

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

	/** Derived. Ordered checkpoint gates baked from CheckpointGateSpecs (or generated). Never authored directly. */
	UPROPERTY(Transient)
	FRacingCheckpointGateSet BakedCheckpointGates;

	/**
	 * Derived. Why the last gate bake failed, empty when it succeeded.
	 *
	 * Kept so Validate() can report the REAL reason -- "gate 2 is inside one centerline
	 * segment of gate 1" -- instead of the downstream symptom "the track has no gates",
	 * which is the same class of misdirection TRACK-001's bake-failure branch was
	 * corrected for.
	 */
	UPROPERTY(Transient)
	FString CheckpointGateBakeError;

	/**
	 * Derived. Why the GENERATOR produced fewer gates than NumGeneratedCheckpointGates
	 * asked for, empty when it produced exactly what was requested (and always empty on
	 * the authored path, which the generator never runs on).
	 *
	 * Separate from CheckpointGateBakeError because the two are opposite outcomes: the
	 * bake error means no gates exist, this means gates exist but fewer than were asked
	 * for. Before H1 the second case had no record at all -- the clamp just quietly
	 * returned a shorter array -- so a track that had lost three of its four gates to a
	 * coarse bake was indistinguishable from one that was authored that way.
	 */
	UPROPERTY(Transient)
	FString GeneratedGateClampNote;

	/** Derived. World-space grid poses, index 0 = pole. */
	UPROPERTY(Transient)
	TArray<FTransform> GridSlotTransforms;

	/** Derived. Arc-length distance of each entry in GridSlotTransforms. Parallel array, same indices. */
	UPROPERTY(Transient)
	TArray<double> GridSlotDistancesCm;

	/** Derived. World-space reset poses, ascending in arc length, [0] at distance 0. */
	UPROPERTY(Transient)
	TArray<FTransform> ResetSampleTransforms;

	/** Derived. Arc-length distance of each entry in ResetSampleTransforms. */
	UPROPERTY(Transient)
	TArray<double> ResetSampleDistancesCm;

	/** True when the last bake SUCCEEDED. */
	UPROPERTY(Transient)
	bool bTrackDataBuilt = false;

	/**
	 * True once a bake has been ATTEMPTED, regardless of outcome.
	 *
	 * SEPARATE FROM bTrackDataBuilt BECAUSE A FAILED BAKE MUST NOT BE RETRIED PER
	 * QUERY. A freshly placed, not-yet-authored actor has an empty spline, so its bake
	 * fails -- and that is the normal state of the actor for as long as it takes
	 * someone to draw a circuit. Gating the lazy build on "did it succeed" meant every
	 * single call to GetCenterline() (and therefore GetTrackLengthCm,
	 * GetSectorIndexAtDistanceCm, GetRaceProgressCm, FindNearestCenterlinePoint, ...)
	 * re-ran the whole sample loop with its heap allocations and emitted a fresh
	 * warning. At 60 Hz per car that is a log flood and a per-frame allocation storm on
	 * exactly the CLAUDE.md rules this project calls out.
	 *
	 * Attempt once, cache the outcome. The only ways to retry are the explicit
	 * lifecycle hooks (OnConstruction/PostLoad/BeginPlay/PostEditChangeProperty) and a
	 * direct RebuildTrackData() call -- i.e. the moments the underlying spline data can
	 * actually have changed.
	 */
	UPROPERTY(Transient)
	bool bBakeAttempted = false;

	/** One-shot guard so a repeatedly failing rebuild logs its reason once, not once per call. */
	UPROPERTY(Transient)
	bool bBakeFailureLogged = false;

	/**
	 * One-shot guard for the gate generator's clamp message, re-armed by a bake that
	 * places every requested gate.
	 *
	 * Separate from bBakeFailureLogged because the clamp fires on bakes that SUCCEED. A
	 * freshly placed actor's default two-point 200 cm spline bakes fine and supports
	 * exactly one gate, so without this the actor warns on every OnConstruction and every
	 * property edit for as long as it takes somebody to draw a circuit -- the same flood
	 * bBakeFailureLogged exists to prevent, in a case its own condition never sees.
	 *
	 * Suppresses only the LOG. GeneratedGateClampNote is recorded on every bake.
	 *
	 * RE-ARMED ON TWO PATHS, not one (R1-L3, RACE-002): by a generated bake that places
	 * every requested gate, AND by any bake that uses AUTHORED specs -- the authored path
	 * never calls the generator, so without the second the flag could stay armed across a
	 * generated -> authored -> generated round trip and swallow the second clamp report.
	 *
	 * NEVER LOGGED FOR A TEMPLATE/CDO at all (R1-M2, RACE-002): a class default object is
	 * not a track, its default two-point spline supports one gate, and the test fixture
	 * that borrows the CDO made every suite that touched it emit the same meaningless
	 * warning.
	 */
	UPROPERTY(Transient)
	bool bGateClampLogged = false;

	/** Number of times RebuildTrackData() has run. Automation reads this; nothing else should. */
	UPROPERTY(Transient)
	int32 BakeAttemptCount = 0;

	/** Outcome of the bake PostLoad() ran. See DidPostLoadBakeSucceed(). */
	UPROPERTY(Transient)
	bool bPostLoadBakeSucceeded = false;

	/** 1-based index of PostLoad()'s bake among this instance's bakes, INDEX_NONE until PostLoad runs. */
	UPROPERTY(Transient)
	int32 PostLoadBakeAttemptIndex = INDEX_NONE;

	/** Sample count the last SUCCESSFUL bake produced. Hashed; see ComputeContentHash. */
	UPROPERTY(Transient)
	int32 EffectiveSampleCount = 0;

	/** Step, cm, the last SUCCESSFUL bake used. Not necessarily CenterlineSampleSpacingCm. */
	UPROPERTY(Transient)
	double EffectiveStepCm = 0.0;

	// -- Validation cache (TRACK-001 M7 / TRACK-002 M4). See IsValidatedForRace(). ----

	/**
	 * Content hash the cached answer was computed against.
	 *
	 * Paired with bHasValidationCache rather than using 0 as "no cache": 0 is a value
	 * ComputeContentHash() can legitimately return, and a sentinel that collides with a
	 * legal value is how a cache silently stops caching. The same mistake
	 * FRacingContentVersion avoids by documenting 0 as "not computed" for a field nothing
	 * compares.
	 */
	UPROPERTY(Transient)
	uint32 ValidatedContentHash = 0;

	UPROPERTY(Transient)
	bool bHasValidationCache = false;

	/** The cached answer itself. */
	UPROPERTY(Transient)
	bool bCachedValidationResult = false;

	/** The cached failure reason, empty when the cached answer is "valid". */
	UPROPERTY(Transient)
	FString CachedValidationReason;

	/** Cache MISSES, i.e. how many times Validate() actually ran. See GetValidationRunCount(). */
	UPROPERTY(Transient)
	int32 ValidationRunCount = 0;

	/**
	 * Build on first use if nothing else has, and NEVER more than once for a given
	 * spline state -- a failed bake is cached as a failure, not retried per query.
	 *
	 * const_cast rather than mutable members: the derived data is genuinely derived,
	 * so rebuilding it does not change what this actor MEANS, and marking eleven
	 * members mutable would weaken const on all of them individually. Game thread
	 * only, enforced by check(IsInGameThread()) rather than by comment.
	 */
	void EnsureTrackDataBuilt() const;

	/** Generate GridSlotTransforms from the baked centerline. Requires a valid centerline. */
	void RebuildGridSlots();

	/**
	 * Bake BakedCheckpointGates from CheckpointGateSpecs, or from the generator when
	 * that array is empty. Requires a valid centerline.
	 *
	 * Records CheckpointGateBakeError instead of returning a bool, because a gate bake
	 * can fail on a track whose CENTERLINE is perfectly good, and failing the whole
	 * centerline bake for a mis-authored gate would take progress and ranking down with
	 * it. Validate() is where a bad gate set stops a session.
	 */
	void RebuildCheckpointGates();

	/**
	 * Fill OutSpecs with evenly spaced generated gates, gate 0 at distance 0. Used when
	 * CheckpointGateSpecs is empty.
	 *
	 * NON-CONST because it records GeneratedGateClampNote. It used to be const and to
	 * clamp its own count SILENTLY, which is how a coarse bake could degrade a four-gate
	 * request to one gate with nothing anywhere saying so (finding H1). The note is the
	 * clamp's testimony; Validate() quotes it, and the clamp also logs once per bake.
	 */
	void MakeGeneratedGateSpecs(TArray<FRacingCheckpointGateSpec>& OutSpecs);

	/** Generate ResetSampleTransforms/Distances from the baked centerline. Requires a valid centerline. */
	void RebuildResetSamples();

	/** Lift a centerline transform by PoseHeightOffsetCm and slide it sideways. Shared by grid and reset poses. */
	FTransform MakePoseAtDistance(double DistanceCm, double LateralOffsetCm) const;
};
