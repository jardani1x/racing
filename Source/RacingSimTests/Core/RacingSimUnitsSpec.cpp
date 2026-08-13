// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingSimUnits.h"
#include "Misc/AutomationTest.h"

/**
 * CORE-002: units policy.
 *
 * `.claude/rules/unreal-source.md` requires Unreal-centimetre/SI conversions to be
 * explicit AND tested. The conversions themselves are one-line multiplications, so
 * the value of this test is not that the arithmetic works -- it is that the
 * *constants* are pinned. A wrong constant here is a silent 3.6x or 100x error that
 * shows up as a plausible-looking speedometer and a wrong lap time, and nothing else
 * in the project would catch it.
 *
 * Every case therefore asserts against an independently known physical value, not
 * against another function in the same header.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRacingSimUnitsTest,
	"RacingSim.Core.Units",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::SmokeFilter)

bool FRacingSimUnitsTest::RunTest(const FString& Parameters)
{
	using namespace RacingSim::Units;

	// Tolerance is far tighter than any physically meaningful error but loose
	// enough to survive the inexact binary representation of 3.6 and 1609.344.
	constexpr double Tol = 1.0e-9;

	// -- Distance -----------------------------------------------------------
	// Known value: 1 m == 100 cm, by the definition of Unreal's world scale.
	TestNearlyEqual(TEXT("1 m -> 100 cm"), MetresToCm(1.0), 100.0, 0.0);
	TestNearlyEqual(TEXT("100 cm -> 1 m"), CmToMetres(100.0), 1.0, 0.0);
	TestNearlyEqual(TEXT("1 km -> 100000 cm"), MetresToCm(1000.0), 100000.0, Tol);
	TestNearlyEqual(TEXT("250000 cm -> 2.5 km"), CmToKilometres(250000.0), 2.5, Tol);

	// Round trip on a value with no exact binary representation.
	TestNearlyEqual(TEXT("Distance round trip"), CmToMetres(MetresToCm(37.4)), 37.4, Tol);

	// Zero and negative must pass through untouched: a spline offset behind the
	// start line is legitimately negative, and a conversion that clamps would
	// hide it.
	TestNearlyEqual(TEXT("Zero distance"), CmToMetres(0.0), 0.0, 0.0);
	TestNearlyEqual(TEXT("Negative distance survives"), CmToMetres(-500.0), -5.0, Tol);

	// -- Speed --------------------------------------------------------------
	// Known value: 1 m/s == 3.6 km/h exactly, so 1000 cm/s == 36 km/h.
	TestNearlyEqual(TEXT("1000 cm/s -> 36 km/h"), CmsToKilometresPerHour(1000.0), 36.0, Tol);
	TestNearlyEqual(TEXT("100 cm/s -> 1 m/s"), CmsToMetresPerSecond(100.0), 1.0, Tol);
	TestNearlyEqual(TEXT("1 m/s -> 100 cm/s"), MetresPerSecondToCms(1.0), 100.0, Tol);

	// Known value: 100 km/h is 27.777... m/s, i.e. 2777.77... cm/s.
	TestNearlyEqual(TEXT("100 km/h -> cm/s"), KilometresPerHourToCms(100.0), 100000.0 / 36.0, Tol);
	TestNearlyEqual(TEXT("Speed round trip km/h"), CmsToKilometresPerHour(KilometresPerHourToCms(213.7)), 213.7, Tol);

	// Known value: 1 m/s == 2.2369362920544... mph (international mile).
	TestNearlyEqual(TEXT("100 cm/s -> mph"), CmsToMilesPerHour(100.0), 3600.0 / 1609.344, Tol);
	TestNearlyEqual(TEXT("60 mph -> cm/s"), MilesPerHourToCms(60.0), 2682.24, 1.0e-6);
	TestNearlyEqual(TEXT("Speed round trip mph"), CmsToMilesPerHour(MilesPerHourToCms(88.0)), 88.0, Tol);

	// A reversing car has negative forward speed. The sign must survive, because
	// the HUD and the reverse-crossing rules both depend on it.
	TestNearlyEqual(TEXT("Negative speed survives"), CmsToKilometresPerHour(-1000.0), -36.0, Tol);
	TestNearlyEqual(TEXT("Zero speed"), CmsToKilometresPerHour(0.0), 0.0, 0.0);

	// km/h must exceed mph for the same physical speed. Cheap guard against the
	// two conversions being transposed at a call site -- a mistake that leaves
	// both functions individually correct.
	TestTrue(
		TEXT("km/h reads higher than mph for the same speed"),
		CmsToKilometresPerHour(5000.0) > CmsToMilesPerHour(5000.0));

	// -- Acceleration -------------------------------------------------------
	// Known value: standard gravity is 9.80665 m/s^2 == 980.665 cm/s^2.
	TestNearlyEqual(TEXT("1 g in cm/s^2"), GToCmsSquared(1.0), 980.665, 1.0e-9);
	TestNearlyEqual(TEXT("980.665 cm/s^2 -> 1 g"), CmsSquaredToG(980.665), 1.0, Tol);
	TestNearlyEqual(TEXT("Braking at -1.2 g"), CmsSquaredToG(GToCmsSquared(-1.2)), -1.2, Tol);

	// -- Plausibility of a real lap ----------------------------------------
	// A 4.2 km circuit at an average of 180 km/h must take about 84 s. This is
	// the end-to-end sanity check the individual constants cannot give: it uses
	// distance and speed together, so a mismatched pair fails here even if each
	// conversion is self-consistent.
	{
		const double TrackLengthCm = MetresToCm(4200.0);
		const double AverageSpeedCms = KilometresPerHourToCms(180.0);
		const double LapSeconds = TrackLengthCm / AverageSpeedCms;
		TestNearlyEqual(TEXT("4.2 km at 180 km/h takes 84 s"), LapSeconds, 84.0, 1.0e-6);
	}

	return true;
}
