// Copyright RacingSim. All Rights Reserved.

#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

#include "ChaosWheeledVehicleMovementComponent.h"

#include "Vehicle/RacingVehiclePawn.h"
#include "Vehicle/VehicleChassisDataAsset.h"
#include "Vehicle/VehicleTuneDataAsset.h"

/**
 * VEH-006 criterion 0: does a PHYSICS-STEPPING world exist in this harness at all?
 *
 * ---------------------------------------------------------------------------
 * Why this file is separate from the manoeuvre suite, and why it is small
 * ---------------------------------------------------------------------------
 *
 * Nothing in this repository has ever obtained a world that begins play and steps
 * physics. TrackPrototypeLevelSpec.cpp obtains a world by LoadPackage +
 * FindWorldInPackage and asserts, deliberately, that it NEVER began play. TRACK-001
 * recorded that UWorld::CreateWorld(EWorldType::Game) died with an access violation --
 * but it recorded that from the SmokeFilter phase, and TRACK-002 then proved that the
 * "actors are impossible in this harness" conclusion drawn from that phase was wrong:
 * the cause was FEngineLoop::PreInit running SmokeFilter tests before
 * RegisterEngineElements(), not anything about worlds. So the CreateWorld result has to
 * be re-established at ProductFilter phase rather than inherited.
 *
 * A failure here could be a crash of the whole run with no index.json, not a red test.
 * That is why this probe is one small file that can be invoked alone by name: a crash
 * costs one probe run and is itself the recorded evidence, instead of taking the entire
 * manoeuvre suite down with it.
 *
 * ProductFilter, for the reason Run-AutomationFilter.ps1's header states at length: a
 * SmokeFilter test that constructs a non-template UActorComponent asserts inside
 * UTypedElementRegistry and kills the process.
 *
 * ---------------------------------------------------------------------------
 * What the earlier runs of this probe established, and why the world is built
 * the way it is below
 * ---------------------------------------------------------------------------
 *
 * The ticket requires that the world-construction path be established by running, and
 * that every path tried and rejected be recorded with the evidence that rejected it.
 * Three runs were needed. All three are kept here because each one eliminated a
 * hypothesis that would otherwise be re-tried by the next author.
 *
 * Run 1 -- hand-rolled UWorld::CreateWorld + InitializeActorsForPlay + BeginPlay.
 *   CreateWorld did NOT crash at ProductFilter phase and the world had a non-null
 *   physics scene, so TRACK-001's access violation is confirmed to be a Smoke-phase
 *   artefact and nothing more. But:
 *     "Expected 'The created world has begun play' to be true."
 *   plus every consequence of it: no pawn BeginPlay, so no chassis, so zero wheels.
 *   UWorld::BeginPlay() does not set bBegunPlay; AGameStateBase::HandleBeginPlay does,
 *   and a world with no GameMode has no GameState to do it.
 *
 * Run 2 -- as run 1 plus an explicit World->SetBegunPlay(true) before spawning.
 *   Begun play, chassis applied, WheelSetups.Num() == 4. All of that works. But the
 *   pawn fell exactly 0.27 cm over 30 steps of 1/60 s, and
 *   0.5 * 980 * (1/60)^2 = 0.272 cm is precisely ONE semi-implicit Euler step.
 *
 * Run 3 -- as run 2 plus a bare simulating UBoxComponent as a control, and per-step
 *   logging of position and velocity. Decisive:
 *     Step  0: worldTime = 0.0167, control Z = -0.272, control Vz = -16.331
 *     Step 29: worldTime = 0.5000, control Z = -0.272, control Vz = -16.331
 *   World time advanced, both bodies stayed awake, and both position AND velocity were
 *   frozen at exactly one step's worth. So this is not vehicle sleep, not the vehicle
 *   component, and not a missing pull of results to the game thread -- the Chaos solver
 *   advanced once and then never again, with bTickPhysicsAsync = false.
 *
 * Run 4 -- this file. The cause is that a hand-rolled tick loop is not a frame. Chaos
 *   marshals game-thread state into the solver per frame, and the engine's own test
 *   helper FTestWorldWrapper::TickTestWorld (AutomationCommon.cpp:220) increments
 *   GFrameCounter after every tick specifically to "emulate gameplay". Its
 *   CreateTestWorld also builds a UGameInstance and its BeginPlayInTestWorld calls
 *   SetGameMode(URL), which it documents as "required to actually forward actor
 *   BeginPlay" -- the same problem run 1 hit, solved the supported way rather than by
 *   poking bBegunPlay directly.
 *
 * So the established path is: use the engine's FTestWorldWrapper. Not a hand-rolled
 * world. That is the finding this probe exists to produce.
 */

namespace VehicleManoeuvreWorldProbePrivate
{
	/** Fixed step. 1/60 s, the step the manoeuvre suite will replay at. */
	constexpr float ProbeStepSeconds = 1.0f / 60.0f;

	/** Ticks to run. Enough for gravity to move the chassis measurably, no more. */
	constexpr int32 ProbeStepCount = 30;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleManoeuvreWorldProbeTest,
	"RacingSim.Vehicle.ManoeuvreWorldProbe",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::ProductFilter)

bool FVehicleManoeuvreWorldProbeTest::RunTest(const FString& Parameters)
{
	using namespace VehicleManoeuvreWorldProbePrivate;

	if (GEngine == nullptr)
	{
		AddError(TEXT("GEngine is null; this test cannot be running after UEngine::Init."));
		return false;
	}

	// A car dropped from 200 cm with no ground under it IS a runaway: VEH-004's failure
	// detector logs an Error the moment free-fall speed leaves its acceleration envelope,
	// and an unhandled Error fails the test. That log line is a correct detection, not a
	// defect, and it is also the first end-to-end evidence that the VEH-004 detector fires
	// against real physics rather than against a hand-fed telemetry array. Occurrences 0
	// means "any number of times", since how many frames of free fall trip it is a
	// property of the envelope, not something this probe should pin down.
	AddExpectedErrorPlain(TEXT("VEH-004 failure detected"),
		EAutomationExpectedErrorFlags::Contains, /*Occurrences*/ 0);

	if (const UPhysicsSettings* PhysicsSettings = UPhysicsSettings::Get())
	{
		// Recorded because the run-3 diagnosis turns on it: with async ticking off, a
		// frozen solver cannot be blamed on results lagging behind on a physics thread.
		AddInfo(FString::Printf(
			TEXT("PhysicsSettings: bTickPhysicsAsync = %s, bSubstepping = %s, MaxPhysicsDeltaTime = %.4f, MaxSubsteps = %d."),
			PhysicsSettings->bTickPhysicsAsync ? TEXT("true") : TEXT("false"),
			PhysicsSettings->bSubstepping ? TEXT("true") : TEXT("false"),
			PhysicsSettings->MaxPhysicsDeltaTime,
			PhysicsSettings->MaxSubsteps));
	}

	// -- Step 1: the world, via the engine's own test helper ---------------------
	//
	// FTestWorldWrapper roots the world, creates the world context and a UGameInstance,
	// spawns a GameMode on BeginPlay, ticks with a frame counter, and tears all of it
	// down again. Every one of those is a step this probe got wrong by hand first.
	FTestWorldWrapper WorldWrapper;
	if (!WorldWrapper.CreateTestWorld(EWorldType::Game))
	{
		WorldWrapper.ForwardErrorMessages(this);
		AddError(TEXT("FTestWorldWrapper::CreateTestWorld(EWorldType::Game) failed."));
		return false;
	}

	UWorld* World = WorldWrapper.GetTestWorld();
	if (World == nullptr)
	{
		AddError(TEXT("FTestWorldWrapper produced a null world."));
		return false;
	}
	AddInfo(TEXT("FTestWorldWrapper::CreateTestWorld(EWorldType::Game) returned a world."));

	// -- Step 2: does it have a physics scene? -----------------------------------
	//
	// The whole ticket rests on this one pointer. A world without a physics scene
	// ticks actors and moves nothing, and every manoeuvre assertion downstream would
	// read as "the car did not accelerate" rather than as "there is no physics".
	const bool bHasPhysicsScene = World->GetPhysicsScene() != nullptr;
	AddInfo(FString::Printf(TEXT("World->GetPhysicsScene() is %s."),
		bHasPhysicsScene ? TEXT("non-null") : TEXT("NULL")));
	TestTrue(TEXT("The created world has a physics scene"), bHasPhysicsScene);

	if (!WorldWrapper.BeginPlayInTestWorld())
	{
		WorldWrapper.ForwardErrorMessages(this);
		AddError(TEXT("FTestWorldWrapper::BeginPlayInTestWorld failed."));
		WorldWrapper.DestroyTestWorld(/*bForceGarbageCollect*/ false);
		return false;
	}

	AddInfo(FString::Printf(TEXT("Begun play; HasBegunPlay = %s, gravity Z = %.2f cm/s^2, GameMode = %s."),
		World->HasBegunPlay() ? TEXT("true") : TEXT("false"),
		World->GetGravityZ(),
		World->GetAuthGameMode() != nullptr ? TEXT("present") : TEXT("absent")));
	TestTrue(TEXT("The created world has begun play"), World->HasBegunPlay());

	// -- Step 3: a pawn, spawned deferred so its assets exist before BeginPlay ----
	//
	// ARacingVehiclePawn::BeginPlay refuses a null chassis rather than substituting a
	// default, so the assets must be attached between SpawnActorDeferred and
	// FinishSpawningActor. Transient assets, matching VehicleChassisSpec.cpp:48.
	UVehicleChassisDataAsset* Chassis = NewObject<UVehicleChassisDataAsset>(GetTransientPackage());
	UVehicleTuneDataAsset* Tune = NewObject<UVehicleTuneDataAsset>(GetTransientPackage());
	TStrongObjectPtr<UVehicleChassisDataAsset> ChassisGuard(Chassis);
	TStrongObjectPtr<UVehicleTuneDataAsset> TuneGuard(Tune);

	const FTransform SpawnTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 200.0));

	ARacingVehiclePawn* Pawn = World->SpawnActorDeferred<ARacingVehiclePawn>(
		ARacingVehiclePawn::StaticClass(), SpawnTransform);
	if (Pawn == nullptr)
	{
		AddError(TEXT("SpawnActorDeferred<ARacingVehiclePawn> returned null."));
		WorldWrapper.DestroyTestWorld(/*bForceGarbageCollect*/ false);
		return false;
	}

	Pawn->ChassisAsset = Chassis;
	Pawn->TuneAsset = Tune;
	UGameplayStatics::FinishSpawningActor(Pawn, SpawnTransform);
	AddInfo(TEXT("ARacingVehiclePawn spawned and finished."));

	TestTrue(TEXT("The spawned pawn applied its chassis asset"), Pawn->IsChassisApplied());

	if (const UChaosWheeledVehicleMovementComponent* Movement =
			Pawn->FindComponentByClass<UChaosWheeledVehicleMovementComponent>())
	{
		AddInfo(FString::Printf(TEXT("WheelSetups.Num() = %d."), Movement->WheelSetups.Num()));
		TestEqual(TEXT("The pawn has four wheel setups"), Movement->WheelSetups.Num(), 4);
	}
	else
	{
		AddError(TEXT("The spawned pawn has no UChaosWheeledVehicleMovementComponent."));
	}

	// -- Step 3b: a control body that is NOT a vehicle ---------------------------
	//
	// Kept from run 3. It is what proved the run-2 freeze was the solver rather than
	// the vehicle component, and it stays as a permanent discriminator: if a future
	// change makes the pawn stop moving, this box says immediately whether the world
	// or the vehicle is at fault.
	AActor* ControlActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
	UBoxComponent* ControlBox = nullptr;
	if (ControlActor != nullptr)
	{
		ControlBox = NewObject<UBoxComponent>(ControlActor);
		ControlBox->SetBoxExtent(FVector(50.0, 50.0, 50.0));
		ControlBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		ControlBox->SetCollisionProfileName(TEXT("PhysicsActor"));
		ControlActor->SetRootComponent(ControlBox);
		ControlBox->RegisterComponent();
		ControlBox->SetWorldLocation(FVector(500.0, 0.0, 200.0));
		ControlBox->SetSimulatePhysics(true);
		AddInfo(FString::Printf(TEXT("Control box spawned at Z = %.2f; IsSimulatingPhysics = %s."),
			ControlActor->GetActorLocation().Z,
			ControlBox->IsSimulatingPhysics() ? TEXT("true") : TEXT("false")));
	}
	else
	{
		AddError(TEXT("Failed to spawn the control actor."));
	}

	// -- Step 4: does ticking the world move anything? ---------------------------
	//
	// Gravity is the cheapest liveness signal available: no input, no ground, nothing
	// to tune. If Z does not fall over half a second, the world is not simulating and
	// every manoeuvre number this ticket would go on to produce is meaningless.
	const FVector StartLocation = Pawn->GetActorLocation();
	const FVector ControlStart = ControlActor != nullptr ? ControlActor->GetActorLocation() : FVector::ZeroVector;

	for (int32 Step = 0; Step < ProbeStepCount; ++Step)
	{
		if (!WorldWrapper.TickTestWorld(ProbeStepSeconds))
		{
			WorldWrapper.ForwardErrorMessages(this);
			AddError(FString::Printf(TEXT("TickTestWorld failed at step %d."), Step));
			break;
		}

		// Velocity alongside position: run 3 showed a frozen velocity, and only the
		// pair of numbers distinguishes "solver stopped" from "results not pulled".
		if (Step < 2 || Step == ProbeStepCount - 1)
		{
			const FVector ControlVelocity = ControlBox != nullptr
				? ControlBox->GetPhysicsLinearVelocity() : FVector::ZeroVector;
			AddInfo(FString::Printf(
				TEXT("Step %2d: worldTime = %.4f, pawn Z = %.3f, control Z = %.3f, control Vz = %.3f."),
				Step,
				World->GetTimeSeconds(),
				Pawn->GetActorLocation().Z,
				ControlActor != nullptr ? ControlActor->GetActorLocation().Z : 0.0,
				ControlVelocity.Z));
		}
	}

	const FVector EndLocation = Pawn->GetActorLocation();
	const double FallCm = StartLocation.Z - EndLocation.Z;

	AddInfo(FString::Printf(
		TEXT("After %d steps of %.4f s: start Z = %.2f, end Z = %.2f, fall = %.2f cm."),
		ProbeStepCount, ProbeStepSeconds, StartLocation.Z, EndLocation.Z, FallCm));

	if (ControlActor != nullptr)
	{
		const double ControlFallCm = ControlStart.Z - ControlActor->GetActorLocation().Z;
		AddInfo(FString::Printf(TEXT("Control box fell %.2f cm over the same %d steps."),
			ControlFallCm, ProbeStepCount));
		TestTrue(TEXT("A plain simulating box fell under gravity"), ControlFallCm > 1.0);
	}

	TestFalse(TEXT("The pawn's position is finite after ticking"), EndLocation.ContainsNaN());
	TestTrue(TEXT("The pawn fell under gravity, so the world is simulating physics"), FallCm > 1.0);

	// -- Cleanup: the world must not outlive the test ----------------------------
	//
	// Other suites in this process load packages and trigger GC. A world left behind
	// with a physics scene and a ticking pawn is cross-test contamination, and the
	// TRACK-002 file comment is explicit that fixtures here must leave no residue.
	// DestroyTestWorld also ends play, which is what silences the run-2/3 warning
	// "UWorld::CleanupWorld called on a world that has begun play, missing call to
	// EndPlay".
	WorldWrapper.DestroyTestWorld(/*bForceGarbageCollect*/ false);
	WorldWrapper.ForwardErrorMessages(this);
	AddInfo(TEXT("World destroyed."));

	return true;
}
