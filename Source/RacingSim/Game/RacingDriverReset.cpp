// Copyright RacingSim. All Rights Reserved.

#include "Game/RacingDriverReset.h"

#include "Core/RacingSimLog.h"
#include "Race/RaceDirector.h"
#include "Vehicle/RacingVehiclePawn.h"

ERacingDriverResetOutcome RacingSim::Game::ServiceDriverResetRequest(
	ARaceDirector* Director, ARacingVehiclePawn* Vehicle, FString& OutReason)
{
	OutReason.Reset();

	if (!IsValid(Vehicle) || !Vehicle->ConsumeResetRequest())
	{
		return ERacingDriverResetOutcome::NoRequest;
	}

	double LastValidProgressDistanceCm = 0.0;
	if (!IsValid(Director))
	{
		OutReason = TEXT("no race session to reset within");
	}
	else if (!Director->CanResetCompetitor(Vehicle, LastValidProgressDistanceCm, OutReason))
	{
		// OutReason already set by the director.
	}
	else if (!Vehicle->CanAcceptResetRequest(OutReason))
	{
		UE_LOG(LogRacingCore, Log, TEXT("%s: driver reset refused by vehicle: %s"), *GetNameSafe(Vehicle), *OutReason);
		return ERacingDriverResetOutcome::RefusedByVehicle;
	}
	else if (!Vehicle->ExecuteSafeReset(Director->GetTrack(), Director->GetLapTracker(), LastValidProgressDistanceCm))
	{
		// ExecuteSafeReset has already logged which no-op it took.
		OutReason = TEXT("ExecuteSafeReset did not place the car");
		return ERacingDriverResetOutcome::NotPlaced;
	}
	else
	{
		Director->NotifyCompetitorReset(Vehicle);
		UE_LOG(LogRacingCore, Log, TEXT("%s: driver reset executed from progress %.1f cm."),
			*GetNameSafe(Vehicle), LastValidProgressDistanceCm);
		return ERacingDriverResetOutcome::Executed;
	}

	UE_LOG(LogRacingCore, Log, TEXT("%s: driver reset refused by race: %s"), *GetNameSafe(Vehicle), *OutReason);
	return ERacingDriverResetOutcome::RefusedByRace;
}

const TCHAR* RacingSim::Game::LexDriverResetOutcome(const ERacingDriverResetOutcome Outcome)
{
	switch (Outcome)
	{
	case ERacingDriverResetOutcome::NoRequest:        return TEXT("NoRequest");
	case ERacingDriverResetOutcome::Executed:         return TEXT("Executed");
	case ERacingDriverResetOutcome::RefusedByRace:    return TEXT("RefusedByRace");
	case ERacingDriverResetOutcome::RefusedByVehicle: return TEXT("RefusedByVehicle");
	case ERacingDriverResetOutcome::NotPlaced:        return TEXT("NotPlaced");
	}
	return TEXT("Unknown");
}
