// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

#include "AIController.h"
#include "ChaosVehicleWheel.h"
#include "ChaosWheeledVehicleMovementComponent.h"

#include "Vehicle/RacingVehiclePawn.h"
#include "Vehicle/VehicleChassisDataAsset.h"
#include "Vehicle/VehicleInputComponent.h"
#include "Vehicle/VehicleInputProcessor.h"
#include "Vehicle/VehicleTuneDataAsset.h"

#include "RacingSimTestsLog.h"

/**
 * VEH-006: one driveable car, on ground, in a world that steps physics.
 *
 * ---------------------------------------------------------------------------
 * Why this fixture exists and what it is allowed to assume
 * ---------------------------------------------------------------------------
 *
 * Every earlier vehicle ticket tested pure functions -- input shaping, chassis
 * derivation, telemetry evaluation over a hand-built snapshot -- because no world in
 * this harness stepped physics. VehicleManoeuvreWorldProbeSpec.cpp established the path
 * that does, the hard way, over four runs. Read its file comment before changing
 * anything here: the three paths it rejected all look reasonable and all silently
 * produce a car that never moves, with no error of any kind.
 *
 * The one-line summary of that probe: use the engine's own FTestWorldWrapper. A
 * hand-rolled UWorld::CreateWorld + BeginPlay + World->Tick loop advances world time
 * and leaves the Chaos solver frozen after exactly one integration step, because a
 * hand-rolled tick is not a frame -- FTestWorldWrapper::TickTestWorld increments
 * GFrameCounter after each tick and Chaos marshals game-thread state per frame.
 *
 * NO CONTENT. The ground is a code-built UBoxComponent and the chassis/tune assets are
 * transient NewObject instances carrying their C++ defaults, so this fixture needs no
 * .uasset, no cook, and no licence-ledger entry (CLAUDE.md's content rules). Those
 * defaults are a real car: 1250 kg, 34/35 cm wheel radii, 40 degrees of steering lock,
 * a 285 cm wheelbase.
 *
 * ---------------------------------------------------------------------------
 * Units
 * ---------------------------------------------------------------------------
 *
 * Distances are CENTIMETRES and speeds CENTIMETRES PER SECOND, the project storage
 * units per Core/RacingSimUnits.h. Yaw rate is DEGREES PER SECOND, which is what
 * GetPhysicsAngularVelocityInDegrees returns. Nothing here is in SI; do not mix.
 *
 * ---------------------------------------------------------------------------
 * Lifetime
 * ---------------------------------------------------------------------------
 *
 * Stack-allocated inside one test, torn down by the destructor. Not copyable: two
 * copies would both destroy the same world. Game thread only.
 */
struct FVehicleManoeuvreFixture
{
	/** Half-extents of the ground slab, centimetres. 200 m square, far more than any manoeuvre here needs. */
	static constexpr double GroundHalfSizeCm = 10000.0;

	/** Half-thickness of the ground slab, centimetres. */
	static constexpr double GroundHalfThicknessCm = 50.0;

	/**
	 * Height of the chassis ORIGIN above the ground surface at spawn, centimetres.
	 *
	 * The wheel centres sit ~35 cm below the origin with a 34-35 cm radius, so the tyre
	 * contact patch is roughly 70 cm below it. 90 cm places the car just above its own
	 * suspension travel: high enough that it settles onto its springs rather than
	 * starting interpenetrated with the ground, low enough that it is not a drop test.
	 */
	static constexpr double SpawnHeightCm = 90.0;

	/** Steps allowed for the suspension to settle before any manoeuvre is timed. */
	static constexpr int32 SettleSteps = 60;

	/** The fixed step every manoeuvre uses unless it is explicitly testing step size. */
	static constexpr float DefaultStepSeconds = 1.0f / 60.0f;

	FVehicleManoeuvreFixture() = default;
	FVehicleManoeuvreFixture(const FVehicleManoeuvreFixture&) = delete;
	FVehicleManoeuvreFixture& operator=(const FVehicleManoeuvreFixture&) = delete;

	~FVehicleManoeuvreFixture()
	{
		Teardown();
	}

	UWorld* GetWorld() const
	{
		return WorldWrapper.GetTestWorld();
	}

	/** The pawn under test. Null until a successful Setup. */
	ARacingVehiclePawn* GetPawn() const
	{
		return Pawn;
	}

	/** The pawn's Chaos movement component. Null until a successful Setup. */
	UChaosWheeledVehicleMovementComponent* GetMovement() const
	{
		return Movement;
	}

	/** The pawn's input component, reached by search because the pawn's pointer is private. */
	UVehicleInputComponent* GetInput() const
	{
		return Input;
	}

	/** The chassis body, i.e. the pawn's root. Reached via GetRootComponent for the same reason. */
	UPrimitiveComponent* GetChassisBody() const
	{
		return ChassisBody;
	}

	/**
	 * The transient chassis asset this fixture authored onto the pawn. Null before Setup.
	 *
	 * Exposed so a spec can derive an EXPECTED geometry from the same source the pawn
	 * reads, rather than restating a number the asset owns -- a duplicated 70 cm here
	 * would pass forever after the asset changed underneath it.
	 */
	UVehicleChassisDataAsset* GetChassisAsset() const
	{
		return Chassis.Get();
	}

	/**
	 * Build the world, the ground and the car, then settle the suspension.
	 *
	 * @param Test              the calling test; every failure is reported into it.
	 * @param bEnableTelemetry  false zeroes ARacingVehiclePawn::TelemetrySampleRateHz.
	 *
	 *   TELEMETRY IS OFF BY DEFAULT so that a manoeuvre spec measuring motion is not
	 *   also, silently, a test of the failure detector: a detector change would then
	 *   break specs that never meant to assert anything about it. A spec that does want
	 *   the detector opts in here, and if it injects a fault it must also register the
	 *   expected errors with AddExpectedError, because the automation framework turns
	 *   any Error-severity log into a test failure. VehicleFailureDetectorDrivingSpec
	 *   is the worked example of both halves.
	 *
	 *   HISTORY: this parameter defaulted to false originally because of a real VEH-004
	 *   defect -- the detector divided measured MOVEMENT by a measured WALL-CLOCK
	 *   interval, so a fixed-step loop produced impossible accelerations for a perfectly
	 *   healthy car. Fixed in commit 62134d0 by adding
	 *   FVehicleTelemetrySnapshot::SimulationTimeSeconds and judging simulated motion
	 *   against the simulated clock. Opting in is safe now; the default stands for the
	 *   coupling reason above.
	 *
	 * @return false on any failure, having already reported it. Callers MUST stop on false;
	 *         continuing would dereference a null pawn.
	 */
	bool Setup(FAutomationTestBase& Test, const bool bEnableTelemetry = false)
	{
		if (GEngine == nullptr)
		{
			Test.AddError(TEXT("GEngine is null; this test is running before UEngine::Init and cannot build a world."));
			return false;
		}

		if (!WorldWrapper.CreateTestWorld(EWorldType::Game))
		{
			WorldWrapper.ForwardErrorMessages(&Test);
			Test.AddError(TEXT("FTestWorldWrapper::CreateTestWorld(EWorldType::Game) failed."));
			return false;
		}

		UWorld* World = WorldWrapper.GetTestWorld();
		if (World == nullptr)
		{
			Test.AddError(TEXT("CreateTestWorld reported success but GetTestWorld() is null."));
			return false;
		}

		if (World->GetPhysicsScene() == nullptr)
		{
			Test.AddError(TEXT("The test world has no physics scene; nothing in this fixture can move."));
			return false;
		}

		// BEGIN PLAY BEFORE SPAWNING, not after. AActor::PostActorConstruction only
		// dispatches BeginPlay at spawn time when the world already reports having begun
		// play, and ARacingVehiclePawn builds its wheel setups in BeginPlay. Spawn first
		// and the car exists with zero wheels and no explanation.
		// PIN THE ENGINE GAME MODE. SetGameMode falls back to the project's
		// GlobalDefaultGameMode, which since RACE-005 is ARacingGameMode: it would spawn a
		// race director that finds no track and logs an Error, and possess nothing this
		// fixture spawns. The vehicle suite tests the car, not the session composition.
		if (AWorldSettings* WorldSettings = World->GetWorldSettings())
		{
			WorldSettings->DefaultGameMode = AGameModeBase::StaticClass();
		}

		if (!WorldWrapper.BeginPlayInTestWorld())
		{
			WorldWrapper.ForwardErrorMessages(&Test);
			Test.AddError(TEXT("FTestWorldWrapper::BeginPlayInTestWorld failed."));
			return false;
		}

		if (!SpawnGround(Test) || !SpawnPawn(Test, bEnableTelemetry))
		{
			return false;
		}

		// Settle. A car still falling onto its springs when a manoeuvre starts reports
		// suspension bounce as longitudinal acceleration.
		const double PreSettleZ = Pawn->GetActorLocation().Z;

		// CHECKED, because this is the one call site where ignoring it defeats the whole
		// contract stated on Drive(). A settle phase on a world that never ticks would
		// still return Setup() == true, and every spec built on this fixture would then
		// measure a car in a dead world and report whatever it measured as a physics
		// result. The soak happens to catch that on its first block; nothing else does.
		if (!Drive(FVehicleInputRawSample(), SettleSteps))
		{
			ReportTickFailure(Test);
			return false;
		}

		// Diagnostic, kept permanently rather than deleted once it had done its job. A
		// manoeuvre test that fails reports "the car did not move", which is the same
		// sentence for a broken input chain, a car that never settled onto the ground, a
		// sleeping rigid body and an engine at zero rpm. This line separates them, and
		// separating them cost two full build-and-run cycles the first time.
		UE_LOG(LogRacingTests, Display,
			TEXT("Fixture settled: Z %.2f -> %.2f cm over %d steps; speed %.2f cm/s, gear %d, engine %.1f rpm."),
			PreSettleZ, Pawn->GetActorLocation().Z, SettleSteps,
			GetForwardSpeedCms(), Movement->GetCurrentGear(), Movement->GetEngineRotationSpeed());

		// Second half of the same diagnostic, aimed at the suspension rather than the
		// drivetrain. A resting Z that happens to equal ChassisHalfHeightCm means the
		// collision BOX is on the ground and the wheels are carrying nothing, which is a
		// completely different fault from "the engine produced no torque" even though
		// both end with a stationary car. HasValidPhysicsState() reports whether Chaos
		// ever built the vehicle at all (it returns PVehicleOutput.IsValid(),
		// ChaosVehicleMovementComponent.cpp:825), so a false here means the failure is
		// upstream of every tuning value.
		FString WheelContact;
		for (int32 WheelIndex = 0; WheelIndex < Movement->GetNumWheels(); ++WheelIndex)
		{
			const UChaosVehicleWheel* Wheel = Movement->Wheels.IsValidIndex(WheelIndex)
				? Movement->Wheels[WheelIndex] : nullptr;
			WheelContact += FString::Printf(TEXT("[%d %s off %.2f] "), WheelIndex,
				(Wheel && Wheel->IsInAir()) ? TEXT("air") : TEXT("ground"),
				Movement->GetSuspensionOffset(WheelIndex));
		}
		UE_LOG(LogRacingTests, Display,
			TEXT("Fixture settled: physics state %s, %d wheels, contact %s"),
			Movement->HasValidPhysicsState() ? TEXT("valid") : TEXT("INVALID"),
			Movement->GetNumWheels(), *WheelContact);

		// TargetGear is the game thread's REQUEST; GetCurrentGear() is what the physics
		// transmission actually reached. Separating them splits the last two candidates
		// for a stationary car whose engine is idling: TargetGear 0 means the raw
		// throttle never reached UpdateState (ChaosVehicleMovementComponent.cpp:1298,
		// which shifts an automatic out of neutral on any throttle), while TargetGear 1
		// with CurrentGear 0 means the request was made and the transmission simulation
		// did not act on it.
		UE_LOG(LogRacingTests, Display,
			TEXT("Fixture settled: target gear %d, current gear %d, forward speed %.2f cm/s."),
			Movement->GetTargetGear(), Movement->GetCurrentGear(), Movement->GetForwardSpeed());

		LogDrivetrain(TEXT("settled"));

		return true;
	}

	/**
	 * Per-wheel drivetrain diagnostic, read from the PHYSICS THREAD's own output rather
	 * than from anything the game thread believes.
	 *
	 * Every earlier diagnostic in this fixture reports game-thread state, and game-thread
	 * state was misleading: the component reported "target gear 1, current gear 0" while
	 * the physics transmission was demonstrably in gear (Chaos holds the engine at exactly
	 * its idle rpm only when FSimpleTransmissionSim::IsOutOfGear() is false --
	 * ProcessMechanicalSimulation, ChaosWheeledVehicleMovementComponent.cpp:874). So the
	 * two are not the same fact and must not be read as one.
	 *
	 * UChaosVehicleMovementComponent::PhysicsVehicleOutput() (ChaosVehicleMovementComponent.h:1011)
	 * is the supported read of FPhysicsVehicleOutput, whose per-wheel FWheelsOutput carries
	 * DriveTorque, BrakeTorque, AngularVelocity and SpringForce
	 * (ChaosVehicleManagerAsyncCallback.h:224-300). Those four split the remaining ways a
	 * car can sit still, which "the car did not move" does not:
	 *
	 *   DriveTorque == 0                        the engine/transmission produced nothing;
	 *                                           look at throttle delivery, not at grip.
	 *   DriveTorque > 0, BrakeTorque > 0        something is braking it -- IdleBrakeInput,
	 *                                           an auto-brake path, or a stuck handbrake.
	 *   DriveTorque > 0, AngularVelocity == 0   torque reached the wheel and the wheel is
	 *                                           held; a brake or a locked constraint.
	 *   AngularVelocity > 0, no forward speed   the wheels spin and the tyre model makes no
	 *                                           longitudinal force; a grip/contact problem.
	 *
	 * RawThrottle is the component's own GetThrottleInput() (which returns RawThrottleInput,
	 * NOT the interpolated ThrottleInput that is actually sent to the physics thread), so a
	 * non-zero RawThrottle with zero DriveTorque localises the fault to everything between
	 * UpdateState and FChaosVehicleAsyncInput.
	 *
	 * That gap is exactly three steps wide, and this log splits it by reading the interpolated
	 * ThrottleInput/BrakeInput as well as the raw ones. Both are UPROPERTY(Transient) but
	 * protected (ChaosVehicleMovementComponent.h:1091,1095), so they are read reflectively;
	 * that is acceptable in a test-only module and nowhere else. The three steps are:
	 *
	 *   UpdateState (ChaosVehicleMovementComponent.cpp:1256) computes ModifiedThrottle/
	 *   ModifiedBrake via CalcThrottleBrakeInput and rate-limits them into ThrottleInput/
	 *   BrakeInput -- but only when bProcessLocally, which is
	 *   bRequiresControllerForInputs ? (GetController() && IsLocalController()) : true (:1281).
	 *   Otherwise it overwrites both from ReplicatedState (:1351-1362), which in a standalone
	 *   test only ever holds whatever ServerUpdateState last wrote.
	 *
	 *   Update (:1877) copies those interpolated values into the async input unconditionally.
	 *
	 *   FChaosVehicleManager::Update (ChaosVehicleManager.cpp:161) calls both, but only calls
	 *   Update inside `if (World)` where World is Scene.GetOwningWorld() (:165,173).
	 *
	 * So: raw 1 / interp 0 means UpdateState did not take the local path or CalcThrottleBrakeInput
	 * zeroed it; raw 1 / interp 1 / physics-thread torque 0 means the marshalling itself is the
	 * fault. Controller and bRequiresControllerForInputs are logged because they are the inputs
	 * to the one branch that can produce the first case.
	 */
	void LogDrivetrain(const TCHAR* Phase) const
	{
		if (Movement == nullptr)
		{
			return;
		}

		FString Wheels;
		if (const FPhysicsVehicleOutput* Output = Movement->PhysicsVehicleOutput().Get())
		{
			for (int32 WheelIndex = 0; WheelIndex < Output->Wheels.Num(); ++WheelIndex)
			{
				const FWheelsOutput& Wheel = Output->Wheels[WheelIndex];
				Wheels += FString::Printf(
					TEXT("[%d drive %.1f brake %.1f angvel %.2f spring %.1f] "),
					WheelIndex, Wheel.DriveTorque, Wheel.BrakeTorque,
					Wheel.AngularVelocity, Wheel.SpringForce);
			}
		}
		else
		{
			Wheels = TEXT("(no physics vehicle output)");
		}

		// Cast result checked, not assumed. The owner is an APawn in every fixture this
		// file builds, but this is DIAGNOSTIC code: it runs on the path where something has
		// already gone wrong, and a null-dereference here would replace the failure report
		// with a crash -- destroying exactly the evidence it exists to print.
		const APawn* OwningPawn = Cast<APawn>(Movement->GetOwner());
		const AController* Controller = OwningPawn != nullptr ? OwningPawn->GetController() : nullptr;

		// Whether the chassis rigid body is awake, which gates the ENTIRE physics-thread
		// vehicle tick: FChaosVehicleManagerAsyncCallback::OnPreSimulate_Internal returns
		// before Simulate() unless Handle->ObjectState() == EObjectStateType::Dynamic
		// (ChaosVehicleManagerAsyncCallback.cpp:126-129). A sleeping body therefore leaves the
		// previous FChaosVehicleAsyncOutput in place, so every per-wheel figure below stays
		// frozen at its last simulated value rather than reading as zero -- which is why a
		// sleeping car and a car with a broken drivetrain look identical without this flag.
		const UPrimitiveComponent* UpdatedPrimitive = Cast<UPrimitiveComponent>(Movement->UpdatedComponent);
		const bool bAwake = UpdatedPrimitive != nullptr && UpdatedPrimitive->RigidBodyIsAwake();

		UE_LOG(LogRacingTests, Display,
			TEXT("Drivetrain (%s): raw throttle %.3f, raw brake %.3f, interp throttle %.3f, ")
			TEXT("interp brake %.3f, engine %.1f rpm, gear %d, controller %s (local %s), ")
			TEXT("requires controller %s, body awake %s; %s"),
			Phase,
			Movement->GetThrottleInput(), Movement->GetBrakeInput(),
			ReadProtectedFloat(TEXT("ThrottleInput")), ReadProtectedFloat(TEXT("BrakeInput")),
			Movement->GetEngineRotationSpeed(), Movement->GetCurrentGear(),
			Controller != nullptr ? *Controller->GetName() : TEXT("none"),
			(Controller != nullptr && Controller->IsLocalController()) ? TEXT("yes") : TEXT("no"),
			ReadProtectedBool(TEXT("bRequiresControllerForInputs")) ? TEXT("yes") : TEXT("no"),
			bAwake ? TEXT("yes") : TEXT("no"),
			*Wheels);
	}

	/**
	 * Reflective reads of protected UPROPERTYs on the movement component.
	 *
	 * Only LogDrivetrain uses these, and only for state Chaos exposes no accessor for. They
	 * return 0/false rather than asserting when a property is missing, because an engine
	 * upgrade that renames one must degrade a diagnostic line, not fail a test that is not
	 * about reflection.
	 */
	float ReadProtectedFloat(const TCHAR* PropertyName) const
	{
		const FFloatProperty* Property = CastField<FFloatProperty>(
			UChaosWheeledVehicleMovementComponent::StaticClass()->FindPropertyByName(FName(PropertyName)));
		return Property != nullptr ? Property->GetPropertyValue_InContainer(Movement) : 0.0f;
	}

	bool ReadProtectedBool(const TCHAR* PropertyName) const
	{
		const FBoolProperty* Property = CastField<FBoolProperty>(
			UChaosWheeledVehicleMovementComponent::StaticClass()->FindPropertyByName(FName(PropertyName)));
		return Property != nullptr ? Property->GetPropertyValue_InContainer(Movement) : false;
	}

	/** The shaped command the input layer last produced. Diagnostic, not a race truth source. */
	FVehicleInputCommand GetLastCommand() const
	{
		return Input != nullptr ? Input->GetProcessor().GetLastCommand() : FVehicleInputCommand();
	}

	/** Tear down. Idempotent, and safe after a failed or never-called Setup. */
	void Teardown()
	{
		bTickFailed = false;
		TickFailureReason.Reset();
		LastDriveStepsCompleted = 0;

		Pawn = nullptr;
		Movement = nullptr;
		Input = nullptr;
		ChassisBody = nullptr;
		Ground = nullptr;
		Chassis.Reset();
		Tune.Reset();

		if (WorldWrapper.GetTestWorld() != nullptr)
		{
			// bForceGarbageCollect false: a forced GC here costs seconds per test and the
			// wrapper already unroots the world, so the next GC collects it either way.
			WorldWrapper.DestroyTestWorld(/*bForceGarbageCollect*/ false);
		}
	}

	/**
	 * Hold one raw input sample for Steps fixed steps.
	 *
	 * The sample is re-injected EVERY step rather than once, because
	 * UVehicleInputComponent::TickComponent consumes PendingSample each tick and the
	 * rate limiter is meant to see a control being HELD. Injecting once would be
	 * indistinguishable from a driver who let go after a single frame, and would
	 * silently test the rate limiter's release ramp instead of its apply ramp.
	 *
	 * The return value is NOT optional decoration. FTestWorldWrapper::TickTestWorld
	 * returns false when the world stops ticking, and an earlier version of this
	 * fixture discarded it. That made every step count in every spec bookkeeping
	 * rather than evidence: a world that stopped ticking at step 40,000 still let the
	 * soak report 108,000 steps and 1800 s of simulated time, because the loop counter
	 * had no idea the world underneath it had stopped. The failure is now sticky, so a
	 * spec that drives in several blocks can check once at the end.
	 *
	 * @return true when every requested step ticked.
	 */
	bool Drive(const FVehicleInputRawSample& Sample, const int32 Steps, const float StepSeconds = DefaultStepSeconds)
	{
		LastDriveStepsCompleted = 0;

		for (int32 Step = 0; Step < Steps; ++Step)
		{
			if (Input != nullptr)
			{
				Input->InjectRawSampleForTesting(Sample);
			}

			if (!WorldWrapper.TickTestWorld(StepSeconds))
			{
				if (!bTickFailed)
				{
					bTickFailed = true;
					TickFailureReason = FString::Printf(
						TEXT("TickTestWorld failed at step %d of %d (step size %f s); ")
						TEXT("the world stopped ticking, so every measurement after this point is meaningless."),
						Step, Steps, StepSeconds);
				}

				return false;
			}

			++LastDriveStepsCompleted;
		}

		return true;
	}

	/** True once any Drive call has failed to tick. Sticky until Teardown. */
	bool HasTickFailure() const
	{
		return bTickFailed;
	}

	/** Why the first failed tick failed. Empty while HasTickFailure is false. */
	const FString& GetTickFailureReason() const
	{
		return TickFailureReason;
	}

	/** Steps the most recent Drive call actually ticked. */
	int32 GetLastDriveStepsCompleted() const
	{
		return LastDriveStepsCompleted;
	}

	/**
	 * Report a sticky tick failure to the test, and say so.
	 *
	 * Forwards the world wrapper own error messages first, since those carry the engine
	 * side reason and this fixture only knows that a step returned false.
	 *
	 * @return true when a failure was reported, so a caller can bail on the same line.
	 */
	bool ReportTickFailure(FAutomationTestBase& Test)
	{
		if (!bTickFailed)
		{
			return false;
		}

		WorldWrapper.ForwardErrorMessages(&Test);
		Test.AddError(TickFailureReason);
		return true;
	}


	/** Forward speed, CENTIMETRES PER SECOND, signed. Zero when the fixture failed to build. */
	double GetForwardSpeedCms() const
	{
		return Movement != nullptr ? static_cast<double>(Movement->GetForwardSpeed()) : 0.0;
	}

	/** Chassis world location, centimetres. Zero vector when the fixture failed to build. */
	FVector GetLocation() const
	{
		return Pawn != nullptr ? Pawn->GetActorLocation() : FVector::ZeroVector;
	}

	/**
	 * Yaw rate, DEGREES PER SECOND, from the chassis body's angular velocity.
	 *
	 * Sign follows Unreal's left-handed Z-up convention: positive is a turn to the
	 * RIGHT, matching FVehicleInputCommand's steering sign, so a test can assert that
	 * right steering produces positive yaw without a sign flip to get wrong.
	 */
	double GetYawRateDegreesPerSecond() const
	{
		return ChassisBody != nullptr ? ChassisBody->GetPhysicsAngularVelocityInDegrees().Z : 0.0;
	}

	/** A throttle-and-steer sample. Steer is [-1,1], positive RIGHT. */
	static FVehicleInputRawSample ThrottleSample(const double Throttle = 1.0, const double Steer = 0.0)
	{
		FVehicleInputRawSample Sample;
		Sample.Throttle = Throttle;
		Sample.Steer = Steer;
		return Sample;
	}

	/** A brake sample. */
	static FVehicleInputRawSample BrakeSample(const double Brake = 1.0)
	{
		FVehicleInputRawSample Sample;
		Sample.Brake = Brake;
		return Sample;
	}

private:
	bool SpawnGround(FAutomationTestBase& Test)
	{
		UWorld* World = WorldWrapper.GetTestWorld();

		Ground = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
		if (Ground == nullptr)
		{
			Test.AddError(TEXT("Failed to spawn the ground actor."));
			return false;
		}

		UBoxComponent* GroundBox = NewObject<UBoxComponent>(Ground);
		GroundBox->SetBoxExtent(FVector(GroundHalfSizeCm, GroundHalfSizeCm, GroundHalfThicknessCm));
		GroundBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

		// BlockAll rather than PhysicsActor: Chaos vehicle suspension is a raycast per
		// wheel, so the ground must block the visibility/vehicle query channels, not
		// merely participate in rigid-body collision. A profile that only did the latter
		// gives a car whose wheels find no ground and which sinks through a solid slab.
		GroundBox->SetCollisionProfileName(TEXT("BlockAll"));

		Ground->SetRootComponent(GroundBox);
		GroundBox->RegisterComponent();

		// Top surface at Z = 0, so every height in every spec is measured from a plane at
		// the origin rather than from a slab thickness nobody wants to remember.
		GroundBox->SetWorldLocation(FVector(0.0, 0.0, -GroundHalfThicknessCm));

		// Static and non-simulating. A simulating ground would be shoved away by the car
		// and the test would measure a race between two falling bodies.
		GroundBox->SetSimulatePhysics(false);

		return true;
	}

	bool SpawnPawn(FAutomationTestBase& Test, const bool bEnableTelemetry)
	{
		UWorld* World = WorldWrapper.GetTestWorld();

		// Transient assets carrying their C++ defaults, matching VehicleChassisSpec.cpp.
		// Held in TStrongObjectPtr because the pawn's UPROPERTY pointer alone is not
		// enough to survive a GC triggered by another suite running in the same process.
		Chassis.Reset(NewObject<UVehicleChassisDataAsset>(GetTransientPackage()));
		Tune.Reset(NewObject<UVehicleTuneDataAsset>(GetTransientPackage()));

		const FTransform SpawnTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, SpawnHeightCm));

		// Deferred spawn: ARacingVehiclePawn::BeginPlay refuses a null chassis rather than
		// substituting a default, so the assets must be attached between construction and
		// FinishSpawningActor -- which is the call that runs BeginPlay.
		Pawn = World->SpawnActorDeferred<ARacingVehiclePawn>(
			ARacingVehiclePawn::StaticClass(), SpawnTransform);
		if (Pawn == nullptr)
		{
			Test.AddError(TEXT("SpawnActorDeferred<ARacingVehiclePawn> returned null."));
			return false;
		}

		Pawn->ChassisAsset = Chassis.Get();
		Pawn->TuneAsset = Tune.Get();
		if (!bEnableTelemetry)
		{
			Pawn->TelemetrySampleRateHz = 0.0f;
		}

		UGameplayStatics::FinishSpawningActor(Pawn, SpawnTransform);

		if (!Pawn->IsChassisApplied())
		{
			Test.AddError(TEXT("The spawned pawn did not apply its chassis asset; BeginPlay did not run or the asset was rejected."));
			return false;
		}

		Movement = Pawn->FindComponentByClass<UChaosWheeledVehicleMovementComponent>();
		if (Movement == nullptr)
		{
			Test.AddError(TEXT("The spawned pawn has no UChaosWheeledVehicleMovementComponent."));
			return false;
		}

		if (Movement->WheelSetups.Num() != 4)
		{
			Test.AddError(FString::Printf(
				TEXT("The spawned pawn has %d wheel setups, expected 4."), Movement->WheelSetups.Num()));
			return false;
		}

		// ------------------------------------------------------------------
		// POSSESS THE CAR. This is a precondition, not set dressing.
		// ------------------------------------------------------------------
		//
		// An unpossessed Chaos vehicle is inert, in two separate places, and both were
		// found by running this fixture rather than by reading ahead: the first run of the
		// manoeuvre suite reported 0.00 cm of travel, 0.00 cm/s and 0.00 deg/s on every
		// test, with no error, no warning, and a healthy four-wheel car sitting correctly
		// on the ground. Report: Saved/Automation/ReportVEH006Man1/index.json.
		//
		// GATE 1 -- input is discarded without a local controller.
		//   ChaosVehicleMovementComponent.cpp:1281
		//     bool bProcessLocally = bRequiresControllerForInputs
		//         ? (Controller && Controller->IsLocalController()) : true;
		//   and everything that consumes throttle, brake, steering and gear selection sits
		//   inside `if (bProcessLocally)`. bRequiresControllerForInputs defaults to true
		//   (:625), so a pawn with no controller silently drops every input.
		//
		// Possession is the fix for GATE 1 only, and it answers it the way shipping code
		// does. The earlier fix poked SetRequiresControllerForInputs(false) instead; that
		// silenced the symptom while leaving the fixture testing a code path no real car
		// ever takes, which is worse than the machinery it avoided.
		//
		// GATE 2 -- the vehicle falls asleep and cannot wake itself -- is a SEPARATE
		// defect with a separate fix, and possession does NOT address it. An earlier
		// version of this comment claimed it did; that claim was wrong.
		//   The settle phase holds zero input, so the Chaos solver sleeps the chassis
		//   island. UChaosVehicleMovementComponent::ProcessSleeping would wake it via
		//   SetSleeping(false), but SetSleeping delegates to WakeAllEnabledRigidBodies /
		//   PutAllEnabledRigidBodiesToSleep (ChaosVehicleMovementComponent.cpp:2058-2090)
		//   and BOTH open with `if (USkeletalMeshComponent* Mesh = GetSkeletalMesh())`.
		//   ARacingVehiclePawn's UpdatedComponent is a UBoxComponent, so both are complete
		//   no-ops with or without a controller. A slept chassis is fatal because
		//   FChaosVehicleManagerAsyncCallback::OnPreSimulate_Internal
		//   (ChaosVehicleManagerAsyncCallback.cpp:126-129) returns before
		//   FChaosVehicleAsyncInput::Simulate unless the handle's ObjectState is Dynamic:
		//   the whole vehicle sim stops and the last async output stays latched, so
		//   telemetry keeps reporting plausible frozen numbers. This is what produced the
		//   stationary-car run: target gear 1 (the game thread requested first) with
		//   current gear 0 and the engine pinned at idle.
		//   Report: Saved/Automation/ReportVEH006Man6/index.json.
		//   THE FIX lives in ARacingVehiclePawn::BeginPlay, not here: it pins the chassis
		//   particle to Chaos::ESleepType::NeverSleep through
		//   FSingleParticlePhysicsProxy::GetGameThreadAPI().SetSleepType, which
		//   ParticleHandle.h:3777-3781 documents as also waking an already-sleeping
		//   particle. Shipping cars get the same treatment, so the fixture is not special.
		//
		// AAIController, NOT APlayerController. AController::IsLocalController() returns
		// true immediately for NM_Standalone, but APlayerController overrides it and
		// returns FALSE when there is no NetDriver and no ULocalPlayer -- and a bare test
		// world has neither. A player controller here would possess the car and change
		// nothing, which is the most expensive kind of wrong.
		AAIController* Driver = World->SpawnActor<AAIController>(
			AAIController::StaticClass(), FTransform::Identity);
		if (Driver == nullptr)
		{
			Test.AddError(TEXT("Failed to spawn the AAIController that drives the pawn; an unpossessed Chaos vehicle ignores all input and cannot wake from sleep."));
			return false;
		}

		Driver->Possess(Pawn);

		if (Pawn->GetController() == nullptr || !Pawn->GetController()->IsLocalController())
		{
			Test.AddError(TEXT("The pawn is not locally controlled after possession; ChaosVehicleMovementComponent would discard every input and never wake the chassis body."));
			return false;
		}

		// Both of these are private UPROPERTYs on the pawn (RacingVehiclePawn.h:305, :312),
		// so they are reached the way any other actor would reach them rather than by
		// widening the pawn's API for a test.
		Input = Pawn->FindComponentByClass<UVehicleInputComponent>();
		if (Input == nullptr)
		{
			Test.AddError(TEXT("The spawned pawn has no UVehicleInputComponent."));
			return false;
		}

		ChassisBody = Cast<UPrimitiveComponent>(Pawn->GetRootComponent());
		if (ChassisBody == nullptr)
		{
			Test.AddError(TEXT("The spawned pawn's root is not a UPrimitiveComponent, so it has no physics body."));
			return false;
		}

		return true;
	}

	FTestWorldWrapper WorldWrapper;
	ARacingVehiclePawn* Pawn = nullptr;
	UChaosWheeledVehicleMovementComponent* Movement = nullptr;
	UVehicleInputComponent* Input = nullptr;
	UPrimitiveComponent* ChassisBody = nullptr;
	AActor* Ground = nullptr;
	TStrongObjectPtr<UVehicleChassisDataAsset> Chassis;
	TStrongObjectPtr<UVehicleTuneDataAsset> Tune;

	/** Sticky across Drive calls, cleared by Teardown. See Drive. */
	bool bTickFailed = false;
	FString TickFailureReason;
	int32 LastDriveStepsCompleted = 0;
};
