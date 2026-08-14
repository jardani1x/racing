// Copyright RacingSim. All Rights Reserved.

#include "Race/RaceStateMachine.h"

#include "Core/RacingSimLog.h"
#include "Race/RaceRulesetDataAsset.h"
#include "Engine/World.h"
#include "UObject/Class.h"

namespace
{
	/**
	 * The authored transition graph, as data.
	 *
	 * A table rather than a switch, because it is the thing the tests assert
	 * against: an automation test walks every (state, transition) pair and checks
	 * that exactly these rows are accepted. A switch spread across a function body
	 * cannot be enumerated, so "no other edge exists" would have been untestable and
	 * would have degraded into "no other edge that we thought to test exists".
	 *
	 * Restart is not listed. It is legal from every state and is handled before this
	 * table is consulted -- see the comment on ERaceTransition::Restart for why one
	 * implementation of restart matters more than table symmetry.
	 */
	struct FRaceTransitionRow
	{
		ERaceState From;
		ERaceTransition Transition;
		ERaceState To;
	};

	constexpr FRaceTransitionRow GRaceTransitionTable[] = {
		{ ERaceState::PreRace,   ERaceTransition::BeginCountdown, ERaceState::Countdown },
		{ ERaceState::Countdown, ERaceTransition::StartRace,      ERaceState::Racing    },
		{ ERaceState::Racing,    ERaceTransition::FinishRace,     ERaceState::Finished  },
		{ ERaceState::Finished,  ERaceTransition::ShowResults,    ERaceState::Results   }
	};

	constexpr ERaceState GAllRaceStates[] = {
		ERaceState::PreRace,
		ERaceState::Countdown,
		ERaceState::Racing,
		ERaceState::Finished,
		ERaceState::Results
	};

	/**
	 * The state each transition exists to reach. Kept next to the edge table so the
	 * two cannot drift: every row of GRaceTransitionTable must have a To equal to
	 * this destination, which an automation test asserts.
	 */
	struct FRaceTransitionDestinationRow
	{
		ERaceTransition Transition;
		ERaceState Destination;
	};

	constexpr FRaceTransitionDestinationRow GRaceTransitionDestinations[] = {
		{ ERaceTransition::BeginCountdown, ERaceState::Countdown },
		{ ERaceTransition::StartRace,      ERaceState::Racing    },
		{ ERaceTransition::FinishRace,     ERaceState::Finished  },
		{ ERaceTransition::ShowResults,    ERaceState::Results   },
		{ ERaceTransition::Restart,        ERaceState::PreRace   }
	};

	constexpr ERaceTransition GAllRaceTransitions[] = {
		ERaceTransition::BeginCountdown,
		ERaceTransition::StartRace,
		ERaceTransition::FinishRace,
		ERaceTransition::ShowResults,
		ERaceTransition::Restart
	};

	/** Range check for values that arrived as a cast from an integer. */
	bool IsKnownState(ERaceState State)
	{
		for (const ERaceState Known : GAllRaceStates)
		{
			if (Known == State)
			{
				return true;
			}
		}
		return false;
	}

	bool IsKnownTransition(ERaceTransition Transition)
	{
		for (const ERaceTransition Known : GAllRaceTransitions)
		{
			if (Known == Transition)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Enum name for a log line. Reflection lookup plus an FString, so it is only
	 * ever called on a rejection or on an applied transition -- never per frame.
	 */
	template <typename TEnum>
	FString EnumToDebugString(TEnum Value)
	{
		const UEnum* EnumType = StaticEnum<TEnum>();
		const int64 Raw = static_cast<int64>(Value);
		if (EnumType == nullptr)
		{
			return FString::Printf(TEXT("<%lld>"), Raw);
		}

		const FString Name = EnumType->GetNameStringByValue(Raw);
		// Out-of-range casts resolve to an empty name; print the integer so a bad
		// value is identifiable in a log instead of appearing as a blank.
		return Name.IsEmpty() ? FString::Printf(TEXT("<out-of-range %lld>"), Raw) : Name;
	}
}

// ===========================================================================
// Construction
// ===========================================================================

bool URaceStateMachine::HasRaceAuthority(const UObject* Context)
{
	if (Context == nullptr)
	{
		return false;
	}

	const UWorld* World = Context->GetWorld();
	if (World == nullptr)
	{
		// Commandlet, automation, CDO or a transient-package outer: there is no
		// network session, so there is no client to reject. Failing closed here
		// would make the object impossible to construct in the automation
		// environment that is supposed to prove it correct.
		return true;
	}

	return World->GetNetMode() != NM_Client;
}

URaceStateMachine* URaceStateMachine::CreateWithTimeSource(UObject* Owner, URaceRulesetDataAsset* InRuleset, FRaceTimeSourceFn InTimeSource)
{
	if (Owner == nullptr)
	{
		UE_LOG(LogRacingRace, Error, TEXT("URaceStateMachine::Create refused: no owner. The owner is the GC root and the authority context."));
		return nullptr;
	}

	if (!HasRaceAuthority(Owner))
	{
		// CLAUDE.md: race timing is server-side. A client-side instance would be a
		// second clock, and a second clock is a disagreement waiting to be shown on
		// a HUD as truth.
		UE_LOG(LogRacingRace, Error, TEXT("URaceStateMachine::Create refused on a net client. Race state is server-authoritative."));
		return nullptr;
	}

	if (InTimeSource == nullptr)
	{
		UE_LOG(LogRacingRace, Error, TEXT("URaceStateMachine::Create refused: null time source."));
		return nullptr;
	}

	URaceStateMachine* Machine = NewObject<URaceStateMachine>(Owner);
	Machine->TimeSource = InTimeSource;

	if (InRuleset == nullptr)
	{
		// Not an error. It means Countdown is driven by an explicit StartRace()
		// rather than by the clock. Logged once, at Display, so a session that is
		// silently manual is visible in a log without being alarming.
		UE_LOG(LogRacingRace, Display, TEXT("Race state machine created with no ruleset: countdown is manual, PollAutoTransitions will not advance it."));
	}
	// Deliberately NOT InRuleset->Validate() here: Validate() also rejects
	// CountdownSeconds == 0.0 as "legal for automation but not for a published
	// run" (RaceRulesetDataAsset.h), and 0.0 is exactly the value this project's
	// own automation uses for an instant-release countdown. Validate() is a
	// publish-time content check (CORE-003's job); construction only needs to
	// stop the two values that corrupt state rather than merely being
	// gameplay-unwise: non-finite reaches GetCountdownRemainingSeconds()
	// (a BlueprintCallable getter) as NaN, and negative satisfies the `<`
	// comparison in PollAutoTransitions() on every poll from the moment
	// Countdown is entered, same effective bug as NaN.
	else if (!FMath::IsFinite(InRuleset->CountdownSeconds) || InRuleset->CountdownSeconds < 0.0)
	{
		UE_LOG(LogRacingRace, Error,
			TEXT("Race state machine created with an invalid CountdownSeconds (%f); falling back to manual countdown."),
			InRuleset->CountdownSeconds);
	}
	else
	{
		Machine->Ruleset = InRuleset;
	}

	return Machine;
}

URaceStateMachine* URaceStateMachine::Create(UObject* Owner, URaceRulesetDataAsset* InRuleset)
{
	return CreateWithTimeSource(Owner, InRuleset, &RacingSim::Race::PlatformMonotonicSeconds);
}

void URaceStateMachine::BeginDestroy()
{
	// Stale-delegate hygiene. Observers are expected to unbind themselves, but a
	// broadcast from a half-destroyed object is not a failure mode worth leaving to
	// convention -- and there is no timer or async callback to cancel here precisely
	// because the clock is pull-based (RaceClock.h).
	OnRaceStateChanged.Clear();

	Super::BeginDestroy();
}

// ===========================================================================
// The authored transition graph
// ===========================================================================

bool URaceStateMachine::GetTransitionTarget(ERaceState From, ERaceTransition Transition, ERaceState& OutTo)
{
	if (!IsKnownState(From) || !IsKnownTransition(Transition))
	{
		return false;
	}

	if (Transition == ERaceTransition::Restart)
	{
		OutTo = ERaceState::PreRace;
		return true;
	}

	for (const FRaceTransitionRow& Row : GRaceTransitionTable)
	{
		if (Row.From == From && Row.Transition == Transition)
		{
			OutTo = Row.To;
			return true;
		}
	}

	return false;
}

bool URaceStateMachine::GetTransitionDestination(ERaceTransition Transition, ERaceState& OutDestination)
{
	for (const FRaceTransitionDestinationRow& Row : GRaceTransitionDestinations)
	{
		if (Row.Transition == Transition)
		{
			OutDestination = Row.Destination;
			return true;
		}
	}

	return false;
}

TConstArrayView<ERaceState> URaceStateMachine::AllStates()
{
	return TConstArrayView<ERaceState>(GAllRaceStates, UE_ARRAY_COUNT(GAllRaceStates));
}

TConstArrayView<ERaceTransition> URaceStateMachine::AllTransitions()
{
	return TConstArrayView<ERaceTransition>(GAllRaceTransitions, UE_ARRAY_COUNT(GAllRaceTransitions));
}

// ===========================================================================
// Transitions
// ===========================================================================

double URaceStateMachine::Now() const
{
	// TimeSource is installed by Create() and validated non-null there. The fallback
	// exists for a default-constructed CDO, which has no session and never
	// transitions, but must not crash if something reads it.
	return TimeSource != nullptr ? TimeSource() : RacingSim::Race::PlatformMonotonicSeconds();
}

ERaceTransitionResult URaceStateMachine::RequestTransition(ERaceTransition Transition)
{
	if (bIsBroadcasting)
	{
		UE_LOG(LogRacingRace, Warning,
			TEXT("Re-entrant race transition '%s' requested from inside an OnRaceStateChanged broadcast; rejected. State remains %s."),
			*EnumToDebugString(Transition), *EnumToDebugString(CurrentState));
		return ERaceTransitionResult::RejectedReentrant;
	}

	if (!IsKnownTransition(Transition))
	{
		UE_LOG(LogRacingRace, Warning,
			TEXT("Race transition rejected: value %s is not a declared ERaceTransition. State remains %s."),
			*EnumToDebugString(Transition), *EnumToDebugString(CurrentState));
		return ERaceTransitionResult::RejectedOutOfRange;
	}

	if (!IsKnownState(CurrentState))
	{
		// Only reachable if something outside this class wrote the state, which
		// nothing is permitted to do. Report rather than crash, and do not attempt
		// to guess a recovery -- a state machine that repairs itself hides the write
		// that corrupted it.
		UE_LOG(LogRacingRace, Error,
			TEXT("Race state machine is in an undeclared state %s; transition '%s' rejected."),
			*EnumToDebugString(CurrentState), *EnumToDebugString(Transition));
		return ERaceTransitionResult::RejectedOutOfRange;
	}

	// -- Idempotency, checked BEFORE legality -------------------------------
	//
	// If the machine is already where this transition was going, the caller's
	// post-condition holds and there is nothing to do. Checking this first is what
	// makes a doubled call a no-op rather than a warning, for every transition,
	// including ones whose (state -> same state) pair is not an edge of the graph.
	// No entry action, no clock touch, no broadcast.
	ERaceState DestinationState = CurrentState;
	if (GetTransitionDestination(Transition, DestinationState) && DestinationState == CurrentState)
	{
		// Verbose, not Log: a redundant transition is normal traffic between a UI
		// button and the auto-advance poll, and CLAUDE.md forbids noisy logging on
		// paths that can run every frame.
		UE_LOG(LogRacingRace, Verbose,
			TEXT("Race transition '%s' is redundant: already in %s."),
			*EnumToDebugString(Transition), *EnumToDebugString(CurrentState));
		return ERaceTransitionResult::Redundant;
	}

	ERaceState TargetState = CurrentState;
	if (!GetTransitionTarget(CurrentState, Transition, TargetState))
	{
		UE_LOG(LogRacingRace, Warning,
			TEXT("Illegal race transition '%s' from %s; rejected, state unchanged."),
			*EnumToDebugString(Transition), *EnumToDebugString(CurrentState));
		return ERaceTransitionResult::RejectedIllegal;
	}

	// Unreachable by construction: every transition has a destination, and being
	// already at it was handled above. Guarded anyway so that a future edge added to
	// the table with To == From cannot start silently re-running entry actions --
	// which for Finished would mean re-freezing the result at a later timestamp.
	if (TargetState == CurrentState)
	{
		UE_LOG(LogRacingRace, Verbose,
			TEXT("Race transition '%s' resolves to the current state %s; treated as redundant."),
			*EnumToDebugString(Transition), *EnumToDebugString(CurrentState));
		return ERaceTransitionResult::Redundant;
	}

	// One reading for the whole transition, so any two clocks touched below are
	// stamped with the same instant rather than with two readings a few microseconds
	// apart. Cheap to do, impossible to retrofit once a bug depends on it.
	CommitTransition(TargetState, Transition, Now());
	return ERaceTransitionResult::Applied;
}

void URaceStateMachine::CommitTransition(ERaceState NewState, ERaceTransition Transition, double NowSeconds)
{
	const ERaceState OldState = CurrentState;
	CurrentState = NewState;

	// -- Entry actions. Each runs exactly once, because this function is only
	//    reached from the Applied path. --------------------------------------
	switch (NewState)
	{
	case ERaceState::PreRace:
		// The only way into PreRace is Restart. Both clocks re-zero and the session
		// id bumps. Gate B: "reset cannot award progress" -- there is nothing here
		// that carries a partial time, a partial countdown, or a pending timer
		// across the boundary, because there is no pending timer to carry.
		RaceClock.Reset();
		CountdownClock.Reset();
		++SessionId;
		break;

	case ERaceState::Countdown:
		// Countdown time starts; race time deliberately does not. See ERaceState.
		CountdownClock.Reset();
		CountdownClock.Start(NowSeconds);
		break;

	case ERaceState::Racing:
		CountdownClock.Stop(NowSeconds);
		RaceClock.Start(NowSeconds);
		break;

	case ERaceState::Finished:
		// Freeze once. FRaceClock::Stop is idempotent, so even a second entry could
		// not extend the result -- but a second entry is impossible anyway, since
		// Finished has no incoming edge from itself.
		RaceClock.Stop(NowSeconds);
		break;

	case ERaceState::Results:
		// No clock work. The result was frozen on entry to Finished, and re-freezing
		// it here would make the displayed time depend on how long the finish
		// presentation ran.
		break;

	default:
		break;
	}

	UE_LOG(LogRacingRace, Display,
		TEXT("Race state %s -> %s via '%s' (session %d, race clock %.3fs)."),
		*EnumToDebugString(OldState), *EnumToDebugString(NewState), *EnumToDebugString(Transition),
		SessionId, RaceClock.Peek());

	// -- Notify, guarded against re-entry -----------------------------------
	{
		TGuardValue<bool> BroadcastGuard(bIsBroadcasting, true);
		OnRaceStateChanged.Broadcast(OldState, NewState, SessionId);
	}
}

bool URaceStateMachine::PollAutoTransitions()
{
	// Ordered cheapest-first: the common case (not counting down) costs one compare.
	if (CurrentState != ERaceState::Countdown)
	{
		return false;
	}

	if (Ruleset == nullptr)
	{
		return false;
	}

	if (CountdownClock.Sample(Now()) < Ruleset->CountdownSeconds)
	{
		return false;
	}

	return RequestTransition(ERaceTransition::StartRace) == ERaceTransitionResult::Applied;
}

// ===========================================================================
// Reads
// ===========================================================================

double URaceStateMachine::GetRaceElapsedSeconds()
{
	return RaceClock.Sample(Now());
}

double URaceStateMachine::GetCountdownRemainingSeconds()
{
	if (CurrentState != ERaceState::Countdown || Ruleset == nullptr)
	{
		return 0.0;
	}

	return FMath::Max(0.0, Ruleset->CountdownSeconds - CountdownClock.Sample(Now()));
}
