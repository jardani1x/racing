// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingTelemetryFunctionLibrary.h"

#include "Core/RacingSimSettings.h"
#include "Core/RacingSimUnits.h"

double URacingTelemetryFunctionLibrary::GetForwardSpeedKph(const FRacingVehicleTelemetrySample& Sample)
{
	return Sample.GetForwardSpeedKph();
}

double URacingTelemetryFunctionLibrary::GetForwardSpeedMph(const FRacingVehicleTelemetrySample& Sample)
{
	return Sample.GetForwardSpeedMph();
}

double URacingTelemetryFunctionLibrary::GetForwardSpeedMetresPerSecond(const FRacingVehicleTelemetrySample& Sample)
{
	return Sample.GetForwardSpeedMetresPerSecond();
}

double URacingTelemetryFunctionLibrary::GetForwardSpeedInDisplayUnit(
	const FRacingVehicleTelemetrySample& Sample, const ERacingSpeedDisplayUnit Unit)
{
	return ConvertSpeedCmsToDisplayUnit(Sample.ForwardSpeedCms, Unit);
}

double URacingTelemetryFunctionLibrary::ConvertSpeedCmsToDisplayUnit(
	const double CentimetresPerSecond, const ERacingSpeedDisplayUnit Unit)
{
	switch (Unit)
	{
	case ERacingSpeedDisplayUnit::MilesPerHour:
		return RacingSim::Units::CmsToMilesPerHour(CentimetresPerSecond);
	case ERacingSpeedDisplayUnit::MetresPerSecond:
		return RacingSim::Units::CmsToMetresPerSecond(CentimetresPerSecond);
	case ERacingSpeedDisplayUnit::KilometresPerHour:
	default:
		// km/h is the project's display default; an out-of-range enum value (a corrupt
		// save) reads as the default rather than as zero speed.
		return RacingSim::Units::CmsToKilometresPerHour(CentimetresPerSecond);
	}
}

double URacingTelemetryFunctionLibrary::CmToMetres(const double Centimetres)
{
	return RacingSim::Units::CmToMetres(Centimetres);
}

double URacingTelemetryFunctionLibrary::CmToKilometres(const double Centimetres)
{
	return RacingSim::Units::CmToKilometres(Centimetres);
}

double URacingTelemetryFunctionLibrary::CmsToMetresPerSecond(const double CentimetresPerSecond)
{
	return RacingSim::Units::CmsToMetresPerSecond(CentimetresPerSecond);
}

double URacingTelemetryFunctionLibrary::CmsToKilometresPerHour(const double CentimetresPerSecond)
{
	return RacingSim::Units::CmsToKilometresPerHour(CentimetresPerSecond);
}

double URacingTelemetryFunctionLibrary::CmsToMilesPerHour(const double CentimetresPerSecond)
{
	return RacingSim::Units::CmsToMilesPerHour(CentimetresPerSecond);
}

double URacingTelemetryFunctionLibrary::CmsSquaredToG(const double CentimetresPerSecondSquared)
{
	return RacingSim::Units::CmsSquaredToG(CentimetresPerSecondSquared);
}

bool URacingTelemetryFunctionLibrary::IsTelemetryFrameStale(
	const FRacingTelemetryFrame& Frame, const double NowSeconds, const double MaxAgeSeconds)
{
	return Frame.IsStaleAt(NowSeconds, MaxAgeSeconds);
}

bool URacingTelemetryFunctionLibrary::IsTimestampStale(
	const double TimestampSeconds, const double NowSeconds, const double MaxAgeSeconds)
{
	return FRacingTelemetryFrame::IsTimestampStaleAt(TimestampSeconds, NowSeconds, MaxAgeSeconds);
}

double URacingTelemetryFunctionLibrary::GetTelemetryStaleAfterSeconds()
{
	return GetDefault<URacingSimSettings>()->TelemetryStaleAfterSeconds;
}

bool URacingTelemetryFunctionLibrary::IsLapComplete(const FRacingLapTiming& Lap)
{
	return Lap.IsComplete();
}

double URacingTelemetryFunctionLibrary::GetLapSectorTotalSeconds(const FRacingLapTiming& Lap)
{
	return Lap.GetSectorTotalSeconds();
}

bool URacingTelemetryFunctionLibrary::AreLapSectorsConsistent(
	const FRacingLapTiming& Lap, const int32 ExpectedSectorCount, const double ToleranceSeconds)
{
	return Lap.AreSectorsConsistent(ToleranceSeconds, ExpectedSectorCount);
}

bool URacingTelemetryFunctionLibrary::IsContentVersionPopulated(const FRacingContentVersion& Version)
{
	return Version.IsPopulated();
}

FString URacingTelemetryFunctionLibrary::ContentVersionToString(const FRacingContentVersion& Version)
{
	return Version.ToString();
}

FString URacingTelemetryFunctionLibrary::VersionStampToString(const FRacingSimVersionStamp& Stamp)
{
	return Stamp.ToString();
}
