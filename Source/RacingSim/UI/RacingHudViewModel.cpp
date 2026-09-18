// Copyright RacingSim. All Rights Reserved.

#include "UI/RacingHudViewModel.h"

#include "Core/RacingTelemetryFunctionLibrary.h"

namespace RacingHudViewModelPrivate
{
	// Named rather than anonymous: unity builds concatenate translation units.

	/** A non-finite double never reaches a widget; it reads as 0. */
	double HudFiniteNonNegative(const double Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.0, Value) : 0.0;
	}

	/** Largest countdown the HUD will render; keeps the int32 cast defined. */
	constexpr double HudMaxDisplayCountdownSeconds = 3600.0;
}

FRacingHudViewModel URacingHudViewModelLibrary::BuildHudViewModel(
	const FRacingHudRaceInputs& Inputs,
	const FRacingVehicleTelemetrySample& Vehicle,
	const double NowSeconds,
	const double VehicleStaleAfterSeconds,
	const ERacingSpeedDisplayUnit SpeedUnit)
{
	FRacingHudViewModel ViewModel;
	BuildHudViewModelInto(Inputs, Vehicle, NowSeconds, VehicleStaleAfterSeconds, SpeedUnit, ViewModel);
	return ViewModel;
}

void URacingHudViewModelLibrary::BuildHudViewModelInto(
	const FRacingHudRaceInputs& Inputs,
	const FRacingVehicleTelemetrySample& Vehicle,
	const double NowSeconds,
	const double VehicleStaleAfterSeconds,
	const ERacingSpeedDisplayUnit SpeedUnit,
	FRacingHudViewModel& OutViewModel)
{
	using namespace RacingHudViewModelPrivate;

	// Start from defaults every time: no field may survive from the previous frame, which
	// is what keeps a restart from leaving the last session's numbers on screen.
	OutViewModel = FRacingHudViewModel();
	FRacingHudViewModel& Out = OutViewModel;

	// -- Session --------------------------------------------------------------
	Out.bHasRaceState = Inputs.bHasRaceState;
	Out.RaceState = Inputs.RaceState;
	Out.SessionId = Inputs.SessionId;
	Out.RaceElapsedSeconds = HudFiniteNonNegative(Inputs.RaceElapsedSeconds);
	Out.bRaceClockFaulted = Inputs.bRaceClockFaulted;

	// -- Countdown ------------------------------------------------------------
	Out.bShowCountdown = Inputs.bHasRaceState && Inputs.RaceState == ERaceState::Countdown;
	if (Out.bShowCountdown)
	{
		Out.CountdownRemainingSeconds = HudFiniteNonNegative(Inputs.CountdownRemainingSeconds);
		Out.CountdownWholeSeconds = GetCountdownWholeSeconds(Out.CountdownRemainingSeconds);
	}

	// -- Vehicle --------------------------------------------------------------
	// Fail closed: any non-finite input, the staleness limit included, reads as stale.
	// FRacingTelemetryFrame::IsTimestampStaleAt is the one staleness rule, shared with
	// the frame's own IsStaleAt.
	const bool bVehicleInputsFinite = FMath::IsFinite(NowSeconds)
		&& FMath::IsFinite(VehicleStaleAfterSeconds)
		&& FMath::IsFinite(Vehicle.TimestampSeconds)
		&& FMath::IsFinite(Vehicle.ForwardSpeedCms)
		&& FMath::IsFinite(Vehicle.EngineRPM);

	Out.SpeedUnit = SpeedUnit;
	Out.bVehicleDataFresh = bVehicleInputsFinite
		&& !FRacingTelemetryFrame::IsTimestampStaleAt(Vehicle.TimestampSeconds, NowSeconds, VehicleStaleAfterSeconds);
	if (Out.bVehicleDataFresh)
	{
		Out.Speed = URacingTelemetryFunctionLibrary::ConvertSpeedCmsToDisplayUnit(Vehicle.ForwardSpeedCms, SpeedUnit);
		Out.EngineRPM = FMath::Max(0.0f, Vehicle.EngineRPM);
		Out.GearIndex = Vehicle.GearIndex;
	}

	// -- Laps -----------------------------------------------------------------
	// Passed through, never re-derived (RACE-002 L9).
	Out.CurrentLapNumber = FMath::Max(0, Inputs.CurrentLapNumber);
	Out.LapsCompleted = FMath::Max(0, Inputs.LapsCompleted);
	Out.ValidLapsCompleted = FMath::Max(0, Inputs.ValidLapsCompleted);
	Out.NumSectors = FMath::Max(0, Inputs.NumSectors);

	Out.bLapInProgress = Inputs.bLapInProgress;
	if (Out.bLapInProgress)
	{
		Out.CurrentLapElapsedSeconds = HudFiniteNonNegative(Inputs.CurrentLapElapsedSeconds);
		Out.bCurrentLapInvalid = Inputs.bCurrentLapInvalid;
		Out.CurrentSectorNumber = (Inputs.CurrentSectorIndex >= 0 && Inputs.CurrentSectorIndex < Out.NumSectors)
			? Inputs.CurrentSectorIndex + 1
			: 0;
	}

	Out.bHasLastLap = Inputs.bHasLastLap && FMath::IsFinite(Inputs.LastLapSeconds);
	if (Out.bHasLastLap)
	{
		Out.LastLapSeconds = FMath::Max(0.0, Inputs.LastLapSeconds);
		Out.bLastLapValid = Inputs.bLastLapValid;
		Out.bLastLapSectorsConsistent = Inputs.bLastLapSectorsConsistent;
	}

	// Best lap only from the best-lap fields. Never borrowed from the last lap.
	Out.bHasBestLap = Inputs.bHasBestLap && FMath::IsFinite(Inputs.BestLapSeconds);
	Out.BestLapSeconds = Out.bHasBestLap ? FMath::Max(0.0, Inputs.BestLapSeconds) : 0.0;

	Out.LapProgressFraction = FMath::IsFinite(Inputs.LapProgressFraction)
		? FMath::Clamp(Inputs.LapProgressFraction, 0.0f, 1.0f)
		: 0.0f;

	// -- Position -------------------------------------------------------------
	// Docs/03-TrackRaceUI.md: position is shown only when opponents exist.
	Out.CompetitorCount = FMath::Max(0, Inputs.CompetitorCount);
	Out.bShowPosition = Out.CompetitorCount > 1
		&& Inputs.RacePosition > 0
		&& Inputs.RacePosition <= Out.CompetitorCount;
	Out.RacePosition = Out.bShowPosition ? Inputs.RacePosition : 0;

	// -- Delta ----------------------------------------------------------------
	Out.bHasDelta = Inputs.bHasDelta && FMath::IsFinite(Inputs.DeltaToBestSeconds);
	Out.DeltaToBestSeconds = Out.bHasDelta ? Inputs.DeltaToBestSeconds : 0.0;

	// -- Results --------------------------------------------------------------
	Out.bResultAvailable = Inputs.bResultAvailable;
	if (Out.bResultAvailable)
	{
		Out.ResultFinalTimeSeconds = HudFiniteNonNegative(Inputs.ResultFinalTimeSeconds);
		Out.ResultValidity = Inputs.ResultValidity;
		Out.ResultLapsCompleted = FMath::Max(0, Inputs.ResultLapsCompleted);
		Out.ResultValidLapsCompleted = FMath::Max(0, Inputs.ResultValidLapsCompleted);
		Out.bResultHasBestLap = Inputs.bResultHasBestLap && FMath::IsFinite(Inputs.ResultBestLapSeconds);
		Out.ResultBestLapSeconds = Out.bResultHasBestLap ? FMath::Max(0.0, Inputs.ResultBestLapSeconds) : 0.0;
		Out.bResultClockFaulted = Inputs.bResultClockFaulted;
	}

	Out.bShowResults = Out.bResultAvailable
		&& Inputs.bHasRaceState
		&& (Inputs.RaceState == ERaceState::Finished || Inputs.RaceState == ERaceState::Results);
}

int32 URacingHudViewModelLibrary::GetCountdownWholeSeconds(const double RemainingSeconds)
{
	if (!FMath::IsFinite(RemainingSeconds) || RemainingSeconds <= 0.0)
	{
		return 0;
	}

	const double Clamped = FMath::Min(RemainingSeconds, RacingHudViewModelPrivate::HudMaxDisplayCountdownSeconds);
	return static_cast<int32>(FMath::CeilToDouble(Clamped));
}
