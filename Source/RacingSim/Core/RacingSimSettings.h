// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Core/RacingSimTypes.h"
#include "RacingSimSettings.generated.h"

/**
 * Project-wide RacingSim configuration: units presentation and the build-ID scheme.
 *
 * UDeveloperSettings, so it appears in Project Settings -> Game -> Racing Sim with
 * no editor code of our own, and `config = Game` + `defaultconfig` so edits land
 * in Config/DefaultGame.ini and are reviewable in a diff. CI overrides the same
 * keys on the command line or in a platform ini.
 *
 * SCOPE, deliberately narrow. This holds only values that are genuinely
 * project-wide and have no better home. It is NOT the dumping ground for vehicle
 * or race tuning -- CLAUDE.md requires those in typed DataAssets (CORE-003,
 * VEH-003, RACE-001). The test for whether a value belongs here: would a track
 * or a car ever want a different value? If yes, it is a DataAsset field.
 *
 * NOTE (finding N-3): this is the module's first UObject. From this commit on,
 * moving RacingSim's classes into a different module renames /Script/RacingSim.*
 * and breaks every Blueprint, DataAsset and map reference without authored
 * CoreRedirects. The two-module layout in Docs/15-ProjectStructure.md is
 * therefore final, not provisional.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Racing Sim"))
class RACINGSIM_API URacingSimSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	URacingSimSettings();

	/** Convenience accessor. Never null after module load; the CDO is the live config object. */
	static const URacingSimSettings& Get();

	//~ Begin UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
#endif
	//~ End UDeveloperSettings interface

	// ======================================================================
	// Units
	// ======================================================================
	//
	// There is no "internal units" setting and there never will be one. Storage
	// and simulation are always Unreal centimetres (RacingSimUnits.h). Only
	// presentation is configurable, and only as a DEFAULT -- a player-facing
	// preference at UI-002 overrides these per session without touching config.

	/** Default speed unit shown on the HUD. Presentation only; stored speed is always cm/s. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Units|Display")
	ERacingSpeedDisplayUnit DefaultSpeedDisplayUnit = ERacingSpeedDisplayUnit::KilometresPerHour;

	/** Default distance unit shown on the HUD. Presentation only; stored distance is always centimetres. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Units|Display")
	ERacingDistanceDisplayUnit DefaultDistanceDisplayUnit = ERacingDistanceDisplayUnit::Metric;

	/**
	 * Fractional digits shown on lap and sector times, e.g. 3 -> 1:23.456.
	 *
	 * A display setting, not a storage setting: durations are stored as double
	 * seconds and are never rounded before they are formatted. Clamped to 0..3
	 * because the race clock's resolution does not justify more.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Units|Display", meta = (ClampMin = "0", ClampMax = "3", UIMin = "0", UIMax = "3"))
	int32 LapTimeFractionalDigits = 3;

	// ======================================================================
	// Build ID and versioning
	// ======================================================================

	/**
	 * How to compose the game build ID recorded on every result.
	 *
	 * Derived on a developer machine; Explicit in CI. A published leaderboard
	 * must reject Derived IDs -- see FRacingSimBuildId::bIsAuthoritative.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Versioning")
	ERacingBuildIdScheme BuildIdScheme = ERacingBuildIdScheme::Derived;

	/**
	 * Release channel, used as the first component of a derived build ID.
	 *
	 * Free-form but conventionally one of: dev, ci, qa, release. Whitespace and
	 * unsafe filename/URL/CSV characters are stripped when the ID is composed
	 * (see RacingSim::BuildIdPrivate::SanitiseComponent), so a stray space or
	 * slash in an ini cannot reach the ID. '-' and '.' are NOT stripped -- a
	 * derived build ID is an opaque string for display and comparison only;
	 * nothing in the project splits it back into components, so a channel or
	 * project version containing '-' does not create ambiguity.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Versioning")
	FString BuildChannel = TEXT("dev");

	/**
	 * The build ID stamped by CI. Used only when BuildIdScheme is Explicit.
	 *
	 * Empty is a valid state on a developer machine and is NOT an error; the
	 * resolver falls back to the derived form and marks the result
	 * non-authoritative rather than failing. Overridden in CI with
	 * -ini:Game:[/Script/RacingSim.RacingSimSettings]:ExplicitBuildId=<value>
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Versioning")
	FString ExplicitBuildId;

	/**
	 * Version of the physics policy (substepping, fixed tick, solver settings).
	 *
	 * Bump this by hand in the same commit that changes any simulation-affecting
	 * default. It is what makes an old lap time comparable or not, so it is a
	 * deliberate human act, not something derived from a hash of engine config.
	 * Owned by VEH-002; 0 means "no policy established yet", which is the truth
	 * today and is why nothing may be published as a competitive result yet.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Versioning", meta = (ClampMin = "0"))
	int32 PhysicsPolicyVersion = 0;

	/**
	 * Assist preset assumed when a run does not specify one.
	 *
	 * Raw, deliberately: the strictest option. If plumbing is ever missed, a run
	 * is recorded as having had NO assists, which understates the assistance and
	 * is the direction that makes a wrong record obvious rather than flattering.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Versioning")
	ERacingAssistPreset DefaultAssistPreset = ERacingAssistPreset::Raw;

	// ======================================================================
	// Telemetry
	// ======================================================================

	/**
	 * How often Vehicle/ samples telemetry, in hertz. 0 disables sampling.
	 *
	 * Config, not a magic number in Tick, per CLAUDE.md. 30 Hz is enough to
	 * reconstruct a lap trace and is well under the tick rate, so sampling never
	 * becomes a per-frame allocation path.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Telemetry", meta = (ClampMin = "0.0", ClampMax = "240.0", UIMin = "0.0", UIMax = "120.0"))
	float TelemetrySampleRateHz = 30.0f;

	/**
	 * A telemetry frame older than this many seconds is stale and must not be
	 * displayed as live. Guards the HUD against a dead producer -- a frozen
	 * speedometer reading a plausible number is worse than a blank one.
	 *
	 * 0 disables the check (see RacingTelemetry.cpp, IsStaleAt). Only meant for
	 * isolating an unrelated failure while debugging -- leaving it at 0 in a
	 * built config reintroduces the exact frozen-plausible-HUD failure this
	 * field exists to prevent.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Telemetry", meta = (ClampMin = "0.0", UIMin = "0.05", UIMax = "5.0"))
	double TelemetryStaleAfterSeconds = 0.5;
};
