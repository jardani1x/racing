// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace RacingSim::Vehicle
{
	/**
	 * VEH-005: the height ARacingVehiclePawn::ExecuteSafeReset actually teleports to,
	 * given a one-shot ground trace run against the reset pose's seed transform.
	 *
	 * Factored out as a pure function, per VEH-005's acceptance criteria, so the
	 * height-selection logic is reachable from a SmokeFilter test with no actor and no
	 * UWorld -- the trace itself (UWorld::LineTraceSingleByChannel) is not testable
	 * that way, but deciding what to DO with its result is ordinary arithmetic and
	 * does not need to live behind a world query to be exercised.
	 *
	 * @param SeedZCm          the height ATrackDefinitionActor::GetResetPoseAtOrBefore
	 *                         DistanceCm already returned. This already carries the
	 *                         track's fixed PoseHeightOffsetCm lift (see
	 *                         ATrackDefinitionActor::MakePoseAtDistance), so it is the
	 *                         correct fallback when the trace finds nothing -- exactly
	 *                         the "fixed PoseHeightOffsetCm lift" fallback the ticket
	 *                         documents, not a second, independent guess.
	 * @param GroundClearanceCm the same fixed lift, reused as clearance ABOVE a traced
	 *                          ground hit. Passing the track's own PoseHeightOffsetCm
	 *                          keeps one authored number governing "how high above the
	 *                          road a reset pose sits" whether or not the trace found
	 *                          anything -- on FLAT, LEVEL road. Both the clearance here
	 *                          and the caller's vertical trace operate in world-Z, not
	 *                          along the seed pose's surface normal, so on banked or
	 *                          steeply graded track this clearance and the seed's own
	 *                          up-vector lift (ATrackDefinitionActor::MakePoseAtDistance)
	 *                          diverge by roughly cos(bank angle); code-reviewer
	 *                          MEDIUM-2, not fixed this pass.
	 * @param bTraceHit         whether the ground trace found a usable hit.
	 * @param TraceHitZCm       the hit location's Z, ignored when bTraceHit is false.
	 * @return the Z to teleport to. Falls back to SeedZCm whenever the trace missed or
	 *         either input is non-finite -- never NaN, never a divide, never a guess
	 *         beyond the two documented sources.
	 */
	RACINGSIM_API double ResolveGroundCorrectedResetZCm(
		double SeedZCm,
		double GroundClearanceCm,
		bool bTraceHit,
		double TraceHitZCm);

	/**
	 * RACE-002 M3, resolved as an active check rather than a comment: does
	 * ResetDistanceCm sit at-or-behind QueryDistanceCm, within MaxBackwardGapCm, on a
	 * closed loop of length TrackLengthCm?
	 *
	 * ATrackDefinitionActor::GetResetPoseAtOrBeforeDistanceCm already guarantees this
	 * by construction (RacingSim.Race.TrackDefinitionActor's own reset-sample tests
	 * exercise it exhaustively against real spline geometry) -- this function is NOT a
	 * re-implementation of that guarantee. It is the defence-in-depth half of the
	 * ticket's documented precondition: ExecuteSafeReset calls this on the actor's
	 * returned distance before trusting it, so a future caller's bug (passing a
	 * distance ahead of the car's real last-valid progress) is caught and logged
	 * rather than silently teleporting the car forward.
	 *
	 * A single closed loop is circular, so "at or before" alone is not decidable from
	 * two raw distances without a bound -- MaxBackwardGapCm is that bound, and should
	 * be generous relative to the track's authored ResetSampleSpacingCm (a car can
	 * legitimately be up to one sample's worth of arc behind the query). A gap outside
	 * [0, MaxBackwardGapCm] means the reset distance is either implausibly far behind
	 * or -- the case this exists to catch -- actually AHEAD of the query.
	 *
	 * @return false on any non-finite input, TrackLengthCm <= 0, or a gap outside the
	 *         bound. A false result here is never fatal in the caller: it is logged
	 *         and the reset still proceeds, because the pose itself came from the
	 *         actor's own guaranteed-safe accessor -- this check exists to surface a
	 *         caller-side bug, not to gate the reset on its own output.
	 */
	RACINGSIM_API bool IsResetDistanceAtOrBeforeQuery(
		double QueryDistanceCm,
		double ResetDistanceCm,
		double TrackLengthCm,
		double MaxBackwardGapCm);

	/**
	 * Added on code review (VEH-005 HIGH-1): does the pair
	 * ATrackDefinitionActor::GetResetPoseAtOrBeforeDistanceCm returned actually name a
	 * real reset sample, or the actor's own documented degenerate-track sentinel
	 * (OutIndex == INDEX_NONE, OutDistanceCm == ATrackDefinitionActor::InvalidDistanceCm,
	 * pose == FTransform::Identity)?
	 *
	 * Factored out as a pure predicate, not left as an inline comparison in
	 * ExecuteSafeReset, specifically so the guard against teleporting to world origin on
	 * an unbuilt/empty-centerline track is reachable from a SmokeFilter test with no
	 * actor, same reason every other decision in this file is pulled out this way.
	 *
	 * @param SampleIndex        OutIndex from GetResetPoseAtOrBeforeDistanceCm.
	 * @param SampleDistanceCm   OutDistanceCm from the same call.
	 * @param InvalidDistanceCm  the actor's own sentinel value to compare against --
	 *                           passed in rather than hard-coded, so this function never
	 *                           silently drifts from ATrackDefinitionActor::InvalidDistanceCm
	 *                           if that constant ever changes.
	 * @return false when either half of the pair reads as the degenerate-track sentinel.
	 */
	RACINGSIM_API bool IsResetSampleValid(
		int32 SampleIndex,
		double SampleDistanceCm,
		double InvalidDistanceCm);
}
