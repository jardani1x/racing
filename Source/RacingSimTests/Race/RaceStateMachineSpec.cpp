// Copyright RacingSim. All Rights Reserved.

#include "Race/RaceStateMachine.h"
#include "Race/RaceRulesetDataAsset.h"
#include "Core/RacingSimTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/**
 * RACE-001: URaceStateMachine.
 *
 * Gate B, verbatim: "countdown, start, finish, results, and restart transitions are
 * deterministic and idempotent" and "reset cannot award progress".
 *
 * Structure, and why it is in this order:
 *
 *   RacingSim.Race.TransitionGraph      -- the authored graph, exhaustively, with
 *                                          no instance. 25 (state, transition) pairs
 *                                          checked, not a sample of them.
 *   RacingSim.Race.StateMachineSemantics-- the same 25 pairs against a live object,
 *                                          asserting result, resulting state, and
 *                                          observer notification count.
 *   RacingSim.Race.Idempotency          -- every transition called twice from every
 *                                          state.
 *   RacingSim.Race.ClockUnderStates     -- the clock's behaviour across the
 *                                          lifecycle, on a fake time source that
 *                                          simulates dropped and hitched frames.
 *   RacingSim.Race.RestartAwardsNoProgress
 *   RacingSim.Race.Countdown
 *   RacingSim.Race.Reentrancy
 *   RacingSim.Race.Ruleset
 *
 * The exhaustive matrices matter more than they look. A hand-picked list of illegal
 * transitions tests the ones the author thought of; the requirement is that NO
 * transition outside the authored graph is accepted, and only enumeration can say
 * that. It also means adding a state to ERaceState without adding its rows here
 * fails the enum-count assertion rather than silently going untested.
 *
 * SmokeFilter is mandatory -- Docs/Environment.md records that the project's only
 * automation command is `Automation RunFilter Smoke`, and that a test outside it
 * once sat green and unexecuted.
 */

namespace RaceStateMachineSpecPrivate
{
	// Named uniquely rather than sitting in an anonymous namespace: unity builds
	// concatenate translation units, and two anonymous `GNow` in one blob is a
	// redefinition, not two file-local variables.
	double GRaceSpecNowSeconds = 0.0;

	/** Captureless, so it converts to the raw FRaceTimeSourceFn function pointer. */
	double RaceSpecTimeSource()
	{
		return GRaceSpecNowSeconds;
	}

	struct FRaceSpecStateChange
	{
		ERaceState OldState = ERaceState::PreRace;
		ERaceState NewState = ERaceState::PreRace;
		int32 SessionId = 0;
	};

	FString StateName(ERaceState State)
	{
		const UEnum* EnumType = StaticEnum<ERaceState>();
		const FString Name = EnumType ? EnumType->GetNameStringByValue(static_cast<int64>(State)) : FString();
		return Name.IsEmpty() ? FString::Printf(TEXT("<%d>"), static_cast<int32>(State)) : Name;
	}

	FString TransitionName(ERaceTransition Transition)
	{
		const UEnum* EnumType = StaticEnum<ERaceTransition>();
		const FString Name = EnumType ? EnumType->GetNameStringByValue(static_cast<int64>(Transition)) : FString();
		return Name.IsEmpty() ? FString::Printf(TEXT("<%d>"), static_cast<int32>(Transition)) : Name;
	}

	FString ResultName(ERaceTransitionResult Result)
	{
		const UEnum* EnumType = StaticEnum<ERaceTransitionResult>();
		const FString Name = EnumType ? EnumType->GetNameStringByValue(static_cast<int64>(Result)) : FString();
		return Name.IsEmpty() ? FString::Printf(TEXT("<%d>"), static_cast<int32>(Result)) : Name;
	}

	/**
	 * The expected instance-level truth table, written out longhand.
	 *
	 * Deliberately NOT derived from URaceStateMachine::GetTransitionTarget: a test
	 * that computes its expectation from the code under test asserts only that the
	 * code agrees with itself. These 25 entries are the specification, restated
	 * independently, and they are what a reviewer should read to check the design.
	 */
	ERaceTransitionResult ExpectedResult(ERaceState From, ERaceTransition Transition)
	{
		// Redundant wherever the transition's post-condition already holds.
		switch (Transition)
		{
		case ERaceTransition::BeginCountdown: if (From == ERaceState::Countdown) return ERaceTransitionResult::Redundant; break;
		case ERaceTransition::StartRace:      if (From == ERaceState::Racing)    return ERaceTransitionResult::Redundant; break;
		case ERaceTransition::FinishRace:     if (From == ERaceState::Finished)  return ERaceTransitionResult::Redundant; break;
		case ERaceTransition::ShowResults:    if (From == ERaceState::Results)   return ERaceTransitionResult::Redundant; break;
		case ERaceTransition::Restart:        if (From == ERaceState::PreRace)   return ERaceTransitionResult::Redundant; break;
		}

		// Restart is legal from everywhere else.
		if (Transition == ERaceTransition::Restart)
		{
			return ERaceTransitionResult::Applied;
		}

		// The four forward edges.
		const bool bForwardEdge =
			   (From == ERaceState::PreRace   && Transition == ERaceTransition::BeginCountdown)
			|| (From == ERaceState::Countdown && Transition == ERaceTransition::StartRace)
			|| (From == ERaceState::Racing    && Transition == ERaceTransition::FinishRace)
			|| (From == ERaceState::Finished  && Transition == ERaceTransition::ShowResults);

		return bForwardEdge ? ERaceTransitionResult::Applied : ERaceTransitionResult::RejectedIllegal;
	}

	/** Where the machine ends up after ExpectedResult() says Applied. */
	ERaceState ExpectedStateAfter(ERaceState From, ERaceTransition Transition)
	{
		if (ExpectedResult(From, Transition) != ERaceTransitionResult::Applied)
		{
			return From;
		}

		switch (Transition)
		{
		case ERaceTransition::BeginCountdown: return ERaceState::Countdown;
		case ERaceTransition::StartRace:      return ERaceState::Racing;
		case ERaceTransition::FinishRace:     return ERaceState::Finished;
		case ERaceTransition::ShowResults:    return ERaceState::Results;
		case ERaceTransition::Restart:        return ERaceState::PreRace;
		}

		return From;
	}

	/** Walk a fresh machine forward to Target. Returns false if it did not arrive. */
	bool DriveToState(URaceStateMachine* Machine, ERaceState Target)
	{
		if (Machine->GetRaceState() != ERaceState::PreRace)
		{
			Machine->Restart();
		}

		if (Target == ERaceState::PreRace) { return Machine->GetRaceState() == Target; }
		Machine->BeginCountdown();
		if (Target == ERaceState::Countdown) { return Machine->GetRaceState() == Target; }
		Machine->StartRace();
		if (Target == ERaceState::Racing) { return Machine->GetRaceState() == Target; }
		Machine->FinishRace();
		if (Target == ERaceState::Finished) { return Machine->GetRaceState() == Target; }
		Machine->ShowResults();
		return Machine->GetRaceState() == Target;
	}

	/**
	 * Illegal transitions log a Warning by design. Warnings do not fail an
	 * automation test, but they clutter the report and an unexplained warning is
	 * indistinguishable from a real one -- so the tests that provoke them declare
	 * them. Occurrences < 0 means "ignore however many arrive", which is right here
	 * because the assertion of record is the returned ERaceTransitionResult, not the
	 * log line.
	 */
	void SuppressExpectedRejectionLogs(FAutomationTestBase& Test)
	{
		Test.AddExpectedMessage(TEXT("Illegal race transition"), ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/-1, /*IsRegex=*/false);
		Test.AddExpectedMessage(TEXT("is not a declared ERaceTransition"), ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/-1, /*IsRegex=*/false);
		Test.AddExpectedMessage(TEXT("Re-entrant race transition"), ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/-1, /*IsRegex=*/false);
	}
}

// ===========================================================================
// 1. The authored graph, with no instance
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceTransitionGraphTest,
	"RacingSim.Race.TransitionGraph",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceTransitionGraphTest::RunTest(const FString& Parameters)
{
	using namespace RaceStateMachineSpecPrivate;

	const TConstArrayView<ERaceState> States = URaceStateMachine::AllStates();
	const TConstArrayView<ERaceTransition> Transitions = URaceStateMachine::AllTransitions();

	// -- The tables and the enums must not drift -----------------------------
	//
	// UEnum::NumEnums() counts the compiler-generated _MAX sentinel, hence the -1.
	// This is the assertion that turns "someone added a state and forgot to test it"
	// into a red build rather than into silent coverage loss.
	{
		const UEnum* StateEnum = StaticEnum<ERaceState>();
		if (TestNotNull(TEXT("ERaceState is reflected"), StateEnum))
		{
			TestEqual(TEXT("AllStates() covers every declared ERaceState"),
				States.Num(), StateEnum->NumEnums() - 1);
		}

		const UEnum* TransitionEnum = StaticEnum<ERaceTransition>();
		if (TestNotNull(TEXT("ERaceTransition is reflected"), TransitionEnum))
		{
			TestEqual(TEXT("AllTransitions() covers every declared ERaceTransition"),
				Transitions.Num(), TransitionEnum->NumEnums() - 1);
		}
	}

	// PreRace must be enumerator 0 -- a default-constructed ERaceState is relied on
	// to be the boot state, not a sentinel. See the enum's comment in Core.
	TestEqual(TEXT("PreRace is enumerator 0"), static_cast<int32>(ERaceState::PreRace), 0);

	// -- Exhaustive 5 x 5 edge matrix ----------------------------------------
	int32 LegalEdgeCount = 0;
	for (const ERaceState From : States)
	{
		for (const ERaceTransition Transition : Transitions)
		{
			ERaceState To = ERaceState::PreRace;
			const bool bIsEdge = URaceStateMachine::GetTransitionTarget(From, Transition, To);

			// The graph's own rule, restated independently: the four forward edges,
			// plus Restart from anywhere.
			const bool bExpectedEdge =
				   (Transition == ERaceTransition::Restart)
				|| (From == ERaceState::PreRace   && Transition == ERaceTransition::BeginCountdown)
				|| (From == ERaceState::Countdown && Transition == ERaceTransition::StartRace)
				|| (From == ERaceState::Racing    && Transition == ERaceTransition::FinishRace)
				|| (From == ERaceState::Finished  && Transition == ERaceTransition::ShowResults);

			TestEqual(
				*FString::Printf(TEXT("Edge (%s, %s) legality"), *StateName(From), *TransitionName(Transition)),
				bIsEdge, bExpectedEdge);

			if (bIsEdge)
			{
				++LegalEdgeCount;

				ERaceState Destination = ERaceState::PreRace;
				TestTrue(
					*FString::Printf(TEXT("Transition %s declares a destination"), *TransitionName(Transition)),
					URaceStateMachine::GetTransitionDestination(Transition, Destination));

				// The edge table and the destination table must agree, or Redundant
				// and Applied would disagree about where a transition goes.
				TestEqual(
					*FString::Printf(TEXT("Edge (%s, %s) lands on the transition's declared destination"),
						*StateName(From), *TransitionName(Transition)),
					*StateName(To), *StateName(Destination));
			}
		}
	}

	// 4 forward edges + 5 Restart edges (one from each state, including PreRace).
	TestEqual(TEXT("The graph has exactly 9 edges"), LegalEdgeCount, 9);

	// -- Out-of-range values are not edges -----------------------------------
	//
	// A cast from an integer -- a corrupted save, a Blueprint holding a stale value,
	// a replicated byte -- must be rejected by the guard rather than indexing a
	// table. The requirement is "never a crash", so this is checked at the pure
	// function before it is checked on a live object.
	{
		ERaceState Unused = ERaceState::PreRace;
		TestFalse(TEXT("An out-of-range source state has no edges"),
			URaceStateMachine::GetTransitionTarget(static_cast<ERaceState>(200), ERaceTransition::BeginCountdown, Unused));
		TestFalse(TEXT("An out-of-range transition has no edges"),
			URaceStateMachine::GetTransitionTarget(ERaceState::PreRace, static_cast<ERaceTransition>(200), Unused));
		TestFalse(TEXT("An out-of-range transition has no destination"),
			URaceStateMachine::GetTransitionDestination(static_cast<ERaceTransition>(200), Unused));
	}

	// -- Reachability, both directions ---------------------------------------
	//
	// Two properties that the edge matrix above does not imply: every state can be
	// reached from PreRace (no state is dead content), and PreRace can be reached
	// from every state (no state is a trap the player cannot restart out of).
	{
		TSet<ERaceState> Reached;
		TArray<ERaceState> Frontier;
		Reached.Add(ERaceState::PreRace);
		Frontier.Add(ERaceState::PreRace);

		while (Frontier.Num() > 0)
		{
			const ERaceState Current = Frontier.Pop();
			for (const ERaceTransition Transition : Transitions)
			{
				ERaceState To = ERaceState::PreRace;
				if (URaceStateMachine::GetTransitionTarget(Current, Transition, To) && !Reached.Contains(To))
				{
					Reached.Add(To);
					Frontier.Add(To);
				}
			}
		}

		TestEqual(TEXT("Every state is reachable from PreRace"), Reached.Num(), States.Num());

		for (const ERaceState From : States)
		{
			ERaceState To = ERaceState::PreRace;
			TestTrue(
				*FString::Printf(TEXT("PreRace is reachable in one Restart from %s"), *StateName(From)),
				URaceStateMachine::GetTransitionTarget(From, ERaceTransition::Restart, To) && To == ERaceState::PreRace);
		}
	}

	return true;
}

// ===========================================================================
// 2. Instance semantics: the same 25 pairs against a live object
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceStateMachineSemanticsTest,
	"RacingSim.Race.StateMachineSemantics",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceStateMachineSemanticsTest::RunTest(const FString& Parameters)
{
	using namespace RaceStateMachineSpecPrivate;

	SuppressExpectedRejectionLogs(*this);
	GRaceSpecNowSeconds = 1000.0;

	// -- Initial conditions ---------------------------------------------------
	{
		const TStrongObjectPtr<URaceStateMachine> Machine(
			URaceStateMachine::CreateWithTimeSource(GetTransientPackage(), nullptr, &RaceSpecTimeSource));

		if (!TestNotNull(TEXT("A state machine can be created with no world"), Machine.Get()))
		{
			return false;
		}

		TestEqual(TEXT("A new machine boots in PreRace"), Machine->GetRaceState(), ERaceState::PreRace);
		TestEqual(TEXT("A new machine is session 0"), Machine->GetSessionId(), 0);
		TestEqual(TEXT("A new machine has no elapsed race time"), Machine->GetRaceElapsedSeconds(), 0.0, 0.0);
		TestFalse(TEXT("A new machine's race clock is not running"), Machine->IsRaceClockRunning());
		TestFalse(TEXT("A machine with no ruleset has no automatic countdown"), Machine->HasAutomaticCountdown());
		TestNull(TEXT("A machine created with no ruleset reports none"), Machine->GetRuleset());
	}

	// -- Create() refuses a null owner ---------------------------------------
	{
		AddExpectedError(TEXT("URaceStateMachine::Create refused: no owner"),
			EAutomationExpectedErrorFlags::Contains, 1, /*IsRegex=*/false);
		TestNull(TEXT("Create refuses a null owner rather than leaking an unrooted object"),
			URaceStateMachine::Create(nullptr, nullptr));

		AddExpectedError(TEXT("URaceStateMachine::Create refused: null time source"),
			EAutomationExpectedErrorFlags::Contains, 1, /*IsRegex=*/false);
		TestNull(TEXT("Create refuses a null time source"),
			URaceStateMachine::CreateWithTimeSource(GetTransientPackage(), nullptr, nullptr));
	}

	// -- Exhaustive 5 x 5 instance matrix ------------------------------------
	//
	// A FRESH machine per pair. Reusing one would make each result depend on the
	// history of the previous 24 attempts, and "deterministic" would be untested.
	for (const ERaceState From : URaceStateMachine::AllStates())
	{
		for (const ERaceTransition Transition : URaceStateMachine::AllTransitions())
		{
			const TStrongObjectPtr<URaceStateMachine> Machine(
				URaceStateMachine::CreateWithTimeSource(GetTransientPackage(), nullptr, &RaceSpecTimeSource));

			if (!TestTrue(*FString::Printf(TEXT("Drove machine to %s"), *StateName(From)),
				DriveToState(Machine.Get(), From)))
			{
				continue;
			}

			// Bind only after arriving, so the count below is about this one call.
			int32 BroadcastCount = 0;
			FRaceSpecStateChange LastChange;
			Machine->OnRaceStateChanged.AddLambda(
				[&BroadcastCount, &LastChange](ERaceState Old, ERaceState New, int32 SessionId)
				{
					++BroadcastCount;
					LastChange = { Old, New, SessionId };
				});

			const int32 SessionBefore = Machine->GetSessionId();
			const ERaceTransitionResult Result = Machine->RequestTransition(Transition);
			const ERaceTransitionResult Expected = ExpectedResult(From, Transition);

			TestEqual(
				*FString::Printf(TEXT("(%s, %s) result is %s"), *StateName(From), *TransitionName(Transition), *ResultName(Expected)),
				*ResultName(Result), *ResultName(Expected));

			TestEqual(
				*FString::Printf(TEXT("(%s, %s) leaves the machine in %s"),
					*StateName(From), *TransitionName(Transition), *StateName(ExpectedStateAfter(From, Transition))),
				*StateName(Machine->GetRaceState()), *StateName(ExpectedStateAfter(From, Transition)));

			// Exactly one notification per applied transition, and none at all for a
			// redundant or rejected one. This is the guarantee a lap counter, a
			// results screen or a telemetry writer depends on.
			const int32 ExpectedBroadcasts = (Expected == ERaceTransitionResult::Applied) ? 1 : 0;
			TestEqual(
				*FString::Printf(TEXT("(%s, %s) broadcasts %d time(s)"),
					*StateName(From), *TransitionName(Transition), ExpectedBroadcasts),
				BroadcastCount, ExpectedBroadcasts);

			if (Expected == ERaceTransitionResult::Applied)
			{
				TestEqual(TEXT("Broadcast reports the state it came from"), *StateName(LastChange.OldState), *StateName(From));
				TestEqual(TEXT("Broadcast reports the state it arrived in"),
					*StateName(LastChange.NewState), *StateName(ExpectedStateAfter(From, Transition)));
				TestEqual(TEXT("Broadcast reports the machine's current session id"),
					LastChange.SessionId, Machine->GetSessionId());
			}

			// Only Restart may change the session id, and only when applied.
			const int32 ExpectedSession = (Expected == ERaceTransitionResult::Applied && Transition == ERaceTransition::Restart)
				? SessionBefore + 1 : SessionBefore;
			TestEqual(
				*FString::Printf(TEXT("(%s, %s) session id"), *StateName(From), *TransitionName(Transition)),
				Machine->GetSessionId(), ExpectedSession);
		}
	}

	// -- An out-of-range transition value is rejected, not crashed -----------
	{
		const TStrongObjectPtr<URaceStateMachine> Machine(
			URaceStateMachine::CreateWithTimeSource(GetTransientPackage(), nullptr, &RaceSpecTimeSource));

		int32 BroadcastCount = 0;
		Machine->OnRaceStateChanged.AddLambda([&BroadcastCount](ERaceState, ERaceState, int32) { ++BroadcastCount; });

		for (const int32 BadValue : { 5, 42, 200, 255 })
		{
			TestEqual(
				*FString::Printf(TEXT("Transition value %d is rejected as out of range"), BadValue),
				*ResultName(Machine->RequestTransition(static_cast<ERaceTransition>(BadValue))),
				*ResultName(ERaceTransitionResult::RejectedOutOfRange));
		}

		TestEqual(TEXT("Out-of-range transitions leave the machine in PreRace"),
			*StateName(Machine->GetRaceState()), *StateName(ERaceState::PreRace));
		TestEqual(TEXT("Out-of-range transitions notify nobody"), BroadcastCount, 0);
	}

	return true;
}

// ===========================================================================
// 3. Idempotency: every transition, twice, from every state
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceStateMachineIdempotencyTest,
	"RacingSim.Race.Idempotency",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceStateMachineIdempotencyTest::RunTest(const FString& Parameters)
{
	using namespace RaceStateMachineSpecPrivate;

	SuppressExpectedRejectionLogs(*this);
	GRaceSpecNowSeconds = 5000.0;

	for (const ERaceState From : URaceStateMachine::AllStates())
	{
		for (const ERaceTransition Transition : URaceStateMachine::AllTransitions())
		{
			const TStrongObjectPtr<URaceStateMachine> Machine(
				URaceStateMachine::CreateWithTimeSource(GetTransientPackage(), nullptr, &RaceSpecTimeSource));

			if (!DriveToState(Machine.Get(), From))
			{
				AddError(FString::Printf(TEXT("Could not drive machine to %s"), *StateName(From)));
				continue;
			}

			int32 BroadcastCount = 0;
			Machine->OnRaceStateChanged.AddLambda([&BroadcastCount](ERaceState, ERaceState, int32) { ++BroadcastCount; });

			Machine->RequestTransition(Transition);

			const ERaceState StateAfterFirst = Machine->GetRaceState();
			const int32 SessionAfterFirst = Machine->GetSessionId();
			const int32 BroadcastsAfterFirst = BroadcastCount;
			const double ElapsedAfterFirst = Machine->GetRaceElapsedSeconds();

			// Time moves between the two calls. That is the point: if the second
			// call re-ran an entry action, the clock would be restarted or refrozen
			// at a different instant and the difference would show up here. A test
			// that called twice at the same timestamp could not tell the difference.
			GRaceSpecNowSeconds += 3.0;

			const ERaceTransitionResult SecondResult = Machine->RequestTransition(Transition);

			TestEqual(
				*FString::Printf(TEXT("Second %s from %s does not change the state"), *TransitionName(Transition), *StateName(From)),
				*StateName(Machine->GetRaceState()), *StateName(StateAfterFirst));

			TestEqual(
				*FString::Printf(TEXT("Second %s from %s does not notify again"), *TransitionName(Transition), *StateName(From)),
				BroadcastCount, BroadcastsAfterFirst);

			TestEqual(
				*FString::Printf(TEXT("Second %s from %s does not bump the session id"), *TransitionName(Transition), *StateName(From)),
				Machine->GetSessionId(), SessionAfterFirst);

			// The second call must never be reported as Applied -- that is the
			// machine-readable form of "no additional effect".
			TestNotEqual(
				*FString::Printf(TEXT("Second %s from %s is not Applied"), *TransitionName(Transition), *StateName(From)),
				*ResultName(SecondResult), *ResultName(ERaceTransitionResult::Applied));

			// Clock effects, which is where a non-idempotent entry action would
			// really hurt. Racing is excluded because its clock is legitimately
			// still running and 3 seconds have passed.
			if (StateAfterFirst != ERaceState::Racing)
			{
				TestEqual(
					*FString::Printf(TEXT("Second %s from %s does not move the race clock"), *TransitionName(Transition), *StateName(From)),
					Machine->GetRaceElapsedSeconds(), ElapsedAfterFirst, 0.0);
			}
		}
	}

	// -- The specific case Gate B names: a doubled finish ---------------------
	{
		GRaceSpecNowSeconds = 8000.0;
		const TStrongObjectPtr<URaceStateMachine> Machine(
			URaceStateMachine::CreateWithTimeSource(GetTransientPackage(), nullptr, &RaceSpecTimeSource));

		Machine->BeginCountdown();
		Machine->StartRace();
		GRaceSpecNowSeconds += 61.5;
		TestEqual(TEXT("Finish applies once"), *ResultName(Machine->FinishRace()), *ResultName(ERaceTransitionResult::Applied));
		const double FrozenTime = Machine->GetRaceElapsedSeconds();
		TestEqual(TEXT("The frozen result is the real duration"), FrozenTime, 61.5, 1e-9);

		// Ten more seconds pass and the finish is signalled four more times -- a
		// double overlap, a retriggered delegate, a UI button. The result must not
		// move by a millisecond.
		GRaceSpecNowSeconds += 10.0;
		for (int32 Attempt = 0; Attempt < 4; ++Attempt)
		{
			TestEqual(TEXT("Repeat finish is redundant"), *ResultName(Machine->FinishRace()), *ResultName(ERaceTransitionResult::Redundant));
		}
		TestEqual(TEXT("Repeat finishes cannot move the frozen result"), Machine->GetRaceElapsedSeconds(), FrozenTime, 0.0);

		// And the same across the move to Results, which is where a naive
		// implementation re-stamps the time for the results screen.
		Machine->ShowResults();
		GRaceSpecNowSeconds += 120.0;
		TestEqual(TEXT("Entering Results cannot move the frozen result"), Machine->GetRaceElapsedSeconds(), FrozenTime, 0.0);
	}

	return true;
}

// ===========================================================================
// 4. The clock across the lifecycle, under variable and dropped frames
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceClockUnderStatesTest,
	"RacingSim.Race.ClockUnderStates",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceClockUnderStatesTest::RunTest(const FString& Parameters)
{
	using namespace RaceStateMachineSpecPrivate;

	// Two runs of an identical 30-second race, differing ONLY in how often the game
	// sampled the clock. Gate B's "independent of render frame rate" is the claim
	// that these two produce the same number.
	auto RunRace = [](const TArray<double>& FrameDeltas) -> double
	{
		GRaceSpecNowSeconds = 20000.0;

		const TStrongObjectPtr<URaceStateMachine> Machine(
			URaceStateMachine::CreateWithTimeSource(GetTransientPackage(), nullptr, &RaceSpecTimeSource));

		Machine->BeginCountdown();
		Machine->StartRace();

		const double RaceStartNow = GRaceSpecNowSeconds;
		for (const double Delta : FrameDeltas)
		{
			GRaceSpecNowSeconds += Delta;
			Machine->PollAutoTransitions();
			Machine->GetRaceElapsedSeconds();   // the HUD read, once per frame
		}

		// Finish at an identical wall-clock instant in both runs, whatever the
		// frames did in between.
		GRaceSpecNowSeconds = RaceStartNow + 30.0;
		Machine->FinishRace();
		return Machine->GetRaceElapsedSeconds();
	};

	// 128 fps, perfectly even, 3000 frames.
	//
	// 1/128 rather than 1/144 so every frame boundary is exactly representable as a
	// double. That is a property of the TEST's fake clock, not of the code under
	// test: with an inexact step the fake "now" would drift a picosecond past the
	// finish instant, the monotonic ratchet would correctly latch that larger value,
	// and the zero-tolerance comparison below would fail for a reason that has
	// nothing to do with the state machine. Making the fixture exact keeps the
	// assertion about the thing being tested.
	TArray<double> SmoothFrames;
	SmoothFrames.Init(1.0 / 128.0, 3000);

	// ~30 fps average, violently uneven, 106 frames: a 2-second hitch, a 4.5-second
	// stall, and several zero-length frames (two samples inside one tick).
	TArray<double> JankyFrames;
	JankyFrames.Add(0.03125);
	JankyFrames.Add(0.0);
	JankyFrames.Add(0.0);
	JankyFrames.Add(2.0);
	JankyFrames.Add(0.0078125);
	JankyFrames.Add(4.5);
	for (int32 Index = 0; Index < 100; ++Index)
	{
		JankyFrames.Add(0.03125);
	}

	const double SmoothResult = RunRace(SmoothFrames);
	const double JankyResult = RunRace(JankyFrames);

	TestEqual(TEXT("A 30-second race sampled 3000 times measures 30 seconds"), SmoothResult, 30.0, 0.0);
	TestEqual(TEXT("A 30-second race sampled 106 times with hitches measures 30 seconds"), JankyResult, 30.0, 0.0);

	// Zero tolerance. An accumulator would differ between these two runs by roughly
	// the frame count times the per-frame rounding error -- small, but not zero, and
	// the requirement is not "small". It would also differ by the whole 6.5 seconds
	// the engine's DeltaTime clamp would have eaten from the two stalls.
	TestEqual(TEXT("Frame rate, frame count and hitches do not change the lap time"),
		SmoothResult, JankyResult, 0.0);

	// -- Never decreasing, sampled continuously through the lifecycle --------
	{
		GRaceSpecNowSeconds = 30000.0;
		const TStrongObjectPtr<URaceStateMachine> Machine(
			URaceStateMachine::CreateWithTimeSource(GetTransientPackage(), nullptr, &RaceSpecTimeSource));

		double Previous = 0.0;
		auto CheckNonDecreasing = [this, &Previous](URaceStateMachine* M, const TCHAR* Where)
		{
			const double Elapsed = M->GetRaceElapsedSeconds();
			TestTrue(*FString::Printf(TEXT("Race time never decreases (%s: %f after %f)"), Where, Elapsed, Previous),
				Elapsed >= Previous);
			TestEqual(*FString::Printf(TEXT("Peek agrees with the last sample (%s)"), Where),
				M->PeekRaceElapsedSeconds(), Elapsed, 0.0);
			Previous = Elapsed;
		};

		CheckNonDecreasing(Machine.Get(), TEXT("PreRace"));
		Machine->BeginCountdown();
		GRaceSpecNowSeconds += 3.0;
		CheckNonDecreasing(Machine.Get(), TEXT("Countdown"));

		// The single most important assertion in this block: countdown time is not
		// race time. Three seconds elapsed above, and the race clock must still be 0.
		TestEqual(TEXT("Countdown does not accrue race time"), Machine->GetRaceElapsedSeconds(), 0.0, 0.0);
		TestFalse(TEXT("The race clock is not running during Countdown"), Machine->IsRaceClockRunning());

		Machine->StartRace();
		TestTrue(TEXT("The race clock runs in Racing"), Machine->IsRaceClockRunning());

		// A time source that steps backwards mid-race. Windows QPC will not do this;
		// a virtualised worker or a future platform might, and the displayed time
		// must not go backwards when it does.
		const double Steps[] = { 5.0, 10.0, -4.0, 1.0, 0.0, 20.0 };
		for (const double Step : Steps)
		{
			GRaceSpecNowSeconds += Step;
			CheckNonDecreasing(Machine.Get(), TEXT("Racing"));
		}

		Machine->FinishRace();
		const double FrozenAtFinish = Machine->GetRaceElapsedSeconds();
		CheckNonDecreasing(Machine.Get(), TEXT("Finished"));

		GRaceSpecNowSeconds += 600.0;
		TestEqual(TEXT("Ten minutes on the results screen do not extend the lap"),
			Machine->GetRaceElapsedSeconds(), FrozenAtFinish, 0.0);
		Machine->ShowResults();
		GRaceSpecNowSeconds += 600.0;
		TestEqual(TEXT("The Results state does not extend the lap either"),
			Machine->GetRaceElapsedSeconds(), FrozenAtFinish, 0.0);
		TestFalse(TEXT("The race clock is stopped once Finished"), Machine->IsRaceClockRunning());
	}

	return true;
}

// ===========================================================================
// 5. Restart cannot award progress
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceRestartAwardsNoProgressTest,
	"RacingSim.Race.RestartAwardsNoProgress",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceRestartAwardsNoProgressTest::RunTest(const FString& Parameters)
{
	using namespace RaceStateMachineSpecPrivate;

	SuppressExpectedRejectionLogs(*this);

	const TStrongObjectPtr<URaceRulesetDataAsset> Ruleset(NewObject<URaceRulesetDataAsset>());
	Ruleset->RulesetId = TEXT("Ruleset.Test.RestartAwardsNoProgress");
	Ruleset->CountdownSeconds = 3.0;

	// Restart from every state, not just from Racing. A restart offered on a
	// results screen and a restart triggered by a mid-race failure must land in the
	// same place, or one of the two paths is the one that leaks state.
	for (const ERaceState From : URaceStateMachine::AllStates())
	{
		GRaceSpecNowSeconds = 40000.0;

		const TStrongObjectPtr<URaceStateMachine> Machine(
			URaceStateMachine::CreateWithTimeSource(GetTransientPackage(), Ruleset.Get(), &RaceSpecTimeSource));

		if (!DriveToState(Machine.Get(), From))
		{
			AddError(FString::Printf(TEXT("Could not drive machine to %s"), *StateName(From)));
			continue;
		}

		// Accrue real, visible progress before restarting wherever the state allows
		// it, so "the clock was re-zeroed" is a meaningful claim rather than a
		// no-op on a clock that was already zero.
		GRaceSpecNowSeconds += 42.25;
		const double ElapsedBefore = Machine->GetRaceElapsedSeconds();
		if (From == ERaceState::Racing)
		{
			TestTrue(TEXT("The race had actually accrued time before the restart"), ElapsedBefore > 42.0);
		}

		const int32 SessionBefore = Machine->GetSessionId();
		Machine->Restart();

		TestEqual(*FString::Printf(TEXT("Restart from %s lands in PreRace"), *StateName(From)),
			*StateName(Machine->GetRaceState()), *StateName(ERaceState::PreRace));
		TestEqual(*FString::Printf(TEXT("Restart from %s re-zeroes the race clock"), *StateName(From)),
			Machine->GetRaceElapsedSeconds(), 0.0, 0.0);
		TestEqual(*FString::Printf(TEXT("Restart from %s re-zeroes the peeked time too"), *StateName(From)),
			Machine->PeekRaceElapsedSeconds(), 0.0, 0.0);
		TestFalse(*FString::Printf(TEXT("Restart from %s leaves the clock stopped"), *StateName(From)),
			Machine->IsRaceClockRunning());
		TestEqual(*FString::Printf(TEXT("Restart from %s ends the countdown"), *StateName(From)),
			Machine->GetCountdownRemainingSeconds(), 0.0, 0.0);

		// Session id is what lets a downstream consumer discard a cached lap, a
		// queued async submission, or a delegate payload from the abandoned run.
		const int32 ExpectedSession = (From == ERaceState::PreRace) ? SessionBefore : SessionBefore + 1;
		TestEqual(*FString::Printf(TEXT("Restart from %s bumps the session id unless it was a no-op"), *StateName(From)),
			Machine->GetSessionId(), ExpectedSession);

		// Time keeps passing after the restart. A clock that had merely been
		// "stopped" rather than re-zeroed, or a countdown timer still pending from
		// the abandoned session, would surface here.
		GRaceSpecNowSeconds += 500.0;
		TestEqual(*FString::Printf(TEXT("Restart from %s: no time accrues while in PreRace"), *StateName(From)),
			Machine->GetRaceElapsedSeconds(), 0.0, 0.0);
		TestFalse(*FString::Printf(TEXT("Restart from %s: no stale countdown fires"), *StateName(From)),
			Machine->PollAutoTransitions());
		TestEqual(*FString::Printf(TEXT("Restart from %s: still in PreRace after 500 s"), *StateName(From)),
			*StateName(Machine->GetRaceState()), *StateName(ERaceState::PreRace));

		// The new race must measure itself, not inherit anything.
		Machine->BeginCountdown();
		GRaceSpecNowSeconds += 3.0;
		TestTrue(TEXT("Countdown expires in the new session"), Machine->PollAutoTransitions());
		GRaceSpecNowSeconds += 7.0;
		TestEqual(*FString::Printf(TEXT("Restart from %s: the next race times from zero"), *StateName(From)),
			Machine->GetRaceElapsedSeconds(), 7.0, 1e-9);
	}

	// -- A restart in the middle of a countdown ------------------------------
	//
	// The case a timer-based implementation gets wrong: a pending "go green" timer
	// set at countdown entry fires into the restarted session and starts a race
	// nobody asked for. There is no timer here, and this proves the behaviour.
	{
		GRaceSpecNowSeconds = 60000.0;
		const TStrongObjectPtr<URaceStateMachine> Machine(
			URaceStateMachine::CreateWithTimeSource(GetTransientPackage(), Ruleset.Get(), &RaceSpecTimeSource));

		Machine->BeginCountdown();
		GRaceSpecNowSeconds += 2.9;         // 0.1 s from green
		Machine->Restart();

		GRaceSpecNowSeconds += 0.2;         // the moment the stale timer would fire
		TestFalse(TEXT("An abandoned countdown does not fire into the new session"), Machine->PollAutoTransitions());
		TestEqual(TEXT("The machine stays in PreRace after an abandoned countdown"),
			*StateName(Machine->GetRaceState()), *StateName(ERaceState::PreRace));

		// And the new countdown gets its full duration, not the 0.1 s left on the old one.
		Machine->BeginCountdown();
		TestEqual(TEXT("The new countdown starts from the full duration"),
			Machine->GetCountdownRemainingSeconds(), 3.0, 1e-9);
		GRaceSpecNowSeconds += 2.9;
		TestFalse(TEXT("The new countdown does not inherit the old one's progress"), Machine->PollAutoTransitions());
		GRaceSpecNowSeconds += 0.1;
		TestTrue(TEXT("The new countdown expires on its own schedule"), Machine->PollAutoTransitions());
	}

	return true;
}

// ===========================================================================
// 6. Countdown
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceCountdownTest,
	"RacingSim.Race.Countdown",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceCountdownTest::RunTest(const FString& Parameters)
{
	using namespace RaceStateMachineSpecPrivate;

	const TStrongObjectPtr<URaceRulesetDataAsset> Ruleset(NewObject<URaceRulesetDataAsset>());
	Ruleset->RulesetId = TEXT("Ruleset.Test.Countdown");
	Ruleset->CountdownSeconds = 3.0;

	// -- With a ruleset: the clock releases the race ------------------------
	{
		GRaceSpecNowSeconds = 70000.0;
		const TStrongObjectPtr<URaceStateMachine> Machine(
			URaceStateMachine::CreateWithTimeSource(GetTransientPackage(), Ruleset.Get(), &RaceSpecTimeSource));

		TestTrue(TEXT("A ruleset gives an automatic countdown"), Machine->HasAutomaticCountdown());
		TestEqual(TEXT("No countdown is pending in PreRace"), Machine->GetCountdownRemainingSeconds(), 0.0, 0.0);
		TestFalse(TEXT("Polling in PreRace does nothing"), Machine->PollAutoTransitions());

		Machine->BeginCountdown();
		TestEqual(TEXT("A fresh countdown shows its full duration"), Machine->GetCountdownRemainingSeconds(), 3.0, 1e-9);

		// Frame by frame at an uneven rate, with a hitch, up to just before green.
		// Binary-exact deltas so that the final step below lands on exactly 3.000 s
		// and the "not one frame early, not one frame late" assertion is about the
		// state machine rather than about the fixture's rounding.
		const double Deltas[] = { 0.5, 0.015625, 1.375, 0.0, 0.875 };   // 2.765625 s total
		for (const double Delta : Deltas)
		{
			GRaceSpecNowSeconds += Delta;
			TestFalse(TEXT("The countdown does not release early"), Machine->PollAutoTransitions());
			TestEqual(TEXT("The machine stays in Countdown until green"),
				*StateName(Machine->GetRaceState()), *StateName(ERaceState::Countdown));
		}
		TestEqual(TEXT("Remaining time counts down monotonically"), Machine->GetCountdownRemainingSeconds(), 0.234375, 0.0);
		TestEqual(TEXT("No race time accrues during the countdown"), Machine->GetRaceElapsedSeconds(), 0.0, 0.0);

		GRaceSpecNowSeconds += 0.234375;   // exactly 3.000000 s since BeginCountdown
		TestTrue(TEXT("The countdown releases the race at exactly its duration"), Machine->PollAutoTransitions());
		TestEqual(TEXT("Green means Racing"), *StateName(Machine->GetRaceState()), *StateName(ERaceState::Racing));
		TestEqual(TEXT("Remaining countdown is 0 once racing"), Machine->GetCountdownRemainingSeconds(), 0.0, 0.0);

		// The race clock starts AT green, not at the start of the countdown. If it
		// started at BeginCountdown, this would read 3.0 rather than 0.0 and every
		// lap in the project would be three seconds slow.
		TestEqual(TEXT("The race clock starts at zero on green, not at countdown start"),
			Machine->GetRaceElapsedSeconds(), 0.0, 0.0);

		GRaceSpecNowSeconds += 12.5;
		TestEqual(TEXT("Race time is measured from green"), Machine->GetRaceElapsedSeconds(), 12.5, 1e-9);

		// The poll is idempotent too: it must not fire again now that we are racing.
		int32 BroadcastCount = 0;
		Machine->OnRaceStateChanged.AddLambda([&BroadcastCount](ERaceState, ERaceState, int32) { ++BroadcastCount; });
		for (int32 Index = 0; Index < 10; ++Index)
		{
			GRaceSpecNowSeconds += 0.016;
			TestFalse(TEXT("Polling while racing does nothing"), Machine->PollAutoTransitions());
		}
		TestEqual(TEXT("Polling while racing notifies nobody"), BroadcastCount, 0);
	}

	// -- A zero-length countdown, which automation will use ------------------
	{
		const TStrongObjectPtr<URaceRulesetDataAsset> Instant(NewObject<URaceRulesetDataAsset>());
		Instant->RulesetId = TEXT("Ruleset.Test.InstantCountdown");
		Instant->CountdownSeconds = 0.0;

		GRaceSpecNowSeconds = 80000.0;
		const TStrongObjectPtr<URaceStateMachine> Machine(
			URaceStateMachine::CreateWithTimeSource(GetTransientPackage(), Instant.Get(), &RaceSpecTimeSource));

		Machine->BeginCountdown();
		TestTrue(TEXT("A zero countdown releases on the first poll, with no time passing"),
			Machine->PollAutoTransitions());
		TestEqual(TEXT("A zero countdown still goes through Countdown into Racing"),
			*StateName(Machine->GetRaceState()), *StateName(ERaceState::Racing));
	}

	// -- Without a ruleset: the countdown is manual --------------------------
	{
		GRaceSpecNowSeconds = 90000.0;
		const TStrongObjectPtr<URaceStateMachine> Machine(
			URaceStateMachine::CreateWithTimeSource(GetTransientPackage(), nullptr, &RaceSpecTimeSource));

		TestFalse(TEXT("No ruleset means no automatic countdown"), Machine->HasAutomaticCountdown());

		Machine->BeginCountdown();
		GRaceSpecNowSeconds += 3600.0;
		TestFalse(TEXT("A manual countdown never expires on its own"), Machine->PollAutoTransitions());
		TestEqual(TEXT("A manual countdown stays in Countdown"),
			*StateName(Machine->GetRaceState()), *StateName(ERaceState::Countdown));
		TestEqual(TEXT("A manual countdown reports 0 remaining, not a sentinel"),
			Machine->GetCountdownRemainingSeconds(), 0.0, 0.0);

		TestEqual(TEXT("An explicit start still works"),
			*ResultName(Machine->StartRace()), *ResultName(ERaceTransitionResult::Applied));
		TestEqual(TEXT("Manual start does not back-date the race clock by an hour"),
			Machine->GetRaceElapsedSeconds(), 0.0, 0.0);
	}

	return true;
}

// ===========================================================================
// 7. Re-entrancy from inside a broadcast
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceStateMachineReentrancyTest,
	"RacingSim.Race.Reentrancy",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceStateMachineReentrancyTest::RunTest(const FString& Parameters)
{
	using namespace RaceStateMachineSpecPrivate;

	SuppressExpectedRejectionLogs(*this);
	GRaceSpecNowSeconds = 100000.0;

	const TStrongObjectPtr<URaceStateMachine> Machine(
		URaceStateMachine::CreateWithTimeSource(GetTransientPackage(), nullptr, &RaceSpecTimeSource));

	// An observer that reacts to "the race started" by finishing it. Contrived on
	// purpose -- the realistic version is a HUD or telemetry listener that calls
	// Restart() on a validation failure while the machine is still notifying.
	ERaceTransitionResult InnerResult = ERaceTransitionResult::Applied;
	int32 BroadcastCount = 0;
	int32 InnerAttempts = 0;

	URaceStateMachine* RawMachine = Machine.Get();
	Machine->OnRaceStateChanged.AddLambda(
		[RawMachine, &InnerResult, &BroadcastCount, &InnerAttempts](ERaceState, ERaceState New, int32)
		{
			++BroadcastCount;
			if (New == ERaceState::Racing && InnerAttempts == 0)
			{
				++InnerAttempts;
				InnerResult = RawMachine->RequestTransition(ERaceTransition::FinishRace);
			}
		});

	Machine->BeginCountdown();
	Machine->StartRace();

	TestEqual(TEXT("A re-entrant transition attempt was made"), InnerAttempts, 1);
	TestEqual(TEXT("A transition requested from inside a broadcast is rejected"),
		*ResultName(InnerResult), *ResultName(ERaceTransitionResult::RejectedReentrant));

	// The important consequence: the final state is what the outer caller asked
	// for, not what an observer's ordering happened to produce.
	TestEqual(TEXT("The observer did not change the outcome of the outer transition"),
		*StateName(Machine->GetRaceState()), *StateName(ERaceState::Racing));
	TestEqual(TEXT("Exactly two transitions were broadcast"), BroadcastCount, 2);

	// The guard must not latch. Once the broadcast unwinds, normal transitions work.
	GRaceSpecNowSeconds += 5.0;
	TestEqual(TEXT("Transitions work normally once the broadcast has unwound"),
		*ResultName(Machine->FinishRace()), *ResultName(ERaceTransitionResult::Applied));
	TestEqual(TEXT("The deferred finish measured the real elapsed time"),
		Machine->GetRaceElapsedSeconds(), 5.0, 1e-9);

	return true;
}

// ===========================================================================
// 8. The ruleset asset
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceRulesetTest,
	"RacingSim.Race.Ruleset",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceRulesetTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<URaceRulesetDataAsset> Ruleset(NewObject<URaceRulesetDataAsset>());

	// -- Defaults -------------------------------------------------------------
	TestEqual(TEXT("Countdown defaults to 3 seconds"), Ruleset->CountdownSeconds, 3.0, 0.0);
	TestTrue(TEXT("A new ruleset has no id until it is authored"), Ruleset->RulesetId.IsNone());

	// -- Validation -----------------------------------------------------------
	{
		FString Reason;
		TestFalse(TEXT("An unnamed ruleset does not validate"), Ruleset->Validate(Reason));
		TestTrue(TEXT("The failure names the missing id"), Reason.Contains(TEXT("RulesetId")));

		Ruleset->RulesetId = TEXT("Ruleset.Test.Default");
		Reason.Reset();
		TestTrue(TEXT("A named ruleset with a 3 s countdown validates"), Ruleset->Validate(Reason));
		TestTrue(TEXT("A successful validation leaves no reason"), Reason.IsEmpty());

		Ruleset->CountdownSeconds = -1.0;
		TestFalse(TEXT("A negative countdown is rejected rather than clamped"), Ruleset->Validate(Reason));

		Ruleset->CountdownSeconds = 0.0;
		TestFalse(TEXT("A zero countdown is legal for automation but not for a published run"), Ruleset->Validate(Reason));

		Ruleset->CountdownSeconds = 3.0;
		TestTrue(TEXT("Restoring a sane countdown validates again"), Ruleset->Validate(Reason));
	}

	// -- Version stamp --------------------------------------------------------
	//
	// FRacingSimVersionStamp::RulesetVersion was reserved by CORE-002 for exactly
	// this. If the hash did not move with the data, two runs under different rules
	// would claim to be comparable.
	{
		const FRacingContentVersion Version = Ruleset->GetContentVersion();
		TestEqual(TEXT("The version carries the ruleset id"), Version.AssetId, FName(TEXT("Ruleset.Test.Default")));
		TestEqual(TEXT("The schema version is the C++ layout version"),
			Version.SchemaVersion, URaceRulesetDataAsset::RulesetSchemaVersion);
		TestTrue(TEXT("A named, versioned ruleset counts as populated"), Version.IsPopulated());

		const uint32 BaselineHash = Ruleset->ComputeContentHash();
		TestTrue(TEXT("The hash is stable across calls"), BaselineHash == Ruleset->ComputeContentHash());

		Ruleset->CountdownSeconds = 5.0;
		TestTrue(TEXT("Retuning the countdown changes the content hash"), Ruleset->ComputeContentHash() != BaselineHash);

		Ruleset->CountdownSeconds = 3.0;
		TestEqual(TEXT("Restoring the value restores the hash"), Ruleset->ComputeContentHash(), BaselineHash);

		Ruleset->RulesetId = TEXT("Ruleset.Test.Other");
		TestTrue(TEXT("Renaming the ruleset changes the content hash"), Ruleset->ComputeContentHash() != BaselineHash);
	}

	return true;
}
