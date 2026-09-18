// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/RacingSimTypes.h"
#include "RacingHudFormat.generated.h"

/**
 * UI-002: how the HUD turns FRacingHudViewModel numbers into text.
 *
 * One formatter per displayed field, so the native widget, a later WBP restyle and the
 * automation all produce the same string for the same value.
 *
 * ===========================================================================
 * Culture
 * ===========================================================================
 *
 * Every string is built with integer Printf and wrapped with FText::AsCultureInvariant.
 * A lap time is "1:23.456" in every locale; FText::AsNumber would give "1:23,456" under a
 * comma-decimal culture, and a timing screen that changes shape with the OS locale is a
 * bug report. RacingSim.UI.HudFormat checks this under a comma-decimal culture.
 *
 * ===========================================================================
 * Rounding
 * ===========================================================================
 *
 * Times round to the NEAREST millisecond with carry (59.9996 s is "1:00.000"), speeds and
 * RPM to the nearest whole unit with halves rounding up. The widget's change detection
 * uses the same RoundTo* helpers, so "the text would change" and "the number changed"
 * are one test.
 *
 * Non-finite inputs never reach the screen as "nan": they read as the absent form.
 */
UCLASS()
class RACINGSIM_API URacingHudFormatLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Upper display bound for speed, whole units. A physics blow-up reads 9999, not a 12-digit number. */
	static constexpr int32 MaxDisplaySpeed = 9999;

	/** Upper display bound for RPM. */
	static constexpr int32 MaxDisplayRPM = 99999;

	/** Minutes shown in a lap time are clamped to this; 999:59.999 is the longest string. */
	static constexpr int32 MaxDisplayLapMinutes = 999;

	/** Countdown digits are clamped to this, matching URacingHudViewModelLibrary::GetCountdownWholeSeconds. */
	static constexpr int32 MaxDisplayCountdownSeconds = 3600;

	// =======================================================================
	// Rounding shared with the widget's change detection
	// =======================================================================

	/** |Speed| to whole units, halves up, clamped to [0, MaxDisplaySpeed]. -1 for non-finite. */
	static int32 RoundSpeedForDisplay(double Speed);

	/** RPM to whole units, halves up, clamped to [0, MaxDisplayRPM]. -1 for non-finite. */
	static int32 RoundRPMForDisplay(float EngineRPM);

	/**
	 * Non-negative SECONDS to whole milliseconds, nearest, clamped to the lap-time display
	 * range. Negative reads as 0. -1 for non-finite.
	 */
	static int64 RoundLapTimeMilliseconds(double Seconds);

	/** Signed SECONDS to signed whole milliseconds, nearest (half away from zero), clamped to +/-999.999 s. 0 for non-finite. */
	static int64 RoundDeltaMilliseconds(double Seconds);

	// =======================================================================
	// Vehicle
	// =======================================================================

	/**
	 * Speed in whole display units. The magnitude is shown: reversing at 12 km/h reads "12",
	 * and the gear readout says R. "--" when the vehicle data is not fresh or the value is
	 * non-finite.
	 */
	UFUNCTION(BlueprintPure, Category = "Racing|HUD|Format")
	static FText FormatSpeed(double Speed, bool bVehicleDataFresh);

	/** "km/h", "mph" or "m/s". */
	UFUNCTION(BlueprintPure, Category = "Racing|HUD|Format")
	static FText FormatSpeedUnit(ERacingSpeedDisplayUnit Unit);

	/** Whole RPM. "--" when the vehicle data is not fresh or the value is non-finite. */
	UFUNCTION(BlueprintPure, Category = "Racing|HUD|Format")
	static FText FormatRPM(float EngineRPM, bool bVehicleDataFresh);

	/** "R" for any reverse gear, "N" for neutral, digits otherwise. "-" when not fresh. */
	UFUNCTION(BlueprintPure, Category = "Racing|HUD|Format")
	static FText FormatGear(int32 GearIndex, bool bVehicleDataFresh);

	// =======================================================================
	// Timing
	// =======================================================================

	/** "M:SS.mmm", e.g. "1:23.456". "-:--.---" when bHasTime is false or the value is non-finite. */
	UFUNCTION(BlueprintPure, Category = "Racing|HUD|Format")
	static FText FormatLapTime(double Seconds, bool bHasTime);

	/** "+S.mmm" or "-S.mmm"; negative is faster. "+0.000" for zero. Empty when bHasDelta is false. */
	UFUNCTION(BlueprintPure, Category = "Racing|HUD|Format")
	static FText FormatDelta(double DeltaSeconds, bool bHasDelta);

	// =======================================================================
	// Race
	// =======================================================================

	/** Whole seconds as digits, "GO" at 0. Empty when bShowCountdown is false. */
	UFUNCTION(BlueprintPure, Category = "Racing|HUD|Format")
	static FText FormatCountdown(int32 CountdownWholeSeconds, bool bShowCountdown);

	/** "LAP n" for a timed lap (L9 lap number >= 1), "LAP -" before the first crossing. */
	UFUNCTION(BlueprintPure, Category = "Racing|HUD|Format")
	static FText FormatLapCounter(int32 CurrentLapNumber);

	/** "P n/m". Empty when bShowPosition is false. */
	UFUNCTION(BlueprintPure, Category = "Racing|HUD|Format")
	static FText FormatPosition(int32 RacePosition, int32 CompetitorCount, bool bShowPosition);

	/** "n LAPS, m VALID" for the results panel. Negative counts read as 0. */
	UFUNCTION(BlueprintPure, Category = "Racing|HUD|Format")
	static FText FormatResultLaps(int32 LapsCompleted, int32 ValidLapsCompleted);

	/** One distinct, player-facing label per ERacingRunValidity value. */
	UFUNCTION(BlueprintPure, Category = "Racing|HUD|Format")
	static FText FormatRunValidity(ERacingRunValidity Validity);
};
