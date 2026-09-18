// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "Core/RacingHudTypes.h"
#include "Core/RacingTelemetry.h"
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

/**
 * The HUD specs' race rig: RaceResultSpec's procedural circle, arithmetic gates and fake
 * monotonic clock, driving a real state machine, lap tracker and result recorder.
 *
 * Shared by RacingSim.UI.HudViewModel.RaceIntegration (UI-001) and
 * RacingSim.UI.HudWidget.RaceIntegration (UI-002). Both run the whole race in one test and
 * FHudSpecRig::Build() re-zeroes the clock, so sharing the clock global cannot couple them
 * through test order. Game thread only.
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

	inline double GHudSpecNowSeconds = 0.0;
	inline double HudSpecTimeSource()
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

		/**
		 * UI-001 L5: a second tracker, driven in lockstep with Tracker but NOT registered with
		 * Recorder, so nothing calls ResetForNewSession() on it at Restart. Until its next
		 * Advance() it still holds the previous session's laps under the previous session id.
		 */
		TStrongObjectPtr<URaceLapTracker> Unregistered;

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

			Unregistered.Reset(URaceLapTracker::Create(GetTransientPackage(), Machine.Get(), Ruleset.Get()));
			if (!Unregistered.IsValid() || !Unregistered->ConfigureTrack(Gates, SectorStartsCm, LapLengthCm, Error))
			{
				Test.AddError(FString::Printf(TEXT("Unregistered lap tracker failed to configure: %s"), *Error));
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
				const FVector Position = PositionAt(CurrentDistanceCm, bWide ? OutwardOffsetCm : 0.0);
				Tracker->Advance(Position, CurrentDistanceCm);
				Unregistered->Advance(Position, CurrentDistanceCm);
			}
		}

		/** Place the car at DistanceCm on the racing line, for both trackers, without a step. */
		void Seed(const double DistanceCm)
		{
			CurrentDistanceCm = DistanceCm;
			Tracker->SeedProgress(PositionAt(DistanceCm), DistanceCm);
			Unregistered->SeedProgress(PositionAt(DistanceCm), DistanceCm);
		}

		bool Gather(FRacingHudRaceInputs& OutInputs, const int32 CompetitorCount = 1) const
		{
			return URaceFunctionLibrary::GatherHudRaceInputs(
				Machine.Get(), Tracker.Get(), Recorder.Get(), CompetitorCount, OutInputs);
		}
	};

	/** A vehicle sample stamped at Now, moving at 50 m/s. */
	inline FRacingVehicleTelemetrySample HudSpecFreshVehicle(const double NowSeconds)
	{
		FRacingVehicleTelemetrySample Vehicle;
		Vehicle.TimestampSeconds = NowSeconds;
		Vehicle.ForwardSpeedCms = 5000.0;
		Vehicle.EngineRPM = 6500.0f;
		Vehicle.GearIndex = 4;
		return Vehicle;
	}

	inline FRacingHudViewModel HudSpecBuild(const FRacingHudRaceInputs& Inputs, const double NowSeconds = 100.0)
	{
		return URacingHudViewModelLibrary::BuildHudViewModel(
			Inputs, HudSpecFreshVehicle(NowSeconds), NowSeconds, 0.5, ERacingSpeedDisplayUnit::KilometresPerHour);
	}
}
