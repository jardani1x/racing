// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "ChaosWheeledVehicleMovementComponent.h"
#include "CoreMinimal.h"
#include "Core/RacingSimTypes.h"
#include "GameFramework/Pawn.h"
#include "Vehicle/VehicleChaosInputMapping.h"
#include "Vehicle/VehicleChassisDataAsset.h"
#include "RacingVehiclePawn.generated.h"

class UBoxComponent;
class UVehicleInputComponent;
class UVehicleTuneDataAsset;
struct FVehicleInputCommand;

/**
 * VEH-002: the first drivable prototype pawn.
 *
 * ---------------------------------------------------------------------------
 * What this ticket owns, and what it deliberately does not
 * ---------------------------------------------------------------------------
 *
 * This pawn is the ONE place that maps UVehicleChassisDataAsset's project-owned
 * EVehicleDrivetrainLayout onto Chaos' own EVehicleDifferential (see the DataAsset's
 * header for why that mapping lives here and not on the asset). It builds the
 * UChaosWheeledVehicleMovementComponent's WheelSetups from the chassis asset's
 * geometry and consumes VEH-001's FVehicleInputCommand every Tick. It does NOT own
 * the tune (VEH-003: torque curve, gear ratios, brake/steer response -- everything
 * this pawn sets below is a Phase-1-drivable placeholder, not a tuned value) and does
 * NOT own the reset pose (VEH-005).
 *
 * ---------------------------------------------------------------------------
 * Original, unbranded prototype, per CLAUDE.md
 * ---------------------------------------------------------------------------
 *
 * The chassis collision is a primitive UBoxComponent sized from the DataAsset's
 * half-extents -- no authored mesh, no licensed geometry, nothing that needs an
 * asset-ownership claim or a license-ledger entry. A visual mesh is a later content
 * task once Docs/13-AssetLicenseLedger.md records one.
 */
UCLASS()
class RACINGSIM_API ARacingVehiclePawn : public APawn
{
	GENERATED_BODY()

public:
	ARacingVehiclePawn(const FObjectInitializer& ObjectInitializer);

	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** The chassis asset in force. Set in the editor per-Blueprint; a null asset is refused at BeginPlay, not silently substituted. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vehicle")
	TObjectPtr<UVehicleChassisDataAsset> ChassisAsset;

	/**
	 * VEH-003 tune asset: engine, transmission, brakes, steering setup, suspension.
	 *
	 * Separate from ChassisAsset on purpose -- one car's geometry can carry several tunes
	 * (and one tune is meaningless on different geometry), and the two answer different
	 * questions. See UVehicleTuneDataAsset's header. A null tune is reported at BeginPlay
	 * and leaves Chaos' own defaults in place; it is not silently substituted.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vehicle")
	TObjectPtr<UVehicleTuneDataAsset> TuneAsset;

	/** Enhanced Input config, forwarded to the input component at possession. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vehicle")
	TObjectPtr<class UVehicleInputConfigDataAsset> InputConfigAsset;

	/** Device profile this pawn's input component initialises with. Runtime device switching is a later ticket's scope; see VEH-001's routed MEDIUM-1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vehicle")
	ERacingInputDeviceType InitialInputDeviceType = ERacingInputDeviceType::Keyboard;

	/** True once ChassisAsset has been applied to the movement component and wheel setups. False for a pawn spawned with no chassis (a validation failure, not a crash). */
	UFUNCTION(BlueprintPure, Category = "Vehicle")
	bool IsChassisApplied() const
	{
		return bChassisApplied;
	}

	/** The Chaos movement component, exposed for VEH-004's telemetry and VEH-003's tune application -- neither owns this Tick, both read from it. */
	UFUNCTION(BlueprintPure, Category = "Vehicle")
	UChaosWheeledVehicleMovementComponent* GetVehicleMovementComponent() const
	{
		return VehicleMovementComponent;
	}

protected:
	virtual void BeginPlay() override;

private:
	/** Chassis collision primitive. Root component; sized from ChassisAsset at BeginPlay. */
	UPROPERTY(VisibleAnywhere, Category = "Vehicle")
	TObjectPtr<UBoxComponent> ChassisCollision;

	UPROPERTY(VisibleAnywhere, Category = "Vehicle")
	TObjectPtr<UChaosWheeledVehicleMovementComponent> VehicleMovementComponent;

	/** VEH-001's input contract. Owned here rather than by the controller so a spectated or AI-driven pawn still has one. */
	UPROPERTY(VisibleAnywhere, Category = "Vehicle")
	TObjectPtr<UVehicleInputComponent> VehicleInputComp;

	/**
	 * Builds WheelSetups and Mass/aerodynamics from ChassisAsset, and sets the Chaos
	 * differential from ChassisAsset's EVehicleDrivetrainLayout -- the one mapping this
	 * pawn exists to own. Called once, from BeginPlay. Idempotent guard via
	 * bChassisApplied: PossessedBy must not re-apply the chassis on every possession.
	 */
	void ApplyChassisAsset();

	/**
	 * VEH-003: writes TuneAsset onto the movement component's EngineSetup,
	 * TransmissionSetup and SteeringSetup, and maps EVehicleSteeringModel onto Chaos'
	 * ESteeringType -- the second project-enum-to-Chaos-enum mapping this pawn owns.
	 *
	 * Called from ApplyChassisAsset() immediately BEFORE RecreatePhysicsState(), because
	 * that call is what pushes the whole configuration into Chaos; a tune written after
	 * it would not take effect until something else recreated the state. Suspension and
	 * brake torques are NOT written here -- Chaos reads those from the wheel class default
	 * object, so they are cross-checked instead (PrototypeVehicleWheel.h).
	 *
	 * Guarded by its own bTuneApplied (code review, VEH-003 MEDIUM-4) -- previously relied
	 * only on its caller's bChassisApplied guard, which is correct today because
	 * ApplyChassisAsset() is this function's only caller, but left this function without
	 * a guard of its own despite being documented as "guarded and idempotent".
	 */
	void ApplyTuneAsset();

	/** Maps FVehicleInputCommand onto the movement component's SetThrottleInput/SetBrakeInput/SetSteeringInput/SetHandbrakeInput. The one Tick-time consumer of VEH-001's contract. */
	void ApplyInputCommand(const FVehicleInputCommand& Command);

	bool bChassisApplied = false;
	bool bTuneApplied = false;
};
