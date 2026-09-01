// Copyright RacingSim. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Vehicle/VehicleInputProcessor.h"

// For std::numeric_limits<double>::infinity(). Written as an explicit infinity
// rather than as 1.0/0.0, which is undefined behaviour the compiler is entitled to
// fold into something else entirely -- and a test whose hostile value the optimiser
// removed would pass while proving nothing.
#include <limits>

/**
 * VEH-001: the input processor.
 *
 * ---------------------------------------------------------------------------
 * Why these are SmokeFilter, and why that was only possible by design
 * ---------------------------------------------------------------------------
 *
 * Docs/Environment.md records two hard constraints that between them decide the
 * shape of this file:
 *
 *   1. A SmokeFilter test in this project CANNOT construct a non-template Actor or
 *      UActorComponent. FEngineLoop::PreInit runs smoke tests itself
 *      (LaunchEngineLoop.cpp:4376), before RegisterEngineElements() runs in
 *      UEngine::Init (UnrealEngine.cpp:2399); UActorComponent::PostInitProperties
 *      creates an editor element for every non-template component
 *      (ActorComponent.cpp:588) and checkf's on the unregistered type, killing the
 *      process with NO index.json at all -- the gate reports nothing rather than a
 *      failure.
 *   2. A test carrying a filter that no recorded gate command uses will sit green
 *      and never execute.
 *
 * FVehicleInputProcessor is a plain struct precisely so that both constraints are
 * satisfiable at once: every rule in VEH-001 is reachable here with no actor, no
 * world, no subsystem and no level, at the fast gate that actually runs.
 *
 * Nothing in this file constructs a UObject. The UVehicleInputConfigDataAsset tests
 * live in VehicleInputConfigSpec.cpp.
 */

namespace
{
	/** Linear, instant, no dead zone. The neutral baseline -- shaping is opt-in per test. */
	FVehicleInputProfile MakeNeutralProfile()
	{
		return FVehicleInputProfile();
	}

	/** Step the processor N times at a fixed rate, holding one raw sample. Returns the last command. */
	FVehicleInputCommand StepFor(
		FVehicleInputProcessor& Processor,
		const FVehicleInputRawSample& Raw,
		const int32 Steps,
		const double DeltaSeconds)
	{
		FVehicleInputCommand Command;
		double Now = 0.0;

		for (int32 Index = 0; Index < Steps; ++Index)
		{
			Now += DeltaSeconds;
			Command = Processor.Tick(Raw, DeltaSeconds, Now);
		}

		return Command;
	}
}

// ---------------------------------------------------------------------------
// Shaping: dead zone, saturation, gamma
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleInputShapingTest,
	"RacingSim.Vehicle.InputShaping",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleInputShapingTest::RunTest(const FString& Parameters)
{
	constexpr float Tol = 1.0e-5f;

	// -- Dead zone RESCALES; it does not subtract ----------------------------
	//
	// This is the assertion that matters most in this test. Subtract-and-clamp is the
	// common shortcut and it silently costs the driver the top of the axis: full
	// throttle would deliver 0.9 instead of 1.0, and the car would be measurably
	// slower than its tune with nothing in the game to point at. Asserting the
	// endpoint (1.0 -> 1.0) is what distinguishes the two implementations; asserting
	// only the dead band would pass for both.
	{
		FVehicleInputProfile Profile = MakeNeutralProfile();
		Profile.PedalDeadZone = 0.1f;
		Profile.PedalSaturation = 1.0f;

		FVehicleInputProcessor Processor;
		Processor.Configure(Profile, ERacingInputDeviceType::Gamepad, ETransmissionInputMode::Automatic, 0.1f);

		FVehicleInputRawSample Raw;

		Raw.Throttle = 0.05;
		TestNearlyEqual(TEXT("Inside dead zone reads zero"), Processor.Tick(Raw, 0.016, 1.0).Throttle, 0.0f, Tol);

		Raw.Throttle = 0.1;
		TestNearlyEqual(TEXT("Exactly at the dead zone edge reads zero"), Processor.Tick(Raw, 0.016, 2.0).Throttle, 0.0f, Tol);

		// THE endpoint assertion. (1.0 - 0.1) / (1.0 - 0.1) == 1.0.
		Raw.Throttle = 1.0;
		TestNearlyEqual(TEXT("Full raw throttle still reaches 1.0 through a dead zone"), Processor.Tick(Raw, 0.016, 3.0).Throttle, 1.0f, Tol);

		// Midpoint of the rescaled band: (0.55 - 0.1) / 0.9 == 0.5.
		Raw.Throttle = 0.55;
		TestNearlyEqual(TEXT("Dead zone rescales rather than shifting"), Processor.Tick(Raw, 0.016, 4.0).Throttle, 0.5f, Tol);
	}

	// -- Saturation reaches full output before full input --------------------
	{
		FVehicleInputProfile Profile = MakeNeutralProfile();
		Profile.SteerDeadZone = 0.0f;
		Profile.SteerSaturation = 0.8f;

		FVehicleInputProcessor Processor;
		Processor.Configure(Profile, ERacingInputDeviceType::Gamepad, ETransmissionInputMode::Automatic, 0.1f);

		FVehicleInputRawSample Raw;

		Raw.Steer = 0.8;
		TestNearlyEqual(TEXT("Steer reaches full lock at the saturation point"), Processor.Tick(Raw, 0.016, 1.0).Steer, 1.0f, Tol);

		// Beyond saturation must CLAMP, not overshoot -- an axis reading 1.25 would
		// arrive at Chaos as a steering angle beyond the mechanical lock.
		Raw.Steer = 1.0;
		TestNearlyEqual(TEXT("Beyond saturation clamps to full lock"), Processor.Tick(Raw, 0.016, 2.0).Steer, 1.0f, Tol);

		// Sign must survive shaping. An inverted steering axis is a defect that looks
		// like a physics bug for a week.
		Raw.Steer = -0.8;
		TestNearlyEqual(TEXT("Negative steer stays negative through shaping"), Processor.Tick(Raw, 0.016, 3.0).Steer, -1.0f, Tol);

		Raw.Steer = 0.0;
		TestNearlyEqual(TEXT("Centred steer is exactly zero"), Processor.Tick(Raw, 0.016, 4.0).Steer, 0.0f, 0.0f);
	}

	// -- Gamma is monotonic, endpoint-preserving, and bends the right way ----
	//
	// Asserting the endpoints alone would pass for gamma applied in either direction,
	// since 0^g == 0 and 1^g == 1 for every legal gamma. The midpoint is what pins
	// the direction: gamma > 1 must give FINER control near centre, i.e. a smaller
	// output for the same input.
	{
		FVehicleInputProfile Profile = MakeNeutralProfile();
		Profile.SteerResponseGamma = 2.0f;

		FVehicleInputProcessor Processor;
		Processor.Configure(Profile, ERacingInputDeviceType::Gamepad, ETransmissionInputMode::Automatic, 0.1f);

		FVehicleInputRawSample Raw;

		Raw.Steer = 0.5;
		const float Half = Processor.Tick(Raw, 0.016, 1.0).Steer;
		TestNearlyEqual(TEXT("Gamma 2.0 maps 0.5 to 0.25"), Half, 0.25f, Tol);
		TestTrue(TEXT("Gamma > 1 gives finer control near centre"), Half < 0.5f);

		Raw.Steer = 1.0;
		TestNearlyEqual(TEXT("Gamma preserves the full-lock endpoint"), Processor.Tick(Raw, 0.016, 2.0).Steer, 1.0f, Tol);

		Raw.Steer = -0.5;
		TestNearlyEqual(TEXT("Gamma is symmetric about centre"), Processor.Tick(Raw, 0.016, 3.0).Steer, -0.25f, Tol);
	}

	// -- A degenerate band must not divide by zero ---------------------------
	//
	// FVehicleInputProfile::Validate rejects saturation <= dead zone, but validation
	// is not guaranteed to have run: a profile can be built in code, or edited after
	// validation. The consequence of an unguarded divide here is an infinity on the
	// steering axis -- exactly what this layer exists to prevent -- so the guard is
	// asserted rather than assumed.
	{
		FVehicleInputProfile Profile = MakeNeutralProfile();
		Profile.SteerDeadZone = 0.9f;
		Profile.SteerSaturation = 0.1f;   // inverted on purpose; never validated

		FVehicleInputProcessor Processor;
		Processor.Configure(Profile, ERacingInputDeviceType::Gamepad, ETransmissionInputMode::Automatic, 0.1f);

		FVehicleInputRawSample Raw;
		Raw.Steer = 0.5;

		const FVehicleInputCommand Command = Processor.Tick(Raw, 0.016, 1.0);
		TestTrue(TEXT("A degenerate dead-zone/saturation band still yields a finite in-range command"), Command.IsFiniteAndInRange());
	}

	return true;
}

// ---------------------------------------------------------------------------
// Frame-rate independence
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleInputFrameRateTest,
	"RacingSim.Vehicle.InputFrameRateIndependence",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleInputFrameRateTest::RunTest(const FString& Parameters)
{
	// CLAUDE.md: "Keep gameplay independent from frame rate", and Gate B names it
	// explicitly. The rate limiter is the only stateful integrator in this layer, so
	// it is the only place frame-rate dependence can enter the input path.
	//
	// The claim being tested is stronger than "the three rates agree": it is that the
	// axis value is exactly Rate * ElapsedTime at EVERY rate. That distinguishes the
	// linear limiter this project uses from an exponential smoother
	// (Lerp with a fixed alpha, or 1 - exp(-k*dt)), which is the more common idiom
	// and which either is outright frame-rate dependent or never reaches its target.

	constexpr float RiseRate = 4.0f;         // 1/s -- 0.25 s from 0 to full throttle
	constexpr double TargetElapsed = 0.2;    // stop short of saturation, or every rate ties at 1.0

	constexpr int32 NumFrameRates = 3;
	const double FrameRates[NumFrameRates] = { 30.0, 60.0, 144.0 };

	float Values[NumFrameRates] = {};

	for (int32 Index = 0; Index < NumFrameRates; ++Index)
	{
		const double Hz = FrameRates[Index];
		const double Delta = 1.0 / Hz;

		FVehicleInputProfile Profile = MakeNeutralProfile();
		Profile.ThrottleRiseRate = RiseRate;

		FVehicleInputProcessor Processor;
		Processor.Configure(Profile, ERacingInputDeviceType::Keyboard, ETransmissionInputMode::Automatic, 0.1f);

		FVehicleInputRawSample Raw;
		Raw.Throttle = 1.0;   // a key is a step function: 0 or 1, never in between

		// A FIXED step count, not a `while (Elapsed < Target)` loop.
		//
		// The loop form was the first version and it was wrong in a way worth
		// recording: accumulating 1/60 twelve times in double gives
		// 0.19999999999999998, which is < 0.2, so the loop took a THIRTEENTH step
		// while 30 Hz took a seventh, and the two rates were then compared over
		// different elapsed times. The test failed against correct production code.
		// Deriving the count up front removes the floating-point cliff entirely.
		const int32 Steps = FMath::RoundToInt32(TargetElapsed * Hz);

		double Elapsed = 0.0;
		FVehicleInputCommand Command;

		for (int32 Step = 0; Step < Steps; ++Step)
		{
			Elapsed += Delta;
			Command = Processor.Tick(Raw, Delta, Elapsed);
		}

		Values[Index] = Command.Throttle;

		// The exact-linearity claim, per rate. 1e-5 absorbs float accumulation over
		// up to 29 steps and nothing else -- it is far tighter than any behavioural
		// difference a driver could feel, and far tighter than the ~15% spread an
		// exponential smoother would show across 30 vs 144 Hz.
		TestNearlyEqual(
			*FString::Printf(TEXT("At %.0f Hz the ramp is exactly Rate * elapsed"), Hz),
			Command.Throttle,
			static_cast<float>(RiseRate * Elapsed),
			1.0e-5f);

		TestTrue(
			*FString::Printf(TEXT("At %.0f Hz the command stays finite and in range"), Hz),
			Command.IsFiniteAndInRange());
	}

	// 30 Hz and 60 Hz both land on exactly 0.2 s elapsed (6 and 12 whole steps), so
	// their VALUES must agree directly, with no elapsed-time term to explain a
	// difference away. This is the cross-rate assertion in its sharpest form.
	TestNearlyEqual(TEXT("30 Hz and 60 Hz reach the same value over the same wall time"), Values[0], Values[1], 1.0e-5f);

	// 144 Hz cannot land exactly on 0.2 s (28.8 steps), so it is compared against its
	// own elapsed time above, and only bounded here. Asserting equality with the
	// others would be asserting a rounding artefact.
	TestTrue(TEXT("144 Hz lands within one step of the others"), FMath::Abs(Values[2] - Values[0]) < RiseRate / 144.0f + 1.0e-5f);

	// -- The hitch clamp -----------------------------------------------------
	//
	// A real event, not a hypothetical one: a Pixel Streaming session that is
	// backgrounded, hitches on a shader compile, or resumes after a tab restore
	// delivers a DeltaSeconds measured in seconds. Unclamped, one such frame moves a
	// rate-limited axis across its whole range in a single step -- full lock from
	// centre -- which is the frame-rate dependence Gate B forbids.
	{
		FVehicleInputProfile Profile = MakeNeutralProfile();
		Profile.ThrottleRiseRate = RiseRate;

		FVehicleInputProcessor Processor;
		Processor.Configure(Profile, ERacingInputDeviceType::Keyboard, ETransmissionInputMode::Automatic, 0.1f);

		FVehicleInputRawSample Raw;
		Raw.Throttle = 1.0;

		const FVehicleInputCommand Command = Processor.Tick(Raw, 5.0, 1.0);

		// 4.0 * 0.1 == 0.4, NOT 1.0. Asserting the value and not merely the flag is
		// what makes this a test of the clamp rather than of the reporting.
		TestNearlyEqual(TEXT("A 5-second hitch advances the ramp by MaxDeltaSeconds only"), Command.Throttle, 0.4f, 1.0e-5f);
		TestTrue(TEXT("The hitch is reported as DeltaClamped"), Command.HasCorrection(EVehicleInputCorrection::DeltaClamped));
		TestNearlyEqual(TEXT("The command reports the clamped delta it actually used"), Command.DeltaSeconds, 0.1f, 1.0e-6f);
	}

	// -- Negative and non-finite deltas --------------------------------------
	{
		FVehicleInputProfile Profile = MakeNeutralProfile();
		Profile.ThrottleRiseRate = RiseRate;

		FVehicleInputProcessor Processor;
		Processor.Configure(Profile, ERacingInputDeviceType::Keyboard, ETransmissionInputMode::Automatic, 0.1f);

		FVehicleInputRawSample Raw;
		Raw.Throttle = 1.0;

		// A negative delta would run the limiter BACKWARDS. Treated as zero.
		const FVehicleInputCommand Negative = Processor.Tick(Raw, -1.0, 1.0);
		TestNearlyEqual(TEXT("A negative delta advances nothing"), Negative.Throttle, 0.0f, 1.0e-6f);
		TestTrue(TEXT("A negative delta is reported as DeltaClamped"), Negative.HasCorrection(EVehicleInputCorrection::DeltaClamped));

		// A NaN delta is the dangerous one: it would poison CurrentThrottle
		// permanently, so one bad frame would break the car for the whole session.
		const FVehicleInputCommand NotANumber = Processor.Tick(Raw, FMath::Sqrt(-1.0), 2.0);
		TestTrue(TEXT("A NaN delta yields a finite in-range command"), NotANumber.IsFiniteAndInRange());
		TestTrue(TEXT("A NaN delta is reported as DeltaClamped"), NotANumber.HasCorrection(EVehicleInputCorrection::DeltaClamped));

		// The state survived: a good frame after the bad ones still works. This is
		// the assertion that proves the NaN did not persist in the smoothing state.
		const FVehicleInputCommand Recovered = Processor.Tick(Raw, 0.05, 3.0);
		TestNearlyEqual(TEXT("A good frame after a NaN frame ramps normally"), Recovered.Throttle, 0.2f, 1.0e-5f);
	}

	return true;
}

// ---------------------------------------------------------------------------
// Hostile input
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleInputHostileTest,
	"RacingSim.Vehicle.InputHostileValues",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleInputHostileTest::RunTest(const FString& Parameters)
{
	// This is not defensive paranoia, it is the actual threat model. Under Pixel
	// Streaming the axis value originates in a BROWSER, travels the network as a
	// number chosen by a client this project does not control, and arrives here. The
	// processor is the only trust boundary on the path to Chaos, and a NaN that
	// reaches the solver is a Gate C failure that VEH-004 would have to catch after
	// the fact.

	const double NaNValue = FMath::Sqrt(-1.0);
	const double PosInf = std::numeric_limits<double>::infinity();
	const double NegInf = -PosInf;

	struct FCase
	{
		const TCHAR* Name;
		FVehicleInputRawSample Raw;
		bool bExpectNonFinite;
		bool bExpectOutOfRange;
	};

	TArray<FCase> Cases;

	{
		FCase Case{ TEXT("NaN throttle"), FVehicleInputRawSample(), true, false };
		Case.Raw.Throttle = NaNValue;
		Cases.Add(Case);
	}
	{
		FCase Case{ TEXT("+Inf steer"), FVehicleInputRawSample(), true, false };
		Case.Raw.Steer = PosInf;
		Cases.Add(Case);
	}
	{
		FCase Case{ TEXT("-Inf brake"), FVehicleInputRawSample(), true, false };
		Case.Raw.Brake = NegInf;
		Cases.Add(Case);
	}
	{
		FCase Case{ TEXT("Steer far beyond full lock"), FVehicleInputRawSample(), false, true };
		Case.Raw.Steer = 12000.0;
		Cases.Add(Case);
	}
	{
		FCase Case{ TEXT("Negative throttle"), FVehicleInputRawSample(), false, true };
		Case.Raw.Throttle = -5.0;
		Cases.Add(Case);
	}
	{
		FCase Case{ TEXT("Every axis hostile at once"), FVehicleInputRawSample(), true, true };
		Case.Raw.Throttle = NaNValue;
		Case.Raw.Brake = PosInf;
		Case.Raw.Steer = -900.0;
		Case.Raw.Handbrake = NaNValue;
		Case.Raw.Clutch = 47.0;
		Case.Raw.SpeedCms = NaNValue;
		Cases.Add(Case);
	}

	for (const FCase& Case : Cases)
	{
		// Fresh processor per case: the point is that a hostile sample cannot corrupt
		// state, and reusing one processor would let an earlier case's recovery mask
		// a later case's failure.
		FVehicleInputProcessor Processor;
		Processor.Configure(MakeNeutralProfile(), ERacingInputDeviceType::RemoteStreamed, ETransmissionInputMode::Manual, 0.1f);

		const FVehicleInputCommand Command = Processor.Tick(Case.Raw, 0.016, 1.0);

		TestTrue(*FString::Printf(TEXT("%s: command is finite and in range"), Case.Name), Command.IsFiniteAndInRange());

		// Individually restated rather than relying only on IsFiniteAndInRange, so a
		// failure names the axis instead of the aggregate.
		TestTrue(*FString::Printf(TEXT("%s: throttle finite"), Case.Name), FMath::IsFinite(Command.Throttle));
		TestTrue(*FString::Printf(TEXT("%s: brake finite"), Case.Name), FMath::IsFinite(Command.Brake));
		TestTrue(*FString::Printf(TEXT("%s: steer finite"), Case.Name), FMath::IsFinite(Command.Steer));
		TestTrue(*FString::Printf(TEXT("%s: handbrake finite"), Case.Name), FMath::IsFinite(Command.Handbrake));
		TestTrue(*FString::Printf(TEXT("%s: clutch finite"), Case.Name), FMath::IsFinite(Command.Clutch));

		if (Case.bExpectNonFinite)
		{
			// The correction must be RECORDED, not merely survived. A processor that
			// silently swallowed a NaN would pass every assertion above and leave
			// VEH-004 with no evidence that the browser was sending garbage.
			TestTrue(*FString::Printf(TEXT("%s: reported as NonFinite"), Case.Name),
				Command.HasCorrection(EVehicleInputCorrection::NonFinite));
		}

		if (Case.bExpectOutOfRange)
		{
			TestTrue(*FString::Printf(TEXT("%s: reported as OutOfRange"), Case.Name),
				Command.HasCorrection(EVehicleInputCorrection::OutOfRange));
		}

		TestTrue(*FString::Printf(TEXT("%s: WasCorrected agrees with the bitmask"), Case.Name),
			Command.WasCorrected() == (Command.Corrections != 0));

		// The state must be clean afterwards, which is the claim that a per-frame
		// clamp alone would not establish: CurrentSteer feeds itself every frame, so
		// a single NaN stored there is permanent.
		FVehicleInputRawSample Clean;
		Clean.Steer = 0.5;
		const FVehicleInputCommand After = Processor.Tick(Clean, 0.016, 2.0);

		TestTrue(*FString::Printf(TEXT("%s: a clean sample afterwards is uncorrupted"), Case.Name), After.IsFiniteAndInRange());
		TestNearlyEqual(*FString::Printf(TEXT("%s: a clean sample afterwards is exact"), Case.Name), After.Steer, 0.5f, 1.0e-5f);
		TestTrue(*FString::Printf(TEXT("%s: a clean sample afterwards reports no correction"), Case.Name), !After.WasCorrected());
	}

	// A default-constructed command must be the safe standing-still command. A
	// consumer that reads a command before the component initialises -- unpossessed
	// pawn, missing config -- gets a coasting car rather than an undefined one.
	{
		const FVehicleInputCommand Default;
		TestTrue(TEXT("A default command is finite and in range"), Default.IsFiniteAndInRange());
		TestNearlyEqual(TEXT("A default command has no throttle"), Default.Throttle, 0.0f, 0.0f);
		TestNearlyEqual(TEXT("A default command has no brake"), Default.Brake, 0.0f, 0.0f);
		TestNearlyEqual(TEXT("A default command is straight ahead"), Default.Steer, 0.0f, 0.0f);
		TestTrue(TEXT("A default command requests no reset"), !Default.bResetRequested);
		TestTrue(TEXT("A default command requests no gear"), Default.GearRequest == EVehicleGearRequest::None);
		TestTrue(TEXT("A default command reports no corrections"), !Default.WasCorrected());
	}

	return true;
}

// ---------------------------------------------------------------------------
// Reset hold
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleInputResetTest,
	"RacingSim.Vehicle.InputResetHold",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleInputResetTest::RunTest(const FString& Parameters)
{
	// The reset is the one control in this layer that can silently destroy a clean
	// lap: it sets ERacingRunValidity::InvalidVehicleReset on the run
	// (Core/RacingSimTypes.h), and the driver would not find out until the results
	// screen. Hence a hold rather than a press, and hence a one-shot request rather
	// than a level.

	constexpr float HoldSeconds = 0.5f;

	auto MakeProcessor = [](FVehicleInputProcessor& Processor)
	{
		FVehicleInputProfile Profile = MakeNeutralProfile();
		Profile.ResetHoldSeconds = HoldSeconds;
		Processor.Configure(Profile, ERacingInputDeviceType::Keyboard, ETransmissionInputMode::Automatic, 0.1f);
	};

	// -- A short press must NOT reset ----------------------------------------
	{
		FVehicleInputProcessor Processor;
		MakeProcessor(Processor);

		FVehicleInputRawSample Held;
		Held.bResetHeld = true;

		// 0.16 s of holding, well short of 0.5 s.
		const FVehicleInputCommand Command = StepFor(Processor, Held, 10, 0.016);

		TestTrue(TEXT("A short press does not request a reset"), !Command.bResetRequested);
		TestTrue(TEXT("A short press does report partial hold progress"), Command.ResetHoldProgress > 0.0f && Command.ResetHoldProgress < 1.0f);

		// Released before the threshold: progress must NOT accumulate across the
		// release, or repeated taps would eventually fire a reset the driver never
		// asked for.
		FVehicleInputRawSample Released;
		const FVehicleInputCommand AfterRelease = Processor.Tick(Released, 0.016, 99.0);
		TestNearlyEqual(TEXT("Releasing clears hold progress"), AfterRelease.ResetHoldProgress, 0.0f, 0.0f);

		const FVehicleInputCommand Tapped = StepFor(Processor, Held, 10, 0.016);
		TestTrue(TEXT("Partial holds do not accumulate across a release"), !Tapped.bResetRequested);
	}

	// -- A full hold fires exactly ONCE --------------------------------------
	//
	// The one-shot guarantee is load-bearing for Race/: a held key that fired every
	// frame would call URaceLapTracker::NotifyVehicleReset sixty times a second.
	{
		FVehicleInputProcessor Processor;
		MakeProcessor(Processor);

		FVehicleInputRawSample Held;
		Held.bResetHeld = true;

		int32 RequestCount = 0;
		double Elapsed = 0.0;
		double FiredAt = -1.0;

		// Hold for 2 s -- four times the threshold. A repeating implementation would
		// fire three more times in that window.
		for (int32 Index = 0; Index < 125; ++Index)
		{
			Elapsed += 0.016;
			const FVehicleInputCommand Command = Processor.Tick(Held, 0.016, Elapsed);

			if (Command.bResetRequested)
			{
				++RequestCount;
				if (FiredAt < 0.0)
				{
					FiredAt = Elapsed;
				}
			}
		}

		TestEqual(TEXT("A 2-second hold requests exactly one reset"), RequestCount, 1);
		TestTrue(TEXT("The reset fires at the hold threshold, within one frame"),
			FiredAt >= HoldSeconds && FiredAt <= HoldSeconds + 0.016 + KINDA_SMALL_NUMBER);

		// Still held after firing: progress pinned at 1, no further requests.
		TestNearlyEqual(TEXT("Hold progress is 1 once latched"), Processor.GetLastCommand().ResetHoldProgress, 1.0f, 0.0f);

		// Release and re-hold gives a second reset. Without this, a driver who went
		// off twice could only recover once.
		FVehicleInputRawSample Released;
		Processor.Tick(Released, 0.016, Elapsed + 0.1);

		int32 SecondCount = 0;
		for (int32 Index = 0; Index < 60; ++Index)
		{
			Elapsed += 0.016;
			if (Processor.Tick(Held, 0.016, Elapsed).bResetRequested)
			{
				++SecondCount;
			}
		}
		TestEqual(TEXT("Release and re-hold permits exactly one more reset"), SecondCount, 1);
	}

	// -- The hold threshold is wall time, not frame count --------------------
	//
	// A frame-counting implementation would let a 144 Hz player reset in a third of
	// the time a 30 Hz player needs, which is both unfair and a Gate B violation.
	{
		const double FrameRates[] = { 30.0, 60.0, 144.0 };

		for (const double Hz : FrameRates)
		{
			FVehicleInputProcessor Processor;
			MakeProcessor(Processor);

			FVehicleInputRawSample Held;
			Held.bResetHeld = true;

			const double Delta = 1.0 / Hz;
			double Elapsed = 0.0;
			double FiredAt = -1.0;

			for (int32 Index = 0; Index < FMath::CeilToInt32(2.0 * Hz); ++Index)
			{
				Elapsed += Delta;
				if (Processor.Tick(Held, Delta, Elapsed).bResetRequested)
				{
					FiredAt = Elapsed;
					break;
				}
			}

			// "Within one frame of the threshold", in both directions.
			//
			// A one-sided `FiredAt >= HoldSeconds` was the first version and it failed
			// against correct code: 15 accumulations of 1/30 land on
			// 0.49999999999999994, which is below 0.5 by one ulp. The claim being
			// tested is a wall-clock one, and a half-ulp is not a wall-clock
			// difference; the extra 1e-3 absorbs the accumulation without weakening
			// the test, because a frame-COUNTING implementation would fire at 15
			// frames regardless of rate -- 0.104 s at 144 Hz, nowhere near the bound.
			TestTrue(
				*FString::Printf(TEXT("At %.0f Hz the reset fires within one frame of %.2f s of wall time"), Hz, HoldSeconds),
				FiredAt >= HoldSeconds - 1.0e-3 && FiredAt <= HoldSeconds + Delta + 1.0e-3);
		}
	}

	// -- A zero hold threshold cannot disarm the guard -----------------------
	//
	// Validate() clamps ResetHoldSeconds to [0.05, 10], but a profile built in code
	// bypasses validation entirely, and a threshold of 0 would fire a reset on the
	// first frame the key is touched -- precisely the accident the hold exists to
	// prevent.
	{
		FVehicleInputProfile Profile = MakeNeutralProfile();
		Profile.ResetHoldSeconds = 0.0f;

		FVehicleInputProcessor Processor;
		Processor.Configure(Profile, ERacingInputDeviceType::Keyboard, ETransmissionInputMode::Automatic, 0.1f);

		FVehicleInputRawSample Held;
		Held.bResetHeld = true;

		TestTrue(TEXT("A zero hold threshold does not reset on the first frame"), !Processor.Tick(Held, 0.016, 1.0).bResetRequested);
	}

	// -- ResetState clears vehicle state but not button memory ---------------
	{
		FVehicleInputProcessor Processor;
		MakeProcessor(Processor);

		FVehicleInputProfile Profile = MakeNeutralProfile();
		Profile.ResetHoldSeconds = HoldSeconds;
		Profile.ThrottleRiseRate = 4.0f;
		Profile.SteerRate = 2.5f;
		Processor.Configure(Profile, ERacingInputDeviceType::Keyboard, ETransmissionInputMode::Automatic, 0.1f);

		FVehicleInputRawSample Driving;
		Driving.Throttle = 1.0;
		Driving.Steer = 1.0;
		Driving.bResetHeld = true;

		// Drive to full lock and full throttle, and complete the reset hold.
		const FVehicleInputCommand Before = StepFor(Processor, Driving, 60, 0.016);
		TestTrue(TEXT("Precondition: the car is at meaningful throttle before the reset"), Before.Throttle > 0.5f);
		TestTrue(TEXT("Precondition: the car is at meaningful steer before the reset"), Before.Steer > 0.5f);

		Processor.ResetState();

		// Smoothing state IS cleared -- a car that was at full lock and full throttle
		// when it went off must not resume that way on the grid. This is the input
		// layer's share of Gate B's "reset can never award progress".
		FVehicleInputRawSample StillHeld;
		StillHeld.bResetHeld = true;
		const FVehicleInputCommand After = Processor.Tick(StillHeld, 0.016, 100.0);

		TestNearlyEqual(TEXT("ResetState clears the throttle ramp"), After.Throttle, 0.0f, 1.0e-6f);
		TestNearlyEqual(TEXT("ResetState clears the steering ramp"), After.Steer, 0.0f, 1.0e-6f);

		// Button memory is NOT cleared. If it were, the still-held reset key would
		// re-arm and fire a SECOND reset one threshold later -- and holding the key
		// would reset repeatedly forever, invalidating every subsequent lap.
		int32 RepeatCount = 0;
		double Elapsed = 100.0;
		for (int32 Index = 0; Index < 125; ++Index)
		{
			Elapsed += 0.016;
			if (Processor.Tick(StillHeld, 0.016, Elapsed).bResetRequested)
			{
				++RepeatCount;
			}
		}
		TestEqual(TEXT("A reset key still held across ResetState does not fire again"), RepeatCount, 0);
	}

	return true;
}

// ---------------------------------------------------------------------------
// Reset hold, frozen across a stale-connection gap (VEH-005)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleInputResetStaleFreezeTest,
	"RacingSim.Vehicle.InputResetStaleFreeze",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleInputResetStaleFreezeTest::RunTest(const FString& Parameters)
{
	// Resolves the TRADE-OFF VEH-004 pass 2 named and deferred to this ticket
	// (VehicleInputProcessor.cpp's ProcessSample stale-handling block): a reset key
	// that is still genuinely held stays a latched demand and is NOT neutralised
	// during a stale gap, so ResetHeldSeconds must not complete a hold purely because
	// the connection died mid-press -- no consumer ever observed the hold in progress.
	// ResetHeldSeconds/bResetLatched must FREEZE for the whole gap, not accumulate and
	// not clear.

	constexpr float HoldSeconds = 0.5f;
	constexpr float StaleAfterSeconds = 0.1f;

	FVehicleInputProfile Profile = MakeNeutralProfile();
	Profile.ResetHoldSeconds = HoldSeconds;

	FVehicleInputProcessor Processor;
	Processor.Configure(Profile, ERacingInputDeviceType::Keyboard, ETransmissionInputMode::Automatic,
		/*MaxDeltaSeconds=*/1.0f, /*InputStaleAfterSeconds=*/StaleAfterSeconds);

	// -- Prime a partial hold on a fresh, correctly-stamped sample -----------
	FVehicleInputRawSample Held;
	Held.bResetHeld = true;
	Held.SampleTimestampSeconds = 1.0;

	// 0.3 s of 0.5 s needed -- short of the threshold, but a non-trivial amount of
	// progress to prove frozen later, rather than merely proving "stayed at zero".
	const FVehicleInputCommand Primed = Processor.Tick(Held, 0.3, 1.0);
	TestTrue(TEXT("Precondition: the primed hold is short of the threshold"), !Primed.bResetRequested);
	TestTrue(TEXT("Precondition: the primed sample is not stale"), !Primed.HasCorrection(EVehicleInputCorrection::StaleSample));
	TestTrue(TEXT("Precondition: some hold progress was made"), Primed.ResetHoldProgress > 0.0f);

	// -- Go stale: the device stamp stops advancing even though wall time does, while
	// the key is still (as far as the last real sample showed) held down ------------
	FVehicleInputRawSample Stale = Held;
	// Stale.SampleTimestampSeconds stays fixed at 1.0 -- the dead connection's last
	// known stamp -- for the whole block below.

	double Elapsed = 1.2; // already past the 0.1 s staleness threshold relative to the frozen stamp.
	FVehicleInputCommand LastStaleCommand = Processor.Tick(Stale, 0.2, Elapsed);

	for (int32 Index = 0; Index < 20; ++Index)
	{
		TestTrue(*FString::Printf(TEXT("Stale tick %d is reported as StaleSample"), Index),
			LastStaleCommand.HasCorrection(EVehicleInputCorrection::StaleSample));
		TestTrue(*FString::Printf(TEXT("Stale tick %d must not request a reset"), Index),
			!LastStaleCommand.bResetRequested);

		Elapsed += 0.05;
		LastStaleCommand = Processor.Tick(Stale, 0.05, Elapsed);
	}

	// Well over a full second of "held" wall time has now passed since priming --
	// easily enough to complete the remaining 0.2 s of hold if the accumulator were
	// not frozen. FROZEN means unchanged, not merely "did not fire": progress must sit
	// exactly where the primed tick left it.
	TestNearlyEqual(TEXT("Hold progress is frozen at its pre-stale value throughout the gap"),
		LastStaleCommand.ResetHoldProgress, Primed.ResetHoldProgress, 1.0e-6f);

	// -- Fresh samples resume: the hold must still need its REMAINING time, not fire
	// immediately from an accumulator that secretly kept advancing during the gap ----
	const double FreshStartElapsed = Elapsed;
	FVehicleInputRawSample Fresh = Held;

	int32 RequestCount = 0;
	double FiredAt = -1.0;
	for (int32 Index = 0; Index < 40; ++Index)
	{
		Elapsed += 0.016;
		Fresh.SampleTimestampSeconds = Elapsed; // connection resumed: stamp tracks wall time again.

		const FVehicleInputCommand Command = Processor.Tick(Fresh, 0.016, Elapsed);
		TestFalse(*FString::Printf(TEXT("Resumed tick %d is not reported as stale"), Index),
			Command.HasCorrection(EVehicleInputCorrection::StaleSample));

		if (Command.bResetRequested)
		{
			++RequestCount;
			if (FiredAt < 0.0)
			{
				FiredAt = Elapsed;
			}
		}
	}

	TestEqual(TEXT("Exactly one reset fires once fresh samples resume"), RequestCount, 1);

	// The remaining hold needed is HoldSeconds - 0.3 == 0.2 s. Asserting THIS bound,
	// not merely "eventually fired", is what catches a freeze that silently degraded
	// back into accumulate-through-stale: that bug would fire on the very first
	// resumed tick instead.
	const double AdditionalHoldNeeded = FiredAt - FreshStartElapsed;
	const double ExpectedRemainingHold = HoldSeconds - 0.3;
	TestTrue(
		*FString::Printf(TEXT("The reset needed its remaining ~%.2f s of hold after resuming, not zero (got %.3f s)"),
			ExpectedRemainingHold, AdditionalHoldNeeded),
		AdditionalHoldNeeded >= ExpectedRemainingHold - 0.05 && AdditionalHoldNeeded <= ExpectedRemainingHold + 0.05);

	return true;
}

// ---------------------------------------------------------------------------
// Gear requests, pedal conflict, device profiles
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimVehicleInputControlsTest,
	"RacingSim.Vehicle.InputControls",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimVehicleInputControlsTest::RunTest(const FString& Parameters)
{
	constexpr float Tol = 1.0e-5f;

	// -- Gear requests are edges, one-shot, and manual-only ------------------
	{
		FVehicleInputProcessor Processor;
		Processor.Configure(MakeNeutralProfile(), ERacingInputDeviceType::Gamepad, ETransmissionInputMode::Manual, 0.1f);

		FVehicleInputRawSample Raw;

		// Rising edge.
		Raw.bShiftUpHeld = true;
		TestTrue(TEXT("A shift-up press requests an upshift"), Processor.Tick(Raw, 0.016, 1.0).GearRequest == EVehicleGearRequest::ShiftUp);

		// Still held: no repeat. A held paddle must not run the gearbox to top gear.
		TestTrue(TEXT("A held shift-up does not repeat"), Processor.Tick(Raw, 0.016, 2.0).GearRequest == EVehicleGearRequest::None);
		TestTrue(TEXT("A held shift-up still does not repeat"), Processor.Tick(Raw, 0.016, 3.0).GearRequest == EVehicleGearRequest::None);

		// Release, then press again.
		Raw.bShiftUpHeld = false;
		TestTrue(TEXT("Releasing shift-up requests nothing"), Processor.Tick(Raw, 0.016, 4.0).GearRequest == EVehicleGearRequest::None);
		Raw.bShiftUpHeld = true;
		TestTrue(TEXT("A second shift-up press requests a second upshift"), Processor.Tick(Raw, 0.016, 5.0).GearRequest == EVehicleGearRequest::ShiftUp);

		// Downshift.
		Raw.bShiftUpHeld = false;
		Raw.bShiftDownHeld = true;
		TestTrue(TEXT("A shift-down press requests a downshift"), Processor.Tick(Raw, 0.016, 6.0).GearRequest == EVehicleGearRequest::ShiftDown);

		// Both at once on a fresh edge: up wins. Arbitrary, but deterministic and
		// documented, which is what a consumer needs. Emitting None would lose a
		// shift the driver definitely asked for.
		Raw.bShiftUpHeld = false;
		Raw.bShiftDownHeld = false;
		Processor.Tick(Raw, 0.016, 7.0);
		Raw.bShiftUpHeld = true;
		Raw.bShiftDownHeld = true;
		TestTrue(TEXT("Simultaneous shift edges resolve deterministically to an upshift"),
			Processor.Tick(Raw, 0.016, 8.0).GearRequest == EVehicleGearRequest::ShiftUp);
	}

	// -- An automatic transmission emits no gear requests and no clutch ------
	{
		FVehicleInputProcessor Processor;
		Processor.Configure(MakeNeutralProfile(), ERacingInputDeviceType::Gamepad, ETransmissionInputMode::Automatic, 0.1f);

		FVehicleInputRawSample Raw;
		Raw.bShiftUpHeld = true;
		Raw.Clutch = 1.0;

		const FVehicleInputCommand Command = Processor.Tick(Raw, 0.016, 1.0);

		TestTrue(TEXT("An automatic transmission ignores shift edges"), Command.GearRequest == EVehicleGearRequest::None);

		// Forced to 0 rather than passed through: a stale binding must not
		// half-disengage a drivetrain that has no clutch model.
		TestNearlyEqual(TEXT("An automatic transmission zeroes the clutch"), Command.Clutch, 0.0f, 0.0f);
	}

	// -- Switching to Manual with a shift key already held is not an edge ----
	{
		FVehicleInputProcessor Processor;
		Processor.Configure(MakeNeutralProfile(), ERacingInputDeviceType::Gamepad, ETransmissionInputMode::Automatic, 0.1f);

		FVehicleInputRawSample Raw;
		Raw.bShiftUpHeld = true;
		Processor.Tick(Raw, 0.016, 1.0);   // held, under Automatic

		FVehicleInputProfile Profile = MakeNeutralProfile();
		Processor.Configure(Profile, ERacingInputDeviceType::Gamepad, ETransmissionInputMode::Manual, 0.1f);

		// Configure() does not clear edge state, so the still-held key is not a new
		// edge. Otherwise switching transmission mode would shift the car.
		TestTrue(TEXT("Switching to Manual with a shift key held does not manufacture a shift"),
			Processor.Tick(Raw, 0.016, 2.0).GearRequest == EVehicleGearRequest::None);
	}

	// -- Pedal conflict policies ---------------------------------------------
	{
		struct FPolicyCase
		{
			EVehiclePedalConflictPolicy Policy;
			double RawThrottle;
			double RawBrake;
			float ExpectedThrottle;
			float ExpectedBrake;
			bool bExpectFlag;
			const TCHAR* Name;
		};

		const FPolicyCase Cases[] =
		{
			{ EVehiclePedalConflictPolicy::Independent,    1.0, 1.0, 1.0f, 1.0f, false, TEXT("Independent keeps both (left-foot braking)") },
			{ EVehiclePedalConflictPolicy::BrakeOverrides, 1.0, 1.0, 0.0f, 1.0f, true,  TEXT("BrakeOverrides zeroes throttle") },
			{ EVehiclePedalConflictPolicy::BrakeOverrides, 1.0, 0.1, 0.0f, 0.1f, true,  TEXT("BrakeOverrides zeroes throttle even for a light brush of brake") },
			{ EVehiclePedalConflictPolicy::LargerWins,     0.9, 0.2, 0.9f, 0.0f, true,  TEXT("LargerWins keeps the larger throttle") },
			{ EVehiclePedalConflictPolicy::LargerWins,     0.2, 0.9, 0.0f, 0.9f, true,  TEXT("LargerWins keeps the larger brake") },
			{ EVehiclePedalConflictPolicy::LargerWins,     0.5, 0.5, 0.0f, 0.5f, true,  TEXT("LargerWins breaks a tie toward the brake") }
		};

		for (const FPolicyCase& Case : Cases)
		{
			FVehicleInputProfile Profile = MakeNeutralProfile();
			Profile.PedalConflictPolicy = Case.Policy;

			FVehicleInputProcessor Processor;
			Processor.Configure(Profile, ERacingInputDeviceType::Gamepad, ETransmissionInputMode::Automatic, 0.1f);

			FVehicleInputRawSample Raw;
			Raw.Throttle = Case.RawThrottle;
			Raw.Brake = Case.RawBrake;

			const FVehicleInputCommand Command = Processor.Tick(Raw, 0.016, 1.0);

			TestNearlyEqual(*FString::Printf(TEXT("%s: throttle"), Case.Name), Command.Throttle, Case.ExpectedThrottle, Tol);
			TestNearlyEqual(*FString::Printf(TEXT("%s: brake"), Case.Name), Command.Brake, Case.ExpectedBrake, Tol);
			TestTrue(*FString::Printf(TEXT("%s: conflict flag"), Case.Name),
				Command.HasCorrection(EVehicleInputCorrection::PedalConflict) == Case.bExpectFlag);
		}

		// No conflict when only one pedal is applied -- the flag must not fire on
		// ordinary driving, or the telemetry signal is worthless.
		{
			FVehicleInputProfile Profile = MakeNeutralProfile();
			Profile.PedalConflictPolicy = EVehiclePedalConflictPolicy::BrakeOverrides;

			FVehicleInputProcessor Processor;
			Processor.Configure(Profile, ERacingInputDeviceType::Keyboard, ETransmissionInputMode::Automatic, 0.1f);

			FVehicleInputRawSample Raw;
			Raw.Throttle = 1.0;

			const FVehicleInputCommand Command = Processor.Tick(Raw, 0.016, 1.0);
			TestNearlyEqual(TEXT("Throttle alone survives BrakeOverrides"), Command.Throttle, 1.0f, Tol);
			TestTrue(TEXT("Throttle alone raises no conflict flag"), !Command.HasCorrection(EVehicleInputCorrection::PedalConflict));
		}
	}

	// -- The two shipped device profiles behave as their device demands ------
	//
	// Docs/00-ExecutivePlan.md names keyboard AND gamepad as shipping input methods,
	// and the whole reason FVehicleInputProfile is per-device is that one set of
	// numbers cannot serve both. This asserts that difference rather than trusting it.
	{
		const FVehicleInputProfile Keyboard = FVehicleInputProfile::MakeKeyboardDefault();
		const FVehicleInputProfile Gamepad = FVehicleInputProfile::MakeGamepadDefault();

		// Both defaults must be valid by their own rules, or every consumer starts
		// from a config that Validate() would reject.
		TestTrue(TEXT("The keyboard default profile validates clean"), Keyboard.ValidateReadOnly(TEXT("Keyboard")).IsClean());
		TestTrue(TEXT("The gamepad default profile validates clean"), Gamepad.ValidateReadOnly(TEXT("Gamepad")).IsClean());

		// The defining difference: a key must be ramped, a trigger must not.
		TestTrue(TEXT("The keyboard profile rate-limits the throttle"), Keyboard.ThrottleRiseRate > 0.0f);
		TestTrue(TEXT("The keyboard profile rate-limits the steering"), Keyboard.SteerRate > 0.0f);
		TestTrue(TEXT("The gamepad profile does not rate-limit the throttle"), Gamepad.ThrottleRiseRate == 0.0f);
		TestTrue(TEXT("The gamepad profile does not rate-limit the steering"), Gamepad.SteerRate == 0.0f);

		// Release faster than apply, on both pedals. A driver lifting to catch a
		// slide cannot wait for a symmetric ramp.
		TestTrue(TEXT("Keyboard throttle releases faster than it applies"), Keyboard.ThrottleFallRate > Keyboard.ThrottleRiseRate);
		TestTrue(TEXT("Keyboard brake releases faster than it applies"), Keyboard.BrakeFallRate > Keyboard.BrakeRiseRate);

		// Self-centring is the half that saves a car.
		TestTrue(TEXT("Keyboard steering centres faster than it deflects"), Keyboard.SteerCentringRate > Keyboard.SteerRate);

		// A key has nothing for a dead zone to do; a stick does.
		TestTrue(TEXT("The keyboard profile has no steering dead zone"), Keyboard.SteerDeadZone == 0.0f);
		TestTrue(TEXT("The gamepad profile has a steering dead zone"), Gamepad.SteerDeadZone > 0.0f);

		// Behavioural confirmation of the same claim, at the processor: the keyboard
		// cannot reach full throttle in one frame and the gamepad can.
		{
			FVehicleInputProcessor KeyboardProcessor;
			KeyboardProcessor.Configure(Keyboard, ERacingInputDeviceType::Keyboard, ETransmissionInputMode::Automatic, 0.1f);

			FVehicleInputProcessor GamepadProcessor;
			GamepadProcessor.Configure(Gamepad, ERacingInputDeviceType::Gamepad, ETransmissionInputMode::Automatic, 0.1f);

			FVehicleInputRawSample Pinned;
			Pinned.Throttle = 1.0;

			TestTrue(TEXT("The keyboard ramps rather than snapping to full throttle"),
				KeyboardProcessor.Tick(Pinned, 0.016, 1.0).Throttle < 1.0f);
			TestNearlyEqual(TEXT("The gamepad trigger reaches full throttle immediately"),
				GamepadProcessor.Tick(Pinned, 0.016, 1.0).Throttle, 1.0f, Tol);
		}

		// The device is recorded on every command, so a result's
		// FRacingSimVersionStamp::InputDeviceType and the profile that shaped the lap
		// are the same value by construction. A keyboard lap and a wheel lap are not
		// the same athletic event (Core/RacingSimTypes.h).
		{
			FVehicleInputProcessor Processor;
			Processor.Configure(Keyboard, ERacingInputDeviceType::Keyboard, ETransmissionInputMode::Automatic, 0.1f);

			const FVehicleInputRawSample Raw;
			TestTrue(TEXT("The command records the device that produced it"),
				Processor.Tick(Raw, 0.016, 1.0).DeviceType == ERacingInputDeviceType::Keyboard);
			TestTrue(TEXT("The processor reports its device"), Processor.GetDeviceType() == ERacingInputDeviceType::Keyboard);
		}
	}

	// -- The handbrake is never rate-limited ---------------------------------
	//
	// A handbrake is a lever yanked in one motion. Ramping it would put lag between
	// the driver and one of the two controls used to recover a car that is already
	// sideways.
	{
		FVehicleInputProfile Profile = FVehicleInputProfile::MakeKeyboardDefault();

		FVehicleInputProcessor Processor;
		Processor.Configure(Profile, ERacingInputDeviceType::Keyboard, ETransmissionInputMode::Automatic, 0.1f);

		FVehicleInputRawSample Raw;
		Raw.Handbrake = 1.0;

		TestNearlyEqual(TEXT("The handbrake applies fully in one frame even on a rate-limited profile"),
			Processor.Tick(Raw, 0.016, 1.0).Handbrake, 1.0f, Tol);
	}

	// -- GetLastCommand mirrors the returned command -------------------------
	{
		FVehicleInputProcessor Processor;
		Processor.Configure(MakeNeutralProfile(), ERacingInputDeviceType::Gamepad, ETransmissionInputMode::Automatic, 0.1f);

		TestTrue(TEXT("Before any Tick, the last command is the safe default"), Processor.GetLastCommand().IsFiniteAndInRange());
		TestNearlyEqual(TEXT("Before any Tick, there is no throttle"), Processor.GetLastCommand().Throttle, 0.0f, 0.0f);

		FVehicleInputRawSample Raw;
		Raw.Throttle = 0.75;
		const FVehicleInputCommand Returned = Processor.Tick(Raw, 0.016, 1.0);

		TestNearlyEqual(TEXT("GetLastCommand matches the returned command"), Processor.GetLastCommand().Throttle, Returned.Throttle, 0.0f);
	}

	return true;
}
