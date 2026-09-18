// Copyright RacingSim. All Rights Reserved.

#include "Race/RaceFunctionLibrary.h"
#include "Race/RaceResult.h"
#include "Race/TrackCenterline.h"
#include "Race/TrackDefinitionActor.h"

#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include <limits>

/**
 * UI-001: URaceFunctionLibrary's wrappers, closing TRACK-001 L3 and RACE-003 M2.
 *
 * Same two properties as RacingSim.Core.TelemetryFunctionLibrary: each wrapper AGREES with
 * the member it names, and each is a real UFUNCTION with the flags a widget needs.
 * GatherHudRaceInputs is tested against a live session in RacingSim.UI.HudViewModel.
 * RaceIntegration, not here.
 *
 * The Smoke test spawns no actor. The track-actor wrappers are checked against the
 * ATrackDefinitionActor CDO (see RaceResultSpec's header for why the CDO is the only actor
 * this gate can obtain), with a circle authored on it for the duration of the block and
 * every touched property restored afterwards -- see FRaceLibSpecTrackFixture. The one
 * case the CDO cannot model, a destroyed (garbage) actor, is the Product test at the end.
 */

namespace RaceFunctionLibrarySpecPrivate
{
	constexpr double RaceLibSpecRadiusCm = 5000.0;
	constexpr int32 RaceLibSpecSamples = 360;
	// The authored track uses RaceResultSpec's proven 100 m radius, clear of any minimum corner radius.
	constexpr double RaceLibSpecTrackRadiusCm = 10000.0;

	bool RaceLibBuildCircle(FAutomationTestBase& Test, FTrackCenterline& OutCircle)
	{
		const double TotalCm = 2.0 * UE_DOUBLE_PI * RaceLibSpecRadiusCm;
		TArray<FVector> Locations;
		TArray<double> Distances;
		for (int32 Index = 0; Index < RaceLibSpecSamples; ++Index)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(Index) / static_cast<double>(RaceLibSpecSamples);
			Locations.Add(FVector(RaceLibSpecRadiusCm * FMath::Cos(Angle), RaceLibSpecRadiusCm * FMath::Sin(Angle), 0.0));
			Distances.Add(TotalCm * static_cast<double>(Index) / static_cast<double>(RaceLibSpecSamples));
		}

		FString Error;
		if (!OutCircle.Build(Locations, Distances, TotalCm, /*bClosedLoop*/ true, Error))
		{
			Test.AddError(FString::Printf(TEXT("Circle centerline failed to build: %s"), *Error));
			return false;
		}
		return true;
	}

	/**
	 * Borrows the ATrackDefinitionActor CDO, authors a circle on it, and restores every
	 * property it touched. A copy of RaceResultSpec's FResultSpecTrackFixture under this
	 * file's own name (unity builds); see that fixture for why all four spline channels
	 * are restored rather than locations alone.
	 */
	struct FRaceLibSpecTrackFixture
	{
		ATrackDefinitionActor* Track = nullptr;

		FRaceLibSpecTrackFixture()
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

				TArray<FVector> Points;
				for (int32 Index = 0; Index < 12; ++Index)
				{
					const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(Index) / 12.0;
					Points.Add(FVector(RaceLibSpecTrackRadiusCm * FMath::Cos(Angle), RaceLibSpecTrackRadiusCm * FMath::Sin(Angle), 0.0));
				}
				Spline->SetClosedLoop(true, /*bUpdateSpline*/ false);
				Spline->SetSplinePoints(Points, ESplineCoordinateSpace::Local, /*bUpdateSpline*/ true);
			}

			Track->TrackId = FName(TEXT("Track.Test.RaceLib"));
			Track->RebuildTrackData();
		}

		~FRaceLibSpecTrackFixture()
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

		FRaceLibSpecTrackFixture(const FRaceLibSpecTrackFixture&) = delete;
		FRaceLibSpecTrackFixture& operator=(const FRaceLibSpecTrackFixture&) = delete;

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

	void RaceLibExpectFunction(FAutomationTestBase& Test, const TCHAR* Name, const EFunctionFlags Required, const EFunctionFlags Forbidden)
	{
		const UFunction* Function = URaceFunctionLibrary::StaticClass()->FindFunctionByName(FName(Name));
		Test.TestNotNull(FString::Printf(TEXT("%s is a UFUNCTION"), Name), Function);
		if (Function != nullptr)
		{
			Test.TestTrue(FString::Printf(TEXT("%s carries the required Blueprint flag"), Name), Function->HasAllFunctionFlags(Required));
			Test.TestFalse(FString::Printf(TEXT("%s does not carry a forbidden flag"), Name), Function->HasAnyFunctionFlags(Forbidden));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceFunctionLibraryTest,
	"RacingSim.Race.FunctionLibrary",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRaceFunctionLibraryTest::RunTest(const FString& Parameters)
{
	using namespace RaceFunctionLibrarySpecPrivate;
	using Lib = URaceFunctionLibrary;

	// -- Centerline struct: wrapper == member ---------------------------------
	{
		FTrackCenterline Circle;
		if (!RaceLibBuildCircle(*this, Circle))
		{
			return false;
		}

		const double LengthCm = Circle.GetLengthCm();

		TestEqual(TEXT("IsCenterlineValid agrees"), Lib::IsCenterlineValid(Circle), Circle.IsValid());
		TestTrue(TEXT("...and the circle is valid"), Lib::IsCenterlineValid(Circle));
		TestEqual(TEXT("GetCenterlineLengthCm agrees"), Lib::GetCenterlineLengthCm(Circle), LengthCm);

		for (const double DistanceCm : { 0.0, 1234.5, -250.0, LengthCm + 10.0, 3.0 * LengthCm - 1.0 })
		{
			TestEqual(FString::Printf(TEXT("WrapCenterlineDistanceCm agrees at %f"), DistanceCm),
				Lib::WrapCenterlineDistanceCm(Circle, DistanceCm), Circle.WrapDistanceCm(DistanceCm));

			const float Fraction = Lib::GetCenterlineLapProgressFraction(Circle, DistanceCm);
			TestTrue(FString::Printf(TEXT("Lap progress fraction is in 0..1 at %f"), DistanceCm), Fraction >= 0.0f && Fraction <= 1.0f);
			TestNearlyEqual(FString::Printf(TEXT("...and equals wrapped distance / length at %f"), DistanceCm),
				static_cast<double>(Fraction), Circle.WrapDistanceCm(DistanceCm) / LengthCm, 1.0e-6);
		}

		TestEqual(TEXT("GetCenterlineSignedDistanceDeltaCm agrees across the seam"),
			Lib::GetCenterlineSignedDistanceDeltaCm(Circle, LengthCm - 100.0, 100.0),
			Circle.GetSignedDistanceDeltaCm(LengthCm - 100.0, 100.0));
		TestEqual(TEXT("GetCenterlineSignedDistanceDeltaCm agrees backwards"),
			Lib::GetCenterlineSignedDistanceDeltaCm(Circle, 500.0, 200.0),
			Circle.GetSignedDistanceDeltaCm(500.0, 200.0));

		TestEqual(TEXT("A non-finite distance reads as zero progress"),
			Lib::GetCenterlineLapProgressFraction(Circle, std::numeric_limits<double>::quiet_NaN()), 0.0f);

		const FTrackCenterline Invalid;
		TestFalse(TEXT("A default centerline is invalid"), Lib::IsCenterlineValid(Invalid));
		TestEqual(TEXT("...and its lap progress is zero, not a divide by zero"),
			Lib::GetCenterlineLapProgressFraction(Invalid, 100.0), 0.0f);
	}

	// -- Query lateral offset: validity is returned, not folded into 0 ---------
	{
		FTrackCenterlineQuery Query;
		Query.bValid = true;
		Query.bLateralOffsetValid = true;
		Query.LateralOffsetCm = -42.5;

		bool bValid = false;
		TestEqual(TEXT("A valid query returns its offset"), Lib::GetQueryLateralOffsetCm(Query, bValid), -42.5);
		TestTrue(TEXT("...and says so"), bValid);

		Query.bLateralOffsetValid = false;
		bValid = true;
		TestEqual(TEXT("An underived sideways axis returns 0"), Lib::GetQueryLateralOffsetCm(Query, bValid), 0.0);
		TestFalse(TEXT("...flagged invalid, so 0 cannot be mistaken for dead centre"), bValid);

		Query.bValid = false;
		Query.bLateralOffsetValid = true;
		bValid = true;
		Lib::GetQueryLateralOffsetCm(Query, bValid);
		TestFalse(TEXT("A failed query is invalid whatever the axis flag says"), bValid);
	}

	// -- Track actor: null-safe, and agrees with the actor ---------------------
	{
		TestEqual(TEXT("Null track length is 0 cm"), Lib::GetTrackLengthCm(nullptr), 0.0);
		TestEqual(TEXT("Null track length is 0 m"), Lib::GetTrackLengthMetres(nullptr), 0.0);
		TestEqual(TEXT("Null track progress is 0"), Lib::GetTrackLapProgressFraction(nullptr, 100.0), 0.0f);
		TestEqual(TEXT("Null track wrap is 0"), Lib::WrapTrackDistanceCm(nullptr, 100.0), 0.0);
		TestEqual(TEXT("Null track delta is 0"), Lib::GetTrackSignedDistanceDeltaCm(nullptr, 0.0, 100.0), 0.0);

		// An authored track, not the empty CDO: on a zero-length centerline every wrapper
		// and every member returns 0, so "agrees" would hold for a wrapper that ignored
		// its input entirely.
		const FRaceLibSpecTrackFixture Fixture;
		const ATrackDefinitionActor* Track = Fixture.Track;
		if (TestNotNull(TEXT("The ATrackDefinitionActor CDO is available"), Track))
		{
			TestTrue(TEXT("Precondition: the authored centerline is valid"), Track->GetCenterline().IsValid());
			TestTrue(TEXT("Precondition: ...and has length"), Track->GetTrackLengthCm() > 0.0);
			TestTrue(TEXT("Precondition: 777 cm is a non-trivial wrap input"),
				Track->GetCenterline().WrapDistanceCm(Track->GetTrackLengthCm() + 777.0) > 0.0);
			TestTrue(TEXT("Precondition: the delta is non-zero"),
				!FMath::IsNearlyZero(Track->GetCenterline().GetSignedDistanceDeltaCm(10.0, 777.0)));
			TestTrue(TEXT("GetTrackLapProgressFraction is strictly between 0 and 1 mid-lap"),
				Lib::GetTrackLapProgressFraction(Track, 777.0) > 0.0f && Lib::GetTrackLapProgressFraction(Track, 777.0) < 1.0f);
			TestEqual(TEXT("WrapTrackDistanceCm agrees past the seam"),
				Lib::WrapTrackDistanceCm(Track, Track->GetTrackLengthCm() + 777.0),
				Track->GetCenterline().WrapDistanceCm(Track->GetTrackLengthCm() + 777.0));

			TestEqual(TEXT("GetTrackLengthCm agrees with the actor"), Lib::GetTrackLengthCm(Track), Track->GetTrackLengthCm());
			TestEqual(TEXT("GetTrackLengthMetres agrees with the actor"), Lib::GetTrackLengthMetres(Track), Track->GetTrackLengthMetres());
			TestEqual(TEXT("WrapTrackDistanceCm agrees with the actor's centerline"),
				Lib::WrapTrackDistanceCm(Track, 777.0), Track->GetCenterline().WrapDistanceCm(777.0));
			TestEqual(TEXT("GetTrackSignedDistanceDeltaCm agrees with the actor's centerline"),
				Lib::GetTrackSignedDistanceDeltaCm(Track, 10.0, 777.0), Track->GetCenterline().GetSignedDistanceDeltaCm(10.0, 777.0));
			TestEqual(TEXT("GetTrackLapProgressFraction agrees with the centerline wrapper"),
				Lib::GetTrackLapProgressFraction(Track, 777.0), Lib::GetCenterlineLapProgressFraction(Track->GetCenterline(), 777.0));
		}
	}

	// -- Frozen result: wrapper == member --------------------------------------
	{
		FRacingRaceResult Result;
		Result.bFrozen = true;
		Result.FinalTimeSeconds = 91.25;
		Result.LapsCompleted = 1;
		Result.ValidLapsCompleted = 1;
		Result.BestLap.LapNumber = 1;
		Result.BestLap.LapDurationSeconds = 88.0;
		Result.BestLap.Validity = ERacingRunValidity::Valid;

		TestEqual(TEXT("GetResultValidity agrees"), Lib::GetResultValidity(Result), Result.GetValidity());
		TestEqual(TEXT("ResultHasValidLap agrees"), Lib::ResultHasValidLap(Result), Result.HasValidLap());
		TestEqual(TEXT("ResultToString agrees"), Lib::ResultToString(Result), Result.ToString());

		FString MemberReason;
		const bool bMemberSubmittable = Result.IsSubmittable(&MemberReason);

		FString WrapperReason = TEXT("stale text a Blueprint variable might still hold");
		const bool bWrapperSubmittable = Lib::IsResultSubmittable(Result, WrapperReason);
		TestEqual(TEXT("IsResultSubmittable agrees"), bWrapperSubmittable, bMemberSubmittable);
		TestEqual(TEXT("...with the same reason, the stale text replaced"), WrapperReason, MemberReason);
		TestFalse(TEXT("A non-authoritative default build is not submittable"), bWrapperSubmittable);

		FString MemberQuery;
		FString MemberQueryReason;
		const bool bMemberQuery = Result.MakeSubmissionQueryString(MemberQuery, MemberQueryReason);
		FString WrapperQuery;
		FString WrapperQueryReason;
		TestEqual(TEXT("MakeResultSubmissionQueryString agrees"),
			Lib::MakeResultSubmissionQueryString(Result, WrapperQuery, WrapperQueryReason), bMemberQuery);
		TestEqual(TEXT("...on the query"), WrapperQuery, MemberQuery);
		TestEqual(TEXT("...and the reason"), WrapperQueryReason, MemberQueryReason);
	}

	// -- Frozen result, the POSITIVE case: a wrapper that always said "no" would pass above --
	{
		FRacingRaceResult Publishable;
		Publishable.bFrozen = true;
		Publishable.FinalTimeSeconds = 91.25;
		Publishable.LapsCompleted = 1;
		Publishable.ValidLapsCompleted = 1;
		Publishable.BestLap.LapNumber = 1;
		Publishable.BestLap.LapDurationSeconds = 88.0;
		Publishable.BestLap.Validity = ERacingRunValidity::Valid;
		Publishable.bTrackValidated = true;

		// Every field FRacingSimVersionStamp::IsPublishable() requires, and nothing else.
		FRacingSimVersionStamp& Version = Publishable.Version;
		Version.GameBuildId.Scheme = ERacingBuildIdScheme::Explicit;
		Version.GameBuildId.bIsAuthoritative = true;
		Version.GameBuildId.Value = TEXT("ci-2026.08.21+4417");
		Version.EngineVersion = TEXT("5.8.0");
		Version.TrackVersion.AssetId = FName(TEXT("Track.Test.RaceLib"));
		Version.TrackVersion.SchemaVersion = 1;
		Version.TrackVersion.ContentHash = 0x0DD0C1C2;
		Version.CarSpecVersion.AssetId = FName(TEXT("Car.Test.Prototype"));
		Version.CarSpecVersion.SchemaVersion = 1;
		Version.CarSpecVersion.ContentHash = 0x0CA20001;
		Version.RulesetVersion.AssetId = FName(TEXT("Ruleset.Test.RaceLib"));
		Version.RulesetVersion.SchemaVersion = 1;
		Version.PhysicsPolicyVersion = 1;
		Version.InputDeviceType = ERacingInputDeviceType::Wheel;
		Version.Validity = ERacingRunValidity::Valid;

		FString MemberReason;
		const bool bMemberSubmittable = Publishable.IsSubmittable(&MemberReason);
		TestTrue(FString::Printf(TEXT("Precondition: the member accepts this result (%s)"), *MemberReason), bMemberSubmittable);

		FString WrapperReason = TEXT("stale text a Blueprint variable might still hold");
		TestTrue(TEXT("IsResultSubmittable accepts a publishable result"), Lib::IsResultSubmittable(Publishable, WrapperReason));
		TestTrue(TEXT("...and clears the stale reason"), WrapperReason.IsEmpty());

		FString MemberQuery;
		FString MemberQueryReason;
		Publishable.MakeSubmissionQueryString(MemberQuery, MemberQueryReason);

		FString WrapperQuery;
		FString WrapperQueryReason = TEXT("stale");
		TestTrue(TEXT("MakeResultSubmissionQueryString succeeds for a publishable result"),
			Lib::MakeResultSubmissionQueryString(Publishable, WrapperQuery, WrapperQueryReason));
		TestFalse(TEXT("...with a non-empty query"), WrapperQuery.IsEmpty());
		TestEqual(TEXT("...identical to the member's"), WrapperQuery, MemberQuery);
		TestTrue(TEXT("...and no reason"), WrapperQueryReason.IsEmpty());
	}

	// -- Reachability ----------------------------------------------------------
	for (const TCHAR* Name : {
		TEXT("GetResultValidity"), TEXT("ResultHasValidLap"), TEXT("IsResultSubmittable"),
		TEXT("MakeResultSubmissionQueryString"), TEXT("ResultToString"),
		TEXT("IsCenterlineValid"), TEXT("GetCenterlineLengthCm"), TEXT("WrapCenterlineDistanceCm"),
		TEXT("GetCenterlineSignedDistanceDeltaCm"), TEXT("GetCenterlineLapProgressFraction"), TEXT("GetQueryLateralOffsetCm"),
		TEXT("GetTrackLengthCm"), TEXT("GetTrackLengthMetres"), TEXT("GetTrackLapProgressFraction"),
		TEXT("WrapTrackDistanceCm"), TEXT("GetTrackSignedDistanceDeltaCm") })
	{
		RaceLibExpectFunction(*this, Name, FUNC_BlueprintPure, FUNC_None);
	}

	// UI-002 made the gatherer read-only (it peeks the countdown instead of sampling it), so it
	// is Pure: re-evaluating it once per connected output pin cannot move any clock.
	RaceLibExpectFunction(*this, TEXT("GatherHudRaceInputs"), FUNC_BlueprintPure, FUNC_None);

	return true;
}

/**
 * UI-001 N3 (closed at UI-002): the track wrappers test IsValid(), not != nullptr, so a
 * track actor that has been destroyed -- marked garbage but not yet collected -- reads the
 * same 0 defaults as a null one instead of its stale cached centerline.
 *
 * ProductFilter, not Smoke: it spawns a real (non-template) actor, which the smoke window
 * cannot do -- see Docs/Environment.md. The CDO cannot stand in, because a CDO is never
 * marked garbage.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRaceFunctionLibraryGarbageTrackTest,
	"RacingSim.Race.FunctionLibrary.GarbageTrack",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FRaceFunctionLibraryGarbageTrackTest::RunTest(const FString& Parameters)
{
	using namespace RaceFunctionLibrarySpecPrivate;
	using Lib = URaceFunctionLibrary;

	FTestWorldWrapper WorldWrapper;
	if (!WorldWrapper.CreateTestWorld(EWorldType::Game) || WorldWrapper.GetTestWorld() == nullptr)
	{
		AddError(TEXT("FTestWorldWrapper::CreateTestWorld(EWorldType::Game) failed."));
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();

	// Deferred, so construction bakes the authored circle rather than the default two-point
	// spline (which is too short for the generated gates and would log a warning).
	ATrackDefinitionActor* Track = World->SpawnActorDeferred<ATrackDefinitionActor>(
		ATrackDefinitionActor::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("The track actor spawns"), Track))
	{
		return false;
	}
	Track->SetFlags(RF_Transient);

	if (USplineComponent* Spline = Track->GetCenterlineSpline())
	{
		TArray<FVector> Points;
		for (int32 Index = 0; Index < 12; ++Index)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(Index) / 12.0;
			Points.Add(FVector(RaceLibSpecTrackRadiusCm * FMath::Cos(Angle), RaceLibSpecTrackRadiusCm * FMath::Sin(Angle), 0.0));
		}
		Spline->SetClosedLoop(true, /*bUpdateSpline*/ false);
		Spline->SetSplinePoints(Points, ESplineCoordinateSpace::Local, /*bUpdateSpline*/ true);
	}
	Track->TrackId = FName(TEXT("Track.Test.RaceLibGarbage"));
	Track->FinishSpawning(FTransform::Identity);
	Track->RebuildTrackData();

	// Live: the wrappers agree with the actor, so the zeros below are the guard, not a blank track.
	const double LiveLengthCm = Track->GetTrackLengthCm();
	TestTrue(TEXT("Live: precondition -- the track is the authored circle, not the default spline"),
		LiveLengthCm > 0.9 * 2.0 * UE_DOUBLE_PI * RaceLibSpecTrackRadiusCm);
	TestEqual(TEXT("Live: GetTrackLengthCm is the actor's"), Lib::GetTrackLengthCm(Track), LiveLengthCm);

	TestTrue(TEXT("DestroyActor succeeds"), World->DestroyActor(Track));
	TestFalse(TEXT("Garbage: precondition -- IsValid() is false"), IsValid(Track));
	// Not yet collected, so the member still answers with the stale centerline: that is what
	// a != nullptr guard would have passed through.
	TestEqual(TEXT("Garbage: precondition -- the stale member still reports the old length"),
		Track->GetTrackLengthCm(), LiveLengthCm);

	TestEqual(TEXT("Garbage track length is 0 cm, as for null"), Lib::GetTrackLengthCm(Track), Lib::GetTrackLengthCm(nullptr));
	TestEqual(TEXT("Garbage track length is 0 m, as for null"), Lib::GetTrackLengthMetres(Track), Lib::GetTrackLengthMetres(nullptr));
	TestEqual(TEXT("Garbage track progress is 0, as for null"),
		Lib::GetTrackLapProgressFraction(Track, 777.0), Lib::GetTrackLapProgressFraction(nullptr, 777.0));
	TestEqual(TEXT("Garbage track wrap is 0, as for null"),
		Lib::WrapTrackDistanceCm(Track, LiveLengthCm + 777.0), Lib::WrapTrackDistanceCm(nullptr, LiveLengthCm + 777.0));
	TestEqual(TEXT("Garbage track delta is 0, as for null"),
		Lib::GetTrackSignedDistanceDeltaCm(Track, 10.0, 777.0), Lib::GetTrackSignedDistanceDeltaCm(nullptr, 10.0, 777.0));
	TestEqual(TEXT("...and that default is 0"), Lib::GetTrackLengthCm(Track), 0.0);

	Track = nullptr;
	WorldWrapper.DestroyTestWorld(/*bForceGarbageCollect*/ false);
	return true;
}
