// Copyright RacingSim. All Rights Reserved.

#include "Race/RaceFunctionLibrary.h"

#include "Race/RaceLapTracker.h"
#include "Race/RaceStateMachine.h"
#include "Race/TrackDefinitionActor.h"

// ---------------------------------------------------------------------------
// Frozen result
// ---------------------------------------------------------------------------

ERacingRunValidity URaceFunctionLibrary::GetResultValidity(const FRacingRaceResult& Result)
{
	return Result.GetValidity();
}

bool URaceFunctionLibrary::ResultHasValidLap(const FRacingRaceResult& Result)
{
	return Result.HasValidLap();
}

bool URaceFunctionLibrary::IsResultSubmittable(const FRacingRaceResult& Result, FString& OutReason)
{
	OutReason.Reset();
	return Result.IsSubmittable(&OutReason);
}

bool URaceFunctionLibrary::MakeResultSubmissionQueryString(
	const FRacingRaceResult& Result, FString& OutQuery, FString& OutReason)
{
	return Result.MakeSubmissionQueryString(OutQuery, OutReason);
}

FString URaceFunctionLibrary::ResultToString(const FRacingRaceResult& Result)
{
	return Result.ToString();
}

// ---------------------------------------------------------------------------
// Centerline struct
// ---------------------------------------------------------------------------

bool URaceFunctionLibrary::IsCenterlineValid(const FTrackCenterline& Centerline)
{
	return Centerline.IsValid();
}

double URaceFunctionLibrary::GetCenterlineLengthCm(const FTrackCenterline& Centerline)
{
	return Centerline.GetLengthCm();
}

double URaceFunctionLibrary::WrapCenterlineDistanceCm(const FTrackCenterline& Centerline, const double DistanceCm)
{
	return Centerline.WrapDistanceCm(DistanceCm);
}

double URaceFunctionLibrary::GetCenterlineSignedDistanceDeltaCm(
	const FTrackCenterline& Centerline, const double FromCm, const double ToCm)
{
	return Centerline.GetSignedDistanceDeltaCm(FromCm, ToCm);
}

float URaceFunctionLibrary::GetCenterlineLapProgressFraction(const FTrackCenterline& Centerline, const double DistanceCm)
{
	if (!Centerline.IsValid() || !FMath::IsFinite(DistanceCm))
	{
		return 0.0f;
	}

	// IsValid() guarantees a positive length.
	return static_cast<float>(FMath::Clamp(Centerline.WrapDistanceCm(DistanceCm) / Centerline.GetLengthCm(), 0.0, 1.0));
}

double URaceFunctionLibrary::GetQueryLateralOffsetCm(const FTrackCenterlineQuery& Query, bool& bOutValid)
{
	bOutValid = Query.bValid && Query.bLateralOffsetValid;
	return bOutValid ? Query.LateralOffsetCm : 0.0;
}

// ---------------------------------------------------------------------------
// Track actor
// ---------------------------------------------------------------------------

double URaceFunctionLibrary::GetTrackLengthCm(const ATrackDefinitionActor* Track)
{
	return IsValid(Track) ? Track->GetTrackLengthCm() : 0.0;
}

double URaceFunctionLibrary::GetTrackLengthMetres(const ATrackDefinitionActor* Track)
{
	return IsValid(Track) ? Track->GetTrackLengthMetres() : 0.0;
}

float URaceFunctionLibrary::GetTrackLapProgressFraction(const ATrackDefinitionActor* Track, const double DistanceCm)
{
	return IsValid(Track) ? GetCenterlineLapProgressFraction(Track->GetCenterline(), DistanceCm) : 0.0f;
}

double URaceFunctionLibrary::WrapTrackDistanceCm(const ATrackDefinitionActor* Track, const double DistanceCm)
{
	return IsValid(Track) ? Track->GetCenterline().WrapDistanceCm(DistanceCm) : 0.0;
}

double URaceFunctionLibrary::GetTrackSignedDistanceDeltaCm(
	const ATrackDefinitionActor* Track, const double FromCm, const double ToCm)
{
	return IsValid(Track) ? Track->GetCenterline().GetSignedDistanceDeltaCm(FromCm, ToCm) : 0.0;
}

// ---------------------------------------------------------------------------
// HUD data contract
// ---------------------------------------------------------------------------

bool URaceFunctionLibrary::GatherHudRaceInputs(
	const URaceStateMachine* StateMachine,
	const URaceLapTracker* LapTracker,
	const URaceResultRecorder* ResultRecorder,
	const int32 CompetitorCount,
	FRacingHudRaceInputs& OutInputs)
{
	// Reset first, so a caller reusing one struct across a restart can never read the
	// previous session's numbers through a field this call did not reach.
	OutInputs = FRacingHudRaceInputs();

	if (!IsValid(StateMachine))
	{
		return false;
	}

	OutInputs.bHasRaceState = true;
	OutInputs.RaceState = StateMachine->GetRaceState();
	OutInputs.SessionId = StateMachine->GetSessionId();
	// UI-001 L7: peeks only. A HUD read must never advance a race clock.
	OutInputs.CountdownRemainingSeconds = StateMachine->PeekCountdownRemainingSeconds();
	OutInputs.RaceElapsedSeconds = StateMachine->PeekRaceElapsedSeconds();
	OutInputs.bRaceClockFaulted = StateMachine->HasRaceClockFault();
	OutInputs.CompetitorCount = FMath::Max(0, CompetitorCount);

	// UI-001 L5: a tracker still holding another session's laps contributes nothing.
	if (IsValid(LapTracker) && LapTracker->GetObservedSessionId() == OutInputs.SessionId)
	{
		// L9: passed through exactly, never re-derived from one another.
		OutInputs.CurrentLapNumber = LapTracker->GetCurrentLapNumber();
		OutInputs.LapsCompleted = LapTracker->GetLapsCompleted();
		OutInputs.ValidLapsCompleted = LapTracker->GetValidLapsCompleted();
		OutInputs.bLapInProgress = LapTracker->IsLapInProgress();
		OutInputs.CurrentLapElapsedSeconds = LapTracker->GetCurrentLapElapsedSeconds();
		OutInputs.bCurrentLapInvalid = OutInputs.bLapInProgress && !LapTracker->GetCurrentLapInvalidity().IsClean();
		OutInputs.CurrentSectorIndex = LapTracker->GetCurrentSectorIndex();
		OutInputs.NumSectors = LapTracker->GetNumSectors();

		const FRacingLapTiming& LastLap = LapTracker->PeekLastCompletedLap();
		OutInputs.bHasLastLap = LastLap.LapNumber > 0;
		if (OutInputs.bHasLastLap)
		{
			OutInputs.LastLapSeconds = LastLap.LapDurationSeconds;
			OutInputs.bLastLapValid = LastLap.Validity == ERacingRunValidity::Valid;

			// RACE-003 M3: the real sector count, so a withheld split set reads false.
			OutInputs.bLastLapSectorsConsistent =
				LastLap.AreSectorsConsistent(/*ToleranceSeconds*/ 0.001, /*ExpectedSectorCount*/ OutInputs.NumSectors);
		}

		// Best VALID lap only. LastLap is never a fallback (RACE-002 L9).
		const FRacingLapTiming& BestLap = LapTracker->PeekBestValidLap();
		OutInputs.bHasBestLap = BestLap.LapNumber > 0;
		OutInputs.BestLapSeconds = OutInputs.bHasBestLap ? BestLap.LapDurationSeconds : 0.0;

		// FRacingProgressSample holds no container; returning it by value does not allocate.
		const FRacingProgressSample Progress = LapTracker->GetProgressSample();
		OutInputs.LapProgressFraction = Progress.LapProgressFraction;
		OutInputs.RacePosition = Progress.RacePosition;
	}

	if (IsValid(ResultRecorder) && ResultRecorder->HasFrozenResult())
	{
		const FRacingRaceResult& Result = ResultRecorder->GetFrozenResult();
		OutInputs.bResultAvailable = true;
		OutInputs.ResultFinalTimeSeconds = Result.FinalTimeSeconds;
		OutInputs.ResultValidity = Result.GetValidity();
		OutInputs.ResultLapsCompleted = Result.LapsCompleted;
		OutInputs.ResultValidLapsCompleted = Result.ValidLapsCompleted;
		OutInputs.bResultHasBestLap = Result.HasValidLap();
		OutInputs.ResultBestLapSeconds = OutInputs.bResultHasBestLap ? Result.BestLap.LapDurationSeconds : 0.0;
		OutInputs.bResultClockFaulted = Result.bRaceClockFaulted;
	}

	return true;
}
