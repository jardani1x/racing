// Copyright RacingSim. All Rights Reserved.

#include "Game/RacingGrayboxGround.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

namespace RacingGrayboxGroundPrivate
{
	/** The engine basic cube is 100 cm on a side, centred on its origin. */
	constexpr double EngineCubeSizeCm = 100.0;
}

ARacingGrayboxGround::ARacingGrayboxGround()
{
	PrimaryActorTick.bCanEverTick = false;

	// Movable rather than Static: the slab is sized and placed after spawn, and a Static
	// component refuses to move once registered. It never moves after ConfigureSurface.
	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	CollisionBox->SetMobility(EComponentMobility::Movable);
	CollisionBox->SetCollisionProfileName(TEXT("BlockAll"));
	CollisionBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionBox->SetSimulatePhysics(false);
	CollisionBox->SetHiddenInGame(true);
	RootComponent = CollisionBox;

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(CollisionBox);
	VisualMesh->SetMobility(EComponentMobility::Movable);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Constructor-time load of an engine asset: resolved once when the CDO is built, never
	// during a race.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		VisualMesh->SetStaticMesh(CubeFinder.Object);
	}
}

void ARacingGrayboxGround::ConfigureSurface(const FBox2D& XYBounds, const double TopZCm, const double MarginCm)
{
	const FVector2D Center = XYBounds.bIsValid ? XYBounds.GetCenter() : FVector2D::ZeroVector;
	const FVector2D HalfSize = (XYBounds.bIsValid ? XYBounds.GetExtent() : FVector2D::ZeroVector)
		+ FVector2D(FMath::Max(MarginCm, 0.0));
	const double HalfThicknessCm = SlabThicknessCm * 0.5;

	CollisionBox->SetBoxExtent(FVector(HalfSize.X, HalfSize.Y, HalfThicknessCm));
	SetActorLocation(FVector(Center.X, Center.Y, TopZCm - HalfThicknessCm));

	VisualMesh->SetRelativeScale3D(FVector(
		2.0 * HalfSize.X / RacingGrayboxGroundPrivate::EngineCubeSizeCm,
		2.0 * HalfSize.Y / RacingGrayboxGroundPrivate::EngineCubeSizeCm,
		SlabThicknessCm / RacingGrayboxGroundPrivate::EngineCubeSizeCm));
}

double ARacingGrayboxGround::GetTopZCm() const
{
	return CollisionBox->GetComponentLocation().Z + CollisionBox->GetScaledBoxExtent().Z;
}
