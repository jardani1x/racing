// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/RacingSimValidation.h"
#include "Engine/DataAsset.h"
#include "Vehicle/VehicleFailureDetection.h"
#include "VehicleFailureThresholdsDataAsset.generated.h"

/**
 * VEH-004: the authored, validated failure envelopes.
 *
 * CLAUDE.md: "Put tunable vehicle and race parameters in typed DataAssets or config,
 * not magic numbers in Tick." These numbers decide whether a run is reported as
 * corrupt, so they are exactly the kind of value that must be visible, versioned and
 * range-checked rather than buried in a detector.
 *
 * ---------------------------------------------------------------------------
 * Why the asset and FVehicleFailureThresholds are separate types
 * ---------------------------------------------------------------------------
 *
 * CORE-003's EnforceRanges is reflection-driven and resolves each declared range by
 * name to a FLAT numeric UPROPERTY on the validated object's class. Nesting the POD
 * inside the asset as a single struct property would put every field one level down
 * where the reflective pass cannot reach it -- VEH-001 hit exactly this and had to
 * hand-write ApplyRange for its nested FVehicleInputProfile.
 *
 * So the asset declares flat properties and GetThresholds() assembles the POD. The
 * detector then needs no UObject at all, which is what keeps it reachable from a Smoke
 * test (Docs/Environment.md).
 *
 * THE DUPLICATED DEFAULTS ARE PINNED BY A TEST. Two independent copies of the same
 * numbers rot; RacingSim.Vehicle.FailureThresholdDefaultsMatchAsset compares the CDO's
 * GetThresholds() against a default-constructed FVehicleFailureThresholds field by
 * field and fails when they diverge -- including when a new field is added to one and
 * forgotten in the other, which is the direction that actually causes the bug.
 *
 * ---------------------------------------------------------------------------
 * Original prototype envelopes, per CLAUDE.md
 * ---------------------------------------------------------------------------
 *
 * Every default is invented for this project as a deliberately generous outer bound on
 * SIMULATION CORRUPTION. None is a branded specification, a manufacturer figure, or a
 * number taken from another game. Docs/02-VehiclePhysics.md: "Use envelopes rather
 * than fake precision until source data is authoritative."
 *
 * ---------------------------------------------------------------------------
 * No .uasset is authored by this ticket
 * ---------------------------------------------------------------------------
 *
 * CLAUDE.md forbids editing Unreal binary assets from a worktree. The class and its
 * validated defaults are what VEH-004 owes; a pawn with a null thresholds asset uses
 * FVehicleFailureThresholds' defaults, which are these same numbers, and says so once.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Vehicle Failure Thresholds"))
class RACINGSIM_API UVehicleFailureThresholdsDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** CORE-003 range pass over this class's flat numeric properties. */
	static TConstArrayView<RacingSim::Validation::FRacingPropertyRange> StaticRanges();

	/**
	 * Ranges, then the relationships no per-field clamp can express.
	 *
	 * @param bCorrect  true to write safe values back over out-of-range numerics.
	 *                  RELATIONSHIP failures are never corrected -- same policy as the
	 *                  chassis and tune assets: there is no single safe value for
	 *                  "the tunnelling tolerance is wider than the analysis window
	 *                  allows", and inventing one produces an asset that validates and
	 *                  detects nothing.
	 */
	RacingSim::Validation::FRacingValidationResult Validate(bool bCorrect);

	/** Const report-only form. Never mutates this asset. */
	RacingSim::Validation::FRacingValidationResult ValidateReadOnly() const;

	/** Assemble the POD the pure detector consumes. Performs no validation of its own -- call Validate() first. */
	FVehicleFailureThresholds GetThresholds() const;

	// -- Runaway energy -----------------------------------------------------

	/** Speed envelope, CENTIMETRES PER SECOND. 15,000 == 540 km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Runaway energy", meta = (ClampMin = "100.0", ClampMax = "1000000.0"))
	float MaxSpeedCms = 15000.0f;

	/** Angular speed envelope, DEGREES PER SECOND. 2160 == six revolutions per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Runaway energy", meta = (ClampMin = "90.0", ClampMax = "100000.0"))
	float MaxAngularSpeedDegreesPerSecond = 2160.0f;

	/** Engine speed envelope, RPM. Must stay above any tune's MaxRpm or a healthy engine reads as diverged. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Runaway energy", meta = (ClampMin = "1000.0", ClampMax = "1000000.0"))
	float MaxEngineRpm = 30000.0f;

	/** Acceleration envelope, CENTIMETRES PER SECOND SQUARED. 8,000 is about 8.15 g. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Runaway energy", meta = (ClampMin = "100.0", ClampMax = "1000000.0"))
	float MaxAccelerationCmsPerSecondSquared = 8000.0f;

	// -- Tunnelling ---------------------------------------------------------

	/**
	 * How much further than |velocity| * step the body may move, dimensionless.
	 *
	 * ClampMin is 1.0, not 0: below 1.0 the allowance is smaller than the distance the
	 * body's own recorded velocity already explains, so EVERY step would report
	 * tunnelling. A value that guarantees a false positive is not a tuning choice.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tunnelling", meta = (ClampMin = "1.0", ClampMax = "100.0"))
	float TunnelVelocityFactor = 2.0f;

	/** Absolute slack added to the velocity-explained distance, CENTIMETRES. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tunnelling", meta = (ClampMin = "0.0", ClampMax = "100000.0"))
	float TunnelToleranceCm = 100.0f;

	/**
	 * Steps longer than this suppress tunnelling and acceleration analysis, SECONDS.
	 *
	 * Declares a ReplacementValue rather than clamping, because its minimum is NOT its
	 * safe end: a tiny analysis window suppresses the extrapolating detectors on almost
	 * every step, which disarms them silently -- the same shape of hazard
	 * URacingSimSettings::TelemetryStaleAfterSeconds forced FRacingPropertyRange::
	 * ReplacementValue into existence for (Core/RacingSimValidation.h).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tunnelling", meta = (ClampMin = "0.02", ClampMax = "5.0"))
	float MaxAnalysisStepSeconds = 0.5f;

	// -- Wheel state --------------------------------------------------------

	/** How far outside [0,1] a normalised suspension length may sit, dimensionless. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheel state", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float SuspensionLengthTolerance = 0.05f;

	/** All wheels off the ground for longer than this is unstable wheel state, SECONDS. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheel state", meta = (ClampMin = "0.1", ClampMax = "600.0"))
	float MaxAirborneSeconds = 5.0f;

	/** A contact point further than this from the body origin is not a real contact, CENTIMETRES. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheel state", meta = (ClampMin = "10.0", ClampMax = "100000.0"))
	float MaxContactDistanceCm = 1000.0f;

	// -- Persistent penetration ---------------------------------------------

	/** Normalised suspension length at or below this counts as fully compressed, dimensionless. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Penetration", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float PenetrationSuspensionLengthFraction = 0.02f;

	/** Spring force above this counts as "under load", NEWTONS. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Penetration", meta = (ClampMin = "0.0", ClampMax = "1000000.0"))
	float PenetrationSpringForceN = 1.0f;

	/** How long a wheel must stay fully compressed under load before it is a fault, SECONDS. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Penetration", meta = (ClampMin = "0.05", ClampMax = "600.0"))
	float PenetrationPersistSeconds = 2.0f;
};
