// Copyright RacingSim. All Rights Reserved.

#include "Race/TrackDefinitionActor.h"
#include "Race/TrackCenterline.h"
#include "Core/RacingSimBuildId.h"
#include "Core/RacingSimUnits.h"

#include "Components/SplineComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#include <limits>

/**
 * TRACK-001: ATrackDefinitionActor.
 *
 * THE FIXTURE IS THE CLASS DEFAULT OBJECT, and that needs justifying, because it
 * is not the obvious choice. Two more obvious ones were tried and both crash
 * UnrealEditor-Cmd under this project's only automation gate
 * (`Automation RunFilter Smoke`, -nullrhi, per Docs/Environment.md):
 *
 *   1. NewObject<ATrackDefinitionActor>(GetTransientPackage()) -- what RACE-001
 *      does for URaceStateMachine. Dies with
 *        Assertion failed: RegisteredElementType
 *        [Elements/Framework/TypedElementRegistry.h:536]
 *        ... ATrackDefinitionActor::ATrackDefinitionActor() -> CreateDefaultSubobject
 *      The Typed Element Framework requires an actor to be created through the
 *      spawn path, not a bare NewObject.
 *
 *   2. UWorld::CreateWorld(EWorldType::Game) + SpawnActor. Dies with a bare
 *      "Fatal error!" (access violation, no assertion) inside UWorld::CreateWorld
 *      itself, before any actor is spawned.
 *
 * Both failures are recorded rather than quietly worked around, because each looks
 * like the correct approach and the next person will otherwise re-derive them at
 * the same cost.
 *
 * The CDO already exists -- it is constructed once at module load, through the
 * engine's own path, complete with its default subobjects -- so it needs neither a
 * world nor an object-creation path that this harness cannot provide. The price is
 * that it is SHARED process-wide, so every test here snapshots the authored
 * properties in its fixture's constructor and restores them in the destructor.
 * That keeps the tests order-independent and leaves no state behind, which
 * `.claude/rules/race-tests.md` requires.
 *
 * WHAT THIS COSTS IN COVERAGE, stated plainly rather than buried: the spawn path
 * itself -- OnConstruction, BeginPlay's validation logging, and PostLoad -- is NOT
 * exercised by these tests. Those need a functional test in a real map, which is
 * TEST-001/TRACK-002 territory and is called out in the ticket as owed. What IS
 * exercised is every piece of authored-data logic: baking, sector lookup, grid and
 * reset generation, progress arithmetic, content hashing and validation.
 *
 * The geometry is a 100 m radius circle built from 12 spline points. It is not
 * traced from anything; a circle is used because its circumference is closed-form,
 * so the length assertions compare against mathematics.
 *
 * What is NOT tested here, on purpose:
 *
 *   - anything about laps, gates or crossing direction. TRACK-001 is
 *     checkpoint-agnostic and asserting lap behaviour here would legitimise
 *     exactly the dependency CLAUDE.md forbids.
 *   - USplineComponent's own interpolation accuracy. GetTrackLengthCm() is
 *     asserted to equal GetSplineLength() exactly; whether the engine's spline
 *     approximates a circle well is the engine's test, not this project's.
 */

namespace TrackDefinitionSpecPrivate
{
	constexpr double SpecRadiusCm = 10000.0;      // 100 m
	constexpr int32 SpecSplinePoints = 12;

	/**
	 * Authors a closed circular track onto the ATrackDefinitionActor CDO and puts
	 * every property it touched back afterwards.
	 *
	 * Named uniquely rather than placed in an anonymous namespace: unity builds
	 * concatenate translation units, and duplicate anonymous symbols across a blob
	 * are a redefinition rather than two file-local helpers.
	 */
	struct FTrackSpecCircleFixture
	{
		ATrackDefinitionActor* Track = nullptr;

		FTrackSpecCircleFixture()
		{
			Track = GetMutableDefault<ATrackDefinitionActor>();
			if (!Track)
			{
				return;
			}

			// Snapshot every authored property, so the restore below is exhaustive by
			// construction rather than by whoever remembered to add a line.
			SavedTrackId = Track->TrackId;
			SavedSampleSpacingCm = Track->CenterlineSampleSpacingCm;
			SavedSectorStartsCm = Track->SectorStartDistancesCm;
			SavedNumGridSlots = Track->NumGridSlots;
			SavedGridPoleSetbackCm = Track->GridPoleSetbackCm;
			SavedGridSlotSpacingCm = Track->GridSlotSpacingCm;
			SavedGridSlotLateralOffsetCm = Track->GridSlotLateralOffsetCm;
			SavedPoseHeightOffsetCm = Track->PoseHeightOffsetCm;
			SavedResetSampleSpacingCm = Track->ResetSampleSpacingCm;

			USplineComponent* Spline = Track->GetCenterlineSpline();
			if (Spline)
			{
				bSavedClosedLoop = Spline->IsClosedLoop();
				const int32 SavedPointCount = Spline->GetNumberOfSplinePoints();
				SavedSplinePoints.Reserve(SavedPointCount);
				for (int32 Index = 0; Index < SavedPointCount; ++Index)
				{
					SavedSplinePoints.Add(Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local));
				}
			}

			Track->TrackId = FName(TEXT("Track.Test.Circle"));

			if (Spline)
			{
				TArray<FVector> Points;
				Points.Reserve(SpecSplinePoints);
				for (int32 Index = 0; Index < SpecSplinePoints; ++Index)
				{
					const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(Index) / static_cast<double>(SpecSplinePoints);
					Points.Add(FVector(SpecRadiusCm * FMath::Cos(Angle), SpecRadiusCm * FMath::Sin(Angle), 0.0));
				}

				Spline->SetClosedLoop(true, /*bUpdateSpline*/ false);
				Spline->SetSplinePoints(Points, ESplineCoordinateSpace::Local, /*bUpdateSpline*/ true);
			}

			Track->RebuildTrackData();
		}

		~FTrackSpecCircleFixture()
		{
			if (!Track)
			{
				return;
			}

			Track->TrackId = SavedTrackId;
			Track->CenterlineSampleSpacingCm = SavedSampleSpacingCm;
			Track->SectorStartDistancesCm = SavedSectorStartsCm;
			Track->NumGridSlots = SavedNumGridSlots;
			Track->GridPoleSetbackCm = SavedGridPoleSetbackCm;
			Track->GridSlotSpacingCm = SavedGridSlotSpacingCm;
			Track->GridSlotLateralOffsetCm = SavedGridSlotLateralOffsetCm;
			Track->PoseHeightOffsetCm = SavedPoseHeightOffsetCm;
			Track->ResetSampleSpacingCm = SavedResetSampleSpacingCm;

			if (USplineComponent* Spline = Track->GetCenterlineSpline())
			{
				Spline->SetClosedLoop(bSavedClosedLoop, /*bUpdateSpline*/ false);
				Spline->SetSplinePoints(SavedSplinePoints, ESplineCoordinateSpace::Local, /*bUpdateSpline*/ true);
			}

			Track->RebuildTrackData();
		}

		FTrackSpecCircleFixture(const FTrackSpecCircleFixture&) = delete;
		FTrackSpecCircleFixture& operator=(const FTrackSpecCircleFixture&) = delete;

	private:
		FName SavedTrackId;
		double SavedSampleSpacingCm = 0.0;
		TArray<double> SavedSectorStartsCm;
		int32 SavedNumGridSlots = 0;
		double SavedGridPoleSetbackCm = 0.0;
		double SavedGridSlotSpacingCm = 0.0;
		double SavedGridSlotLateralOffsetCm = 0.0;
		double SavedPoseHeightOffsetCm = 0.0;
		double SavedResetSampleSpacingCm = 0.0;
		TArray<FVector> SavedSplinePoints;
		bool bSavedClosedLoop = false;
	};

	/** Three equal sectors on an already-built track. */
	void SetThreeSectors(ATrackDefinitionActor& Track)
	{
		const double LengthCm = Track.GetTrackLengthCm();
		Track.SectorStartDistancesCm = { 0.0, LengthCm / 3.0, 2.0 * LengthCm / 3.0 };
	}
}

// ===========================================================================
// Bake, length and the SI boundary
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrackDefinitionBakeTest,
	"RacingSim.Race.TrackBake",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FTrackDefinitionBakeTest::RunTest(const FString& Parameters)
{
	using namespace TrackDefinitionSpecPrivate;

	FTrackSpecCircleFixture Fixture;
	ATrackDefinitionActor* Track = Fixture.Track;
	if (!Track)
	{
		AddError(TEXT("Class default object for ATrackDefinitionActor is unavailable."));
		return false;
	}

	USplineComponent* Spline = Track->GetCenterlineSpline();
	TestNotNull(TEXT("Centerline spline default subobject exists"), Spline);
	if (!Spline)
	{
		return false;
	}

	// The assumption every geometric assertion below rests on: the actor is spawned
	// at the world origin with no rotation or scale, so the spline's
	// component-to-world transform is identity and its world space equals its local
	// space. Asserted rather than assumed -- if a future spawn transform changes,
	// this fails here instead of silently moving every expected coordinate.
	TestTrue(TEXT("Spline component-to-world is identity at the origin"),
		Spline->GetComponentTransform().Equals(FTransform::Identity, 1.0e-6));

	TestTrue(TEXT("Track data built"), Track->IsTrackDataBuilt());
	TestTrue(TEXT("Baked centerline is valid"), Track->GetCenterline().IsValid());
	TestTrue(TEXT("Baked centerline is closed"), Track->GetCenterline().IsClosedLoop());

	// The published track length is the SPLINE's arc length, not the chord sum of
	// the baked samples. That distinction is the whole reason RebuildTrackData
	// calls Build() with an explicit total rather than BuildFromPolyline.
	// GetSplineLength() returns FLOAT -- the engine's spline distance API is float
	// throughout. The cast is explicit both to disambiguate the TestNearlyEqual
	// overload set and to mark the precision boundary RebuildTrackData documents.
	const double LengthCm = Track->GetTrackLengthCm();
	TestNearlyEqual(TEXT("Track length is the spline's own arc length"),
		LengthCm, static_cast<double>(Spline->GetSplineLength()), 1.0e-6);
	TestTrue(TEXT("Track length is positive"), LengthCm > 0.0);

	// Sanity check on the fixture, not on the engine: a 12-point closed spline
	// through a 100 m circle should be within 1% of the true circumference.
	const double TrueCircumferenceCm = 2.0 * UE_DOUBLE_PI * SpecRadiusCm;
	TestTrue(TEXT("Fixture is approximately the intended circle"),
		FMath::Abs(LengthCm - TrueCircumferenceCm) < TrueCircumferenceCm * 0.01);

	// UNIT BOUNDARY: storage is centimetres (CORE-002), SI is derived.
	TestNearlyEqual(TEXT("Length in metres is cm/100"),
		Track->GetTrackLengthMetres(), RacingSim::Units::CmToMetres(LengthCm), 1.0e-9);
	TestNearlyEqual(TEXT("Length in kilometres is cm/100000"),
		Track->GetTrackLengthKilometres(), RacingSim::Units::CmToKilometres(LengthCm), 1.0e-12);
	TestNearlyEqual(TEXT("Metres and kilometres agree"),
		Track->GetTrackLengthMetres() / 1000.0, Track->GetTrackLengthKilometres(), 1.0e-9);

	// Sample spacing: the bake divides the lap into whole segments at or below the
	// authored spacing, never above it.
	TestTrue(TEXT("Baked spacing does not exceed the authored spacing"),
		Track->GetCenterline().GetSampleSpacingCm() <= Track->CenterlineSampleSpacingCm + 1.0e-6);
	TestEqual(TEXT("Closed bake has one segment per sample"),
		Track->GetCenterline().NumSegments(), Track->GetCenterline().NumSamples());

	// -- Staleness after a runtime re-author ---------------------------------
	{
		// KNOWN LIMITATION, asserted rather than glossed over.
		//
		// bTrackDataBuilt is a "has anything been baked" flag, not a dirty flag. If
		// authored data changes at RUNTIME without a rebuild -- from a construction
		// script, a spawn-time setter, or a test -- the queries keep answering from
		// the previous bake.
		//
		// This is safe in the editor (PostEditChangeProperty rebuilds) and safe for a
		// placed level actor (OnConstruction/PostLoad/BeginPlay rebuild), so it is
		// accepted for TRACK-001 rather than papered over with a change counter that
		// nothing yet needs. Pinning it here means a future ticket that adds dirty
		// tracking sees this test fail and updates it deliberately.
		USplineComponent* MutableSpline = Track->GetCenterlineSpline();
		const FVector MovedFrom = MutableSpline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::Local);
		MutableSpline->SetLocationAtSplinePoint(0, MovedFrom * 2.0, ESplineCoordinateSpace::Local, true);

		TestNearlyEqual(TEXT("Re-authoring without a rebuild leaves the previous bake in place"),
			Track->GetTrackLengthCm(), LengthCm, 1.0e-6);

		TestTrue(TEXT("An explicit rebuild succeeds"), Track->RebuildTrackData());
		TestTrue(TEXT("...and the length now reflects the moved point"),
			FMath::Abs(Track->GetTrackLengthCm() - LengthCm) > 1.0);

		// Put the fixture geometry back for the assertions below.
		MutableSpline->SetLocationAtSplinePoint(0, MovedFrom, ESplineCoordinateSpace::Local, true);
		Track->RebuildTrackData();
		TestNearlyEqual(TEXT("Restoring the point restores the length"),
			Track->GetTrackLengthCm(), LengthCm, 1.0e-6);
	}

	// -- Start/finish is the distance origin, by definition ------------------
	{
		const FTransform StartFinish = Track->GetStartFinishTransform();
		const FTransform AtZero = Track->GetCenterline().GetTransformAtDistanceCm(0.0);
		TestTrue(TEXT("Start/finish transform is the centerline at distance zero"),
			StartFinish.Equals(AtZero, 1.0e-6));

		// The circle starts at angle 0, i.e. (R, 0, 0).
		TestNearlyEqual(TEXT("Start/finish X is the circle's start"), StartFinish.GetLocation().X, SpecRadiusCm, 10.0);
		TestNearlyEqual(TEXT("Start/finish Y is zero"), StartFinish.GetLocation().Y, 0.0, 10.0);

		// Projecting the start/finish location back must give distance ~0 (or ~L,
		// which wraps to the same place).
		const FTrackCenterlineQuery Query = Track->FindNearestCenterlinePoint(StartFinish.GetLocation());
		TestTrue(TEXT("Start/finish projects back onto the centerline"), Query.bValid);
		TestNearlyEqual(TEXT("...at distance zero"),
			Track->GetCenterline().GetSignedDistanceDeltaCm(0.0, Query.DistanceAlongCm), 0.0, 1.0);
	}

	// -- Progress ranking key ------------------------------------------------
	{
		TestNearlyEqual(TEXT("Zero laps, zero distance"), Track->GetRaceProgressCm(0, 0.0), 0.0, 1.0e-9);
		TestNearlyEqual(TEXT("Two laps plus 500 cm"), Track->GetRaceProgressCm(2, 500.0), 2.0 * LengthCm + 500.0, 1.0e-6);

		// A distance beyond a lap is wrapped, not added twice -- otherwise a stale
		// progress value would double-count.
		TestNearlyEqual(TEXT("An over-lap distance is wrapped, not added"),
			Track->GetRaceProgressCm(1, LengthCm + 500.0), LengthCm + 500.0, 1.0e-6);

		// A negative lap count is a bug upstream; clamped rather than trusted,
		// because a negative ranking key sorts a bugged car ahead of the field.
		TestNearlyEqual(TEXT("Negative laps clamp to zero"), Track->GetRaceProgressCm(-5, 500.0), 500.0, 1.0e-6);

		// Monotonic within a lap, and across the lap boundary when the lap count
		// advances with it. This is Docs/03-TrackRaceUI.md rule 7.
		TestTrue(TEXT("Progress increases with distance"),
			Track->GetRaceProgressCm(0, 1000.0) > Track->GetRaceProgressCm(0, 500.0));
		TestTrue(TEXT("Progress increases across the line when the lap advances"),
			Track->GetRaceProgressCm(1, 10.0) > Track->GetRaceProgressCm(0, LengthCm - 10.0));
	}

	return true;
}

// ===========================================================================
// Sectors
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrackDefinitionSectorTest,
	"RacingSim.Race.TrackSectors",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FTrackDefinitionSectorTest::RunTest(const FString& Parameters)
{
	using namespace TrackDefinitionSpecPrivate;

	FTrackSpecCircleFixture Fixture;
	ATrackDefinitionActor* Track = Fixture.Track;
	if (!Track)
	{
		AddError(TEXT("Class default object for ATrackDefinitionActor is unavailable."));
		return false;
	}
	const double LengthCm = Track->GetTrackLengthCm();

	// A fresh actor has exactly one sector, starting at the line.
	TestEqual(TEXT("Default track has one sector"), Track->GetNumSectors(), 1);
	TestNearlyEqual(TEXT("Default sector starts at the line"), Track->GetSectorStartDistanceCm(0), 0.0, 1.0e-9);
	TestNearlyEqual(TEXT("Default sector is the whole lap"), Track->GetSectorLengthCm(0), LengthCm, 1.0e-6);

	SetThreeSectors(*Track);
	TestEqual(TEXT("Three sectors"), Track->GetNumSectors(), 3);

	const double ThirdCm = LengthCm / 3.0;

	TestEqual(TEXT("At the line is sector 0"), Track->GetSectorIndexAtDistanceCm(0.0), 0);
	TestEqual(TEXT("Just before the first boundary is sector 0"), Track->GetSectorIndexAtDistanceCm(ThirdCm - 1.0), 0);
	TestEqual(TEXT("Exactly on a boundary belongs to the sector it STARTS"), Track->GetSectorIndexAtDistanceCm(ThirdCm), 1);
	TestEqual(TEXT("Mid second sector"), Track->GetSectorIndexAtDistanceCm(ThirdCm * 1.5), 1);
	TestEqual(TEXT("Third sector"), Track->GetSectorIndexAtDistanceCm(ThirdCm * 2.5), 2);
	TestEqual(TEXT("Just before the line is the last sector"), Track->GetSectorIndexAtDistanceCm(LengthCm - 1.0), 2);

	// A wrapped or negative distance answers about the equivalent point on the lap,
	// rather than falling off the end of the array.
	TestEqual(TEXT("A whole lap wraps to sector 0"), Track->GetSectorIndexAtDistanceCm(LengthCm), 0);
	TestEqual(TEXT("Three laps plus a bit wraps"), Track->GetSectorIndexAtDistanceCm(3.0 * LengthCm + ThirdCm * 1.5), 1);
	TestEqual(TEXT("A negative distance wraps into the last sector"), Track->GetSectorIndexAtDistanceCm(-1.0), 2);
	TestEqual(TEXT("A NaN distance does not produce an out-of-range index"), Track->GetSectorIndexAtDistanceCm(std::numeric_limits<double>::quiet_NaN()), 0);

	// Sector lengths partition the lap exactly. If they did not, sector splits
	// would not add up to a lap time and nobody would know which one was wrong.
	double SumCm = 0.0;
	for (int32 Sector = 0; Sector < Track->GetNumSectors(); ++Sector)
	{
		const double SectorLengthCm = Track->GetSectorLengthCm(Sector);
		TestTrue(FString::Printf(TEXT("Sector %d has positive length"), Sector), SectorLengthCm > 0.0);
		SumCm += SectorLengthCm;
	}
	TestNearlyEqual(TEXT("Sector lengths sum to the lap"), SumCm, LengthCm, 1.0e-6);

	// Out-of-range indices must be safe, not asserted: a HUD asking for sector 5 of
	// a 3-sector track is a bug, but it must not take the race down.
	TestNearlyEqual(TEXT("Out-of-range sector start is zero"), Track->GetSectorStartDistanceCm(99), 0.0, 1.0e-9);
	TestNearlyEqual(TEXT("Out-of-range sector length is zero"), Track->GetSectorLengthCm(99), 0.0, 1.0e-9);
	TestNearlyEqual(TEXT("Negative sector index is zero"), Track->GetSectorLengthCm(-1), 0.0, 1.0e-9);

	return true;
}

// ===========================================================================
// Grid and reset poses
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrackDefinitionPoseTest,
	"RacingSim.Race.TrackPoses",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FTrackDefinitionPoseTest::RunTest(const FString& Parameters)
{
	using namespace TrackDefinitionSpecPrivate;

	FTrackSpecCircleFixture Fixture;
	ATrackDefinitionActor* Track = Fixture.Track;
	if (!Track)
	{
		AddError(TEXT("Class default object for ATrackDefinitionActor is unavailable."));
		return false;
	}
	const FTrackCenterline& Centerline = Track->GetCenterline();
	const double LengthCm = Track->GetTrackLengthCm();

	// -- Grid ----------------------------------------------------------------
	{
		TestEqual(TEXT("Grid slot count matches the authored count"), Track->GetNumGridSlots(), Track->NumGridSlots);
		TestTrue(TEXT("There is at least one grid slot"), Track->GetNumGridSlots() > 0);

		for (int32 Slot = 0; Slot < Track->GetNumGridSlots(); ++Slot)
		{
			const FTransform Pose = Track->GetGridSlotTransform(Slot);

			// Every slot must sit above the road, not in it.
			const FTrackCenterlineQuery Query = Centerline.FindNearest(Pose.GetLocation());
			if (!Query.bValid)
			{
				AddError(FString::Printf(TEXT("Grid slot %d does not project onto the centerline"), Slot));
				break;
			}

			// Expected arc distance: pole is GridPoleSetbackCm behind the line, each
			// subsequent slot one GridSlotSpacingCm further back.
			const double ExpectedCm = Centerline.WrapDistanceCm(
				-(Track->GridPoleSetbackCm + static_cast<double>(Slot) * Track->GridSlotSpacingCm));
			const double ErrorCm = FMath::Abs(Centerline.GetSignedDistanceDeltaCm(ExpectedCm, Query.DistanceAlongCm));
			if (ErrorCm > 5.0)
			{
				AddError(FString::Printf(TEXT("Grid slot %d sits %f cm from its expected arc distance"), Slot, ErrorCm));
				break;
			}

			// Even slots left of the centerline, odd slots right. Fixed convention:
			// if this ever flips, the whole grid mirrors and nothing else notices.
			const double ExpectedLateralCm = (Slot % 2 == 0) ? -Track->GridSlotLateralOffsetCm : Track->GridSlotLateralOffsetCm;
			if (!FMath::IsNearlyEqual(Query.LateralOffsetCm, ExpectedLateralCm, 5.0))
			{
				AddError(FString::Printf(TEXT("Grid slot %d lateral offset %f, expected %f"),
					Slot, Query.LateralOffsetCm, ExpectedLateralCm));
				break;
			}

			// The pose is lifted along its own up axis, so its height above the flat
			// centerline is exactly PoseHeightOffsetCm.
			if (!FMath::IsNearlyEqual(Pose.GetLocation().Z, Track->PoseHeightOffsetCm, 1.0e-6))
			{
				AddError(FString::Printf(TEXT("Grid slot %d Z is %f, expected %f"),
					Slot, Pose.GetLocation().Z, Track->PoseHeightOffsetCm));
				break;
			}
		}

		// Pole is ahead of every other slot. Asserted through the signed delta,
		// because raw distances wrap and a naive comparison gets this backwards on
		// the slots that sit before the line.
		const FTransform Pole = Track->GetGridSlotTransform(0);
		const FTrackCenterlineQuery PoleQuery = Centerline.FindNearest(Pole.GetLocation());
		for (int32 Slot = 1; Slot < Track->GetNumGridSlots(); ++Slot)
		{
			const FTrackCenterlineQuery SlotQuery = Centerline.FindNearest(Track->GetGridSlotTransform(Slot).GetLocation());
			const double DeltaCm = Centerline.GetSignedDistanceDeltaCm(SlotQuery.DistanceAlongCm, PoleQuery.DistanceAlongCm);
			if (DeltaCm <= 0.0)
			{
				AddError(FString::Printf(TEXT("Grid slot %d is not behind pole (delta %f cm)"), Slot, DeltaCm));
				break;
			}
		}

		// Out of range is identity, not a crash.
		TestTrue(TEXT("Out-of-range grid slot is identity"),
			Track->GetGridSlotTransform(999).Equals(FTransform::Identity, 1.0e-9));
		TestTrue(TEXT("Negative grid slot is identity"),
			Track->GetGridSlotTransform(-1).Equals(FTransform::Identity, 1.0e-9));
	}

	// -- Reset samples -------------------------------------------------------
	{
		const int32 ExpectedCount = FMath::Max(1, FMath::FloorToInt32(LengthCm / Track->ResetSampleSpacingCm));
		TestEqual(TEXT("Reset sample count follows the authored spacing"), Track->GetNumResetSamples(), ExpectedCount);
		TestTrue(TEXT("There is at least one reset sample"), Track->GetNumResetSamples() > 0);

		// Sample 0 is at the line, so every wrapped distance has a predecessor.
		int32 ZeroIndex = INDEX_NONE;
		Track->GetResetTransformAtOrBeforeDistanceCm(0.0, ZeroIndex);
		TestEqual(TEXT("At the line, the reset sample is sample 0"), ZeroIndex, 0);

		// THE RULE THAT MATTERS: a reset can never move a car forwards.
		// Docs/03-TrackRaceUI.md rule 8 says "most recent safe valid sample", and the
		// nearest sample is not the same thing -- placing a car at the nearest one can
		// put it AHEAD of where it left the road, which awards free distance.
		const double ActualSpacingCm = LengthCm / static_cast<double>(Track->GetNumResetSamples());
		bool bMonotonic = true;
		for (int32 Step = 0; Step < 997; ++Step)
		{
			const double QueryCm = LengthCm * static_cast<double>(Step) / 997.0;
			int32 Index = INDEX_NONE;
			const FTransform Pose = Track->GetResetTransformAtOrBeforeDistanceCm(QueryCm, Index);
			if (Index == INDEX_NONE)
			{
				AddError(FString::Printf(TEXT("No reset sample found at %f cm"), QueryCm));
				bMonotonic = false;
				break;
			}

			const FTrackCenterlineQuery PoseQuery = Centerline.FindNearest(Pose.GetLocation());
			const double SampleCm = PoseQuery.DistanceAlongCm;

			// Compared through the signed delta, not by subtraction: at the
			// start/finish origin a re-projected sample can legitimately report
			// L - 1e-10 instead of 0, and a raw comparison would call that a
			// whole lap of free distance.
			const double DeltaCm = Centerline.GetSignedDistanceDeltaCm(SampleCm, QueryCm);

			// Negative means the sample is AHEAD of the query -- free distance.
			if (DeltaCm < -1.0)
			{
				AddError(FString::Printf(TEXT("Reset at %f cm returned a sample AHEAD of it, at %f cm"), QueryCm, SampleCm));
				bMonotonic = false;
				break;
			}

			// ...and it must be the MOST RECENT one, not an arbitrary earlier one.
			if (DeltaCm > ActualSpacingCm + 1.0)
			{
				AddError(FString::Printf(TEXT("Reset at %f cm returned a stale sample at %f cm (spacing %f)"),
					QueryCm, SampleCm, ActualSpacingCm));
				bMonotonic = false;
				break;
			}
		}
		TestTrue(TEXT("Reset never awards distance and never returns a stale sample"), bMonotonic);

		// A multi-lap or negative distance wraps rather than falling off the array.
		{
			int32 WrappedIndex = INDEX_NONE;
			Track->GetResetTransformAtOrBeforeDistanceCm(7.0 * LengthCm + ActualSpacingCm * 1.5, WrappedIndex);
			TestEqual(TEXT("A multi-lap distance wraps to the right sample"), WrappedIndex, 1);

			int32 NegativeIndex = INDEX_NONE;
			Track->GetResetTransformAtOrBeforeDistanceCm(-1.0, NegativeIndex);
			TestEqual(TEXT("A distance just before the line uses the last sample"),
				NegativeIndex, Track->GetNumResetSamples() - 1);

			int32 NaNIndex = INDEX_NONE;
			Track->GetResetTransformAtOrBeforeDistanceCm(std::numeric_limits<double>::quiet_NaN(), NaNIndex);
			TestEqual(TEXT("A NaN distance falls back to sample 0 rather than an invalid index"), NaNIndex, 0);
		}

		// Every reset pose is on the legal route by construction, lifted clear of it.
		for (int32 Sample = 0; Sample < Track->GetNumResetSamples(); ++Sample)
		{
			const FTransform Pose = Track->GetResetSampleTransform(Sample);
			const FTrackCenterlineQuery Query = Centerline.FindNearest(Pose.GetLocation());
			if (!Query.bValid || FMath::Abs(Query.LateralOffsetCm) > 1.0)
			{
				AddError(FString::Printf(TEXT("Reset sample %d is not on the centerline (lateral %f)"),
					Sample, Query.LateralOffsetCm));
				break;
			}
			if (!FMath::IsNearlyEqual(Pose.GetLocation().Z, Track->PoseHeightOffsetCm, 1.0e-6))
			{
				AddError(FString::Printf(TEXT("Reset sample %d is not lifted clear of the road"), Sample));
				break;
			}
		}

		TestTrue(TEXT("Out-of-range reset sample is identity"),
			Track->GetResetSampleTransform(99999).Equals(FTransform::Identity, 1.0e-9));
	}

	return true;
}

// ===========================================================================
// Content version and validation
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrackDefinitionVersionTest,
	"RacingSim.Race.TrackVersion",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FTrackDefinitionVersionTest::RunTest(const FString& Parameters)
{
	using namespace TrackDefinitionSpecPrivate;

	FTrackSpecCircleFixture Fixture;
	ATrackDefinitionActor* Track = Fixture.Track;
	if (!Track)
	{
		AddError(TEXT("Class default object for ATrackDefinitionActor is unavailable."));
		return false;
	}

	// -- The version fills CORE-002's reserved slot --------------------------
	{
		const FRacingContentVersion Version = Track->GetContentVersion();
		TestEqual(TEXT("AssetId is the TrackId"), Version.AssetId, Track->TrackId);
		TestEqual(TEXT("SchemaVersion is the class constant"), Version.SchemaVersion, ATrackDefinitionActor::TrackSchemaVersion);
		TestEqual(TEXT("Schema version 1 for TRACK-001"), ATrackDefinitionActor::TrackSchemaVersion, 1);
		TestTrue(TEXT("A populated track version is publishable"), Version.IsPopulated());
		TestTrue(TEXT("ToString includes the track id"), Version.ToString().Contains(TEXT("Track.Test.Circle")));

		// It goes where CORE-002 said it would.
		FRacingSimVersionStamp Stamp = FRacingSimVersionStamp::MakeCurrent();
		TestFalse(TEXT("CORE-002 leaves TrackVersion unpopulated"), Stamp.TrackVersion.IsPopulated());
		Stamp.TrackVersion = Version;
		TestTrue(TEXT("TRACK-001 populates it"), Stamp.TrackVersion.IsPopulated());
	}

	// -- The hash is stable ---------------------------------------------------
	const FRacingContentVersion BaseVersion = Track->GetContentVersion();
	const uint32 BaseHash = Track->ComputeContentHash();
	TestTrue(TEXT("Hashing twice gives the same answer"), Track->ComputeContentHash() == BaseHash);
	TestTrue(TEXT("Hash is not zero for authored content"), BaseHash != 0u);

	// -- ...and sensitive to every authored field ----------------------------
	// A field that does not move the hash is a field that lets two different tracks
	// claim to be the same one on a leaderboard. Each case restores its change, so
	// the cases stay independent and order does not matter.
	auto ExpectHashChanges = [this, Track, BaseHash](const TCHAR* What, TFunctionRef<void()> Mutate, TFunctionRef<void()> Restore)
	{
		Mutate();
		const uint32 Mutated = Track->ComputeContentHash();
		Restore();
		const uint32 Restored = Track->ComputeContentHash();

		TestTrue(FString::Printf(TEXT("Hash responds to %s"), What), Mutated != BaseHash);
		TestTrue(FString::Printf(TEXT("Hash returns after restoring %s"), What), Restored == BaseHash);
	};

	{
		const FName Original = Track->TrackId;
		ExpectHashChanges(TEXT("TrackId"),
			[Track] { Track->TrackId = FName(TEXT("Track.Test.Other")); },
			[Track, Original] { Track->TrackId = Original; });
	}
	{
		const double Original = Track->CenterlineSampleSpacingCm;
		ExpectHashChanges(TEXT("CenterlineSampleSpacingCm"),
			[Track] { Track->CenterlineSampleSpacingCm = 250.0; },
			[Track, Original] { Track->CenterlineSampleSpacingCm = Original; });
	}
	{
		const TArray<double> Original = Track->SectorStartDistancesCm;
		ExpectHashChanges(TEXT("SectorStartDistancesCm"),
			[Track] { Track->SectorStartDistancesCm = { 0.0, 1000.0, 2000.0 }; },
			[Track, Original] { Track->SectorStartDistancesCm = Original; });
	}
	{
		const int32 Original = Track->NumGridSlots;
		ExpectHashChanges(TEXT("NumGridSlots"),
			[Track] { Track->NumGridSlots = 20; },
			[Track, Original] { Track->NumGridSlots = Original; });
	}
	{
		const double Original = Track->GridPoleSetbackCm;
		ExpectHashChanges(TEXT("GridPoleSetbackCm"),
			[Track] { Track->GridPoleSetbackCm = 1234.0; },
			[Track, Original] { Track->GridPoleSetbackCm = Original; });
	}
	{
		const double Original = Track->GridSlotSpacingCm;
		ExpectHashChanges(TEXT("GridSlotSpacingCm"),
			[Track] { Track->GridSlotSpacingCm = 1234.0; },
			[Track, Original] { Track->GridSlotSpacingCm = Original; });
	}
	{
		const double Original = Track->GridSlotLateralOffsetCm;
		ExpectHashChanges(TEXT("GridSlotLateralOffsetCm"),
			[Track] { Track->GridSlotLateralOffsetCm = 321.0; },
			[Track, Original] { Track->GridSlotLateralOffsetCm = Original; });
	}
	{
		const double Original = Track->PoseHeightOffsetCm;
		ExpectHashChanges(TEXT("PoseHeightOffsetCm"),
			[Track] { Track->PoseHeightOffsetCm = 123.0; },
			[Track, Original] { Track->PoseHeightOffsetCm = Original; });
	}
	{
		const double Original = Track->ResetSampleSpacingCm;
		ExpectHashChanges(TEXT("ResetSampleSpacingCm"),
			[Track] { Track->ResetSampleSpacingCm = 999.0; },
			[Track, Original] { Track->ResetSampleSpacingCm = Original; });
	}
	{
		// Geometry: moving a single spline point must move the hash, or a retraced
		// corner is invisible on a result.
		USplineComponent* Spline = Track->GetCenterlineSpline();
		const FVector Original = Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::Local);
		ExpectHashChanges(TEXT("a moved spline point"),
			[Spline, Original] { Spline->SetLocationAtSplinePoint(0, Original + FVector(0.0, 0.0, 500.0), ESplineCoordinateSpace::Local, true); },
			[Spline, Original] { Spline->SetLocationAtSplinePoint(0, Original, ESplineCoordinateSpace::Local, true); });
	}
	{
		USplineComponent* Spline = Track->GetCenterlineSpline();
		ExpectHashChanges(TEXT("opening the loop"),
			[Spline] { Spline->SetClosedLoop(false, true); },
			[Spline] { Spline->SetClosedLoop(true, true); });
	}

	// -- Identical content hashes identically --------------------------------
	//
	// With a CDO fixture there is only ever one instance, so this cannot be phrased
	// as "two tracks agree". It is the same property in a stronger form: every
	// mutation above was reverted and each ExpectHashChanges call already asserted
	// that the hash came back. This adds the end-to-end version of that -- after all
	// ten mutate/restore cycles, the identity is byte-for-byte what it started as.
	{
		TestTrue(TEXT("After every mutation and restore, the hash is unchanged"),
			Track->ComputeContentHash() == BaseHash);
		TestTrue(TEXT("...and so is the whole content version"),
			Track->GetContentVersion() == BaseVersion);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrackDefinitionValidationTest,
	"RacingSim.Race.TrackValidation",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FTrackDefinitionValidationTest::RunTest(const FString& Parameters)
{
	using namespace TrackDefinitionSpecPrivate;

	FTrackSpecCircleFixture Fixture;
	ATrackDefinitionActor* Track = Fixture.Track;
	if (!Track)
	{
		AddError(TEXT("Class default object for ATrackDefinitionActor is unavailable."));
		return false;
	}
	SetThreeSectors(*Track);

	{
		FString Reason;
		TestTrue(TEXT("A well-authored track validates"), Track->Validate(Reason));
		TestEqual(TEXT("...and gives no reason"), Reason, FString());
	}

	const double LengthCm = Track->GetTrackLengthCm();

	// Every rejection restores its change, so cases are independent of order --
	// `.claude/rules/race-tests.md` requires tests not to depend on execution order.
	auto ExpectRejected = [this, Track](const TCHAR* What, TFunctionRef<void()> Mutate, TFunctionRef<void()> Restore)
	{
		Mutate();
		FString Reason;
		const bool bValid = Track->Validate(Reason);
		Restore();

		TestFalse(FString::Printf(TEXT("Rejected: %s"), What), bValid);
		TestTrue(FString::Printf(TEXT("Gave a reason: %s"), What), !Reason.IsEmpty());

		FString RecoveredReason;
		TestTrue(FString::Printf(TEXT("Recovered after: %s"), What), Track->Validate(RecoveredReason));
	};

	{
		const FName Original = Track->TrackId;
		ExpectRejected(TEXT("TrackId is None"),
			[Track] { Track->TrackId = NAME_None; },
			[Track, Original] { Track->TrackId = Original; });
	}
	{
		USplineComponent* Spline = Track->GetCenterlineSpline();
		ExpectRejected(TEXT("centerline is not a closed loop"),
			[Spline] { Spline->SetClosedLoop(false, true); },
			[Spline, Track] { Spline->SetClosedLoop(true, true); Track->RebuildTrackData(); });
	}
	{
		const double Original = Track->CenterlineSampleSpacingCm;
		ExpectRejected(TEXT("sample spacing is zero"),
			[Track] { Track->CenterlineSampleSpacingCm = 0.0; },
			[Track, Original] { Track->CenterlineSampleSpacingCm = Original; });
	}
	{
		const TArray<double> Original = Track->SectorStartDistancesCm;
		ExpectRejected(TEXT("no sectors"),
			[Track] { Track->SectorStartDistancesCm.Reset(); },
			[Track, Original] { Track->SectorStartDistancesCm = Original; });
		ExpectRejected(TEXT("first sector does not start at the line"),
			[Track] { Track->SectorStartDistancesCm = { 500.0, 1000.0 }; },
			[Track, Original] { Track->SectorStartDistancesCm = Original; });
		ExpectRejected(TEXT("sector starts not strictly increasing"),
			[Track] { Track->SectorStartDistancesCm = { 0.0, 1000.0, 1000.0 }; },
			[Track, Original] { Track->SectorStartDistancesCm = Original; });
		ExpectRejected(TEXT("a sector starts past the end of the lap"),
			[Track, LengthCm] { Track->SectorStartDistancesCm = { 0.0, LengthCm + 1.0 }; },
			[Track, Original] { Track->SectorStartDistancesCm = Original; });
		ExpectRejected(TEXT("a sector start is not finite"),
			[Track] { Track->SectorStartDistancesCm = { 0.0, std::numeric_limits<double>::quiet_NaN() }; },
			[Track, Original] { Track->SectorStartDistancesCm = Original; });
	}
	{
		const int32 Original = Track->NumGridSlots;
		ExpectRejected(TEXT("no grid slots"),
			[Track] { Track->NumGridSlots = 0; },
			[Track, Original] { Track->NumGridSlots = Original; });

		// The grid wrapping past the line puts the back of the grid AHEAD of pole.
		// Nothing crashes; the race is simply nonsense, which is exactly the class of
		// bug a validation pass exists to catch before anyone drives it.
		ExpectRejected(TEXT("the grid is longer than the lap"),
			[Track] { Track->NumGridSlots = 500; },
			[Track, Original] { Track->NumGridSlots = Original; });
	}
	{
		const double Original = Track->GridSlotSpacingCm;
		ExpectRejected(TEXT("grid slot spacing is zero"),
			[Track] { Track->GridSlotSpacingCm = 0.0; },
			[Track, Original] { Track->GridSlotSpacingCm = Original; });
	}
	{
		const double Original = Track->GridPoleSetbackCm;
		ExpectRejected(TEXT("pole setback is negative"),
			[Track] { Track->GridPoleSetbackCm = -1.0; },
			[Track, Original] { Track->GridPoleSetbackCm = Original; });
	}
	{
		const double Original = Track->PoseHeightOffsetCm;
		ExpectRejected(TEXT("pose height offset is not finite"),
			[Track] { Track->PoseHeightOffsetCm = std::numeric_limits<double>::quiet_NaN(); },
			[Track, Original] { Track->PoseHeightOffsetCm = Original; });
	}
	{
		const double Original = Track->ResetSampleSpacingCm;
		ExpectRejected(TEXT("reset spacing is zero"),
			[Track] { Track->ResetSampleSpacingCm = 0.0; },
			[Track, Original] { Track->ResetSampleSpacingCm = Original; });
		ExpectRejected(TEXT("reset spacing exceeds the lap"),
			[Track, LengthCm] { Track->ResetSampleSpacingCm = LengthCm * 2.0; },
			[Track, Original] { Track->ResetSampleSpacingCm = Original; });
	}

	return true;
}
