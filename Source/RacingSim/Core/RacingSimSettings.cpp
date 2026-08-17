// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingSimSettings.h"

#include "Core/RacingSimLog.h"

#define LOCTEXT_NAMESPACE "RacingSimSettings"

URacingSimSettings::URacingSimSettings()
{
	// Project Settings section identity. CategoryName is the left-hand group;
	// the section name comes from the class's DisplayName meta.
	CategoryName = TEXT("Game");
	SectionName = TEXT("RacingSim");
}

const URacingSimSettings& URacingSimSettings::Get()
{
	// GetDefault<T>() on a UDeveloperSettings returns the CDO, which IS the live
	// config object -- the ini is loaded into it during module startup. It is
	// never null once the module is loaded, and checkf here would turn a
	// hypothetical engine change into a crash with a readable message rather
	// than a null dereference somewhere downstream.
	const URacingSimSettings* Settings = GetDefault<URacingSimSettings>();
	checkf(Settings != nullptr, TEXT("URacingSimSettings CDO is null; module initialisation order is broken."));
	return *Settings;
}

TConstArrayView<RacingSim::Validation::FRacingPropertyRange> URacingSimSettings::GetValidatedPropertyRanges()
{
	using namespace RacingSim::Validation;

	// Function-local static rather than a file-scope array: FName construction at
	// static-initialisation time depends on the FName subsystem being up, and this
	// table is first touched during CDO construction, which is comfortably after
	// that. Built once, then shared.
	//
	// Each entry mirrors the ClampMin/ClampMax on the matching UPROPERTY in
	// RacingSimSettings.h, and RacingSim.Core.SettingsRangeMetadata asserts that
	// -- in both directions -- so this cannot quietly drift.
	//
	// UIMin/UIMax are deliberately NOT mirrored. TelemetryStaleAfterSeconds
	// carries UIMax = 5.0 and no ClampMax; 10 seconds is a strange but legal
	// value, and clamping a slider hint would destroy valid data.
	static const TArray<FRacingPropertyRange> Ranges = {
		FRacingPropertyRange::Between(GET_MEMBER_NAME_CHECKED(URacingSimSettings, LapTimeFractionalDigits), 0.0, 3.0),
		FRacingPropertyRange::AtLeast(GET_MEMBER_NAME_CHECKED(URacingSimSettings, PhysicsPolicyVersion), 0.0),
		FRacingPropertyRange::Between(GET_MEMBER_NAME_CHECKED(URacingSimSettings, TelemetrySampleRateHz), 0.0, 240.0),

		// The one property whose ClampMin is NOT its safe value. 0.0 does not
		// mean "least staleness", it means "staleness checking disabled" --
		// FRacingTelemetryFrame::IsStaleAt returns false for every frame when
		// MaxAgeSeconds <= 0 (RacingTelemetry.cpp). Clamping a NaN or a negative
		// to 0.0 would therefore take an obviously broken config and quietly
		// disarm the guard that stops the HUD presenting a dead producer's frozen
		// numbers as live. Corrections go to the class default instead, which
		// keeps the guard armed. Shares URacingSimSettings::DefaultTelemetryStaleAfterSeconds
		// with the UPROPERTY initialiser on TelemetryStaleAfterSeconds in
		// RacingSimSettings.h so the two cannot silently diverge.
		FRacingPropertyRange::AtLeast(GET_MEMBER_NAME_CHECKED(URacingSimSettings, TelemetryStaleAfterSeconds), 0.0)
			.WithReplacement(URacingSimSettings::DefaultTelemetryStaleAfterSeconds)
	};

	return Ranges;
}

RacingSim::Validation::FRacingValidationResult URacingSimSettings::ValidateConfiguredRanges(const TCHAR* SourceDescription)
{
	using namespace RacingSim::Validation;

	FRacingValidationResult Result = EnforceRanges(this, GetValidatedPropertyRanges());

	if (!Result.IsClean())
	{
		// Named source first, so a reader of the log knows whether to go looking
		// in DefaultGame.ini, on the command line, or in an editor session.
		UE_LOG(
			LogRacingCore,
			Warning,
			TEXT("RacingSim settings failed range validation after %s: %d value(s) corrected, %d range(s) could not be applied."),
			SourceDescription,
			Result.NumCorrected(),
			Result.NumFailed());
	}

	LogResult(Result, this);
	return Result;
}

void URacingSimSettings::PostInitProperties()
{
	Super::PostInitProperties();
	ValidateConfiguredRanges(TEXT("PostInitProperties (ini and -ini: overrides)"));
}

void URacingSimSettings::PostReloadConfig(FProperty* PropertyThatWasLoaded)
{
	Super::PostReloadConfig(PropertyThatWasLoaded);
	ValidateConfiguredRanges(TEXT("PostReloadConfig"));
}

#if WITH_EDITOR
void URacingSimSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ValidateConfiguredRanges(TEXT("PostEditChangeProperty"));
}
#endif

FName URacingSimSettings::GetCategoryName() const
{
	return TEXT("Game");
}

#if WITH_EDITOR
FText URacingSimSettings::GetSectionText() const
{
	return LOCTEXT("SectionText", "Racing Sim");
}

FText URacingSimSettings::GetSectionDescription() const
{
	return LOCTEXT(
		"SectionDescription",
		"Project-wide RacingSim configuration: unit display defaults, the build-ID scheme "
		"recorded on every competitive result, and telemetry sampling. Simulation and storage "
		"always use Unreal centimetres; only presentation is configurable here. Vehicle and "
		"race tuning belongs in DataAssets, not on this page.");
}
#endif

#undef LOCTEXT_NAMESPACE
