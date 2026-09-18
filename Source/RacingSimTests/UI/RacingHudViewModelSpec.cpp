// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingHudTypes.h"
#include "Core/RacingTelemetry.h"
#include "Core/RacingTelemetryFunctionLibrary.h"
#include "UI/RacingHudViewModel.h"

#include "Race/RaceFunctionLibrary.h"
#include "Race/RaceLapTracker.h"
#include "Race/RaceResult.h"
#include "Race/RaceRulesetDataAsset.h"
#include "Race/RaceStateMachine.h"
#include "Race/TrackCenterline.h"
#include "Race/TrackCheckpointGate.h"
#include "Race/TrackDefinitionActor.h"

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include <limits>

/**
 * UI-001: the HUD view model and its data contract.
 *
 * Two suites:
 *
 *   RacingSim.UI.HudViewModel.Builder -- the builder as a pure function. Every rule a
 *   widget would otherwise re-implement (staleness, countdown rounding, sector numbering,
 *   position visibility, best-lap sourcing, result visibility, reset-per-call) against
 *   hand-built inputs, including the non-finite ones a widget cannot be trusted to catch.
 *
 *   RacingSim.UI.HudViewModel.RaceIntegration -- URaceFunctionLibrary::GatherHudRaceInputs
 *   against a live session: PreRace, countdown, green, lap 1, lap 2, finish, results,
 *   restart. Every value is checked against the getter that owns it, so the gatherer is
 *   proven to copy the race truth and not to derive a second version of it.
 *
 * The rig is RaceResultSpec's -- procedural circle, arithmetic gates, fake monotonic clock --
 * copied under its own namespace and clock global rather than shared, so neither suite can
 * perturb the other's clock through test order.
 */

namespace HudViewModelSpecPrivate
{
	constexpr double HudSpecCircleRadiusCm = 10000.0;
	constexpr int32 HudSpecCircleSamples = 720;
	constexpr double HudSpecGateHalfWidthCm = 900.0;
	constexpr double HudSpecGateHalfHeightCm = 500.0;
	constexpr int32 HudSpecStepsPerLap = 400;
	constexpr double HudSpecSecondsPerStep = 0.016;
	constexpr double HudSpecCountdownSeconds = 3.0;

	double GHudSpecNowSeconds = 0.0;
	double HudSpecTimeSource()
	{
		return GHudSpecNowSeconds;
	}

	struct FHudSpecRig
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

		bool Build(FAutomationTestBase& Test)
		{
			GHudSpecNowSeconds = 7000.0;

			const double TotalCm = 2.0 * UE_DOUBLE_PI * HudSpecCircleRadiusCm;
			const double StepCm = TotalCm / static_cast<double>(HudSpecCircleSamples);

			TArray<FVector> Locations;
			TArray<double> Distances;
			for (int32 Index = 0; Index < HudSpecCircleSamples; ++Index)
			{
				const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(Index) / static_cast<double>(HudSpecCircleSamples);
				Locations.Add(FVector(HudSpecCircleRadiusCm * FMath::Cos(Angle), HudSpecCircleRadiusCm * FMath::Sin(Angle), 0.0));
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
				Spec.HalfWidthCm = HudSpecGateHalfWidthCm;
				Spec.HalfHeightCm = HudSpecGateHalfHeightCm;
				Spec.LegalDirection = ERacingGateDirection::Forward;
				Specs.Add(Spec);
			}

			if (!Gates.Build(Specs, Circle, HudSpecCircleRadiusCm, Error))
			{
				Test.AddError(FString::Printf(TEXT("Gate set failed to build: %s"), *Error));
				return false;
			}

			SectorStartsCm = { 0.0, LapLengthCm / 3.0, LapLengthCm * 2.0 / 3.0 };

			Ruleset.Reset(NewObject<URaceRulesetDataAsset>(GetTransientPackage()));
			Ruleset->RulesetId = FName(TEXT("Ruleset.Test.Hud"));
			Ruleset->CountdownSeconds = HudSpecCountdownSeconds;

			Machine.Reset(URaceStateMachine::CreateWithTimeSource(GetTransientPackage(), Ruleset.Get(), &HudSpecTimeSource));
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

			FRacingContentVersion TrackVersion;
			TrackVersion.AssetId = FName(TEXT("Track.Test.HudCircle"));
			TrackVersion.SchemaVersion = ATrackDefinitionActor::TrackSchemaVersion;
			TrackVersion.ContentHash = 0x0DD0C1C1;
			Recorder->SetTrackSnapshot(TrackVersion, SectorStartsCm.Num(), /*bValidated*/ true, FString());

			return true;
		}

		FVector PositionAt(const double DistanceCm, const double OutwardOffsetCm = 0.0) const
		{
			const FVector Base = Circle.GetLocationAtDistanceCm(DistanceCm);
			// The circle is centred on the origin, so radially outward is the position's own direction.
			return Base + Base.GetSafeNormal2D() * OutwardOffsetCm;
		}

		/**
		 * Drive forward to ToDistanceCm in Steps steps. Inside [WideFromCm, WideToCm] the car
		 * runs OutwardOffsetCm wide, which takes it round the outside of any gate there.
		 */
		void Drive(
			const double ToDistanceCm,
			const int32 Steps,
			const double WideFromCm = 0.0,
			const double WideToCm = -1.0,
			const double OutwardOffsetCm = 0.0)
		{
			const double FromCm = CurrentDistanceCm;
			for (int32 Index = 1; Index <= Steps; ++Index)
			{
				GHudSpecNowSeconds += HudSpecSecondsPerStep;
				const double Alpha = static_cast<double>(Index) / static_cast<double>(Steps);
				CurrentDistanceCm = FMath::Lerp(FromCm, ToDistanceCm, Alpha);
				const bool bWide = (WideToCm > WideFromCm) && (CurrentDistanceCm >= WideFromCm) && (CurrentDistanceCm <= WideToCm);
				Tracker->Advance(PositionAt(CurrentDistanceCm, bWide ? OutwardOffsetCm : 0.0), CurrentDistanceCm);
			}
		}

		bool Gather(FRacingHudRaceInputs& OutInputs, const int32 CompetitorCount = 1) const
		{
			return URaceFunctionLibrary::GatherHudRaceInputs(
				Machine.Get(), Tracker.Get(), Recorder.Get(), CompetitorCount, OutInputs);
		}
	};

	/** A vehicle sample stamped at Now, moving at 50 m/s. */
	FRacingVehicleTelemetrySample HudSpecFreshVehicle(const double NowSeconds)
	{
		FRacingVehicleTelemetrySample Vehicle;
		Vehicle.TimestampSeconds = NowSeconds;
		Vehicle.ForwardSpeedCms = 5000.0;
		Vehicle.EngineRPM = 6500.0f;
		Vehicle.GearIndex = 4;
		return Vehicle;
	}

	FRacingHudViewModel HudSpecBuild(const FRacingHudRaceInputs& Inputs, const double NowSeconds = 100.0)
	{
		return URacingHudViewModelLibrary::BuildHudViewModel(
			Inputs, HudSpecFreshVehicle(NowSeconds), NowSeconds, 0.5, ERacingSpeedDisplayUnit::KilometresPerHour);
	}
}

// ===========================================================================
// Builder
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingHudViewModelBuilderTest,
	"RacingSim.UI.HudViewModel.Builder",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingHudViewModelBuilderTest::RunTest(const FString& Parameters)
{
	using namespace HudViewModelSpecPrivate;
	using Lib = URacingHudViewModelLibrary;

	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const double Inf = std::numeric_limits<double>::infinity();

	// -- Defaults in, defaults out --------------------------------------------
	{
		const FRacingHudRaceInputs Inputs;
		const FRacingHudViewModel VM = HudSpecBuild(Inputs);
		TestFalse(TEXT("No race state in, none out"), VM.bHasRaceState);
		TestFalse(TEXT("No countdown without a state"), VM.bShowCountdown);
		TestFalse(TEXT("No lap in progress"), VM.bLapInProgress);
		TestEqual(TEXT("No sector"), VM.CurrentSectorNumber, 0);
		TestFalse(TEXT("No last lap"), VM.bHasLastLap);
		TestFalse(TEXT("No best lap"), VM.bHasBestLap);
		TestFalse(TEXT("No position"), VM.bShowPosition);
		TestFalse(TEXT("No results"), VM.bShowResults);
	}

	// -- Vehicle freshness: the one staleness rule, failing closed -------------
	{
		const FRacingHudRaceInputs Inputs;
		const auto Build = [&Inputs](const FRacingVehicleTelemetrySample& Vehicle, const double Now, const double MaxAge,
			const ERacingSpeedDisplayUnit Unit = ERacingSpeedDisplayUnit::KilometresPerHour)
		{
			return URacingHudViewModelLibrary::BuildHudViewModel(Inputs, Vehicle, Now, MaxAge, Unit);
		};

		FRacingVehicleTelemetrySample Vehicle = HudSpecFreshVehicle(10.0);

		FRacingHudViewModel VM = Build(Vehicle, 10.2, 0.5);
		TestTrue(TEXT("A 0.2 s old sample is fresh"), VM.bVehicleDataFresh);
		TestNearlyEqual(TEXT("...showing 180 km/h"), VM.Speed, 180.0, 1.0e-9);
		TestEqual(TEXT("...its RPM"), VM.EngineRPM, 6500.0f);
		TestEqual(TEXT("...and its gear"), VM.GearIndex, 4);
		TestEqual(TEXT("...in the requested unit"), VM.SpeedUnit, ERacingSpeedDisplayUnit::KilometresPerHour);

		VM = Build(Vehicle, 10.6, 0.5);
		TestFalse(TEXT("A 0.6 s old sample is stale at a 0.5 s limit"), VM.bVehicleDataFresh);
		TestEqual(TEXT("...and its speed is blanked, not frozen"), VM.Speed, 0.0);
		TestEqual(TEXT("...its RPM blanked"), VM.EngineRPM, 0.0f);
		TestEqual(TEXT("...its gear blanked"), VM.GearIndex, 0);

		VM = Build(Vehicle, 9.0, 0.5);
		TestFalse(TEXT("A sample stamped in the future (restart) is stale"), VM.bVehicleDataFresh);

		VM = Build(Vehicle, 1000.0, 0.0);
		TestTrue(TEXT("A limit of 0 disables the age check"), VM.bVehicleDataFresh);

		TestEqual(TEXT("Freshness agrees with FRacingTelemetryFrame::IsTimestampStaleAt"),
			Build(Vehicle, 10.5, 0.5).bVehicleDataFresh, !FRacingTelemetryFrame::IsTimestampStaleAt(10.0, 10.5, 0.5));

		VM = Build(Vehicle, NaN, 0.5);
		TestFalse(TEXT("A NaN clock reading fails closed"), VM.bVehicleDataFresh);
		VM = Build(Vehicle, 10.2, NaN);
		TestFalse(TEXT("A NaN staleness limit fails closed rather than disabling the check"), VM.bVehicleDataFresh);

		FRacingVehicleTelemetrySample BadSpeed = Vehicle;
		BadSpeed.ForwardSpeedCms = Inf;
		VM = Build(BadSpeed, 10.2, 0.5);
		TestFalse(TEXT("An infinite speed fails closed"), VM.bVehicleDataFresh);
		TestEqual(TEXT("...and never reaches the widget"), VM.Speed, 0.0);

		FRacingVehicleTelemetrySample BadRpm = Vehicle;
		BadRpm.EngineRPM = std::numeric_limits<float>::quiet_NaN();
		TestFalse(TEXT("A NaN RPM fails closed"), Build(BadRpm, 10.2, 0.5).bVehicleDataFresh);

		FRacingVehicleTelemetrySample NegativeRpm = Vehicle;
		NegativeRpm.EngineRPM = -50.0f;
		TestEqual(TEXT("A negative RPM is clamped to 0"), Build(NegativeRpm, 10.2, 0.5).EngineRPM, 0.0f);

		FRacingVehicleTelemetrySample Reversing = Vehicle;
		Reversing.ForwardSpeedCms = -300.0;
		Reversing.GearIndex = -1;
		VM = Build(Reversing, 10.2, 0.5);
		TestTrue(TEXT("Reversing keeps its sign"), VM.Speed < 0.0);
		TestEqual(TEXT("...and the reverse gear"), VM.GearIndex, -1);

		TestEqual(TEXT("mph goes through the library conversion"),
			Build(Vehicle, 10.2, 0.5, ERacingSpeedDisplayUnit::MilesPerHour).Speed,
			URacingTelemetryFunctionLibrary::ConvertSpeedCmsToDisplayUnit(5000.0, ERacingSpeedDisplayUnit::MilesPerHour));
		TestNearlyEqual(TEXT("m/s reads 50"),
			Build(Vehicle, 10.2, 0.5, ERacingSpeedDisplayUnit::MetresPerSecond).Speed, 50.0, 1.0e-9);
	}

	// -- Countdown -------------------------------------------------------------
	{
		TestEqual(TEXT("3.0 s shows 3"), Lib::GetCountdownWholeSeconds(3.0), 3);
		TestEqual(TEXT("2.0001 s shows 3 (rounded up)"), Lib::GetCountdownWholeSeconds(2.0001), 3);
		TestEqual(TEXT("1.8 s shows 2"), Lib::GetCountdownWholeSeconds(1.8), 2);
		TestEqual(TEXT("0.0001 s still shows 1"), Lib::GetCountdownWholeSeconds(0.0001), 1);
		TestEqual(TEXT("0 s shows 0"), Lib::GetCountdownWholeSeconds(0.0), 0);
		TestEqual(TEXT("Negative shows 0"), Lib::GetCountdownWholeSeconds(-1.0), 0);
		TestEqual(TEXT("NaN shows 0"), Lib::GetCountdownWholeSeconds(NaN), 0);
		TestEqual(TEXT("Infinity is non-finite and reads 0, like NaN"), Lib::GetCountdownWholeSeconds(Inf), 0);
		TestEqual(TEXT("A huge value is capped at 3600"), Lib::GetCountdownWholeSeconds(1.0e12), 3600);

		FRacingHudRaceInputs Inputs;
		Inputs.bHasRaceState = true;
		Inputs.RaceState = ERaceState::Countdown;
		Inputs.CountdownRemainingSeconds = 2.1;

		FRacingHudViewModel VM = HudSpecBuild(Inputs);
		TestTrue(TEXT("Countdown shows in Countdown"), VM.bShowCountdown);
		TestEqual(TEXT("...as 3"), VM.CountdownWholeSeconds, 3);
		TestNearlyEqual(TEXT("...with the raw remaining time"), VM.CountdownRemainingSeconds, 2.1, 1.0e-12);

		Inputs.RaceState = ERaceState::Racing;
		VM = HudSpecBuild(Inputs);
		TestFalse(TEXT("Countdown hides once Racing, whatever the input still says"), VM.bShowCountdown);
		TestEqual(TEXT("...and its number is 0"), VM.CountdownWholeSeconds, 0);

		Inputs.RaceState = ERaceState::Countdown;
		Inputs.bHasRaceState = false;
		TestFalse(TEXT("No countdown without a state machine"), HudSpecBuild(Inputs).bShowCountdown);
	}

	// -- Laps: sector numbering, in-progress gating, best-lap sourcing ---------
	{
		FRacingHudRaceInputs Inputs;
		Inputs.bHasRaceState = true;
		Inputs.RaceState = ERaceState::Racing;
		Inputs.CurrentLapNumber = 2;
		Inputs.LapsCompleted = 1;
		Inputs.ValidLapsCompleted = 1;
		Inputs.NumSectors = 3;
		Inputs.bLapInProgress = true;
		Inputs.CurrentLapElapsedSeconds = 12.5;
		Inputs.CurrentSectorIndex = 0;
		Inputs.bCurrentLapInvalid = true;

		FRacingHudViewModel VM = HudSpecBuild(Inputs);
		TestEqual(TEXT("L9: lap number passes through"), VM.CurrentLapNumber, 2);
		TestEqual(TEXT("L9: laps completed passes through"), VM.LapsCompleted, 1);
		TestEqual(TEXT("Sector index 0 shows as sector 1"), VM.CurrentSectorNumber, 1);
		TestEqual(TEXT("Lap time passes through"), VM.CurrentLapElapsedSeconds, 12.5);
		TestTrue(TEXT("An invalidated lap shows as invalid"), VM.bCurrentLapInvalid);

		Inputs.CurrentSectorIndex = 2;
		TestEqual(TEXT("The last sector index shows as sector 3"), HudSpecBuild(Inputs).CurrentSectorNumber, 3);
		Inputs.CurrentSectorIndex = 3;
		TestEqual(TEXT("An out-of-range index shows no sector"), HudSpecBuild(Inputs).CurrentSectorNumber, 0);
		Inputs.CurrentSectorIndex = INDEX_NONE;
		TestEqual(TEXT("INDEX_NONE shows no sector"), HudSpecBuild(Inputs).CurrentSectorNumber, 0);

		Inputs.CurrentSectorIndex = 1;
		Inputs.bLapInProgress = false;
		VM = HudSpecBuild(Inputs);
		TestEqual(TEXT("No lap in progress: no sector, even with an index"), VM.CurrentSectorNumber, 0);
		TestEqual(TEXT("...no running time"), VM.CurrentLapElapsedSeconds, 0.0);
		TestFalse(TEXT("...and no invalid flag"), VM.bCurrentLapInvalid);

		Inputs.bLapInProgress = true;
		Inputs.CurrentLapElapsedSeconds = NaN;
		TestEqual(TEXT("A NaN lap time reads 0"), HudSpecBuild(Inputs).CurrentLapElapsedSeconds, 0.0);

		// Last lap present, best lap absent: the best lap is NOT borrowed (RACE-002 L9).
		Inputs.bHasLastLap = true;
		Inputs.LastLapSeconds = 61.5;
		Inputs.bLastLapValid = false;
		Inputs.bLastLapSectorsConsistent = true;
		Inputs.bHasBestLap = false;
		Inputs.BestLapSeconds = 0.0;
		VM = HudSpecBuild(Inputs);
		TestTrue(TEXT("The last lap shows"), VM.bHasLastLap);
		TestEqual(TEXT("...with its time"), VM.LastLapSeconds, 61.5);
		TestFalse(TEXT("...marked invalid"), VM.bLastLapValid);
		TestFalse(TEXT("L9: an invalid last lap is never shown as the best lap"), VM.bHasBestLap);
		TestEqual(TEXT("L9: ...and the best time stays 0"), VM.BestLapSeconds, 0.0);

		Inputs.LastLapSeconds = Inf;
		VM = HudSpecBuild(Inputs);
		TestFalse(TEXT("A non-finite last lap is not shown"), VM.bHasLastLap);
		TestFalse(TEXT("...nor its validity"), VM.bLastLapValid);

		Inputs.bHasBestLap = true;
		Inputs.BestLapSeconds = NaN;
		TestFalse(TEXT("A non-finite best lap is not shown"), HudSpecBuild(Inputs).bHasBestLap);

		Inputs.LapProgressFraction = 1.5f;
		TestEqual(TEXT("Progress above 1 clamps"), HudSpecBuild(Inputs).LapProgressFraction, 1.0f);
		Inputs.LapProgressFraction = -0.25f;
		TestEqual(TEXT("Progress below 0 clamps"), HudSpecBuild(Inputs).LapProgressFraction, 0.0f);
		Inputs.LapProgressFraction = std::numeric_limits<float>::quiet_NaN();
		TestEqual(TEXT("NaN progress reads 0"), HudSpecBuild(Inputs).LapProgressFraction, 0.0f);
	}

	// -- Position: only with opponents, only when classified -------------------
	{
		FRacingHudRaceInputs Inputs;
		Inputs.bHasRaceState = true;
		Inputs.RacePosition = 1;
		Inputs.CompetitorCount = 1;
		TestFalse(TEXT("Solo: position is hidden"), HudSpecBuild(Inputs).bShowPosition);
		TestEqual(TEXT("...and reads 0"), HudSpecBuild(Inputs).RacePosition, 0);

		Inputs.CompetitorCount = 4;
		Inputs.RacePosition = 2;
		TestTrue(TEXT("P2 of 4 is shown"), HudSpecBuild(Inputs).bShowPosition);
		TestEqual(TEXT("...as 2"), HudSpecBuild(Inputs).RacePosition, 2);

		Inputs.RacePosition = 0;
		TestFalse(TEXT("Unclassified (0) is hidden"), HudSpecBuild(Inputs).bShowPosition);
		Inputs.RacePosition = 5;
		TestFalse(TEXT("P5 of 4 is hidden"), HudSpecBuild(Inputs).bShowPosition);
		Inputs.CompetitorCount = -3;
		TestEqual(TEXT("A negative competitor count reads 0"), HudSpecBuild(Inputs).CompetitorCount, 0);
	}

	// -- Delta -----------------------------------------------------------------
	{
		FRacingHudRaceInputs Inputs;
		Inputs.bHasDelta = true;
		Inputs.DeltaToBestSeconds = -0.35;
		TestTrue(TEXT("A finite delta shows"), HudSpecBuild(Inputs).bHasDelta);
		TestEqual(TEXT("...signed"), HudSpecBuild(Inputs).DeltaToBestSeconds, -0.35);
		Inputs.DeltaToBestSeconds = NaN;
		TestFalse(TEXT("A NaN delta is hidden"), HudSpecBuild(Inputs).bHasDelta);
		TestEqual(TEXT("...and reads 0"), HudSpecBuild(Inputs).DeltaToBestSeconds, 0.0);
	}

	// -- Results ---------------------------------------------------------------
	{
		FRacingHudRaceInputs Inputs;
		Inputs.bHasRaceState = true;
		Inputs.RaceState = ERaceState::Racing;
		Inputs.bResultAvailable = false;
		Inputs.ResultFinalTimeSeconds = 99.0;
		Inputs.ResultLapsCompleted = 3;

		FRacingHudViewModel VM = HudSpecBuild(Inputs);
		TestFalse(TEXT("No result: nothing shown"), VM.bShowResults);
		TestEqual(TEXT("...and result numbers are not passed through"), VM.ResultFinalTimeSeconds, 0.0);
		TestEqual(TEXT("...none of them"), VM.ResultLapsCompleted, 0);

		Inputs.bResultAvailable = true;
		Inputs.ResultValidity = ERacingRunValidity::Valid;
		Inputs.bResultHasBestLap = true;
		Inputs.ResultBestLapSeconds = 30.0;
		VM = HudSpecBuild(Inputs);
		TestTrue(TEXT("A result while still Racing is available"), VM.bResultAvailable);
		TestFalse(TEXT("...but not shown"), VM.bShowResults);

		for (const ERaceState State : { ERaceState::Finished, ERaceState::Results })
		{
			Inputs.RaceState = State;
			VM = HudSpecBuild(Inputs);
			TestTrue(FString::Printf(TEXT("A result shows in state %d"), static_cast<int32>(State)), VM.bShowResults);
			TestEqual(TEXT("...with its time"), VM.ResultFinalTimeSeconds, 99.0);
			TestEqual(TEXT("...its validity"), VM.ResultValidity, ERacingRunValidity::Valid);
			TestEqual(TEXT("...and its best lap"), VM.ResultBestLapSeconds, 30.0);
		}

		Inputs.RaceState = ERaceState::PreRace;
		TestFalse(TEXT("A stale result is not shown after a restart to PreRace"), HudSpecBuild(Inputs).bShowResults);
	}

	// -- Reset per call: nothing survives from the previous frame --------------
	{
		FRacingHudViewModel VM;
		VM.bShowResults = true;
		VM.Speed = 999.0;
		VM.bHasBestLap = true;
		VM.BestLapSeconds = 42.0;
		VM.CurrentSectorNumber = 3;
		VM.bShowCountdown = true;

		FRacingVehicleTelemetrySample Stale;
		Stale.TimestampSeconds = 0.0;
		Stale.ForwardSpeedCms = 5000.0;

		Lib::BuildHudViewModelInto(FRacingHudRaceInputs(), Stale, 100.0, 0.5, ERacingSpeedDisplayUnit::KilometresPerHour, VM);
		TestFalse(TEXT("Into resets bShowResults"), VM.bShowResults);
		TestEqual(TEXT("Into resets Speed"), VM.Speed, 0.0);
		TestFalse(TEXT("Into resets bHasBestLap"), VM.bHasBestLap);
		TestEqual(TEXT("Into resets BestLapSeconds"), VM.BestLapSeconds, 0.0);
		TestEqual(TEXT("Into resets CurrentSectorNumber"), VM.CurrentSectorNumber, 0);
		TestFalse(TEXT("Into resets bShowCountdown"), VM.bShowCountdown);
	}

	// -- No allocation, as a type property (CORE-002 M-4) ----------------------
	TestTrue(TEXT("FRacingHudRaceInputs is trivially copyable"), std::is_trivially_copyable_v<FRacingHudRaceInputs>);
	TestTrue(TEXT("FRacingHudViewModel is trivially copyable"), std::is_trivially_copyable_v<FRacingHudViewModel>);

	// -- Reachability ----------------------------------------------------------
	for (const TCHAR* Name : { TEXT("BuildHudViewModel"), TEXT("GetCountdownWholeSeconds") })
	{
		const UFunction* Function = URacingHudViewModelLibrary::StaticClass()->FindFunctionByName(FName(Name));
		TestTrue(FString::Printf(TEXT("%s is a BlueprintPure UFUNCTION"), Name),
			Function != nullptr && Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
	}

	return true;
}

// ===========================================================================
// Race integration
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingHudViewModelRaceIntegrationTest,
	"RacingSim.UI.HudViewModel.RaceIntegration",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingHudViewModelRaceIntegrationTest::RunTest(const FString& Parameters)
{
	using namespace HudViewModelSpecPrivate;

	// -- No state machine: false, and the struct is reset, not left stale ------
	{
		FRacingHudRaceInputs Inputs;
		Inputs.CurrentLapNumber = 7;
		Inputs.bResultAvailable = true;
		TestFalse(TEXT("A null state machine is refused"),
			URaceFunctionLibrary::GatherHudRaceInputs(nullptr, nullptr, nullptr, 1, Inputs));
		TestFalse(TEXT("...with no race state"), Inputs.bHasRaceState);
		TestEqual(TEXT("...and the previous lap number cleared"), Inputs.CurrentLapNumber, 0);
		TestFalse(TEXT("...and the previous result cleared"), Inputs.bResultAvailable);
	}

	FHudSpecRig Rig;
	if (!Rig.Build(*this))
	{
		return false;
	}

	FRacingHudRaceInputs Inputs;
	double CleanLapSeconds = 0.0;

	// -- PreRace ---------------------------------------------------------------
	{
		TestTrue(TEXT("PreRace: gather succeeds"), Rig.Gather(Inputs));
		TestTrue(TEXT("PreRace: has state"), Inputs.bHasRaceState);
		TestEqual(TEXT("PreRace: state"), Inputs.RaceState, ERaceState::PreRace);
		TestEqual(TEXT("PreRace: session id is the machine's"), Inputs.SessionId, Rig.Machine->GetSessionId());
		TestEqual(TEXT("PreRace: L9 lap 0"), Inputs.CurrentLapNumber, 0);
		TestEqual(TEXT("PreRace: L9 completed 0"), Inputs.LapsCompleted, 0);
		TestFalse(TEXT("PreRace: no lap in progress"), Inputs.bLapInProgress);
		TestFalse(TEXT("PreRace: no last lap"), Inputs.bHasLastLap);
		TestFalse(TEXT("PreRace: no best lap"), Inputs.bHasBestLap);
		TestFalse(TEXT("PreRace: no result"), Inputs.bResultAvailable);
		TestEqual(TEXT("PreRace: sectors from the tracker"), Inputs.NumSectors, Rig.Tracker->GetNumSectors());

		const FRacingHudViewModel VM = HudSpecBuild(Inputs, GHudSpecNowSeconds);
		TestFalse(TEXT("PreRace VM: no countdown"), VM.bShowCountdown);
		TestFalse(TEXT("PreRace VM: no results"), VM.bShowResults);
		TestFalse(TEXT("PreRace VM: solo, no position"), VM.bShowPosition);
	}

	// -- Countdown -------------------------------------------------------------
	{
		const double GridCm = -800.0;
		Rig.CurrentDistanceCm = GridCm;
		Rig.Tracker->SeedProgress(Rig.PositionAt(GridCm), GridCm);
		Rig.Machine->BeginCountdown();

		// The car on the grid publishes a sample now, on the monotonic clock (as
		// ARacingVehiclePawn does with FPlatformTime::Seconds()), then 1.2 s of countdown pass.
		const FRacingVehicleTelemetrySample GridSample = HudSpecFreshVehicle(GHudSpecNowSeconds);
		GHudSpecNowSeconds += 1.2;

		TestTrue(TEXT("Countdown: gather succeeds"), Rig.Gather(Inputs));
		TestEqual(TEXT("Countdown: state"), Inputs.RaceState, ERaceState::Countdown);
		TestNearlyEqual(TEXT("Countdown: 1.8 s of 3.0 s remain"), Inputs.CountdownRemainingSeconds, 1.8, 1.0e-6);
		TestEqual(TEXT("Countdown: the race clock has not started"), Inputs.RaceElapsedSeconds, 0.0);

		const FRacingHudViewModel VM = HudSpecBuild(Inputs, GHudSpecNowSeconds);
		TestTrue(TEXT("Countdown VM: shown"), VM.bShowCountdown);
		TestEqual(TEXT("Countdown VM: reads 2"), VM.CountdownWholeSeconds, 2);
		TestTrue(TEXT("Countdown VM: a sample stamped now on the monotonic clock is fresh"), VM.bVehicleDataFresh);

		// M3: the clock contract, both ways. A sample 1.2 s old on the monotonic clock is
		// stale, and passing the race clock (0 during the countdown) as "now" for a
		// monotonic-stamped sample fails CLOSED -- the sample reads future-stamped, never live.
		const FRacingHudViewModel OldSample = URacingHudViewModelLibrary::BuildHudViewModel(
			Inputs, GridSample, GHudSpecNowSeconds, 0.5, ERacingSpeedDisplayUnit::KilometresPerHour);
		TestFalse(TEXT("Countdown VM: a 1.2 s old sample is stale"), OldSample.bVehicleDataFresh);
		TestEqual(TEXT("...and its speed is withheld"), OldSample.Speed, 0.0);

		const FRacingHudViewModel WrongClock = URacingHudViewModelLibrary::BuildHudViewModel(
			Inputs, HudSpecFreshVehicle(GHudSpecNowSeconds), Inputs.RaceElapsedSeconds, 0.5,
			ERacingSpeedDisplayUnit::KilometresPerHour);
		TestFalse(TEXT("Countdown VM: race clock as 'now' for a monotonic sample is stale, not live"),
			WrongClock.bVehicleDataFresh);

		GHudSpecNowSeconds += HudSpecCountdownSeconds - 1.2;
		Rig.Machine->StartRace();
	}

	// -- Green, before the first crossing --------------------------------------
	{
		Rig.Drive(-400.0, 4);
		TestTrue(TEXT("Green: gather succeeds"), Rig.Gather(Inputs));
		TestEqual(TEXT("Green: state"), Inputs.RaceState, ERaceState::Racing);
		TestEqual(TEXT("Green: L9 still lap 0 before the line"), Inputs.CurrentLapNumber, 0);
		TestFalse(TEXT("Green: no lap in progress"), Inputs.bLapInProgress);
		TestEqual(TEXT("Green: no running lap time"), Inputs.CurrentLapElapsedSeconds, 0.0);

		const FRacingHudViewModel VM = HudSpecBuild(Inputs, GHudSpecNowSeconds);
		TestFalse(TEXT("Green VM: countdown gone"), VM.bShowCountdown);
		TestEqual(TEXT("Green VM: no sector"), VM.CurrentSectorNumber, 0);
		TestTrue(TEXT("Green VM: race clock running"), VM.RaceElapsedSeconds > 0.0);
	}

	// -- Lap 1 ----------------------------------------------------------------
	{
		Rig.Drive(400.0, 8);
		TestTrue(TEXT("Lap 1: gather succeeds"), Rig.Gather(Inputs));
		TestEqual(TEXT("Lap 1: L9 lap number is the tracker's"), Inputs.CurrentLapNumber, Rig.Tracker->GetCurrentLapNumber());
		TestEqual(TEXT("Lap 1: ...which is 1"), Inputs.CurrentLapNumber, 1);
		TestEqual(TEXT("Lap 1: L9 completed 0"), Inputs.LapsCompleted, 0);
		TestTrue(TEXT("Lap 1: in progress"), Inputs.bLapInProgress);
		TestFalse(TEXT("Lap 1: clean"), Inputs.bCurrentLapInvalid);
		TestEqual(TEXT("Lap 1: sector index is the tracker's"), Inputs.CurrentSectorIndex, Rig.Tracker->GetCurrentSectorIndex());
		TestFalse(TEXT("Lap 1: no last lap yet"), Inputs.bHasLastLap);

		// M-4: the non-allocating running time is the same number the allocating getter gives.
		TestTrue(TEXT("Lap 1: the running lap time is positive"), Inputs.CurrentLapElapsedSeconds > 0.0);

		// Independent of the tracker: the line (distance 0) is halfway along this 8-step
		// drive from -400 to +400, so the lap has been open about 4 steps. One step of
		// slack covers where inside the crossing step the tracker interpolates the open.
		TestNearlyEqual(TEXT("Lap 1: the running lap time is ~4 steps since the line"),
			Inputs.CurrentLapElapsedSeconds, 4.0 * HudSpecSecondsPerStep, HudSpecSecondsPerStep);

		// M-4 equivalence (not independence): the non-allocating read and the allocating one agree.
		TestNearlyEqual(TEXT("Lap 1: GetCurrentLapElapsedSeconds == GetCurrentLapTiming().LapDurationSeconds"),
			Inputs.CurrentLapElapsedSeconds, Rig.Tracker->GetCurrentLapTiming().LapDurationSeconds, 1.0e-12);
		TestEqual(TEXT("Lap 1: race time is the machine's peek"), Inputs.RaceElapsedSeconds, Rig.Machine->PeekRaceElapsedSeconds());

		// Gathering is a read: doing it again at the same instant changes nothing.
		FRacingHudRaceInputs Again;
		Rig.Gather(Again);
		TestEqual(TEXT("Lap 1: a second gather at the same instant reads the same race time"),
			Again.RaceElapsedSeconds, Inputs.RaceElapsedSeconds);
		TestEqual(TEXT("Lap 1: ...and the same lap time"), Again.CurrentLapElapsedSeconds, Inputs.CurrentLapElapsedSeconds);

		const FRacingHudViewModel VM = HudSpecBuild(Inputs, GHudSpecNowSeconds);
		TestEqual(TEXT("Lap 1 VM: sector 1"), VM.CurrentSectorNumber, 1);
		TestEqual(TEXT("Lap 1 VM: 3 sectors"), VM.NumSectors, 3);
		TestTrue(TEXT("Lap 1 VM: vehicle fresh"), VM.bVehicleDataFresh);
	}

	// -- Lap 2: one lap closed -------------------------------------------------
	{
		Rig.Drive(400.0 + Rig.LapLengthCm, HudSpecStepsPerLap);
		TestTrue(TEXT("Lap 2: gather succeeds"), Rig.Gather(Inputs));

		const FRacingLapTiming& Last = Rig.Tracker->PeekLastCompletedLap();
		const FRacingLapTiming& Best = Rig.Tracker->PeekBestValidLap();

		TestEqual(TEXT("Lap 2: L9 lap 2"), Inputs.CurrentLapNumber, 2);
		TestEqual(TEXT("Lap 2: L9 completed 1"), Inputs.LapsCompleted, 1);
		TestEqual(TEXT("Lap 2: valid completed is the tracker's"), Inputs.ValidLapsCompleted, Rig.Tracker->GetValidLapsCompleted());
		TestTrue(TEXT("Lap 2: last lap present"), Inputs.bHasLastLap);
		TestEqual(TEXT("Lap 2: last lap time is the tracker's"), Inputs.LastLapSeconds, Last.LapDurationSeconds);
		TestEqual(TEXT("Lap 2: last lap validity is the tracker's"), Inputs.bLastLapValid, Last.Validity == ERacingRunValidity::Valid);
		TestTrue(TEXT("Lap 2: ...and it is valid"), Inputs.bLastLapValid);
		TestEqual(TEXT("Lap 2: sector consistency uses the track's count"),
			Inputs.bLastLapSectorsConsistent, Last.AreSectorsConsistent(0.001, Rig.Tracker->GetNumSectors()));
		TestTrue(TEXT("Lap 2: ...and the splits are consistent"), Inputs.bLastLapSectorsConsistent);
		TestTrue(TEXT("Lap 2: best lap present"), Inputs.bHasBestLap);
		TestEqual(TEXT("Lap 2: best lap time is the tracker's best VALID lap"), Inputs.BestLapSeconds, Best.LapDurationSeconds);
		TestEqual(TEXT("Lap 2: Peek and by-value getters agree"), Best.LapDurationSeconds, Rig.Tracker->GetBestValidLap().LapDurationSeconds);
		TestEqual(TEXT("Lap 2: progress is the tracker's"),
			Inputs.LapProgressFraction, Rig.Tracker->GetProgressSample().LapProgressFraction);
		TestEqual(TEXT("Lap 2: position is the tracker's"), Inputs.RacePosition, Rig.Tracker->GetProgressSample().RacePosition);

		const FRacingHudViewModel VM = HudSpecBuild(Inputs, GHudSpecNowSeconds);
		TestTrue(TEXT("Lap 2 VM: best lap shown"), VM.bHasBestLap);
		TestEqual(TEXT("Lap 2 VM: best == last after one clean lap"), VM.BestLapSeconds, VM.LastLapSeconds);
		TestFalse(TEXT("Lap 2 VM: no results mid-race"), VM.bShowResults);

		TestFalse(TEXT("Lap 2: no result while racing"), Inputs.bResultAvailable);

		CleanLapSeconds = Inputs.BestLapSeconds;
	}

	// -- Lap 2 driven invalid, and FASTER than the clean lap 1 -------------------
	// Best must be the best VALID lap. A faster invalid lap is the case that proves it:
	// a best-lap that simply took the minimum would pick this one up. The labels say
	// "Lap 3" for the section order; the lap being driven here is lap 2 (opened at the end
	// of the previous section), and closing it opens lap 3.
	{
		const double LapCm = Rig.LapLengthCm;
		const double WideFromCm = LapCm * 1.5 - 3000.0;
		const double WideToCm = LapCm * 1.5 + 3000.0;
		const double WideOffsetCm = HudSpecGateHalfWidthCm + 600.0;

		// Round the outside of gate 2, then take gate 3 while gate 2 is still expected.
		Rig.Drive(LapCm * 1.75 + 2000.0, 200, WideFromCm, WideToCm, WideOffsetCm);
		TestTrue(TEXT("Lap 3 mid: gather succeeds"), Rig.Gather(Inputs));
		TestEqual(TEXT("Lap 3 mid: L9 lap 2 is still the one timed"), Inputs.CurrentLapNumber, 2);
		TestFalse(TEXT("Lap 3 mid: the tracker holds a fault"), Rig.Tracker->GetCurrentLapInvalidity().IsClean());
		TestTrue(TEXT("Lap 3 mid: the gatherer reports the lap invalid"), Inputs.bCurrentLapInvalid);
		TestTrue(TEXT("Lap 3 mid VM: invalid flag reaches the view model"),
			HudSpecBuild(Inputs, GHudSpecNowSeconds).bCurrentLapInvalid);

		Rig.Drive(400.0 + 2.0 * LapCm, 60);
		TestTrue(TEXT("Lap 3 closed: gather succeeds"), Rig.Gather(Inputs));

		const FRacingLapTiming& Last = Rig.Tracker->PeekLastCompletedLap();
		TestEqual(TEXT("Lap 3 closed: L9 lap 3 now timed"), Inputs.CurrentLapNumber, 3);
		TestEqual(TEXT("Lap 3 closed: L9 completed 2"), Inputs.LapsCompleted, 2);
		TestEqual(TEXT("Lap 3 closed: still one valid lap"), Inputs.ValidLapsCompleted, 1);
		TestFalse(TEXT("Lap 3 closed: the new lap is clean"), Inputs.bCurrentLapInvalid);
		TestTrue(TEXT("Lap 3 closed: last lap present"), Inputs.bHasLastLap);
		TestEqual(TEXT("Lap 3 closed: last lap is the tracker's"), Inputs.LastLapSeconds, Last.LapDurationSeconds);
		TestEqual(TEXT("Lap 3 closed: ...a shortcut"), Last.Validity, ERacingRunValidity::InvalidShortcut);
		TestFalse(TEXT("Lap 3 closed: last lap reported invalid"), Inputs.bLastLapValid);
		TestTrue(TEXT("Lap 3 closed: precondition -- the invalid lap is the faster one"),
			Inputs.LastLapSeconds < CleanLapSeconds);
		TestTrue(TEXT("Lap 3 closed: best lap still present"), Inputs.bHasBestLap);
		TestEqual(TEXT("Lap 3 closed: best lap is still the clean lap"), Inputs.BestLapSeconds, CleanLapSeconds);
		// Timing and validity are separate questions: the route was driven in order, so the splits still add up.
		TestTrue(TEXT("Lap 3 closed: the invalid lap's splits are still consistent"), Inputs.bLastLapSectorsConsistent);

		const FRacingHudViewModel VM = HudSpecBuild(Inputs, GHudSpecNowSeconds);
		TestFalse(TEXT("Lap 3 VM: last lap shown invalid"), VM.bLastLapValid);
		TestEqual(TEXT("Lap 3 VM: best is the clean lap"), VM.BestLapSeconds, CleanLapSeconds);
		TestNotEqual(TEXT("Lap 3 VM: best is not the (faster, invalid) last lap"), VM.BestLapSeconds, VM.LastLapSeconds);
	}

	// -- Finish ---------------------------------------------------------------
	{
		Rig.Machine->FinishRace();
		TestTrue(TEXT("Finish: gather succeeds"), Rig.Gather(Inputs));
		TestEqual(TEXT("Finish: state"), Inputs.RaceState, ERaceState::Finished);
		TestTrue(TEXT("Finish: the recorder froze a result"), Rig.Recorder->HasFrozenResult());
		TestTrue(TEXT("Finish: result available"), Inputs.bResultAvailable);

		const FRacingRaceResult& Frozen = Rig.Recorder->GetFrozenResult();
		TestEqual(TEXT("Finish: final time is the frozen result's"), Inputs.ResultFinalTimeSeconds, Frozen.FinalTimeSeconds);
		TestEqual(TEXT("Finish: validity is the frozen result's"), Inputs.ResultValidity, Frozen.GetValidity());
		TestEqual(TEXT("Finish: laps completed is the frozen result's"), Inputs.ResultLapsCompleted, Frozen.LapsCompleted);
		TestEqual(TEXT("Finish: valid laps is the frozen result's"), Inputs.ResultValidLapsCompleted, Frozen.ValidLapsCompleted);
		TestEqual(TEXT("Finish: best-lap presence is the frozen result's"), Inputs.bResultHasBestLap, Frozen.HasValidLap());
		TestEqual(TEXT("Finish: best lap time is the frozen result's"), Inputs.ResultBestLapSeconds, Frozen.BestLap.LapDurationSeconds);
		TestEqual(TEXT("Finish: clock fault is the frozen result's"), Inputs.bResultClockFaulted, Frozen.bRaceClockFaulted);

		const FRacingHudViewModel VM = HudSpecBuild(Inputs, GHudSpecNowSeconds);
		TestTrue(TEXT("Finish VM: results shown"), VM.bShowResults);

		// The frozen clock stays frozen however long the finish presentation runs.
		const double FrozenElapsed = Inputs.RaceElapsedSeconds;
		GHudSpecNowSeconds += 5.0;
		Rig.Gather(Inputs);
		TestEqual(TEXT("Finish: race time does not move after the finish"), Inputs.RaceElapsedSeconds, FrozenElapsed);
	}

	// -- Results --------------------------------------------------------------
	{
		Rig.Machine->ShowResults();
		TestTrue(TEXT("Results: gather succeeds"), Rig.Gather(Inputs));
		TestEqual(TEXT("Results: state"), Inputs.RaceState, ERaceState::Results);
		TestTrue(TEXT("Results VM: still shown"), HudSpecBuild(Inputs, GHudSpecNowSeconds).bShowResults);
	}

	// -- Restart: nothing from the last session reaches the HUD ---------------
	{
		Rig.Machine->Restart();
		TestTrue(TEXT("Restart: gather succeeds"), Rig.Gather(Inputs));
		TestEqual(TEXT("Restart: state"), Inputs.RaceState, ERaceState::PreRace);
		TestEqual(TEXT("Restart: session id is the machine's"), Inputs.SessionId, Rig.Machine->GetSessionId());
		TestFalse(TEXT("Restart: result cleared"), Inputs.bResultAvailable);
		TestEqual(TEXT("Restart: L9 lap 0"), Inputs.CurrentLapNumber, 0);
		TestEqual(TEXT("Restart: L9 completed 0"), Inputs.LapsCompleted, 0);
		TestFalse(TEXT("Restart: no lap in progress"), Inputs.bLapInProgress);
		TestFalse(TEXT("Restart: last lap cleared"), Inputs.bHasLastLap);
		TestFalse(TEXT("Restart: best lap cleared"), Inputs.bHasBestLap);

		const FRacingHudViewModel VM = HudSpecBuild(Inputs, GHudSpecNowSeconds);
		TestFalse(TEXT("Restart VM: results hidden"), VM.bShowResults);
		TestFalse(TEXT("Restart VM: best lap hidden"), VM.bHasBestLap);
	}

	return true;
}
