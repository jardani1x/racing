// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/VehicleInputProcessor.h"

namespace
{
	/**
	 * Clamp a raw axis into [MinValue, MaxValue], flagging what had to be done.
	 *
	 * Non-finite is reported separately from out-of-range and NOT clamped, because a
	 * NaN is not "slightly too large" -- it carries no information at all, so it is
	 * replaced with 0 (the neutral value for every axis this is used on, including
	 * the bipolar steer axis). Conflating the two would let a telemetry reader
	 * believe the browser sent 1.4 when it actually sent garbage.
	 */
	double SanitiseAxis(const double Raw, const double MinValue, const double MaxValue, uint8& Corrections)
	{
		if (!FMath::IsFinite(Raw))
		{
			Corrections |= static_cast<uint8>(EVehicleInputCorrection::NonFinite);
			return 0.0;
		}

		if (Raw < MinValue || Raw > MaxValue)
		{
			Corrections |= static_cast<uint8>(EVehicleInputCorrection::OutOfRange);
			return FMath::Clamp(Raw, MinValue, MaxValue);
		}

		return Raw;
	}

	/**
	 * Dead zone, saturation and response gamma for a unipolar [0,1] axis.
	 *
	 * The dead zone RESCALES rather than subtracting: at DeadZone 0.1 a raw 0.1 gives
	 * 0 and a raw 1.0 still gives 1.0. Subtract-and-clamp is the common shortcut and
	 * it silently costs the driver the top 10% of the axis -- full throttle would
	 * become 0.9, and the car would be measurably slower than the tune says with
	 * nothing to point at.
	 *
	 * Saturation ends the ramp early, so a stick that physically reaches only 0.9 can
	 * still deliver 1.0.
	 *
	 * Both bounds are guarded here even though FVehicleInputProfile::Validate rejects
	 * a non-positive band. Validation is not guaranteed to have run -- a profile can
	 * be constructed in code, or edited after validation -- and the consequence of
	 * skipping the guard is a division by zero that produces an infinity on the
	 * steering axis, which is precisely what this layer exists to prevent.
	 */
	float ShapeUnipolar(const float Value, const float DeadZone, const float Saturation, const float Gamma)
	{
		const float Clamped = FMath::Clamp(Value, 0.0f, 1.0f);

		const float SafeDeadZone = FMath::Clamp(DeadZone, 0.0f, 0.9f);
		const float SafeSaturation = FMath::Clamp(Saturation, 0.1f, 1.0f);

		const float Width = SafeSaturation - SafeDeadZone;
		if (!(Width > SMALL_NUMBER))
		{
			// Degenerate band: fall back to a pass-through rather than dividing. A
			// pass-through is the behaviour a driver can still work with; an
			// infinity is not.
			return Clamped;
		}

		if (Clamped <= SafeDeadZone)
		{
			return 0.0f;
		}

		const float Normalised = FMath::Clamp((Clamped - SafeDeadZone) / Width, 0.0f, 1.0f);

		const float SafeGamma = FMath::Clamp(Gamma, 0.2f, 5.0f);
		if (FMath::IsNearlyEqual(SafeGamma, 1.0f))
		{
			// Skip the pow for the overwhelmingly common linear case. Not just a
			// micro-optimisation: it also makes the linear path exact, so a test
			// asserting 0.5 gets 0.5 and not 0.49999997.
			return Normalised;
		}

		return FMath::Pow(Normalised, SafeGamma);
	}

	/** Bipolar [-1,1] shaping: shape the magnitude, restore the sign. Sign must survive exactly -- see below. */
	float ShapeBipolar(const float Value, const float DeadZone, const float Saturation, const float Gamma)
	{
		const float Magnitude = ShapeUnipolar(FMath::Abs(Value), DeadZone, Saturation, Gamma);

		// FMath::Sign(0) is 1, which would turn a centred stick into a right-hand
		// deflection of 0 -- harmless numerically, but it makes the sign of a zero
		// steer input asymmetric, and the equality tests downstream care.
		return (Value < 0.0f) ? -Magnitude : Magnitude;
	}

	/**
	 * Move Current toward Target by at most Rate * DeltaSeconds.
	 *
	 * THIS IS THE FRAME-RATE INDEPENDENCE GUARANTEE, and it is exactly linear in
	 * DeltaSeconds on purpose. An exponential smoother (Lerp with a fixed alpha, or
	 * even 1 - exp(-k*dt)) is the more common idiom and it is the wrong one here:
	 * the fixed-alpha form is outright frame-rate dependent, and the exponential form
	 * never actually reaches its target, so "hold the key for 0.25 s to get full
	 * throttle" would be false at every frame rate.
	 *
	 * With this form, N steps of dt and one step of N*dt cover the same distance, so
	 * 30 Hz, 60 Hz and 144 Hz agree to floating-point accumulation error and nothing
	 * more. RacingSim.Vehicle.InputFrameRateIndependence asserts exactly that.
	 *
	 * A Rate of zero means INSTANT -- see FVehicleInputProfile's rate fields for why
	 * that encoding was chosen over a separate boolean.
	 */
	float RateLimit(const float Current, const float Target, const float Rate, const float DeltaSeconds)
	{
		// A rate of zero means INSTANT, at any DeltaSeconds including zero -- see
		// FVehicleInputProfile's rate fields for why that encoding was chosen over a
		// separate boolean. An analog trigger must respond on the very first frame,
		// which may legitimately carry a DeltaSeconds of 0.
		if (!(Rate > 0.0f))
		{
			return Target;
		}

		// Zero or negative elapsed time on a RATE-LIMITED axis means NO ADVANCE.
		//
		// This is not the same case as the one above and conflating them was a real
		// defect, caught by RacingSim.Vehicle.InputFrameRateIndependence: the first
		// version of this function returned Target whenever `Rate <= 0 ||
		// DeltaSeconds <= 0`, so a hitched, paused or negative frame -- exactly the
		// input the MaxDeltaSeconds guard clamps to zero -- SNAPPED the axis straight
		// to full deflection instead of holding it. The guard against a hitch was
		// itself the thing that caused the snap.
		if (!(DeltaSeconds > 0.0f))
		{
			return Current;
		}

		const float MaxStep = Rate * DeltaSeconds;

		return FMath::Clamp(Target, Current - MaxStep, Current + MaxStep);
	}
}

bool FVehicleInputProcessor::ConfigureFromAsset(
	const UVehicleInputConfigDataAsset* Config,
	const ERacingInputDeviceType InDeviceType)
{
	// Always take the device, even on failure. The device the player is using is a
	// fact about the world; whether this project has a profile for it is a fact about
	// the content. Recording Unknown because a profile is missing would put a wrong
	// value on the result's version stamp.
	DeviceType = InDeviceType;
	ConfigAsset = Config;

	if (Config == nullptr)
	{
		Profile = FVehicleInputProfile();
		TransmissionMode = ETransmissionInputMode::Automatic;
		MaxDeltaSeconds = 0.1f;
		InputStaleAfterSeconds = 0.0f;
		return false;
	}

	TransmissionMode = Config->TransmissionMode;

	// Guarded rather than trusted: Validate() enforces the range, but nothing
	// guarantees Validate() ran on this asset, and a MaxDeltaSeconds of 0 would
	// freeze every rate-limited axis permanently.
	MaxDeltaSeconds = FMath::Clamp(Config->MaxDeltaSeconds, 0.001f, 1.0f);

	// VEH-004. Guarded the same way, and asymmetrically: a non-finite or negative
	// timeout DISABLES the guard rather than being clamped up to some minimum. This
	// mechanism can zero a driver's throttle, so a broken configuration must fail
	// toward "do nothing", never toward "intervene on a timescale nobody authored".
	InputStaleAfterSeconds = FMath::IsFinite(Config->InputStaleAfterSeconds)
		? FMath::Max(Config->InputStaleAfterSeconds, 0.0f)
		: 0.0f;

	if (const FVehicleInputProfile* Found = Config->FindProfile(InDeviceType))
	{
		Profile = *Found;
		return true;
	}

	// No profile for this device. Neutral shaping (no dead zone, linear, instant)
	// rather than another device's numbers -- see UVehicleInputConfigDataAsset::FindProfile
	// for why a silent fallback is worse than none.
	Profile = FVehicleInputProfile();
	return false;
}

void FVehicleInputProcessor::Configure(
	const FVehicleInputProfile& InProfile,
	const ERacingInputDeviceType InDeviceType,
	const ETransmissionInputMode InTransmissionMode,
	const float InMaxDeltaSeconds,
	const float InInputStaleAfterSeconds)
{
	Profile = InProfile;
	DeviceType = InDeviceType;
	TransmissionMode = InTransmissionMode;
	MaxDeltaSeconds = FMath::Clamp(InMaxDeltaSeconds, 0.001f, 1.0f);
	InputStaleAfterSeconds = FMath::IsFinite(InInputStaleAfterSeconds)
		? FMath::Max(InInputStaleAfterSeconds, 0.0f)
		: 0.0f;

	// Explicitly cleared: this overload is the "no asset" path, and leaving a stale
	// asset from an earlier ConfigureFromAsset would silently reintroduce
	// speed-sensitive steering that the caller did not ask for.
	ConfigAsset.Reset();
}

void FVehicleInputProcessor::ResetState()
{
	CurrentThrottle = 0.0f;
	CurrentBrake = 0.0f;
	CurrentSteer = 0.0f;

	// DELIBERATELY NOT CLEARED: bShiftUpWasHeld, bShiftDownWasHeld, ResetHeldSeconds,
	// bResetLatched.
	//
	// These four are not vehicle state, they are a memory of which BUTTONS ARE
	// PHYSICALLY DOWN, and a reset does not lift the driver's fingers off the pad.
	// Clearing them would manufacture events out of nothing:
	//
	//   - clearing bShiftUpWasHeld while the driver is still holding shift-up makes
	//     the next Tick see (held && !was-held), a rising edge, and the car shifts up
	//     on the grid without anyone touching anything;
	//   - clearing bResetLatched while the reset key is still held re-arms the hold,
	//     so continuing to hold the key fires a SECOND reset one HoldThreshold later,
	//     and holding it down resets repeatedly forever. Since every reset sets
	//     ERacingRunValidity::InvalidVehicleReset, that is an input bug that
	//     permanently invalidates every subsequent lap.
	//
	// Both self-correct the moment the driver releases the button, which is the
	// normal path back to a clean state. The smoothing axes above ARE cleared,
	// because those genuinely are vehicle state: a car that was at full lock and full
	// throttle when it went off must not resume at full lock and full throttle.
	LastCommand = FVehicleInputCommand();
}

FVehicleInputCommand FVehicleInputProcessor::Tick(
	const FVehicleInputRawSample& IncomingRaw,
	const double DeltaSeconds,
	const double TimestampSeconds)
{
	FVehicleInputCommand Command;
	uint8 Corrections = 0;

	// -- Time -----------------------------------------------------------------
	//
	// Everything downstream integrates this, so it is sanitised first and hard. A
	// non-finite delta would poison every rate-limited axis in one step and the
	// resulting NaN would persist in CurrentSteer forever, surviving every
	// subsequent good frame -- a single bad frame permanently breaking the car.
	float SafeDelta;
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0)
	{
		SafeDelta = 0.0f;
		Corrections |= static_cast<uint8>(EVehicleInputCorrection::DeltaClamped);
	}
	else if (DeltaSeconds > static_cast<double>(MaxDeltaSeconds))
	{
		// The hitch case. See UVehicleInputConfigDataAsset::MaxDeltaSeconds for why
		// this is a correctness guard and not a comfort setting.
		SafeDelta = MaxDeltaSeconds;
		Corrections |= static_cast<uint8>(EVehicleInputCorrection::DeltaClamped);
	}
	else
	{
		SafeDelta = static_cast<float>(DeltaSeconds);
	}

	// -- Staleness (VEH-004, closing VEH-001 MEDIUM-4) ------------------------
	//
	// The browser trust boundary's second failure mode. VEH-001 already rejects a
	// HOSTILE value; this rejects the ABSENCE of values -- a Pixel Streaming
	// disconnect, a backgrounded tab or a focus loss produces neither a Triggered nor
	// a Completed event, so the last non-zero throttle and steer stay latched in the
	// component's buffer indefinitely and the car drives itself into the scenery.
	//
	// The distinguishing signal is the SAMPLE's own timestamp, not the frame's:
	// Enhanced Input re-fires Triggered every frame for a genuinely held control, so a
	// held key keeps its stamp fresh and is correctly NOT stale, while a dead
	// connection's stamp stops advancing. That distinction is the whole mechanism, and
	// it is what RacingSim.Vehicle.InputStaleSample pins.
	//
	// Opt-in from both ends: a timeout must be configured AND the sample must actually
	// carry a stamp (see FVehicleInputRawSample::SampleTimestampSeconds on why 0 is
	// "never stamped" rather than "infinitely old"). A mechanism that can zero a
	// driver's throttle must not arm itself by accident.
	FVehicleInputRawSample Raw = IncomingRaw;

	const bool bStalenessArmed =
		InputStaleAfterSeconds > 0.0f
		&& FMath::IsFinite(TimestampSeconds)
		&& FMath::IsFinite(IncomingRaw.SampleTimestampSeconds)
		&& IncomingRaw.SampleTimestampSeconds > 0.0;

	if (bStalenessArmed
		&& (TimestampSeconds - IncomingRaw.SampleTimestampSeconds) > static_cast<double>(InputStaleAfterSeconds))
	{
		Corrections |= static_cast<uint8>(EVehicleInputCorrection::StaleSample);

		// Neutralised, not zeroed-on-output: the demands become zero TARGETS and the
		// rate limiter ramps them down, so a lost connection at speed lifts off and
		// unwinds the lock rather than snapping to zero -- which at 200 km/h is its own
		// loss of control. Same reasoning as resolving the pedal conflict before the
		// limiter rather than after it.
		Raw.Throttle = 0.0;
		Raw.Brake = 0.0;
		Raw.Steer = 0.0;
		Raw.Handbrake = 0.0;
		Raw.Clutch = 0.0;

		// The held flags go too. Left standing, a stale "shift up held" would fire a
		// phantom edge the moment the connection returned, and a stale "reset held"
		// would keep accumulating toward a reset that permanently invalidates the lap
		// -- a disconnected player must not be able to destroy their own run.
		Raw.bShiftUpHeld = false;
		Raw.bShiftDownHeld = false;
		Raw.bResetHeld = false;

		// SpeedCms is deliberately NOT neutralised. It is vehicle state pushed by the
		// pawn, not a device value, and it stays true while the connection is dead.
	}

	// -- Raw axes -------------------------------------------------------------
	const float RawThrottle  = static_cast<float>(SanitiseAxis(Raw.Throttle,  0.0, 1.0, Corrections));
	const float RawBrake     = static_cast<float>(SanitiseAxis(Raw.Brake,     0.0, 1.0, Corrections));
	const float RawSteer     = static_cast<float>(SanitiseAxis(Raw.Steer,    -1.0, 1.0, Corrections));
	const float RawHandbrake = static_cast<float>(SanitiseAxis(Raw.Handbrake, 0.0, 1.0, Corrections));
	const float RawClutch    = static_cast<float>(SanitiseAxis(Raw.Clutch,    0.0, 1.0, Corrections));

	// -- Shaping --------------------------------------------------------------
	float TargetThrottle = ShapeUnipolar(RawThrottle, Profile.PedalDeadZone, Profile.PedalSaturation, Profile.PedalResponseGamma);
	float TargetBrake    = ShapeUnipolar(RawBrake,    Profile.PedalDeadZone, Profile.PedalSaturation, Profile.PedalResponseGamma);
	float TargetSteer    = ShapeBipolar(RawSteer,     Profile.SteerDeadZone, Profile.SteerSaturation, Profile.SteerResponseGamma);

	// Handbrake and clutch are shaped but NOT rate-limited. A handbrake is a lever
	// yanked in one motion and a clutch is a pedal stamped; ramping either would put
	// lag between the driver and the two controls most often used to recover a car
	// that is already sideways.
	const float Handbrake = ShapeUnipolar(RawHandbrake, Profile.PedalDeadZone, Profile.PedalSaturation, Profile.PedalResponseGamma);

	// Under an automatic transmission the clutch slot is meaningless. Forced to 0
	// rather than passed through so a stale binding cannot half-disengage a drivetrain
	// that has no clutch model.
	const float Clutch = (TransmissionMode == ETransmissionInputMode::Manual)
		? ShapeUnipolar(RawClutch, Profile.PedalDeadZone, Profile.PedalSaturation, Profile.PedalResponseGamma)
		: 0.0f;

	// -- Pedal conflict -------------------------------------------------------
	//
	// Resolved on the TARGETS, before rate limiting, so the suppressed pedal ramps
	// down through the limiter instead of snapping to zero. Resolving afterwards
	// would produce a throttle that steps discontinuously the instant the brake is
	// touched, which is both worse to drive and a discontinuity in the telemetry.
	if (TargetThrottle > 0.0f && TargetBrake > 0.0f)
	{
		switch (Profile.PedalConflictPolicy)
		{
		case EVehiclePedalConflictPolicy::BrakeOverrides:
			TargetThrottle = 0.0f;
			Corrections |= static_cast<uint8>(EVehicleInputCorrection::PedalConflict);
			break;

		case EVehiclePedalConflictPolicy::LargerWins:
			// Ties go to the brake. An arbitrary rule, but a documented and
			// deterministic one, and it errs toward slowing down.
			if (TargetBrake >= TargetThrottle)
			{
				TargetThrottle = 0.0f;
			}
			else
			{
				TargetBrake = 0.0f;
			}
			Corrections |= static_cast<uint8>(EVehicleInputCorrection::PedalConflict);
			break;

		case EVehiclePedalConflictPolicy::Independent:
		default:
			// Both survive. Not flagged as a correction, because nothing was corrected.
			break;
		}
	}

	// -- Rate limiting --------------------------------------------------------
	CurrentThrottle = RateLimit(
		CurrentThrottle,
		TargetThrottle,
		(TargetThrottle > CurrentThrottle) ? Profile.ThrottleRiseRate : Profile.ThrottleFallRate,
		SafeDelta);

	CurrentBrake = RateLimit(
		CurrentBrake,
		TargetBrake,
		(TargetBrake > CurrentBrake) ? Profile.BrakeRiseRate : Profile.BrakeFallRate,
		SafeDelta);

	// Steering picks its rate by whether the wheel is moving TOWARD centre, on
	// magnitude. A lock-to-lock flick (|target| == |current|, opposite sign) uses the
	// deflection rate, which is the conservative choice: it is the slower of the two
	// in every sane profile, so a flick cannot be faster than a deliberate turn-in.
	const bool bReturningToCentre = FMath::Abs(TargetSteer) < FMath::Abs(CurrentSteer);
	CurrentSteer = RateLimit(
		CurrentSteer,
		TargetSteer,
		bReturningToCentre ? Profile.SteerCentringRate : Profile.SteerRate,
		SafeDelta);

	// -- Speed-sensitive steering ---------------------------------------------
	//
	// Applied AFTER rate limiting, so the scale attenuates the wheel angle rather
	// than the rate at which the driver may move it. Applying it before would make
	// the steering not only smaller at speed but also slower, which is a different
	// (and much worse) feel than the one this feature is for.
	//
	// The cm/s -> km/h conversion lives inside GetSteerScaleForSpeedCms.
	float SteerScale = 1.0f;
	if (const UVehicleInputConfigDataAsset* Config = ConfigAsset.Get())
	{
		SteerScale = Config->GetSteerScaleForSpeedCms(Raw.SpeedCms);
	}

	// -- Gear requests --------------------------------------------------------
	EVehicleGearRequest GearRequest = EVehicleGearRequest::None;

	if (TransmissionMode == ETransmissionInputMode::Manual)
	{
		const bool bShiftUpEdge = Raw.bShiftUpHeld && !bShiftUpWasHeld;
		const bool bShiftDownEdge = Raw.bShiftDownHeld && !bShiftDownWasHeld;

		// Both on the same Tick is ambiguous. Up wins -- arbitrary, but deterministic
		// and documented, which is what a consumer needs. Silently emitting None
		// would lose a shift the driver definitely asked for.
		if (bShiftUpEdge)
		{
			GearRequest = EVehicleGearRequest::ShiftUp;
		}
		else if (bShiftDownEdge)
		{
			GearRequest = EVehicleGearRequest::ShiftDown;
		}
	}

	// Edge state is tracked regardless of transmission mode, so that switching to
	// Manual while a shift key is already held does not manufacture an edge.
	bShiftUpWasHeld = Raw.bShiftUpHeld;
	bShiftDownWasHeld = Raw.bShiftDownHeld;

	// -- Reset hold -----------------------------------------------------------
	//
	// A press is not enough. A reset sets ERacingRunValidity::InvalidVehicleReset on
	// the run, so this is the one control in the layer that can silently destroy a
	// clean lap, and it is gated behind a deliberate hold.
	bool bResetRequested = false;

	// Guarded rather than trusted, for the same reason as MaxDeltaSeconds: a profile
	// built in code can carry 0, and a hold threshold of 0 would fire a reset on the
	// first frame the key is touched -- exactly the accident the hold prevents.
	const float HoldThreshold = FMath::Clamp(Profile.ResetHoldSeconds, 0.05f, 10.0f);

	if (Raw.bResetHeld)
	{
		if (!bResetLatched)
		{
			ResetHeldSeconds += SafeDelta;

			if (ResetHeldSeconds >= HoldThreshold)
			{
				bResetRequested = true;
				bResetLatched = true;
			}
		}
	}
	else
	{
		// Released: the latch and the accumulator both clear, so a second reset needs
		// a second full hold. Partial holds do not accumulate across releases.
		ResetHeldSeconds = 0.0f;
		bResetLatched = false;
	}

	const float ResetHoldProgress = bResetLatched
		? 1.0f
		: FMath::Clamp(ResetHeldSeconds / HoldThreshold, 0.0f, 1.0f);

	// -- Assemble -------------------------------------------------------------
	//
	// Every field is clamped on the way out even though each path above already
	// bounds it. That is one redundant clamp per field per frame against a class of
	// defect -- a NaN or an out-of-range value reaching Chaos -- that Gate C treats
	// as a test failure and that is very hard to trace back once it has propagated
	// into a physics solver. The cost is negligible and the guarantee is
	// unconditional, which is what lets IsFiniteAndInRange() be a promise.
	Command.Throttle = FMath::Clamp(CurrentThrottle, 0.0f, 1.0f);
	Command.Brake = FMath::Clamp(CurrentBrake, 0.0f, 1.0f);
	Command.Steer = FMath::Clamp(CurrentSteer * SteerScale, -1.0f, 1.0f);
	Command.Handbrake = FMath::Clamp(Handbrake, 0.0f, 1.0f);
	Command.Clutch = FMath::Clamp(Clutch, 0.0f, 1.0f);
	Command.GearRequest = GearRequest;
	Command.bResetRequested = bResetRequested;
	Command.ResetHoldProgress = ResetHoldProgress;
	Command.TimestampSeconds = FMath::IsFinite(TimestampSeconds) ? TimestampSeconds : 0.0;
	Command.DeltaSeconds = SafeDelta;
	Command.DeviceType = DeviceType;
	Command.Corrections = Corrections;

	// Last line of defence. If a future change ever breaks the guarantee above, the
	// smoothing state is the thing that must not be allowed to keep the poison --
	// CurrentSteer feeds itself every frame, so one NaN there is permanent.
	if (!Command.IsFiniteAndInRange())
	{
		CurrentThrottle = 0.0f;
		CurrentBrake = 0.0f;
		CurrentSteer = 0.0f;

		Command = FVehicleInputCommand();
		Command.TimestampSeconds = FMath::IsFinite(TimestampSeconds) ? TimestampSeconds : 0.0;
		Command.DeviceType = DeviceType;
		Command.Corrections = Corrections | static_cast<uint8>(EVehicleInputCorrection::NonFinite);
	}

	LastCommand = Command;

	return Command;
}
