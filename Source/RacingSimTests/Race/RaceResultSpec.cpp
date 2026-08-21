// Copyright RacingSim. All Rights Reserved.

#include "Race/RaceResult.h"
#include "Race/RaceLapTracker.h"
#include "Race/RaceRulesetDataAsset.h"
#include "Race/RaceStateMachine.h"
#include "Race/TrackCenterline.h"
#include "Race/TrackCheckpointGate.h"
#include "Race/TrackDefinitionActor.h"

#include "Components/SplineComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include <limits>

/**
 * RACE-003: the frozen result, the submission gate, and the restart cycle.
 *
 * ===========================================================================
 * No level, no spawned actor. Deliberately, and with one exception.
 * ===========================================================================
 *
 * TRACK-002 and RACE-002 set the precedent and Docs/Environment.md gives the hard reason:
 * a SmokeFilter test in this project cannot construct a NON-TEMPLATE Actor at all (the
 * typed-element registry is not registered when FEngineLoop::PreInit runs the smoke
 * tests), and the Product gate that could cannot complete on the reference machine. So the
 * session rig here is a procedurally built centerline, a gate set from arithmetic, and
 * UObject state machines on a fake monotonic clock -- exactly RACE-002's rig.
 *
 * THE EXCEPTION IS THE TRACK-VALIDITY GATE, and it matters enough to pay for. The
 * acceptance criterion is that a result carrying an INVALID TRACK is refused, and the
 * whole point of TRACK-002 finding M4 is that a track can bake "successfully" while its
 * GATES fail to bake. Asserting that against a hand-made snapshot would only prove the
 * recorder honours a bool it was handed. So RacingSim.Race.ResultTrackGate borrows the
 * ATrackDefinitionActor CDO -- which the engine constructs through its own path at module
 * load, and which TrackDefinitionActorSpec has used since TRACK-001 for the same reason --
 * and drives a REAL gate-bake failure through a REAL cached-validity lookup.
 *
 * The CDO is shared process-wide, so that suite snapshots every property it touches and
 * restores it, like the fixture it is modelled on.
 *
 * ===========================================================================
 * What is deliberately NOT tested here
 * ===========================================================================
 *
 * Lap ordering, sector timing, gate crossing direction and reset policy. RACE-002 and
 * TRACK-002 own all of it and re-asserting it here would create a second opinion about the
 * same rules. This suite consumes their answers and tests what is done WITH them.
 */

namespace RaceResultSpecPrivate
{
	// Named uniquely rather than placed in an anonymous namespace: unity builds concatenate
	// translation units, and duplicate anonymous symbols across a blob are a redefinition
	// rather than two file-local helpers.

	constexpr double ResultSpecCircleRadiusCm = 10000.0;   // 100 m radius, ~628 m lap
	constexpr int32 ResultSpecCircleSamples = 720;
	constexpr double ResultSpecGateHalfWidthCm = 900.0;
	constexpr double ResultSpecGateHalfHeightCm = 500.0;
	constexpr int32 ResultSpecStepsPerLap = 400;
	constexpr double ResultSpecSecondsPerStep = 0.016;

	/** The fake monotonic clock. Captureless so it converts to FRaceTimeSourceFn. */
	double GResultSpecNowSeconds = 0.0;
	double ResultSpecTimeSource()
	{
		return GResultSpecNowSeconds;
	}

	/** A clock that never returns a usable reading. Drives RACE-001's M4 fault latch. */
	double ResultSpecBrokenTimeSource()
	{
		return std::numeric_limits<double>::quiet_NaN();
	}

	/**
	 * A whole session: circle, four gates, three sectors, state machine, lap tracker and
	 * the result recorder wired to all of it.
	 *
	 * Everything is arithmetic; nothing traces any circuit (CLAUDE.md).
	 */
	struct FResultSpecRig
	{
		FTrackCenterline Circle;
		FRacingCheckpointGateSet Gates;
		TArray<double> SectorStartsCm;
		TStrongObjectPtr<URaceRulesetDataAsset> Ruleset;
		TStrongObjectPtr<URaceStateMachine> Machine;
		TStrongObjectPtr<URaceLapTracker> Tracker;
		TStrongObjectPtr<URaceResultRecorder> Recorder;

		double LapLengthCm = 0.0;
		double CurrentDistanceCm = 0.0;

		bool Build(FAutomationTestBase& Test, const bool bBrokenClock = false)
		{
			GResultSpecNowSeconds = 5000.0;

			const double TotalCm = 2.0 * UE_DOUBLE_PI * ResultSpecCircleRadiusCm;
			const double StepCm = TotalCm / static_cast<double>(ResultSpecCircleSamples);

			TArray<FVector> Locations;
			TArray<double> Distances;
			Locations.Reserve(ResultSpecCircleSamples);
			Distances.Reserve(ResultSpecCircleSamples);
			for (int32 Index = 0; Index < ResultSpecCircleSamples; ++Index)
			{
				const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(Index) / static_cast<double>(ResultSpecCircleSamples);
				Locations.Add(FVector(
					ResultSpecCircleRadiusCm * FMath::Cos(Angle),
					ResultSpecCircleRadiusCm * FMath::Sin(Angle),
					0.0));
				Distances.Add(static_cast<double>(Index) * StepCm);
			}

			FString Error;
			if (!Circle.Build(Locations, Distances, TotalCm, /*bClosedLoop*/ true, Error))
			{
				Test.AddError(FString::Printf(TEXT("Circle centerline failed to build: %s"), *Error));
				return false;
			}

			LapLengthCm = Circle.GetLengthCm();

			TArray<FRacingCheckpointGateSpec> Specs;
			for (int32 Index = 0; Index < 4; ++Index)
			{
				FRacingCheckpointGateSpec Spec;
				Spec.GateId = (Index == 0)
					? FName(TEXT("Gate.StartFinish"))
					: FName(*FString::Printf(TEXT("Gate.%02d"), Index));
				Spec.DistanceAlongCm = (Index == 0) ? 0.0 : LapLengthCm * static_cast<double>(Index) / 4.0;
				Spec.HalfWidthCm = ResultSpecGateHalfWidthCm;
				Spec.HalfHeightCm = ResultSpecGateHalfHeightCm;
				Spec.LegalDirection = ERacingGateDirection::Forward;
				Specs.Add(Spec);
			}

			if (!Gates.Build(Specs, Circle, ResultSpecCircleRadiusCm, Error))
			{
				Test.AddError(FString::Printf(TEXT("Gate set failed to build: %s"), *Error));
				return false;
			}

			SectorStartsCm = { 0.0, LapLengthCm / 3.0, LapLengthCm * 2.0 / 3.0 };

			Ruleset.Reset(NewObject<URaceRulesetDataAsset>(GetTransientPackage()));
			Ruleset->RulesetId = FName(TEXT("Ruleset.Test.Result"));
			Ruleset->CountdownSeconds = 3.0;

			Machine.Reset(URaceStateMachine::CreateWithTimeSource(
				GetTransientPackage(), Ruleset.Get(),
				bBrokenClock ? &ResultSpecBrokenTimeSource : &ResultSpecTimeSource));
			if (!Machine.IsValid())
			{
				Test.AddError(TEXT("URaceStateMachine::CreateWithTimeSource returned null."));
				return false;
			}

			Tracker.Reset(URaceLapTracker::Create(GetTransientPackage(), Machine.Get(), Ruleset.Get()));
			if (!Tracker.IsValid() || !Tracker->ConfigureTrack(Gates, SectorStartsCm, LapLengthCm, Error))
			{
				Test.AddError(FString::Printf(TEXT("Lap tracker failed to configure: %s"), *Error));
				return false;
			}

			Recorder.Reset(URaceResultRecorder::Create(GetTransientPackage(), Machine.Get()));
			if (!Recorder.IsValid())
			{
				Test.AddError(TEXT("URaceResultRecorder::Create returned null."));
				return false;
			}

			Recorder->RegisterLapTracker(Tracker.Get());
			ApplyValidTrackSnapshot();

			return true;
		}

		/** The level-free track metadata path: a validated, fully populated track. */
		void ApplyValidTrackSnapshot()
		{
			FRacingContentVersion TrackVersion;
			TrackVersion.AssetId = FName(TEXT("Track.Test.ResultCircle"));
			TrackVersion.SchemaVersion = ATrackDefinitionActor::TrackSchemaVersion;
			TrackVersion.ContentHash = 0xC1C1E000;

			Recorder->SetTrackSnapshot(TrackVersion, SectorStartsCm.Num(), /*bValidated*/ true, FString());
		}

		/** Everything a result needs to be SUBMITTABLE, short of the build ID. */
		void ApplyPublishableMetadata()
		{
			FRacingContentVersion CarVersion;
			CarVersion.AssetId = FName(TEXT("Car.Test.Prototype"));
			CarVersion.SchemaVersion = 1;
			CarVersion.ContentHash = 0x0CA20001;

			Recorder->SetCarSpecVersion(CarVersion);
			Recorder->SetInputDeviceType(ERacingInputDeviceType::Wheel);
		}

		FVector PositionAt(const double DistanceCm) const
		{
			return Circle.GetLocationAtDistanceCm(DistanceCm);
		}

		void StartRacing(const double GridDistanceCm)
		{
			CurrentDistanceCm = GridDistanceCm;
			Tracker->SeedProgress(PositionAt(GridDistanceCm), GridDistanceCm);
			Machine->BeginCountdown();
			GResultSpecNowSeconds += 3.0;
			Machine->StartRace();
		}

		void Drive(const double ToDistanceCm, const int32 Steps)
		{
			const double FromCm = CurrentDistanceCm;
			for (int32 Index = 1; Index <= Steps; ++Index)
			{
				GResultSpecNowSeconds += ResultSpecSecondsPerStep;
				const double Alpha = static_cast<double>(Index) / static_cast<double>(Steps);
				CurrentDistanceCm = FMath::Lerp(FromCm, ToDistanceCm, Alpha);
				Tracker->Advance(PositionAt(CurrentDistanceCm), CurrentDistanceCm);
			}
		}

		/** Grid -> green -> one clean lap -> finish. The ordinary happy path, once. */
		void DriveOneCleanLapAndFinish()
		{
			StartRacing(-800.0);
			Drive(400.0, 8);
			Drive(400.0 + LapLengthCm, ResultSpecStepsPerLap);
			Machine->FinishRace();
		}
	};

	/**
	 * Borrows the ATrackDefinitionActor CDO, authors a circle on it, and restores every
	 * property it touched.
	 *
	 * A trimmed sibling of TrackDefinitionActorSpec's FTrackSpecCircleFixture -- see that
	 * file's header for the full argument about why the CDO is the only actor a SmokeFilter
	 * test in this project can obtain. Only the fields this suite mutates are snapshotted,
	 * and the spline is restored in all four authored channels (locations, arrive tangents,
	 * leave tangents, point types), because a location-only restore looks correct in a diff
	 * and silently hands the next suite a different curve.
	 */
	struct FResultSpecTrackFixture
	{
		ATrackDefinitionActor* Track = nullptr;

		FResultSpecTrackFixture()
		{
			Track = GetMutableDefault<ATrackDefinitionActor>();
			if (!Track)
			{
				return;
			}

			SavedTrackId = Track->TrackId;
			SavedSampleSpacingCm = Track->CenterlineSampleSpacingCm;
			SavedSectorStartsCm = Track->SectorStartDistancesCm;
			SavedGateSpecs = Track->CheckpointGateSpecs;
			SavedNumGeneratedGates = Track->NumGeneratedCheckpointGates;
			SavedGateHalfWidthCm = Track->GeneratedGateHalfWidthCm;
			SavedGateHalfHeightCm = Track->GeneratedGateHalfHeightCm;
			SavedMinCornerRadiusCm = Track->MinCornerRadiusCm;

			if (USplineComponent* Spline = Track->GetCenterlineSpline())
			{
				bSavedClosedLoop = Spline->IsClosedLoop();
				const int32 Count = Spline->GetNumberOfSplinePoints();
				for (int32 Index = 0; Index < Count; ++Index)
				{
					SavedPoints.Add(Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local));
					SavedArrive.Add(Spline->GetArriveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local));
					SavedLeave.Add(Spline->GetLeaveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local));
					SavedTypes.Add(Spline->GetSplinePointType(Index));
				}
			}

			Track->TrackId = FName(TEXT("Track.Test.ResultGate"));
			AuthorCircle();
			Track->RebuildTrackData();
		}

		~FResultSpecTrackFixture()
		{
			if (!Track)
			{
				return;
			}

			Track->TrackId = SavedTrackId;
			Track->CenterlineSampleSpacingCm = SavedSampleSpacingCm;
			Track->SectorStartDistancesCm = SavedSectorStartsCm;
			Track->CheckpointGateSpecs = SavedGateSpecs;
			Track->NumGeneratedCheckpointGates = SavedNumGeneratedGates;
			Track->GeneratedGateHalfWidthCm = SavedGateHalfWidthCm;
			Track->GeneratedGateHalfHeightCm = SavedGateHalfHeightCm;
			Track->MinCornerRadiusCm = SavedMinCornerRadiusCm;

			if (USplineComponent* Spline = Track->GetCenterlineSpline())
			{
				Spline->SetClosedLoop(bSavedClosedLoop, /*bUpdateSpline*/ false);
				Spline->SetSplinePoints(SavedPoints, ESplineCoordinateSpace::Local, /*bUpdateSpline*/ false);

				const int32 Count = FMath::Min(SavedPoints.Num(), Spline->GetNumberOfSplinePoints());
				for (int32 Index = 0; Index < Count; ++Index)
				{
					Spline->SetTangentsAtSplinePoint(Index, SavedArrive[Index], SavedLeave[Index],
						ESplineCoordinateSpace::Local, /*bUpdateSpline*/ false);
					Spline->SetSplinePointType(Index, SavedTypes[Index], /*bUpdateSpline*/ false);
				}

				Spline->UpdateSpline();
			}

			Track->RebuildTrackData();
		}

		void AuthorCircle() const
		{
			USplineComponent* Spline = Track ? Track->GetCenterlineSpline() : nullptr;
			if (!Spline)
			{
				return;
			}

			TArray<FVector> Points;
			for (int32 Index = 0; Index < 12; ++Index)
			{
				const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(Index) / 12.0;
				Points.Add(FVector(
					ResultSpecCircleRadiusCm * FMath::Cos(Angle),
					ResultSpecCircleRadiusCm * FMath::Sin(Angle),
					0.0));
			}

			Spline->SetClosedLoop(true, /*bUpdateSpline*/ false);
			Spline->SetSplinePoints(Points, ESplineCoordinateSpace::Local, /*bUpdateSpline*/ true);
		}

		FResultSpecTrackFixture(const FResultSpecTrackFixture&) = delete;
		FResultSpecTrackFixture& operator=(const FResultSpecTrackFixture&) = delete;

	private:
		FName SavedTrackId;
		double SavedSampleSpacingCm = 0.0;
		TArray<double> SavedSectorStartsCm;
		TArray<FRacingCheckpointGateSpec> SavedGateSpecs;
		int32 SavedNumGeneratedGates = 0;
		double SavedGateHalfWidthCm = 0.0;
		double SavedGateHalfHeightCm = 0.0;
		double SavedMinCornerRadiusCm = 0.0;
		TArray<FVector> SavedPoints;
		TArray<FVector> SavedArrive;
		TArray<FVector> SavedLeave;
		TArray<ESplinePointType::Type> SavedTypes;
		bool bSavedClosedLoop = false;
	};
}

// ===========================================================================
// 1. The result is assembled ONCE at Finished, from existing accessors
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceResultFreezeTest,
	"RacingSim.Race.ResultFreeze",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceResultFreezeTest::RunTest(const FString& Parameters)
{
	using namespace RaceResultSpecPrivate;

	AddExpectedMessage(TEXT("URaceResultRecorder::Create requires"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/-1, /*IsRegex=*/false);

	// -- Create() refuses what it cannot time --------------------------------
	{
		FResultSpecRig Rig;
		if (!Rig.Build(*this))
		{
			return false;
		}

		TestNull(TEXT("A recorder cannot be created without an owner"),
			URaceResultRecorder::Create(nullptr, Rig.Machine.Get()));
		TestNull(TEXT("A recorder cannot be created without a state machine: nothing else owns the clock"),
			URaceResultRecorder::Create(GetTransientPackage(), nullptr));
	}

	FResultSpecRig Rig;
	if (!Rig.Build(*this))
	{
		return false;
	}

	// -- Before the finish there is NO result --------------------------------
	{
		TestFalse(TEXT("A fresh recorder has no frozen result"), Rig.Recorder->HasFrozenResult());
		TestFalse(TEXT("...and the struct it returns says so itself"),
			Rig.Recorder->GetFrozenResult().bFrozen);
		TestEqual(TEXT("...with one registered tracker"), Rig.Recorder->GetNumLapTrackers(), 1);
		TestTrue(TEXT("...observing its state machine"), Rig.Recorder->IsObservingStateMachine());
	}

	Rig.ApplyPublishableMetadata();

	// -- A session that starts, laps and finishes ----------------------------
	Rig.StartRacing(-800.0);
	Rig.Drive(400.0, 8);
	Rig.Drive(400.0 + Rig.LapLengthCm, ResultSpecStepsPerLap);
	Rig.Drive(400.0 + Rig.LapLengthCm * 2.0, ResultSpecStepsPerLap);

	TestFalse(TEXT("Mid-race, nothing has been frozen"), Rig.Recorder->HasFrozenResult());

	const double ExpectedFinalSeconds = Rig.Machine->GetRaceElapsedSeconds();
	const FRacingLapTiming ExpectedBest = Rig.Tracker->GetBestValidLap();
	const FRacingLapTiming ExpectedLast = Rig.Tracker->GetLastCompletedLap();

	// THE FREEZE INSTANT: the state-machine transition, not a poll.
	TestEqual(TEXT("FinishRace is applied"),
		Rig.Machine->FinishRace(), ERaceTransitionResult::Applied);
	TestTrue(TEXT("The transition into Finished froze the result, with no polling"),
		Rig.Recorder->HasFrozenResult());

	const FRacingRaceResult& Result = Rig.Recorder->GetFrozenResult();

	// -- Every field Docs/03-TrackRaceUI.md's Timing section commits to -------
	{
		TestTrue(TEXT("The result is marked frozen"), Result.bFrozen);

		// FINAL TIME, from the clock the Finished entry action already stopped. Compared
		// against the reading taken just before the transition: the fake clock did not
		// advance in between, so these must be equal, which is what proves the result is
		// the FROZEN time rather than a fresh sample.
		TestEqual(TEXT("Final time is the frozen race clock, not a re-sample"),
			Result.FinalTimeSeconds, ExpectedFinalSeconds, 1.0e-9);
		TestTrue(TEXT("...and it is a real duration"), Result.FinalTimeSeconds > 0.0);

		// BEST LAP and LAST LAP, ASSEMBLED from the tracker's existing accessors. Compared
		// field-for-field against what the tracker said BEFORE the freeze: if the recorder
		// re-derived anything, these would differ.
		TestEqual(TEXT("Best lap is URaceLapTracker::GetBestValidLap(), copied not re-derived"),
			Result.BestLap.LapNumber, ExpectedBest.LapNumber);
		TestEqual(TEXT("...with the same duration"),
			Result.BestLap.LapDurationSeconds, ExpectedBest.LapDurationSeconds, 1.0e-12);
		TestEqual(TEXT("Last lap is URaceLapTracker::GetLastCompletedLap()"),
			Result.LastLap.LapNumber, ExpectedLast.LapNumber);
		TestEqual(TEXT("...with the same duration"),
			Result.LastLap.LapDurationSeconds, ExpectedLast.LapDurationSeconds, 1.0e-12);

		// ALL SECTOR SPLITS, on both laps, complete.
		TestEqual(TEXT("The track authored three sectors and the result records that"),
			Result.TrackSectorCount, 3);
		TestEqual(TEXT("The best lap carries its full split set"),
			Result.BestLap.SectorDurationsSeconds.Num(), 3);
		TestTrue(TEXT("...telescoping to the lap, checked against the recorded sector count"),
			Result.BestLap.AreSectorsConsistent(0.001, Result.TrackSectorCount));
		TestEqual(TEXT("The last lap carries its full split set too"),
			Result.LastLap.SectorDurationsSeconds.Num(), 3);
		TestTrue(TEXT("...also telescoping"),
			Result.LastLap.AreSectorsConsistent(0.001, Result.TrackSectorCount));

		// OVERALL VALIDITY.
		TestEqual(TEXT("Two clean laps make the run Valid"),
			Result.GetValidity(), ERacingRunValidity::Valid);
		TestTrue(TEXT("...and the result reports it has a valid lap"), Result.HasValidLap());
		TestFalse(TEXT("...with no clock fault"), Result.bRaceClockFaulted);

		// TRACK VERSION, CAR TUNE VERSION, ASSIST STATE, BUILD ID.
		TestEqual(TEXT("Track version is recorded"),
			Result.Version.TrackVersion.AssetId, FName(TEXT("Track.Test.ResultCircle")));
		TestTrue(TEXT("...and is populated"), Result.Version.TrackVersion.IsPopulated());
		TestEqual(TEXT("Car tune version is recorded"),
			Result.Version.CarSpecVersion.AssetId, FName(TEXT("Car.Test.Prototype")));
		TestEqual(TEXT("Ruleset version is recorded, from the asset's own hash"),
			Result.Version.RulesetVersion.AssetId, FName(TEXT("Ruleset.Test.Result")));
		TestEqual(TEXT("...at the ruleset's schema version"),
			Result.Version.RulesetVersion.SchemaVersion, URaceRulesetDataAsset::RulesetSchemaVersion);
		TestTrue(TEXT("Build ID is recorded and non-empty"), !Result.Version.GameBuildId.Value.IsEmpty());
		TestTrue(TEXT("Engine version is recorded"), !Result.Version.EngineVersion.IsEmpty());
		TestEqual(TEXT("Input device is recorded"),
			Result.Version.InputDeviceType, ERacingInputDeviceType::Wheel);

		// Assist state comes from the project settings via MakeCurrent(); asserting a
		// PARTICULAR preset would be asserting a setting, so what is pinned is that the
		// result carries the same one the stamp would produce on its own -- i.e. that this
		// ticket did not overwrite or invent it.
		TestEqual(TEXT("Assist state is the stamp's, not one this ticket invented"),
			Result.Version.AssistPreset, FRacingSimVersionStamp::MakeCurrent().AssistPreset);

		// SESSION IDENTITY.
		TestEqual(TEXT("The session id is recorded"), Result.SessionId, Rig.Machine->GetSessionId());
		TestTrue(TEXT("The track validity as of the freeze is recorded"), Result.bTrackValidated);
	}

	// -- THE LAP-NUMBER CONVENTION (RACE-002 L9), as the result publishes it --
	{
		TestEqual(TEXT("L9: two laps ended at the line"), Result.LapsCompleted, 2);
		TestEqual(TEXT("L9: both were valid"), Result.ValidLapsCompleted, 2);
		TestEqual(TEXT("L9: and the car was on lap 3 when the flag fell"),
			Result.CurrentLapNumberAtFinish, 3);
		TestEqual(TEXT("L9: CurrentLapNumberAtFinish leads LapsCompleted by exactly one, because the "
					   "first line crossing OPENS lap 1 and closes nothing"),
			Result.CurrentLapNumberAtFinish, Result.LapsCompleted + 1);
	}

	// -- FROZEN MEANS FROZEN --------------------------------------------------
	{
		const double FrozenFinal = Result.FinalTimeSeconds;

		// Time passes, the presentation runs, someone calls FinishRace again, someone calls
		// FreezeResult by hand. None of it may move the result.
		GResultSpecNowSeconds += 30.0;
		TestEqual(TEXT("A second FinishRace is redundant, not a second freeze"),
			Rig.Machine->FinishRace(), ERaceTransitionResult::Redundant);
		TestFalse(TEXT("An explicit second FreezeResult() returns false: it froze nothing"),
			Rig.Recorder->FreezeResult());

		TestEqual(TEXT("ShowResults is applied"),
			Rig.Machine->ShowResults(), ERaceTransitionResult::Applied);
		TestEqual(TEXT("...and entering Results does NOT re-freeze: the displayed time cannot depend "
					   "on how long the finish presentation ran"),
			Rig.Recorder->GetFrozenResult().FinalTimeSeconds, FrozenFinal, 1.0e-12);

		// And 30 seconds of wall time later, the number is still the one from the flag.
		Rig.Machine->GetRaceElapsedSeconds();
		TestEqual(TEXT("...30 s and a clock sample later, still the same frozen time"),
			Rig.Recorder->GetFrozenResult().FinalTimeSeconds, FrozenFinal, 1.0e-12);
	}

	return true;
}

// ===========================================================================
// 2. RACE-001 M4: a session whose clock never ran cannot publish a Valid run
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceResultClockFaultTest,
	"RacingSim.Race.ResultClockFault",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceResultClockFaultTest::RunTest(const FString& Parameters)
{
	using namespace RaceResultSpecPrivate;

	AddExpectedMessage(TEXT("clock refused to start"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/-1, /*IsRegex=*/false);
	AddExpectedMessage(TEXT("clock was not running at the finish"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/-1, /*IsRegex=*/false);
	AddExpectedMessage(TEXT("authoritative race clock reported a fault"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/-1, /*IsRegex=*/false);
	AddExpectedMessage(TEXT("FRaceClock::Start rejected a non-finite timestamp"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/-1, /*IsRegex=*/false);

	FResultSpecRig Rig;
	if (!Rig.Build(*this, /*bBrokenClock*/ true))
	{
		return false;
	}

	Rig.ApplyPublishableMetadata();
	Rig.DriveOneCleanLapAndFinish();

	const FRacingRaceResult& Result = Rig.Recorder->GetFrozenResult();
	TestTrue(TEXT("The result still freezes -- a broken clock is reported, not hidden"), Result.bFrozen);
	TestTrue(TEXT("The clock fault is recorded on the result"), Result.bRaceClockFaulted);

	// THE POINT: a gate-perfect lap on a clock that never ran must NOT publish as Valid.
	// RACE-002 turns the state machine's fault latch into an InvalidIncomplete run validity;
	// this asserts the result carries that through rather than deciding for itself that two
	// clean gate sequences mean a valid run.
	TestEqual(TEXT("A gate-perfect session on a faulted clock is InvalidIncomplete, not Valid"),
		Result.GetValidity(), ERacingRunValidity::InvalidIncomplete);
	TestFalse(TEXT("...and reports no valid lap"), Result.HasValidLap());
	TestEqual(TEXT("...with no valid laps counted"), Result.ValidLapsCompleted, 0);

	// AND IT CANNOT BE SUBMITTED. Asserted on a copy whose OTHER submission blockers are
	// cleared, so that the validity is demonstrably the operative reason rather than merely
	// one of several. On a developer machine the build ID is Derived and non-authoritative,
	// which IsPublishable() reports first -- a bare IsSubmittable() check here would pass
	// for a reason that has nothing to do with the clock.
	FRacingRaceResult Probe = Result;
	Probe.Version.GameBuildId.Scheme = ERacingBuildIdScheme::Explicit;
	Probe.Version.GameBuildId.bIsAuthoritative = true;
	Probe.Version.GameBuildId.Value = TEXT("ci-clockfault-probe");
	Probe.Version.PhysicsPolicyVersion = 1;

	FString Reason;
	TestFalse(TEXT("...so it cannot be submitted, even with every other blocker cleared"),
		Probe.IsSubmittable(&Reason));

	// THE REASON NAMES THE CLOCK, not the validity, and that distinction is the finding.
	//
	// This assertion originally expected the validity to be the operative reason, and it
	// FAILED: ERacingRunValidity::InvalidIncomplete is a TERMINAL validity, so CORE-002's
	// IsPublishable() accepted it and the whole result was submittable. That is correct for
	// an ordinary invalid run -- a shortcut has real times and a void verdict, and a
	// leaderboard may record it struck through -- but a clock-faulted run has NO times, only
	// 0.000s, which is exactly the "zero-duration lap reaching a leaderboard as the fastest
	// ever driven" failure RACE-001 M4 exists to prevent. FRacingRaceResult::IsSubmittable()
	// gained a separate clock gate rather than this test being weakened to match.
	TestTrue(FString::Printf(TEXT("...and the operative reason names the CLOCK, separately from the "
		"validity, because a faulted clock leaves no time to publish at all: %s"), *Reason),
		Reason.Contains(TEXT("race clock faulted")));

	// The control that keeps the new gate honest: an ORDINARILY invalid run -- real times,
	// void verdict -- is still submittable. Without this, the clock gate could have been
	// written as "refuse anything not Valid" and passed the assertion above while quietly
	// removing every void attempt from the record.
	FRacingRaceResult VoidButTimed = Probe;
	VoidButTimed.bRaceClockFaulted = false;
	VoidButTimed.Version.Validity = ERacingRunValidity::InvalidShortcut;
	VoidButTimed.FinalTimeSeconds = 91.5;

	FString VoidReason;
	TestTrue(FString::Printf(TEXT("A shortcut lap -- real times, void verdict -- IS still submittable: %s"),
		*VoidReason), VoidButTimed.IsSubmittable(&VoidReason));

	return true;
}

// ===========================================================================
// 3. "Result submission rejects invalid build/track/tune metadata"
// ===========================================================================
//
// Docs/03-TrackRaceUI.md's functional-test list, verbatim. Four ways a result can be
// unsubmittable and one way it can be submittable, each asserted, plus the percent-encoding
// rule CORE-003 C3-4 attaches to the query string a submission would carry.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceResultSubmissionTest,
	"RacingSim.Race.ResultSubmission",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceResultSubmissionTest::RunTest(const FString& Parameters)
{
	using namespace RaceResultSpecPrivate;

	FResultSpecRig Rig;
	if (!Rig.Build(*this))
	{
		return false;
	}

	// -- An UNFROZEN result is refused before anything else is even looked at -
	{
		const FRacingRaceResult Empty;
		FString Reason;
		TestFalse(TEXT("A default-constructed result is not submittable"), Empty.IsSubmittable(&Reason));
		TestTrue(FString::Printf(TEXT("...because it was never frozen: %s"), *Reason),
			Reason.Contains(TEXT("not been frozen")));

		FString Query;
		FString QueryReason;
		TestFalse(TEXT("...and it produces no query string"),
			Empty.MakeSubmissionQueryString(Query, QueryReason));
		TestTrue(TEXT("...with the query left EMPTY, so an ignored return value cannot leak one"),
			Query.IsEmpty());
	}

	Rig.ApplyPublishableMetadata();
	Rig.DriveOneCleanLapAndFinish();

	// The frozen result is copied out, because the recorder's own copy is const and the
	// cases below need to vary one field at a time. Varying a copy is also the honest way
	// to test IsSubmittable(): it is a property of the RESULT, and a reviewer can see that
	// nothing else changed between cases.
	const FRacingRaceResult Frozen = Rig.Recorder->GetFrozenResult();
	TestTrue(TEXT("The session produced a frozen, valid result"), Frozen.bFrozen);
	TestEqual(TEXT("...that is Valid"), Frozen.GetValidity(), ERacingRunValidity::Valid);

	// -- CASE 1: a NON-AUTHORITATIVE BUILD ID ---------------------------------
	//
	// This is the case that fires on every developer machine, because the Derived scheme is
	// never authoritative by construction: two different local source trees on the same
	// engine patch produce the same string. A leaderboard must not silently merge them.
	{
		FRacingRaceResult NonAuthoritative = Frozen;
		NonAuthoritative.Version.GameBuildId.Scheme = ERacingBuildIdScheme::Derived;
		NonAuthoritative.Version.GameBuildId.bIsAuthoritative = false;

		FString Reason;
		TestFalse(TEXT("A result carrying a non-authoritative build ID is REFUSED"),
			NonAuthoritative.IsSubmittable(&Reason));
		TestTrue(FString::Printf(TEXT("...and the reason names the build ID: %s"), *Reason),
			Reason.Contains(TEXT("authoritative")));

		FString Query;
		FString QueryReason;
		TestFalse(TEXT("...and no query string is produced"),
			NonAuthoritative.MakeSubmissionQueryString(Query, QueryReason));
		TestTrue(TEXT("...which is empty"), Query.IsEmpty());
	}

	// -- CASE 2: an INVALID TRACK ---------------------------------------------
	//
	// The cached-validity flag TRACK-001 M7 asked for, doing the job it exists for. See
	// RacingSim.Race.ResultTrackGate for the same refusal driven by a REAL gate-bake
	// failure on a real track actor rather than by a set field.
	{
		FRacingRaceResult BadTrack = Frozen;
		BadTrack.Version.GameBuildId.Scheme = ERacingBuildIdScheme::Explicit;
		BadTrack.Version.GameBuildId.bIsAuthoritative = true;
		BadTrack.bTrackValidated = false;
		BadTrack.TrackValidationReason = TEXT("Checkpoint gates failed to bake: test-injected reason");

		FString Reason;
		TestFalse(TEXT("A result from a track that did not validate is REFUSED"),
			BadTrack.IsSubmittable(&Reason));
		TestTrue(FString::Printf(TEXT("...and the reason quotes the track's own failure: %s"), *Reason),
			Reason.Contains(TEXT("Checkpoint gates failed to bake")));
	}

	// -- CASE 3: an UNPOPULATED CAR TUNE VERSION -------------------------------
	{
		FRacingRaceResult NoTune = Frozen;
		NoTune.Version.GameBuildId.Scheme = ERacingBuildIdScheme::Explicit;
		NoTune.Version.GameBuildId.bIsAuthoritative = true;
		NoTune.Version.CarSpecVersion = FRacingContentVersion();

		FString Reason;
		TestFalse(TEXT("A result with no car tune version is REFUSED"), NoTune.IsSubmittable(&Reason));
		TestTrue(FString::Printf(TEXT("...naming the car spec: %s"), *Reason),
			Reason.Contains(TEXT("CarSpecVersion")));
	}

	// -- CASE 4: an UNKNOWN INPUT DEVICE ---------------------------------------
	{
		FRacingRaceResult NoInput = Frozen;
		NoInput.Version.GameBuildId.Scheme = ERacingBuildIdScheme::Explicit;
		NoInput.Version.GameBuildId.bIsAuthoritative = true;

		// IsPublishable() checks PhysicsPolicyVersion BEFORE InputDeviceType, and it is 0 on
		// a fresh project because VEH-002 has not established a physics policy yet. Cleared
		// here so the input device is demonstrably the operative reason rather than a later
		// one that never gets reached.
		NoInput.Version.PhysicsPolicyVersion = 1;
		NoInput.Version.InputDeviceType = ERacingInputDeviceType::Unknown;

		FString Reason;
		TestFalse(TEXT("A result with an Unknown input device is REFUSED"), NoInput.IsSubmittable(&Reason));
		TestTrue(FString::Printf(TEXT("...naming the input device: %s"), *Reason),
			Reason.Contains(TEXT("InputDeviceType")));
	}

	// -- THE POSITIVE CONTROL, and the query string it produces ----------------
	//
	// Without this, every rejection above would be satisfied by an IsSubmittable() that had
	// simply stopped returning true.
	{
		FRacingRaceResult Publishable = Frozen;
		Publishable.Version.GameBuildId.Scheme = ERacingBuildIdScheme::Explicit;
		Publishable.Version.GameBuildId.bIsAuthoritative = true;

		// A CI-stamped ID of exactly the shape SanitiseComponent's allow-list permits --
		// including the '+' that makes CORE-003 C3-4 necessary. Semver build metadata
		// ("1.4.0+4417") is the conventional form and is why '+' is allowed at all.
		Publishable.Version.GameBuildId.Value = TEXT("ci-2026.08.21+4417");

		// PhysicsPolicyVersion has no producer yet (VEH-002 owns it) and MakeCurrent()
		// reads it from settings, where it is 0 on a fresh project. Set explicitly so this
		// case tests the SUBMISSION rule rather than the state of an unrelated setting.
		Publishable.Version.PhysicsPolicyVersion = 1;

		FString Reason;
		TestTrue(FString::Printf(TEXT("A fully populated, authoritative, validated result IS submittable: %s"),
			*Reason), Publishable.IsSubmittable(&Reason));
		TestTrue(TEXT("...with no reason recorded"), Reason.IsEmpty());

		FString Query;
		FString QueryReason;
		TestTrue(TEXT("...and it produces a query string"),
			Publishable.MakeSubmissionQueryString(Query, QueryReason));
		TestTrue(TEXT("...which is non-empty"), !Query.IsEmpty());

		// -- CORE-003 C3-4, at the one site in the project that writes a build ID into a
		//    query string.
		TestTrue(TEXT("C3-4: the build ID's '+' is percent-encoded as %2B"),
			Query.Contains(TEXT("build=ci-2026.08.21%2B4417")));
		TestFalse(TEXT("C3-4: NO bare '+' survives anywhere in the query -- unencoded it decodes to a space"),
			Query.Contains(TEXT("+")));

		// FRacingContentVersion::ToString() emits '@' and '#'. '#' is the dangerous one: it
		// would truncate the URL at a fragment boundary and silently drop every field after
		// the track.
		TestFalse(TEXT("C3-4: no bare '#' survives -- it would truncate the URL"),
			Query.Contains(TEXT("#")));
		TestFalse(TEXT("C3-4: no bare '@' survives"), Query.Contains(TEXT("@")));
		TestFalse(TEXT("C3-4: no bare space survives"), Query.Contains(TEXT(" ")));

		// The keys are present and the '&'/'=' that remain are the STRUCTURAL ones this
		// function wrote, not characters that leaked out of a value.
		for (const TCHAR* Key : { TEXT("build="), TEXT("track="), TEXT("car="), TEXT("ruleset="),
			TEXT("engine="), TEXT("physics="), TEXT("assists="), TEXT("input="),
			TEXT("validity="), TEXT("laps="), TEXT("best="), TEXT("final=") })
		{
			TestTrue(FString::Printf(TEXT("The query carries the '%s' field"), Key), Query.Contains(Key));
		}

		int32 SeparatorCount = 0;
		for (const TCHAR Ch : Query)
		{
			if (Ch == TEXT('&'))
			{
				++SeparatorCount;
			}
		}
		TestEqual(TEXT("Twelve fields are joined by exactly eleven separators, so no value smuggled an '&'"),
			SeparatorCount, 11);

		// Durations go out as RAW SECONDS, never formatted. Docs/03-TrackRaceUI.md:
		// "Store raw duration with high precision; format only in UI."
		TestFalse(TEXT("No formatted mm:ss time reaches the wire -- durations are raw seconds"),
			Query.Contains(TEXT("%3A")));   // an encoded ':'
	}

	return true;
}

// ===========================================================================
// 4. A full session -> results -> restart -> new session cycle
// ===========================================================================
//
// `.claude/rules/race-tests.md` lists restart as a required category, and
// Docs/03-TrackRaceUI.md's Restart state requires "a complete state reset without reusing
// stale timers, delegates, input, or checkpoint state". RACE-002 proved the LAP TRACKER
// clears; nothing yet called ResetForNewSession() from a state-machine transition, and
// nothing owned the frozen result. This is that boundary.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceResultRestartCycleTest,
	"RacingSim.Race.ResultRestartCycle",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceResultRestartCycleTest::RunTest(const FString& Parameters)
{
	using namespace RaceResultSpecPrivate;

	FResultSpecRig Rig;
	if (!Rig.Build(*this))
	{
		return false;
	}

	// A SECOND tracker, so "every registered tracker is reset" is a claim with a plural in
	// it. One tracker cannot distinguish "resets every tracker" from "resets the primary".
	FString SecondError;
	TStrongObjectPtr<URaceLapTracker> SecondTracker(
		URaceLapTracker::Create(GetTransientPackage(), Rig.Machine.Get(), Rig.Ruleset.Get()));
	if (!SecondTracker.IsValid()
		|| !SecondTracker->ConfigureTrack(Rig.Gates, Rig.SectorStartsCm, Rig.LapLengthCm, SecondError))
	{
		AddError(FString::Printf(TEXT("Second lap tracker failed to configure: %s"), *SecondError));
		return false;
	}

	TestTrue(TEXT("A second tracker registers"), Rig.Recorder->RegisterLapTracker(SecondTracker.Get()));
	TestFalse(TEXT("...and registering it again is a no-op, not a second reset target"),
		Rig.Recorder->RegisterLapTracker(SecondTracker.Get()));
	TestEqual(TEXT("...leaving two trackers"), Rig.Recorder->GetNumLapTrackers(), 2);
	TestTrue(TEXT("The PRIMARY is the first registered"),
		Rig.Recorder->GetPrimaryLapTracker() == Rig.Tracker.Get());

	Rig.ApplyPublishableMetadata();

	// -- CanStartSession: the cheap refusal, before and after --------------------
	{
		FString Reason;
		TestTrue(FString::Printf(TEXT("A validated track and a configured tracker may start a session: %s"),
			*Reason), Rig.Recorder->CanStartSession(Reason));

		// AND THE BAR IS DELIBERATELY LOWER THAN SUBMISSION'S. A developer build produces a
		// Derived, non-authoritative ID by construction; refusing to START on that basis
		// would make this project unable to race on the machine that develops it.
		FRacingContentVersion TrackVersion;
		TrackVersion.AssetId = FName(TEXT("Track.Test.Unvalidated"));
		TrackVersion.SchemaVersion = ATrackDefinitionActor::TrackSchemaVersion;
		Rig.Recorder->SetTrackSnapshot(TrackVersion, 3, /*bValidated*/ false,
			TEXT("Centerline failed to bake."));

		FString BadReason;
		TestFalse(TEXT("An unvalidated track cannot start a session"),
			Rig.Recorder->CanStartSession(BadReason));
		TestTrue(FString::Printf(TEXT("...and the reason quotes the track: %s"), *BadReason),
			BadReason.Contains(TEXT("Centerline failed to bake")));

		Rig.ApplyValidTrackSnapshot();
	}

	// =======================================================================
	// SESSION 1
	// =======================================================================

	const int32 FirstSessionId = Rig.Machine->GetSessionId();

	Rig.StartRacing(-800.0);
	Rig.Drive(400.0, 8);
	Rig.Drive(400.0 + Rig.LapLengthCm, ResultSpecStepsPerLap);

	// Drive the second tracker part-way round, so it has real state to lose. It shares the
	// clock and the gate set; only its cursor differs.
	SecondTracker->SeedProgress(Rig.PositionAt(-800.0), -800.0);
	for (int32 Step = 1; Step <= 120; ++Step)
	{
		GResultSpecNowSeconds += ResultSpecSecondsPerStep;
		const double DistanceCm = FMath::Lerp(-800.0, Rig.LapLengthCm * 0.4, static_cast<double>(Step) / 120.0);
		SecondTracker->Advance(Rig.PositionAt(DistanceCm), DistanceCm);
	}

	TestTrue(TEXT("Session 1: the primary tracker completed a valid lap"),
		Rig.Tracker->GetValidLapsCompleted() >= 1);
	TestTrue(TEXT("Session 1: the second tracker has taken a gate"), SecondTracker->IsGateSatisfied(1));
	TestTrue(TEXT("Session 1: ...and has a lap in progress"), SecondTracker->IsLapInProgress());

	Rig.Machine->FinishRace();
	Rig.Machine->ShowResults();

	TestEqual(TEXT("Session 1 reached Results"), Rig.Machine->GetRaceState(), ERaceState::Results);
	TestTrue(TEXT("...with a frozen result"), Rig.Recorder->HasFrozenResult());

	const double Session1FinalSeconds = Rig.Recorder->GetFrozenResult().FinalTimeSeconds;
	const int32 Session1Laps = Rig.Recorder->GetFrozenResult().LapsCompleted;
	TestTrue(TEXT("...describing a real run"), Session1FinalSeconds > 0.0 && Session1Laps >= 1);

	const int32 NotificationsBeforeRestart = Rig.Recorder->GetStateChangeNotificationCount();

	// =======================================================================
	// RESTART
	// =======================================================================

	TestEqual(TEXT("Restart from Results is applied"),
		Rig.Machine->Restart(), ERaceTransitionResult::Applied);
	TestEqual(TEXT("...landing in PreRace"), Rig.Machine->GetRaceState(), ERaceState::PreRace);
	TestTrue(TEXT("...and bumping the session id"), Rig.Machine->GetSessionId() != FirstSessionId);

	// -- NO STALE STATE CROSSES THE BOUNDARY ----------------------------------
	{
		// THE FROZEN RESULT IS GONE. A HUD reading it during the new PreRace/Countdown must
		// see "no result", not the previous run's final time sitting there looking live.
		TestFalse(TEXT("Restart drops the frozen result"), Rig.Recorder->HasFrozenResult());
		TestFalse(TEXT("...and the struct itself reports unfrozen"),
			Rig.Recorder->GetFrozenResult().bFrozen);
		TestEqual(TEXT("...with no residual final time"),
			Rig.Recorder->GetFrozenResult().FinalTimeSeconds, 0.0, 0.0);
		TestEqual(TEXT("...no residual lap count"), Rig.Recorder->GetFrozenResult().LapsCompleted, 0);
		TestEqual(TEXT("...and no residual validity"),
			Rig.Recorder->GetFrozenResult().GetValidity(), ERacingRunValidity::Unknown);

		// EVERY registered tracker was reset -- this is the ResetForNewSession() call site
		// RACE-002 built and nothing had. Asserted over BOTH trackers, because one tracker
		// cannot distinguish "resets every tracker" from "resets the primary".
		auto AssertTrackerCleared = [this](const TCHAR* Which, const URaceLapTracker* Tracker)
		{
			TestEqual(*FString::Printf(TEXT("Restart cleared the %s tracker's lap counter"), Which),
				Tracker->GetCurrentLapNumber(), 0);
			TestEqual(*FString::Printf(TEXT("...the %s tracker's completed laps"), Which),
				Tracker->GetLapsCompleted(), 0);
			TestEqual(*FString::Printf(TEXT("...the %s tracker's valid laps"), Which),
				Tracker->GetValidLapsCompleted(), 0);
			TestFalse(*FString::Printf(TEXT("...the %s tracker's lap-in-progress flag"), Which),
				Tracker->IsLapInProgress());
			TestEqual(*FString::Printf(TEXT("...the %s tracker's expected gate"), Which),
				Tracker->GetExpectedGateIndex(), FRacingCheckpointGateSet::StartFinishGateIndex);
			TestFalse(*FString::Printf(TEXT("...every %s gate-crossed flag"), Which),
				Tracker->IsGateSatisfied(0) || Tracker->IsGateSatisfied(1)
					|| Tracker->IsGateSatisfied(2) || Tracker->IsGateSatisfied(3));
			TestEqual(*FString::Printf(TEXT("...the %s tracker's best lap"), Which),
				Tracker->GetBestValidLap().LapNumber, 0);
			TestEqual(*FString::Printf(TEXT("...the %s tracker's last lap"), Which),
				Tracker->GetLastCompletedLap().LapNumber, 0);
			TestEqual(*FString::Printf(TEXT("...and the %s tracker's run validity"), Which),
				Tracker->GetRunValidity(), ERacingRunValidity::Pending);
			TestTrue(*FString::Printf(TEXT("...while the %s tracker keeps its track snapshot: a restart "
				"re-races the same circuit"), Which), Tracker->IsConfigured());
		};

		AssertTrackerCleared(TEXT("primary"), Rig.Tracker.Get());
		AssertTrackerCleared(TEXT("second"), SecondTracker.Get());

		// CONFIGURATION SURVIVES. A restart resets STATE, not wiring: the same car on the
		// same circuit with the same trackers registered.
		TestEqual(TEXT("Both trackers are still registered"), Rig.Recorder->GetNumLapTrackers(), 2);
		TestTrue(TEXT("...with the same primary"),
			Rig.Recorder->GetPrimaryLapTracker() == Rig.Tracker.Get());
		FString StartReason;
		TestTrue(TEXT("...and the session may start again immediately"),
			Rig.Recorder->CanStartSession(StartReason));

		// NO STALE DELEGATE. The binding is per-OBJECT, not per-session, so a restart cannot
		// accumulate a second subscription -- which, on a native multicast delegate, nothing
		// else can observe. See GetStateChangeNotificationCount().
		TestTrue(TEXT("The recorder is still observing its state machine"),
			Rig.Recorder->IsObservingStateMachine());
		TestEqual(TEXT("The restart delivered exactly ONE notification, so no duplicate binding exists"),
			Rig.Recorder->GetStateChangeNotificationCount(), NotificationsBeforeRestart + 1);

		// IDEMPOTENCE, both ways in.
		Rig.Recorder->ClearForNewSession();
		Rig.Recorder->ClearForNewSession();
		TestFalse(TEXT("An explicit, doubled ClearForNewSession is a no-op"),
			Rig.Recorder->HasFrozenResult());
		TestEqual(TEXT("...and does not lose a tracker"), Rig.Recorder->GetNumLapTrackers(), 2);

		// RACE-001 finding L3: Restart while ALREADY in PreRace is Redundant -- no
		// transition, no broadcast, no session-id bump. This class must not depend on the id
		// changing, and this asserts it does not have to: there is nothing to clear.
		const int32 IdBefore = Rig.Machine->GetSessionId();
		const int32 NotificationsBefore = Rig.Recorder->GetStateChangeNotificationCount();
		TestEqual(TEXT("L3: Restart from PreRace is Redundant"),
			Rig.Machine->Restart(), ERaceTransitionResult::Redundant);
		TestEqual(TEXT("L3: ...bumping no session id"), Rig.Machine->GetSessionId(), IdBefore);
		TestEqual(TEXT("L3: ...and broadcasting nothing, which is correct: to be in PreRace is already "
					   "to have been cleared"),
			Rig.Recorder->GetStateChangeNotificationCount(), NotificationsBefore);
	}

	// =======================================================================
	// SESSION 2 -- behaves exactly like a first session
	// =======================================================================

	Rig.StartRacing(-800.0);
	Rig.Drive(400.0, 8);
	Rig.Drive(400.0 + Rig.LapLengthCm, ResultSpecStepsPerLap);
	Rig.Machine->FinishRace();

	{
		const FRacingRaceResult& Second = Rig.Recorder->GetFrozenResult();
		TestTrue(TEXT("Session 2 froze its own result"), Second.bFrozen);
		TestEqual(TEXT("...numbered from lap 1 again"), Second.LapsCompleted, 1);
		TestEqual(TEXT("...with one valid lap"), Second.ValidLapsCompleted, 1);
		TestEqual(TEXT("...and the car on lap 2"), Second.CurrentLapNumberAtFinish, 2);
		TestEqual(TEXT("...at the NEW session id"), Second.SessionId, Rig.Machine->GetSessionId());
		TestTrue(TEXT("...with a different session id from the first run"),
			Second.SessionId != FirstSessionId);
		TestEqual(TEXT("...and Valid"), Second.GetValidity(), ERacingRunValidity::Valid);

		// THE TIMER RE-ZEROED. Session 2's final time is one lap's worth, not session 1's
		// time plus one lap -- which is what a restart that failed to re-zero the clock
		// would produce, and it would look entirely plausible on a HUD.
		TestTrue(FString::Printf(TEXT("Session 2's final time (%.3fs) is a fresh clock, not session 1's "
			"(%.3fs) continued"), Second.FinalTimeSeconds, Session1FinalSeconds),
			Second.FinalTimeSeconds < Session1FinalSeconds);
		TestTrue(TEXT("...and is still a real duration"), Second.FinalTimeSeconds > 0.0);
	}

	// -- DETACH: the explicit teardown, and it is idempotent -------------------
	{
		Rig.Recorder->DetachFromStateMachine();
		TestFalse(TEXT("After detaching, the recorder no longer observes"),
			Rig.Recorder->IsObservingStateMachine());

		const int32 NotificationsAfterDetach = Rig.Recorder->GetStateChangeNotificationCount();
		Rig.Machine->Restart();
		TestEqual(TEXT("...and receives no further notifications: the binding is genuinely gone"),
			Rig.Recorder->GetStateChangeNotificationCount(), NotificationsAfterDetach);

		Rig.Recorder->DetachFromStateMachine();   // idempotent
		TestFalse(TEXT("A second detach is a no-op"), Rig.Recorder->IsObservingStateMachine());
	}

	return true;
}

// ===========================================================================
// 5. TRACK-001 M7 + TRACK-002 M4, end to end: a real invalid track is refused
// ===========================================================================
//
// The other four suites hand the recorder a track SNAPSHOT, which can only prove it
// honours a bool. This one drives a REAL ATrackDefinitionActor through a REAL gate-bake
// failure and a REAL cached-validity lookup, because the substance of TRACK-002 M4 is that
// RebuildTrackData() returns TRUE in exactly that case -- so a session-start check built on
// IsTrackDataBuilt(), or on DidPostLoadBakeSucceed(), would wave it through.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceResultTrackGateTest,
	"RacingSim.Race.ResultTrackGate",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceResultTrackGateTest::RunTest(const FString& Parameters)
{
	using namespace RaceResultSpecPrivate;

	FResultSpecTrackFixture Fixture;
	ATrackDefinitionActor* Track = Fixture.Track;
	if (!Track)
	{
		AddError(TEXT("Class default object for ATrackDefinitionActor is unavailable."));
		return false;
	}

	FResultSpecRig Rig;
	if (!Rig.Build(*this))
	{
		return false;
	}
	Rig.ApplyPublishableMetadata();

	const double TrackLengthCm = Track->GetTrackLengthCm();
	Track->SectorStartDistancesCm = { 0.0, TrackLengthCm / 3.0, 2.0 * TrackLengthCm / 3.0 };
	Track->RebuildTrackData();

	// -- The healthy track: SetTrack() reads the cache and the session may start
	{
		Rig.Recorder->SetTrack(Track);

		FString Reason;
		TestTrue(FString::Printf(TEXT("A validated track lets a session start: %s"), *Reason),
			Rig.Recorder->CanStartSession(Reason));
		TestTrue(TEXT("...and the track's own cached answer agrees"), Track->IsValidatedForRace());
	}

	// -- BREAK THE GATE BAKE, and nothing else --------------------------------
	//
	// Authored gates narrower than the placement tolerance. FRacingCheckpointGateSet::Build
	// refuses them, ATrackDefinitionActor records CheckpointGateBakeError -- and
	// RebuildTrackData() STILL RETURNS TRUE, because RebuildCheckpointGates() returns void
	// by design: a mis-authored gate must not take the centerline, and therefore progress
	// and ranking, down with it. That design decision is exactly what makes this case
	// invisible to every other health signal on the actor.
	{
		// DERIVED FROM THE BAKE, NOT HARD-CODED. FRacingCheckpointGateSet::Build refuses a
		// gate whose half-width is at or below the centerline's placement tolerance, which
		// on this fixture is sub-centimetre -- a literal "1.0 cm, surely narrow enough"
		// silently PASSED the bake in the first draft of this test, and the suite caught it.
		// Deriving the number means a future change to the bake resolution cannot make this
		// case quietly stop testing anything.
		const double ToleranceCm =
			Track->GetCenterline().GetSagittaBoundCm(Track->MinCornerRadiusCm);
		const double NarrowHalfWidthCm = ToleranceCm * 0.5;

		TestTrue(TEXT("The derived narrow half-width is positive but under the placement tolerance"),
			NarrowHalfWidthCm > 0.0 && NarrowHalfWidthCm < ToleranceCm);

		TArray<FRacingCheckpointGateSpec> NarrowSpecs;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			FRacingCheckpointGateSpec Spec;
			Spec.GateId = (Index == 0)
				? FName(TEXT("Gate.StartFinish"))
				: FName(*FString::Printf(TEXT("Gate.%02d"), Index));
			Spec.DistanceAlongCm = (Index == 0)
				? 0.0
				: TrackLengthCm * static_cast<double>(Index) / 4.0;

			// Half the placement tolerance: the gate's entire margin is consumed by the
			// baked model's own systematic inward error before a car is anywhere near it.
			Spec.HalfWidthCm = NarrowHalfWidthCm;
			Spec.HalfHeightCm = ResultSpecGateHalfHeightCm;
			Spec.LegalDirection = ERacingGateDirection::Forward;
			NarrowSpecs.Add(Spec);
		}

		Track->CheckpointGateSpecs = NarrowSpecs;

		// M4 IN ONE ASSERTION.
		TestTrue(TEXT("M4: RebuildTrackData() returns TRUE even though the gate bake failed"),
			Track->RebuildTrackData());
		TestTrue(TEXT("M4: ...and IsTrackDataBuilt() agrees, because the CENTERLINE is fine"),
			Track->IsTrackDataBuilt());
		TestFalse(TEXT("M4: ...while the gate set is not valid at all"),
			Track->GetCheckpointGates().IsValid());

		// THE CACHE SEES IT. This is what the criterion asks for: the cached-validity flag
		// covers the gate-bake-failed case, not just the centerline-bake-failed case.
		FString CacheReason;
		TestFalse(TEXT("M7+M4: the cached validity refuses the track"),
			Track->GetCachedValidation(CacheReason));
		TestTrue(FString::Printf(TEXT("...naming the gate bake, not a downstream symptom: %s"), *CacheReason),
			CacheReason.Contains(TEXT("Checkpoint gates failed to bake")));

		// AND THE RECORDER REFUSES TO START THE SESSION on it, cheaply -- without a live
		// Validate() call, which is the point of the cache.
		Rig.Recorder->SetTrack(Track);

		FString StartReason;
		TestFalse(TEXT("A session cannot start on a track whose gates did not bake"),
			Rig.Recorder->CanStartSession(StartReason));
		TestTrue(FString::Printf(TEXT("...and the refusal quotes the real reason: %s"), *StartReason),
			StartReason.Contains(TEXT("Checkpoint gates failed to bake")));

		// AND A RESULT FROZEN ON IT IS UNSUBMITTABLE. The session is driven anyway -- the
		// game does not crash on a bad track, it refuses to publish from one.
		Rig.DriveOneCleanLapAndFinish();

		const FRacingRaceResult& Result = Rig.Recorder->GetFrozenResult();
		TestTrue(TEXT("The result still freezes"), Result.bFrozen);
		TestFalse(TEXT("...recording that the track did not validate"), Result.bTrackValidated);

		FString SubmitReason;
		TestFalse(TEXT("...and it is REFUSED for submission"), Result.IsSubmittable(&SubmitReason));
		TestTrue(FString::Printf(TEXT("...on the track's own recorded reason: %s"), *SubmitReason),
			SubmitReason.Contains(TEXT("Checkpoint gates failed to bake")));

		FString Query;
		FString QueryReason;
		TestFalse(TEXT("...producing no query string"), Result.MakeSubmissionQueryString(Query, QueryReason));
		TestTrue(TEXT("...which is empty"), Query.IsEmpty());
	}

	// -- Repair the track: the cache must NOTICE, not stay stuck on the old answer
	{
		Track->CheckpointGateSpecs.Reset();
		Track->RebuildTrackData();

		FString RepairedReason;
		TestTrue(FString::Printf(TEXT("Removing the bad gates re-validates the track: %s"), *RepairedReason),
			Track->GetCachedValidation(RepairedReason));
		TestTrue(TEXT("...with no reason recorded"), RepairedReason.IsEmpty());

		Rig.Recorder->SetTrack(Track);
		FString StartReason;
		TestTrue(FString::Printf(TEXT("...and a session may start again: %s"), *StartReason),
			Rig.Recorder->CanStartSession(StartReason));
	}

	return true;
}
