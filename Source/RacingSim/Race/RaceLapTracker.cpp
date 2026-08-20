// Copyright RacingSim. All Rights Reserved.

#include "Race/RaceLapTracker.h"

#include "Core/RacingSimLog.h"
#include "Race/RaceRulesetDataAsset.h"
#include "Race/RaceStateMachine.h"
#include "Race/TrackDefinitionActor.h"

namespace RaceLapTrackerPrivate
{
	/**
	 * How many crossing events one step is expected to produce before the buffer spills
	 * to the heap.
	 *
	 * A step normally produces zero or one. Eight covers a long hitch through a chicane
	 * plus the sector boundaries in the same window, which is the realistic worst case;
	 * anything beyond it still WORKS, it just allocates, and CLAUDE.md's rule is about
	 * the per-frame path rather than about the pathological one.
	 */
	constexpr int32 EventInlineCapacity = 8;

	/** Kinds of thing that can happen inside one evaluation step, in time order. */
	enum class EStepEventType : uint8
	{
		GateCrossing,
		SectorBoundary
	};

	/**
	 * One thing that happened during a step, with WHERE in the step it happened.
	 *
	 * The whole reason this intermediate representation exists: a single step can cross
	 * a gate and a sector boundary, or two gates, and the ORDER they are applied in is
	 * the order they physically occurred -- ascending alpha -- not gate-index order and
	 * not "gates first, then sectors". Applying them in the wrong order attributes a
	 * sector split to the wrong lap at the one place it matters most, the start/finish
	 * line.
	 *
	 * ON THE TWO ALPHAS. A gate's alpha is a fraction of the straight motion SEGMENT
	 * (TRACK-002 computes it from the plane intersection); a sector boundary's alpha is
	 * a fraction of the ARC-LENGTH travelled. They are the same parameterisation to
	 * first order and agree exactly at 0 and 1. They are used here only to order events
	 * within one step and to interpolate a time inside it -- never to measure a distance
	 * -- so the first-order agreement is sufficient and the difference is bounded by the
	 * chord error of a single step.
	 */
	struct FStepEvent
	{
		double Alpha = 0.0;
		EStepEventType Type = EStepEventType::GateCrossing;

		/** GateCrossing: the crossing. SectorBoundary: unused. */
		FRacingGateCrossingResult Crossing;

		/** SectorBoundary: which boundary, 1-based (boundary k starts sector k). */
		int32 BoundaryIndex = INDEX_NONE;
	};

	bool IsFiniteVector(const FVector& V)
	{
		return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
	}

	FString ReasonName(const ERaceLapInvalidReason Reason)
	{
		const UEnum* EnumType = StaticEnum<ERaceLapInvalidReason>();
		const FString Name = EnumType ? EnumType->GetNameStringByValue(static_cast<int64>(Reason)) : FString();
		return Name.IsEmpty() ? FString::Printf(TEXT("<%d>"), static_cast<int32>(Reason)) : Name;
	}
}

// ===========================================================================
// FRaceLapInvalidity
// ===========================================================================

ERacingRunValidity FRaceLapInvalidity::ToRunValidity() const
{
	switch (Reason)
	{
	case ERaceLapInvalidReason::None:
		return ERacingRunValidity::Valid;

	case ERaceLapInvalidReason::MissedCheckpoint:
		// ERacingRunValidity::InvalidShortcut's own comment: "An ordered checkpoint gate
		// was missed, or a gate order violation was detected."
		return ERacingRunValidity::InvalidShortcut;

	case ERaceLapInvalidReason::ReverseFinishCrossing:
		return ERacingRunValidity::InvalidReverseCrossing;

	case ERaceLapInvalidReason::VehicleReset:
		return ERacingRunValidity::InvalidVehicleReset;

	case ERaceLapInvalidReason::TimingUnavailable:
		// No dedicated Core enumerator exists, and RACE-002 deliberately did not add
		// one: ERacingRunValidity is a CORE-002 contract that UI/, results and any
		// future leaderboard read, and appending to it to describe a fault that is
		// unreachable with the shipped platform clock would change a published
		// vocabulary for a case nobody can produce. InvalidIncomplete is the honest
		// coarse answer -- the run did not produce a time -- and the precise reason is
		// carried in ERaceLapInvalidReason, which is what a diagnostic reads.
		return ERacingRunValidity::InvalidIncomplete;

	default:
		return ERacingRunValidity::Unknown;
	}
}

FString FRaceLapInvalidity::ToDebugString() const
{
	using namespace RaceLapTrackerPrivate;

	if (IsClean())
	{
		return TEXT("valid");
	}

	if (GateIndex != INDEX_NONE)
	{
		// The gate is NAMED. "Invalid lap" tells a driver nothing and tells a support
		// engineer less; "missed Gate.02 (index 2)" is actionable and is what the ticket
		// requires of the skipped-gate case specifically.
		return FString::Printf(TEXT("%s at gate %d ('%s')"), *ReasonName(Reason), GateIndex, *GateId.ToString());
	}

	return ReasonName(Reason);
}

// ===========================================================================
// Construction and configuration
// ===========================================================================

URaceLapTracker* URaceLapTracker::Create(UObject* Owner, URaceStateMachine* InStateMachine, URaceRulesetDataAsset* InRuleset)
{
	if (Owner == nullptr)
	{
		UE_LOG(LogRacingRace, Error, TEXT("URaceLapTracker::Create requires an owner to outer the tracker to."));
		return nullptr;
	}

	if (InStateMachine == nullptr)
	{
		// Refused rather than defaulted. A tracker with no clock would happily count
		// laps and stamp every one of them 0.000, which is worse than not existing:
		// CLAUDE.md requires a monotonic server-side time source for lap timing, and
		// there is exactly one in this project.
		UE_LOG(LogRacingRace, Error, TEXT("URaceLapTracker::Create requires a URaceStateMachine; lap timing has no other clock."));
		return nullptr;
	}

	URaceLapTracker* Tracker = NewObject<URaceLapTracker>(Owner);
	Tracker->StateMachine = InStateMachine;
	Tracker->Ruleset = InRuleset;
	Tracker->ObservedSessionId = InStateMachine->GetSessionId();
	return Tracker;
}

bool URaceLapTracker::ConfigureTrack(
	const FRacingCheckpointGateSet& InGates,
	const TArrayView<const double> InSectorStartDistancesCm,
	const double InTrackLengthCm,
	FString& OutError)
{
	// Validate everything before mutating anything, the same commit-at-the-bottom
	// discipline FTrackCenterline::Build and FRacingCheckpointGateSet::Build use: a
	// rejected reconfiguration must leave a working tracker working.
	if (!InGates.IsValid())
	{
		OutError = TEXT("Cannot configure a lap tracker against an unbuilt checkpoint gate set.");
		return false;
	}

	if (InGates.NumGates() < MinOrderedGateCount)
	{
		OutError = FString::Printf(
			TEXT("A lap tracker needs at least %d ordered gates; got %d. With fewer there is no order to enforce, "
				 "so every crossing is a crossing of the only gate and no out-of-order lap is detectable. "
				 "(Whether a track has ENOUGH gates to make a shortcut detectable is a stricter, separate "
				 "question, answered by ATrackDefinitionActor::MinCheckpointGateCount.)"),
			MinOrderedGateCount, InGates.NumGates());
		return false;
	}

	if (!FMath::IsFinite(InTrackLengthCm) || InTrackLengthCm <= 0.0)
	{
		OutError = FString::Printf(TEXT("Track length must be finite and positive, got %f cm."), InTrackLengthCm);
		return false;
	}

	{
		const FRacingCheckpointGate* StartFinish = InGates.GetGate(FRacingCheckpointGateSet::StartFinishGateIndex);
		if (StartFinish == nullptr)
		{
			OutError = TEXT("The gate set has no start/finish gate.");
			return false;
		}

		if (StartFinish->LegalDirection == ERacingGateDirection::Reverse)
		{
			// A reverse-only line can never be crossed forwards, so no lap could ever
			// open, and the tracker would sit silently at lap 0 for the whole session.
			// ATrackDefinitionActor::Validate() rejects this too; checked here as well
			// because a tracker can be configured from a hand-built set in a test or a
			// tool, where nothing ran that validation.
			OutError = FString::Printf(
				TEXT("Start/finish gate '%s' is Reverse-only, so a forward lap could never be completed."),
				*StartFinish->GateId.ToString());
			return false;
		}
	}

	for (int32 Index = 0; Index < InSectorStartDistancesCm.Num(); ++Index)
	{
		const double StartCm = InSectorStartDistancesCm[Index];

		if (!FMath::IsFinite(StartCm))
		{
			OutError = FString::Printf(TEXT("Sector start %d is not finite."), Index);
			return false;
		}

		if (Index == 0 && !FMath::IsNearlyZero(StartCm, 1.0e-4))
		{
			OutError = FString::Printf(
				TEXT("Sector start 0 must be exactly 0 (sector 0 begins at the start/finish line); got %f cm."), StartCm);
			return false;
		}

		if (Index > 0 && StartCm <= InSectorStartDistancesCm[Index - 1])
		{
			OutError = FString::Printf(
				TEXT("Sector starts must strictly increase; [%d] (%f cm) <= [%d] (%f cm)."),
				Index, StartCm, Index - 1, InSectorStartDistancesCm[Index - 1]);
			return false;
		}

		if (StartCm >= InTrackLengthCm)
		{
			OutError = FString::Printf(
				TEXT("Sector start %d is %f cm, at or past the lap length %f cm."), Index, StartCm, InTrackLengthCm);
			return false;
		}
	}

	// Commit.
	Gates = InGates;
	SectorStartDistancesCm.Reset(InSectorStartDistancesCm.Num());
	SectorStartDistancesCm.Append(InSectorStartDistancesCm.GetData(), InSectorStartDistancesCm.Num());
	TrackLengthCm = InTrackLengthCm;

	GateSatisfied.SetNumUninitialized(Gates.NumGates());
	SectorStartTimesSeconds.Reserve(FMath::Max(1, SectorStartDistancesCm.Num()));

	// A reconfiguration is a new race on a new track: nothing about the previous one
	// may survive it, least of all a gate-crossed flag indexed into a different set.
	ResetForNewSession();
	return true;
}

bool URaceLapTracker::ConfigureFromTrack(const ATrackDefinitionActor* Track, FString& OutError)
{
	if (Track == nullptr)
	{
		OutError = TEXT("Cannot configure a lap tracker from a null track.");
		return false;
	}

	if (!Track->IsTrackDataBuilt())
	{
		OutError = FString::Printf(
			TEXT("Track '%s' has no baked centerline, so it can supply neither gates nor a lap length."),
			*Track->TrackId.ToString());
		return false;
	}

	// The ONE call that touches the actor. Everything afterwards runs off the snapshot
	// taken here, which is TRACK-002's finding L5 (every gate query through the actor is
	// game-thread-only) closed by construction.
	return ConfigureTrack(
		Track->GetCheckpointGates(),
		Track->SectorStartDistancesCm,
		Track->GetTrackLengthCm(),
		OutError);
}

// ===========================================================================
// Session lifecycle
// ===========================================================================

void URaceLapTracker::ResetForNewSession()
{
	// Element-by-element rather than Reset()/SetNum, so a restart allocates nothing and
	// the array's length stays pinned to the configured gate count. A restart that
	// resized this array would be a restart that could leave a stale flag at an index
	// the new session then reads.
	for (int32 Index = 0; Index < GateSatisfied.Num(); ++Index)
	{
		GateSatisfied[Index] = false;
	}

	SectorStartTimesSeconds.Reset();

	ValidLapsCompleted = 0;
	LapsCompleted = 0;
	CurrentLapNumber = 0;
	ExpectedGateIndex = Gates.IsValid() ? FRacingCheckpointGateSet::StartFinishGateIndex : INDEX_NONE;
	CurrentSectorIndex = 0;
	bLapInProgress = false;
	LapOpenTimeSeconds = 0.0;
	CurrentLapInvalidity = FRaceLapInvalidity();
	LastCompletedLap = FRacingLapTiming();
	BestValidLap = FRacingLapTiming();
	RunValidity = ERacingRunValidity::Pending;

	PreviousWorldLocationCm = FVector::ZeroVector;
	PreviousDistanceCm = 0.0;
	PreviousTimeSeconds = 0.0;

	// THE IMPORTANT ONE. Dropping the previous sample is what stops the first step of
	// the new session sweeping a segment from wherever the car was when the old session
	// ended -- which, on a restart from mid-race, is a segment across most of the
	// circuit and through every gate on it.
	bHasPreviousSample = false;

	if (StateMachine != nullptr)
	{
		ObservedSessionId = StateMachine->GetSessionId();
	}
}

void URaceLapTracker::SeedProgress(const FVector& WorldLocationCm, const double CenterlineDistanceCm)
{
	using namespace RaceLapTrackerPrivate;

	if (!IsFiniteVector(WorldLocationCm) || !FMath::IsFinite(CenterlineDistanceCm))
	{
		UE_LOG(LogRacingRace, Warning,
			TEXT("URaceLapTracker::SeedProgress refused a non-finite sample; the previous position is unchanged."));
		return;
	}

	AcceptSample(WorldLocationCm, CenterlineDistanceCm);
}

void URaceLapTracker::AcceptSample(const FVector& WorldLocationCm, const double CenterlineDistanceCm)
{
	PreviousWorldLocationCm = WorldLocationCm;
	PreviousDistanceCm = TrackLengthCm > 0.0
		? FMath::Fmod(FMath::Fmod(CenterlineDistanceCm, TrackLengthCm) + TrackLengthCm, TrackLengthCm)
		: CenterlineDistanceCm;
	PreviousTimeSeconds = StateMachine != nullptr ? StateMachine->PeekRaceElapsedSeconds() : 0.0;
	bHasPreviousSample = true;
}

void URaceLapTracker::NotifyVehicleReset(const FVector& ResetWorldLocationCm, const double ResetSampleDistanceCm)
{
	using namespace RaceLapTrackerPrivate;

	if (!IsFiniteVector(ResetWorldLocationCm) || !FMath::IsFinite(ResetSampleDistanceCm))
	{
		// Do NOT silently keep the old sample: the car has moved and the next segment
		// would sweep. Invalidate the lap and drop the sample, so the next Advance()
		// re-seeds instead of evaluating a segment that spans the teleport.
		UE_LOG(LogRacingRace, Warning,
			TEXT("URaceLapTracker::NotifyVehicleReset was given a non-finite pose; progress dropped and the lap invalidated."));
		MarkLapInvalid(ERaceLapInvalidReason::VehicleReset, INDEX_NONE, NAME_None);
		bHasPreviousSample = false;
		return;
	}

	// THE RESET POLICY, DECIDED AND WRITTEN DOWN RATHER THAN LEFT IMPLICIT.
	//
	// Docs/03-TrackRaceUI.md rule 8 delegates it: reset "may invalidate or penalize the
	// lap according to the ruleset". The ruleset's default is to INVALIDATE, and with no
	// ruleset configured the same default applies, because:
	//
	//   - the reset pose is a position the car did not drive to. TRACK-001's
	//     GetResetTransformAtOrBeforeDistanceCm can only move a car BACKWARDS along the
	//     route, so a reset never awards distance -- but it does award a clean, on-line,
	//     stationary restart from a moment the driver had lost the car, and a lap
	//     containing that is not comparable with one that does not;
	//   - failing open would make "reset" the cheapest way to survive a mistake on a
	//     timed lap, which is exactly the reset-awards-progress failure Gate B names;
	//   - the opposite default cannot be recovered from by a downstream consumer: a lap
	//     wrongly marked valid is indistinguishable from a clean one by the time it
	//     reaches a result, whereas a lap wrongly marked invalid is at least visible.
	//
	// Set URaceRulesetDataAsset::bResetInvalidatesLap to false for a practice ruleset
	// where resets are free.
	const bool bInvalidates = (Ruleset == nullptr) || Ruleset->bResetInvalidatesLap;
	if (bInvalidates)
	{
		MarkLapInvalid(ERaceLapInvalidReason::VehicleReset, INDEX_NONE, NAME_None);
	}

	// Re-seed from the RESET SAMPLE'S OWN ARC LENGTH, never from the stale pre-reset
	// progress, and never by re-deriving it with a global nearest-point search. This is
	// the call TRACK-001 added GetResetSampleDistanceCm() for.
	AcceptSample(ResetWorldLocationCm, ResetSampleDistanceCm);

	// The sector cursor is deliberately NOT rewound, even though the car may have been
	// placed behind a boundary it had already crossed. Sector splits must telescope to
	// the lap time (FRacingLapTiming::AreSectorsConsistent), so a boundary may be timed
	// exactly once per lap; re-timing one would produce two splits for one sector and a
	// sum that no longer matches. The lap is invalid under the default policy anyway.
	UE_LOG(LogRacingRace, Verbose,
		TEXT("Lap tracker re-seeded by a vehicle reset at %f cm (lap %d, invalidated: %s)."),
		PreviousDistanceCm, CurrentLapNumber, bInvalidates ? TEXT("yes") : TEXT("no"));
}

// ===========================================================================
// The step
// ===========================================================================

FRaceLapTrackerUpdate URaceLapTracker::Advance(const FVector& WorldLocationCm, const double CenterlineDistanceCm)
{
	using namespace RaceLapTrackerPrivate;

	FRaceLapTrackerUpdate Update;

	if (!IsConfigured() || StateMachine == nullptr)
	{
		return Update;
	}

	if (!IsFiniteVector(WorldLocationCm) || !FMath::IsFinite(CenterlineDistanceCm))
	{
		// "I could not look" is not "nothing happened" -- the same distinction
		// FRacingGateCrossingResult::bEvaluated draws. The previous sample is left
		// alone so the next good sample still has a segment to test against, but the
		// step is not treated as evaluated.
		UE_LOG(LogRacingRace, Warning, TEXT("URaceLapTracker::Advance refused a non-finite sample."));
		return Update;
	}

	// -- A restart underneath us -------------------------------------------
	//
	// Defensive, not primary: the owner is expected to call ResetForNewSession(). But a
	// session id that has moved means the clock was re-zeroed, and continuing to
	// subtract a lap-open time taken in the previous session would produce a negative
	// or wildly wrong lap. RACE-001's own doc comment recommends exactly this pattern.
	const int32 SessionId = StateMachine->GetSessionId();
	if (SessionId != ObservedSessionId)
	{
		UE_LOG(LogRacingRace, Verbose,
			TEXT("Lap tracker observed session %d -> %d and cleared its state."), ObservedSessionId, SessionId);
		ResetForNewSession();
	}

	ObserveClockFault();

	// One reading for the whole step, so every event interpolated below shares one
	// endpoint. Sampling per event would let two events in one step be measured against
	// two different "now"s.
	const double NowSeconds = StateMachine->GetRaceElapsedSeconds();

	// Lap logic runs ONLY while racing. Docs/03-TrackRaceUI.md: Finished "disable[s]
	// further lap counting", and PreRace/Countdown have not released drive input. The
	// sample is still accepted, so the first racing step tests a segment from where the
	// car actually was rather than from wherever it last was during Racing.
	if (StateMachine->GetRaceState() != ERaceState::Racing)
	{
		AcceptSample(WorldLocationCm, CenterlineDistanceCm);
		return Update;
	}

	if (!bHasPreviousSample)
	{
		// See SeedProgress(): the first step of a session has no segment, only a point.
		AcceptSample(WorldLocationCm, CenterlineDistanceCm);
		return Update;
	}

	Update.bEvaluated = true;

	// -- Unannounced teleport guard -----------------------------------------
	//
	// NotifyVehicleReset() is the supported path and every in-project reset uses it.
	// This catches the case where something moves a car without saying so -- a debug
	// SetActorLocation, a future spectator/pit path, a physics blow-up followed by a
	// world re-spawn. Sweeping that segment would hand out gate crossings for track the
	// car never drove, which is the one failure mode that manufactures a VALID lap out
	// of nothing.
	//
	// TWO QUANTITIES, because neither alone is sufficient -- see
	// GetImplausibleArcStepCm/GetImplausibleChordStepCm. The arc test catches a jump
	// along the route (the one that would sweep gates within their extents and award
	// false progress); the chord test catches a jump through space that barely moves the
	// arc length (the far side of a hairpin).
	const double StepChordCm = FVector::Dist(PreviousWorldLocationCm, WorldLocationCm);

	// Computed once and reused by the sector-boundary sweep below, so the guard and the
	// arithmetic it protects can never disagree about how far the car went.
	const double SignedTravelCm = SignedDistanceDeltaCm(PreviousDistanceCm, CenterlineDistanceCm);
	const double StepArcCm = FMath::Abs(SignedTravelCm);

	if (StepArcCm > GetImplausibleArcStepCm() || StepChordCm > GetImplausibleChordStepCm())
	{
		UE_LOG(LogRacingRace, Warning,
			TEXT("Lap tracker saw a %f cm arc / %f cm straight-line step, beyond the %f cm / %f cm plausibility "
				 "bounds; treating it as an unannounced teleport, invalidating lap %d and re-seeding progress."),
			StepArcCm, StepChordCm, GetImplausibleArcStepCm(), GetImplausibleChordStepCm(), CurrentLapNumber);

		MarkLapInvalid(ERaceLapInvalidReason::VehicleReset, INDEX_NONE, NAME_None);
		AcceptSample(WorldLocationCm, CenterlineDistanceCm);
		Update.bTeleportDetected = true;
		return Update;
	}

	// -- Collect everything that happened, with WHEN inside the step --------
	TArray<FStepEvent, TInlineAllocator<EventInlineCapacity>> Events;

	// EvaluateCrossings, never FindFirstCrossing. TRACK-002's finding M3, discharged at
	// the one call site that matters: FindFirstCrossing returns the earliest PLANE
	// crossing including OutsideExtent, so a car that goes wide past gate 1 and then
	// cleanly through gate 2 would come back as gate 1 -- a near-miss silently promoted
	// to the through-gate event that authorises order. Every order-relevant decision in
	// this class is made from the full visitor sweep.
	Gates.EvaluateCrossings(PreviousWorldLocationCm, WorldLocationCm,
		[&Events, &Update](const FRacingGateCrossingResult& Result)
		{
			if (Result.Crossing == ERacingGateCrossing::OutsideExtent)
			{
				++Update.NearMissCount;

				// TRACK-002 finding L4: OutsideExtent carries no direction of its own,
				// but the signed-distance pair does -- forward is "started behind the
				// plane". Read here rather than adding an accessor to the crossing
				// result, because this is the only site in the project that acts on a
				// near-miss and a one-line derivation beats a new API surface.
				const bool bForwardNearMiss = Result.SignedDistanceFromCm < 0.0;
				UE_LOG(LogRacingRace, Verbose,
					TEXT("Near miss: gate %d ('%s') plane crossed %s, %f cm lateral / %f cm vertical off centre."),
					Result.GateIndex, *Result.GateId.ToString(),
					bForwardNearMiss ? TEXT("forwards") : TEXT("backwards"),
					Result.CrossingLateralOffsetCm, Result.CrossingVerticalOffsetCm);
				return;
			}

			if (!Result.IsThroughGate())
			{
				return;
			}

			FStepEvent Event;
			Event.Alpha = Result.CrossingAlpha;
			Event.Type = EStepEventType::GateCrossing;
			Event.Crossing = Result;
			Events.Add(Event);
		});

	// Sector boundaries, in ARC-LENGTH space. Progress, never authority: this decides
	// where a split is taken inside a lap the GATES authorised, and it can neither open,
	// close, count nor validate a lap. Core's FRacingProgressSample states the same rule
	// from the other side ("Spline distance in particular ranks cars; it never
	// authorises a lap").
	if (bLapInProgress && SignedTravelCm > 0.0 && SectorStartDistancesCm.Num() > 1)
	{
		for (int32 Boundary = CurrentSectorIndex + 1; Boundary < SectorStartDistancesCm.Num(); ++Boundary)
		{
			const double ToBoundaryCm = ForwardDistanceDeltaCm(PreviousDistanceCm, SectorStartDistancesCm[Boundary]);

			// Half-open, matching the gate plane's own sign rule: arriving exactly ON a
			// boundary counts as having crossed it, and standing there does not re-cross
			// it on the next step.
			if (ToBoundaryCm <= 0.0 || ToBoundaryCm > SignedTravelCm)
			{
				break;
			}

			FStepEvent Event;
			Event.Alpha = ToBoundaryCm / SignedTravelCm;
			Event.Type = EStepEventType::SectorBoundary;
			Event.BoundaryIndex = Boundary;
			Events.Add(Event);
		}
	}

	// -- Apply them in the order they physically happened --------------------
	if (Events.Num() > 1)
	{
		Events.Sort([](const FStepEvent& A, const FStepEvent& B) { return A.Alpha < B.Alpha; });
	}

	for (const FStepEvent& Event : Events)
	{
		const double EventTimeSeconds = FMath::Lerp(PreviousTimeSeconds, NowSeconds, Event.Alpha);

		if (Event.Type == EStepEventType::SectorBoundary)
		{
			// Re-checked at APPLY time, not just at collection time: a lap may have
			// opened earlier in this same step, which resets the sector cursor and makes
			// a boundary collected against the previous lap's cursor no longer the one
			// being waited for. Skipping it leaves that lap's sector set incomplete,
			// which CloseLap() reports honestly rather than papering over.
			if (Event.BoundaryIndex != CurrentSectorIndex + 1 || !bLapInProgress)
			{
				UE_LOG(LogRacingRace, Verbose,
					TEXT("Sector boundary %d ignored: the tracker is waiting for boundary %d."),
					Event.BoundaryIndex, CurrentSectorIndex + 1);
				continue;
			}

			CurrentSectorIndex = Event.BoundaryIndex;
			SectorStartTimesSeconds.Add(EventTimeSeconds);
			++Update.SectorsClosed;
			continue;
		}

		const FRacingGateCrossingResult& Crossing = Event.Crossing;
		const int32 GateIndex = Crossing.GateIndex;
		const bool bIsFinishGate = (GateIndex == FRacingCheckpointGateSet::StartFinishGateIndex);
		const bool bForward = (Crossing.Crossing == ERacingGateCrossing::Forward);

		// A crossing that does not match the gate's authored legal direction never
		// advances anything. For the ordinary Forward-only gate this is the reverse
		// crossing; for a Reverse-only or Bidirectional gate (a pit or observation gate)
		// it is whatever that gate forbids. Direction legality is TRACK-002's answer and
		// is not re-derived here.
		const bool bLegal = Crossing.bMatchesLegalDirection;

		if (bIsFinishGate)
		{
			if (bForward && bLegal)
			{
				if (bLapInProgress)
				{
					// THE FINISH LINE ALWAYS CLOSES THE LAP IN PROGRESS AND OPENS THE
					// NEXT ONE, valid or not.
					//
					// The alternative -- refusing to close an invalid lap -- was
					// rejected: a car that cuts the last corner would then never get a
					// lap boundary at the line, its timer would run on into the next lap,
					// and the HUD would show one endless lap. A shortcut lap is a lap
					// that happened and was not valid, which is what CloseLap records.
					FRacingLapTiming Closed;
					const bool bValid = CloseLap(EventTimeSeconds, Closed);

					Update.bLapClosed = true;
					Update.bLapCounted = bValid;
					Update.ClosedLap = Closed;
				}

				OpenLap(EventTimeSeconds);
				Update.bLapOpened = true;

				// The line is gate 0, and crossing it forwards satisfies it for the lap
				// that just opened.
				GateSatisfied[FRacingCheckpointGateSet::StartFinishGateIndex] = true;
				ExpectedGateIndex = Gates.GetNextGateIndex(FRacingCheckpointGateSet::StartFinishGateIndex);
				++Update.GatesAdvanced;
				continue;
			}

			// A REVERSE CROSSING OF THE LINE. Its own named reason, never folded into
			// MissedCheckpoint: CLAUDE.md calls out reverse finish crossings on their
			// own, and the ticket requires the two to stay distinguishable.
			//
			// Progress is NOT rewound here, and that is what keeps a spin on the line
			// from manufacturing laps. The car is behind the line again with gate 0
			// still marked; the next forward crossing therefore closes this (already
			// invalid) lap exactly once and opens exactly one new one, however many
			// times the car crosses back and forth.
			if (bLapInProgress)
			{
				MarkLapInvalid(ERaceLapInvalidReason::ReverseFinishCrossing, GateIndex, Crossing.GateId);
			}

			UE_LOG(LogRacingRace, Verbose,
				TEXT("Reverse crossing of the start/finish gate on lap %d; the lap cannot be counted."), CurrentLapNumber);
			continue;
		}

		// -- An ordered checkpoint gate -----------------------------------
		if (!bLapInProgress)
		{
			// Before the first line crossing there is no lap to make progress on. A car
			// rolling forward off the grid is behind the line, so this is either a
			// warm-up or an out-of-session movement; either way it advances nothing.
			continue;
		}

		if (bForward && bLegal)
		{
			if (GateIndex == ExpectedGateIndex)
			{
				GateSatisfied[GateIndex] = true;
				ExpectedGateIndex = Gates.GetNextGateIndex(GateIndex);
				++Update.GatesAdvanced;
				continue;
			}

			if (GateSatisfied[GateIndex])
			{
				// DOUBLE TRIGGER. Re-crossing a gate already taken this lap advances
				// nothing and counts nothing -- `.claude/rules/race-tests.md`'s
				// double-overlap case. Not an invalidity either: it is what a spin, a
				// recovery or a slow rejoin looks like.
				UE_LOG(LogRacingRace, Verbose,
					TEXT("Gate %d ('%s') re-crossed forwards on lap %d; already satisfied, progress unchanged."),
					GateIndex, *Crossing.GateId.ToString(), CurrentLapNumber);
				continue;
			}

			// OUT OF ORDER: a gate ahead of the expected one, with the expected one
			// never taken. Docs/03-TrackRaceUI.md rule 4. The reason names the gate that
			// was MISSED, not the one that was crossed -- the missed one is the fact the
			// driver needs.
			const FRacingCheckpointGate* Missed = Gates.GetGate(ExpectedGateIndex);
			MarkLapInvalid(
				ERaceLapInvalidReason::MissedCheckpoint,
				ExpectedGateIndex,
				Missed != nullptr ? Missed->GateId : NAME_None);

			// Progress still moves to the gate actually taken. The lap is already
			// invalid and cannot be repaired, but leaving the cursor parked on a gate the
			// car has driven past would make every later gate look out of order too, and
			// the lap would never close at the line.
			GateSatisfied[GateIndex] = true;
			ExpectedGateIndex = Gates.GetNextGateIndex(GateIndex);
			++Update.GatesAdvanced;
			continue;
		}

		// A reverse crossing of an ordinary gate: the car came back through it.
		//
		// If it is the gate just taken, progress rewinds by exactly one. That is what
		// makes a spin AT a gate net to a single forward pass no matter how long it
		// lasts, and it is deliberately the same net TRACK-002's own GateCurvedTrack
		// spin case asserts ("no two consecutive crossings share a direction and the net
		// is exactly one forward pass"). Building a different net on top of the same
		// crossing stream is how two layers end up disagreeing about the same spin.
		//
		// This is NOT an invalidity. Spinning is a racing incident, not a shortcut, and
		// the ticket only requires the FINISH gate's reverse crossing to be reported as
		// one. A car that spins and rejoins must still take every remaining gate in
		// order to complete a valid lap, which the rewound cursor enforces.
		const int32 ExpectedAfterThisGate = (GateIndex + 1) % Gates.NumGates();
		if (ExpectedGateIndex == ExpectedAfterThisGate && GateSatisfied[GateIndex])
		{
			GateSatisfied[GateIndex] = false;
			ExpectedGateIndex = GateIndex;

			// Signed, deliberately: see FRaceLapTrackerUpdate::GatesAdvanced. A caller
			// summing these across the steps of a spin must arrive at the same net
			// TRACK-002's crossing stream does, which is one.
			--Update.GatesAdvanced;
			continue;
		}

		UE_LOG(LogRacingRace, Verbose,
			TEXT("Reverse crossing of gate %d ('%s') on lap %d; not the gate last taken, so progress is unchanged."),
			GateIndex, *Crossing.GateId.ToString(), CurrentLapNumber);
	}

	AcceptSample(WorldLocationCm, CenterlineDistanceCm);
	PreviousTimeSeconds = NowSeconds;
	return Update;
}

// ===========================================================================
// Lap open/close
// ===========================================================================

void URaceLapTracker::OpenLap(const double TimeSeconds)
{
	for (int32 Index = 0; Index < GateSatisfied.Num(); ++Index)
	{
		GateSatisfied[Index] = false;
	}

	SectorStartTimesSeconds.Reset();
	if (SectorStartDistancesCm.Num() > 0)
	{
		// Sector 0 starts when the lap does. Recording it here is what makes the sector
		// splits telescope exactly to the lap duration.
		SectorStartTimesSeconds.Add(TimeSeconds);
	}

	CurrentSectorIndex = 0;
	bLapInProgress = true;
	LapOpenTimeSeconds = TimeSeconds;
	CurrentLapInvalidity = FRaceLapInvalidity();
	++CurrentLapNumber;

	// A clock that refused to start earlier taints every lap opened afterwards, not just
	// the one that was open at the time.
	if (RunValidity == ERacingRunValidity::InvalidIncomplete)
	{
		MarkLapInvalid(ERaceLapInvalidReason::TimingUnavailable, INDEX_NONE, NAME_None);
	}
}

bool URaceLapTracker::CloseLap(const double TimeSeconds, FRacingLapTiming& OutTiming)
{
	OutTiming = FRacingLapTiming();
	OutTiming.LapNumber = CurrentLapNumber;
	OutTiming.LapDurationSeconds = FMath::Max(0.0, TimeSeconds - LapOpenTimeSeconds);

	// Every ordered gate must have been taken forwards. The reason names the FIRST one
	// that was not -- lowest index, i.e. earliest on the lap -- because that is where the
	// lap actually went wrong; a later missing gate is usually a consequence.
	const int32 MissedIndex = FindFirstMissedGateIndex();
	if (MissedIndex != INDEX_NONE)
	{
		const FRacingCheckpointGate* Missed = Gates.GetGate(MissedIndex);
		MarkLapInvalid(
			ERaceLapInvalidReason::MissedCheckpoint,
			MissedIndex,
			Missed != nullptr ? Missed->GateId : NAME_None);
	}

	// A lap with no measurable duration cannot be published, whatever else was correct
	// about it. Reached when the authoritative clock refused to run (RACE-001 M4) or when
	// a lap opens and closes inside one evaluation step.
	if (!(OutTiming.LapDurationSeconds > 0.0))
	{
		MarkLapInvalid(ERaceLapInvalidReason::TimingUnavailable, INDEX_NONE, NAME_None);
	}

	// -- Sector splits ------------------------------------------------------
	//
	// Emitted only as a COMPLETE set. A partial set would sum to less than the lap and
	// AreSectorsConsistent() would fail on it -- correctly, but silently, at whichever
	// consumer happened to check. Publishing nothing and naming the fault is the honest
	// version of the same answer.
	const int32 NumSectors = SectorStartDistancesCm.Num();
	if (NumSectors > 0)
	{
		if (SectorStartTimesSeconds.Num() == NumSectors)
		{
			OutTiming.SectorDurationsSeconds.Reserve(NumSectors);
			for (int32 Index = 0; Index < NumSectors; ++Index)
			{
				const double EndSeconds = (Index + 1 < NumSectors) ? SectorStartTimesSeconds[Index + 1] : TimeSeconds;
				OutTiming.SectorDurationsSeconds.Add(EndSeconds - SectorStartTimesSeconds[Index]);
			}
		}
		else
		{
			MarkLapInvalid(ERaceLapInvalidReason::TimingUnavailable, INDEX_NONE, NAME_None);
			UE_LOG(LogRacingRace, Verbose,
				TEXT("Lap %d closed with %d of %d sector starts recorded; sector splits withheld."),
				CurrentLapNumber, SectorStartTimesSeconds.Num(), NumSectors);
		}
	}

	OutTiming.Validity = CurrentLapInvalidity.ToRunValidity();

	const bool bValid = CurrentLapInvalidity.IsClean();

	++LapsCompleted;
	if (bValid)
	{
		++ValidLapsCompleted;

		if (BestValidLap.LapNumber == 0 || OutTiming.LapDurationSeconds < BestValidLap.LapDurationSeconds)
		{
			BestValidLap = OutTiming;
		}
	}

	LastCompletedLap = OutTiming;
	bLapInProgress = false;

	UE_LOG(LogRacingRace, Display,
		TEXT("Lap %d closed in %.3fs (%s), %d sector split(s). Valid laps: %d of %d."),
		OutTiming.LapNumber, OutTiming.LapDurationSeconds, *CurrentLapInvalidity.ToDebugString(),
		OutTiming.SectorDurationsSeconds.Num(), ValidLapsCompleted, LapsCompleted);

	return bValid;
}

int32 URaceLapTracker::FindFirstMissedGateIndex() const
{
	for (int32 Index = 0; Index < GateSatisfied.Num(); ++Index)
	{
		if (!GateSatisfied[Index])
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

void URaceLapTracker::MarkLapInvalid(const ERaceLapInvalidReason Reason, const int32 GateIndex, const FName GateId)
{
	if (Reason == ERaceLapInvalidReason::None)
	{
		return;
	}

	// FIRST FAULT WINS. See FRaceLapInvalidity: which fault is reported must not depend
	// on how much further the car drove after ruining the lap.
	if (!CurrentLapInvalidity.IsClean())
	{
		return;
	}

	CurrentLapInvalidity.Reason = Reason;
	CurrentLapInvalidity.GateIndex = GateIndex;
	CurrentLapInvalidity.GateId = GateId;
}

void URaceLapTracker::ObserveClockFault()
{
	if (StateMachine == nullptr || !StateMachine->HasRaceClockFault())
	{
		return;
	}

	if (RunValidity == ERacingRunValidity::InvalidIncomplete)
	{
		return;
	}

	// RACE-001 FINDING M4, CLOSED HERE.
	//
	// URaceStateMachine::CommitTransition now checks FRaceClock::Start()/Stop()'s bool
	// return and latches a fault when the authoritative clock refuses. Before this, a
	// refused Start still entered Racing and froze a silent 0.000 result with nothing
	// marking the run. The run is now marked invalid the moment the fault is observed,
	// and every lap opened or closed afterwards carries TimingUnavailable.
	RunValidity = ERacingRunValidity::InvalidIncomplete;
	MarkLapInvalid(ERaceLapInvalidReason::TimingUnavailable, INDEX_NONE, NAME_None);

	UE_LOG(LogRacingRace, Error,
		TEXT("The authoritative race clock reported a fault; this run is marked invalid and no lap from it may be published."));
}

// ===========================================================================
// Arc-length helpers
// ===========================================================================

double URaceLapTracker::ForwardDistanceDeltaCm(const double FromCm, const double ToCm) const
{
	if (TrackLengthCm <= 0.0)
	{
		return 0.0;
	}

	const double Raw = FMath::Fmod(ToCm - FromCm, TrackLengthCm);
	return Raw < 0.0 ? Raw + TrackLengthCm : Raw;
}

double URaceLapTracker::SignedDistanceDeltaCm(const double FromCm, const double ToCm) const
{
	if (TrackLengthCm <= 0.0)
	{
		return 0.0;
	}

	// Shortest way round. A step longer than half a lap is not decidable this way, which
	// is exactly why Advance() rejects one as a teleport before it ever gets here -- the
	// same bound, used twice, rather than two numbers that can drift apart.
	const double Forward = ForwardDistanceDeltaCm(FromCm, ToCm);
	return (Forward > TrackLengthCm * 0.5) ? (Forward - TrackLengthCm) : Forward;
}

// ===========================================================================
// Reads
// ===========================================================================

bool URaceLapTracker::IsGateSatisfied(const int32 GateIndex) const
{
	return GateSatisfied.IsValidIndex(GateIndex) && GateSatisfied[GateIndex];
}

FRacingLapTiming URaceLapTracker::GetCurrentLapTiming() const
{
	FRacingLapTiming Timing;

	if (!bLapInProgress || StateMachine == nullptr)
	{
		return Timing;
	}

	Timing.LapNumber = CurrentLapNumber;

	// Peek, not Sample: a HUD repaint must not advance the authoritative clock's
	// monotonic ratchet. RACE-001 exposed both for exactly this split.
	Timing.LapDurationSeconds = FMath::Max(0.0, StateMachine->PeekRaceElapsedSeconds() - LapOpenTimeSeconds);

	// Splits closed so far, which is shorter than the sector count while the lap runs --
	// FRacingLapTiming::SectorDurationsSeconds documents that a consumer must not assume
	// a fixed length.
	for (int32 Index = 0; Index + 1 < SectorStartTimesSeconds.Num(); ++Index)
	{
		Timing.SectorDurationsSeconds.Add(SectorStartTimesSeconds[Index + 1] - SectorStartTimesSeconds[Index]);
	}

	// Pending unless something has already gone wrong: a lap in progress has no terminal
	// validity, but a lap already ruined should not read as if it might still count.
	Timing.Validity = CurrentLapInvalidity.IsClean()
		? ERacingRunValidity::Pending
		: CurrentLapInvalidity.ToRunValidity();

	return Timing;
}

FRacingProgressSample URaceLapTracker::GetProgressSample() const
{
	FRacingProgressSample Sample;

	Sample.TimestampSeconds = StateMachine != nullptr ? StateMachine->PeekRaceElapsedSeconds() : 0.0;
	Sample.LapNumber = CurrentLapNumber;

	// The last gate actually taken, which is the one before the expected one -- and
	// INDEX_NONE before any has been taken, matching the field's documented contract.
	Sample.LastCheckpointIndex = INDEX_NONE;
	for (int32 Index = GateSatisfied.Num() - 1; Index >= 0; --Index)
	{
		if (GateSatisfied[Index])
		{
			Sample.LastCheckpointIndex = Index;
			break;
		}
	}

	Sample.SplineDistanceCm = FMath::Max(0.0, PreviousDistanceCm);
	Sample.LapProgressFraction = TrackLengthCm > 0.0
		? static_cast<float>(FMath::Clamp(PreviousDistanceCm / TrackLengthCm, 0.0, 1.0))
		: 0.0f;

	// Position stays 0 ("not yet classified"): there are no opponents in this slice, and
	// Docs/03-TrackRaceUI.md requires position to be shown only when there are.
	Sample.RacePosition = 0;

	return Sample;
}
