// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/VehicleFailureDetection.h"

#include "Vehicle/VehicleInputTypes.h"

namespace
{
	/**
	 * Raise one flag and append its reasoning.
	 *
	 * NAMED FOR THIS TICKET ON PURPOSE. Anonymous namespaces give internal linkage, so
	 * a non-unity build would link two identically-signed helpers cleanly and hide the
	 * clash -- but a Unity Build concatenates translation units and treats them as a
	 * redefinition (C2084). This project has hit that exact bug three times already:
	 * AddFailure -> AddChassisValidationFailure (VEH-002), AllFinite ->
	 * AllTuneValuesFinite (VEH-003), HasIssueFor -> HasTuneIssueFor (VEH-003). Every
	 * file-anonymous helper added here therefore carries a VEH-004-specific name.
	 */
	void RaiseVehicleFailure(
		FVehicleFailureReport& Report,
		const EVehicleFailureFlag Flag,
		FString Detail)
	{
		Report.Flags |= static_cast<uint8>(Flag);

		if (!Report.Reason.IsEmpty())
		{
			Report.Reason.Append(TEXT("; "));
		}
		Report.Reason.Append(MoveTemp(Detail));
	}

	/**
	 * Fewest evaluations the post-discontinuity contact basis is allowed to survive,
	 * before MaxContactSuppressionSeconds is permitted to expire it.
	 *
	 * STRUCTURAL, not tunable, which is why it lives here and not in
	 * FVehicleFailureThresholds. It is not a policy about how long suppression should
	 * last -- that is the threshold's job -- it is the arithmetic fact that the stale
	 * tail being suppressed is two captures long (see
	 * FVehicleFailureDetectorState::PreDiscontinuityLocationCm), so any bound that can
	 * fire before the third evaluation cannot do the job at all.
	 *
	 * Compared against FVehicleFailureDetectorState::PreDiscontinuityArmEvaluations, the
	 * PER-ARM count, which every NotifyDiscontinuity() restarts at zero. The derivation
	 * below counts from that zero; it did not hold while the floor read the carried
	 * ceiling count, which a re-announcement leaves wherever it was (VEH-007, spec S-M1).
	 *
	 * Three is the tail exactly, and NOT the tail plus a margin -- an earlier version of
	 * this comment claimed a margin it does not have. Evaluation 1 is always the straddling
	 * evaluation, already suppressed by bDiscontinuityPending, which is consumed on the
	 * first valid evaluation after the announcement; invalid snapshots return before both
	 * that consumption and this section, so the flag and this counter cannot drift apart.
	 * The basis alone therefore covers evaluations 2 and 3, which is the two-capture tail
	 * and nothing spare. If the tail is ever measured at three captures, this constant has
	 * to rise with it; it will not absorb the change on its own.
	 */
	constexpr int32 GVehicleFailureMinContactSuppressionEvaluations = 3;

	/**
	 * Most evaluations the basis may survive, whatever the clock says.
	 *
	 * Covers the one case the time budget cannot: a simulated clock that stops
	 * advancing while evaluations keep arriving. SuppressedForSeconds then stays at
	 * zero for ever and the time bound never fires, which is precisely the unbounded
	 * suppression this whole section exists to prevent.
	 *
	 * Measured in evaluations, so its duration depends on the capture rate. The detector
	 * runs once per capture, at ARacingVehiclePawn::TelemetrySampleRateHz (default 60 Hz,
	 * range [0, 1000], in practice capped by the tick rate), so the ceiling lasts
	 * 240 / rate seconds: 4 s at the default, eight times the 0.5 s time budget. Above
	 * 480 Hz it lasts less than that budget and fires first even on a healthy clock;
	 * that is still safe, because 240 is far above the floor of 3, but at such rates it
	 * stops being purely the stopped-clock backstop.
	 *
	 * Compared against FVehicleFailureDetectorState::PreDiscontinuityEvaluations, the
	 * CARRIED count, so re-announcing a discontinuity cannot rewind it.
	 */
	constexpr int32 GVehicleFailureMaxContactSuppressionEvaluations = 240;

	// IsWithinVehicleFailureLimit was removed here on code review (VEH-004 MEDIUM-3):
	// its only two call sites were both dead code (a non-finite Wheel.SpringForceN or
	// Wheel.ContactPointCm can never reach them -- Wheel.IsFinite() already `continue`s
	// past the whole wheel before either check runs), and an unused static helper is
	// its own maintenance hazard. Other magnitude bounds in this file compare directly.
}

namespace RacingSim::Vehicle
{
	FString DescribeVehicleFailureFlags(const uint8 Flags)
	{
		if (Flags == 0)
		{
			return TEXT("None");
		}

		// Declaration order, so a log line reads the same way every time.
		static const TPair<EVehicleFailureFlag, const TCHAR*> Names[] =
		{
			{ EVehicleFailureFlag::NonFiniteState,			TEXT("NonFiniteState") },
			{ EVehicleFailureFlag::RunawayEnergy,			TEXT("RunawayEnergy") },
			{ EVehicleFailureFlag::Tunnelling,				TEXT("Tunnelling") },
			{ EVehicleFailureFlag::UnstableWheelState,		TEXT("UnstableWheelState") },
			{ EVehicleFailureFlag::InvalidContact,			TEXT("InvalidContact") },
			{ EVehicleFailureFlag::PersistentPenetration,	TEXT("PersistentPenetration") },
			{ EVehicleFailureFlag::TimeAnomaly,				TEXT("TimeAnomaly") },
			{ EVehicleFailureFlag::StaleInput,				TEXT("StaleInput") }
		};

		FString Result;
		for (const TPair<EVehicleFailureFlag, const TCHAR*>& Entry : Names)
		{
			if ((Flags & static_cast<uint8>(Entry.Key)) != 0)
			{
				if (!Result.IsEmpty())
				{
					Result.Append(TEXT(","));
				}
				Result.Append(Entry.Value);
			}
		}

		return Result;
	}

	FVehicleFailureReport EvaluateVehicleFailures(
		const FVehicleTelemetrySnapshot& Previous,
		const FVehicleTelemetrySnapshot& Current,
		const FVehicleFailureThresholds& Thresholds,
		FVehicleFailureDetectorState& State)
	{
		FVehicleFailureReport Report;
		Report.TimestampSeconds = Current.TimestampSeconds;
		Report.CaptureIndex = Current.CaptureIndex;

		if (!Current.bIsValid)
		{
			// "No sample was taken" is not a failure. Returning a fault here would make
			// every frame before the movement component exists look like a crash.
			return Report;
		}

		// CONSUMED HERE, unconditionally, so exactly ONE evaluation is suppressed however
		// this function returns below. Read into a local first: clearing it later, or only
		// on some paths, would leave a reset suppressing faults for as long as the car
		// happened to stay clean, which is the opposite of what a detector is for.
		const bool bStraddlesDiscontinuity = State.bDiscontinuityPending;
		State.bDiscontinuityPending = false;

		// The one way the contact-suppression basis ends. Shared by the three exits that
		// drop it -- the ceiling and the time budget in section 0, fresh contact after the
		// wheel loop -- so they cannot drift apart on which fields they clear.
		auto DropContactSuppressionBasis = [&State]()
		{
			State.bHasPreDiscontinuityLocation = false;
			State.PreDiscontinuityLocationCm = FVector::ZeroVector;
			State.bHasPreDiscontinuityArmTime = false;
			State.PreDiscontinuityArmSimSeconds = 0.0;
			State.PreDiscontinuityEvaluations = 0;
			State.PreDiscontinuityArmEvaluations = 0;
		};

		// -- 0. Bound the contact-suppression basis ------------------------------
		//
		// Runs BEFORE the wheel loop reads the basis, so the budget expires on the
		// evaluation it runs out on rather than one evaluation later.
		//
		// The fresh-contact rule further down is the normal way this basis ends, and it
		// is the right rule whenever contact comes back. It has no answer when contact
		// never does -- a reset that leaves the car airborne, inverted, wedged or under
		// the world -- nor when the reset moved the car less than MaxContactDistanceCm,
		// where every contact keeps matching the stale basis and so never counts as
		// fresh. Without a second bound the basis stayed armed indefinitely and went on
		// suppressing genuine InvalidContact near that one pose, in precisely the
		// situations the reset path exists to recover from.
		//
		// TWO bounds, because one is not enough and they fail in opposite directions.
		// MaxContactSuppressionSeconds is measured in simulated time; the stale tail it
		// covers is measured in captures. A single long frame -- a teleport into a cell
		// that then streams in -- can exceed the whole budget on its own, expiring the
		// basis on the very evaluation that still needs it and raising the false
		// InvalidContact this suppression was written to prevent. So the time budget may
		// only expire the basis once GVehicleFailureMinContactSuppressionEvaluations
		// evaluations have actually seen it. The evaluation CEILING then covers what the
		// floor opens up plus what the clock cannot bound at all: a simulated clock that
		// stops advancing leaves SuppressedForSeconds pinned at zero, and only a count
		// can end that.
		if (State.bHasPreDiscontinuityLocation)
		{
			// Plain increments. The ceiling counter used to be clamped against MAX_int32,
			// which read as an overflow guard and was not one: the ceiling below drops the
			// basis at 240, about seven orders of magnitude short of MAX_int32, so the clamp
			// was unreachable, and had it ever been reachable the signed addition would
			// already have been undefined behaviour before FMath::Min saw the result. The
			// real bound on both counters is the ceiling, which zeroes them together; the
			// floor counter never exceeds the ceiling counter because every path that
			// zeroes the ceiling counter zeroes it too, and NotifyDiscontinuity() zeroes it
			// on its own.
			++State.PreDiscontinuityEvaluations;
			++State.PreDiscontinuityArmEvaluations;

			// The floor reads the PER-ARM count and the ceiling reads the CARRIED count
			// (VEH-007, spec S-M1). A re-announcement starts a new stale tail, so the floor
			// must restart with it; reading the carried count here let a re-announced basis
			// start already past the floor, and a long frame inside the new tail then
			// expired it and raised a false InvalidContact on a stationary car.
			const bool bPastEvaluationFloor =
				State.PreDiscontinuityArmEvaluations > GVehicleFailureMinContactSuppressionEvaluations;
			const bool bPastEvaluationCeiling =
				State.PreDiscontinuityEvaluations >= GVehicleFailureMaxContactSuppressionEvaluations;

			if (bPastEvaluationCeiling)
			{
				// The ceiling ignores the floor deliberately: it is the bound of last resort,
				// and it is set far enough above the tail that reaching it means something
				// other than a single normal reset is happening -- a stopped clock, or a
				// storm of re-announcements each arriving before the time budget could
				// expire the basis it re-armed.
				//
				// ACCEPTED COST of ignoring the floor (VEH-007, the near-ceiling half of spec
				// S-M1): a genuine teleport announced when the carried count is already
				// within three evaluations of the ceiling (237..239) has its basis dropped
				// inside its own stale tail and raises a false InvalidContact on one or both
				// tail captures. Gating the ceiling on the floor would close that and reopen
				// CASE 8 -- a caller re-announcing every third evaluation would then never be
				// bounded. With a healthy clock the count only gets that high if
				// announcements keep arriving before the previous basis expired, so a reset
				// caller whose cooldown exceeds MaxContactSuppressionSeconds plus the floor's
				// evaluations at the active capture rate cannot reach it (RACE-006
				// requirement).
				//
				// It is also tested BEFORE the non-finite branch below, which costs that
				// branch its guarantee on exactly one evaluation. If the clock is non-finite
				// at the moment the count reaches the ceiling, the basis drops here, and a
				// stale-but-finite contact can then raise InvalidContact alongside the
				// NonFiniteState report -- the stacking the branch below says must not
				// happen. Accepted, and stated rather than papered over: the alternative is
				// letting a non-finite clock outrank the only bound that still works when
				// the clock is unusable, and one evaluation of a doubled report is a much
				// smaller problem than suppression with no bound at all.
				DropContactSuppressionBasis();
			}
			else if (!FMath::IsFinite(Current.SimulationTimeSeconds))
			{
				// A non-finite clock cannot bound anything, but it is ALSO about to raise
				// NonFiniteState on this same evaluation, so the report already says what is
				// wrong. Dropping the basis here as well would stack a second, misleading
				// InvalidContact on top of it from contact geometry that is merely stale.
				// Hold the basis and let the evaluation ceiling above be its bound; that is
				// the bound written for exactly the case where the clock is unusable. The
				// one evaluation on which this does not hold is the ceiling evaluation
				// itself -- see the note there.
			}
			else if (!State.bHasPreDiscontinuityArmTime)
			{
				State.PreDiscontinuityArmSimSeconds = Current.SimulationTimeSeconds;
				State.bHasPreDiscontinuityArmTime = true;
			}
			else
			{
				const double SuppressedForSeconds =
					Current.SimulationTimeSeconds - State.PreDiscontinuityArmSimSeconds;

				if (SuppressedForSeconds < 0.0)
				{
					// RE-STAMPED, not expired. The simulated clock only runs backwards when it
					// has been re-based under the detector; the stamp from the old timeline can
					// no longer bound anything, but the basis itself is still as young as its
					// evaluation count says it is, and expiring it on a clock event would be the
					// same early-expiry bug the floor exists to prevent. Re-basing repeatedly
					// cannot keep the basis alive for ever, because the ceiling still counts.
					State.PreDiscontinuityArmSimSeconds = Current.SimulationTimeSeconds;
				}
				else if (bPastEvaluationFloor
					&& SuppressedForSeconds > static_cast<double>(Thresholds.MaxContactSuppressionSeconds))
				{
					DropContactSuppressionBasis();
				}
			}
		}

		// -- 1. Non-finite state ------------------------------------------------
		//
		// Checked FIRST and used to gate everything numeric below. Once a NaN is in the
		// state, every derived rate is also NaN, and NaN compares false against every
		// bound -- so a naive "speed > limit" test would silently pass on the most
		// corrupt state the solver can produce, and the report would say the car was
		// fine. Ordering is the guard.
		const bool bFinite = Current.IsFinite();
		if (!bFinite)
		{
			RaiseVehicleFailure(Report, EVehicleFailureFlag::NonFiniteState,
				FString::Printf(
					TEXT("non-finite chassis or wheel state at capture %lld (location %s, velocity %s, forward speed %f)"),
					Current.CaptureIndex,
					*Current.LocationCm.ToString(),
					*Current.VelocityCms.ToString(),
					Current.ForwardSpeedCms));
		}

		// -- 2. Time --------------------------------------------------------------
		//
		// The clock is judged before anything that divides by it. A backwards or
		// non-finite step makes every rate below meaningless, so it is reported and the
		// rate-based checks are skipped rather than run on garbage.
		//
		// SIMULATED time, not wall-clock time. Every quantity below -- position,
		// velocity, wheel angular velocity -- was produced by stepping the solver with
		// DeltaSeconds, so the only honest denominator is the sum of those deltas.
		// VEH-004 shipped this line reading TimestampSeconds, which is wall-clock: under
		// a fixed-step test loop, a hitch, a breakpoint or any time dilation the two
		// clocks diverge and the detector divides real motion by an unrelated step,
		// manufacturing impossible accelerations for a car behaving perfectly. See
		// FVehicleTelemetrySnapshot::SimulationTimeSeconds.
		bool bHasUsableStep = false;
		float StepSeconds = 0.0f;

		if (Previous.bIsValid && !bStraddlesDiscontinuity)
		{
			const double RawStep = Current.SimulationTimeSeconds - Previous.SimulationTimeSeconds;

			if (!FMath::IsFinite(RawStep) || RawStep < 0.0)
			{
				RaiseVehicleFailure(Report, EVehicleFailureFlag::TimeAnomaly,
					FString::Printf(
						TEXT("non-monotonic or non-finite simulation time: previous %f, current %f"),
						Previous.SimulationTimeSeconds, Current.SimulationTimeSeconds));
			}
			else if (RawStep > 0.0)
			{
				StepSeconds = static_cast<float>(RawStep);
				Report.MeasuredStepSeconds = StepSeconds;

				// A step longer than the analysis window suppresses the extrapolating
				// checks. A hitch is not a physics fault, and pretending a velocity
				// held for half a second would manufacture one. The accumulators still
				// advance -- time really did pass.
				bHasUsableStep = StepSeconds <= Thresholds.MaxAnalysisStepSeconds;
			}
			// RawStep == 0 with a valid previous snapshot: two captures at one instant.
			// Legal (a decimated capture re-reading the same frame) and deliberately not
			// a TimeAnomaly, but it leaves no step to divide by, so nothing rate-based runs.
		}

		// -- 3. Runaway energy ----------------------------------------------------
		//
		// Absolute envelopes first, then the rate. Absolute bounds are the ones that
		// catch a divergence that has already happened; the acceleration bound catches
		// the step it happens on.
		if (bFinite)
		{
			const double SpeedCms = Current.GetSpeedMagnitudeCms();
			if (SpeedCms > Thresholds.MaxSpeedCms)
			{
				RaiseVehicleFailure(Report, EVehicleFailureFlag::RunawayEnergy,
					FString::Printf(TEXT("speed %f cm/s exceeds the %f cm/s envelope"),
						SpeedCms, Thresholds.MaxSpeedCms));
			}

			const double AngularSpeed = Current.AngularVelocityDegreesPerSecond.Size();
			if (AngularSpeed > Thresholds.MaxAngularSpeedDegreesPerSecond)
			{
				RaiseVehicleFailure(Report, EVehicleFailureFlag::RunawayEnergy,
					FString::Printf(TEXT("angular speed %f deg/s exceeds the %f deg/s envelope"),
						AngularSpeed, Thresholds.MaxAngularSpeedDegreesPerSecond));
			}

			if (FMath::Abs(Current.EngineRpm) > Thresholds.MaxEngineRpm)
			{
				RaiseVehicleFailure(Report, EVehicleFailureFlag::RunawayEnergy,
					FString::Printf(TEXT("engine speed %f rpm exceeds the %f rpm envelope"),
						Current.EngineRpm, Thresholds.MaxEngineRpm));
			}

			if (bHasUsableStep && Previous.IsFinite())
			{
				const double PreviousSpeed = Previous.GetSpeedMagnitudeCms();
				const double Acceleration = FMath::Abs(SpeedCms - PreviousSpeed) / StepSeconds;

				if (Acceleration > Thresholds.MaxAccelerationCmsPerSecondSquared)
				{
					RaiseVehicleFailure(Report, EVehicleFailureFlag::RunawayEnergy,
						FString::Printf(
							TEXT("speed changed by %f cm/s over %f s (%f cm/s^2), exceeding the %f cm/s^2 envelope"),
							SpeedCms - PreviousSpeed, StepSeconds, Acceleration,
							Thresholds.MaxAccelerationCmsPerSecondSquared));
				}
			}
		}

		// -- 4. Tunnelling --------------------------------------------------------
		//
		// The body moved further than its own recorded velocity can explain. That is
		// what passing through geometry looks like from outside the solver, and it is
		// also what a teleport looks like -- which is correct: an unannounced teleport
		// during a race is a fault, and VEH-005's DELIBERATE reset is expected to call
		// FVehicleFailureDetectorState::Reset() so it does not read as one.
		//
		// The bound scales with the measured step, which is what makes this
		// frame-rate independent: at 30 Hz a legitimate step is twice as long as at
		// 60 Hz and the allowance doubles with it.
		if (bFinite && bHasUsableStep && Previous.IsFinite())
		{
			const double MovedCm = FVector::Dist(Previous.LocationCm, Current.LocationCm);

			// Both endpoints' velocities, because either one alone under-predicts a step
			// that accelerated or decelerated hard within it.
			const double FastestCms = FMath::Max(Previous.GetSpeedMagnitudeCms(), Current.GetSpeedMagnitudeCms());
			const double ExplainedCm =
				FastestCms * StepSeconds * Thresholds.TunnelVelocityFactor + Thresholds.TunnelToleranceCm;

			if (MovedCm > ExplainedCm)
			{
				RaiseVehicleFailure(Report, EVehicleFailureFlag::Tunnelling,
					FString::Printf(
						TEXT("moved %f cm in %f s, but %f cm/s explains at most %f cm"),
						MovedCm, StepSeconds, FastestCms, ExplainedCm));
			}
		}

		// -- 5. Wheel state and contacts -----------------------------------------
		//
		// Per wheel, and each condition is separate: an out-of-range suspension length
		// is the solver diverging, an impossible contact point is the collision query
		// returning nonsense, and they have different causes.
		int32 WheelsInContact = 0;

		// Whether any wheel in contact is reporting a point that does NOT belong to the
		// pose the car left at the last announced discontinuity -- that is, proof the
		// physics output has caught up. See
		// FVehicleFailureDetectorState::PreDiscontinuityLocationCm.
		//
		// Phrased around FRESH contact, not around stale contact, and the difference is
		// the whole bug this replaced. The evaluation immediately after a teleport
		// reports every wheel OUT of contact with a zeroed contact point:
		//
		//     cap=241 loc=(-2500,4330,70) [w0 c=0 pt=(0,0,0)] ... [w3 c=0 pt=(0,0,0)]
		//     cap=242 loc=(-2500,4330,72) [w0 c=1 pt=(-297,1083,0)] ... stale, pre-teleport
		//
		// so a rule that expired the basis whenever nothing MATCHED it threw the basis
		// away at capture 241 -- on a snapshot carrying no contact evidence at all -- and
		// then had nothing left to suppress the genuinely stale capture 242 with. Absence
		// of contact is not evidence of catching up. Only a contact somewhere else is.
		bool bAnyWheelReportsFreshContact = false;

		for (int32 WheelIndex = 0; WheelIndex < Current.NumWheels && WheelIndex < MaxVehicleTelemetryWheels; ++WheelIndex)
		{
			const FVehicleWheelTelemetry& Wheel = Current.Wheels[WheelIndex];

			if (!Wheel.IsFinite())
			{
				// Already covered by the whole-snapshot check above, but named per wheel
				// so the report says WHICH wheel -- which is the whole diagnostic value.
				RaiseVehicleFailure(Report, EVehicleFailureFlag::UnstableWheelState,
					FString::Printf(TEXT("wheel %d has non-finite state"), WheelIndex));
				continue;
			}

			const float Length = Wheel.NormalisedSuspensionLength;
			if (Length < -Thresholds.SuspensionLengthTolerance
				|| Length > 1.0f + Thresholds.SuspensionLengthTolerance)
			{
				RaiseVehicleFailure(Report, EVehicleFailureFlag::UnstableWheelState,
					FString::Printf(
						TEXT("wheel %d normalised suspension length %f is outside [0,1] by more than %f"),
						WheelIndex, Length, Thresholds.SuspensionLengthTolerance));
			}

			if (Wheel.bInContact)
			{
				++WheelsInContact;

				// Distance-bound only -- corrected on code review (VEH-004 MEDIUM-3),
				// which found the non-finite sub-checks this block used to carry were
				// dead code. Wheel.IsFinite() (checked above, with its own `continue`)
				// already covers ContactPointCm and SpringForceN as part of the whole
				// wheel struct, so a non-finite contact point or spring force can never
				// reach this line -- it was reported as UnstableWheelState and skipped
				// before this block runs. InvalidContact is therefore genuinely the
				// FINITE-but-impossible case: a contact point that is a real number,
				// just an absurd one (the collision query returning nonsense), which is
				// a different cause from the solver producing NaN.
				// Skipped across a discontinuity: see FVehicleFailureDetectorState::
				// bDiscontinuityPending. The pose is post-teleport and the contact point is
				// pre-teleport, so the distance between them measures the teleport, not a
				// bad collision query.
				// Second, wider suppression, and it is not redundant with the first.
				// bStraddlesDiscontinuity covers exactly the evaluation that spans the
				// teleport; this covers the TAIL, because the wheel half of the snapshot
				// keeps arriving from before the teleport for at least one more capture
				// after that. A wheel whose contact point is still within the bound of
				// the pose the car LEFT is describing the old world correctly, not
				// describing the new one wrongly.
				const bool bContactPredatesDiscontinuity =
					State.bHasPreDiscontinuityLocation
					&& FVector::Dist(State.PreDiscontinuityLocationCm, Wheel.ContactPointCm)
						<= Thresholds.MaxContactDistanceCm;

				bAnyWheelReportsFreshContact |= !bContactPredatesDiscontinuity;

				const double ContactDistanceCm = FVector::Dist(Current.LocationCm, Wheel.ContactPointCm);
				if (!bStraddlesDiscontinuity
					&& !bContactPredatesDiscontinuity
					&& ContactDistanceCm > Thresholds.MaxContactDistanceCm)
				{
					RaiseVehicleFailure(Report, EVehicleFailureFlag::InvalidContact,
						FString::Printf(
							TEXT("wheel %d reports contact %f cm from the body, beyond the %f cm bound"),
							WheelIndex, ContactDistanceCm, Thresholds.MaxContactDistanceCm));
				}
			}

			// -- Persistent penetration. The accumulating one.
			//
			// Fully compressed AND loaded. The load test is what separates "the body is
			// inside the world and the solver is pushing back hard" from "this wheel is
			// hanging at its bump stop with nothing under it", which is an ordinary
			// airborne reading.
			const bool bPinned =
				Wheel.bInContact
				&& Length <= Thresholds.PenetrationSuspensionLengthFraction
				&& Wheel.SpringForceN > Thresholds.PenetrationSpringForceN;

			if (bPinned && StepSeconds > 0.0f)
			{
				State.PenetrationSeconds[WheelIndex] += StepSeconds;

				if (State.PenetrationSeconds[WheelIndex] >= Thresholds.PenetrationPersistSeconds)
				{
					RaiseVehicleFailure(Report, EVehicleFailureFlag::PersistentPenetration,
						FString::Printf(
							TEXT("wheel %d has been fully compressed under load for %f s (threshold %f s)"),
							WheelIndex, State.PenetrationSeconds[WheelIndex], Thresholds.PenetrationPersistSeconds));
				}
			}
			else if (!bPinned)
			{
				// Cleared the instant the wheel comes off the stop. Penetration is
				// CONTINUOUS by definition; a car bouncing over kerbs must not accumulate
				// its way into a fault one strike at a time.
				State.PenetrationSeconds[WheelIndex] = 0.0f;
			}
		}

		// The basis expires the first evaluation that produces contact evidence from
		// somewhere other than the pose the car left -- the frame the physics output
		// caught up with the teleport. Holding it any longer would eventually suppress a
		// GENUINE bad contact that happened to land near a pose the car was reset from,
		// which is a real reading this detector must still raise.
		//
		// This is the FIRST of two exits, and the one that fires in the ordinary case.
		// The simulated-time budget in section 0 is the backstop for the cases where
		// fresh contact evidence never arrives at all.
		if (bAnyWheelReportsFreshContact)
		{
			DropContactSuppressionBasis();
		}

		// Wheels beyond NumWheels never accumulate, but a vehicle that loses wheels
		// between samples would otherwise keep stale history for the missing ones.
		for (int32 WheelIndex = Current.NumWheels; WheelIndex < MaxVehicleTelemetryWheels; ++WheelIndex)
		{
			State.PenetrationSeconds[WheelIndex] = 0.0f;
		}

		// -- 6. Airborne ----------------------------------------------------------
		//
		// Only meaningful for a vehicle that HAS wheels: a snapshot with NumWheels == 0
		// is an unconfigured vehicle, not a flying one, and must not accumulate.
		if (Current.NumWheels > 0 && WheelsInContact == 0)
		{
			if (StepSeconds > 0.0f)
			{
				State.AirborneSeconds += StepSeconds;
			}

			if (State.AirborneSeconds >= Thresholds.MaxAirborneSeconds)
			{
				RaiseVehicleFailure(Report, EVehicleFailureFlag::UnstableWheelState,
					FString::Printf(
						TEXT("no wheel has been in contact for %f s (threshold %f s)"),
						State.AirborneSeconds, Thresholds.MaxAirborneSeconds));
			}
		}
		else
		{
			State.AirborneSeconds = 0.0f;
		}

		// -- 7. Stale input -------------------------------------------------------
		//
		// Not a physics fault, and deliberately reported through the same channel:
		// "the car stopped responding" has two very different causes -- the solver
		// broke, or the browser stopped sending -- and a single report that can say
		// which is the difference between a five-minute and a five-hour investigation.
		if ((Current.InputCorrections & static_cast<uint8>(EVehicleInputCorrection::StaleSample)) != 0)
		{
			RaiseVehicleFailure(Report, EVehicleFailureFlag::StaleInput,
				TEXT("the input layer neutralised a stale device sample (a demand or held control survived silence past the configured timeout)"));
		}

		return Report;
	}

	int32 GetMinContactSuppressionEvaluations()
	{
		return GVehicleFailureMinContactSuppressionEvaluations;
	}

	int32 GetMaxContactSuppressionEvaluations()
	{
		return GVehicleFailureMaxContactSuppressionEvaluations;
	}
}
