// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/RacingSimBuildId.h"
#include "Core/RacingSimTypes.h"
#include "Core/RacingTelemetry.h"
#include "RacingTelemetryFunctionLibrary.generated.h"

/**
 * UI-001, closing CORE-002 finding M-3: Blueprint and UMG access to the Core telemetry
 * and unit contracts.
 *
 * A USTRUCT member function cannot be a UFUNCTION, so every accessor on
 * FRacingVehicleTelemetrySample, FRacingLapTiming, FRacingTelemetryFrame and the version
 * structs was unreachable from the HUD that CLAUDE.md says is built in UMG. Each function
 * here is a one-line forward to the C++ member or RacingSim::Units function it names, and
 * RacingSim.Core.TelemetryFunctionLibrary asserts the two agree -- so there is exactly one
 * definition of each rule and this file cannot drift into a second opinion.
 *
 * Kept in Core/ rather than UI/: it wraps Core contracts, and putting unit conversion in
 * UI/ would give CORE-002's conversion policy a second home.
 *
 * Structs are taken by const& so a call from native code copies nothing (CORE-002 M-4).
 */
UCLASS()
class RACINGSIM_API URacingTelemetryFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// =======================================================================
	// Speed (UNIT BOUNDARY: storage is cm/s; these are for display only)
	// =======================================================================

	/** Forward speed in km/h. Signed: negative means reversing. */
	UFUNCTION(BlueprintPure, Category = "Racing|Telemetry|Speed")
	static double GetForwardSpeedKph(const FRacingVehicleTelemetrySample& Sample);

	/** Forward speed in mph. Signed: negative means reversing. */
	UFUNCTION(BlueprintPure, Category = "Racing|Telemetry|Speed")
	static double GetForwardSpeedMph(const FRacingVehicleTelemetrySample& Sample);

	/** Forward speed in m/s. Signed: negative means reversing. */
	UFUNCTION(BlueprintPure, Category = "Racing|Telemetry|Speed")
	static double GetForwardSpeedMetresPerSecond(const FRacingVehicleTelemetrySample& Sample);

	/** Forward speed in the player's chosen display unit. */
	UFUNCTION(BlueprintPure, Category = "Racing|Telemetry|Speed")
	static double GetForwardSpeedInDisplayUnit(const FRacingVehicleTelemetrySample& Sample, ERacingSpeedDisplayUnit Unit);

	/** Convert a speed in cm/s to the given display unit. The one switch over ERacingSpeedDisplayUnit. */
	UFUNCTION(BlueprintPure, Category = "Racing|Telemetry|Speed")
	static double ConvertSpeedCmsToDisplayUnit(double CentimetresPerSecond, ERacingSpeedDisplayUnit Unit);

	// =======================================================================
	// Unit conversions (RacingSim::Units)
	// =======================================================================

	UFUNCTION(BlueprintPure, Category = "Racing|Units")
	static double CmToMetres(double Centimetres);

	UFUNCTION(BlueprintPure, Category = "Racing|Units")
	static double CmToKilometres(double Centimetres);

	UFUNCTION(BlueprintPure, Category = "Racing|Units")
	static double CmsToMetresPerSecond(double CentimetresPerSecond);

	UFUNCTION(BlueprintPure, Category = "Racing|Units")
	static double CmsToKilometresPerHour(double CentimetresPerSecond);

	UFUNCTION(BlueprintPure, Category = "Racing|Units")
	static double CmsToMilesPerHour(double CentimetresPerSecond);

	UFUNCTION(BlueprintPure, Category = "Racing|Units")
	static double CmsSquaredToG(double CentimetresPerSecondSquared);

	// =======================================================================
	// Staleness
	// =======================================================================

	/** FRacingTelemetryFrame::IsStaleAt. A frame stamped in the future is stale. */
	UFUNCTION(BlueprintPure, Category = "Racing|Telemetry")
	static bool IsTelemetryFrameStale(const FRacingTelemetryFrame& Frame, double NowSeconds, double MaxAgeSeconds);

	/** FRacingTelemetryFrame::IsTimestampStaleAt, for any timestamp, e.g. a vehicle sample's. */
	UFUNCTION(BlueprintPure, Category = "Racing|Telemetry")
	static bool IsTimestampStale(double TimestampSeconds, double NowSeconds, double MaxAgeSeconds);

	/** Project default for MaxAgeSeconds: URacingSimSettings::TelemetryStaleAfterSeconds. */
	UFUNCTION(BlueprintPure, Category = "Racing|Telemetry")
	static double GetTelemetryStaleAfterSeconds();

	// =======================================================================
	// Lap timing
	// =======================================================================

	UFUNCTION(BlueprintPure, Category = "Racing|Timing")
	static bool IsLapComplete(const FRacingLapTiming& Lap);

	UFUNCTION(BlueprintPure, Category = "Racing|Timing")
	static double GetLapSectorTotalSeconds(const FRacingLapTiming& Lap);

	/**
	 * FRacingLapTiming::AreSectorsConsistent with the track's sector count REQUIRED.
	 *
	 * RACE-003 finding M3: the C++ default (INDEX_NONE) answers "the splits present are
	 * consistent", which reads true for a lap whose splits were withheld. A HUD always has
	 * the real count in scope, so this wrapper gives it no way to skip it. Pass 0 for a
	 * track that authored no sectors.
	 */
	UFUNCTION(BlueprintPure, Category = "Racing|Timing")
	static bool AreLapSectorsConsistent(const FRacingLapTiming& Lap, int32 ExpectedSectorCount, double ToleranceSeconds = 0.001);

	// =======================================================================
	// Versions
	// =======================================================================

	/**
	 * FRacingContentVersion::IsPopulated: every identity field is filled in.
	 *
	 * NOT "safe to race" (RACE-003 finding L7): a track whose gate bake failed still hashes
	 * and still reads populated. Ask the track's cached validation for fitness.
	 */
	UFUNCTION(BlueprintPure, Category = "Racing|Version")
	static bool IsContentVersionPopulated(const FRacingContentVersion& Version);

	/** Diagnostics only. Allocates. */
	UFUNCTION(BlueprintPure, Category = "Racing|Version")
	static FString ContentVersionToString(const FRacingContentVersion& Version);

	/** Diagnostics only. Allocates. */
	UFUNCTION(BlueprintPure, Category = "Racing|Version")
	static FString VersionStampToString(const FRacingSimVersionStamp& Stamp);
};
