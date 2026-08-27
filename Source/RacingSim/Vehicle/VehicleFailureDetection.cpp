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

	/** True when the value is finite and its magnitude is within Limit. A non-finite value is NOT within any limit. */
	bool IsWithinVehicleFailureLimit(const double Value, const double Limit)
	{
		return FMath::IsFinite(Value) && FMath::Abs(Value) <= Limit;
	}
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
		bool bHasUsableStep = false;
		float StepSeconds = 0.0f;

		if (Previous.bIsValid)
		{
			const double RawStep = Current.TimestampSeconds - Previous.TimestampSeconds;

			if (!FMath::IsFinite(RawStep) || RawStep < 0.0)
			{
				RaiseVehicleFailure(Report, EVehicleFailureFlag::TimeAnomaly,
					FString::Printf(
						TEXT("non-monotonic or non-finite timestamp: previous %f, current %f"),
						Previous.TimestampSeconds, Current.TimestampSeconds));
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

				const double ContactDistanceCm = FVector::Dist(Current.LocationCm, Wheel.ContactPointCm);
				if (!FMath::IsFinite(ContactDistanceCm) || ContactDistanceCm > Thresholds.MaxContactDistanceCm)
				{
					RaiseVehicleFailure(Report, EVehicleFailureFlag::InvalidContact,
						FString::Printf(
							TEXT("wheel %d reports contact %f cm from the body, beyond the %f cm bound"),
							WheelIndex, ContactDistanceCm, Thresholds.MaxContactDistanceCm));
				}

				if (!IsWithinVehicleFailureLimit(Wheel.SpringForceN, TNumericLimits<float>::Max()))
				{
					RaiseVehicleFailure(Report, EVehicleFailureFlag::InvalidContact,
						FString::Printf(TEXT("wheel %d reports a non-finite spring force"), WheelIndex));
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
				TEXT("the input layer neutralised a stale device sample (no device event within the configured timeout)"));
		}

		return Report;
	}
}
