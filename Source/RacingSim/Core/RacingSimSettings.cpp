// Copyright RacingSim. All Rights Reserved.

#include "Core/RacingSimSettings.h"

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
