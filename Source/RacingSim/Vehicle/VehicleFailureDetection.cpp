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
		if (bAnyWheelReportsFreshContact)
		{
			State.bHasPreDiscontinuityLocation = false;
			State.PreDiscontinuityLocationCm = FVector::ZeroVector;
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
}
