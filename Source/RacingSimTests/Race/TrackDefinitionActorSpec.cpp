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
 * ===========================================================================
 * Why these suites still carry EditorContext (code-reviewer pass 1, M4)
 * ===========================================================================
 *
 * M4 asked for EditorContext to be dropped, on the sound reasoning that a suite
 * which mutates a shared CDO has no business running inside a live editor session.
 * The reasoning is right; the remedy would silently delete this file's coverage,
 * so the flag is KEPT and the finding is disputed with evidence rather than
 * complied with blindly.
 *
 * Engine/Source/Runtime/Core/Private/Misc/AutomationTest.cpp:800-870,
 * FAutomationTestFramework::GetValidTestNames, computes:
 *
 *     const bool bRunningCommandlet = IsRunningCommandlet();
 *     const bool bRunningEditor     = GIsEditor && !bRunningCommandlet;
 *     if (bRunningEditor)     ApplicationSupportFlags |= EditorContext;
 *     if (bRunningCommandlet) ApplicationSupportFlags |= CommandletContext;
 *     ...
 *     bPassesApplicationRequirements =
 *         !CurTestApplicationFlags || !!(CurTestApplicationFlags & ApplicationSupportFlags);
 *
 * This project's ONLY recorded automation gate (Docs/Environment.md) is
 *
 *     UnrealEditor-Cmd.exe RacingSim.uproject -ExecCmds="Automation RunFilter Smoke; Quit"
 *
 * which passes no `-run=`, so IsRunningCommandlet() is FALSE and GIsEditor is TRUE.
 * ApplicationSupportFlags is therefore EditorContext ALONE. A suite flagged
 * CommandletContext-only would AND to zero against it and never be collected -- it
 * would not fail, it would silently not exist, and the gate would keep reporting
 * green with six fewer suites in it. Docs/Environment.md already records that exact
 * failure mode as a code-reviewer blocker from CORE-001: "A test the documented gate
 * cannot see is not coverage."
 *
 * So M4's substance is addressed the only way that does not trade a real defect for
 * a worse one:
 *
 *   - the actual data-loss bug is FIXED. The fixture previously restored spline point
 *     LOCATIONS only, silently discarding authored arrive tangents, leave tangents and
 *     point types -- so any suite running after this file saw a CDO whose curve
 *     differed from the one it shipped with. All four are now snapshotted and restored.
 *   - RacingSim.Race.TrackFixtureRestore asserts the restore is exhaustive, so a future
 *     field added to the fixture without a matching restore fails a test instead of
 *     quietly corrupting whatever runs next.
 *   - the remaining risk -- mutating a CDO at all -- is batched forward to TEST-001,
 *     which owns the harness fix. The correct end state is "do not mutate the CDO"
 *     (a functional-test map, or a working world bootstrap), NOT "hide the suite from
 *     the only gate that runs it".
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
	 * Write the spec's 100 m circle onto a track's spline.
	 *
	 * Shared by the fixture's constructor and by any mid-test case that wrecks the
	 * geometry on purpose and needs the FIXTURE's baseline back. That distinction
	 * matters and cost a red test to learn: FTrackSpecCircleFixture::RestoreSpline
	 * restores the CDO's ORIGINAL spline, which is USplineComponent's two-point
	 * default, not this circle. Using it as a mid-test "restore" leaves the track with
	 * two spline points, so Validate() then fails for a completely different reason and
	 * every later case in the suite cascades.
	 */
	void AuthorSpecCircle(ATrackDefinitionActor& Track)
	{
		USplineComponent* Spline = Track.GetCenterlineSpline();
		if (!Spline)
		{
			return;
		}

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

			// TRACK-002's authored fields. Added here at the same commit that added them
			// to the actor, because this fixture mutates a process-wide CDO and its whole
			// safety argument is that the snapshot is EXHAUSTIVE. A new authored field
			// that is not saved here is a field one suite can leave altered for every
			// suite that runs after it -- which is exactly the location-only restore bug
			// M4 found, in a new place.
			SavedCheckpointGateSpecs = Track->CheckpointGateSpecs;
			SavedNumGeneratedGates = Track->NumGeneratedCheckpointGates;
			SavedGeneratedGateHalfWidthCm = Track->GeneratedGateHalfWidthCm;
			SavedGeneratedGateHalfHeightCm = Track->GeneratedGateHalfHeightCm;
			SavedMinCornerRadiusCm = Track->MinCornerRadiusCm;

			USplineComponent* Spline = Track->GetCenterlineSpline();
			if (Spline)
			{
				bSavedClosedLoop = Spline->IsClosedLoop();

				// M4: a spline point is FOUR pieces of authored data, not one. Saving
				// only the location and restoring with SetSplinePoints silently reset
				// every tangent to auto and every point type to Curve, so the CDO this
				// file handed to whatever ran next was not the CDO it borrowed. A
				// location-only restore looks correct in a diff and is not.
				const int32 SavedPointCount = Spline->GetNumberOfSplinePoints();
				SavedSplinePoints.Reserve(SavedPointCount);
				SavedArriveTangents.Reserve(SavedPointCount);
				SavedLeaveTangents.Reserve(SavedPointCount);
				SavedPointTypes.Reserve(SavedPointCount);
				for (int32 Index = 0; Index < SavedPointCount; ++Index)
				{
					SavedSplinePoints.Add(Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local));
					SavedArriveTangents.Add(Spline->GetArriveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local));
					SavedLeaveTangents.Add(Spline->GetLeaveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local));
					SavedPointTypes.Add(Spline->GetSplinePointType(Index));
				}
			}

			Track->TrackId = FName(TEXT("Track.Test.Circle"));
			AuthorSpecCircle(*Track);
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

			Track->CheckpointGateSpecs = SavedCheckpointGateSpecs;
			Track->NumGeneratedCheckpointGates = SavedNumGeneratedGates;
			Track->GeneratedGateHalfWidthCm = SavedGeneratedGateHalfWidthCm;
			Track->GeneratedGateHalfHeightCm = SavedGeneratedGateHalfHeightCm;
			Track->MinCornerRadiusCm = SavedMinCornerRadiusCm;

			RestoreSpline();
			Track->RebuildTrackData();
		}

		/**
		 * Put the spline back exactly as it was found: locations, arrive tangents,
		 * leave tangents and point types.
		 *
		 * Public so RacingSim.Race.TrackFixtureRestore can invoke it mid-test and then
		 * assert, from outside, that the restore really is exhaustive. A restore that
		 * only its own author ever checks is how the location-only bug survived.
		 */
		void RestoreSpline() const
		{
			USplineComponent* Spline = Track ? Track->GetCenterlineSpline() : nullptr;
			if (!Spline)
			{
				return;
			}

			Spline->SetClosedLoop(bSavedClosedLoop, /*bUpdateSpline*/ false);
			Spline->SetSplinePoints(SavedSplinePoints, ESplineCoordinateSpace::Local, /*bUpdateSpline*/ false);

			// SetSplinePoints resets tangents to auto and types to Curve, so these must
			// be reapplied afterwards, not before.
			const int32 Count = FMath::Min(SavedSplinePoints.Num(), Spline->GetNumberOfSplinePoints());
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Spline->SetTangentsAtSplinePoint(
					Index, SavedArriveTangents[Index], SavedLeaveTangents[Index],
					ESplineCoordinateSpace::Local, /*bUpdateSpline*/ false);
				Spline->SetSplinePointType(Index, SavedPointTypes[Index], /*bUpdateSpline*/ false);
			}

			Spline->UpdateSpline();
		}

		/** True when the live spline matches the snapshot in all four authored channels. */
		bool SplineMatchesSnapshot(FString& OutMismatch) const
		{
			USplineComponent* Spline = Track ? Track->GetCenterlineSpline() : nullptr;
			if (!Spline)
			{
				OutMismatch = TEXT("no spline component");
				return false;
			}

			if (Spline->IsClosedLoop() != bSavedClosedLoop)
			{
				OutMismatch = TEXT("closed-loop flag");
				return false;
			}

			if (Spline->GetNumberOfSplinePoints() != SavedSplinePoints.Num())
			{
				OutMismatch = FString::Printf(TEXT("point count %d, expected %d"),
					Spline->GetNumberOfSplinePoints(), SavedSplinePoints.Num());
				return false;
			}

			for (int32 Index = 0; Index < SavedSplinePoints.Num(); ++Index)
			{
				if (!Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local).Equals(SavedSplinePoints[Index], 1.0e-4))
				{
					OutMismatch = FString::Printf(TEXT("location at point %d"), Index);
					return false;
				}
				if (!Spline->GetArriveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local).Equals(SavedArriveTangents[Index], 1.0e-3))
				{
					OutMismatch = FString::Printf(TEXT("arrive tangent at point %d"), Index);
					return false;
				}
				if (!Spline->GetLeaveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local).Equals(SavedLeaveTangents[Index], 1.0e-3))
				{
					OutMismatch = FString::Printf(TEXT("leave tangent at point %d"), Index);
					return false;
				}
				if (Spline->GetSplinePointType(Index) != SavedPointTypes[Index])
				{
					OutMismatch = FString::Printf(TEXT("point type at point %d"), Index);
					return false;
				}
			}

			return true;
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
		TArray<FRacingCheckpointGateSpec> SavedCheckpointGateSpecs;
		int32 SavedNumGeneratedGates = 0;
		double SavedGeneratedGateHalfWidthCm = 0.0;
		double SavedGeneratedGateHalfHeightCm = 0.0;
		double SavedMinCornerRadiusCm = 0.0;
		TArray<FVector> SavedSplinePoints;
		TArray<FVector> SavedArriveTangents;
		TArray<FVector> SavedLeaveTangents;
		TArray<ESplinePointType::Type> SavedPointTypes;
		bool bSavedClosedLoop = false;
	};

	/** Replace the spline with three coincident points: a closed loop of length zero, which the bake must reject. */
	void SetDegenerateZeroLengthSpline(ATrackDefinitionActor& Track)
	{
		USplineComponent* Spline = Track.GetCenterlineSpline();
		if (!Spline)
		{
			return;
		}

		const TArray<FVector> Coincident = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
		Spline->SetClosedLoop(true, /*bUpdateSpline*/ false);
		Spline->SetSplinePoints(Coincident, ESplineCoordinateSpace::Local, /*bUpdateSpline*/ true);
	}

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
		Track->GetCenterline().GetAverageSegmentLengthCm() <= Track->CenterlineSampleSpacingCm + 1.0e-6);
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

		// Bumped from 1 to 2 by TRACK-002, which added ordered checkpoint gates and their
		// legal crossing directions. Pinned to a literal on purpose: the whole value of a
		// hand-bumped schema version is that adding an authored field without bumping it
		// fails a test instead of silently making two incomparable results comparable.
		TestEqual(TEXT("Schema version 2 for TRACK-002"), ATrackDefinitionActor::TrackSchemaVersion, 2);
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

	// -- M1: the hash covers the EFFECTIVE bake, not just the authored field --
	//
	// THE DEFECT, stated precisely. ComputeContentHash used to hash only the AUTHORED
	// CenterlineSampleSpacingCm. But the bake does not always use it: it substitutes
	// 100 cm for a non-finite or non-positive value, and clamps the sample count at an
	// internal MaxGeneratedSamples ceiling that is a CODE constant, not authored data.
	// So two runs could bake at genuinely different resolutions -- shifting every
	// progress value on the track -- and publish the same content hash. A leaderboard
	// would treat them as comparable. They are not.
	//
	// Isolated below without touching geometry or the authored field at comparison
	// time: bake at one resolution, re-bake at another, then put the AUTHORED value
	// back WITHOUT rebuilding. Authored data and geometry are then byte-identical to
	// the starting state while the effective bake is not. A hash that ignores the
	// effective bake cannot tell those apart; this one can.
	{
		const double OriginalSpacingCm = Track->CenterlineSampleSpacingCm;

		Track->CenterlineSampleSpacingCm = 100.0;
		TestTrue(TEXT("Bake at 100 cm succeeds"), Track->RebuildTrackData());

		const int32 FineSampleCount = Track->GetEffectiveSampleCount();
		const double FineStepCm = Track->GetEffectiveStepCm();
		const uint32 FineHash = Track->ComputeContentHash();

		TestTrue(TEXT("The effective sample count is recorded"), FineSampleCount > 0);
		TestTrue(TEXT("The effective step is recorded"), FineStepCm > 0.0);
		TestNearlyEqual(TEXT("Effective step times sample count is the lap"),
			FineStepCm * static_cast<double>(FineSampleCount), Track->GetTrackLengthCm(), 1.0e-6);

		Track->CenterlineSampleSpacingCm = 200.0;
		TestTrue(TEXT("Bake at 200 cm succeeds"), Track->RebuildTrackData());

		const int32 CoarseSampleCount = Track->GetEffectiveSampleCount();
		TestTrue(TEXT("A coarser authored spacing really does bake fewer samples"),
			CoarseSampleCount < FineSampleCount);

		// Restore the AUTHORED value only. Authored data and geometry now match the
		// 100 cm state exactly; the effective bake is still the 200 cm one.
		Track->CenterlineSampleSpacingCm = 100.0;
		TestEqual(TEXT("The stale bake is still the coarse one"),
			Track->GetEffectiveSampleCount(), CoarseSampleCount);

		TestTrue(TEXT("Identical authored data over a DIFFERENT effective bake hashes differently"),
			Track->ComputeContentHash() != FineHash);

		// ...and re-baking at the authored resolution restores the identity.
		TestTrue(TEXT("Re-baking restores the fine bake"), Track->RebuildTrackData());
		TestEqual(TEXT("...to the same effective sample count"), Track->GetEffectiveSampleCount(), FineSampleCount);
		TestTrue(TEXT("...and the same content hash"), Track->ComputeContentHash() == FineHash);

		Track->CenterlineSampleSpacingCm = OriginalSpacingCm;
		Track->RebuildTrackData();
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

	// =======================================================================
	// M6 (code-reviewer pass 1): rejection branches that had no coverage
	// =======================================================================

	// -- Fewer than three spline points --------------------------------------
	//
	// A closed loop through two points is a degenerate "there and back" with no
	// enclosed area. Reachable in one editor action: select a point, press delete.
	{
		ExpectRejected(TEXT("a closed loop with fewer than 3 spline points"),
			[Track]
			{
				USplineComponent* Spline = Track->GetCenterlineSpline();
				const TArray<FVector> TwoPoints = { FVector(10000.0, 0.0, 0.0), FVector(-10000.0, 0.0, 0.0) };
				Spline->SetClosedLoop(true, /*bUpdateSpline*/ false);
				Spline->SetSplinePoints(TwoPoints, ESplineCoordinateSpace::Local, /*bUpdateSpline*/ true);
				Track->RebuildTrackData();
			},
			[Track]
			{
				// The FIXTURE's circle, not Fixture.RestoreSpline() -- that restores the
				// CDO's two-point default and would leave the suite failing the very
				// branch this case just exercised.
				AuthorSpecCircle(*Track);
				Track->RebuildTrackData();
			});
	}

	// -- Negative grid lateral offset ----------------------------------------
	//
	// The offset is a half-width, applied as -Lateral for even slots and +Lateral for
	// odd. A negative value does not mirror the grid, it INVERTS the stagger, so
	// "pole is on the left" quietly stops being true while everything still works.
	{
		const double Original = Track->GridSlotLateralOffsetCm;
		ExpectRejected(TEXT("grid lateral offset is negative"),
			[Track] { Track->GridSlotLateralOffsetCm = -200.0; },
			[Track, Original] { Track->GridSlotLateralOffsetCm = Original; });
		ExpectRejected(TEXT("grid lateral offset is not finite"),
			[Track] { Track->GridSlotLateralOffsetCm = std::numeric_limits<double>::quiet_NaN(); },
			[Track, Original] { Track->GridSlotLateralOffsetCm = Original; });
	}

	// -- The bake-failure branch ---------------------------------------------
	//
	// Validate() must report a failed bake as a validation failure rather than
	// answering from whatever the previous bake left behind. Reached with a
	// zero-length spline, which is what a not-yet-authored actor effectively is.
	{
		ExpectRejected(TEXT("the centerline failed to bake"),
			[Track]
			{
				SetDegenerateZeroLengthSpline(*Track);
				Track->RebuildTrackData();
			},
			[Track]
			{
				AuthorSpecCircle(*Track);
				Track->RebuildTrackData();
			});

		// ...and the reason names the bake, not some downstream symptom. A validation
		// message that blames sectors for a broken spline sends the next person to the
		// wrong file.
		SetDegenerateZeroLengthSpline(*Track);
		Track->RebuildTrackData();
		FString BakeReason;
		TestFalse(TEXT("A zero-length spline does not validate"), Track->Validate(BakeReason));
		TestTrue(TEXT("...and the reason names the bake"), BakeReason.Contains(TEXT("bake")));
		AuthorSpecCircle(*Track);
		Track->RebuildTrackData();
	}

	// -- The NumSegments() < 3 branch is unreachable ONLY for a bake consistent with
	// -- the spline state Validate() is currently reading; it is NOT unreachable in
	// -- general (code-reviewer pass 2, M6-A) --
	//
	// M6 asked for a rejection test for `BakedCenterline.NumSegments() < 3`. For a
	// closed loop baked from the spline Validate() is reading, the guard cannot fire:
	//
	//   - Validate() rejects a non-closed loop several branches earlier, so only a
	//     closed loop ever reaches this check;
	//   - RebuildTrackData computes NumSamples = Max(MinSamples, ...) with MinSamples
	//     = 3 for a closed loop;
	//   - FTrackCenterline::NumSegments() == NumSamples() for a closed loop.
	//
	// But bBakeAttempted is a has-been-baked flag, not a dirty flag (see the known
	// gap recorded in Docs/Tickets.md), so the bake and the spline Validate() reads
	// can disagree: bake an open, short spline (NumSegments() == 1), then call
	// SetClosedLoop(true) without rebuilding. Validate() now passes the closed-loop
	// and >=3-point branches and reaches this guard with NumSegments() == 1 -- live
	// code on a stale-bake path, correctly firing. Writing a rejection test for THAT
	// path is possible with only public API and is left to whoever fixes the
	// stale-bake gap (TRACK-002/RACE-002), since it is the same fix. What this test
	// asserts is narrower and still real: the guard does not false-positive at the
	// coarsest bake a *consistent* spline/bake pair can produce.
	//
	// What CAN be tested is the boundary: force the coarsest bake the code permits and
	// assert the guard sits exactly one below it and does not fire.
	{
		const double OriginalSpacing = Track->CenterlineSampleSpacingCm;
		const double OriginalRadiusCm = Track->MinCornerRadiusCm;

		// Ten laps' worth of spacing -- far coarser than any authoring mistake.
		Track->CenterlineSampleSpacingCm = LengthCm * 10.0;
		TestTrue(TEXT("An absurdly coarse bake still succeeds"), Track->RebuildTrackData());
		TestEqual(TEXT("A closed-loop bake floors at 3 samples"), Track->GetCenterline().NumSamples(), 3);
		TestEqual(TEXT("...and therefore at 3 segments, one above the guard"),
			Track->GetCenterline().NumSegments(), 3);

		// TRACK-002 CHANGED WHAT THIS CASE MEASURES, and the change is recorded here
		// rather than absorbed silently, because TRACK-001 wrote this test specifically so
		// that a later ticket would have to update it deliberately.
		//
		// A three-segment bake has segments of L/3 -- about 209 m on this fixture. The
		// gate rules added by TRACK-002 both fire on that: gates would be closer together
		// than one segment (handled by the generator, which clamps its own count), and a
		// 900 cm gate is narrower than the placement tolerance at the default 15 m minimum
		// corner radius. Both rejections are CORRECT: a 209 m chord has entirely lost a
		// 15 m corner, and gates measured against it cannot be trusted.
		//
		// The corner radius is lifted out of the way so the WIDTH rule stops firing and
		// the gate set actually bakes. What remains is the case code-reviewer's TRACK-002
		// finding H1 is about, and it is asserted here rather than avoided.
		Track->MinCornerRadiusCm = 1.0e6;
		TestTrue(TEXT("Rebake with the gate WIDTH rule out of the way"), Track->RebuildTrackData());

		// -- H1, the defect this block used to pin as correct ------------------
		//
		// WHAT THIS BLOCK ASSERTED BEFORE, AND WHY IT WAS WRONG. It asserted
		// `Validate()` == true here, plus `GetNumCheckpointGates() >= 1 && < 4`. Both pass
		// on a set of EXACTLY ONE GATE, which is what this bake actually produces:
		// SupportedGates = floor(L / (2 * L/3)) == 1. A one-gate track has no order to
		// enforce and no detectable shortcut, so the old assertions certified the bug.
		//
		// The count is now asserted EXACTLY, not as a range. A range that happens to
		// include the broken value is how this survived review once already.
		TestEqual(TEXT("The coarse bake degrades the generated set to exactly one gate"),
			Track->GetNumCheckpointGates(), 1);
		TestNearlyEqual(TEXT("...and that one gate is still the start/finish gate at distance 0"),
			Track->GetCheckpointGateDistanceCm(0), 0.0, 1.0e-9);

		// The clamp no longer does that silently: it records what it threw away.
		TestFalse(TEXT("...and the generator records that it reduced the count"),
			Track->GetGeneratedGateClampNote().IsEmpty());
		TestTrue(TEXT("...naming the requested count in the note"),
			Track->GetGeneratedGateClampNote().Contains(TEXT("asked for 4 gates")));

		// THE NEW BEHAVIOUR, asserted positively: a one-gate track does not validate.
		FString CoarseReason;
		TestFalse(TEXT("A one-gate track does NOT validate, however healthy its centerline is"),
			Track->Validate(CoarseReason));
		TestTrue(FString::Printf(TEXT("...and the reason names the gate floor: %s"), *CoarseReason),
			CoarseReason.Contains(TEXT("baked 1 checkpoint gate")));
		TestTrue(FString::Printf(TEXT("...and quotes the clamp, so the COARSE BAKE is named as the cause: %s"),
			*CoarseReason), CoarseReason.Contains(TEXT("could only place 1")));

		// The original point of this case, preserved: the NumSegments() < 3 guard must
		// still not fire at the floor. Asserted by the reason NOT being the segment
		// guard's, which is a stronger statement than the old `Validate() == true` --
		// that one would have gone green for any reason at all once the guard was fixed.
		TestFalse(FString::Printf(TEXT("The NumSegments() < 3 guard does not fire at the floor: %s"), *CoarseReason),
			CoarseReason.Contains(TEXT("too coarse to query")));

		// -- The interaction, asserted rather than merely avoided -------------
		//
		// At the DEFAULT minimum corner radius, this same coarse bake must NOT validate,
		// and the reason must name the gates. A track whose centerline still bakes but
		// whose gates cannot be trusted is exactly the case that looks healthy in a HUD.
		Track->MinCornerRadiusCm = OriginalRadiusCm;
		TestTrue(TEXT("Rebake at the default minimum corner radius"), Track->RebuildTrackData());

		FString GateReason;
		TestFalse(TEXT("A 209 m-segment bake does NOT validate at a 15 m minimum corner radius"),
			Track->Validate(GateReason));
		TestTrue(FString::Printf(TEXT("...and the reason names the checkpoint gates: %s"), *GateReason),
			GateReason.Contains(TEXT("Checkpoint gate")));

		Track->CenterlineSampleSpacingCm = OriginalSpacing;
		Track->MinCornerRadiusCm = OriginalRadiusCm;
		Track->RebuildTrackData();

		// The positive half of the control: at the authored spacing the same track bakes
		// the full requested set, clamps nothing, and validates. Without this the two
		// rejections above would be satisfied by a Validate() that had simply stopped
		// returning true.
		FString RestoredReason;
		TestEqual(TEXT("At the authored spacing the generator places every requested gate"),
			Track->GetNumCheckpointGates(), Track->NumGeneratedCheckpointGates);
		TestTrue(TEXT("...at or above the enforced floor"),
			Track->GetNumCheckpointGates() >= ATrackDefinitionActor::MinCheckpointGateCount);
		TestTrue(TEXT("...with nothing clamped away"), Track->GetGeneratedGateClampNote().IsEmpty());
		TestTrue(FString::Printf(TEXT("...and the track validates again: %s"), *RestoredReason),
			Track->Validate(RestoredReason));
	}

	return true;
}

// ===========================================================================
// H1 (code-reviewer, TRACK-002 pass 1): a gate set too small to enforce order
// ===========================================================================
//
// WHY THIS IS A SEPARATE TEST AND NOT ANOTHER ExpectRejected CASE IN TrackValidation.
// The defect had two independent entrances, and a fix that closes only one of them
// still ships the bug:
//
//   1. the GENERATOR silently clamping a legal request down past the floor (covered in
//      TrackValidation's coarse-bake block, which is where the coarse bake already is);
//   2. an AUTHOR writing a two-gate CheckpointGateSpecs array by hand. The generator is
//      never involved, so no amount of generator hardening touches it -- only a floor
//      on the BAKED set does.
//
// Case 2 below is the negative control that distinguishes the two fixes. It asserts the
// gate set is genuinely well-formed -- it builds, it reports two gates, nothing was
// clamped -- and is refused anyway, on the race rule rather than on a geometry error.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrackDefinitionGateOrderFloorTest,
	"RacingSim.Race.TrackCheckpointGateOrderFloor",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FTrackDefinitionGateOrderFloorTest::RunTest(const FString& Parameters)
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

	const double LengthCm = Track->GetTrackLengthCm();

	// The floor is a published constant, not a literal repeated in the test. A test that
	// hard-codes 4 keeps passing if someone edits the constant to 1.
	const int32 FloorCount = ATrackDefinitionActor::MinCheckpointGateCount;
	TestTrue(TEXT("The enforced floor is at least 2, or there is no order to enforce"), FloorCount >= 2);

	// Builds an evenly spaced authored set of N gates, gate 0 pinned to exactly 0. Widths
	// are the actor's own generated defaults, which the fixture's 100 cm bake clears
	// comfortably -- the point of these cases is the COUNT, so nothing else may be the
	// reason a case fails.
	auto AuthorGates = [Track, LengthCm](const int32 Count)
	{
		TArray<FRacingCheckpointGateSpec> Specs;
		Specs.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FRacingCheckpointGateSpec Spec;
			Spec.GateId = (Index == 0)
				? FName(TEXT("Gate.StartFinish"))
				: FName(*FString::Printf(TEXT("Gate.%02d"), Index));
			Spec.DistanceAlongCm = (Index == 0) ? 0.0 : LengthCm * static_cast<double>(Index) / static_cast<double>(Count);
			Spec.HalfWidthCm = 900.0;
			Spec.HalfHeightCm = 500.0;
			Spec.LegalDirection = ERacingGateDirection::Forward;
			Specs.Add(Spec);
		}

		Track->CheckpointGateSpecs = Specs;
		Track->RebuildTrackData();
	};

	// -- Baseline: the shipped defaults sit at or above the floor -------------
	{
		FString Reason;
		TestTrue(FString::Printf(TEXT("The default generated set validates: %s"), *Reason), Track->Validate(Reason));
		TestTrue(TEXT("...and the default NumGeneratedCheckpointGates is at or above the floor"),
			Track->NumGeneratedCheckpointGates >= FloorCount);
	}

	// -- Entrance 1, at the authored knob rather than through the bake --------
	//
	// Asking the generator for fewer gates than the floor is rejected at the FIELD, so the
	// message points at the property to change rather than at a baked count the author
	// never typed.
	{
		const int32 OriginalCount = Track->NumGeneratedCheckpointGates;
		Track->NumGeneratedCheckpointGates = FloorCount - 1;
		Track->RebuildTrackData();

		FString Reason;
		TestFalse(TEXT("A generator asked for fewer gates than the floor does not validate"),
			Track->Validate(Reason));
		TestTrue(FString::Printf(TEXT("...and the reason names the authored field: %s"), *Reason),
			Reason.Contains(TEXT("NumGeneratedCheckpointGates")));

		Track->NumGeneratedCheckpointGates = OriginalCount;
		Track->RebuildTrackData();

		FString RecoveredReason;
		TestTrue(FString::Printf(TEXT("Recovered: %s"), *RecoveredReason), Track->Validate(RecoveredReason));
	}

	// -- Entrance 2, THE NEGATIVE CONTROL: a hand-authored set below the floor -
	//
	// Two gates half a lap apart on a 100 m circle: strictly increasing, separated by far
	// more than one centerline segment, wide enough to swallow the placement tolerance.
	// Geometrically impeccable, and unraceable -- a car can reach the far gate across the
	// infield, turn round, cross the line forwards and be credited a lap.
	{
		AuthorGates(2);

		TestTrue(TEXT("A two-gate authored set BUILDS -- this rejection is not a bake failure"),
			Track->GetCheckpointGates().IsValid());
		TestEqual(TEXT("...with exactly the two gates that were authored"), Track->GetNumCheckpointGates(), 2);
		TestTrue(TEXT("...and the generator did not run, so nothing was clamped"),
			Track->GetGeneratedGateClampNote().IsEmpty());

		FString Reason;
		TestFalse(TEXT("A two-gate track does not validate"), Track->Validate(Reason));
		TestTrue(FString::Printf(TEXT("...and the reason is the ORDER floor, not geometry: %s"), *Reason),
			Reason.Contains(TEXT("baked 2 checkpoint gate")));
		TestFalse(FString::Printf(TEXT("...and does not blame the bake: %s"), *Reason),
			Reason.Contains(TEXT("failed to bake")));
	}

	// -- One gate: the exact set finding H1 reported as validating green ------
	{
		AuthorGates(1);

		TestEqual(TEXT("A one-gate authored set builds and reports one gate"), Track->GetNumCheckpointGates(), 1);

		FString Reason;
		TestFalse(TEXT("A one-gate track does not validate"), Track->Validate(Reason));
		TestTrue(FString::Printf(TEXT("...and the reason names the ordering rule: %s"), *Reason),
			Reason.Contains(TEXT("enforce checkpoint order")));
	}

	// -- Exactly at the floor: accepted, so the check is a floor and not a ban -
	{
		AuthorGates(FloorCount);

		TestEqual(TEXT("An authored set exactly at the floor bakes every gate"),
			Track->GetNumCheckpointGates(), FloorCount);

		FString Reason;
		TestTrue(FString::Printf(TEXT("...and validates: %s"), *Reason), Track->Validate(Reason));
		TestEqual(TEXT("...giving no reason"), Reason, FString());
	}

	// Hand the CDO back the way the fixture found it. The destructor restores
	// CheckpointGateSpecs, but every later case in THIS test would otherwise run against
	// whatever the previous case authored.
	Track->CheckpointGateSpecs.Reset();
	Track->RebuildTrackData();

	return true;
}

// ===========================================================================
// H1 (code-reviewer pass 1): a failed bake must not be retried per query
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrackDefinitionFailedBakeTest,
	"RacingSim.Race.TrackFailedBakeIsNotRetried",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FTrackDefinitionFailedBakeTest::RunTest(const FString& Parameters)
{
	using namespace TrackDefinitionSpecPrivate;

	FTrackSpecCircleFixture Fixture;
	ATrackDefinitionActor* Track = Fixture.Track;
	if (!Track)
	{
		AddError(TEXT("Class default object for ATrackDefinitionActor is unavailable."));
		return false;
	}

	// THE BUG THIS PINS. EnsureTrackDataBuilt() used to gate on bTrackDataBuilt, i.e.
	// on SUCCESS. A freshly placed, not-yet-authored actor has no usable spline, so its
	// bake fails -- and that is its normal state for as long as it takes someone to
	// draw a circuit. Every query then re-ran the whole sample loop, with its two
	// TArray allocations and a fresh UE_LOG(Warning), forever. At 60 Hz per car that is
	// a per-frame allocation storm and a log flood, against two CLAUDE.md rules at once.
	//
	// Measured by COUNTING ATTEMPTS, not by timing. Wall-clock timing would measure the
	// build host, which this project shares with other agents' concurrent builds.

	// -- The two flags are genuinely different -------------------------------
	SetDegenerateZeroLengthSpline(*Track);
	const bool bRebuildSucceeded = Track->RebuildTrackData();

	TestFalse(TEXT("A zero-length spline fails to bake"), bRebuildSucceeded);
	TestFalse(TEXT("...so IsTrackDataBuilt() is false"), Track->IsTrackDataBuilt());
	TestTrue(TEXT("...but HasBakeBeenAttempted() is TRUE -- this is the whole fix"),
		Track->HasBakeBeenAttempted());

	// -- N queries, zero additional attempts ---------------------------------
	const int32 AttemptsBefore = Track->GetBakeAttemptCount();

	// Every public entry point that funnels through GetCenterline()/EnsureTrackDataBuilt.
	constexpr int32 QueryRounds = 50;
	for (int32 Round = 0; Round < QueryRounds; ++Round)
	{
		Track->GetCenterline();
		Track->GetTrackLengthCm();
		Track->GetTrackLengthMetres();
		Track->GetTrackLengthKilometres();
		Track->GetStartFinishTransform();
		Track->GetSectorIndexAtDistanceCm(1234.0);
		Track->GetSectorLengthCm(0);
		Track->GetNumGridSlots();
		Track->GetGridSlotTransform(0);
		Track->GetGridSlotDistanceCm(0);
		Track->GetNumResetSamples();
		Track->GetResetSampleTransform(0);
		Track->GetResetSampleDistanceCm(0);
		Track->GetRaceProgressCm(1, 500.0);
		Track->FindNearestCenterlinePoint(FVector(100.0, 200.0, 0.0));
		Track->FindNearestCenterlinePointNear(FVector(100.0, 200.0, 0.0), 0.0, 500.0);

		int32 ResetIndex = INDEX_NONE;
		Track->GetResetTransformAtOrBeforeDistanceCm(500.0, ResetIndex);
	}

	TestEqual(TEXT("50 rounds of 17 queries after a failed bake trigger ZERO further bake attempts"),
		Track->GetBakeAttemptCount(), AttemptsBefore);

	// -- ...and every one of those queries still answered safely --------------
	// Caching the failure must not turn "unbuilt" into "crashes" or "returns garbage".
	TestFalse(TEXT("The centerline is still reported invalid"), Track->GetCenterline().IsValid());
	TestEqual(TEXT("Track length is zero, not NaN"), Track->GetTrackLengthCm(), 0.0);
	TestEqual(TEXT("Sector index is INDEX_NONE on an unbuilt track"), Track->GetSectorIndexAtDistanceCm(0.0), INDEX_NONE);
	TestEqual(TEXT("Race progress is zero on an unbuilt track"), Track->GetRaceProgressCm(3, 100.0), 0.0);
	TestEqual(TEXT("There are no grid slots"), Track->GetNumGridSlots(), 0);
	TestEqual(TEXT("There are no reset samples"), Track->GetNumResetSamples(), 0);
	TestFalse(TEXT("A projection onto an unbuilt centerline is invalid, not garbage"),
		Track->FindNearestCenterlinePoint(FVector(1.0, 2.0, 3.0)).bValid);

	// -- An explicit rebuild is still the way back ---------------------------
	// Caching a failure must not be a one-way door: fixing the spline and rebuilding
	// has to recover, or a track could never be authored in a live editor session.
	AuthorSpecCircle(*Track);
	const int32 AttemptsBeforeRecovery = Track->GetBakeAttemptCount();
	TestTrue(TEXT("An explicit rebuild after fixing the spline succeeds"), Track->RebuildTrackData());
	TestEqual(TEXT("...and it counted as exactly one more attempt"),
		Track->GetBakeAttemptCount(), AttemptsBeforeRecovery + 1);
	TestTrue(TEXT("...and the track is built again"), Track->IsTrackDataBuilt());
	TestTrue(TEXT("...with a positive length"), Track->GetTrackLengthCm() > 0.0);

	// A successful query on a HEALTHY track must also not re-bake.
	const int32 AttemptsAfterRecovery = Track->GetBakeAttemptCount();
	for (int32 Round = 0; Round < QueryRounds; ++Round)
	{
		Track->GetTrackLengthCm();
		Track->FindNearestCenterlinePoint(FVector(100.0, 200.0, 0.0));
	}
	TestEqual(TEXT("Queries on a healthy track trigger no bake attempts either"),
		Track->GetBakeAttemptCount(), AttemptsAfterRecovery);

	return true;
}

// ===========================================================================
// H2 (code-reviewer pass 1): arc-length accessors for grid and reset poses
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrackDefinitionHintReseedTest,
	"RacingSim.Race.TrackHintReseed",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FTrackDefinitionHintReseedTest::RunTest(const FString& Parameters)
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

	// WHY THIS EXISTS. FindNearestCenterlinePointNear is the correctness-critical
	// query -- RacingSim.Race.CenterlineAmbiguity shows the global search snapping to
	// the wrong leg of a hairpin. It needs a hint. At race start and immediately after
	// a reset, the caller's hint is by definition absent or stale, and those are
	// precisely the two moments a car is sitting on a grid slot or a reset pose.
	// Before this fix a caller could only recover the distance by calling the global
	// search -- the exact search that gets it wrong.

	// -- Grid slots ----------------------------------------------------------
	{
		TestTrue(TEXT("There is at least one grid slot"), Track->GetNumGridSlots() > 0);

		for (int32 Slot = 0; Slot < Track->GetNumGridSlots(); ++Slot)
		{
			const double ExpectedCm = Centerline.WrapDistanceCm(
				-(Track->GridPoleSetbackCm + static_cast<double>(Slot) * Track->GridSlotSpacingCm));

			const double ActualCm = Track->GetGridSlotDistanceCm(Slot);
			if (!FMath::IsNearlyEqual(ActualCm, ExpectedCm, 1.0e-6))
			{
				AddError(FString::Printf(TEXT("Grid slot %d distance %f, expected %f"), Slot, ActualCm, ExpectedCm));
				break;
			}

			// Every returned distance must be a legal arc length, or it cannot be used
			// as a hint at all.
			if (!(ActualCm >= 0.0 && ActualCm < LengthCm))
			{
				AddError(FString::Printf(TEXT("Grid slot %d distance %f is outside [0, %f)"), Slot, ActualCm, LengthCm));
				break;
			}

			// The combined accessor must agree with both single accessors.
			double PoseDistanceCm = 0.0;
			const FTransform Pose = Track->GetGridSlotPose(Slot, PoseDistanceCm);
			if (!FMath::IsNearlyEqual(PoseDistanceCm, ActualCm, 1.0e-9)
				|| !Pose.Equals(Track->GetGridSlotTransform(Slot), 1.0e-6))
			{
				AddError(FString::Printf(TEXT("GetGridSlotPose disagrees with the single accessors at slot %d"), Slot));
				break;
			}
		}

		// Out of range must be distinguishable from pole-on-the-line. Returning 0.0
		// would be a legal arc length and a silently wrong hint.
		TestEqual(TEXT("Out-of-range grid distance is the invalid sentinel"),
			Track->GetGridSlotDistanceCm(999), ATrackDefinitionActor::InvalidDistanceCm);
		TestEqual(TEXT("Negative grid index is the invalid sentinel"),
			Track->GetGridSlotDistanceCm(-1), ATrackDefinitionActor::InvalidDistanceCm);
		TestTrue(TEXT("The sentinel is negative, so it can never be mistaken for a real arc length"),
			ATrackDefinitionActor::InvalidDistanceCm < 0.0);

		double OutOfRangeDistanceCm = 0.0;
		const FTransform OutOfRangePose = Track->GetGridSlotPose(999, OutOfRangeDistanceCm);
		TestEqual(TEXT("GetGridSlotPose reports the sentinel out of range"),
			OutOfRangeDistanceCm, ATrackDefinitionActor::InvalidDistanceCm);
		TestTrue(TEXT("...and returns identity"), OutOfRangePose.Equals(FTransform::Identity, 1.0e-9));
	}

	// -- Reset samples -------------------------------------------------------
	{
		const int32 SampleCount = Track->GetNumResetSamples();
		TestTrue(TEXT("There is at least one reset sample"), SampleCount > 0);

		const double StepCm = LengthCm / static_cast<double>(SampleCount);

		TestNearlyEqual(TEXT("Reset sample 0 is at the start/finish line"),
			Track->GetResetSampleDistanceCm(0), 0.0, 1.0e-9);

		bool bAscending = true;
		for (int32 Sample = 0; Sample < SampleCount; ++Sample)
		{
			const double ActualCm = Track->GetResetSampleDistanceCm(Sample);
			const double ExpectedCm = static_cast<double>(Sample) * StepCm;

			if (!FMath::IsNearlyEqual(ActualCm, ExpectedCm, 1.0e-6))
			{
				AddError(FString::Printf(TEXT("Reset sample %d distance %f, expected %f"), Sample, ActualCm, ExpectedCm));
				bAscending = false;
				break;
			}

			if (Sample > 0 && ActualCm <= Track->GetResetSampleDistanceCm(Sample - 1))
			{
				AddError(FString::Printf(TEXT("Reset sample distances are not strictly ascending at %d"), Sample));
				bAscending = false;
				break;
			}
		}
		TestTrue(TEXT("Reset sample distances are the bake's own ascending arc lengths"), bAscending);

		TestEqual(TEXT("Out-of-range reset distance is the invalid sentinel"),
			Track->GetResetSampleDistanceCm(99999), ATrackDefinitionActor::InvalidDistanceCm);
		TestEqual(TEXT("Negative reset index is the invalid sentinel"),
			Track->GetResetSampleDistanceCm(-1), ATrackDefinitionActor::InvalidDistanceCm);

		// -- The reset path returns index AND distance together ---------------
		bool bConsistent = true;
		for (int32 Step = 0; Step < 401; ++Step)
		{
			const double QueryCm = LengthCm * static_cast<double>(Step) / 401.0;

			int32 Index = INDEX_NONE;
			double DistanceCm = ATrackDefinitionActor::InvalidDistanceCm;
			const FTransform Pose = Track->GetResetPoseAtOrBeforeDistanceCm(QueryCm, Index, DistanceCm);

			if (Index == INDEX_NONE)
			{
				AddError(FString::Printf(TEXT("No reset sample at %f cm"), QueryCm));
				bConsistent = false;
				break;
			}

			// The distance must be the chosen sample's own distance -- not the query's,
			// and not a re-projection of the pose.
			if (!FMath::IsNearlyEqual(DistanceCm, Track->GetResetSampleDistanceCm(Index), 1.0e-9))
			{
				AddError(FString::Printf(TEXT("Reset pose at %f cm reported distance %f for index %d"),
					QueryCm, DistanceCm, Index));
				bConsistent = false;
				break;
			}

			// Rule 8 again, now through the distance rather than through a projection:
			// the reported distance can never be ahead of the query.
			if (Centerline.GetSignedDistanceDeltaCm(DistanceCm, QueryCm) < -1.0e-6)
			{
				AddError(FString::Printf(TEXT("Reset at %f cm reported a distance AHEAD of it (%f)"), QueryCm, DistanceCm));
				bConsistent = false;
				break;
			}

			// The legacy two-argument overload must still agree, or existing callers
			// silently diverge from new ones.
			int32 LegacyIndex = INDEX_NONE;
			const FTransform LegacyPose = Track->GetResetTransformAtOrBeforeDistanceCm(QueryCm, LegacyIndex);
			if (LegacyIndex != Index || !LegacyPose.Equals(Pose, 1.0e-9))
			{
				AddError(FString::Printf(TEXT("The two- and three-argument reset overloads disagree at %f cm"), QueryCm));
				bConsistent = false;
				break;
			}
		}
		TestTrue(TEXT("Reset pose, index and distance are mutually consistent across the lap"), bConsistent);

		// -- THE POINT OF THE WHOLE FINDING -----------------------------------
		// The returned distance must actually work as a hint: re-seeding the windowed
		// search with it has to reproduce the same place on the track. If it did not,
		// the accessor would be decoration.
		int32 SeedIndex = INDEX_NONE;
		double SeedDistanceCm = ATrackDefinitionActor::InvalidDistanceCm;
		const FTransform SeedPose = Track->GetResetPoseAtOrBeforeDistanceCm(LengthCm * 0.37, SeedIndex, SeedDistanceCm);

		const FTrackCenterlineQuery Reseeded = Track->FindNearestCenterlinePointNear(
			SeedPose.GetLocation(), SeedDistanceCm, /*SearchWindowCm*/ 1000.0);

		TestTrue(TEXT("A reset distance re-seeds the windowed search successfully"), Reseeded.bValid);
		TestNearlyEqual(TEXT("...and lands back on the pose it came from"),
			Centerline.GetSignedDistanceDeltaCm(SeedDistanceCm, Reseeded.DistanceAlongCm), 0.0, 1.0);

		// Same for a grid slot at race start.
		double PoleDistanceCm = 0.0;
		const FTransform PolePose = Track->GetGridSlotPose(0, PoleDistanceCm);
		const FTrackCenterlineQuery PoleReseeded = Track->FindNearestCenterlinePointNear(
			PolePose.GetLocation(), PoleDistanceCm, /*SearchWindowCm*/ 1000.0);

		TestTrue(TEXT("A grid slot distance re-seeds the windowed search at race start"), PoleReseeded.bValid);
		TestNearlyEqual(TEXT("...and lands back on pole"),
			Centerline.GetSignedDistanceDeltaCm(PoleDistanceCm, PoleReseeded.DistanceAlongCm), 0.0, 5.0);
	}

	return true;
}

// ===========================================================================
// M4 (code-reviewer pass 1): the CDO fixture's restore must be exhaustive
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrackDefinitionFixtureRestoreTest,
	"RacingSim.Race.TrackFixtureRestore",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FTrackDefinitionFixtureRestoreTest::RunTest(const FString& Parameters)
{
	using namespace TrackDefinitionSpecPrivate;

	// This suite tests the TEST HARNESS, deliberately. Every other suite in this file
	// borrows a process-wide CDO and promises to give it back unchanged; nothing
	// verified the promise, and it was being broken. The fixture saved spline point
	// LOCATIONS only, so its restore silently reset every arrive tangent, leave tangent
	// and point type. A suite running afterwards would query a subtly different curve
	// and blame the code under test.

	ATrackDefinitionActor* Track = GetMutableDefault<ATrackDefinitionActor>();
	if (!Track)
	{
		AddError(TEXT("Class default object for ATrackDefinitionActor is unavailable."));
		return false;
	}

	// Snapshot from OUTSIDE the fixture, so this is an independent witness rather than
	// the fixture checking its own homework.
	USplineComponent* Spline = Track->GetCenterlineSpline();
	if (!Spline)
	{
		AddError(TEXT("Centerline spline default subobject is unavailable."));
		return false;
	}

	const FName OuterTrackId = Track->TrackId;
	const double OuterSpacingCm = Track->CenterlineSampleSpacingCm;
	const TArray<double> OuterSectorsCm = Track->SectorStartDistancesCm;
	const int32 OuterGridSlots = Track->NumGridSlots;
	const double OuterPoleSetbackCm = Track->GridPoleSetbackCm;
	const double OuterGridSpacingCm = Track->GridSlotSpacingCm;
	const double OuterLateralCm = Track->GridSlotLateralOffsetCm;
	const double OuterHeightCm = Track->PoseHeightOffsetCm;
	const double OuterResetSpacingCm = Track->ResetSampleSpacingCm;
	const bool bOuterClosedLoop = Spline->IsClosedLoop();

	const int32 OuterPointCount = Spline->GetNumberOfSplinePoints();
	TArray<FVector> OuterLocations;
	TArray<FVector> OuterArriveTangents;
	TArray<FVector> OuterLeaveTangents;
	TArray<ESplinePointType::Type> OuterPointTypes;
	for (int32 Index = 0; Index < OuterPointCount; ++Index)
	{
		OuterLocations.Add(Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local));
		OuterArriveTangents.Add(Spline->GetArriveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local));
		OuterLeaveTangents.Add(Spline->GetLeaveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local));
		OuterPointTypes.Add(Spline->GetSplinePointType(Index));
	}

	// -- Run a fixture through its whole lifetime, mutating hard inside it ----
	{
		FTrackSpecCircleFixture Fixture;
		if (!Fixture.Track)
		{
			AddError(TEXT("Fixture failed to acquire the CDO."));
			return false;
		}

		// The fixture's own snapshot must survive its own authoring: restore mid-life
		// and the spline must match what the fixture found, not what it wrote.
		Fixture.RestoreSpline();
		FString Mismatch;
		const bool bMatches = Fixture.SplineMatchesSnapshot(Mismatch);
		TestTrue(FString::Printf(TEXT("Mid-life RestoreSpline is exhaustive (mismatch: %s)"), *Mismatch), bMatches);

		// Re-author, then abuse every channel a restore could forget.
		Fixture.Track->RebuildTrackData();
		Spline->SetSplinePointType(0, ESplinePointType::Linear, /*bUpdateSpline*/ false);
		Spline->SetTangentsAtSplinePoint(0, FVector(1234.0, 0.0, 0.0), FVector(0.0, 4321.0, 0.0),
			ESplineCoordinateSpace::Local, /*bUpdateSpline*/ true);
		Fixture.Track->TrackId = FName(TEXT("Track.Test.Vandalised"));
		Fixture.Track->NumGridSlots = 31;
		Fixture.Track->PoseHeightOffsetCm = 4321.0;
	}
	// Fixture destructor has now run.

	// -- Everything must be back ---------------------------------------------
	TestEqual(TEXT("TrackId restored"), Track->TrackId, OuterTrackId);
	TestEqual(TEXT("CenterlineSampleSpacingCm restored"), Track->CenterlineSampleSpacingCm, OuterSpacingCm);
	TestTrue(TEXT("SectorStartDistancesCm restored"), Track->SectorStartDistancesCm == OuterSectorsCm);
	TestEqual(TEXT("NumGridSlots restored"), Track->NumGridSlots, OuterGridSlots);
	TestEqual(TEXT("GridPoleSetbackCm restored"), Track->GridPoleSetbackCm, OuterPoleSetbackCm);
	TestEqual(TEXT("GridSlotSpacingCm restored"), Track->GridSlotSpacingCm, OuterGridSpacingCm);
	TestEqual(TEXT("GridSlotLateralOffsetCm restored"), Track->GridSlotLateralOffsetCm, OuterLateralCm);
	TestEqual(TEXT("PoseHeightOffsetCm restored"), Track->PoseHeightOffsetCm, OuterHeightCm);
	TestEqual(TEXT("ResetSampleSpacingCm restored"), Track->ResetSampleSpacingCm, OuterResetSpacingCm);

	TestTrue(TEXT("Closed-loop flag restored"), Spline->IsClosedLoop() == bOuterClosedLoop);
	TestEqual(TEXT("Spline point count restored"), Spline->GetNumberOfSplinePoints(), OuterPointCount);

	for (int32 Index = 0; Index < FMath::Min(OuterPointCount, Spline->GetNumberOfSplinePoints()); ++Index)
	{
		TestTrue(FString::Printf(TEXT("Spline point %d location restored"), Index),
			Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local).Equals(OuterLocations[Index], 1.0e-4));

		// THE FOUR ASSERTIONS THAT WOULD HAVE CAUGHT THE ORIGINAL BUG.
		TestTrue(FString::Printf(TEXT("Spline point %d arrive tangent restored"), Index),
			Spline->GetArriveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local).Equals(OuterArriveTangents[Index], 1.0e-3));
		TestTrue(FString::Printf(TEXT("Spline point %d leave tangent restored"), Index),
			Spline->GetLeaveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local).Equals(OuterLeaveTangents[Index], 1.0e-3));
		TestEqual(FString::Printf(TEXT("Spline point %d type restored"), Index),
			static_cast<int32>(Spline->GetSplinePointType(Index)), static_cast<int32>(OuterPointTypes[Index]));
	}

	return true;
}
