// Copyright RacingSim. All Rights Reserved.

#include "Race/TrackCheckpointGate.h"
#include "Race/TrackCenterline.h"

#include "Misc/AutomationTest.h"

#include <limits>

/**
 * TRACK-002: FRacingCheckpointGate / FRacingCheckpointGateSet.
 *
 * ===========================================================================
 * These tests need no level, and that is deliberate
 * ===========================================================================
 *
 * TRACK-001 established the pattern and the ticket requires it to continue: the gate
 * contract is exercised against a PROCEDURALLY CONSTRUCTED centerline and gate set, so
 * every correctness claim here stands on its own regardless of what any .umap contains.
 * A test that needs a loaded map to prove a sign convention cannot distinguish "the rule
 * is wrong" from "the map moved", and the project's only automation gate
 * (`Automation RunFilter Smoke`, -nullrhi, Docs/Environment.md) is a poor place to
 * discover which.
 *
 * The map-dependent claim -- that a PLACED actor bakes correctly from PostLoad -- is
 * TRACK-001 M5 and lives in TrackLevelSpec.cpp. Nothing in this file depends on it.
 *
 * ===========================================================================
 * Why the primary fixture is a straight line
 * ===========================================================================
 *
 * Every geometric expectation below is then exact rather than approximate: a gate on a
 * straight has PlaneNormal = +X, RightAxis = +Y, UpAxis = +Z with no discretisation
 * error, so an assertion that fails is a rule that is wrong and not a tolerance that is
 * tight. The circle fixture is used only where curvature is the subject.
 *
 * Both shapes are constructed from arithmetic. Neither traces any circuit.
 *
 * ===========================================================================
 * What is NOT tested here, on purpose
 * ===========================================================================
 *
 * Lap counting, expected-gate cursors, skipped-gate invalidation and restart. TRACK-002
 * is lap-agnostic; those are RACE-002's, and asserting them here would legitimise the
 * dependency the ticket explicitly forbids. What IS here is the crossing-direction half
 * of `.claude/rules/race-tests.md`'s rule: forward, reverse, tangential/grazing,
 * high-speed single-tick, double overlap and spin-at-a-gate.
 *
 * SmokeFilter is mandatory: Docs/Environment.md records that a test outside the one
 * filter the documented gate uses once sat green and unexecuted.
 */

namespace TrackGateSpecPrivate
{
	// Named uniquely rather than placed in an anonymous namespace: unity builds
	// concatenate translation units, and duplicate anonymous symbols across a blob are a
	// redefinition rather than two file-local helpers.

	constexpr double GateSpecStraightLengthCm = 100000.0;   // 1 km
	constexpr int32 GateSpecStraightSamples = 101;          // 1000 cm apart
	constexpr double GateSpecStraightSegmentCm = 1000.0;

	constexpr double GateSpecCircleRadiusCm = 10000.0;      // 100 m
	constexpr int32 GateSpecCircleSamples = 720;

	constexpr double GateSpecHalfWidthCm = 900.0;
	constexpr double GateSpecHalfHeightCm = 500.0;

	/** An OPEN straight centerline along +X, sampled every 1000 cm. Exact in every axis. */
	bool BuildStraight(FTrackCenterline& Out, FString& OutError)
	{
		TArray<FVector> Locations;
		TArray<double> Distances;
		Locations.Reserve(GateSpecStraightSamples);
		Distances.Reserve(GateSpecStraightSamples);

		for (int32 Index = 0; Index < GateSpecStraightSamples; ++Index)
		{
			const double DistanceCm = static_cast<double>(Index) * GateSpecStraightSegmentCm;
			Locations.Add(FVector(DistanceCm, 0.0, 0.0));
			Distances.Add(DistanceCm);
		}

		return Out.Build(Locations, Distances, GateSpecStraightLengthCm, /*bClosedLoop*/ false, OutError);
	}

	/** A closed circular centerline travelled counter-clockwise, with EXACT arc-length distances. */
	bool BuildCircle(FTrackCenterline& Out, FString& OutError)
	{
		const double TotalCm = 2.0 * UE_DOUBLE_PI * GateSpecCircleRadiusCm;
		const double StepCm = TotalCm / static_cast<double>(GateSpecCircleSamples);

		TArray<FVector> Locations;
		TArray<double> Distances;
		Locations.Reserve(GateSpecCircleSamples);
		Distances.Reserve(GateSpecCircleSamples);

		for (int32 Index = 0; Index < GateSpecCircleSamples; ++Index)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(Index) / static_cast<double>(GateSpecCircleSamples);
			Locations.Add(FVector(
				GateSpecCircleRadiusCm * FMath::Cos(Angle),
				GateSpecCircleRadiusCm * FMath::Sin(Angle),
				0.0));
			Distances.Add(static_cast<double>(Index) * StepCm);
		}

		return Out.Build(Locations, Distances, TotalCm, /*bClosedLoop*/ true, OutError);
	}

	FRacingCheckpointGateSpec MakeSpec(
		const TCHAR* Id,
		const double DistanceCm,
		const ERacingGateDirection Direction = ERacingGateDirection::Forward)
	{
		FRacingCheckpointGateSpec Spec;
		Spec.GateId = FName(Id);
		Spec.DistanceAlongCm = DistanceCm;
		Spec.HalfWidthCm = GateSpecHalfWidthCm;
		Spec.HalfHeightCm = GateSpecHalfHeightCm;
		Spec.LegalDirection = Direction;
		return Spec;
	}

	/** Three forward gates on the straight, at 0, 20000 and 40000 cm. */
	void MakeStraightSpecs(TArray<FRacingCheckpointGateSpec>& Out)
	{
		Out.Reset();
		Out.Add(MakeSpec(TEXT("Gate.StartFinish"), 0.0));
		Out.Add(MakeSpec(TEXT("Gate.01"), 20000.0));
		Out.Add(MakeSpec(TEXT("Gate.02"), 40000.0));
	}
}

// ===========================================================================
// Build and rejection
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingGateSetBuildTest,
	"RacingSim.Race.GateSetBuild",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingGateSetBuildTest::RunTest(const FString& Parameters)
{
	using namespace TrackGateSpecPrivate;

	FTrackCenterline Straight;
	FString Error;
	if (!BuildStraight(Straight, Error))
	{
		AddError(FString::Printf(TEXT("Straight centerline failed to build: %s"), *Error));
		return false;
	}

	TArray<FRacingCheckpointGateSpec> Specs;
	MakeStraightSpecs(Specs);

	// The straight is straight, so any radius is honest; a large one keeps the placement
	// tolerance small and lets the gate-width rejection below be tested in isolation.
	constexpr double StraightRadiusCm = 100000.0;

	// -- The happy path ------------------------------------------------------
	{
		FRacingCheckpointGateSet Set;
		FString BuildError;
		TestTrue(TEXT("A well-formed gate set builds"), Set.Build(Specs, Straight, StraightRadiusCm, BuildError));
		TestEqual(TEXT("Error string untouched on success"), BuildError, FString());
		TestTrue(TEXT("Built set is valid"), Set.IsValid());
		TestEqual(TEXT("Three gates"), Set.NumGates(), 3);

		// The set carries the MAXIMUM segment length, not the mean. On a uniform straight
		// the two coincide, which is exactly why the non-uniform case is asserted
		// separately in RacingSim.Race.CenterlinePolylineBias -- here it only has to
		// agree with the geometry.
		TestNearlyEqual(TEXT("Set records the centerline's max segment length"),
			Set.GetMaxSegmentLengthCm(), GateSpecStraightSegmentCm, 1.0e-6);
		TestNearlyEqual(TEXT("Placement tolerance is the sagitta at the supplied radius"),
			Set.GetPlacementToleranceCm(), Straight.GetSagittaBoundCm(StraightRadiusCm), 1.0e-9);
		TestTrue(TEXT("Placement tolerance is positive but far below a gate half-width"),
			Set.GetPlacementToleranceCm() > 0.0 && Set.GetPlacementToleranceCm() < GateSpecHalfWidthCm);

		// -- Geometry, exact on a straight -----------------------------------
		const FRacingCheckpointGate* Gate = Set.GetGate(1);
		TestNotNull(TEXT("Gate 1 exists"), Gate);
		if (Gate)
		{
			TestEqual(TEXT("Ordinal matches the index"), Gate->Ordinal, 1);
			TestEqual(TEXT("Gate id survives the bake"), Gate->GateId, FName(TEXT("Gate.01")));
			TestNearlyEqual(TEXT("Gate distance survives the bake"), Gate->DistanceAlongCm, 20000.0, 1.0e-9);
			TestTrue(TEXT("Gate sits on the centerline at its distance"),
				Gate->Location.Equals(FVector(20000.0, 0.0, 0.0), 1.0e-6));

			// The three axes ARE the contract: PlaneNormal decides which crossings are
			// forward, RightAxis decides what "width" means, UpAxis decides what "height"
			// means. On a +X straight they are exactly the world axes.
			TestTrue(TEXT("Plane normal is the direction of travel"),
				Gate->PlaneNormal.Equals(FVector(1.0, 0.0, 0.0), 1.0e-9));
			TestTrue(TEXT("Right axis is +Y (Up x Forward, matching FTrackCenterlineQuery)"),
				Gate->RightAxis.Equals(FVector(0.0, 1.0, 0.0), 1.0e-9));
			TestTrue(TEXT("Up axis is +Z"), Gate->UpAxis.Equals(FVector(0.0, 0.0, 1.0), 1.0e-9));

			// Orthonormality is what makes the extent test a rectangle rather than a
			// parallelogram. Asserted rather than assumed, because the up axis is derived.
			TestNearlyEqual(TEXT("Normal and right are orthogonal"),
				FVector::DotProduct(Gate->PlaneNormal, Gate->RightAxis), 0.0, 1.0e-12);
			TestNearlyEqual(TEXT("Normal and up are orthogonal"),
				FVector::DotProduct(Gate->PlaneNormal, Gate->UpAxis), 0.0, 1.0e-12);
			TestNearlyEqual(TEXT("Right and up are orthogonal"),
				FVector::DotProduct(Gate->RightAxis, Gate->UpAxis), 0.0, 1.0e-12);

			// The pose must describe the SAME rectangle the crossing test uses, or a
			// debug draw and the rule disagree and the rule loses.
			const FTransform Pose = Gate->GetTransform();
			TestTrue(TEXT("Pose location is the gate location"), Pose.GetLocation().Equals(Gate->Location, 1.0e-6));
			TestTrue(TEXT("Pose X is the plane normal"),
				Pose.GetRotation().GetForwardVector().Equals(Gate->PlaneNormal, 1.0e-6));
			TestTrue(TEXT("Pose Y is the right axis"),
				Pose.GetRotation().GetRightVector().Equals(Gate->RightAxis, 1.0e-6));
			TestTrue(TEXT("Pose Z is the up axis"),
				Pose.GetRotation().GetUpVector().Equals(Gate->UpAxis, 1.0e-6));
			TestTrue(TEXT("Pose scale is identity"), Pose.GetScale3D().Equals(FVector::OneVector, 1.0e-9));
		}

		// -- Ordering and lookup ---------------------------------------------
		TestEqual(TEXT("Lookup by id"), Set.FindGateIndexById(FName(TEXT("Gate.02"))), 2);
		TestEqual(TEXT("Lookup of an unknown id"), Set.FindGateIndexById(FName(TEXT("Gate.Nope"))), INDEX_NONE);
		TestEqual(TEXT("Start/finish is gate 0"), FRacingCheckpointGateSet::StartFinishGateIndex, 0);
		TestEqual(TEXT("Next after 0 is 1"), Set.GetNextGateIndex(0), 1);
		TestEqual(TEXT("Next after the last wraps to 0"), Set.GetNextGateIndex(2), 0);
		TestEqual(TEXT("Next after an invalid index is INDEX_NONE"), Set.GetNextGateIndex(99), INDEX_NONE);
		TestNull(TEXT("Out-of-range gate is null, not a plausible gate at the origin"), Set.GetGate(99));
		TestNull(TEXT("Negative gate index is null"), Set.GetGate(-1));

		// -- Reset -----------------------------------------------------------
		Set.Reset();
		TestFalse(TEXT("Reset invalidates the set"), Set.IsValid());
		TestEqual(TEXT("Reset empties the gates"), Set.NumGates(), 0);
		TestNearlyEqual(TEXT("Reset clears the placement tolerance"), Set.GetPlacementToleranceCm(), 0.0, 0.0);
	}

	// -- Every rejection -----------------------------------------------------
	//
	// A malformed gate set that half-builds is worse than one that refuses: the symptom
	// is a track that silently stops counting laps, or counts ones it should not.
	auto ExpectRejected = [this, &Straight](const TCHAR* What, TArrayView<const FRacingCheckpointGateSpec> InSpecs, double RadiusCm)
	{
		FRacingCheckpointGateSet Set;
		FString BuildError;
		const bool bBuilt = Set.Build(InSpecs, Straight, RadiusCm, BuildError);
		TestFalse(FString::Printf(TEXT("Rejected: %s"), What), bBuilt);
		TestFalse(FString::Printf(TEXT("Left unbuilt after rejection: %s"), What), Set.IsValid());
		TestTrue(FString::Printf(TEXT("Gave a reason: %s"), What), !BuildError.IsEmpty());
	};

	ExpectRejected(TEXT("no gates at all"), TArray<FRacingCheckpointGateSpec>(), StraightRadiusCm);

	{
		TArray<FRacingCheckpointGateSpec> Bad = Specs;
		Bad[0].GateId = NAME_None;
		ExpectRejected(TEXT("a gate with no id"), Bad, StraightRadiusCm);
	}
	{
		TArray<FRacingCheckpointGateSpec> Bad = Specs;
		Bad[2].GateId = Bad[1].GateId;
		ExpectRejected(TEXT("two gates sharing an id"), Bad, StraightRadiusCm);
	}
	{
		// Gate 0 IS the start/finish gate. Letting it float off the distance origin
		// reintroduces exactly the offset-and-rewrap arithmetic TRACK-001 refused.
		TArray<FRacingCheckpointGateSpec> Bad = Specs;
		Bad[0].DistanceAlongCm = 500.0;
		ExpectRejected(TEXT("gate 0 not at distance 0"), Bad, StraightRadiusCm);
	}
	{
		TArray<FRacingCheckpointGateSpec> Bad = Specs;
		Bad[2].DistanceAlongCm = Bad[1].DistanceAlongCm - 100.0;
		ExpectRejected(TEXT("gate distances not strictly increasing"), Bad, StraightRadiusCm);
	}
	{
		TArray<FRacingCheckpointGateSpec> Bad = Specs;
		Bad[2].DistanceAlongCm = GateSpecStraightLengthCm;
		ExpectRejected(TEXT("a gate at or past the lap length"), Bad, StraightRadiusCm);
	}
	{
		TArray<FRacingCheckpointGateSpec> Bad = Specs;
		Bad[1].DistanceAlongCm = std::numeric_limits<double>::quiet_NaN();
		ExpectRejected(TEXT("a non-finite gate distance"), Bad, StraightRadiusCm);
	}
	{
		// TRACK-001 L2 IN ANGER. Two gates inside one polyline segment share a plane
		// normal and a near-identical location, so which one a motion segment met first
		// becomes a floating-point coin flip. The separation floor is the MAXIMUM segment
		// length; the mean would have permitted this on any non-uniform centerline.
		TArray<FRacingCheckpointGateSpec> Bad = Specs;
		Bad[1].DistanceAlongCm = 20000.0;
		Bad[2].DistanceAlongCm = 20000.0 + GateSpecStraightSegmentCm * 0.5;
		ExpectRejected(TEXT("two gates inside one centerline segment"), Bad, StraightRadiusCm);

		// ...and exactly one segment apart is still rejected (the check is <=), while
		// comfortably more than one segment apart is accepted. Asserting the boundary in
		// both directions is what stops this degenerating into "some separation is
		// required, nobody knows how much".
		Bad[2].DistanceAlongCm = 20000.0 + GateSpecStraightSegmentCm;
		ExpectRejected(TEXT("two gates exactly one segment apart"), Bad, StraightRadiusCm);

		Bad[2].DistanceAlongCm = 20000.0 + GateSpecStraightSegmentCm * 1.5;
		FRacingCheckpointGateSet Set;
		FString BuildError;
		TestTrue(TEXT("One and a half segments apart is accepted"),
			Set.Build(Bad, Straight, StraightRadiusCm, BuildError));
	}
	{
		TArray<FRacingCheckpointGateSpec> Bad = Specs;
		Bad[1].HalfWidthCm = 0.0;
		ExpectRejected(TEXT("zero gate half-width"), Bad, StraightRadiusCm);

		Bad[1].HalfWidthCm = -900.0;
		ExpectRejected(TEXT("negative gate half-width"), Bad, StraightRadiusCm);

		Bad[1].HalfWidthCm = std::numeric_limits<double>::infinity();
		ExpectRejected(TEXT("non-finite gate half-width"), Bad, StraightRadiusCm);
	}
	{
		TArray<FRacingCheckpointGateSpec> Bad = Specs;
		Bad[1].HalfHeightCm = 0.0;
		ExpectRejected(TEXT("zero gate half-height"), Bad, StraightRadiusCm);
	}
	{
		// THE POLYLINE-BIAS REJECTION. A gate narrower than the model's own systematic
		// inward error has no margin left before a car is anywhere near it, and because
		// the bias is one-directional it does not average away over a lap.
		//
		// The numbers are worked rather than guessed, because a first attempt at this
		// case used a 100 cm radius and a 400 cm half-width and FAILED TO BE REJECTED.
		// The sagitta at a 1000 cm segment peaks where the segment subtends exactly pi,
		// i.e. at R = 1000 / pi = 318 cm, and that peak is R itself -- about 318 cm, not
		// the 500 cm clamp the case had assumed. A tighter radius than that makes the
		// bound SMALLER, not larger. So: sit at the peak and use a gate narrower than it.
		constexpr double PeakToleranceRadiusCm = 320.0;

		FTrackCenterline Probe;
		FString ProbeError;
		if (BuildStraight(Probe, ProbeError))
		{
			const double PeakToleranceCm = Probe.GetSagittaBoundCm(PeakToleranceRadiusCm);
			TestTrue(TEXT("The placement tolerance peaks near R = segment/pi, above 300 cm"),
				PeakToleranceCm > 300.0);

			TArray<FRacingCheckpointGateSpec> Bad = Specs;
			Bad[1].HalfWidthCm = 200.0;
			ExpectRejected(TEXT("a gate narrower than the placement tolerance"), Bad, PeakToleranceRadiusCm);

			// ...and a gate comfortably wider than the same tolerance is accepted, so the
			// rejection is the rule firing rather than the whole configuration being bad.
			Bad[1].HalfWidthCm = 900.0;
			FRacingCheckpointGateSet WideSet;
			FString WideError;
			TestTrue(TEXT("A gate wider than the placement tolerance is accepted"),
				WideSet.Build(Bad, Probe, PeakToleranceRadiusCm, WideError));
		}
	}
	{
		ExpectRejected(TEXT("a non-positive minimum corner radius"), Specs, 0.0);
		ExpectRejected(TEXT("a non-finite minimum corner radius"), Specs,
			std::numeric_limits<double>::quiet_NaN());
	}
	{
		// An unbuilt centerline has no geometry to derive a plane from. Refusing here
		// rather than baking gates at the origin means "the track has no gates" instead
		// of "every gate is stacked on the world origin", which would look like a working
		// track that rejects every lap.
		const FTrackCenterline Unbuilt;
		FRacingCheckpointGateSet Set;
		FString BuildError;
		TestFalse(TEXT("Rejected: gates against an unbuilt centerline"),
			Set.Build(Specs, Unbuilt, StraightRadiusCm, BuildError));
		TestTrue(TEXT("Gave a reason: gates against an unbuilt centerline"), !BuildError.IsEmpty());
	}

	// -- A rejected rebuild must not clobber a good set ----------------------
	{
		FRacingCheckpointGateSet Set;
		FString BuildError;
		TestTrue(TEXT("Good build first"), Set.Build(Specs, Straight, StraightRadiusCm, BuildError));

		TArray<FRacingCheckpointGateSpec> Bad = Specs;
		Bad[1].GateId = NAME_None;

		FString SecondError;
		TestFalse(TEXT("Second, malformed build is rejected"), Set.Build(Bad, Straight, StraightRadiusCm, SecondError));
		TestTrue(TEXT("Rejected rebuild left the previous set intact"), Set.IsValid());
		TestEqual(TEXT("...with all its gates"), Set.NumGates(), 3);
		TestEqual(TEXT("...and their ids"), Set.FindGateIndexById(FName(TEXT("Gate.01"))), 1);
	}

	// -- A gate on a vertical segment cannot exist ---------------------------
	//
	// TRACK-001 L7, enforced at BAKE time. FTrackCenterlineQuery now flags a degenerate
	// sideways axis instead of reporting a false zero, but a gate must go further and
	// refuse to be built: a rectangle whose width has no direction is not a rectangle.
	{
		const TArray<FVector> Vertical({ FVector(0.0, 0.0, 0.0), FVector(0.0, 0.0, 5000.0), FVector(0.0, 0.0, 10000.0) });
		FTrackCenterline VerticalLine;
		FString VerticalError;
		TestTrue(TEXT("A vertical centerline builds (it is legal geometry)"),
			VerticalLine.BuildFromPolyline(Vertical, /*bClosedLoop*/ false, VerticalError));

		FRacingCheckpointGateSet Set;
		FString BuildError;
		const TArray<FRacingCheckpointGateSpec> OneGate({ MakeSpec(TEXT("Gate.StartFinish"), 0.0) });
		TestFalse(TEXT("Rejected: a gate on an exactly vertical centerline segment"),
			Set.Build(OneGate, VerticalLine, StraightRadiusCm, BuildError));
		TestTrue(TEXT("...and the reason names the vertical geometry"),
			BuildError.Contains(TEXT("vertical")));
	}

	return true;
}

// ===========================================================================
// Crossing direction: the rule this ticket owns
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingGateCrossingDirectionTest,
	"RacingSim.Race.GateCrossingDirection",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingGateCrossingDirectionTest::RunTest(const FString& Parameters)
{
	using namespace TrackGateSpecPrivate;

	FTrackCenterline Straight;
	FString Error;
	if (!BuildStraight(Straight, Error))
	{
		AddError(FString::Printf(TEXT("Straight centerline failed to build: %s"), *Error));
		return false;
	}

	TArray<FRacingCheckpointGateSpec> Specs;
	MakeStraightSpecs(Specs);

	FRacingCheckpointGateSet Set;
	FString BuildError;
	if (!Set.Build(Specs, Straight, 100000.0, BuildError))
	{
		AddError(FString::Printf(TEXT("Gate set failed to build: %s"), *BuildError));
		return false;
	}

	// Gate 1 sits at (20000, 0, 0) with normal +X, right +Y, up +Z.
	constexpr int32 GateIndex = 1;
	const FVector GateAt(20000.0, 0.0, 0.0);

	// -- Forward -------------------------------------------------------------
	{
		const FRacingGateCrossingResult Result = Set.EvaluateCrossing(
			GateIndex, GateAt - FVector(500.0, 0.0, 0.0), GateAt + FVector(500.0, 0.0, 0.0));

		TestTrue(TEXT("Forward crossing was evaluated"), Result.bEvaluated);
		TestEqual(TEXT("Forward crossing is Forward"), Result.Crossing, ERacingGateCrossing::Forward);
		TestTrue(TEXT("Forward crossing of a Forward gate is legal"), Result.bMatchesLegalDirection);
		TestTrue(TEXT("Forward crossing goes through the gate"), Result.IsThroughGate());
		TestTrue(TEXT("...and through the plane"), Result.DidCrossPlane());
		TestEqual(TEXT("Result carries the gate index"), Result.GateIndex, GateIndex);
		TestEqual(TEXT("Result carries the gate id"), Result.GateId, FName(TEXT("Gate.01")));
		TestNearlyEqual(TEXT("Start is 500 cm behind the plane"), Result.SignedDistanceFromCm, -500.0, 1.0e-9);
		TestNearlyEqual(TEXT("End is 500 cm past the plane"), Result.SignedDistanceToCm, 500.0, 1.0e-9);
		TestNearlyEqual(TEXT("Crossed halfway through the motion"), Result.CrossingAlpha, 0.5, 1.0e-12);
		TestTrue(TEXT("Crossing point is on the gate"), Result.CrossingPoint.Equals(GateAt, 1.0e-6));
		TestNearlyEqual(TEXT("Crossed dead centre"), Result.CrossingLateralOffsetCm, 0.0, 1.0e-9);
		TestNearlyEqual(TEXT("...at gate height"), Result.CrossingVerticalOffsetCm, 0.0, 1.0e-9);
	}

	// -- Reverse -------------------------------------------------------------
	//
	// THE CENTRAL REQUIREMENT OF THIS TICKET. A reverse crossing must be
	// DISTINGUISHABLE from a forward one, not merely rejected: RACE-002 has to
	// invalidate the lap AND record a reason, and "nothing happened" is not a reason.
	{
		const FRacingGateCrossingResult Result = Set.EvaluateCrossing(
			GateIndex, GateAt + FVector(500.0, 0.0, 0.0), GateAt - FVector(500.0, 0.0, 0.0));

		TestTrue(TEXT("Reverse crossing was evaluated"), Result.bEvaluated);
		TestEqual(TEXT("Reverse crossing is Reverse, not None"), Result.Crossing, ERacingGateCrossing::Reverse);
		TestFalse(TEXT("Reverse crossing of a Forward gate is ILLEGAL"), Result.bMatchesLegalDirection);
		TestTrue(TEXT("...but it still counts as going through the gate"), Result.IsThroughGate());
		TestNearlyEqual(TEXT("Start is ahead of the plane"), Result.SignedDistanceFromCm, 500.0, 1.0e-9);
		TestNearlyEqual(TEXT("End is behind it"), Result.SignedDistanceToCm, -500.0, 1.0e-9);
		TestNearlyEqual(TEXT("Crossed halfway"), Result.CrossingAlpha, 0.5, 1.0e-12);
	}

	// -- Legality is per gate, and independent of the physics ----------------
	{
		TArray<FRacingCheckpointGateSpec> Mixed;
		Mixed.Add(MakeSpec(TEXT("Gate.StartFinish"), 0.0, ERacingGateDirection::Forward));
		Mixed.Add(MakeSpec(TEXT("Gate.Both"), 20000.0, ERacingGateDirection::Bidirectional));
		Mixed.Add(MakeSpec(TEXT("Gate.Back"), 40000.0, ERacingGateDirection::Reverse));

		FRacingCheckpointGateSet MixedSet;
		FString MixedError;
		TestTrue(TEXT("A set with mixed legal directions builds"),
			MixedSet.Build(Mixed, Straight, 100000.0, MixedError));

		auto Cross = [&MixedSet](int32 Index, double AtCm, bool bForward)
		{
			const FVector Centre(AtCm, 0.0, 0.0);
			const FVector Delta(500.0, 0.0, 0.0);
			return bForward
				? MixedSet.EvaluateCrossing(Index, Centre - Delta, Centre + Delta)
				: MixedSet.EvaluateCrossing(Index, Centre + Delta, Centre - Delta);
		};

		TestTrue(TEXT("Bidirectional gate: forward is legal"), Cross(1, 20000.0, true).bMatchesLegalDirection);
		TestTrue(TEXT("Bidirectional gate: reverse is ALSO legal"), Cross(1, 20000.0, false).bMatchesLegalDirection);
		TestEqual(TEXT("Bidirectional gate still reports which way"),
			Cross(1, 20000.0, false).Crossing, ERacingGateCrossing::Reverse);

		TestFalse(TEXT("Reverse-only gate: forward is illegal"), Cross(2, 40000.0, true).bMatchesLegalDirection);
		TestTrue(TEXT("Reverse-only gate: reverse is legal"), Cross(2, 40000.0, false).bMatchesLegalDirection);
		TestEqual(TEXT("Reverse-only gate crossed forwards still reports Forward"),
			Cross(2, 40000.0, true).Crossing, ERacingGateCrossing::Forward);
	}

	// -- Tangential and grazing ----------------------------------------------
	{
		// Sliding sideways ENTIRELY on one side of the plane. A car running parallel to
		// the start/finish line on the approach must not trip it.
		const FVector Behind(GateAt.X - 50.0, 0.0, 0.0);
		const FRacingGateCrossingResult Tangential = Set.EvaluateCrossing(
			GateIndex, Behind - FVector(0.0, 500.0, 0.0), Behind + FVector(0.0, 500.0, 0.0));
		TestTrue(TEXT("Tangential motion was evaluated"), Tangential.bEvaluated);
		TestEqual(TEXT("Motion parallel to the plane, behind it, is None"),
			Tangential.Crossing, ERacingGateCrossing::None);
		TestFalse(TEXT("...and is not legal, because it is not a crossing"), Tangential.bMatchesLegalDirection);
		TestNearlyEqual(TEXT("...and reports zero crossing alpha"), Tangential.CrossingAlpha, 0.0, 0.0);

		// The same, ahead of the plane.
		const FVector Ahead(GateAt.X + 50.0, 0.0, 0.0);
		TestEqual(TEXT("Motion parallel to the plane, ahead of it, is None"),
			Set.EvaluateCrossing(GateIndex, Ahead - FVector(0.0, 500.0, 0.0), Ahead + FVector(0.0, 500.0, 0.0)).Crossing,
			ERacingGateCrossing::None);

		// EXACTLY IN THE PLANE. Driving along the start/finish line is the case a
		// symmetric `<=` sign rule gets wrong: it would report a crossing every tick for a
		// car that never left the line.
		TestEqual(TEXT("Motion entirely WITHIN the plane is None"),
			Set.EvaluateCrossing(GateIndex, GateAt - FVector(0.0, 500.0, 0.0), GateAt + FVector(0.0, 500.0, 0.0)).Crossing,
			ERacingGateCrossing::None);

		// Grazing the edge of the extent. Inclusive at exactly the half-width, because a
		// gate's stated width should be usable rather than notionally one epsilon short.
		const FVector EdgeOffset(0.0, GateSpecHalfWidthCm, 0.0);
		const FRacingGateCrossingResult Edge = Set.EvaluateCrossing(
			GateIndex, GateAt + EdgeOffset - FVector(500.0, 0.0, 0.0), GateAt + EdgeOffset + FVector(500.0, 0.0, 0.0));
		TestEqual(TEXT("Crossing exactly at the half-width is inside the gate"),
			Edge.Crossing, ERacingGateCrossing::Forward);
		TestNearlyEqual(TEXT("...at the stated lateral offset"), Edge.CrossingLateralOffsetCm, GateSpecHalfWidthCm, 1.0e-9);

		// One centimetre wider, and it is a plane crossing OUTSIDE the gate -- which is
		// NOT the same as no crossing, and is the signature of a shortcut.
		const FVector PastEdge(0.0, GateSpecHalfWidthCm + 1.0, 0.0);
		const FRacingGateCrossingResult Outside = Set.EvaluateCrossing(
			GateIndex, GateAt + PastEdge - FVector(500.0, 0.0, 0.0), GateAt + PastEdge + FVector(500.0, 0.0, 0.0));
		TestEqual(TEXT("Crossing 1 cm outside the half-width is OutsideExtent"),
			Outside.Crossing, ERacingGateCrossing::OutsideExtent);
		TestTrue(TEXT("...which still counts as crossing the plane"), Outside.DidCrossPlane());
		TestFalse(TEXT("...but not as going through the gate"), Outside.IsThroughGate());
		TestFalse(TEXT("...and can never be legal"), Outside.bMatchesLegalDirection);

		// Over the top. A finite half-height is what makes a launched car distinguishable
		// from a clean crossing; an infinite one would silently count it.
		const FVector Over(0.0, 0.0, GateSpecHalfHeightCm + 1.0);
		TestEqual(TEXT("Crossing above the half-height is OutsideExtent"),
			Set.EvaluateCrossing(GateIndex, GateAt + Over - FVector(500.0, 0.0, 0.0), GateAt + Over + FVector(500.0, 0.0, 0.0)).Crossing,
			ERacingGateCrossing::OutsideExtent);
	}

	// -- High-speed single-tick crossing --------------------------------------
	//
	// A trigger-volume gate is defeated by speed; a segment/plane test is not. 300 km/h
	// is 8333 cm/s, i.e. 139 cm per 60 Hz tick, and a 400 ms hitch is over 3300 cm. The
	// sweep below goes far past anything a car can do, up to a full teleport.
	{
		const double StepsCm[] = { 139.0, 833.0, 3333.0, 20000.0, 100000.0, 1.0e6 };
		for (const double StepCm : StepsCm)
		{
			// Placed so the plane is crossed at a known fraction rather than always at
			// the midpoint -- a midpoint-only test would pass on an implementation that
			// ignored the endpoints entirely.
			const FVector From = GateAt - FVector(StepCm * 0.25, 0.0, 0.0);
			const FVector To = GateAt + FVector(StepCm * 0.75, 0.0, 0.0);

			const FRacingGateCrossingResult Result = Set.EvaluateCrossing(GateIndex, From, To);
			if (Result.Crossing != ERacingGateCrossing::Forward)
			{
				AddError(FString::Printf(TEXT("A %f cm single-step motion failed to register a forward crossing"), StepCm));
				break;
			}
			if (!FMath::IsNearlyEqual(Result.CrossingAlpha, 0.25, 1.0e-9))
			{
				AddError(FString::Printf(TEXT("A %f cm step crossed at alpha %f, expected 0.25"), StepCm, Result.CrossingAlpha));
				break;
			}
			if (!Result.CrossingPoint.Equals(GateAt, 1.0e-3))
			{
				AddError(FString::Printf(TEXT("A %f cm step put the crossing point at %s"), StepCm, *Result.CrossingPoint.ToString()));
				break;
			}
		}
	}

	// -- Double overlap: one motion, several gates ---------------------------
	//
	// A hitch or a teleport can cross more than one gate in a single evaluation.
	// Reporting only the first would manufacture a skipped-gate bug in the reporting
	// layer, which RACE-002 would then blame on the driver.
	{
		const FVector From(-500.0, 0.0, 0.0);
		const FVector To(50000.0, 0.0, 0.0);

		int32 Visited = 0;
		int32 ForwardCount = 0;
		TArray<int32> VisitedIndices;
		const int32 Crossed = Set.EvaluateCrossings(From, To,
			[&Visited, &ForwardCount, &VisitedIndices](const FRacingGateCrossingResult& Result)
			{
				++Visited;
				VisitedIndices.Add(Result.GateIndex);
				if (Result.Crossing == ERacingGateCrossing::Forward)
				{
					++ForwardCount;
				}
			});

		TestEqual(TEXT("One motion crossed all three gates"), Crossed, 3);
		TestEqual(TEXT("...and the visitor saw all three"), Visited, 3);
		TestEqual(TEXT("...all forward"), ForwardCount, 3);
		TestTrue(TEXT("...reported in gate order"),
			VisitedIndices.Num() == 3 && VisitedIndices[0] == 0 && VisitedIndices[1] == 1 && VisitedIndices[2] == 2);

		// FindFirstCrossing orders by WHERE ALONG THE MOTION each plane was met, which is
		// the order they physically happened in. Gate index order would agree here by
		// luck; the reverse sweep below is the case that separates them.
		FRacingGateCrossingResult First;
		TestEqual(TEXT("The earliest crossing is gate 0"), Set.FindFirstCrossing(From, To, First), 0);
		TestEqual(TEXT("...reported as Forward"), First.Crossing, ERacingGateCrossing::Forward);

		FRacingGateCrossingResult FirstBackwards;
		TestEqual(TEXT("Driving the other way, the earliest crossing is gate 2, not gate 0"),
			Set.FindFirstCrossing(To, From, FirstBackwards), 2);
		TestEqual(TEXT("...reported as Reverse"), FirstBackwards.Crossing, ERacingGateCrossing::Reverse);
		TestFalse(TEXT("...and illegal"), FirstBackwards.bMatchesLegalDirection);

		// Nothing crossed at all.
		FRacingGateCrossingResult NoneFound;
		TestEqual(TEXT("A motion that crosses nothing returns INDEX_NONE"),
			Set.FindFirstCrossing(FVector(5000.0, 0.0, 0.0), FVector(6000.0, 0.0, 0.0), NoneFound), INDEX_NONE);
		TestFalse(TEXT("...with an unevaluated result"), NoneFound.bEvaluated);
		TestEqual(TEXT("...and zero crossings from the visitor form"),
			Set.EvaluateCrossings(FVector(5000.0, 0.0, 0.0), FVector(6000.0, 0.0, 0.0),
				[](const FRacingGateCrossingResult&) {}),
			0);
	}

	// -- Spin / oscillation at a gate -----------------------------------------
	//
	// `.claude/rules/race-tests.md` requires that spinning across a gate cannot
	// double-trigger. This is where that is decided, and it is decided by the ASYMMETRIC
	// sign rule: zero belongs to the ahead side, so the sequence strictly alternates and
	// the net is exactly one forward crossing however long the spin lasts.
	{
		// A car crossing, spinning back, crossing again, twice over, then settling ahead.
		const double PositionsCm[] = { -300.0, 200.0, -100.0, 400.0, -50.0, 600.0, 900.0 };
		constexpr int32 PositionCount = UE_ARRAY_COUNT(PositionsCm);

		int32 ForwardCount = 0;
		int32 ReverseCount = 0;
		ERacingGateCrossing Previous = ERacingGateCrossing::None;

		for (int32 Step = 1; Step < PositionCount; ++Step)
		{
			const FVector From = GateAt + FVector(PositionsCm[Step - 1], 0.0, 0.0);
			const FVector To = GateAt + FVector(PositionsCm[Step], 0.0, 0.0);
			const FRacingGateCrossingResult Result = Set.EvaluateCrossing(GateIndex, From, To);

			if (!Result.IsThroughGate())
			{
				continue;
			}

			if (Result.Crossing == ERacingGateCrossing::Forward)
			{
				++ForwardCount;
			}
			else
			{
				++ReverseCount;
			}

			// No two consecutive crossings may be the same direction. A repeat would mean
			// a crossing was counted twice without the car ever going back, which is the
			// double-trigger the rule forbids.
			if (Result.Crossing == Previous)
			{
				AddError(FString::Printf(
					TEXT("Two consecutive crossings in the same direction at step %d -- the gate double-triggered"), Step));
				break;
			}
			Previous = Result.Crossing;
		}

		TestEqual(TEXT("A triple spin registers three forward crossings"), ForwardCount, 3);
		TestEqual(TEXT("...and two reverse ones"), ReverseCount, 2);
		TestEqual(TEXT("...netting exactly one forward pass"), ForwardCount - ReverseCount, 1);

		// A car STOPPED exactly on the plane cannot re-trigger by standing still. This is
		// the case the asymmetric rule exists for: a stalled car nudged onto the line
		// would otherwise register a crossing on every tick forever.
		for (int32 Tick = 0; Tick < 10; ++Tick)
		{
			const FRacingGateCrossingResult Idle = Set.EvaluateCrossing(GateIndex, GateAt, GateAt);
			if (Idle.Crossing != ERacingGateCrossing::None)
			{
				AddError(TEXT("A car resting exactly on the gate plane re-triggered it"));
				break;
			}
		}

		// Approaching to exactly the plane counts as forward (zero is the ahead side);
		// leaving it forwards afterwards must NOT count a second time.
		TestEqual(TEXT("Arriving exactly on the plane is a forward crossing"),
			Set.EvaluateCrossing(GateIndex, GateAt - FVector(100.0, 0.0, 0.0), GateAt).Crossing,
			ERacingGateCrossing::Forward);
		TestEqual(TEXT("...and continuing forwards from there does NOT cross again"),
			Set.EvaluateCrossing(GateIndex, GateAt, GateAt + FVector(100.0, 0.0, 0.0)).Crossing,
			ERacingGateCrossing::None);
	}

	// -- Garbage in, "I could not look" out -----------------------------------
	//
	// bEvaluated == false is not the same as Crossing == None. A NaN position after a
	// physics blow-up must not be reported as a clean tick with nothing happening.
	{
		const FVector NotANumber(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0);
		const FRacingGateCrossingResult NaNFrom = Set.EvaluateCrossing(GateIndex, NotANumber, GateAt);
		TestFalse(TEXT("A NaN start position is not evaluated"), NaNFrom.bEvaluated);
		TestEqual(TEXT("...and reports no crossing"), NaNFrom.Crossing, ERacingGateCrossing::None);
		TestEqual(TEXT("...with no gate index"), NaNFrom.GateIndex, INDEX_NONE);

		const FVector Infinite(0.0, std::numeric_limits<double>::infinity(), 0.0);
		TestFalse(TEXT("An infinite end position is not evaluated"),
			Set.EvaluateCrossing(GateIndex, GateAt, Infinite).bEvaluated);

		TestFalse(TEXT("An out-of-range gate index is not evaluated"),
			Set.EvaluateCrossing(99, GateAt - FVector(500.0, 0.0, 0.0), GateAt + FVector(500.0, 0.0, 0.0)).bEvaluated);
		TestFalse(TEXT("A negative gate index is not evaluated"),
			Set.EvaluateCrossing(-1, GateAt - FVector(500.0, 0.0, 0.0), GateAt + FVector(500.0, 0.0, 0.0)).bEvaluated);

		const FRacingCheckpointGateSet Unbuilt;
		TestFalse(TEXT("An unbuilt gate set evaluates nothing"),
			Unbuilt.EvaluateCrossing(0, GateAt - FVector(500.0, 0.0, 0.0), GateAt + FVector(500.0, 0.0, 0.0)).bEvaluated);
		TestEqual(TEXT("...and crosses nothing"),
			Unbuilt.EvaluateCrossings(GateAt - FVector(500.0, 0.0, 0.0), GateAt + FVector(500.0, 0.0, 0.0),
				[](const FRacingGateCrossingResult&) {}),
			0);
	}

	return true;
}

// ===========================================================================
// Gates on a curve: the geometry comes from the centerline, not from an author
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingGateCurvedTrackTest,
	"RacingSim.Race.GateCurvedTrack",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingGateCurvedTrackTest::RunTest(const FString& Parameters)
{
	using namespace TrackGateSpecPrivate;

	FTrackCenterline Circle;
	FString Error;
	if (!BuildCircle(Circle, Error))
	{
		AddError(FString::Printf(TEXT("Circle failed to build: %s"), *Error));
		return false;
	}

	const double LengthCm = Circle.GetLengthCm();

	TArray<FRacingCheckpointGateSpec> Specs;
	Specs.Add(MakeSpec(TEXT("Gate.StartFinish"), 0.0));
	Specs.Add(MakeSpec(TEXT("Gate.01"), LengthCm * 0.25));
	Specs.Add(MakeSpec(TEXT("Gate.02"), LengthCm * 0.5));
	Specs.Add(MakeSpec(TEXT("Gate.03"), LengthCm * 0.75));

	FRacingCheckpointGateSet Set;
	FString BuildError;
	if (!Set.Build(Specs, Circle, GateSpecCircleRadiusCm, BuildError))
	{
		AddError(FString::Printf(TEXT("Gate set failed to build on a circle: %s"), *BuildError));
		return false;
	}

	// -- Every gate faces the way the track goes, and stands across it -------
	//
	// This is the whole reason gate geometry is DERIVED rather than authored: N
	// hand-placed transforms are N chances to leave one facing backwards, and a graybox
	// would not show it. Checked against the centerline's own answers, which are
	// independently verified against closed-form circle geometry in
	// RacingSim.Race.CenterlineQueries.
	for (int32 Index = 0; Index < Set.NumGates(); ++Index)
	{
		const FRacingCheckpointGate* Gate = Set.GetGate(Index);
		if (!Gate)
		{
			AddError(FString::Printf(TEXT("Gate %d is missing"), Index));
			break;
		}

		const FVector ExpectedLocation = Circle.GetLocationAtDistanceCm(Gate->DistanceAlongCm);
		const FVector ExpectedForward = Circle.GetForwardAtDistanceCm(Gate->DistanceAlongCm);

		if (!Gate->Location.Equals(ExpectedLocation, 1.0e-6))
		{
			AddError(FString::Printf(TEXT("Gate %d is not on the centerline at its own distance"), Index));
			break;
		}
		if (!Gate->PlaneNormal.Equals(ExpectedForward, 1.0e-9))
		{
			AddError(FString::Printf(TEXT("Gate %d does not face the direction of travel"), Index));
			break;
		}

		// On a flat circle the gate's up axis is world up and its right axis points at
		// the centre (travel is counter-clockwise, so the inside of the circle is to the
		// driver's right -- the same convention FTrackCenterlineQuery uses).
		if (FMath::Abs(Gate->UpAxis.Z - 1.0) > 1.0e-6)
		{
			AddError(FString::Printf(TEXT("Gate %d up axis is not world up on a flat track"), Index));
			break;
		}
		if (FVector::DotProduct(Gate->RightAxis, -Gate->Location.GetSafeNormal2D()) < 0.999)
		{
			AddError(FString::Printf(TEXT("Gate %d right axis does not point at the inside of the circle"), Index));
			break;
		}
	}

	// -- The far side of the loop is not a gate crossing ---------------------
	//
	// FOUND BY RUNNING, NOT BY INSPECTION, and it is the sharpest thing this suite
	// caught. A gate's plane is INFINITE and a circuit is a LOOP, so every gate's plane
	// is met a second time on the far side. The first implementation had no bound on
	// that, and a single clean four-gate lap reported NINE plane crossings -- the five
	// extra being far-side hits up to 200 m off centre, reported as OutsideExtent.
	// Harmless to lap validity, since only IsThroughGate() authorises anything, but it
	// would have handed RACE-002 a stream of phantom near-miss events on gates the car
	// was nowhere near. FRacingCheckpointGate::RelevanceRadiusCm now bounds it.
	{
		const FRacingCheckpointGate* StartFinish = Set.GetGate(0);
		TestNotNull(TEXT("Start/finish gate exists"), StartFinish);
		if (StartFinish)
		{
			TestTrue(TEXT("The relevance radius is derived and positive"), StartFinish->RelevanceRadiusCm > 0.0);
			TestTrue(TEXT("...and far wider than the gate itself, so runoff still counts as a near-miss"),
				StartFinish->RelevanceRadiusCm > StartFinish->HalfWidthCm * 3.0);
			TestTrue(TEXT("...and far narrower than the circuit, so the far side cannot masquerade as one"),
				StartFinish->RelevanceRadiusCm < GateSpecCircleRadiusCm * 2.0);

			// Gate 0 sits at (R, 0, 0) with its plane normal along +Y. That same plane
			// passes through (-R, 0, 0) on the opposite side of the circle. A car crossing
			// it THERE has crossed the plane, and must be told it crossed nothing.
			const FVector FarSide(-GateSpecCircleRadiusCm, 0.0, 0.0);
			const FRacingGateCrossingResult FarResult = Set.EvaluateCrossing(
				0, FarSide - FVector(0.0, 500.0, 0.0), FarSide + FVector(0.0, 500.0, 0.0));

			TestTrue(TEXT("The far-side motion was evaluated"), FarResult.bEvaluated);
			TestEqual(TEXT("Crossing the same plane on the far side of the loop is None"),
				FarResult.Crossing, ERacingGateCrossing::None);
			TestFalse(TEXT("...and does not count as a plane crossing at all"), FarResult.DidCrossPlane());
			TestNearlyEqual(TEXT("...and reports no crossing alpha, so None always means alpha 0"),
				FarResult.CrossingAlpha, 0.0, 0.0);

			// The control that keeps the above honest: the SAME motion at the gate itself
			// is a clean forward crossing, so the None above is the radius doing its job
			// and not the plane test being broken.
			const FVector AtGate(GateSpecCircleRadiusCm, 0.0, 0.0);
			TestEqual(TEXT("The same motion at the gate itself IS a forward crossing"),
				Set.EvaluateCrossing(0, AtGate - FVector(0.0, 500.0, 0.0), AtGate + FVector(0.0, 500.0, 0.0)).Crossing,
				ERacingGateCrossing::Forward);

			// And just outside the rectangle, but well inside the relevance radius, is
			// still the interesting near-miss case rather than silence.
			const FVector Wide(GateSpecCircleRadiusCm, 0.0, 0.0);
			const FVector Offset = StartFinish->RightAxis * (StartFinish->HalfWidthCm + 200.0);
			TestEqual(TEXT("Going round the gate but near it is OutsideExtent, not None"),
				Set.EvaluateCrossing(0, Wide + Offset - FVector(0.0, 500.0, 0.0), Wide + Offset + FVector(0.0, 500.0, 0.0)).Crossing,
				ERacingGateCrossing::OutsideExtent);
		}
	}

	// -- Driving the circuit crosses every gate, forwards, in order ----------
	//
	// The end-to-end shape of the contract, swept rather than sampled at hand-picked
	// points: a car following the centerline must meet the gates in ascending order,
	// each exactly once per lap, each forwards and legally, each within the gate.
	{
		constexpr int32 StepsPerLap = 400;
		TArray<int32> Sequence;
		TArray<double> Laterals;

		// EXACTLY one lap, and the offset is load-bearing rather than cosmetic.
		//
		// Starting the sweep AT the line and ending it AT the line covers slightly more
		// than a lap, and gate 0 is then crossed twice -- once leaving and once
		// arriving. The first version of this test did that and reported five crossings
		// for four gates, which looked exactly like a duplicate-trigger defect and was
		// not one. Starting an eighth of a step past the line means the lap begins and
		// ends between gate 0 and gate 1, so every gate is met once and gate 0 is met
		// last.
		const double StartOffsetCm = LengthCm / (StepsPerLap * 2);

		FVector Previous = Circle.GetLocationAtDistanceCm(StartOffsetCm);

		for (int32 Step = 1; Step <= StepsPerLap; ++Step)
		{
			const FVector Current = Circle.GetLocationAtDistanceCm(
				StartOffsetCm + LengthCm * static_cast<double>(Step) / static_cast<double>(StepsPerLap));

			Set.EvaluateCrossings(Previous, Current,
				[&Sequence, &Laterals](const FRacingGateCrossingResult& Result)
				{
					Sequence.Add(Result.Crossing == ERacingGateCrossing::Forward ? Result.GateIndex : -1 - Result.GateIndex);
					Laterals.Add(Result.CrossingLateralOffsetCm);
				});

			Previous = Current;
		}

		// A lap crosses each of the four gates exactly once, all forward. The lap starts
		// just past gate 0, so the order is 1, 2, 3, 0 -- gate 0 is met last, on the way
		// back to the line. Sequence entries are the gate index for a forward crossing
		// and -1-index for a reverse one, so a stray reverse shows up as a negative
		// rather than being silently counted as a pass.
		TestEqual(TEXT("A lap on the centerline crosses four gates"), Sequence.Num(), 4);
		if (Sequence.Num() == 4)
		{
			TestTrue(TEXT("...each exactly once, in route order, all forward"),
				Sequence[0] == 1 && Sequence[1] == 2 && Sequence[2] == 3 && Sequence[3] == 0);
		}

		// ...and each crossing is essentially dead centre. The residual is the polyline
		// bias plus the chord of one 400th of a lap, both far inside a gate half-width;
		// bounded here rather than ignored, because a systematic lateral drift in gate
		// geometry would show up exactly as this number growing.
		for (int32 Index = 0; Index < Laterals.Num(); ++Index)
		{
			if (FMath::Abs(Laterals[Index]) > 10.0)
			{
				AddError(FString::Printf(
					TEXT("Crossing %d on the centerline was %f cm off centre; gate geometry has drifted"),
					Index, Laterals[Index]));
				break;
			}
		}
	}

	// -- Driving the circuit BACKWARDS crosses every gate in reverse ---------
	{
		constexpr int32 StepsPerLap = 400;
		int32 ReverseCount = 0;
		int32 LegalCount = 0;

		FVector Previous = Circle.GetLocationAtDistanceCm(LengthCm / StepsPerLap);

		for (int32 Step = 0; Step <= StepsPerLap; ++Step)
		{
			const FVector Current = Circle.GetLocationAtDistanceCm(
				-LengthCm * static_cast<double>(Step) / static_cast<double>(StepsPerLap));

			Set.EvaluateCrossings(Previous, Current,
				[&ReverseCount, &LegalCount](const FRacingGateCrossingResult& Result)
				{
					if (Result.Crossing == ERacingGateCrossing::Reverse)
					{
						++ReverseCount;
					}
					if (Result.bMatchesLegalDirection)
					{
						++LegalCount;
					}
				});

			Previous = Current;
		}

		TestEqual(TEXT("A backwards lap crosses all four gates in reverse"), ReverseCount, 4);
		TestEqual(TEXT("...and not one of them is legal"), LegalCount, 0);
	}

	return true;
}
