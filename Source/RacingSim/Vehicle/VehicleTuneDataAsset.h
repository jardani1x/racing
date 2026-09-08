// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "Core/RacingSimBuildId.h"
#include "Core/RacingSimValidation.h"
#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataAsset.h"
#include "VehicleTuneDataAsset.generated.h"

/**
 * VEH-003: the prototype's TUNE, as data.
 *
 * ---------------------------------------------------------------------------
 * What this asset is, and what it deliberately is not
 * ---------------------------------------------------------------------------
 *
 * "How FAST is it." Engine torque curve, gear ratios, brake torques, steering setup,
 * spring rates and damping.
 *
 * It is NOT the geometry. Wheel radius/width, wheel placement, chassis extent, mass,
 * centre of mass, the mechanical steering LOCK (MaxSteerAngleDegrees) and the drivetrain
 * TOPOLOGY all belong to VEH-002's UVehicleChassisDataAsset, which states the dividing
 * question in its own header: a number answering "what SHAPE is it" lives there, a
 * number answering "how FAST is it" lives here. Crossing that line gives one Chaos value
 * two owners and gives a handling regression two places to hide.
 *
 * ---------------------------------------------------------------------------
 * The differential is NOT declared here, and that is a decision
 * ---------------------------------------------------------------------------
 *
 * This ticket is titled ".../diff/..." so its absence needs an explanation rather than
 * a shrug. Chaos' FVehicleDifferentialConfig (ChaosWheeledVehicleMovementComponent.h)
 * exposes exactly two fields: DifferentialType and FrontRearSplit. VEH-002 already owns
 * both -- as EVehicleDrivetrainLayout and FrontRearTorqueSplit on the chassis asset,
 * complete with the validation that reports a non-default split under a non-AWD layout
 * as meaningless. Re-declaring the bias here would mean two assets writing one Chaos
 * field, with whichever ran last winning silently.
 *
 * So VEH-003's differential contribution is a CROSS-CHECK and this comment, not a
 * duplicate field. If Phase 2 replaces Chaos with a driveline model that has a real
 * preload/ramp-angle/locking-factor set, those are tune and they belong here; the
 * topology stays on the chassis.
 *
 * ---------------------------------------------------------------------------
 * There is no Chaos type in this file, on purpose
 * ---------------------------------------------------------------------------
 *
 * Same reasoning as the chassis asset, and the same enforcement: EVehicleSteeringModel
 * mirrors Chaos' ESteeringType, and ARacingVehiclePawn is the ONE place that maps it,
 * enumerator-for-enumerator. Docs/02-VehiclePhysics.md's Phase 2 clause requires the
 * data contract to survive a Chaos replacement; an ESteeringType in this header would
 * make that promise unkeepable.
 *
 * ---------------------------------------------------------------------------
 * Units. Stated once, obeyed everywhere. Two of them are NOT project units.
 * ---------------------------------------------------------------------------
 *
 *   torque                 NEWTON-METRES (Nm)          -- Chaos' unit for MaxTorque
 *   engine speed           REVOLUTIONS PER MINUTE (rpm)
 *   gear/final ratios      DIMENSIONLESS
 *   spring rate            NEWTONS PER METRE (N/m)     -- Chaos' unit, NOT N/cm
 *   spring preload         NEWTONS (N)
 *   damping ratio          DIMENSIONLESS
 *   suspension travel      CENTIMETRES (project storage unit)
 *   time                   SECONDS
 *   steer scale curve X    MILES PER HOUR              -- see below. Chaos' unit.
 *
 * SpringRate in N/m while every distance in this project is centimetres is Chaos' choice,
 * inherited by VEH-002's wheel classes and recorded in PrototypeVehicleWheel.h.
 *
 * The MPH one is a genuine trap and was verified in engine source rather than assumed:
 * UChaosWheeledVehicleMovementComponent samples SteeringSetup.SteeringCurve with
 * CmSToMPH(VehicleState.ForwardSpeed) (ChaosWheeledVehicleMovementComponent.cpp:738).
 * VEH-001's own speed-sensitive steering curve is in KM/H. Two curves, two units, one
 * multiplied by the other unless something decides -- which is what
 * EVehicleSteerSpeedAuthority exists to do.
 *
 * ---------------------------------------------------------------------------
 * Provenance of the numbers
 * ---------------------------------------------------------------------------
 *
 * Every default below is an ORIGINAL PROTOTYPE ENVELOPE invented for this project. No
 * branded vehicle's specification was consulted, inferred or approximated, per CLAUDE.md
 * and Docs/02-VehiclePhysics.md ("Use envelopes rather than fake precision until source
 * data is authoritative"). They are chosen to be self-consistent and stable, not to
 * resemble anything.
 */

namespace RacingSim::Vehicle::PrototypeTuneDefaults
{
	// -----------------------------------------------------------------------
	// The values Chaos reads from the WHEEL CLASS DEFAULT OBJECT.
	//
	// VEH-002 (PrototypeVehicleWheel.h) established why these cannot be written onto a
	// wheel from a DataAsset: UChaosWheeledVehicleMovementComponent::SetupVehicle reads
	// WheelSetups[i].WheelClass.GetDefaultObject(), so an instance write lands too late
	// and a CDO write is process-global. VEH-002 solved that for geometry by DUPLICATING
	// the literal into both places and cross-checking them.
	//
	// VEH-003 keeps the cross-check and removes the duplication: these constexpr values
	// are the single definition, consumed by BOTH the wheel constructors
	// (PrototypeVehicleWheel.cpp) and this asset's UPROPERTY defaults. The cross-check
	// still matters, because an ASSET INSTANCE can be edited after construction and the
	// wheel class cannot follow it.
	//
	// These REPLACE -- not extend -- the VEH-002 placeholders, as that file demanded.
	// -----------------------------------------------------------------------

	// -----------------------------------------------------------------------
	// SPRING RATES. VEH-006 raised these from 62/70 to 250/282, keeping the ratio.
	//
	// The VEH-003 values were picked for a plausible-looking front/rear balance and were
	// never checked against the weight they have to hold up. They could not hold it up:
	//
	//   Chaos stores the rate in per-CENTIMETRE units -- UChaosVehicleWheel applies
	//   Chaos::MToCm(SpringRate), i.e. UI x 100 (ChaosVehicleWheel.h:387-390).
	//   FSimpleSuspensionSim::Simulate then computes
	//       StiffnessForce = SpringDisplacement * Setup().SpringRate
	//   and SpringDisplacement saturates at
	//       MaxLength = |SuspensionMaxRaise| + |SuspensionMaxDrop| = 12 + 12 = 24 cm.
	//
	//   So the MOST force a corner could ever make was 24 * (62 * 100) = 148,800.
	//   A 1250 kg car under Unreal's -980 cm/s^2 gravity weighs 1250 * 980 = 1,225,000
	//   in those same units, which is 306,250 per corner. The springs were roughly half
	//   as stiff as needed even fully compressed, so the car could only ever bottom out.
	//
	// The new values put the static ride height near the middle of the travel, which is
	// where a suspension is supposed to sit: 306,250 / 12 cm = 25,520 per cm, i.e. about
	// 255 N/m in these UI units. Front 250 and rear 282 straddle that while preserving
	// the original 70/62 = 1.129 rear/front stiffness ratio, so the handling intent
	// below survives the correction. Chaos' own UChaosVehicleWheel default is 250.0f
	// (ChaosVehicleWheel.cpp:42-48), which is the same order of magnitude -- the old
	// numbers were the outlier, not these.
	//
	// If ChassisMassKg, SuspensionMaxRaiseCm or SuspensionMaxDropCm change, redo this
	// arithmetic. It is not a free parameter.
	//
	// THE SAME RATES IN SI, because everything above is in Unreal's centimetre force
	// units and those are not newtons per metre however the field is labelled:
	//
	//   Chaos stores UI x 100, and force = displacement_cm * stored. One Unreal force
	//   unit is 1 kg*cm/s^2 = 0.01 N, so a corner pushed d metres makes
	//       (d * 100) * (UI * 100) * 0.01  =  d * (UI * 100) newtons,
	//   i.e. the TRUE SI stiffness is UI x 100 N/m. Front 250 is 25.0 kN/m per corner
	//   and rear 282 is 28.2 kN/m per corner. The field's "N/m" label is out by a
	//   factor of 100; the value is really newtons per CENTIMETRE.
	//
	//   Cross-check in SI, independent of the cm arithmetic above: a corner carries
	//   1250 / 4 * 9.81 = 3065 N, and 3065 / 25000 = 0.123 m = 12.3 cm of static
	//   compression against 12 cm of available droop. Same answer, so the correction is
	//   right in both unit systems rather than right in one and lucky in the other.
	//
	// SPRING PRELOAD IS INERT TODAY, and is kept only so the authored intent survives:
	// FSimpleSuspensionSim::Simulate computes StiffnessForce from SpringRate alone and
	// never reads SpringPreload (SuspensionSystem.cpp:48-50). The value reaches Chaos
	// only through the CONSTRAINT suspension path
	// (ChaosWheeledVehicleMovementComponent.cpp:1268, 2816), and there the preload term
	// is commented out of the solver (PBDSuspensionConstraints.cpp:572) -- on top of
	// which this project runs with p.Vehicle.DisableConstraintSuspension. So the numbers
	// below change nothing at present. Do NOT tune ride height with them.
	// -----------------------------------------------------------------------

	/** Front spring rate, N/m. Softer than the rear so the platform pushes rather than snaps into oversteer. */
	inline constexpr float FrontSpringRateNPerM = 250.0f;
	/** Rear spring rate, N/m. */
	inline constexpr float RearSpringRateNPerM = 282.0f;

	/**
	 * Spring preload, NEWTONS. Authored intent: the car sits on its springs rather than
	 * on its bump stops at rest. INERT in the current configuration -- see the block
	 * above for why, and do not use it to move the ride height.
	 */
	inline constexpr float FrontSpringPreloadN = 55.0f;
	/** Rear spring preload, NEWTONS. */
	inline constexpr float RearSpringPreloadN = 62.0f;

	/** Damping ratio, dimensionless. Below 1 is underdamped; ~0.5 is the stable Phase 1 starting point. */
	inline constexpr float FrontDampingRatio = 0.55f;
	/** Rear damping ratio, dimensionless. Slightly higher: the driven axle must not pump under power. */
	inline constexpr float RearDampingRatio = 0.60f;

	/** Suspension travel above the rest position, CENTIMETRES. */
	inline constexpr float SuspensionMaxRaiseCm = 12.0f;
	/** Suspension travel below the rest position, CENTIMETRES. */
	inline constexpr float SuspensionMaxDropCm = 12.0f;

	/** Anti-roll bar contribution, dimensionless [0,1]. Rear stiffer than front trims mid-corner understeer. */
	inline constexpr float FrontRollbarScaling = 0.15f;
	/** Rear anti-roll bar contribution, dimensionless [0,1]. */
	inline constexpr float RearRollbarScaling = 0.20f;

	/** Chaos' load-transfer blend for the suspension, dimensionless [0,1]. */
	inline constexpr float WheelLoadRatio = 0.5f;

	/** Front brake torque per wheel, NEWTON-METRES. Front-biased, as every road-going brake system is. */
	inline constexpr float FrontBrakeTorqueNm = 1500.0f;
	/** Rear brake torque per wheel, NEWTON-METRES. */
	inline constexpr float RearBrakeTorqueNm = 1000.0f;

	/** Handbrake torque per REAR wheel, NEWTON-METRES. The front axle carries none. */
	inline constexpr float HandbrakeTorqueNm = 1600.0f;
}

/**
 * How the steered wheels resolve one steering input into two road-wheel angles.
 *
 * Mirrors Chaos' ESteeringType (ChaosWheeledVehicleMovementComponent.h) without
 * including it -- see the file header. ARacingVehiclePawn maps this
 * enumerator-for-enumerator. Append; do not renumber.
 */
UENUM(BlueprintType)
enum class EVehicleSteeringModel : uint8
{
	/** Both wheels steer by the same angle. Simplest, and visibly wrong at low speed on a wide track. */
	SingleAngle	UMETA(DisplayName = "Single angle"),
	/** The outer wheel steers less than the inner one by OuterInnerAngleRatio. */
	AngleRatio	UMETA(DisplayName = "Angle ratio"),
	/** True Ackermann geometry, derived by Chaos from the track width and wheelbase. */
	Ackermann	UMETA(DisplayName = "Ackermann")
};

/**
 * Which layer owns speed-sensitive steering. There must be exactly one.
 *
 * VEH-001 already ships a speed-sensitive steer scale on UVehicleInputConfigDataAsset
 * (ESteerSpeedScaleMode, curve domain KM/H) and Chaos ships its own
 * SteeringSetup.SteeringCurve (curve domain MPH, defaulted by
 * FVehicleSteeringConfig::InitDefaults to fall from 1.0 at 0 to 0.3 at 120). If both are
 * live they MULTIPLY: at 120 mph the driver would get 0.35 * 0.3 == 0.105 of lock while
 * both assets read as if they were applying a mild reduction. Nothing in the car says so.
 */
UENUM(BlueprintType)
enum class EVehicleSteerSpeedAuthority : uint8
{
	/**
	 * VEH-001's input layer owns it. The DEFAULT.
	 *
	 * Chosen because that curve is testable at the Smoke gate with no actor, no world and
	 * no physics step, and because it is in the project's own unit (km/h). The pawn writes
	 * a FLAT UNITY curve into Chaos so the engine default cannot apply underneath.
	 */
	InputLayer	UMETA(DisplayName = "Input layer (VEH-001 config, km/h)"),

	/**
	 * Chaos owns it, from SteerScaleBySpeedMphCurve below.
	 *
	 * Requires the input config's SteerSpeedScaleMode to be Off; the pawn reports the
	 * disagreement by name. Note the domain is MILES PER HOUR, which is Chaos' unit and
	 * not this project's -- hence the field name.
	 */
	ChaosCurve	UMETA(DisplayName = "Chaos steering curve (mph)")
};

/**
 * VEH-003: engine, transmission, brake, steering and suspension tune for one prototype
 * vehicle.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Vehicle Tune"))
class RACINGSIM_API UVehicleTuneDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Authors the default normalised torque curve.
	 *
	 * A FRuntimeFloatCurve has no keys until something adds them, and this asset's own
	 * validation REQUIRES two -- so a freshly created tune would fail its own validation
	 * if the curve were left to the property default. Chaos solves the same problem the
	 * same way (FVehicleEngineConfig::InitDefaults authors its curve in a constructor).
	 * Every other default is a member initialiser; only the curves need this.
	 */
	UVehicleTuneDataAsset();

	/** CORE-003 range pass over this class's flat numeric properties. */
	static TConstArrayView<RacingSim::Validation::FRacingPropertyRange> StaticRanges();

	/**
	 * Ranges, then the relationships no per-field clamp can express, then the curves.
	 *
	 * @param bCorrect  true to write safe values back over out-of-range numerics.
	 *                  RELATIONSHIP and CURVE failures are never corrected -- there is no
	 *                  single safe value for "third gear is shorter than second", and
	 *                  inventing one produces an asset that validates and cannot be
	 *                  driven. Same policy as the chassis asset and VEH-001's input config.
	 */
	RacingSim::Validation::FRacingValidationResult Validate(bool bCorrect);

	/** Const report-only form. Equivalent to Validate(false) on a duplicate; never mutates this asset. */
	RacingSim::Validation::FRacingValidationResult ValidateReadOnly() const;

	/**
	 * Fills FRacingSimVersionStamp::CarSpecVersion -- the hole CORE-002 opened and
	 * documented as "Populated by VEH-003" (RacingSimBuildId.h). Without this a lap time
	 * cannot name the tune it was set on, and FRacingSimVersionStamp::IsPublishable()
	 * refuses the stamp.
	 */
	FRacingContentVersion GetContentVersion() const;

	/**
	 * Hash of every authored tune value, INCLUDING the curve keys.
	 *
	 * Same caveats URaceRulesetDataAsset::ComputeContentHash records: it hashes bit
	 * patterns, so +0.0 and -0.0 differ, and a NaN hashes stably while comparing unequal
	 * to itself. Correction (code review, VEH-003 HIGH-2): Validate() only REPORTS
	 * non-finite values, in every DataAsset in this project -- it does not remove them,
	 * so one can still reach this hash. ARacingVehiclePawn::ApplyTuneAsset() is what
	 * refuses to hand an unusable torque curve to Chaos; this hash still reflects
	 * whatever was authored, valid or not. This detects accidental drift between builds;
	 * it is not a signature.
	 */
	uint32 ComputeContentHash() const;

	/** Peak of the AUTHORED normalised torque curve, dimensionless, before Chaos re-normalises it. 0 when the curve is unusable. Not what Chaos delivers -- see GetPeakTorqueNm(). */
	float GetPeakNormalisedTorque() const;

	/**
	 * May SteerScaleBySpeedMphCurve be handed to Chaos' FVehicleSteeringConfig?
	 *
	 * ADDED BY VEH-004, closing VEH-003 review pass 3 MEDIUM-1, which routed the fix
	 * forward with the instruction "mirror the torque-curve gate onto the steering
	 * curve BEFORE that asset can exist". Not reachable today only because no tune
	 * .uasset exists and the constructor default is valid; the moment one is authored,
	 * three separate faults in FVehicleSteeringConfig::FillSteeringSetup become live:
	 *
	 *   1. `Curve.GetRichCurveConst()->GetLastKey()` HARD-ASSERTS on an empty curve;
	 *   2. `Eval(X) / MaxValue` is the same divide-by-peak that VEH-003 HIGH-2 closed
	 *      for the torque curve -- a zero or non-finite peak puts NaN straight into
	 *      Chaos' steering config, a corrupting solver state rather than a bad feel;
	 *   3. `MaxX / NumSamples` divides by zero when the last key's TIME is 0, which a
	 *      single-key-at-origin curve produces and no per-key range check catches.
	 *
	 * ValidateReadOnly() already reports all three -- but only reports, exactly as the
	 * torque curve did before HIGH-2. This is the predicate the write is gated on.
	 *
	 * @param OutReason  names the specific fault when the result is false; untouched otherwise.
	 * @return true only when the curve has at least one key, every key time and value
	 *         is finite, the peak value is strictly positive, and the last key's time
	 *         is strictly positive.
	 */
	bool IsSteerSpeedCurveUsableByChaos(FString& OutReason) const;

	/**
	 * The peak engine torque Chaos TARGETS, NEWTON-METRES -- an upper bound, not a
	 * bit-exact runtime guarantee (softened on code review, repair cycle 2, MEDIUM-2).
	 *
	 * NOT `MaxTorqueNm * GetPeakNormalisedTorque()` -- that was this ticket's original,
	 * incorrect implementation, corrected on code review (VEH-003 HIGH-1).
	 * `FVehicleEngineConfig::FillEngineSetup` (ChaosWheeledVehicleMovementComponent.h)
	 * does `Eval(X) / MaxVal` before handing the curve to Chaos: it RE-NORMALISES the
	 * authored curve to its OWN peak, so the curve Chaos actually samples always TARGETS
	 * a peak of 1.0, regardless of what the authored curve's peak value was -- a curve
	 * authored peaking at 0.85 (perfectly legal; only a non-zero peak is required)
	 * targets the full MaxTorqueNm, not 0.85 of it. GetPeakNormalisedTorque() answers a
	 * different, authoring-time question (how the curve looks as authored) and must not
	 * be multiplied into this.
	 *
	 * The word "targets" is deliberate: `FillEngineSetup` resamples the curve at a fixed
	 * number of discrete points into an `FNormalisedGraph` before Chaos ever evaluates
	 * it, so the value actually delivered equals `MaxTorqueNm` only if a sample lands
	 * exactly on the authored peak -- for most curves it is `MaxTorqueNm` to within a
	 * fraction of a percent, closer for smoother curves and further for a sharply
	 * peaked one. This asset does not validate that the curve's key domain stays within
	 * `[0, MaxRpm]`; a peak authored above `MaxRpm` is not sampled and this accessor
	 * would then overstate what Chaos delivers by more than that rounding error.
	 */
	float GetPeakTorqueNm() const
	{
		// GetPeakNormalisedTorque() == 0 means the curve is unusable (empty, all-zero,
		// or every key non-finite) -- Chaos's own re-normalisation divides by zero in
		// that case and the engine delivers no torque at all, not MaxTorqueNm. Every
		// OTHER curve, regardless of its own peak value, delivers exactly MaxTorqueNm
		// once Chaos re-normalises it -- see the comment above.
		return (GetPeakNormalisedTorque() > 0.0f) ? MaxTorqueNm : 0.0f;
	}

	/**
	 * Overall drive ratio in the given forward gear (1-based, as a driver counts them),
	 * dimensionless: gear ratio * final drive.
	 *
	 * Returns 0 for an out-of-range gear rather than asserting -- this is reachable from
	 * Blueprint.
	 */
	UFUNCTION(BlueprintPure, Category = "Vehicle|Tune")
	float GetOverallRatioForGear(int32 ForwardGear) const;

	// -- Identity -----------------------------------------------------------

	/**
	 * Stable identifier of this tune, e.g. "Car.Prototype.Meridian.Base". NAME_None means
	 * unpopulated, which FRacingContentVersion::IsPopulated rejects and Validate() reports.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName TuneId = TEXT("Car.Prototype.Meridian.Base");

	/**
	 * Layout version of the C++ that reads this asset. Hand-bumped when fields are added
	 * or reinterpreted -- NOT when a value is retuned; that is what ContentHash is for.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (ClampMin = "1", ClampMax = "1000"))
	int32 TuneSchemaVersion = 1;

	// -- Engine -------------------------------------------------------------

	/**
	 * NORMALISED torque [0,1] against engine speed in RPM.
	 *
	 * Chaos does NOT use this curve's authored scale directly: `FillEngineSetup`
	 * (ChaosWheeledVehicleMovementComponent.h) evaluates it and divides by the curve's
	 * OWN peak before multiplying by MaxTorqueNm, so the curve Chaos actually samples
	 * always peaks at exactly 1.0 -- see GetPeakTorqueNm(). An empty curve is still a
	 * car with no torque at any RPM that starts, revs and reports a healthy MaxTorque.
	 * Required: at least two keys, finite, non-negative RPM domain, values within
	 * [0,1], non-zero peak.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine")
	FRuntimeFloatCurve NormalisedTorqueCurve;

	/** Peak engine torque, NEWTON-METRES, before the curve scales it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "50.0", ClampMax = "2000.0"))
	float MaxTorqueNm = 420.0f;

	/** Redline, RPM. Must exceed IdleRpm and must not be below ChangeUpRpm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "1000.0", ClampMax = "20000.0"))
	float MaxRpm = 7800.0f;

	/** Idle speed in neutral/stationary, RPM. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "200.0", ClampMax = "5000.0"))
	float IdleRpm = 950.0f;

	/**
	 * Engine braking with the throttle closed, dimensionless.
	 *
	 * 0 is a car that coasts forever off-throttle, which is not a tune, it is a
	 * drivetrain that has been disconnected without anyone saying so.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EngineBrakeEffect = 0.06f;

	/** Rotational inertia term governing how fast the engine revs UP. Chaos' unit, dimensionless here. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.01", ClampMax = "100.0"))
	float EngineRevUpMoi = 5.0f;

	/** How fast the engine revs DOWN. Chaos' unit, dimensionless here. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.01", ClampMax = "2000.0"))
	float EngineRevDownRate = 600.0f;

	// -- Transmission -------------------------------------------------------

	/**
	 * Forward gear ratios, dimensionless, FIRST GEAR FIRST.
	 *
	 * Must be strictly DECREASING. A set whose third gear is shorter than its second is
	 * individually plausible and collectively impossible: the car would accelerate harder
	 * after an upshift, which reads as a tyre or torque bug for as long as anyone lets it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transmission")
	TArray<float> ForwardGearRatios = {3.35f, 2.18f, 1.57f, 1.19f, 0.95f, 0.78f};

	/** Reverse gear ratios, dimensionless, as POSITIVE magnitudes -- Chaos applies the sign. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transmission")
	TArray<float> ReverseGearRatios = {2.90f};

	/** Final drive ratio, dimensionless. Multiplies every gear. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transmission", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float FinalDriveRatio = 3.70f;

	/** Automatic upshift point, RPM. Must exceed ChangeDownRpm and must not exceed MaxRpm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transmission", meta = (ClampMin = "500.0", ClampMax = "20000.0"))
	float ChangeUpRpm = 7200.0f;

	/** Automatic downshift point, RPM. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transmission", meta = (ClampMin = "300.0", ClampMax = "20000.0"))
	float ChangeDownRpm = 3200.0f;

	/** Time a shift takes, SECONDS. 0 is legal (an instant dual-clutch abstraction) but is a choice, not a default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transmission", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float GearChangeTimeSeconds = 0.18f;

	/** Fraction of engine torque reaching the wheels, dimensionless (0,1]. 1.0 is a lossless driveline. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transmission", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float TransmissionEfficiency = 0.94f;

	// -- Brakes -------------------------------------------------------------
	//
	// Declared here as the CONTRACT and validated against the wheel classes rather than
	// written to them -- see PrototypeTuneDefaults above and PrototypeVehicleWheel.h.

	/** Brake torque at EACH front wheel, NEWTON-METRES. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Brakes", meta = (ClampMin = "100.0", ClampMax = "10000.0"))
	float FrontBrakeTorqueNm = RacingSim::Vehicle::PrototypeTuneDefaults::FrontBrakeTorqueNm;

	/** Brake torque at EACH rear wheel, NEWTON-METRES. Must not exceed the front; see Validate(). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Brakes", meta = (ClampMin = "50.0", ClampMax = "10000.0"))
	float RearBrakeTorqueNm = RacingSim::Vehicle::PrototypeTuneDefaults::RearBrakeTorqueNm;

	/** Handbrake torque at each REAR wheel, NEWTON-METRES. The front axle carries none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Brakes", meta = (ClampMin = "100.0", ClampMax = "10000.0"))
	float HandbrakeTorqueNm = RacingSim::Vehicle::PrototypeTuneDefaults::HandbrakeTorqueNm;

	// -- Steering -----------------------------------------------------------

	/** How two road-wheel angles are derived from one steering input. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering")
	EVehicleSteeringModel SteeringModel = EVehicleSteeringModel::AngleRatio;

	/**
	 * Outer-to-inner road wheel angle ratio, dimensionless. Chaos reads it ONLY under
	 * AngleRatio, so Validate() reports a non-default value under the other two models
	 * rather than letting an author believe they changed something -- the same policy the
	 * chassis asset applies to FrontRearTorqueSplit outside AWD.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float OuterInnerAngleRatio = 0.7f;

	/** Which layer owns speed-sensitive steering. There must be exactly one; see the enum. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering")
	EVehicleSteerSpeedAuthority SteerSpeedAuthority = EVehicleSteerSpeedAuthority::InputLayer;

	/**
	 * Steering authority against road speed in MILES PER HOUR. Chaos' domain, not this
	 * project's -- CmSToMPH is applied at ChaosWheeledVehicleMovementComponent.cpp:738.
	 *
	 * Required, and validated, only when SteerSpeedAuthority is ChaosCurve. Ignored
	 * otherwise, and the pawn overwrites Chaos' copy with a flat unity curve so the engine
	 * default cannot apply underneath VEH-001's km/h curve.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering")
	FRuntimeFloatCurve SteerScaleBySpeedMphCurve;

	// -- Suspension ---------------------------------------------------------
	//
	// Same contract-and-cross-check treatment as the brake torques.

	/** Front spring rate, NEWTONS PER METRE. Chaos' unit; note the project stores distance in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension", meta = (ClampMin = "1.0", ClampMax = "1000.0"))
	float FrontSpringRateNPerM = RacingSim::Vehicle::PrototypeTuneDefaults::FrontSpringRateNPerM;

	/** Rear spring rate, NEWTONS PER METRE. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension", meta = (ClampMin = "1.0", ClampMax = "1000.0"))
	float RearSpringRateNPerM = RacingSim::Vehicle::PrototypeTuneDefaults::RearSpringRateNPerM;

	/** Front spring preload, NEWTONS. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float FrontSpringPreloadN = RacingSim::Vehicle::PrototypeTuneDefaults::FrontSpringPreloadN;

	/** Rear spring preload, NEWTONS. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float RearSpringPreloadN = RacingSim::Vehicle::PrototypeTuneDefaults::RearSpringPreloadN;

	/**
	 * Front damping ratio, dimensionless.
	 *
	 * Clamped to [0.05, 1.5] rather than [0,x]: zero damping is an undamped spring, i.e.
	 * a car that oscillates until the integrator gives up, which VEH-004's runaway-energy
	 * detection would report as an instability with no obvious cause.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension", meta = (ClampMin = "0.05", ClampMax = "1.5"))
	float FrontDampingRatio = RacingSim::Vehicle::PrototypeTuneDefaults::FrontDampingRatio;

	/** Rear damping ratio, dimensionless. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension", meta = (ClampMin = "0.05", ClampMax = "1.5"))
	float RearDampingRatio = RacingSim::Vehicle::PrototypeTuneDefaults::RearDampingRatio;

	/** Travel above the rest position, CENTIMETRES. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension", meta = (ClampMin = "0.5", ClampMax = "60.0"))
	float SuspensionMaxRaiseCm = RacingSim::Vehicle::PrototypeTuneDefaults::SuspensionMaxRaiseCm;

	/** Travel below the rest position, CENTIMETRES. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension", meta = (ClampMin = "0.5", ClampMax = "60.0"))
	float SuspensionMaxDropCm = RacingSim::Vehicle::PrototypeTuneDefaults::SuspensionMaxDropCm;

	/** Front anti-roll bar contribution, dimensionless [0,1]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FrontRollbarScaling = RacingSim::Vehicle::PrototypeTuneDefaults::FrontRollbarScaling;

	/** Rear anti-roll bar contribution, dimensionless [0,1]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RearRollbarScaling = RacingSim::Vehicle::PrototypeTuneDefaults::RearRollbarScaling;

	/** Chaos' suspension load-transfer blend, dimensionless [0,1]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WheelLoadRatio = RacingSim::Vehicle::PrototypeTuneDefaults::WheelLoadRatio;
};
