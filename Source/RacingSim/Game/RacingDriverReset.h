// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ARaceDirector;
class ARacingVehiclePawn;

/** RACE-006: what servicing one frame's driver reset request did. */
enum class ERacingDriverResetOutcome : uint8
{
	/** No request was latched on the pawn. */
	NoRequest,
	/** The request was consumed and the car was placed. */
	Executed,
	/** The race session refused it (not Racing, not the competitor, no track or progress). */
	RefusedByRace,
	/** The vehicle refused it (cooldown, suppression still armed, unusable clock). */
	RefusedByVehicle,
	/** Both gates approved, but ExecuteSafeReset hit one of its documented no-ops. */
	NotPlaced,
};

namespace RacingSim::Game
{
	/**
	 * RACE-006: service one latched driver reset request.
	 *
	 * Game/ is the composition root: Race must not include Vehicle, and Vehicle never
	 * stores a Race pointer, so this is the one place that sees both halves. Order:
	 *   1. consume the pawn's latched request (a refused request is dropped, not
	 *      retried every frame -- the driver holds reset again);
	 *   2. ARaceDirector::CanResetCompetitor, which yields the last valid progress;
	 *   3. ARacingVehiclePawn::CanAcceptResetRequest (cooldown, detector basis);
	 *   4. ARacingVehiclePawn::ExecuteSafeReset with the director's track and tracker;
	 *   5. ARaceDirector::NotifyCompetitorReset.
	 *
	 * Logs once per serviced request (each refusal, each execution); never per frame,
	 * because NoRequest returns silently.
	 *
	 * @param Director  may be null (no race session): a latched request is then consumed
	 *                  and refused by the race, so it cannot fire later in a session.
	 * @param Vehicle   may be null: NoRequest.
	 * @param OutReason the refusal line; empty on NoRequest and Executed.
	 */
	RACINGSIM_API ERacingDriverResetOutcome ServiceDriverResetRequest(
		ARaceDirector* Director, ARacingVehiclePawn* Vehicle, FString& OutReason);

	/** Stable name for a log line or a test message. */
	RACINGSIM_API const TCHAR* LexDriverResetOutcome(ERacingDriverResetOutcome Outcome);
}
