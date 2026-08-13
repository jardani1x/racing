// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * CORE-002 units policy.
 *
 * RULE: every stored, simulated and replicated value in this project is in
 * **Unreal units**, and one Unreal unit is one **centimetre**. Nothing in
 * Core/, Vehicle/, Race/ or Streaming/ stores metres, kilometres per hour, or
 * miles per hour. SI and display units exist only at two boundaries:
 *
 *   1. presentation (UI/, and Streaming/ messages aimed at the browser);
 *   2. authored data expressed in SI for human sanity, converted on load.
 *
 * Reason: mixing unit systems in stored state is the classic source of silent
 * 100x errors, and Unreal's own APIs (FVector, physics, splines, distances
 * along a spline) are all centimetre-based. Converting on the way in and out is
 * cheap; converting in the middle is a bug generator.
 *
 * CONVENTION: any variable NOT in Unreal units carries the unit in its name --
 * `SpeedKph`, `LengthMetres`, `LapDurationSeconds`. Any variable in Unreal
 * units carries `Cm` or `Cms` (centimetres per second) or no suffix where the
 * type makes it obvious (FVector locations). Every call site below is a
 * documented unit boundary.
 *
 * TIME is the exception and is stored in **seconds as double**, not in Unreal
 * units (Unreal has none for time). Durations are stored; formatting to
 * mm:ss.mmm happens in UI only.
 *
 * All functions here are constexpr and header-only so they cost nothing and can
 * be used in static_asserts. Covered by RacingSim.Core.Units.
 */
namespace RacingSim::Units
{
	/** Unreal's world scale: 1 uu == 1 cm. This is the single assumption everything else rests on. */
	inline constexpr double CentimetresPerMetre = 100.0;
	inline constexpr double MetresPerCentimetre = 1.0 / CentimetresPerMetre;

	/** 1 m/s == 3.6 km/h exactly. */
	inline constexpr double KilometresPerHourPerMetrePerSecond = 3.6;

	/** 1 m == 1/0.3048 ft; 1 mile == 1609.344 m exactly (international mile). */
	inline constexpr double MetresPerMile = 1609.344;
	inline constexpr double MilesPerHourPerMetrePerSecond = 3600.0 / MetresPerMile;

	/** Standard gravity, CODATA/ISO 80000-3: 9.80665 m/s^2 exactly, i.e. 980.665 cm/s^2. */
	inline constexpr double StandardGravityMetresPerSecondSquared = 9.80665;
	inline constexpr double StandardGravityCentimetresPerSecondSquared =
		StandardGravityMetresPerSecondSquared * CentimetresPerMetre;

	inline constexpr double SecondsPerMinute = 60.0;
	inline constexpr double MillisecondsPerSecond = 1000.0;

	// -- Distance -----------------------------------------------------------
	// Boundary: Unreal centimetres <-> SI metres.

	/** cm (Unreal) -> m (SI). */
	inline constexpr double CmToMetres(const double Centimetres)
	{
		return Centimetres * MetresPerCentimetre;
	}

	/** m (SI) -> cm (Unreal). Use on authored data at load time, never per tick. */
	inline constexpr double MetresToCm(const double Metres)
	{
		return Metres * CentimetresPerMetre;
	}

	/** cm (Unreal) -> km (display). Track length and odometers only. */
	inline constexpr double CmToKilometres(const double Centimetres)
	{
		return CmToMetres(Centimetres) * 0.001;
	}

	// -- Speed --------------------------------------------------------------
	// Boundary: Unreal cm/s <-> SI m/s <-> display km/h or mph.
	// Chaos Vehicles reports velocity in cm/s; so does FVector Velocity on any
	// Unreal component. Treat everything arriving from the engine as cm/s.

	/** cm/s (Unreal) -> m/s (SI). */
	inline constexpr double CmsToMetresPerSecond(const double CentimetresPerSecond)
	{
		return CentimetresPerSecond * MetresPerCentimetre;
	}

	/** m/s (SI) -> cm/s (Unreal). */
	inline constexpr double MetresPerSecondToCms(const double MetresPerSecond)
	{
		return MetresPerSecond * CentimetresPerMetre;
	}

	/** cm/s (Unreal) -> km/h (display). 1000 cm/s == 36 km/h. */
	inline constexpr double CmsToKilometresPerHour(const double CentimetresPerSecond)
	{
		return CmsToMetresPerSecond(CentimetresPerSecond) * KilometresPerHourPerMetrePerSecond;
	}

	/** km/h (display or authored) -> cm/s (Unreal). */
	inline constexpr double KilometresPerHourToCms(const double KilometresPerHour)
	{
		return MetresPerSecondToCms(KilometresPerHour / KilometresPerHourPerMetrePerSecond);
	}

	/** cm/s (Unreal) -> mph (display). */
	inline constexpr double CmsToMilesPerHour(const double CentimetresPerSecond)
	{
		return CmsToMetresPerSecond(CentimetresPerSecond) * MilesPerHourPerMetrePerSecond;
	}

	/** mph (display or authored) -> cm/s (Unreal). */
	inline constexpr double MilesPerHourToCms(const double MilesPerHour)
	{
		return MetresPerSecondToCms(MilesPerHour / MilesPerHourPerMetrePerSecond);
	}

	// -- Acceleration -------------------------------------------------------

	/** cm/s^2 (Unreal) -> g. Used by telemetry/HUD only; never store g. */
	inline constexpr double CmsSquaredToG(const double CentimetresPerSecondSquared)
	{
		return CentimetresPerSecondSquared / StandardGravityCentimetresPerSecondSquared;
	}

	/** g -> cm/s^2 (Unreal). */
	inline constexpr double GToCmsSquared(const double G)
	{
		return G * StandardGravityCentimetresPerSecondSquared;
	}

	// -- Compile-time guards ------------------------------------------------
	// These fail the build, not a test, if someone "simplifies" a constant.
	// Deliberately restricted to relations that are exact in IEEE-754 binary64
	// (powers of ten scaling by x100, and literal identity). Ratios that involve
	// an inexact literal such as 3.6 are asserted in RacingSim.Core.Units with an
	// explicit tolerance instead -- a static_assert on those would be asserting
	// the compiler's constant folding, not the project's policy.
	static_assert(CentimetresPerMetre == 100.0, "Unreal world scale is 1 uu == 1 cm; the project depends on it.");
	static_assert(MetresToCm(1.0) == 100.0, "m -> cm must be x100.");
	static_assert(CmToMetres(100.0) == 1.0, "cm -> m must be /100.");
	static_assert(MetresPerMile == 1609.344, "International mile is exactly 1609.344 m.");
}
