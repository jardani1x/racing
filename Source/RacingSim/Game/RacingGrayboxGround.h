// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RacingGrayboxGround.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 * RACE-005: a flat, static driving surface under a graybox circuit.
 *
 * The graybox map (L_Meridian_Graybox) holds a track definition and nothing a wheel can
 * stand on, so a car spawned on its grid falls forever. Road meshes and landscape are
 * TRACK-003's; until then ARacingGameMode spawns one of these, sized from the centerline.
 *
 * COLLISION IS THE BOX, NOT THE MESH. A UBoxComponent with the BlockAll profile, query and
 * physics, never simulating -- the same ground VehicleManoeuvreFixture drives on, so the
 * graybox and the vehicle suite share one tested surface. BlockAll matters: Chaos vehicle
 * suspension is a raycast per wheel, and a profile that only participated in rigid-body
 * collision would give wheels that find no ground.
 *
 * THE MESH IS VISIBILITY ONLY: the engine's basic cube, scaled to the box, collision off.
 * Its cook inclusion is unverified until packaging (TRACK-003).
 */
UCLASS(NotPlaceable, Transient)
class RACINGSIM_API ARacingGrayboxGround : public AActor
{
	GENERATED_BODY()

public:
	ARacingGrayboxGround();

	/** Slab thickness, cm. Thick enough that a car at speed cannot tunnel through in one substep. */
	static constexpr double SlabThicknessCm = 200.0;

	/**
	 * Size and place the slab: its TOP face at TopZCm, covering XYBounds grown by MarginCm
	 * on every side. Safe to call again; the last call wins.
	 */
	void ConfigureSurface(const FBox2D& XYBounds, double TopZCm, double MarginCm);

	/** World Z of the top face, cm. */
	double GetTopZCm() const;

	UBoxComponent* GetCollisionBox() const { return CollisionBox; }
	UStaticMeshComponent* GetVisualMesh() const { return VisualMesh; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Graybox")
	TObjectPtr<UBoxComponent> CollisionBox;

	UPROPERTY(VisibleAnywhere, Category = "Graybox")
	TObjectPtr<UStaticMeshComponent> VisualMesh;
};
