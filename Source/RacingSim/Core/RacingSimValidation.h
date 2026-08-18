// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

class FProperty;
class UClass;
class UObject;

/**
 * CORE-003: reusable range validation for config-backed and DataAsset-backed
 * numeric properties.
 *
 * ---------------------------------------------------------------------------
 * The defect this closes (CORE-002 finding M-5)
 * ---------------------------------------------------------------------------
 *
 * `meta = (ClampMin = "0", ClampMax = "3")` on a UPROPERTY constrains exactly one
 * thing: the spin box in the editor's Details panel. It is not consulted when a
 * value arrives from an ini or from
 *
 *   -ini:Game:[/Script/RacingSim.RacingSimSettings]:LapTimeFractionalDigits=99
 *
 * so `99` loads unchallenged, and so does `TelemetrySampleRateHz=1e6`, a negative
 * `PhysicsPolicyVersion`, and `nan`. The declared range reads like an invariant
 * and is not one. This module makes it one on every load path.
 *
 * ---------------------------------------------------------------------------
 * Why the ranges are declared in C++ and not simply read back from the metadata
 * ---------------------------------------------------------------------------
 *
 * This is the single most important design decision in this file, and the
 * obvious implementation is wrong.
 *
 * `FField::HasMetaData`/`GetMetaData` are compiled out unless `WITH_METADATA` is
 * 1, and `CoreMiscDefines.h:31` defines `WITH_METADATA` as `WITH_EDITORONLY_DATA`
 * -- which is 0 in every non-editor target, i.e. in the packaged Game build that
 * a CI `-ini:` override actually ships to. A validation pass that reads the
 * metadata directly would enforce the range in the editor, silently enforce
 * nothing in the packaged build, and pass every test (automation runs in the
 * editor). That failure mode is strictly worse than the defect it replaces,
 * because it would come with green evidence.
 *
 * So: the ranges are declared as data (`FRacingPropertyRange`) next to the class
 * that owns them, and this pass is what applies them -- identically in every
 * build configuration. The `UPROPERTY` metadata remains, because it is what makes
 * the Details panel behave, and `VerifyRangesMatchMetadata()` proves the two
 * agree. The duplication is real; it is made safe by being asserted, in a test
 * that also fails when a *new* clamped property is added and the table is not
 * updated. That last direction is the one that caused M-5 in the first place.
 *
 * ---------------------------------------------------------------------------
 * Policy: correct, do not refuse to start
 * ---------------------------------------------------------------------------
 *
 * An out-of-range value is corrected and logged as a Warning naming the
 * property, the loaded value and the corrected value. It is not a fatal error,
 * for two reasons: a settings CDO has no "last known good" value to fall back to
 * (it *is* the defaults object), and refusing to boot a Pixel Streaming worker
 * over an ini typo converts a cosmetic misconfiguration into an outage.
 *
 * ---------------------------------------------------------------------------
 * Why "clamp to the nearest bound" is NOT always the safe correction
 * ---------------------------------------------------------------------------
 *
 * An earlier version of this file claimed "every range in this project has its
 * safe end at the minimum, so a nonsense value degrades toward unpublishable".
 * That was false, and the counter-example is in this very module's settings.
 *
 * URacingSimSettings::TelemetryStaleAfterSeconds has ClampMin = "0.0", but 0 is
 * not "the least staleness" -- FRacingTelemetryFrame::IsStaleAt treats
 * MaxAgeSeconds <= 0 as "staleness checking disabled, nothing is ever stale"
 * (RacingTelemetry.cpp). So clamping a NaN or a negative to 0.0 would take an
 * obviously broken config and turn it into a silently permissive one: the HUD
 * would happily present a frozen speedometer from a dead producer as live, which
 * is the exact failure that field exists to prevent.
 *
 * Hence FRacingPropertyRange::ReplacementValue. A property whose bound is not
 * its safe value declares what to substitute instead, and gets its guard back
 * armed rather than disarmed. Properties that do not declare one clamp to the
 * nearest bound as before, which remains correct for them.
 */

namespace RacingSim::Validation
{
	/** What the pass did to one property. */
	enum class ERangeAction : uint8
	{
		/** Value was already inside the declared range. No write occurred. */
		InRange,
		/** Value was below the declared minimum and was raised to it. */
		ClampedToMin,
		/** Value was above the declared maximum and was lowered to it. */
		ClampedToMax,
		/** Value was NaN or infinite and was replaced; see FRacingValidationIssue. */
		ReplacedNonFinite,
		/**
		 * Value was out of range on a property that declares a ReplacementValue,
		 * so it was replaced by that value rather than clamped to the bound it
		 * violated. See the header comment on why clamping is not always safe.
		 */
		ReplacedOutOfRange,
		/**
		 * The range could not be applied at all -- the property does not exist,
		 * or is not a plain numeric property. Always a programming error in the
		 * range table, never bad user data, so it is reported separately.
		 */
		Failed
	};

	/**
	 * One declared numeric range, mirroring a UPROPERTY's ClampMin/ClampMax.
	 *
	 * Bounds are stored as double regardless of the property's own type: double
	 * represents every int32 exactly, and every float exactly, so nothing is lost
	 * on the way in. Integer properties round the bound *inward* when applying it
	 * (ceil the minimum, floor the maximum) so a fractional bound can never widen
	 * an integer range.
	 *
	 * Deliberately mirrors ClampMin/ClampMax only, NOT UIMin/UIMax. UIMin/UIMax
	 * are slider hints, not invariants -- URacingSimSettings::TelemetryStaleAfterSeconds
	 * carries UIMax = 5.0 with no ClampMax, and 10.0 seconds is a legal (if odd)
	 * value. Treating a slider hint as a hard bound would silently clamp valid data.
	 *
	 * PRECISION LIMIT, and the reason FUInt64Property is rejected outright while
	 * FInt64Property is not: a double represents every int32 exactly, but only
	 * integers up to 2^53 exactly. A bound declared here for an int64 property
	 * beyond 2^53 would be rounded on the way in, so the enforced bound could
	 * differ from the declared one by a few units. No int64 UPROPERTY exists in
	 * this codebase, so this is documented rather than guarded; a future one with
	 * bounds that large needs a hand-written check instead. uint64 is rejected
	 * because it is worse than imprecise -- FNumericProperty's integer accessors
	 * are int64-based, so a value above INT64_MAX reads as negative and would be
	 * "corrected" into garbage.
	 */
	struct FRacingPropertyRange
	{
		/** Name of the UPROPERTY on the validated object's class. */
		FName PropertyName;

		bool bHasMin = false;
		double Min = 0.0;

		bool bHasMax = false;
		double Max = 0.0;

		/**
		 * Value substituted for ANY correction on this property -- out of range
		 * at either end, or non-finite -- instead of the violated bound.
		 *
		 * Declare this when a bound is not the property's safe value. The case
		 * that forced it into existence: TelemetryStaleAfterSeconds' ClampMin is
		 * 0.0, and 0.0 means "disable staleness checking", so clamping a broken
		 * value to the minimum disarms a HUD safety guard. See the header.
		 *
		 * Left unset, corrections clamp to the nearest bound, which is right for
		 * every property whose minimum genuinely is its least-permissive value.
		 */
		bool bHasReplacement = false;
		double ReplacementValue = 0.0;

		/** Declare the safe substitute for any correction on this property. */
		FRacingPropertyRange& WithReplacement(const double InReplacement)
		{
			bHasReplacement = true;
			ReplacementValue = InReplacement;
			return *this;
		}

		/** Mirrors meta = (ClampMin = "InMin", ClampMax = "InMax"). */
		static FRacingPropertyRange Between(const FName InName, const double InMin, const double InMax)
		{
			FRacingPropertyRange Range;
			Range.PropertyName = InName;
			Range.bHasMin = true;
			Range.Min = InMin;
			Range.bHasMax = true;
			Range.Max = InMax;
			return Range;
		}

		/** Mirrors meta = (ClampMin = "InMin") with no ClampMax. */
		static FRacingPropertyRange AtLeast(const FName InName, const double InMin)
		{
			FRacingPropertyRange Range;
			Range.PropertyName = InName;
			Range.bHasMin = true;
			Range.Min = InMin;
			return Range;
		}

		/** Mirrors meta = (ClampMax = "InMax") with no ClampMin. */
		static FRacingPropertyRange AtMost(const FName InName, const double InMax)
		{
			FRacingPropertyRange Range;
			Range.PropertyName = InName;
			Range.bHasMax = true;
			Range.Max = InMax;
			return Range;
		}
	};

	/** One property the pass had something to say about. */
	struct FRacingValidationIssue
	{
		FName PropertyName;
		ERangeAction Action = ERangeAction::InRange;

		/** The value as loaded, before correction. Meaningless when Action == Failed. */
		double LoadedValue = 0.0;

		/** The value after correction. Equal to LoadedValue when nothing was written. */
		double CorrectedValue = 0.0;

		/** Human-readable, already naming the property and both values. Not localised. */
		FString Message;
	};

	/**
	 * Outcome of one validation pass.
	 *
	 * The inline allocator sizes for "a handful of properties were wrong", which
	 * is the realistic worst case for a hand-edited ini; a clean pass never
	 * allocates at all. This runs at CDO construction and on explicit reload, not
	 * per frame, but CLAUDE.md's no-gratuitous-allocation rule is cheap to honour
	 * here and the type is shared with DataAsset validation that may run per race.
	 */
	struct FRacingValidationResult
	{
		TArray<FRacingValidationIssue, TInlineAllocator<4>> Issues;

		/** True when every declared range applied and every value was already in range. */
		bool IsClean() const
		{
			return Issues.Num() == 0;
		}

		/** Number of values this pass actually wrote back. */
		RACINGSIM_API int32 NumCorrected() const;

		/** Number of ranges that could not be applied at all (a range-table bug). */
		RACINGSIM_API int32 NumFailed() const;

		/** True when a specific property was corrected. For tests and diagnostics. */
		RACINGSIM_API bool WasCorrected(const FName PropertyName) const;

		/** Single line per issue, newline-joined. Empty when clean. */
		RACINGSIM_API FString ToString() const;
	};

	/**
	 * Re-apply the declared ranges to Object's live property values.
	 *
	 * Reflection-driven: each range is resolved by name to an FProperty on
	 * Object's class and read/written through FNumericProperty, so this works for
	 * any int8..int32/float/double UPROPERTY without the caller writing per-field
	 * code. Call it AFTER config has been loaded -- for a config CDO that means
	 * PostInitProperties, which UObjectGlobals.cpp runs at line 4320, after
	 * LoadConfig at line 4274 (verified in UE 5.8.1 source, not assumed).
	 *
	 * Non-finite and out-of-range handling: NaN and +/-Inf compare false against
	 * every bound, so they would slip through a naive clamp. A declared
	 * ReplacementValue (see WithReplacement()) wins first if the range has one.
	 * Otherwise: below-min or non-finite uses Min when the range has one, else
	 * Max, else 0.0; above-max uses Max. A non-finite value carries no
	 * information, so there is no "nearest legal value" to move it to; a
	 * replacement or the range's own safe end is used. Reported as
	 * ReplacedNonFinite for the non-finite case, ReplacedOutOfRange when a
	 * declared replacement is used for an in-bounds-domain violation, and
	 * ClampedToMin/ClampedToMax when no replacement is declared.
	 *
	 * Not thread-safe with respect to concurrent readers of Object. Called during
	 * object construction and from explicit reload paths, both game thread.
	 *
	 * @param Object  object to validate in place. Null is reported as a Failed issue, never a crash.
	 * @param Ranges  declared ranges, typically a class's own static table.
	 */
	RACINGSIM_API FRacingValidationResult EnforceRanges(UObject* Object, TConstArrayView<FRacingPropertyRange> Ranges);

	/**
	 * Log a result: one Warning per corrected value, one Error per Failed range.
	 *
	 * Separated from EnforceRanges so tests can assert on the returned result
	 * without provoking log output they then have to whitelist, and so a caller
	 * that wants to handle issues itself is not forced to also log them.
	 *
	 * @param Result   the pass to report.
	 * @param Context  object the pass ran on; used only to name the source in the log. May be null.
	 */
	RACINGSIM_API void LogResult(const FRacingValidationResult& Result, const UObject* Context);

	/**
	 * Prove the declared table and the UPROPERTY metadata say the same thing.
	 *
	 * Checks both directions, and the second one is the one that matters:
	 *
	 *   1. every declared range resolves to a real numeric property whose
	 *      ClampMin/ClampMax metadata (if present) equals the declared bound;
	 *   2. every config-backed numeric property on the class that carries
	 *      ClampMin or ClampMax metadata appears in the table.
	 *
	 * (2) is what catches the actual M-5 regression shape: someone adds a new
	 * clamped setting, the Details panel constrains it, and nothing constrains the
	 * ini. Without this check the table would silently rot.
	 *
	 * Metadata does not exist outside WITH_METADATA (== WITH_EDITORONLY_DATA), so
	 * in a packaged Game build this returns a clean result with nothing checked.
	 * That is fine and is the whole reason the table exists: this is a
	 * *consistency* check for the editor and for automation, never the runtime
	 * enforcement path.
	 *
	 * @param Class   class to inspect. Null is reported as a Failed issue.
	 * @param Ranges  the class's declared range table.
	 */
	RACINGSIM_API FRacingValidationResult VerifyRangesMatchMetadata(const UClass* Class, TConstArrayView<FRacingPropertyRange> Ranges);
}
