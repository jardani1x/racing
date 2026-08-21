// Copyright RacingSim. All Rights Reserved.

#include "Race/RaceResult.h"

#include "Core/RacingSimLog.h"
#include "Core/RacingSimUrl.h"
#include "Race/RaceLapTracker.h"
#include "Race/RaceRulesetDataAsset.h"
#include "Race/RaceStateMachine.h"
#include "Race/TrackDefinitionActor.h"

namespace RaceResultPrivate
{
	/**
	 * Append `Key=AlreadyEncodedValue`, inserting '&' when needed. Encodes NOTHING.
	 *
	 * Split from AppendRawQueryPair because one value in this file arrives pre-encoded --
	 * the build ID, through FRacingSimBuildId::ToUrlQueryValue(), which is the documented
	 * answer to CORE-003 C3-4 and must stay the API that site uses. Passing an
	 * already-encoded value to an encoding helper DOUBLE-ENCODES it: "%2B" becomes "%252B",
	 * which decodes to the literal text "%2B" rather than to '+'. That is not a hypothetical
	 * -- it is what the first draft of this function did, and
	 * RacingSim.Race.ResultSubmission caught it by asserting the exact expected substring
	 * rather than merely asserting that no bare '+' survived. A "no bare '+'" check alone
	 * passes happily on a double-encoded string.
	 *
	 * The keys are compile-time literals from the unreserved set and need no encoding.
	 */
	void AppendEncodedQueryPair(FString& Query, const TCHAR* Key, const FString& EncodedValue)
	{
		if (!Query.IsEmpty())
		{
			Query.AppendChar(TEXT('&'));
		}

		Query.Append(Key);
		Query.AppendChar(TEXT('='));
		Query.Append(EncodedValue);
	}

	/**
	 * Append `Key=PercentEncoded(RawValue)`. The default for every value that is not
	 * already encoded -- which is all of them except the build ID.
	 */
	void AppendRawQueryPair(FString& Query, const TCHAR* Key, const FString& RawValue)
	{
		// EVERY value, without exception. Content identities carry '@' and '#'; the moment
		// one is exempted "because it is just a number" is the moment a future field or a
		// locale puts a reserved character in it.
		AppendEncodedQueryPair(Query, Key, RacingSim::Url::PercentEncodeQueryValue(RawValue));
	}

	FString EnumName(const ERacingRunValidity Validity)
	{
		const UEnum* EnumType = StaticEnum<ERacingRunValidity>();
		const FString Name = EnumType ? EnumType->GetNameStringByValue(static_cast<int64>(Validity)) : FString();
		return Name.IsEmpty() ? FString::Printf(TEXT("<%d>"), static_cast<int32>(Validity)) : Name;
	}
}

// ===========================================================================
// FRacingRaceResult
// ===========================================================================

bool FRacingRaceResult::IsSubmittable(FString* OutReason) const
{
	auto Fail = [OutReason](const FString& Reason) -> bool
	{
		if (OutReason != nullptr)
		{
			*OutReason = Reason;
		}
		return false;
	};

	// Checked FIRST. An unfrozen result is a default-constructed struct whose every other
	// field is meaningless, and running the metadata checks against it would produce a
	// confident, specific and entirely wrong reason.
	if (!bFrozen)
	{
		return Fail(TEXT("The result has not been frozen; there is no run to submit."));
	}

	// THE TRACK GATE. TRACK-001 M7 and TRACK-002 M4, at the point where they finally
	// matter: a track can bake successfully -- including with a FAILED GATE BAKE, because
	// RebuildTrackData() returns true regardless -- and still be unpublishable. Gates
	// decide which laps count, so a result from a track whose gates did not bake is a
	// result whose lap validity means nothing, however clean it looks.
	if (!bTrackValidated)
	{
		return Fail(FString::Printf(
			TEXT("The track did not validate when this result was frozen, so its lap validity cannot be "
				 "trusted: %s"),
			TrackValidationReason.IsEmpty() ? TEXT("<no reason recorded>") : *TrackValidationReason));
	}

	// THE CLOCK GATE (RACE-001 finding M4, at the publication boundary).
	//
	// Checked SEPARATELY from validity, and the distinction is the point. CORE-002's
	// IsPublishable() requires only a TERMINAL validity, which is right: an invalid run is
	// still a run that happened, and a leaderboard legitimately records a void attempt
	// struck through rather than pretending it did not occur. InvalidShortcut,
	// InvalidReverseCrossing and InvalidVehicleReset are all like that -- the TIMES ARE
	// REAL and only the verdict is void.
	//
	// A CLOCK FAULT IS NOT LIKE THAT. When the authoritative clock refused to start, every
	// timing field on this result is a fiction rather than a void measurement:
	// FinalTimeSeconds is 0.000 and so is every lap. Publishing that is precisely the
	// failure RACE-001 M4 exists to prevent -- "a zero-duration lap reaching a leaderboard
	// as the fastest ever driven" -- and relying on a downstream consumer to notice the
	// validity first is the "harmless because the other end checks" reasoning this project
	// refuses elsewhere. The run is refused here instead.
	//
	// Found by RacingSim.Race.ResultClockFault, which asserted the refusal before this
	// branch existed and got a submittable result: InvalidIncomplete is terminal, so
	// IsPublishable() waved it through.
	if (bRaceClockFaulted)
	{
		return Fail(TEXT("The authoritative race clock faulted during this session, so every duration on "
						 "this result is 0.000 rather than a real measurement; there is no time to publish."));
	}

	// Everything else is CORE-002's published rule, called rather than re-implemented: a
	// non-authoritative (Derived) build ID, an unpopulated track/car/ruleset version,
	// PhysicsPolicyVersion == 0, an Unknown input device, a non-terminal validity. A
	// second copy of that list here would be a second place for it to drift.
	FString StampReason;
	if (!Version.IsPublishable(&StampReason))
	{
		return Fail(FString::Printf(TEXT("Version metadata is not publishable: %s"), *StampReason));
	}

	if (OutReason != nullptr)
	{
		OutReason->Reset();
	}
	return true;
}

bool FRacingRaceResult::MakeSubmissionQueryString(FString& OutQuery, FString& OutReason) const
{
	using namespace RaceResultPrivate;

	// REFUSAL IS STRUCTURAL. OutQuery is cleared before the check, so a caller that
	// ignores the return value gets an empty string rather than a submittable-looking one.
	// "Rejects invalid build/track/tune metadata" has to mean the string does not exist,
	// not that a flag says it should not be used.
	OutQuery.Reset();

	if (!IsSubmittable(&OutReason))
	{
		return false;
	}

	// CORE-003 C3-4, discharged on EVERY value.
	//
	// The build ID goes through its own accessor -- FRacingSimBuildId::ToUrlQueryValue() --
	// because that accessor IS the documented answer to C3-4 and this is the site the
	// finding is about. It returns an already-encoded string, so it is appended with the
	// non-encoding helper; everything else is raw and is encoded on the way in.
	AppendEncodedQueryPair(OutQuery, TEXT("build"), Version.GameBuildId.ToUrlQueryValue());
	AppendRawQueryPair(OutQuery, TEXT("track"), Version.TrackVersion.ToString());
	AppendRawQueryPair(OutQuery, TEXT("car"), Version.CarSpecVersion.ToString());
	AppendRawQueryPair(OutQuery, TEXT("ruleset"), Version.RulesetVersion.ToString());
	AppendRawQueryPair(OutQuery, TEXT("engine"),
		FString::Printf(TEXT("%s+%d"), *Version.EngineVersion, Version.EngineChangelist));
	AppendRawQueryPair(OutQuery, TEXT("physics"), FString::FromInt(Version.PhysicsPolicyVersion));
	AppendRawQueryPair(OutQuery, TEXT("assists"), FString::FromInt(static_cast<int32>(Version.AssistPreset)));
	AppendRawQueryPair(OutQuery, TEXT("input"), FString::FromInt(static_cast<int32>(Version.InputDeviceType)));
	AppendRawQueryPair(OutQuery, TEXT("validity"), EnumName(Version.Validity));
	AppendRawQueryPair(OutQuery, TEXT("laps"), FString::FromInt(ValidLapsCompleted));

	// DURATIONS AS RAW SECONDS, never formatted. Docs/03-TrackRaceUI.md: "Store raw
	// duration with high precision; format only in UI." %.6f keeps microseconds, which is
	// three orders of magnitude finer than any timing decision this project makes and far
	// coarser than double's ~15 significant digits -- so it is lossy in the last place and
	// deliberately so: a full-precision decimal expansion would make two bit-identical
	// laps compare unequal as strings on some platforms' printf.
	AppendRawQueryPair(OutQuery, TEXT("best"), FString::Printf(TEXT("%.6f"), BestLap.LapDurationSeconds));
	AppendRawQueryPair(OutQuery, TEXT("final"), FString::Printf(TEXT("%.6f"), FinalTimeSeconds));

	OutReason.Reset();
	return true;
}

FString FRacingRaceResult::ToString() const
{
	using namespace RaceResultPrivate;

	if (!bFrozen)
	{
		return TEXT("<no frozen result>");
	}

	return FString::Printf(
		TEXT("session=%d final=%.3fs best=%.3fs(lap %d) last=%.3fs(lap %d) laps=%d valid=%d sectors=%d "
			 "validity=%s trackValidated=%s clockFaulted=%s | %s"),
		SessionId,
		FinalTimeSeconds,
		BestLap.LapDurationSeconds, BestLap.LapNumber,
		LastLap.LapDurationSeconds, LastLap.LapNumber,
		LapsCompleted, ValidLapsCompleted, TrackSectorCount,
		*EnumName(Version.Validity),
		bTrackValidated ? TEXT("yes") : TEXT("no"),
		bRaceClockFaulted ? TEXT("yes") : TEXT("no"),
		*Version.ToString());
}

// ===========================================================================
// URaceResultRecorder: construction and wiring
// ===========================================================================

URaceResultRecorder* URaceResultRecorder::Create(UObject* Owner, URaceStateMachine* InStateMachine)
{
	if (Owner == nullptr)
	{
		UE_LOG(LogRacingRace, Error,
			TEXT("URaceResultRecorder::Create requires an owner to outer the recorder to; the owner is also its GC root."));
		return nullptr;
	}

	if (InStateMachine == nullptr)
	{
		// Refused rather than defaulted, for the same reason URaceLapTracker::Create
		// refuses: the final time and the freeze INSTANT both come from the state machine,
		// and a recorder without one could only invent both. A result stamped 0.000 by a
		// recorder that had no clock is indistinguishable from a real zero.
		UE_LOG(LogRacingRace, Error,
			TEXT("URaceResultRecorder::Create requires a URaceStateMachine; the final time and the freeze "
				 "instant have no other source."));
		return nullptr;
	}

	URaceResultRecorder* Recorder = NewObject<URaceResultRecorder>(Owner);
	Recorder->StateMachine = InStateMachine;

	// THE ONE BINDING, for this object's whole lifetime. See the class comment: AddUObject
	// so the invocation list drops a collected recorder rather than calling into it, the
	// handle stored so DetachFromStateMachine() can remove it explicitly, and bound HERE
	// rather than per session so a restart cannot accumulate a second subscription.
	Recorder->StateChangedHandle = InStateMachine->OnRaceStateChanged.AddUObject(
		Recorder, &URaceResultRecorder::HandleRaceStateChanged);

	return Recorder;
}

void URaceResultRecorder::BeginDestroy()
{
	DetachFromStateMachine();
	Super::BeginDestroy();
}

void URaceResultRecorder::DetachFromStateMachine()
{
	if (StateChangedHandle.IsValid() && StateMachine != nullptr)
	{
		StateMachine->OnRaceStateChanged.Remove(StateChangedHandle);
	}

	// Reset unconditionally, so a handle whose owner has already gone does not linger
	// making IsObservingStateMachine() lie. Idempotent by construction.
	StateChangedHandle.Reset();
}

bool URaceResultRecorder::RegisterLapTracker(URaceLapTracker* Tracker)
{
	if (Tracker == nullptr)
	{
		UE_LOG(LogRacingRace, Warning, TEXT("URaceResultRecorder::RegisterLapTracker refused a null tracker."));
		return false;
	}

	if (LapTrackers.Contains(Tracker))
	{
		// A no-op rather than an error, and it matters: a double-registered tracker would
		// be reset TWICE by ClearForNewSession(). ResetForNewSession() is idempotent so
		// that would be harmless today, but "harmless because the other end happens to be
		// idempotent" is not a property to build a restart on.
		UE_LOG(LogRacingRace, Verbose, TEXT("Lap tracker is already registered with this recorder."));
		return false;
	}

	LapTrackers.Add(Tracker);
	return true;
}

bool URaceResultRecorder::UnregisterLapTracker(URaceLapTracker* Tracker)
{
	return Tracker != nullptr && LapTrackers.Remove(Tracker) > 0;
}

URaceLapTracker* URaceResultRecorder::GetPrimaryLapTracker() const
{
	// Index 0 is primary by definition (registration order). Skipping to the first
	// non-null entry rather than returning LapTrackers[0] blindly: a tracker can be
	// collected if its other owner drops it, and this array holding a null is a state
	// ClearForNewSession() prunes but a read may still encounter first.
	for (const TObjectPtr<URaceLapTracker>& Candidate : LapTrackers)
	{
		if (Candidate != nullptr)
		{
			return Candidate;
		}
	}

	return nullptr;
}

void URaceResultRecorder::SetTrack(ATrackDefinitionActor* InTrack)
{
	Track = InTrack;
	RefreshTrackSnapshot();
}

void URaceResultRecorder::SetTrackSnapshot(
	const FRacingContentVersion& InTrackVersion,
	const int32 InSectorCount,
	const bool bInValidated,
	const FString& InValidationReason)
{
	TrackVersion = InTrackVersion;
	TrackSectorCount = FMath::Max(0, InSectorCount);
	bTrackValidated = bInValidated;
	TrackValidationReason = InValidationReason;
}

void URaceResultRecorder::SetCarSpecVersion(const FRacingContentVersion& InCarSpecVersion)
{
	CarSpecVersion = InCarSpecVersion;
}

void URaceResultRecorder::SetInputDeviceType(const ERacingInputDeviceType InInputDeviceType)
{
	InputDeviceType = InInputDeviceType;
}

void URaceResultRecorder::RefreshTrackSnapshot()
{
	if (Track == nullptr)
	{
		return;
	}

	// THROUGH THE CACHE, NEVER THROUGH Validate(). This is TRACK-001 M7's first consumer
	// and the whole reason the cache exists: a race director must be able to refuse a
	// session on an invalid track without paying a full validation, and a result must
	// record whether the track was publishable AT THE TIME rather than asking again later
	// when an editor edit may have changed the answer.
	FString Reason;
	bTrackValidated = Track->GetCachedValidation(Reason);
	TrackValidationReason = MoveTemp(Reason);

	TrackVersion = Track->GetContentVersion();
	TrackSectorCount = Track->GetNumSectors();
}

// ===========================================================================
// Session lifecycle
// ===========================================================================

void URaceResultRecorder::HandleRaceStateChanged(
	const ERaceState OldState,
	const ERaceState NewState,
	const int32 InSessionId)
{
	++StateChangeNotificationCount;

	switch (NewState)
	{
	case ERaceState::Finished:
		// THE FREEZE INSTANT. The state machine has already run Finished's entry action,
		// which stops the race clock, before it broadcasts -- CommitTransition commits
		// state and clocks first and notifies last, precisely so an observer always reads
		// a consistent object. So PeekRaceElapsedSeconds() below is the frozen final time,
		// not a live sample racing the transition.
		FreezeResult();
		break;

	case ERaceState::PreRace:
		// The ONLY way into PreRace is Restart (RaceStateMachine.cpp says so at the entry
		// action, and the transition table has no other edge landing here). So this is the
		// restart hook, and it does not need -- and deliberately does not use -- the
		// session id to detect one: RACE-001's finding L3 is that a redundant Restart from
		// PreRace bumps no id and broadcasts nothing, which is correct, because to be in
		// PreRace already is to have been cleared already.
		ClearForNewSession();
		break;

	default:
		// Countdown, Racing and Results carry no result work. Results in particular must
		// NOT re-freeze: the displayed time would then depend on how long the finish
		// presentation ran, which is the bug ERaceState::Results' own comment warns about.
		break;
	}

	UE_LOG(LogRacingRace, Verbose,
		TEXT("Result recorder observed a race state change (session %d), notification %d."),
		InSessionId, StateChangeNotificationCount);
}

bool URaceResultRecorder::CanStartSession(FString& OutReason) const
{
	if (StateMachine == nullptr)
	{
		OutReason = TEXT("No race state machine; the session has no authoritative clock or state.");
		return false;
	}

	// THE CHEAP REFUSAL TRACK-001 M7 ASKED FOR. Note this reads the snapshot, which
	// SetTrack() filled from the cache -- it does not call Validate() and does not even
	// re-hash unless the caller re-sets the track.
	if (!bTrackValidated)
	{
		OutReason = TrackValidationReason.IsEmpty()
			? TEXT("The track has not been validated for racing. Call SetTrack() or SetTrackSnapshot() first.")
			: FString::Printf(TEXT("The track is not valid for racing: %s"), *TrackValidationReason);
		return false;
	}

	const URaceLapTracker* Primary = GetPrimaryLapTracker();
	if (Primary == nullptr)
	{
		OutReason = TEXT("No lap tracker is registered, so no lap could be counted or timed.");
		return false;
	}

	if (!Primary->IsConfigured())
	{
		OutReason = TEXT("The primary lap tracker has no track snapshot; call ConfigureTrack/ConfigureFromTrack.");
		return false;
	}

	// DELIBERATELY NOT CHECKED HERE: build-ID authority, the car version, the input
	// device. Those are SUBMISSION requirements (FRacingRaceResult::IsSubmittable). A
	// developer build produces a Derived, non-authoritative build ID by construction, and
	// refusing to start a session on that basis would make this project unable to race on
	// the machine that develops it. The two bars are different on purpose.

	OutReason.Reset();
	return true;
}

bool URaceResultRecorder::FreezeResult()
{
	if (Result.bFrozen)
	{
		// FREEZE ONCE. Docs/03-TrackRaceUI.md's Finished state, in as many words. The
		// state machine already guarantees Finished has no self-edge and that a redundant
		// FinishRace() neither runs an entry action nor broadcasts, so this cannot be
		// reached through the delegate -- but FreezeResult() is public, and "the result is
		// frozen once" must be a property of the result, not of who called it.
		UE_LOG(LogRacingRace, Verbose,
			TEXT("Result is already frozen for session %d; ignoring a second freeze."), Result.SessionId);
		return false;
	}

	// Re-read the track through its cache at the freeze, so a result records the track's
	// validity as of the run rather than as of whenever SetTrack() happened to be called.
	RefreshTrackSnapshot();

	Result = AssembleResult();
	Result.bFrozen = true;

	UE_LOG(LogRacingRace, Display, TEXT("Race result frozen: %s"), *Result.ToString());
	return true;
}

void URaceResultRecorder::ClearForNewSession()
{
	// EVERY registered tracker, not just the primary. RACE-002 built
	// ResetForNewSession() and nothing called it from a state-machine transition; this is
	// that call site. It is idempotent at both ends, so a doubled restart is a no-op
	// rather than a second, subtly different clear.
	for (const TObjectPtr<URaceLapTracker>& Tracker : LapTrackers)
	{
		if (Tracker != nullptr)
		{
			Tracker->ResetForNewSession();
		}
	}

	// Prune collected entries. Without this a tracker whose other owner released it would
	// leave a null in the array forever -- a stale entry that GetPrimaryLapTracker() has
	// to skip past on every read and that makes GetNumLapTrackers() report a car that no
	// longer exists.
	const int32 RemovedCount = LapTrackers.RemoveAll(
		[](const TObjectPtr<URaceLapTracker>& Tracker) { return Tracker == nullptr; });

	// THE PREVIOUS RUN'S RESULT IS DROPPED. A HUD reading GetFrozenResult() during the new
	// PreRace/Countdown must see "no result", not the last run's final time sitting there
	// looking live -- the same failure FRacingTelemetryFrame::IsStaleAt exists to prevent
	// for a telemetry frame, and the reason that function treats a future-stamped frame as
	// stale. Assigning a default-constructed struct also clears bFrozen, which is what
	// every consumer checks first.
	Result = FRacingRaceResult();

	UE_LOG(LogRacingRace, Verbose,
		TEXT("Result recorder cleared for a new session: %d tracker(s) reset, %d stale entr(ies) pruned, "
			 "frozen result dropped."),
		LapTrackers.Num(), RemovedCount);
}

// ===========================================================================
// Assembly
// ===========================================================================

ERacingRunValidity URaceResultRecorder::ResolveOverallValidity(const URaceLapTracker* Primary) const
{
	// COARSE-GRAINING, NOT DERIVATION. Every input below is a terminal answer some other
	// system already committed to; this only decides which of them the RUN publishes when
	// they disagree. The order is by severity of the claim, strongest evidence first.

	if (Primary == nullptr)
	{
		// No tracker means no lap was ever validated, whatever the clock says.
		return ERacingRunValidity::InvalidIncomplete;
	}

	// 1. A SESSION-LEVEL FAULT OUTRANKS EVERY LAP. URaceLapTracker::GetRunValidity()
	//    is Pending in normal operation and InvalidIncomplete once a timing fault has been
	//    observed (RACE-001 M4: a race clock that refused to start). A session whose clock
	//    never ran cannot publish a valid lap even if the gates were all taken, because
	//    the lap's duration is a fiction.
	const ERacingRunValidity RunValidity = Primary->GetRunValidity();
	if (RunValidity != ERacingRunValidity::Pending && RunValidity != ERacingRunValidity::Unknown)
	{
		return RunValidity;
	}

	// 2. A VALID LAP MAKES THE RUN VALID. This is a time-trial rule and it is worth
	//    stating: a session with one clean lap and three ruined ones is a VALID run that
	//    set a time. The ruined laps are not hidden -- LapsCompleted and
	//    ValidLapsCompleted are both published and RACE-002's own counter-case warns they
	//    must not be conflated -- but the run produced a comparable lap, which is the
	//    question this field answers.
	if (Primary->GetValidLapsCompleted() > 0)
	{
		return ERacingRunValidity::Valid;
	}

	// 3. NO VALID LAP, BUT LAPS HAPPENED. Publish the last completed lap's own verdict, so
	//    the result says WHY the run produced nothing -- a shortcut, a reverse crossing, a
	//    reset -- rather than collapsing three different driving mistakes into "incomplete".
	const FRacingLapTiming LastCompleted = Primary->GetLastCompletedLap();
	if (LastCompleted.LapNumber > 0
		&& LastCompleted.Validity != ERacingRunValidity::Pending
		&& LastCompleted.Validity != ERacingRunValidity::Unknown)
	{
		return LastCompleted.Validity;
	}

	// 4. NO LAP CLOSED AT ALL. The car never completed a lap: it finished on its out-lap,
	//    abandoned, or -- the RACE-002 R2-M1 case -- drove outside every gate so no line
	//    crossing ever became a lap boundary. InvalidIncomplete is exactly that: "Run did
	//    not finish (abandoned, disconnected, session ended)".
	return ERacingRunValidity::InvalidIncomplete;
}

FRacingRaceResult URaceResultRecorder::AssembleResult() const
{
	FRacingRaceResult Assembled;

	// -- Metadata this build already knows ---------------------------------
	//
	// Build ID, engine patch + changelist, physics policy version and the configured
	// assist preset. MakeCurrent() deliberately leaves the track, car, ruleset, input
	// device and validity unpopulated -- "an unpopulated field is honest; a guessed one is
	// a silently wrong leaderboard" -- and the four lines below are this ticket filling in
	// the ones it is the owner of.
	Assembled.Version = FRacingSimVersionStamp::MakeCurrent();
	Assembled.Version.TrackVersion = TrackVersion;
	Assembled.Version.CarSpecVersion = CarSpecVersion;
	Assembled.Version.InputDeviceType = InputDeviceType;

	if (StateMachine != nullptr)
	{
		if (const URaceRulesetDataAsset* Ruleset = StateMachine->GetRuleset())
		{
			// CORE-002 reserved RulesetVersion with the comment "Populated by
			// RACE-001/RACE-002 from the ruleset asset"; neither had a result to put it on.
			// This is that slot filled, from the asset's own hash rather than from anything
			// this file knows about rules.
			Assembled.Version.RulesetVersion = Ruleset->GetContentVersion();
		}

		Assembled.SessionId = StateMachine->GetSessionId();

		// Peek, not Sample. The clock is already stopped and Stop() raised the ratchet to
		// the frozen value, so Peek() IS the final time -- and sampling here would advance
		// a monotonic ratchet from a result-assembly path, which is the exact split
		// RACE-001 exposed the two functions for.
		Assembled.FinalTimeSeconds = StateMachine->PeekRaceElapsedSeconds();
		Assembled.bRaceClockFaulted = StateMachine->HasRaceClockFault();
	}

	// -- Lap truth, copied from the one system allowed to decide it --------
	const URaceLapTracker* Primary = GetPrimaryLapTracker();
	if (Primary != nullptr)
	{
		Assembled.BestLap = Primary->GetBestValidLap();
		Assembled.LastLap = Primary->GetLastCompletedLap();
		Assembled.LapsCompleted = Primary->GetLapsCompleted();
		Assembled.ValidLapsCompleted = Primary->GetValidLapsCompleted();
		Assembled.CurrentLapNumberAtFinish = Primary->GetCurrentLapNumber();
	}

	// -- Track facts, as of the freeze --------------------------------------
	Assembled.TrackSectorCount = TrackSectorCount;
	Assembled.bTrackValidated = bTrackValidated;
	Assembled.TrackValidationReason = TrackValidationReason;

	// Last, because it reads several of the fields above.
	Assembled.Version.Validity = ResolveOverallValidity(Primary);

	// PENALTIES ARE LEFT CLEAN, STATED RATHER THAN ASSUMED. Nothing in this project issues
	// a penalty yet -- there is no track-limits system, no collision rule and no penalty
	// producer of any kind -- so FRacingPenaltySummary's default (zero penalties, zero
	// seconds, DominantReason None) is the truthful value, not a placeholder. The first
	// ticket that issues a penalty owns wiring it into Version.Penalties here.

	return Assembled;
}
