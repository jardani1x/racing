// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/RacingSimBuildId.h"
#include "RaceRulesetDataAsset.generated.h"

/**
 * RACE-001: the typed home for race-session rules.
 *
 * WHY THIS EXISTS AT ALL, given RACE-001 is a state machine ticket. The countdown
 * has a length. A length is a tunable, and CLAUDE.md is explicit that tunables live
 * in typed DataAssets or config rather than as magic numbers -- and
 * URacingSimSettings' own class comment (CORE-002) refuses race tuning by name and
 * points at this ticket. A `3.0f` in RaceStateMachine.cpp would have been the
 * cheapest possible violation of the project's own rule, in the first race file
 * written.
 *
 * WHY IT IS ALMOST EMPTY. RACE-002 owns lap and sector rules, RACE-003 owns results
 * and penalties, TRACK-002 owns gate policy. RACE-001 is deliberately track-agnostic
 * (see the ticket), so this asset holds exactly the rules the state machine itself
 * enforces and nothing speculative. It is a place, correctly versioned, for those
 * tickets to add to.
 *
 * IT IS NOT A TRACK. No checkpoint, no lap count, no spline, no map reference. A
 * ruleset is meant to outlive any one circuit: the same rules apply on every track.
 * TRACK-001's ATrackDefinitionActor carries its own FRacingContentVersion, and
 * FRacingSimVersionStamp keeps the two separate for exactly this reason.
 *
 * VERSIONING. GetContentVersion() fills the FRacingContentVersion that CORE-002
 * already reserved as FRacingSimVersionStamp::RulesetVersion ("Populated by
 * RACE-001/RACE-002 from the ruleset asset"). SchemaVersion is hand-bumped when the
 * C++ layout changes; ContentHash is computed from the authored values, so a
 * designer retuning the countdown is visible on a result without a code change.
 * Any ticket adding a field here MUST bump RulesetSchemaVersion and add the field
 * to ComputeContentHash(), or results will silently claim two different rulesets
 * were the same one.
 */
UCLASS(BlueprintType)
class RACINGSIM_API URaceRulesetDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Layout version of the C++ that reads this asset. Hand-bumped, never derived.
	 *
	 * 1 = RACE-001: RulesetId + CountdownSeconds.
	 * 2 = RACE-002: bResetInvalidatesLap. A version-1 result and a version-2 result are
	 *     not comparable even on identical geometry, because this field decides whether
	 *     a lap containing a reset counts at all.
	 */
	static constexpr int32 RulesetSchemaVersion = 2;

	/**
	 * Stable identifier written into every result, e.g. "Ruleset.TimeTrial.Default".
	 *
	 * A name rather than the asset path: the path changes when content is moved and
	 * would invalidate historical results for a reason that has nothing to do with
	 * the rules. NAME_None means unpopulated, and FRacingContentVersion::IsPopulated
	 * will reject it, so an unnamed ruleset cannot reach a published result.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Ruleset")
	FName RulesetId;

	/**
	 * How long ERaceState::Countdown lasts, in SECONDS, before the state machine
	 * releases drive input and starts the race clock.
	 *
	 * Measured against the same monotonic source as the race clock, so it neither
	 * drifts with frame rate nor shortens under a hitch -- a countdown that ran
	 * short would hand a driver a rolling start.
	 *
	 * 0.0 is legal and means "advance on the first poll after entering Countdown".
	 * It is the value automation uses to skip the wait; it is not a sensible
	 * gameplay value, which is why Validate() reports it rather than silently
	 * accepting it as a preference.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Ruleset", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0", ForceUnits = "s"))
	double CountdownSeconds = 3.0;

	/**
	 * Does resetting/teleporting the vehicle invalidate the lap it happened on?
	 *
	 * RACE-002. Docs/03-TrackRaceUI.md rule 8 explicitly defers this to the ruleset:
	 * "Reset returns the car to the most recent safe valid sample and may invalidate or
	 * penalize the lap according to the ruleset." This is that decision, made once,
	 * here, rather than implied by whatever URaceLapTracker happened to do.
	 *
	 * TRUE BY DEFAULT, and the default is the safe direction:
	 *
	 *   - a reset places the car somewhere it did not drive to. The pose is at or BEFORE
	 *     where it left the road (TRACK-001's GetResetTransformAtOrBeforeDistanceCm can
	 *     never award distance), so it is not a shortcut -- but it is a free, on-line,
	 *     stationary recovery from a moment the driver had lost the car, and a lap
	 *     containing one is not comparable with a lap that does not;
	 *   - failing open would make "reset" the cheapest way to survive a mistake on a
	 *     timed lap, which is the reset-awards-progress failure Gate B names;
	 *   - the error is asymmetric. A lap wrongly marked INVALID is visible and can be
	 *     re-driven; a lap wrongly marked VALID is indistinguishable from a clean one by
	 *     the time it reaches a leaderboard.
	 *
	 * Set false for a practice/exploration ruleset where resets are free. The reset is
	 * still recorded on the lap either way -- the flag decides validity, not visibility.
	 *
	 * NO RANGE TABLE, DELIBERATELY: a bool has no range to validate, so CORE-003's
	 * EnforceRanges/Validate() split (C3-1) has nothing to enforce here and the
	 * DataAsset property-flag gap (C3-2) is not triggered. See RACE-002's evidence
	 * section in Docs/Tickets.md.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Ruleset")
	bool bResetInvalidatesLap = true;

	/**
	 * The identity of this ruleset for FRacingSimVersionStamp::RulesetVersion.
	 * Cheap, but not free (it hashes) -- call it once when a run starts, not per
	 * frame and not per transition.
	 */
	FRacingContentVersion GetContentVersion() const;

	/**
	 * Hash of the authored values only. Deliberately excludes SchemaVersion, which
	 * FRacingContentVersion records separately: "the code changed" and "the data
	 * changed" are different questions and collapsing them loses the answer to both.
	 */
	uint32 ComputeContentHash() const;

	/**
	 * Content validation, callable from automation and from a future editor
	 * validation pass.
	 *
	 * A plain function rather than an IsDataValid override on purpose: the engine's
	 * validation signature has changed across UE versions and its context type lives
	 * in an editor-only module, so overriding it would either pull an editor
	 * dependency into a runtime module or need a WITH_EDITOR fork that automation
	 * running in a commandlet could not exercise. CORE-003 built a reusable
	 * range-enforcement framework (RacingSim::Validation::EnforceRanges) but
	 * deliberately left this asset untouched, since RACE-001 owned it and was
	 * in flight concurrently (see Docs/Tickets.md, "CORE-003 -- decision:
	 * URaceRulesetDataAsset::Validate() stays separate"). RACE-002 is the
	 * recommended owner of adopting that framework here: declare a range table
	 * as URacingSimSettings::GetValidatedPropertyRanges() does, call
	 * EnforceRanges from PostLoad(), and have Validate() call it first and fail
	 * if any range check fails, before the content-specific checks below.
	 *
	 * @param OutReason set to a human-readable reason on failure; untouched on success.
	 * @return true when this asset may be used for a result that will be published.
	 */
	bool Validate(FString& OutReason) const;
};
