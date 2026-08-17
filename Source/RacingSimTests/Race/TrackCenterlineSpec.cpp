// Copyright RacingSim. All Rights Reserved.

#include "Race/TrackCenterline.h"
#include "Core/RacingSimUnits.h"
#include "Misc/AutomationTest.h"

#include <limits>

/**
 * TRACK-001: FTrackCenterline.
 *
 * WHY THESE CASES AND NOT OTHERS. The centerline supplies continuous progress for
 * ranking (Docs/03-TrackRaceUI.md rule 6/7) and the poses that reset uses (rule 8).
 * It is explicitly NOT allowed to authorise a lap, so none of these tests assert
 * anything about laps -- that is TRACK-002/RACE-002 and asserting it here would
 * quietly legitimise the wrong dependency.
 *
 * What can actually go wrong, in the order these tests are written:
 *
 *   Build          -- a malformed centerline that silently half-builds. Every
 *                     rejection case is asserted, because the failure mode of a
 *                     bad bake is a plausible-looking track with wrong distances.
 *   DistanceDomain -- wrap and signed-delta arithmetic across the start/finish
 *                     origin. This is the single highest-risk arithmetic in the
 *                     file: a plain subtraction reports a car that has just crossed
 *                     the line as having gone a whole lap BACKWARDS.
 *   Queries        -- round-tripping distance -> location -> distance, which is the
 *                     one property the polyline model guarantees absolutely, plus
 *                     the sign convention for lateral offset.
 *   Ambiguity      -- a global nearest-point search snapping to the wrong one of
 *                     two nearby track sections. This is the bug the hinted
 *                     overload exists to prevent and the reason it is documented as
 *                     the correctness-critical one rather than the fast one.
 *
 * Geometry is a circle wherever possible, because a circle's arc length, tangent
 * and nearest point are all closed-form, so the assertions compare against
 * mathematics rather than against another function in the same file.
 *
 * SmokeFilter is mandatory: Docs/Environment.md records that the project's only
 * automation command is `Automation RunFilter Smoke`, and that a test outside that
 * filter once sat green and unexecuted.
 */

namespace TrackCenterlineSpecPrivate
{
	// Named uniquely rather than placed in an anonymous namespace: unity builds
	// concatenate translation units, and duplicate anonymous symbols across a blob
	// are a redefinition rather than two file-local helpers.

	constexpr double TrackSpecCircleRadiusCm = 10000.0;   // 100 m
	constexpr int32 TrackSpecCircleSamples = 720;         // 0.5 degrees per sample

	double TrackSpecCircleCircumferenceCm()
	{
		return 2.0 * UE_DOUBLE_PI * TrackSpecCircleRadiusCm;
	}

	/**
	 * A closed circular centerline travelled counter-clockwise in the XY plane,
	 * built with EXACT arc-length distances rather than chord sums, so the length
	 * assertions test the code and not the sampling.
	 */
	void MakeCircle(TArray<FVector>& OutLocations, TArray<double>& OutDistancesCm, double& OutTotalLengthCm)
	{
		OutTotalLengthCm = TrackSpecCircleCircumferenceCm();
		const double StepCm = OutTotalLengthCm / static_cast<double>(TrackSpecCircleSamples);

		OutLocations.Reset(TrackSpecCircleSamples);
		OutDistancesCm.Reset(TrackSpecCircleSamples);

		for (int32 Index = 0; Index < TrackSpecCircleSamples; ++Index)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(Index) / static_cast<double>(TrackSpecCircleSamples);
			OutLocations.Add(FVector(
				TrackSpecCircleRadiusCm * FMath::Cos(Angle),
				TrackSpecCircleRadiusCm * FMath::Sin(Angle),
				0.0));
			OutDistancesCm.Add(static_cast<double>(Index) * StepCm);
		}
	}

	bool BuildCircle(FTrackCenterline& OutCenterline, FString& OutError)
	{
		TArray<FVector> Locations;
		TArray<double> Distances;
		double TotalCm = 0.0;
		MakeCircle(Locations, Distances, TotalCm);
		return OutCenterline.Build(Locations, Distances, TotalCm, /*bClosedLoop*/ true, OutError);
	}

	/** Append a straight run of samples, excluding the end point (the next piece supplies it). */
	void AppendStraight(TArray<FVector>& Out, const FVector& From, const FVector& To, const int32 Steps)
	{
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			Out.Add(FMath::Lerp(From, To, static_cast<double>(Step) / static_cast<double>(Steps)));
		}
	}

	/** Append an arc, excluding the end point. Angles in radians, swept from StartAngle to EndAngle. */
	void AppendArc(TArray<FVector>& Out, const FVector& Center, const double RadiusCm,
		const double StartAngle, const double EndAngle, const int32 Steps)
	{
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const double Angle = FMath::Lerp(StartAngle, EndAngle, static_cast<double>(Step) / static_cast<double>(Steps));
			Out.Add(Center + FVector(RadiusCm * FMath::Cos(Angle), RadiusCm * FMath::Sin(Angle), 0.0));
		}
	}
}

// ===========================================================================
// Build
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrackCenterlineBuildTest,
	"RacingSim.Race.CenterlineBuild",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FTrackCenterlineBuildTest::RunTest(const FString& Parameters)
{
	using namespace TrackCenterlineSpecPrivate;

	TArray<FVector> Locations;
	TArray<double> Distances;
	double TotalCm = 0.0;
	MakeCircle(Locations, Distances, TotalCm);

	// -- The happy path -----------------------------------------------------
	{
		FTrackCenterline Centerline;
		FString Error;
		TestTrue(TEXT("A well-formed closed circle builds"), Centerline.Build(Locations, Distances, TotalCm, true, Error));
		TestTrue(TEXT("Built centerline is valid"), Centerline.IsValid());
		TestTrue(TEXT("Built centerline is closed"), Centerline.IsClosedLoop());
		TestEqual(TEXT("Sample count survives the build"), Centerline.NumSamples(), TrackSpecCircleSamples);

		// A closed loop has one segment per sample: the last one wraps back to the
		// origin. Getting this off by one is how a track ends up one sample short of
		// a lap.
		TestEqual(TEXT("Closed loop has one segment per sample"), Centerline.NumSegments(), TrackSpecCircleSamples);
		TestEqual(TEXT("Error string untouched on success"), Error, FString());
	}

	// -- Every rejection ----------------------------------------------------
	// A malformed centerline that half-builds is worse than one that refuses:
	// the symptom is a plausible track with wrong distances.

	auto ExpectRejected = [this](const TCHAR* What, TArrayView<const FVector> InLocations,
		TArrayView<const double> InDistances, double InTotal, bool bClosed)
	{
		FTrackCenterline Centerline;
		FString Error;
		const bool bBuilt = Centerline.Build(InLocations, InDistances, InTotal, bClosed, Error);
		TestFalse(FString::Printf(TEXT("Rejected: %s"), What), bBuilt);
		TestFalse(FString::Printf(TEXT("Left unbuilt after rejection: %s"), What), Centerline.IsValid());
		TestTrue(FString::Printf(TEXT("Gave a reason: %s"), What), !Error.IsEmpty());
	};

	{
		TArray<double> Short = Distances;
		Short.Pop();
		ExpectRejected(TEXT("location/distance count mismatch"), Locations, Short, TotalCm, true);
	}
	{
		const TArray<FVector> TwoPoints({ Locations[0], Locations[1] });
		const TArray<double> TwoDistances({ Distances[0], Distances[1] });
		ExpectRejected(TEXT("closed loop with only two samples"), TwoPoints, TwoDistances, TotalCm, true);
	}
	{
		TArray<double> Offset = Distances;
		Offset[0] = 5.0;
		ExpectRejected(TEXT("first sample distance is not zero"), Locations, Offset, TotalCm, true);
	}
	{
		TArray<double> NonMonotonic = Distances;
		NonMonotonic[10] = NonMonotonic[9];
		ExpectRejected(TEXT("distances not strictly increasing"), Locations, NonMonotonic, TotalCm, true);
	}
	{
		TArray<double> WithNaN = Distances;
		WithNaN[10] = std::numeric_limits<double>::quiet_NaN();
		ExpectRejected(TEXT("non-finite sample distance"), Locations, WithNaN, TotalCm, true);
	}
	{
		TArray<FVector> WithInf = Locations;
		WithInf[10].Y = std::numeric_limits<double>::infinity();
		ExpectRejected(TEXT("non-finite sample location"), WithInf, Distances, TotalCm, true);
	}
	ExpectRejected(TEXT("zero total length"), Locations, Distances, 0.0, true);
	ExpectRejected(TEXT("negative total length"), Locations, Distances, -TotalCm, true);
	ExpectRejected(TEXT("non-finite total length"), Locations, Distances,
		std::numeric_limits<double>::quiet_NaN(), true);
	ExpectRejected(TEXT("closed loop whose total length does not exceed the last sample"),
		Locations, Distances, Distances.Last(), true);

	{
		// The classic closed-loop baking bug: duplicating the origin as the final
		// sample. It produces a zero-length wrap segment with an undefined direction,
		// right on the start/finish line where it does the most damage.
		TArray<FVector> Duplicated = Locations;
		TArray<double> DuplicatedDistances = Distances;
		Duplicated.Add(Locations[0]);
		DuplicatedDistances.Add(TotalCm);
		ExpectRejected(TEXT("closed loop repeating its origin as its last sample"),
			Duplicated, DuplicatedDistances, TotalCm + 1.0, true);
	}

	// -- A rejected build must not clobber a good one -----------------------
	{
		FTrackCenterline Centerline;
		FString Error;
		TestTrue(TEXT("Good build first"), Centerline.Build(Locations, Distances, TotalCm, true, Error));

		TArray<double> Bad = Distances;
		Bad[3] = Bad[2];
		FString SecondError;
		TestFalse(TEXT("Second, malformed build is rejected"), Centerline.Build(Locations, Bad, TotalCm, true, SecondError));
		TestTrue(TEXT("Rejected rebuild left the previous centerline intact"), Centerline.IsValid());
		TestNearlyEqual(TEXT("...and its length unchanged"), Centerline.GetLengthCm(), TotalCm, 1.0e-6);
	}

	// -- BuildFromPolyline derives a chord length ---------------------------
	{
		FTrackCenterline Polyline;
		FString Error;
		TestTrue(TEXT("BuildFromPolyline accepts the circle points"), Polyline.BuildFromPolyline(Locations, true, Error));

		// An inscribed polygon under-reports a circle by 1 - sinc(pi/N); at 720
		// samples that is about 3.2e-6 relative. Asserted as a bound rather than an
		// equality, because the point is that the shortfall is negligible at a sane
		// sample count -- and that it exists at all, which is why
		// ATrackDefinitionActor uses Build() with the spline's own arc length.
		const double Shortfall = TotalCm - Polyline.GetLengthCm();
		TestTrue(TEXT("Chord length under-reports the circle"), Shortfall > 0.0);
		TestTrue(TEXT("...but by less than 0.01%"), Shortfall < TotalCm * 1.0e-4);
	}

	// -- Unbuilt centerline answers safely ----------------------------------
	{
		const FTrackCenterline Empty;
		TestFalse(TEXT("Default-constructed centerline is invalid"), Empty.IsValid());
		TestNearlyEqual(TEXT("Unbuilt length is zero"), Empty.GetLengthCm(), 0.0, 0.0);
		TestEqual(TEXT("Unbuilt segment count is zero"), Empty.NumSegments(), 0);
		TestNearlyEqual(TEXT("Unbuilt spacing is zero"), Empty.GetSampleSpacingCm(), 0.0, 0.0);
		TestEqual(TEXT("Unbuilt location query is the origin"), Empty.GetLocationAtDistanceCm(500.0), FVector::ZeroVector);
		TestFalse(TEXT("Unbuilt nearest-point query is invalid"), Empty.FindNearest(FVector(1.0, 2.0, 3.0)).bValid);
		TestFalse(TEXT("Unbuilt hinted query is invalid"), Empty.FindNearestNear(FVector(1.0, 2.0, 3.0), 0.0, 100.0).bValid);
	}

	return true;
}

// ===========================================================================
// Distance domain: wrapping and signed deltas
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrackCenterlineDistanceDomainTest,
	"RacingSim.Race.CenterlineDistanceDomain",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FTrackCenterlineDistanceDomainTest::RunTest(const FString& Parameters)
{
	using namespace TrackCenterlineSpecPrivate;

	FTrackCenterline Centerline;
	FString Error;
	if (!BuildCircle(Centerline, Error))
	{
		AddError(FString::Printf(TEXT("Circle failed to build: %s"), *Error));
		return false;
	}

	const double LengthCm = Centerline.GetLengthCm();
	constexpr double TolCm = 1.0e-6;

	// -- Length and the SI boundary -----------------------------------------
	TestNearlyEqual(TEXT("Length is the supplied circumference"), LengthCm, TrackSpecCircleCircumferenceCm(), TolCm);
	TestNearlyEqual(TEXT("Length in metres is cm/100"),
		Centerline.GetLengthMetres(), RacingSim::Units::CmToMetres(LengthCm), 1.0e-9);
	TestNearlyEqual(TEXT("Length in kilometres is cm/100000"),
		Centerline.GetLengthKilometres(), RacingSim::Units::CmToKilometres(LengthCm), 1.0e-12);
	TestNearlyEqual(TEXT("100 m radius circle is about 628.3 m"), Centerline.GetLengthMetres(), 628.3185307, 1.0e-4);

	// -- Sample spacing ------------------------------------------------------
	TestNearlyEqual(TEXT("Spacing is length over segment count"),
		Centerline.GetSampleSpacingCm(), LengthCm / static_cast<double>(TrackSpecCircleSamples), TolCm);

	// -- Wrapping ------------------------------------------------------------
	TestNearlyEqual(TEXT("Wrap 0"), Centerline.WrapDistanceCm(0.0), 0.0, TolCm);
	TestNearlyEqual(TEXT("Wrap a lap is the origin, not the length"), Centerline.WrapDistanceCm(LengthCm), 0.0, TolCm);
	TestNearlyEqual(TEXT("Wrap just past a lap"), Centerline.WrapDistanceCm(LengthCm + 500.0), 500.0, 1.0e-4);
	TestNearlyEqual(TEXT("Wrap negative"), Centerline.WrapDistanceCm(-500.0), LengthCm - 500.0, 1.0e-4);
	TestNearlyEqual(TEXT("Wrap forty laps back"), Centerline.WrapDistanceCm(-40.0 * LengthCm + 250.0), 250.0, 1.0e-3);
	TestNearlyEqual(TEXT("Wrap forty laps forward"), Centerline.WrapDistanceCm(40.0 * LengthCm + 250.0), 250.0, 1.0e-3);

	// A tiny negative can round up to exactly the length after the correction; the
	// API promises [0, L), so this must not escape the domain.
	const double TinyNegative = Centerline.WrapDistanceCm(-1.0e-300);
	TestTrue(TEXT("A tiny negative stays inside [0, L)"), TinyNegative >= 0.0 && TinyNegative < LengthCm);

	// Non-finite input must not propagate. It is a bug upstream, but returning NaN
	// would spread it into every transform and ranking downstream.
	TestNearlyEqual(TEXT("NaN distance wraps to the origin"),
		Centerline.WrapDistanceCm(std::numeric_limits<double>::quiet_NaN()), 0.0, 0.0);
	TestNearlyEqual(TEXT("Infinite distance wraps to the origin"),
		Centerline.WrapDistanceCm(std::numeric_limits<double>::infinity()), 0.0, 0.0);

	// Every wrapped value is inside the domain, swept rather than sampled at
	// hand-picked points.
	for (int32 Step = -500; Step <= 500; ++Step)
	{
		const double Raw = static_cast<double>(Step) * LengthCm * 0.017;
		const double Wrapped = Centerline.WrapDistanceCm(Raw);
		if (Wrapped < 0.0 || Wrapped >= LengthCm)
		{
			AddError(FString::Printf(TEXT("WrapDistanceCm(%f) = %f escaped [0, %f)"), Raw, Wrapped, LengthCm));
			break;
		}
	}

	// -- Signed delta across the start/finish origin -------------------------
	// This is the arithmetic that a plain subtraction gets catastrophically wrong.
	TestNearlyEqual(TEXT("Delta forward within a lap"),
		Centerline.GetSignedDistanceDeltaCm(1000.0, 1200.0), 200.0, 1.0e-4);
	TestNearlyEqual(TEXT("Delta backward within a lap"),
		Centerline.GetSignedDistanceDeltaCm(1200.0, 1000.0), -200.0, 1.0e-4);
	TestNearlyEqual(TEXT("Delta forward ACROSS the line is small and positive"),
		Centerline.GetSignedDistanceDeltaCm(LengthCm - 100.0, 100.0), 200.0, 1.0e-4);
	TestNearlyEqual(TEXT("Delta backward ACROSS the line is small and negative"),
		Centerline.GetSignedDistanceDeltaCm(100.0, LengthCm - 100.0), -200.0, 1.0e-4);
	TestNearlyEqual(TEXT("Delta to self is zero"), Centerline.GetSignedDistanceDeltaCm(4321.0, 4321.0), 0.0, TolCm);

	// Exactly half a lap: the tie must resolve the same way regardless of argument
	// order, or this is not a function.
	const double HalfCm = LengthCm * 0.5;
	TestNearlyEqual(TEXT("Half a lap forward is +L/2"),
		Centerline.GetSignedDistanceDeltaCm(0.0, HalfCm), HalfCm, 1.0e-4);
	TestNearlyEqual(TEXT("Half a lap the other way is also +L/2"),
		Centerline.GetSignedDistanceDeltaCm(HalfCm, 0.0), HalfCm, 1.0e-4);

	// Antisymmetry everywhere else.
	for (int32 Step = 1; Step < 200; ++Step)
	{
		const double A = static_cast<double>(Step) * 137.0;
		const double B = static_cast<double>(Step) * 911.0;
		const double Forward = Centerline.GetSignedDistanceDeltaCm(A, B);
		const double Backward = Centerline.GetSignedDistanceDeltaCm(B, A);
		if (!FMath::IsNearlyEqual(FMath::Abs(Forward), FMath::Abs(Backward), 1.0e-6))
		{
			AddError(FString::Printf(TEXT("Delta not antisymmetric at (%f, %f): %f vs %f"), A, B, Forward, Backward));
			break;
		}
		if (FMath::Abs(Forward) > HalfCm + 1.0e-6)
		{
			AddError(FString::Printf(TEXT("Delta (%f) exceeded half a lap (%f)"), Forward, HalfCm));
			break;
		}
	}

	TestNearlyEqual(TEXT("Non-finite delta arguments give zero"),
		Centerline.GetSignedDistanceDeltaCm(std::numeric_limits<double>::quiet_NaN(), 100.0), 0.0, 0.0);

	// -- Open centerlines clamp, they do not wrap ---------------------------
	{
		TArray<FVector> Line({ FVector::ZeroVector, FVector(1000.0, 0.0, 0.0), FVector(2000.0, 0.0, 0.0) });
		FTrackCenterline Open;
		FString OpenError;
		TestTrue(TEXT("Open centerline builds"), Open.BuildFromPolyline(Line, false, OpenError));
		TestFalse(TEXT("Open centerline is not closed"), Open.IsClosedLoop());
		TestEqual(TEXT("Open centerline has one fewer segment than samples"), Open.NumSegments(), 2);
		TestNearlyEqual(TEXT("Open length is the chord sum"), Open.GetLengthCm(), 2000.0, TolCm);
		TestNearlyEqual(TEXT("Past the end clamps to the end"), Open.WrapDistanceCm(9999.0), 2000.0, TolCm);
		TestNearlyEqual(TEXT("Before the start clamps to the start"), Open.WrapDistanceCm(-9999.0), 0.0, TolCm);

		// Querying exactly at the end must land on the final segment, not run off
		// the end of the segment table.
		const FVector End = Open.GetLocationAtDistanceCm(2000.0);
		TestNearlyEqual(TEXT("Open centerline endpoint"), End.X, 2000.0, 1.0e-6);
	}

	return true;
}

// ===========================================================================
// Point queries
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrackCenterlineQueryTest,
	"RacingSim.Race.CenterlineQueries",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FTrackCenterlineQueryTest::RunTest(const FString& Parameters)
{
	using namespace TrackCenterlineSpecPrivate;

	FTrackCenterline Centerline;
	FString Error;
	if (!BuildCircle(Centerline, Error))
	{
		AddError(FString::Printf(TEXT("Circle failed to build: %s"), *Error));
		return false;
	}

	const double LengthCm = Centerline.GetLengthCm();

	// The polyline is inscribed in the circle, so a point sampled between two
	// samples sits R*(1 - cos(pi/N)) inside the true radius. At 720 samples on a
	// 100 m radius that is 0.19 cm. Bound the tolerance by that geometry rather
	// than by trial and error.
	const double MaxSagittaCm = TrackSpecCircleRadiusCm * (1.0 - FMath::Cos(UE_DOUBLE_PI / TrackSpecCircleSamples));

	// -- Location at distance ------------------------------------------------
	{
		// At a sample distance the answer must be the sample itself, exactly.
		const double StepCm = LengthCm / static_cast<double>(TrackSpecCircleSamples);
		const FVector AtOrigin = Centerline.GetLocationAtDistanceCm(0.0);
		TestNearlyEqual(TEXT("Origin is at angle zero, X = R"), AtOrigin.X, TrackSpecCircleRadiusCm, 1.0e-6);
		TestNearlyEqual(TEXT("Origin Y is zero"), AtOrigin.Y, 0.0, 1.0e-6);

		const FVector AtQuarter = Centerline.GetLocationAtDistanceCm(LengthCm * 0.25);
		TestNearlyEqual(TEXT("Quarter lap is at +Y"), AtQuarter.Y, TrackSpecCircleRadiusCm, MaxSagittaCm + 1.0e-6);
		TestNearlyEqual(TEXT("Quarter lap X is zero"), AtQuarter.X, 0.0, MaxSagittaCm + 1.0e-6);

		// A whole lap returns to the origin -- the wrap, checked through the
		// geometry rather than through WrapDistanceCm alone.
		TestTrue(TEXT("A full lap returns to the origin"),
			FVector::Dist(Centerline.GetLocationAtDistanceCm(LengthCm), AtOrigin) < 1.0e-6);

		// Every queried point sits on the circle, to within the sagitta.
		for (int32 Step = 0; Step < 2000; ++Step)
		{
			const double DistanceCm = LengthCm * static_cast<double>(Step) / 2000.0;
			const double Radius = Centerline.GetLocationAtDistanceCm(DistanceCm).Size2D();
			if (Radius > TrackSpecCircleRadiusCm + 1.0e-6 || Radius < TrackSpecCircleRadiusCm - MaxSagittaCm - 1.0e-6)
			{
				AddError(FString::Printf(TEXT("Location at %f cm has radius %f, outside [R - sagitta, R]"), DistanceCm, Radius));
				break;
			}
		}
		(void)StepCm;
	}

	// -- Direction of travel -------------------------------------------------
	{
		// Counter-clockwise: at angle 0 the tangent points along +Y.
		const FVector Forward = Centerline.GetForwardAtDistanceCm(0.0);
		TestNearlyEqual(TEXT("Forward is a unit vector"), Forward.Size(), 1.0, 1.0e-9);
		TestTrue(TEXT("Forward at the origin points along +Y"), Forward.Y > 0.99);

		// The tangent is always perpendicular to the radius on a circle.
		for (int32 Step = 0; Step < 500; ++Step)
		{
			const double DistanceCm = LengthCm * static_cast<double>(Step) / 500.0;
			const FVector Radial = Centerline.GetLocationAtDistanceCm(DistanceCm).GetSafeNormal2D();
			const double Dot = FVector::DotProduct(Radial, Centerline.GetForwardAtDistanceCm(DistanceCm));
			if (FMath::Abs(Dot) > 0.01)
			{
				AddError(FString::Printf(TEXT("Tangent not perpendicular to radius at %f cm (dot %f)"), DistanceCm, Dot));
				break;
			}
		}
	}

	// -- Transform -----------------------------------------------------------
	{
		const FTransform Transform = Centerline.GetTransformAtDistanceCm(0.0);
		TestTrue(TEXT("Transform location matches the location query"),
			FVector::Dist(Transform.GetLocation(), Centerline.GetLocationAtDistanceCm(0.0)) < 1.0e-9);
		TestTrue(TEXT("Transform X axis is the direction of travel"),
			FVector::DotProduct(Transform.GetRotation().GetForwardVector(), Centerline.GetForwardAtDistanceCm(0.0)) > 0.9999);
		TestTrue(TEXT("Transform Z axis is up on a flat track"),
			Transform.GetRotation().GetUpVector().Z > 0.9999);
		TestTrue(TEXT("Transform scale is identity"),
			Transform.GetScale3D().Equals(FVector::OneVector, 1.0e-9));
	}

	// -- Nearest point: the round trip ---------------------------------------
	// The one property this model guarantees absolutely is that FindNearest and
	// GetLocationAtDistanceCm describe the same polyline, so progress is monotonic
	// and never jumps. If this holds, ranking is trustworthy even where the
	// absolute geometry is approximate.
	{
		double WorstErrorCm = 0.0;
		for (int32 Step = 0; Step < 1500; ++Step)
		{
			const double DistanceCm = LengthCm * static_cast<double>(Step) / 1500.0;
			const FVector OnTrack = Centerline.GetLocationAtDistanceCm(DistanceCm);
			const FTrackCenterlineQuery Query = Centerline.FindNearest(OnTrack);
			if (!Query.bValid)
			{
				AddError(FString::Printf(TEXT("Round trip query invalid at %f cm"), DistanceCm));
				break;
			}

			WorstErrorCm = FMath::Max(WorstErrorCm, FMath::Abs(Centerline.GetSignedDistanceDeltaCm(DistanceCm, Query.DistanceAlongCm)));
		}

		TestTrue(FString::Printf(TEXT("distance -> location -> distance round trip is exact (worst %g cm)"), WorstErrorCm),
			WorstErrorCm < 1.0e-4);
	}

	// -- Nearest point: sign convention --------------------------------------
	{
		// Travel is counter-clockwise, so at angle 0 the direction is +Y and the
		// right-hand side (Up x Forward = -X) points INWARD. A point outside the
		// circle is therefore to the driver's LEFT: negative lateral offset.
		const FVector Outside(TrackSpecCircleRadiusCm + 300.0, 0.0, 0.0);
		const FTrackCenterlineQuery OutsideQuery = Centerline.FindNearest(Outside);
		TestTrue(TEXT("Outside the circle is a valid query"), OutsideQuery.bValid);
		TestTrue(TEXT("Outside the circle is to the LEFT of counter-clockwise travel"), OutsideQuery.LateralOffsetCm < 0.0);
		TestNearlyEqual(TEXT("Outside offset magnitude"), FMath::Abs(OutsideQuery.LateralOffsetCm), 300.0, 1.0);
		TestNearlyEqual(TEXT("Outside 3D distance matches"), OutsideQuery.DistanceToCenterlineCm, 300.0, 1.0);
		TestNearlyEqual(TEXT("Outside projects to the origin distance"),
			Centerline.GetSignedDistanceDeltaCm(0.0, OutsideQuery.DistanceAlongCm), 0.0, 1.0);

		const FVector Inside(TrackSpecCircleRadiusCm - 300.0, 0.0, 0.0);
		const FTrackCenterlineQuery InsideQuery = Centerline.FindNearest(Inside);
		TestTrue(TEXT("Inside the circle is to the RIGHT of counter-clockwise travel"), InsideQuery.LateralOffsetCm > 0.0);
	}

	// -- Elevation separates lateral offset from 3D distance -----------------
	{
		// On a circuit with elevation change these are never the same number, and
		// a track-limits rule that confuses them would penalise a car for being
		// airborne over a crest.
		const FVector Airborne(TrackSpecCircleRadiusCm, 0.0, 400.0);
		const FTrackCenterlineQuery Query = Centerline.FindNearest(Airborne);
		TestTrue(TEXT("Airborne query is valid"), Query.bValid);
		TestNearlyEqual(TEXT("Airborne lateral offset is ~zero"), Query.LateralOffsetCm, 0.0, 1.0);
		TestNearlyEqual(TEXT("Airborne 3D distance is the height"), Query.DistanceToCenterlineCm, 400.0, 1.0);
	}

	// -- Nearest point rejects garbage input ---------------------------------
	{
		const FVector NotANumber(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0);
		TestFalse(TEXT("NaN query location is rejected"), Centerline.FindNearest(NotANumber).bValid);

		const FVector Infinite(std::numeric_limits<double>::infinity(), 0.0, 0.0);
		TestFalse(TEXT("Infinite query location is rejected"), Centerline.FindNearest(Infinite).bValid);
	}

	// -- A far-away point still projects somewhere ---------------------------
	{
		// A teleported or reset car can be a long way off the track. The query must
		// still answer, because "off track" is a decision for the caller, not a
		// reason to return nothing.
		const FTrackCenterlineQuery Query = Centerline.FindNearest(FVector(1.0e7, 1.0e7, 1.0e7));
		TestTrue(TEXT("A distant point still projects onto the track"), Query.bValid);
		TestTrue(TEXT("...within the distance domain"),
			Query.DistanceAlongCm >= 0.0 && Query.DistanceAlongCm < LengthCm);
	}

	return true;
}

// ===========================================================================
// Ambiguity: the reason the hinted overload exists
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrackCenterlineAmbiguityTest,
	"RacingSim.Race.CenterlineAmbiguity",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FTrackCenterlineAmbiguityTest::RunTest(const FString& Parameters)
{
	using namespace TrackCenterlineSpecPrivate;

	// A hairpin: two parallel straights 1000 cm apart, joined by semicircles. This
	// is the shape every real circuit has somewhere, and it is the shape that breaks
	// a global nearest-point search.
	//
	//   outbound leg  (0,0) -> (20000,0)      distance    0 .. 20000
	//   right turn    semicircle, centre (20000,500), r=500
	//   return leg    (20000,1000) -> (0,1000)
	//   left turn     semicircle, centre (0,500), r=500, back to (0,0)
	//
	// Original geometry, constructed from two lines and two arcs; it is not traced
	// from any circuit.
	TArray<FVector> Points;
	AppendStraight(Points, FVector(0.0, 0.0, 0.0), FVector(20000.0, 0.0, 0.0), 200);
	AppendArc(Points, FVector(20000.0, 500.0, 0.0), 500.0, -(UE_DOUBLE_PI * 0.5), (UE_DOUBLE_PI * 0.5), 20);
	AppendStraight(Points, FVector(20000.0, 1000.0, 0.0), FVector(0.0, 1000.0, 0.0), 200);
	AppendArc(Points, FVector(0.0, 500.0, 0.0), 500.0, (UE_DOUBLE_PI * 0.5), 3.0 * (UE_DOUBLE_PI * 0.5), 20);

	FTrackCenterline Centerline;
	FString Error;
	if (!Centerline.BuildFromPolyline(Points, true, Error))
	{
		AddError(FString::Printf(TEXT("Hairpin failed to build: %s"), *Error));
		return false;
	}

	const double LengthCm = Centerline.GetLengthCm();

	// A point between the two legs, deliberately CLOSER to the return leg.
	//   distance to outbound leg (Y=0)    = 600 cm
	//   distance to return   leg (Y=1000) = 400 cm
	const FVector Between(10000.0, 600.0, 0.0);

	// -- The global search does the wrong thing, and that is the point -------
	const FTrackCenterlineQuery Global = Centerline.FindNearest(Between);
	TestTrue(TEXT("Global query is valid"), Global.bValid);
	TestNearlyEqual(TEXT("Global search snaps to the geometrically nearer return leg"),
		Global.DistanceToCenterlineCm, 400.0, 5.0);

	// The return leg runs from roughly 20000 + half a turn onwards, i.e. past the
	// halfway point of the lap; the outbound leg is the first 20000 cm.
	TestTrue(TEXT("Global search reports a distance on the return leg"), Global.DistanceAlongCm > 20000.0);

	// -- The hinted search keeps the car on the leg it is actually on --------
	{
		const double HintCm = 10000.0;      // where the car was last seen: outbound leg
		const double WindowCm = 2000.0;     // generous for one tick at any sane speed
		const FTrackCenterlineQuery Hinted = Centerline.FindNearestNear(Between, HintCm, WindowCm);
		TestTrue(TEXT("Hinted query is valid"), Hinted.bValid);
		TestNearlyEqual(TEXT("Hinted search stays on the outbound leg"), Hinted.DistanceAlongCm, 10000.0, 50.0);
		TestNearlyEqual(TEXT("...and reports the correct, larger offset"), Hinted.DistanceToCenterlineCm, 600.0, 5.0);
		TestTrue(TEXT("The two searches genuinely disagree, so this test can fail"),
			FMath::Abs(Hinted.DistanceAlongCm - Global.DistanceAlongCm) > 1000.0);
	}

	// -- The hint works across the start/finish origin -----------------------
	{
		// A car 100 cm before the line, hinted from 100 cm before the line. The
		// window straddles the wrap and must not fall apart there.
		const double NearLineCm = LengthCm - 100.0;
		const FVector OnTrack = Centerline.GetLocationAtDistanceCm(NearLineCm);
		const FTrackCenterlineQuery Hinted = Centerline.FindNearestNear(OnTrack, NearLineCm, 1500.0);
		TestTrue(TEXT("Hinted query across the origin is valid"), Hinted.bValid);
		TestNearlyEqual(TEXT("Hinted query across the origin is accurate"),
			Centerline.GetSignedDistanceDeltaCm(NearLineCm, Hinted.DistanceAlongCm), 0.0, 1.0);
	}

	// -- Unusable hints degrade to the global search, never to failure -------
	// Losing precision is recoverable; reporting "not on the track" is not.
	{
		const FVector OnTrack = Centerline.GetLocationAtDistanceCm(5000.0);

		const FTrackCenterlineQuery ZeroWindow = Centerline.FindNearestNear(OnTrack, 5000.0, 0.0);
		TestTrue(TEXT("Zero window falls back to the global search"), ZeroWindow.bValid);
		TestNearlyEqual(TEXT("...and is still accurate"), ZeroWindow.DistanceAlongCm, 5000.0, 5.0);

		const FTrackCenterlineQuery NegativeWindow = Centerline.FindNearestNear(OnTrack, 5000.0, -100.0);
		TestTrue(TEXT("Negative window falls back"), NegativeWindow.bValid);

		const FTrackCenterlineQuery NaNHint = Centerline.FindNearestNear(
			OnTrack, std::numeric_limits<double>::quiet_NaN(), 1000.0);
		TestTrue(TEXT("NaN hint falls back"), NaNHint.bValid);
		TestNearlyEqual(TEXT("...and is still accurate"), NaNHint.DistanceAlongCm, 5000.0, 5.0);

		const FTrackCenterlineQuery HugeWindow = Centerline.FindNearestNear(OnTrack, 5000.0, LengthCm);
		TestTrue(TEXT("A window wider than the lap falls back"), HugeWindow.bValid);
		TestNearlyEqual(TEXT("...and is still accurate"), HugeWindow.DistanceAlongCm, 5000.0, 5.0);

		// A hint that is out of the distance domain must be wrapped, not rejected.
		const FTrackCenterlineQuery WrappedHint = Centerline.FindNearestNear(OnTrack, 5000.0 + 7.0 * LengthCm, 1500.0);
		TestTrue(TEXT("A multi-lap hint is wrapped"), WrappedHint.bValid);
		TestNearlyEqual(TEXT("...and lands on the right place"), WrappedHint.DistanceAlongCm, 5000.0, 5.0);

		const FTrackCenterlineQuery NegativeHint = Centerline.FindNearestNear(OnTrack, 5000.0 - 3.0 * LengthCm, 1500.0);
		TestTrue(TEXT("A negative hint is wrapped"), NegativeHint.bValid);
		TestNearlyEqual(TEXT("...and lands on the right place"), NegativeHint.DistanceAlongCm, 5000.0, 5.0);
	}

	// -- A hinted sweep all the way round agrees with the truth --------------
	// The window is deliberately narrow, so a wrap or segment-count error anywhere
	// on the lap shows up as a large distance error rather than as a slow query.
	{
		double WorstErrorCm = 0.0;
		double PreviousCm = 0.0;
		for (int32 Step = 0; Step <= 2000; ++Step)
		{
			const double TruthCm = LengthCm * static_cast<double>(Step) / 2000.0;
			const FVector OnTrack = Centerline.GetLocationAtDistanceCm(TruthCm);
			const FTrackCenterlineQuery Query = Centerline.FindNearestNear(OnTrack, PreviousCm, 200.0);
			if (!Query.bValid)
			{
				AddError(FString::Printf(TEXT("Hinted sweep query invalid at %f cm"), TruthCm));
				break;
			}

			WorstErrorCm = FMath::Max(WorstErrorCm, FMath::Abs(Centerline.GetSignedDistanceDeltaCm(TruthCm, Query.DistanceAlongCm)));
			PreviousCm = Query.DistanceAlongCm;
		}

		TestTrue(FString::Printf(TEXT("Hinted sweep tracks the truth all the way round (worst %g cm)"), WorstErrorCm),
			WorstErrorCm < 1.0);
	}

	return true;
}
