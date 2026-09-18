// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RacingGameMode.generated.h"

class ARaceDirector;
class ARacingGrayboxGround;
class ARacingVehiclePawn;
class ATrackDefinitionActor;
class URaceRulesetDataAsset;
class UVehicleChassisDataAsset;
class UVehicleTuneDataAsset;

/**
 * RACE-005: the composition root for one playable race session.
 *
 * Game/ is the one layer allowed to include Core/, Vehicle/, Race/ and UI/ together, and
 * nothing includes Game/. That keeps Race free of Vehicle and UI and UI free of Race
 * (Docs/01-Architecture.md) while still letting one class wire them. (Vehicle already
 * reaches into Race for safe reset and the car-spec publish; that predates this layer.)
 *
 * ORDER OF EVENTS ON LoadMap, which dictates where each step lives:
 *   1. SetGameMode spawns this actor; PreInitializeComponents spawns the ARaceDirector.
 *      It must exist before step 2, which is why it is not spawned in BeginPlay.
 *   2. The local player logs in: Login spawns the controller, PostLogin calls
 *      RestartPlayer. The pawn is placed on the track's grid slot GridSlotIndex, raised
 *      GridSpawnHeightCm, possessed, and registered with the director.
 *   3. World BeginPlay: every actor's BeginPlay, including the pawn's (which applies its
 *      chassis and tune) and the director's (which starts the session). StartPlay then
 *      publishes the car spec of a pawn that could not publish at step 2 -- the pawn only
 *      knows which tune it actually applied once its BeginPlay has run.
 *
 * GRAYBOX DEFAULTS. A null DefaultChassisAsset/DefaultTuneAsset becomes a transient
 * asset with the C++ defaults, with one warning. That is a composition-root decision
 * until a car-content ticket authors .uassets; the pawn itself still refuses a null
 * chassis. A pawn class that sets its own assets keeps them.
 */
UCLASS()
class RACINGSIM_API ARacingGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARacingGameMode();

	/** Director class to spawn. Must be ARaceDirector or a subclass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Session")
	TSubclassOf<ARaceDirector> DirectorClass;

	/** Handed to the director before its setup. Null uses the director's transient default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Session")
	TObjectPtr<URaceRulesetDataAsset> Ruleset;

	/** Grid slot the local player's car is placed on; 0 is pole. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Session", meta = (ClampMin = "0"))
	int32 GridSlotIndex = 0;

	/**
	 * Height above the grid slot pose the car spawns at, cm. Matches
	 * VehicleManoeuvreFixture::SpawnHeightCm: high enough that the wheels start clear of
	 * the surface and settle onto it, low enough that the drop is not a landing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Session", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	double GridSpawnHeightCm = 90.0;

	/** Spawn a flat BlockAll slab under the circuit. Graybox only; TRACK-003 replaces it with road content. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Graybox")
	bool bSpawnGrayboxGround = true;

	/** How far the graybox slab extends past the centerline's XY bounds on every side, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Graybox", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	double GrayboxGroundMarginCm = 5000.0;

	/** Chassis given to a spawned vehicle whose class leaves ChassisAsset null. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Vehicle")
	TObjectPtr<UVehicleChassisDataAsset> DefaultChassisAsset;

	/** Tune given to a spawned vehicle whose class leaves TuneAsset null. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Vehicle")
	TObjectPtr<UVehicleTuneDataAsset> DefaultTuneAsset;

	ARaceDirector* GetRaceDirector() const { return RaceDirector; }
	ARacingGrayboxGround* GetGrayboxGround() const { return GrayboxGround; }

	//~ Begin AGameModeBase interface
	virtual void PreInitializeComponents() override;
	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;
	//~ End AGameModeBase interface

private:
	/** Spawn the graybox slab once, sized from Track's centerline. */
	void EnsureGrayboxGround(const ATrackDefinitionActor* Track);

	/** Input device now, car spec now or at StartPlay, then register with the director. */
	void OnCompetitorPlaced(APawn* Pawn, double GridDistanceCm);

	UPROPERTY(Transient)
	TObjectPtr<ARaceDirector> RaceDirector;

	UPROPERTY(Transient)
	TObjectPtr<ARacingGrayboxGround> GrayboxGround;

	/** Created once, only when DefaultChassisAsset is null and a pawn needs one. */
	UPROPERTY(Transient)
	TObjectPtr<UVehicleChassisDataAsset> TransientChassisAsset;

	/** Created once, only when DefaultTuneAsset is null and a pawn needs one. */
	UPROPERTY(Transient)
	TObjectPtr<UVehicleTuneDataAsset> TransientTuneAsset;

	/** A placed vehicle whose BeginPlay had not run yet, so its car spec is published at StartPlay. */
	TWeakObjectPtr<ARacingVehiclePawn> PendingCarSpecPawn;
};
