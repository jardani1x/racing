// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/VehicleManoeuvreFixture.h"

#include "Game/RacingDriverReset.h"
#include "Race/RaceDirector.h"
#include "Race/RaceLapTracker.h"
#include "Race/RaceRulesetDataAsset.h"
#include "Race/RaceStateMachine.h"
#include "Race/TrackDefinitionActor.h"
#include "Vehicle/RacingVehiclePawn.h"
#include "Vehicle/VehicleFailureDetection.h"
#include "Vehicle/VehicleInputTypes.h"

#include "Components/SplineComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

/**
 * RACE-006: the whole driver reset path, end to end, on a real Chaos car.
 *
 * Input hold (UVehicleInputComponent) -> latch on the pawn (ApplyInputCommand) ->
 * RacingSim::Game::ServiceDriverResetRequest -> ARaceDirector::CanResetCompetitor ->
 * ARacingVehiclePawn::CanAcceptResetRequest -> ExecuteSafeReset with the director's
 * track and lap tracker -> ARaceDirector::NotifyCompetitorReset.
 *
 * The fixture's pawn is possessed by an AIController, not ARacingPlayerController, so
 * nothing services the latch behind the test's back: the test calls the service itself,
 * once per step it wants to observe, exactly as the controller's Tick does.
 *
 * ProductFilter: real world, real physics (Docs/Environment.md).
 */

namespace RacingDriverResetSpecPrivate
{
	/** 50 m circle on the fixture's 100 m slab; see VehicleResetUnderLoadSpec for why this size. */
	constexpr double DriverResetTrackRadiusCm = 5000.0;
	constexpr int32 DriverResetTrackSplinePoints = 12;

	/** Where the car is first placed, cm of arc. Not a reset-sample multiple, as in VehicleResetUnderLoadSpec. */
	constexpr double DriverResetPlacementCm = 12000.0;

	/** Upper bound on steps to complete one reset hold (0.5 s at 60 Hz is 30). */
	constexpr int32 MaxHoldSteps = 120;

	ATrackDefinitionActor* SpawnDriverResetTrack(FAutomationTestBase& Test, UWorld* World)
	{
		// Deferred: the fixture's world has already begun play, so a plain SpawnActor would
		// run BeginPlay on the default spline before the circle is authored.
		ATrackDefinitionActor* Track = World->SpawnActorDeferred<ATrackDefinitionActor>(
			ATrackDefinitionActor::StaticClass(), FTransform::Identity);
		if (Track == nullptr || Track->GetCenterlineSpline() == nullptr)
		{
			Test.AddError(TEXT("The track actor failed to spawn with a centerline spline."));
			return nullptr;
		}

		TArray<FVector> Points;
		for (int32 Index = 0; Index < DriverResetTrackSplinePoints; ++Index)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(Index) / static_cast<double>(DriverResetTrackSplinePoints);
			Points.Add(FVector(DriverResetTrackRadiusCm * FMath::Cos(Angle), DriverResetTrackRadiusCm * FMath::Sin(Angle), 0.0));
		}
		USplineComponent* Spline = Track->GetCenterlineSpline();
		Spline->SetClosedLoop(true, /*bUpdateSpline*/ false);
		Spline->SetSplinePoints(Points, ESplineCoordinateSpace::Local, /*bUpdateSpline*/ true);
		Track->TrackId = FName(TEXT("Track.Test.DriverReset"));
		Track->SectorStartDistancesCm = { 0.0 };
		UGameplayStatics::FinishSpawningActor(Track, FTransform::Identity);

		FString Reason;
		if (!Track->RebuildTrackData() || !Track->GetCachedValidation(Reason) || Track->GetNumResetSamples() <= 0)
		{
			Test.AddError(FString::Printf(TEXT("The driver reset test track failed to build: %s"), *Reason));
			return nullptr;
		}
		return Track;
	}

	ARaceDirector* SpawnDriverResetDirector(FAutomationTestBase& Test, UWorld* World, ATrackDefinitionActor* Track)
	{
		ARaceDirector* Director = World->SpawnActorDeferred<ARaceDirector>(ARaceDirector::StaticClass(), FTransform::Identity);
		if (Director == nullptr)
		{
			Test.AddError(TEXT("The race director failed to spawn."));
			return nullptr;
		}
		URaceRulesetDataAsset* Ruleset = NewObject<URaceRulesetDataAsset>(Director, NAME_None, RF_Transient);
		Ruleset->RulesetId = FName(TEXT("Ruleset.Test.DriverReset"));
		Ruleset->CountdownSeconds = 0.0;
		Ruleset->LapsToFinish = 3;
		Director->Ruleset = Ruleset;
		Director->Track = Track;
		Director->bAutoStartSession = false;
		UGameplayStatics::FinishSpawningActor(Director, FTransform::Identity);
		return Director;
	}

	FVehicleInputRawSample ResetHeldSample()
	{
		FVehicleInputRawSample Sample;
		Sample.bResetHeld = true;
		return Sample;
	}

	/**
	 * Hold the reset slot until the pawn latches a request, then release it with one
	 * neutral step (the latch on the pawn survives the release; the input layer's own
	 * one-shot needs the release before a second hold can fire).
	 *
	 * @return false when the hold never latched within MaxHoldSteps.
	 */
	bool HoldResetUntilLatched(FVehicleManoeuvreFixture& Fixture, ARacingVehiclePawn* Pawn)
	{
		for (int32 Step = 0; Step < MaxHoldSteps; ++Step)
		{
			Fixture.Drive(ResetHeldSample(), 1);
			if (Pawn->HasPendingResetRequest())
			{
				Fixture.Drive(FVehicleInputRawSample(), 1);
				return Pawn->HasPendingResetRequest();
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingDriverResetTest,
	"RacingSim.Game.DriverReset",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FRacingDriverResetTest::RunTest(const FString& Parameters)
{
	using namespace RacingDriverResetSpecPrivate;
	using RacingSim::Game::LexDriverResetOutcome;
	using RacingSim::Game::ServiceDriverResetRequest;

	// Telemetry ON: a driver reset must not trip the failure detector, and with capture
	// off the suppression half of the vehicle gate is not exercised at all.
	FVehicleManoeuvreFixture Fixture;
	if (!Fixture.Setup(*this, /*bEnableTelemetry*/ true))
	{
		return false;
	}
	ARacingVehiclePawn* Pawn = Fixture.GetPawn();
	UWorld* World = Fixture.GetWorld();
	ATrackDefinitionActor* Track = SpawnDriverResetTrack(*this, World);
	if (Pawn == nullptr || Track == nullptr)
	{
		return false;
	}

	// -- Place the car on the circle and register it ---------------------------------
	// The fixture spawns the car at the slab centre, which is the circle's centre: every
	// centerline point is equidistant from it. Place it on the track first, with the
	// same call a reset uses, then register it at that sample's distance.
	int32 PlacementSampleIndex = INDEX_NONE;
	double PlacementDistanceCm = 0.0;
	Track->GetResetPoseAtOrBeforeDistanceCm(DriverResetPlacementCm, PlacementSampleIndex, PlacementDistanceCm);
	if (!TestTrue(TEXT("The track resolves a placement sample"), PlacementSampleIndex != INDEX_NONE))
	{
		return false;
	}
	TestTrue(TEXT("The placement reset places the car"),
		Pawn->ExecuteSafeReset(Track, /*LapTracker*/ nullptr, DriverResetPlacementCm));

	// Settle past both halves of the vehicle gate (1.0 s cooldown, 0.5 s suppression budget).
	Fixture.Drive(FVehicleInputRawSample(), 90);

	ARaceDirector* Director = SpawnDriverResetDirector(*this, World, Track);
	if (Director == nullptr)
	{
		return false;
	}
	FString Reason;
	if (!TestTrue(TEXT("RegisterCompetitor accepts the placed car"), Director->RegisterCompetitor(Pawn, PlacementDistanceCm, Reason)))
	{
		AddInfo(FString::Printf(TEXT("RegisterCompetitor refused: %s"), *Reason));
		return false;
	}
	URaceStateMachine* StateMachine = Director->GetStateMachine();
	URaceLapTracker* LapTracker = Director->GetLapTracker();
	if (StateMachine == nullptr || LapTracker == nullptr)
	{
		AddError(TEXT("The director has no state machine or lap tracker after registration."));
		return false;
	}

	// -- No request -------------------------------------------------------------------
	TestFalse(TEXT("No request is latched before the driver holds reset"), Pawn->HasPendingResetRequest());
	TestEqual(TEXT("Servicing with nothing latched is NoRequest"),
		FString(LexDriverResetOutcome(ServiceDriverResetRequest(Director, Pawn, Reason))), FString(TEXT("NoRequest")));
	TestTrue(TEXT("NoRequest leaves no reason"), Reason.IsEmpty());
	TestEqual(TEXT("A null vehicle is NoRequest"),
		FString(LexDriverResetOutcome(ServiceDriverResetRequest(Director, nullptr, Reason))), FString(TEXT("NoRequest")));

	// -- PreRace: refused by the race, and the request is consumed ----------------------
	if (!TestTrue(TEXT("Holding reset latches a request (PreRace)"), HoldResetUntilLatched(Fixture, Pawn)))
	{
		return false;
	}
	TestEqual(TEXT("A PreRace reset is refused by the race"),
		FString(LexDriverResetOutcome(ServiceDriverResetRequest(Director, Pawn, Reason))), FString(TEXT("RefusedByRace")));
	TestTrue(FString::Printf(TEXT("The refusal names PreRace (reason: %s)"), *Reason), Reason.Contains(TEXT("PreRace")));
	TestFalse(TEXT("A refused request is consumed, not retried"), Pawn->HasPendingResetRequest());

	// -- No director: refused by the race ----------------------------------------------
	if (!TestTrue(TEXT("Holding reset latches a request (no director)"), HoldResetUntilLatched(Fixture, Pawn)))
	{
		return false;
	}
	TestEqual(TEXT("A reset with no race session is refused by the race"),
		FString(LexDriverResetOutcome(ServiceDriverResetRequest(nullptr, Pawn, Reason))), FString(TEXT("RefusedByRace")));
	TestFalse(TEXT("The no-director request is consumed"), Pawn->HasPendingResetRequest());

	// -- Racing: executed -------------------------------------------------------------
	if (!TestTrue(TEXT("StartSession applies BeginCountdown"), Director->StartSession(Reason)))
	{
		AddInfo(FString::Printf(TEXT("StartSession refused: %s"), *Reason));
		return false;
	}
	Fixture.Drive(FVehicleInputRawSample(), 2);
	if (!TestEqual(TEXT("A zero-second countdown goes green once the world ticks"), StateMachine->GetRaceState(), ERaceState::Racing))
	{
		return false;
	}

	if (!TestTrue(TEXT("Holding reset latches a request (Racing)"), HoldResetUntilLatched(Fixture, Pawn)))
	{
		return false;
	}
	const int32 LapsBeforeReset = LapTracker->GetLapsCompleted();
	const double ProgressBeforeResetCm = LapTracker->GetProgressDistanceCm();
	TestEqual(TEXT("A Racing reset is executed"),
		FString(LexDriverResetOutcome(ServiceDriverResetRequest(Director, Pawn, Reason))), FString(TEXT("Executed")));
	TestTrue(TEXT("Executed leaves no reason"), Reason.IsEmpty());
	TestFalse(TEXT("The executed request is consumed"), Pawn->HasPendingResetRequest());
	TestEqual(TEXT("A reset does not add a lap"), LapTracker->GetLapsCompleted(), LapsBeforeReset);
	TestTrue(FString::Printf(TEXT("A reset does not advance progress: %.1f cm after, %.1f cm before"),
			LapTracker->GetProgressDistanceCm(), ProgressBeforeResetCm),
		LapTracker->GetProgressDistanceCm() <= ProgressBeforeResetCm + 1.0);
	TestEqual(TEXT("The director's search hint follows the tracker after the reset"),
		Director->GetLastCompetitorDistanceCm(), LapTracker->GetProgressDistanceCm());

	// -- Immediately again: refused by the vehicle --------------------------------------
	// The hold is 0.5 s, inside the 1.0 s cooldown.
	if (!TestTrue(TEXT("Holding reset latches a request (inside the cooldown)"), HoldResetUntilLatched(Fixture, Pawn)))
	{
		return false;
	}
	TestEqual(TEXT("A second reset inside the cooldown is refused by the vehicle"),
		FString(LexDriverResetOutcome(ServiceDriverResetRequest(Director, Pawn, Reason))), FString(TEXT("RefusedByVehicle")));
	TestTrue(FString::Printf(TEXT("The refusal names the cooldown (reason: %s)"), *Reason), Reason.Contains(TEXT("CoolingDown")));
	TestFalse(TEXT("The refused request is consumed"), Pawn->HasPendingResetRequest());

	// -- Past the cooldown: executed again ------------------------------------------------
	Fixture.Drive(FVehicleInputRawSample(), 60);
	if (!TestTrue(TEXT("Holding reset latches a request (past the cooldown)"), HoldResetUntilLatched(Fixture, Pawn)))
	{
		return false;
	}
	TestEqual(TEXT("A reset past the cooldown is executed"),
		FString(LexDriverResetOutcome(ServiceDriverResetRequest(Director, Pawn, Reason))), FString(TEXT("Executed")));
	TestEqual(TEXT("Still no lap added"), LapTracker->GetLapsCompleted(), LapsBeforeReset);

	// Let the car settle after the last reset so the detector evaluates the post-reset contacts.
	Fixture.Drive(FVehicleInputRawSample(), 60);
	TestFalse(FString::Printf(TEXT("The failure detector stayed silent across driver resets (%s)"),
			*RacingSim::Vehicle::DescribeVehicleFailureFlags(Pawn->GetLastFailureReport().Flags)),
		Pawn->GetLastFailureReport().HasAnyFailure());
	TestFalse(TEXT("Every Drive step ticked"), Fixture.HasTickFailure());

	return true;
}
