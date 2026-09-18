// Copyright RacingSim. All Rights Reserved.

#include "UI/RacingHudFormat.h"

namespace RacingHudFormatPrivate
{
	/** 999:59.999 in milliseconds. */
	constexpr int64 MaxLapTimeMilliseconds =
		static_cast<int64>(URacingHudFormatLibrary::MaxDisplayLapMinutes) * 60000 + 59999;

	/** +/-999.999 s. */
	constexpr int64 MaxDeltaMilliseconds = 999999;

	// Fixed strings are built once. FText::AsCultureInvariant copies its FString; doing that
	// per call for "--" would put an allocation on every stale frame for no reason.

	const FText& NoVehicleValue()
	{
		static const FText Text = FText::AsCultureInvariant(FString(TEXT("--")));
		return Text;
	}

	const FText& NoLapTime()
	{
		static const FText Text = FText::AsCultureInvariant(FString(TEXT("-:--.---")));
		return Text;
	}
}

// ---------------------------------------------------------------------------
// Rounding
// ---------------------------------------------------------------------------

int32 URacingHudFormatLibrary::RoundSpeedForDisplay(const double Speed)
{
	if (!FMath::IsFinite(Speed))
	{
		return -1;
	}

	const double Magnitude = FMath::Abs(Speed);
	if (Magnitude >= static_cast<double>(MaxDisplaySpeed))
	{
		return MaxDisplaySpeed;
	}
	return static_cast<int32>(FMath::FloorToDouble(Magnitude + 0.5));
}

int32 URacingHudFormatLibrary::RoundRPMForDisplay(const float EngineRPM)
{
	if (!FMath::IsFinite(EngineRPM))
	{
		return -1;
	}

	const double RPM = FMath::Max(0.0, static_cast<double>(EngineRPM));
	if (RPM >= static_cast<double>(MaxDisplayRPM))
	{
		return MaxDisplayRPM;
	}
	return static_cast<int32>(FMath::FloorToDouble(RPM + 0.5));
}

int64 URacingHudFormatLibrary::RoundLapTimeMilliseconds(const double Seconds)
{
	using namespace RacingHudFormatPrivate;

	if (!FMath::IsFinite(Seconds))
	{
		return -1;
	}
	if (Seconds <= 0.0)
	{
		return 0;
	}

	const double Milliseconds = Seconds * 1000.0;
	if (Milliseconds >= static_cast<double>(MaxLapTimeMilliseconds))
	{
		return MaxLapTimeMilliseconds;
	}
	// Nearest, with carry: 59.9996 s is 60000 ms, which formats as 1:00.000.
	return static_cast<int64>(FMath::FloorToDouble(Milliseconds + 0.5));
}

int64 URacingHudFormatLibrary::RoundDeltaMilliseconds(const double Seconds)
{
	using namespace RacingHudFormatPrivate;

	if (!FMath::IsFinite(Seconds))
	{
		return 0;
	}

	const double Magnitude = FMath::Abs(Seconds) * 1000.0;
	const int64 Rounded = (Magnitude >= static_cast<double>(MaxDeltaMilliseconds))
		? MaxDeltaMilliseconds
		: static_cast<int64>(FMath::FloorToDouble(Magnitude + 0.5));
	return Seconds < 0.0 ? -Rounded : Rounded;
}

// ---------------------------------------------------------------------------
// Vehicle
// ---------------------------------------------------------------------------

FText URacingHudFormatLibrary::FormatSpeed(const double Speed, const bool bVehicleDataFresh)
{
	const int32 Whole = RoundSpeedForDisplay(Speed);
	if (!bVehicleDataFresh || Whole < 0)
	{
		return RacingHudFormatPrivate::NoVehicleValue();
	}
	return FText::AsCultureInvariant(FString::Printf(TEXT("%d"), Whole));
}

FText URacingHudFormatLibrary::FormatSpeedUnit(const ERacingSpeedDisplayUnit Unit)
{
	static const FText Kmh = FText::AsCultureInvariant(FString(TEXT("km/h")));
	static const FText Mph = FText::AsCultureInvariant(FString(TEXT("mph")));
	static const FText Ms = FText::AsCultureInvariant(FString(TEXT("m/s")));

	switch (Unit)
	{
	case ERacingSpeedDisplayUnit::MilesPerHour:
		return Mph;
	case ERacingSpeedDisplayUnit::MetresPerSecond:
		return Ms;
	case ERacingSpeedDisplayUnit::KilometresPerHour:
		return Kmh;
	}
	// An out-of-range value cannot come from the view model, which sets the unit it converted to.
	return Kmh;
}

FText URacingHudFormatLibrary::FormatRPM(const float EngineRPM, const bool bVehicleDataFresh)
{
	const int32 Whole = RoundRPMForDisplay(EngineRPM);
	if (!bVehicleDataFresh || Whole < 0)
	{
		return RacingHudFormatPrivate::NoVehicleValue();
	}
	return FText::AsCultureInvariant(FString::Printf(TEXT("%d"), Whole));
}

FText URacingHudFormatLibrary::FormatGear(const int32 GearIndex, const bool bVehicleDataFresh)
{
	static const FText Stale = FText::AsCultureInvariant(FString(TEXT("-")));
	static const FText Reverse = FText::AsCultureInvariant(FString(TEXT("R")));
	static const FText Neutral = FText::AsCultureInvariant(FString(TEXT("N")));

	if (!bVehicleDataFresh)
	{
		return Stale;
	}
	if (GearIndex < 0)
	{
		return Reverse;
	}
	if (GearIndex == 0)
	{
		return Neutral;
	}
	return FText::AsCultureInvariant(FString::Printf(TEXT("%d"), GearIndex));
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

FText URacingHudFormatLibrary::FormatLapTime(const double Seconds, const bool bHasTime)
{
	const int64 TotalMs = RoundLapTimeMilliseconds(Seconds);
	if (!bHasTime || TotalMs < 0)
	{
		return RacingHudFormatPrivate::NoLapTime();
	}

	// Bounded by MaxLapTimeMilliseconds, so every part fits an int32.
	const int32 Minutes = static_cast<int32>(TotalMs / 60000);
	const int32 SecondsPart = static_cast<int32>((TotalMs / 1000) % 60);
	const int32 Millis = static_cast<int32>(TotalMs % 1000);
	return FText::AsCultureInvariant(FString::Printf(TEXT("%d:%02d.%03d"), Minutes, SecondsPart, Millis));
}

FText URacingHudFormatLibrary::FormatDelta(const double DeltaSeconds, const bool bHasDelta)
{
	if (!bHasDelta || !FMath::IsFinite(DeltaSeconds))
	{
		return FText::GetEmpty();
	}

	const int64 Ms = RoundDeltaMilliseconds(DeltaSeconds);
	const int64 Magnitude = Ms < 0 ? -Ms : Ms;
	// A delta that rounds to zero is "+0.000" whichever side of zero it came from.
	const TCHAR* Sign = Ms < 0 ? TEXT("-") : TEXT("+");
	return FText::AsCultureInvariant(FString::Printf(TEXT("%s%d.%03d"), Sign,
		static_cast<int32>(Magnitude / 1000), static_cast<int32>(Magnitude % 1000)));
}

// ---------------------------------------------------------------------------
// Race
// ---------------------------------------------------------------------------

FText URacingHudFormatLibrary::FormatCountdown(const int32 CountdownWholeSeconds, const bool bShowCountdown)
{
	static const FText Go = FText::AsCultureInvariant(FString(TEXT("GO")));

	if (!bShowCountdown)
	{
		return FText::GetEmpty();
	}
	if (CountdownWholeSeconds <= 0)
	{
		return Go;
	}
	return FText::AsCultureInvariant(FString::Printf(TEXT("%d"),
		FMath::Min(CountdownWholeSeconds, MaxDisplayCountdownSeconds)));
}

FText URacingHudFormatLibrary::FormatLapCounter(const int32 CurrentLapNumber)
{
	static const FText NoLap = FText::AsCultureInvariant(FString(TEXT("LAP -")));

	if (CurrentLapNumber <= 0)
	{
		return NoLap;
	}
	return FText::AsCultureInvariant(FString::Printf(TEXT("LAP %d"), CurrentLapNumber));
}

FText URacingHudFormatLibrary::FormatPosition(const int32 RacePosition, const int32 CompetitorCount, const bool bShowPosition)
{
	if (!bShowPosition || RacePosition <= 0 || CompetitorCount <= 0)
	{
		return FText::GetEmpty();
	}
	return FText::AsCultureInvariant(FString::Printf(TEXT("P %d/%d"), RacePosition, CompetitorCount));
}

FText URacingHudFormatLibrary::FormatResultLaps(const int32 LapsCompleted, const int32 ValidLapsCompleted)
{
	return FText::AsCultureInvariant(FString::Printf(TEXT("%d LAPS, %d VALID"),
		FMath::Max(0, LapsCompleted), FMath::Max(0, ValidLapsCompleted)));
}

FText URacingHudFormatLibrary::FormatRunValidity(const ERacingRunValidity Validity)
{
	static const FText Unknown = FText::AsCultureInvariant(FString(TEXT("UNKNOWN")));
	static const FText Pending = FText::AsCultureInvariant(FString(TEXT("PENDING")));
	static const FText Valid = FText::AsCultureInvariant(FString(TEXT("VALID")));
	static const FText Shortcut = FText::AsCultureInvariant(FString(TEXT("INVALID - SHORTCUT")));
	static const FText Reverse = FText::AsCultureInvariant(FString(TEXT("INVALID - WRONG WAY")));
	static const FText Reset = FText::AsCultureInvariant(FString(TEXT("INVALID - VEHICLE RESET")));
	static const FText Penalty = FText::AsCultureInvariant(FString(TEXT("INVALID - PENALTY")));
	static const FText Incomplete = FText::AsCultureInvariant(FString(TEXT("INVALID - INCOMPLETE")));

	switch (Validity)
	{
	case ERacingRunValidity::Unknown:                return Unknown;
	case ERacingRunValidity::Pending:                return Pending;
	case ERacingRunValidity::Valid:                  return Valid;
	case ERacingRunValidity::InvalidShortcut:        return Shortcut;
	case ERacingRunValidity::InvalidReverseCrossing: return Reverse;
	case ERacingRunValidity::InvalidVehicleReset:    return Reset;
	case ERacingRunValidity::InvalidPenalty:         return Penalty;
	case ERacingRunValidity::InvalidIncomplete:      return Incomplete;
	}
	// Out of range: say nothing we cannot stand behind.
	return Unknown;
}
