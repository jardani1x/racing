// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/RacingSimTypes.h"
#include "RacingSimBuildId.generated.h"

/**
 * CORE-002 versioning contract.
 *
 * Docs/15-ProjectStructure.md, "Versioning", requires every competitive result to
 * record eight things. FRacingSimVersionStamp is that list, as one serialisable
 * struct, so a result cannot be written with half of them:
 *
 *   1. game build ID                 -> GameBuildId
 *   2. engine patch                  -> EngineVersion, EngineChangelist
 *   3. track definition/version hash -> TrackVersion       (populated by TRACK-001)
 *   4. car spec/tune version         -> CarSpecVersion     (populated by VEH-003)
 *   5. physics policy version        -> PhysicsPolicyVersion
 *   6. assist preset                 -> AssistPreset
 *   7. input type                    -> InputDeviceType
 *   8. validity/penalty state        -> Validity, Penalties
 *
 * This file is a CONTRACT ONLY. It deliberately does not know what a track or a
 * car is; TRACK-001 and VEH-003 fill those fields from their own typed assets.
 * Anything here that reaches out to the world is limited to values the engine
 * and the project settings already know.
 */

/**
 * Identity of one versioned piece of authored data (a track definition, a car
 * spec, a ruleset).
 *
 * Three fields, because they answer three different questions and collapsing
 * them loses information:
 *
 *   AssetId       - which thing is it;
 *   SchemaVersion - can this build's C++ even read it (a code-side integer,
 *                   bumped by hand when the struct layout changes);
 *   ContentHash   - did the authored values change (a data-side hash, computed
 *                   from the asset's contents by CORE-003's validation pass).
 *
 * A schema bump with an unchanged hash means the code moved; a hash change with
 * an unchanged schema means someone retuned the data. Both invalidate a
 * leaderboard comparison, and only both together tell you why.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FRacingContentVersion
{
	GENERATED_BODY()

	/** Stable identifier of the asset, e.g. "Track.Prototype.Meridian". NAME_None means "not populated". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Racing|Version")
	FName AssetId;

	/** Layout version of the C++ struct that reads this asset. Hand-bumped. 0 means "not populated". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Racing|Version", meta = (ClampMin = "0"))
	int32 SchemaVersion = 0;

	/**
	 * Hash of the authored values. 0 means "not computed".
	 *
	 * uint32 rather than a string: it is compared far more often than it is
	 * printed, and a fixed-width integer keeps FRacingSimVersionStamp cheap to
	 * copy into every telemetry frame. Printed as 8 hex digits by ToString().
	 * Collision risk is accepted -- this detects accidental drift between builds,
	 * it is not a tamper-proof signature, and it must never be used as one.
	 *
	 * EditAnywhere only, not BlueprintReadOnly: uint32 has no native Blueprint
	 * type (UHT error "Type 'uint32' is not supported by blueprint"). Use
	 * ToString() to expose this value to Blueprint.
	 */
	UPROPERTY(EditAnywhere, Category = "Racing|Version")
	uint32 ContentHash = 0;

	/** True once something has actually filled this in. An unpopulated version can never be published. */
	bool IsPopulated() const
	{
		return !AssetId.IsNone() && SchemaVersion > 0;
	}

	/** "Track.Prototype.Meridian@3#1a2b3c4d" */
	FString ToString() const;

	bool operator==(const FRacingContentVersion& Other) const
	{
		return AssetId == Other.AssetId
			&& SchemaVersion == Other.SchemaVersion
			&& ContentHash == Other.ContentHash;
	}

	bool operator!=(const FRacingContentVersion& Other) const
	{
		return !(*this == Other);
	}
};

/**
 * The game build ID: which executable produced a result.
 *
 * Two schemes (ERacingBuildIdScheme). Derived is always available and is what a
 * developer machine uses; Explicit is stamped by CI and is the only one a
 * published leaderboard may trust, because a derived ID cannot distinguish two
 * different local source trees built from the same engine patch.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FRacingSimBuildId
{
	GENERATED_BODY()

	/** The composed identifier. Never empty after Current(); "unset" if never resolved. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Racing|Version")
	FString Value;

	/** Which scheme produced Value. Recorded so a reader knows how much to trust it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Racing|Version")
	ERacingBuildIdScheme Scheme = ERacingBuildIdScheme::Derived;

	/**
	 * True when this ID is unique per build and traceable back to the CI run that
	 * produced it: the Explicit scheme, a non-empty stamped value, AND a value
	 * whose CHARACTERS survived sanitisation unchanged.
	 *
	 * That last condition is CORE-003's fix for CORE-002 finding MEDIUM-1. A
	 * mutated stamp is not authoritative, because sanitisation is lossy --
	 * "feature/x" and "feature-x" both record as "featurex", so the flag could
	 * otherwise promise uniqueness for two different builds sharing one ID.
	 *
	 * PRECISELY WHAT IS CHECKED (C3-7, corrected at RACE-002; the comment previously
	 * read "survived sanitisation byte-for-byte", which overstated it). The comparison
	 * is against the TRIMMED stamp, so leading or trailing whitespace is silently
	 * removed and the ID still counts as authoritative. That is deliberate rather than
	 * an oversight: trimming cannot map two distinct stamps onto one identifier, which
	 * is the collision this flag exists to prevent, and a CI variable with a stray
	 * newline is common enough that failing it closed would produce unlabelled
	 * developer results for a formatting accident. Character-level mutation inside the
	 * trimmed value is what makes it false.
	 *
	 * Results produced with bIsAuthoritative == false are developer results and
	 * must be labelled as such wherever they are shown; IsPublishable() refuses
	 * them outright.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Racing|Version")
	bool bIsAuthoritative = false;

	/**
	 * Resolve the build ID for the running executable.
	 *
	 * Reads URacingSimSettings for the scheme, the channel and any CI-stamped
	 * value; reads FEngineVersion/FApp for everything else. Pure with respect to
	 * the world -- safe to call before a world exists, and safe in a commandlet.
	 * Cheap enough to call per race, not per frame.
	 */
	static FRacingSimBuildId Current();

	bool operator==(const FRacingSimBuildId& Other) const
	{
		return Value == Other.Value && Scheme == Other.Scheme;
	}

	bool operator!=(const FRacingSimBuildId& Other) const
	{
		return !(*this == Other);
	}
};

/**
 * Everything a competitive result must carry to be interpretable later.
 *
 * Copy this into a result the moment the run *starts*, not when it ends: if the
 * settings or the loaded car change mid-session, the stamp must describe the run
 * that happened.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FRacingSimVersionStamp
{
	GENERATED_BODY()

	// -- 1. game build ID ---------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Racing|Version")
	FRacingSimBuildId GameBuildId;

	// -- 2. engine patch ----------------------------------------------------

	/** "5.8.1" -- major.minor.patch, no changelist. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Racing|Version")
	FString EngineVersion;

	/**
	 * Engine changelist, e.g. 56057345. Recorded separately from EngineVersion
	 * because two 5.8.1 installs from different changelists are genuinely
	 * different physics; the patch string alone would hide that.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Racing|Version")
	int32 EngineChangelist = 0;

	// -- 3/4. authored data -------------------------------------------------

	/** Populated by TRACK-001 from the track definition asset. Empty here by design. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Racing|Version")
	FRacingContentVersion TrackVersion;

	/** Populated by VEH-003 from the car spec / tune asset. Empty here by design. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Racing|Version")
	FRacingContentVersion CarSpecVersion;

	/** Populated by RACE-001/RACE-002 from the ruleset asset (lap rules, penalties, gate policy). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Racing|Version")
	FRacingContentVersion RulesetVersion;

	// -- 5. physics policy --------------------------------------------------

	/**
	 * Bumped by hand whenever the substepping/fixed-tick policy or any
	 * simulation-affecting default changes. A lap set under a different physics
	 * policy is not comparable even if every asset hash matches.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Racing|Version", meta = (ClampMin = "0"))
	int32 PhysicsPolicyVersion = 0;

	// -- 6/7. run conditions ------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Racing|Version")
	ERacingAssistPreset AssistPreset = ERacingAssistPreset::Raw;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Racing|Version")
	ERacingInputDeviceType InputDeviceType = ERacingInputDeviceType::Unknown;

	// -- 8. validity / penalty ----------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Racing|Version")
	ERacingRunValidity Validity = ERacingRunValidity::Unknown;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Racing|Version")
	FRacingPenaltySummary Penalties;

	/**
	 * Fill in everything this build already knows: build ID, engine patch,
	 * physics policy version and the configured assist preset. Leaves
	 * TrackVersion, CarSpecVersion, RulesetVersion, InputDeviceType and Validity
	 * for their owning systems -- an unpopulated field is honest; a guessed one
	 * is a silently wrong leaderboard.
	 */
	static FRacingSimVersionStamp MakeCurrent();

	/**
	 * True when every field required to publish a result has been filled by its
	 * owner. Checked before a result is written, so a half-populated stamp fails
	 * loudly at the boundary instead of reaching a leaderboard.
	 */
	bool IsPublishable(FString* OutReason = nullptr) const;

	/**
	 * Can two runs be placed on the same leaderboard?
	 *
	 * Requires identical track, car spec, ruleset, physics policy and assist
	 * preset. Deliberately does NOT require an identical build ID: a pure
	 * rendering or UI change produces a new build and must not split the board,
	 * and the asset hashes plus the physics policy version already catch the
	 * changes that matter. Deliberately does NOT require the same input device
	 * either -- input is recorded for filtering, and whether wheel and gamepad
	 * share a board is a game-design call owed at RACE-003, not a data-contract
	 * call owed here.
	 *
	 * COUNTER-CASE, recorded rather than hidden: this is the weakest rule in the
	 * file. An engine changelist change alters Chaos behaviour without touching
	 * any of the compared fields, so two comparable-by-this-rule runs can be
	 * physically different. Tightening it to include EngineChangelist would
	 * invalidate every board on an engine hotfix. The chosen trade is: compare
	 * across builds, and keep EngineChangelist in the stamp so a suspicious
	 * result can always be explained after the fact.
	 */
	bool IsComparableTo(const FRacingSimVersionStamp& Other) const;

	/** Single-line form for logs and result files. Not localised, not for the HUD. */
	FString ToString() const;
};
