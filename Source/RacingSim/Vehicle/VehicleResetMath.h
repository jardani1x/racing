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

	/**
	 * RACE-006 / VEH-007 routed requirement: the shortest reset cooldown, in the pawn's
	 * SIMULATED seconds, that lets a previous reset's contact-suppression basis expire
	 * before the next reset re-arms it under steady capture.
	 *
	 * Why this number. After a reset the detector's basis expires, at the latest, once
	 * both the per-arm evaluation floor has passed (Floor + 1 evaluations) and
	 * MaxContactSuppressionSeconds of simulated time has elapsed since the first
	 * evaluation after the arm. With a steady capture interval I = 1 / rate, the first
	 * evaluation lands within I of the reset, so expiry is within
	 * I + max(Floor * I, Budget + I) <= Budget + (Floor + 1) * I of it. The carried
	 * ceiling also drops the basis by evaluation Ceiling, within Ceiling * I, so the
	 * result is the smaller of the two: a budget longer than Ceiling * I is inert
	 * (VEH-007) and does not inflate the cooldown.
	 *
	 * ASSUMPTION: I is the real spacing between captures, which holds only while the
	 * frame time is at most 1 / rate. The pawn captures at most once per tick, so
	 * frames slower than the capture rate (20 fps under a 60 Hz capture) stretch the
	 * spacing to the frame time and the basis can outlive this bound. A single long
	 * frame right after a reset does the same by starting the budget late.
	 *
	 * So this is ADVISORY, not the guarantee. The invariant -- "never re-arm while
	 * armed", which keeps a reset storm off the carried ceiling -- is
	 * EvaluateResetGate's SuppressionArmed check, which holds at any frame time. This
	 * cooldown is the authored, testable floor that keeps a well-behaved reset storm
	 * from hitting that gate.
	 *
	 * @param MaxContactSuppressionSeconds  FVehicleFailureThresholds' budget, SECONDS.
	 * @param TelemetrySampleRateHz         the pawn's capture rate, HERTZ. Zero, negative
	 *                                      or non-finite means capture is disabled, the
	 *                                      detector never arms, and the budget alone is
	 *                                      returned.
	 * @return never negative, never NaN. A non-finite or negative budget reads as 0.
	 */
	RACINGSIM_API double ComputeMinimumResetCooldownSeconds(
		float MaxContactSuppressionSeconds,
		float TelemetrySampleRateHz);

	/**
	 * The cooldown the pawn actually enforces: max(Authored, minimum). An authored value
	 * below the minimum, or a non-finite one, is raised to the minimum -- the minimum is
	 * the one safe value, and refusing every reset would strand the driver.
	 */
	RACINGSIM_API double ResolveEffectiveResetCooldownSeconds(
		float AuthoredCooldownSeconds,
		float MaxContactSuppressionSeconds,
		float TelemetrySampleRateHz);

	/** RACE-006: why the vehicle-side reset gate admitted or refused a request. */
	enum class EVehicleResetGateResult : uint8
	{
		Accepted,
		/** Less than the effective cooldown of simulated time since the last executed reset. */
		CoolingDown,
		/** The previous reset's contact-suppression basis is still armed (capture enabled only). */
		SuppressionArmed,
		/** The pawn's simulated clock, or the cooldown, is not finite. */
		ClockUnusable,
	};

	/** RACE-006: everything EvaluateResetGate needs, so it stays a pure function. */
	struct FVehicleResetGateInput
	{
		/** False until the pawn has executed its first reset; the cooldown does not apply before it. */
		bool bHasPreviousReset = false;

		/** The pawn's simulated clock now, SECONDS. */
		double SimulationTimeSeconds = 0.0;

		/** The pawn's simulated clock at the last executed reset, SECONDS. */
		double LastResetSimulationTimeSeconds = 0.0;

		/** The effective cooldown (ResolveEffectiveResetCooldownSeconds), SECONDS. */
		double CooldownSeconds = 0.0;

		/** Telemetry capture is running, so the failure detector is live. */
		bool bCaptureEnabled = true;

		/** FVehicleFailureDetectorState::bHasPreDiscontinuityLocation. */
		bool bContactSuppressionArmed = false;
	};

	/**
	 * RACE-006: the vehicle-side reset gate. Checks, in order: ClockUnusable,
	 * CoolingDown, SuppressionArmed.
	 *
	 * SuppressionArmed applies only while capture is enabled: with capture disabled
	 * the detector is not evaluated, so its state is stale and cannot mean anything.
	 */
	RACINGSIM_API EVehicleResetGateResult EvaluateResetGate(const FVehicleResetGateInput& Input);

	/** Stable name for a log line or a test message. */
	RACINGSIM_API const TCHAR* LexResetGateResult(EVehicleResetGateResult Result);
}
