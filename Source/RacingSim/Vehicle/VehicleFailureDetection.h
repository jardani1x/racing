// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vehicle/VehicleTelemetryTypes.h"
#include "VehicleFailureDetection.generated.h"

/**
 * VEH-004: simulation-failure detection, as a pure function over plain data.
 *
 * ---------------------------------------------------------------------------
 * What this is for, stated exactly
 * ---------------------------------------------------------------------------
 *
 * Docs/Tickets.md, Epic 2 preamble: "VEH-004 must detect NaN, infinity, explosive
 * energy, persistent penetration and unbounded wheel state -- Gate C treats these as
 * test failures, not warnings." Docs/02-VehiclePhysics.md, Simulation timing: "Detect
 * and fail tests on NaN, infinity, tunneling, unstable wheel state, or runaway energy."
 *
 * This detects SIMULATION CORRUPTION, not bad driving. Every threshold is a
 * deliberately generous outer envelope: a car that spins, understeers, hits a wall or
 * lands badly must produce a clean report, because a detector that cries wolf during
 * normal racing is a detector that gets switched off. What it must catch is state the
 * solver can never legitimately produce.
 *
 * ---------------------------------------------------------------------------
 * Why it is a free function on plain data
 * ---------------------------------------------------------------------------
 *
 * Same reason FVehicleInputProcessor is a plain struct and MapCommandToChaosInput is a
 * free function: Docs/Environment.md records, with an engine-source cause, that a
 * SmokeFilter test in this project cannot construct a non-template Actor or
 * UActorComponent. If this logic lived on the pawn, none of it could be tested at the
 * fast gate -- and the pawn is precisely what this project's harness cannot build.
 *
 * So the detector takes two snapshots, a thresholds POD and a state struct, and returns
 * a report. Every branch below is reachable from a Smoke test with synthetic snapshots.
 *
 * ---------------------------------------------------------------------------
 * It reports. It never acts.
 * ---------------------------------------------------------------------------
 *
 * VEH-005 owns the reset POSE. This module must not reposition, respawn, freeze or
 * recover anything, and it deliberately has no access to anything it could. A caller
 * that wants to act on a report is welcome to; that decision is not made here.
 *
 * ---------------------------------------------------------------------------
 * Frame-rate independence
 * ---------------------------------------------------------------------------
 *
 * The two accumulating detectors (airborne time, persistent penetration) integrate
 * WALL-CLOCK SECONDS taken from the difference between consecutive snapshot
 * timestamps -- never a per-tick counter, and never the snapshot's own
 * FrameDeltaSeconds, which is a frame's self-report and can lie. The tunnelling bound
 * scales with that same measured step. CLAUDE.md: "Keep gameplay independent from
 * frame rate", and a failure detector that fires at 144 Hz and not at 30 Hz is worse
 * than none, because it would make every performance change look like a physics
 * regression.
 */

/**
 * What went wrong. A BITMASK, because these co-occur constantly -- a NaN in the
 * velocity produces a non-finite state AND, one step later, an impossible position
 * delta -- and collapsing them to a single enum would report whichever the author
 * happened to check first rather than what actually happened.
 */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EVehicleFailureFlag : uint8
{
	None					= 0			UMETA(Hidden),

	/** NaN or +/-infinity anywhere in the chassis or wheel state. The unrecoverable one: by the time this is observable the solver is already corrupt. */
	NonFiniteState			= 1 << 0	UMETA(DisplayName = "Non-finite state"),

	/** Speed, angular speed, engine speed or acceleration outside any envelope a car can reach. Chaos' classic explosion signature. */
	RunawayEnergy			= 1 << 1	UMETA(DisplayName = "Runaway energy"),

	/** The body moved further in one step than its own recorded velocity can explain -- it passed through something, or was teleported. */
	Tunnelling				= 1 << 2	UMETA(DisplayName = "Tunnelling"),

	/** Normalised suspension length outside [0,1], or all wheels off the ground for longer than the threshold. */
	UnstableWheelState		= 1 << 3	UMETA(DisplayName = "Unstable wheel state"),

	/**
	 * A wheel in contact reports its contact point implausibly far from the chassis.
	 * Corrected on code review (VEH-004 MEDIUM-3): a non-finite contact point or spring
	 * force is caught by UnstableWheelState instead, before this check ever runs --
	 * see EvaluateVehicleFailures' per-wheel IsFinite() guard. This flag is genuinely
	 * the FINITE-but-impossible case (a real number, just an absurd one).
	 */
	InvalidContact			= 1 << 4	UMETA(DisplayName = "Invalid contact"),

	/** Suspension pinned at full compression under load for longer than the threshold: the body is inside the world, not resting on it. */
	PersistentPenetration	= 1 << 5	UMETA(DisplayName = "Persistent penetration"),

	/** Timestamps went backwards, stood still across a moving sample, or were non-finite. A broken clock invalidates every rate above. */
	TimeAnomaly				= 1 << 6	UMETA(DisplayName = "Time anomaly"),

	/** VEH-001's input layer neutralised a stale sample: the browser/device stopped reporting. Not a physics fault; recorded here so one report answers "why did the car stop responding?" */
	StaleInput				= 1 << 7	UMETA(DisplayName = "Stale input")
};
ENUM_CLASS_FLAGS(EVehicleFailureFlag);

/**
 * Tunable envelopes for the detector.
 *
 * A PLAIN STRUCT, not the DataAsset. UVehicleFailureThresholdsDataAsset owns the
 * authored values and CORE-003 range validation; this is what the pure function
 * consumes, so the function stays callable with no UObject at all.
 *
 * THE DUPLICATION IS REAL AND IS MADE SAFE BY A TEST. The asset's defaults and this
 * struct's defaults are two independent copies of the same numbers, exactly the drift
 * hazard CORE-003's VerifyRangesMatchMetadata exists for on the metadata side.
 * RacingSim.Vehicle.FailureThresholdDefaultsMatchAsset pins them together field by
 * field, and fails when a new field is added to one and not the other.
 *
 * EVERY NUMBER BELOW IS AN ORIGINAL PROTOTYPE ENVELOPE invented for this project, per
 * CLAUDE.md's unbranded-prototype rule and Docs/02-VehiclePhysics.md's "use envelopes
 * rather than fake precision". None is derived from a manufacturer figure, another
 * game, or any of the supplied reference images.
 */
struct FVehicleFailureThresholds
{
	/**
	 * Speed past which the body is considered to have exploded, CENTIMETRES PER SECOND.
	 * 15,000 cm/s == 540 km/h: far above anything this prototype can reach under power,
	 * far below the six-figure speeds a diverging solver produces within two steps.
	 */
	float MaxSpeedCms = 15000.0f;

	/**
	 * Angular speed envelope, DEGREES PER SECOND. 2160 == six full revolutions a second.
	 * A violent spin on a kerb is well inside this; a body being flung by a bad contact
	 * is not.
	 */
	float MaxAngularSpeedDegreesPerSecond = 2160.0f;

	/**
	 * Engine speed envelope, RPM. Generous: it must not fire on a tune with a high
	 * redline, only on a drivetrain solver that has diverged.
	 */
	float MaxEngineRpm = 30000.0f;

	/**
	 * Acceleration envelope, CENTIMETRES PER SECOND SQUARED. 8,000 cm/s^2 is about
	 * 8.15 g -- beyond any grip, brake or downforce this prototype has, and beyond a
	 * survivable impact, but below the step change a divergence produces.
	 *
	 * Measured from the change in speed between two snapshots over the measured wall
	 * time, so it is a chord and not an instantaneous derivative; a hard impact
	 * spanning one step can legitimately approach it, which is why it is set high.
	 */
	float MaxAccelerationCmsPerSecondSquared = 8000.0f;

	/**
	 * Tunnelling: how much further than |velocity| * step the body may move before the
	 * step is called unexplained. Dimensionless multiplier.
	 *
	 * Above 1.0 because velocity is sampled at the snapshot instants and a real step
	 * integrates a changing velocity between them -- a car accelerating hard genuinely
	 * covers more ground than its start-of-step velocity predicts.
	 */
	float TunnelVelocityFactor = 2.0f;

	/**
	 * Tunnelling: absolute slack added to the velocity-explained distance, CENTIMETRES.
	 * Covers the small-step case where the multiplier alone is a tiny number and
	 * ordinary solver jitter would trip it.
	 */
	float TunnelToleranceCm = 100.0f;

	/**
	 * Steps longer than this SUPPRESS tunnelling and acceleration analysis, SECONDS.
	 *
	 * Not a failure in itself: a load hitch, a breakpoint or a paused-then-resumed
	 * stream all produce one. Extrapolating a velocity across half a second says
	 * nothing useful, so the honest answer is to decline to judge that step rather than
	 * to report a fault the simulation did not commit.
	 */
	float MaxAnalysisStepSeconds = 0.5f;

	/**
	 * How far outside [0,1] a normalised suspension length may sit before the wheel
	 * state is called unstable. Dimensionless.
	 *
	 * Non-zero because Chaos writes this value from a solver and exact bounds are not
	 * guaranteed; 0.05 is well inside "the suspension model has diverged".
	 */
	float SuspensionLengthTolerance = 0.05f;

	/** All wheels off the ground for longer than this is unstable wheel state, SECONDS. A big jump is seconds; a car flipped onto its roof is forever. */
	float MaxAirborneSeconds = 5.0f;

	/** Persistent penetration: normalised suspension length at or below this counts as fully compressed. Dimensionless. */
	float PenetrationSuspensionLengthFraction = 0.02f;

	/** Persistent penetration: spring force above this counts as "under load", NEWTONS. Excludes a wheel merely resting at the bump stop with no load. */
	float PenetrationSpringForceN = 1.0f;

	/** Persistent penetration: how long a wheel must stay fully compressed under load before it is a fault, SECONDS. A kerb strike is a fraction of this. */
	float PenetrationPersistSeconds = 2.0f;

	/** A contact point further than this from the body origin is not a real contact, CENTIMETRES. Well outside any prototype chassis plus suspension travel. */
	float MaxContactDistanceCm = 1000.0f;
};

/**
 * The detector's memory between samples.
 *
 * PersistentPenetration and the airborne check are inherently temporal -- a single
 * snapshot cannot distinguish "the car is jumping" from "the car has been inside the
 * ground for two seconds" -- so the detector is not memoryless, and its memory is held
 * by the CALLER rather than in a static, so two vehicles cannot contaminate each
 * other's history.
 *
 * Reset() must be called whenever the vehicle is teleported or the session restarts,
 * for the same reason FVehicleInputProcessor::ResetState exists: accumulated history
 * from before a discontinuity describes a car that no longer exists.
 */
struct FVehicleFailureDetectorState
{
	/** Seconds with no wheel in contact. Cleared the moment any wheel touches down. */
	float AirborneSeconds = 0.0f;

	/** Per-wheel seconds spent fully compressed under load. Index-parallel to FVehicleTelemetrySnapshot::Wheels. */
	float PenetrationSeconds[MaxVehicleTelemetryWheels] = {};

	/**
	 * Set by NotifyDiscontinuity, consumed and cleared by the next evaluation, which then
	 * judges NOTHING that straddles the discontinuity.
	 *
	 * WHAT STRADDLES IT is wider than it first looks, and getting that wrong is what this
	 * flag exists to stop. The obvious half is every rate: a car teleported 5 m has moved
	 * 5 m in one step, and Tunnelling is the correct verdict for motion nobody announced
	 * and the wrong one for a reset somebody did. The non-obvious half is the CONTACT
	 * GEOMETRY. InvalidContact compares Current.LocationCm -- a game-thread pose, read
	 * after the teleport -- against Wheel.ContactPointCm, which the physics thread
	 * produced before it. That is a cross-frame comparison wearing a single-snapshot
	 * check's clothes, and it fired on exactly the reset it was meant to tolerate:
	 * "wheel 2 reports contact 1150.085664 cm from the body, beyond the 1000.000000 cm
	 * bound", one capture after an announced discontinuity, with the car sitting still.
	 *
	 * NonFiniteState, UnstableWheelState and StaleInput are deliberately NOT suppressed.
	 * A NaN one frame after a reset is a real fault and the reset is the likeliest cause;
	 * suppressing it would hide the failure at the moment it is most diagnosable.
	 */
	bool bDiscontinuityPending = false;

	/**
	 * Where the body was JUST BEFORE the last announced discontinuity, centimetres.
	 * Only meaningful while bHasPreDiscontinuityLocation is set.
	 *
	 * ONE EVALUATION IS NOT ENOUGH FOR THE CONTACT CHECK, and measurement is the reason.
	 * The two halves of a snapshot catch up to a teleport at different speeds: the
	 * chassis pose is read from the game thread and is correct the instant
	 * SetActorLocationAndRotation returns, while the wheel state is marshalled back from
	 * the physics thread a frame later (UChaosWheeledVehicleMovementComponent::
	 * FillWheelOutputState copies the async output, which the game thread also
	 * INTERPOLATES between two physics results). Probed on the real pipeline, driving one
	 * step at a time after an announced reset:
	 *
	 *   step=0 loc=(-2500,4330,55) [w0 contact=1 dist=3924.2 pt=(-297,1083,0)] ...
	 *   step=1 loc=(-2500,4330,60) [w0 contact=1 dist=171.5 pt=(-2661,4329,0)] ...
	 *
	 * The pose moved at step 0; the wheels did not move until step 1. So the FIRST
	 * post-reset evaluation and the one after it both read pre-teleport contact points,
	 * and a suppression that lasted exactly one evaluation let the second one through as
	 * Error-severity InvalidContact on a car sitting still.
	 *
	 * A fixed count of two would be wrong for a different reason: capture is paced on
	 * TelemetrySampleRateHz, so at a rate below the tick rate two captures span many
	 * ticks and the suppression would outlive the staleness it was aimed at. This is
	 * SELF-TERMINATING instead. A wheel whose contact point is within
	 * MaxContactDistanceCm of the PRE-discontinuity body pose is reporting a contact that
	 * was true for where the car used to be, and is skipped; the basis is dropped the
	 * first time a wheel in contact reports a point somewhere ELSE, which is the frame
	 * the physics output caught up. Nothing here depends on the capture rate, the tick
	 * rate, or whether Chaos is running its async path.
	 *
	 * Expiring on FRESH contact rather than on the absence of matching contact is load
	 * bearing. The capture immediately after a teleport reports every wheel out of
	 * contact with a zeroed contact point, and only the capture after THAT carries the
	 * stale points. Measured on the real pipeline, with the basis at (-147.8,1032.3,69.6)
	 * and dPre the distance from it:
	 *
	 *   cap=241 hasPre=1 loc=(-2500,4330,70) [w0 c=0 pt=(0,0,0)]      ... no evidence
	 *   cap=242 hasPre=1 loc=(-2500,4330,72) [w0 c=1 pt=(-297,1083,0) dPre=172.1]  stale
	 *   cap=243 hasPre=0 loc=(-2500,4330,74) [w0 c=1 pt=(-2661,4329,0) dPre=5081.2] fresh
	 *
	 * An earlier rule that expired the basis whenever nothing matched threw it away at
	 * 241 -- on a snapshot with no contact evidence at all -- and then had nothing left
	 * to suppress 242, which raised Error-severity InvalidContact on a stationary car.
	 */
	FVector PreDiscontinuityLocationCm = FVector::ZeroVector;

	/** Whether PreDiscontinuityLocationCm holds a usable pose. See it for why. */
	bool bHasPreDiscontinuityLocation = false;

	/** Drop all accumulated history. Call on teleport, respawn or session restart. */
	void Reset()
	{
		AirborneSeconds = 0.0f;
		for (float& Seconds : PenetrationSeconds)
		{
			Seconds = 0.0f;
		}

		PreDiscontinuityLocationCm = FVector::ZeroVector;
		bHasPreDiscontinuityLocation = false;
	}

	/**
	 * Reset, AND arm the one-evaluation suppression above.
	 *
	 * Separate from Reset() because the two are not the same request. Reset() means "this
	 * history is meaningless"; NotifyDiscontinuity() means that AND "the next sample is
	 * not comparable to the last one". A caller that teleports a car needs both, and
	 * every caller that only has Reset() available got the second one silently wrong.
	 */
	void NotifyDiscontinuity()
	{
		Reset();
		bDiscontinuityPending = true;
	}

	/**
	 * NotifyDiscontinuity, plus the pose the car is leaving behind.
	 *
	 * @param PreviousLocationCm where the body was before the teleport -- in practice the
	 *        last captured snapshot's LocationCm, which is the only pose the detector can
	 *        be sure the physics thread has already seen. Pass the no-argument overload
	 *        when there is no such snapshot; the contact check then falls back to the
	 *        single-evaluation suppression, which is strictly weaker but never wrong in
	 *        the raising direction.
	 */
	void NotifyDiscontinuity(const FVector& PreviousLocationCm)
	{
		NotifyDiscontinuity();

		if (PreviousLocationCm.ContainsNaN())
		{
			// A non-finite basis would make every distance comparison below false, which
			// silently degrades to the no-argument behaviour. Refusing it explicitly says
			// so rather than leaving it to floating-point luck.
			return;
		}

		PreDiscontinuityLocationCm = PreviousLocationCm;
		bHasPreDiscontinuityLocation = true;
	}
};

/**
 * What the detector found for one sample.
 *
 * ALLOCATION: Reason is populated ONLY when Flags != 0, so the clean path -- which is
 * every frame of a working simulation -- allocates nothing. CLAUDE.md's "avoid
 * per-frame allocations" is about the normal path; a string built at the moment a
 * physics solver has already corrupted itself is not the expensive part of that frame.
 */
struct FVehicleFailureReport
{
	/** Bitmask of EVehicleFailureFlag. */
	uint8 Flags = 0;

	/** The timestamp of the snapshot this report describes, SECONDS. */
	double TimestampSeconds = 0.0;

	/** The capture index of the snapshot this report describes. */
	int64 CaptureIndex = 0;

	/** The measured wall-clock step between the two snapshots, SECONDS. 0 when there was no usable previous snapshot. */
	float MeasuredStepSeconds = 0.0f;

	/** Human-readable, naming each flag and the value that tripped it. EMPTY on the clean path -- see the struct comment. */
	FString Reason;

	bool HasAnyFailure() const
	{
		return Flags != 0;
	}

	bool Has(const EVehicleFailureFlag Flag) const
	{
		return (Flags & static_cast<uint8>(Flag)) != 0;
	}
};

namespace RacingSim::Vehicle
{
	/**
	 * Evaluate one sample against the previous one.
	 *
	 * @param Previous    the last VALID snapshot, or a default-constructed one on the
	 *                    first call. An invalid Previous disables every rate-based
	 *                    check (tunnelling, acceleration, both accumulators) for this
	 *                    call rather than comparing against zeroes -- a first sample
	 *                    taken at a real world position would otherwise always look
	 *                    like a teleport from the origin.
	 * @param Current     the snapshot to judge. An invalid Current returns a clean
	 *                    report: "no sample was taken" is not a failure.
	 * @param Thresholds  envelopes; see FVehicleFailureThresholds.
	 * @param State       the caller's accumulator. Mutated.
	 */
	RACINGSIM_API FVehicleFailureReport EvaluateVehicleFailures(
		const FVehicleTelemetrySnapshot& Previous,
		const FVehicleTelemetrySnapshot& Current,
		const FVehicleFailureThresholds& Thresholds,
		FVehicleFailureDetectorState& State);

	/** Comma-separated flag names, for a log line or a test message. "None" when Flags == 0. */
	RACINGSIM_API FString DescribeVehicleFailureFlags(uint8 Flags);
}
