// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Core/RacingSimTypes.h"
#include "Vehicle/VehicleInputProcessor.h"
#include "Vehicle/VehicleInputTypes.h"
#include "VehicleInputComponent.generated.h"

class APlayerController;
class UEnhancedInputComponent;
class UInputAction;

/**
 * VEH-001: the Enhanced Input adapter. Plumbing only.
 *
 * ---------------------------------------------------------------------------
 * This class deliberately contains no decisions
 * ---------------------------------------------------------------------------
 *
 * Everything that can be gotten wrong -- dead zones, rate limiting, the pedal
 * conflict policy, the frame-rate clamp, the reset hold, NaN rejection -- lives in
 * FVehicleInputProcessor, which is a plain struct. The reason is recorded in detail
 * in VehicleInputProcessor.h: a SmokeFilter test in this project cannot construct a
 * UActorComponent (Docs/Environment.md, with the engine-source cause), so anything
 * implemented HERE is untestable at the fast gate and stays untestable until VEH-002
 * ships a pawn to put it on.
 *
 * So this component does four things and nothing else: resolve the config, push the
 * mapping context, accumulate raw axis values from Enhanced Input, and Tick the
 * processor. If a future change adds an `if` to this file that is not about
 * plumbing, it is in the wrong file.
 *
 * ---------------------------------------------------------------------------
 * What VEH-002 consumes
 * ---------------------------------------------------------------------------
 *
 * GetCommand(). Not the processor, not the config, and never a UInputAction. The
 * command is the published contract (FVehicleInputCommand) and it is the only thing
 * that should cross into the Chaos movement component.
 *
 * ---------------------------------------------------------------------------
 * What is NOT here, and why
 * ---------------------------------------------------------------------------
 *
 * No FKey, no EKeys::, no key literal of any kind -- rebinding must be an asset edit
 * and never a recompile. RacingSim.Vehicle.InputNoHardcodedKeys enforces that by
 * scanning this directory's source, because a review convention would not survive
 * the first "just for now" hard-coded key.
 *
 * No default UInputMappingContext or UInputAction .uasset either. CLAUDE.md forbids
 * editing Unreal binary assets from a worktree and requires a serialized
 * Docs/AssetOwnership.tsv claim; VEH-001 owes the soft-pointer fields and the
 * validation that rejects an unbound slot, which UVehicleInputConfigDataAsset has.
 * The assets themselves are a content task. Until they exist this component
 * initialises, logs that it has no usable config, and produces the safe standing-
 * still command -- it does not crash and does not silently pretend to be bound.
 */
UCLASS(ClassGroup = (RacingSim), meta = (BlueprintSpawnableComponent, DisplayName = "Vehicle Input"))
class RACINGSIM_API UVehicleInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVehicleInputComponent();

	/**
	 * Resolve the config, select the device profile and push the mapping context onto
	 * the local player's UEnhancedInputLocalPlayerSubsystem.
	 *
	 * Loads the mapping context SYNCHRONOUSLY. That is a deliberate exception to
	 * CLAUDE.md's "no synchronous asset loads during a race": this runs at possession,
	 * before the countdown, and an input layer that is asynchronously not-yet-bound
	 * when the lights go out is worse than a load hitch on a loading screen. It must
	 * never be called from a racing state.
	 *
	 * @return false when there is no config or no profile for the device. The
	 *         component stays alive and keeps producing safe commands.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicle|Input")
	bool InitialiseForController(APlayerController* Controller, ERacingInputDeviceType DeviceType);

	/** Bind the configured UInputAction assets to this component's handlers. Call from the pawn's SetupPlayerInputComponent. */
	void BindActions(UEnhancedInputComponent* EnhancedInput);

	/**
	 * The command for the most recent Tick.
	 *
	 * Safe before initialisation: a default FVehicleInputCommand is a coasting,
	 * straight-ahead car, so a consumer that reads this too early gets something
	 * physically meaningful rather than something undefined.
	 */
	UFUNCTION(BlueprintPure, Category = "Vehicle|Input")
	FVehicleInputCommand GetCommand() const
	{
		// By value, not by const reference. A UFUNCTION returning a const struct
		// reference does not round-trip through Blueprint's value semantics, and the
		// command is 40-odd bytes of POD -- the copy is cheaper than the confusion.
		return Processor.GetLastCommand();
	}

	/**
	 * Drop smoothing state after a teleport, respawn or session restart.
	 *
	 * VEH-005 owns the reset POSE; this is the input layer's share of the same event,
	 * and it is separate because a reset that repositions the car without clearing
	 * the input ramp puts it on the grid still holding full lock. See
	 * FVehicleInputProcessor::ResetState for exactly what is and is not cleared.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicle|Input")
	void NotifyVehicleReset();

	/**
	 * Feed the vehicle's forward speed for speed-sensitive steering.
	 *
	 * CENTIMETRES PER SECOND, signed, the project storage unit per
	 * Core/RacingSimUnits.h. VEH-002 pushes this from the movement component; the
	 * conversion to the km/h curve domain happens inside the config asset. Callers
	 * must not pre-convert, and this component deliberately does not go looking for a
	 * movement component itself -- CLAUDE.md forbids broad actor searches, and a
	 * component that finds its own data source is a component VEH-002 cannot test in
	 * isolation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicle|Input")
	void SetVehicleSpeedCms(float SpeedCms);

	/**
	 * The config in force. May be null before InitialiseForController.
	 *
	 * Not a UFUNCTION: a const UObject pointer return is not a Blueprint-exposable
	 * signature. Blueprint reads the Config property directly instead.
	 */
	const UVehicleInputConfigDataAsset* GetConfig() const
	{
		return Config;
	}

	/** Read-only processor access, for VEH-004 telemetry. */
	const FVehicleInputProcessor& GetProcessor() const
	{
		return Processor;
	}

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * Tuning and bindings. Assigned in the pawn Blueprint, per CLAUDE.md's
	 * "Blueprint for assembly, tuning, presentation" split.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Input")
	TObjectPtr<UVehicleInputConfigDataAsset> Config;

	/**
	 * Priority for the pushed UInputMappingContext.
	 *
	 * Driving is the base layer, so 0. A pause menu or a photo mode must push a
	 * HIGHER priority context to take control away; that is UI-001's problem and this
	 * field is what lets it be solved without touching the vehicle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Input", meta = (ClampMin = "0"))
	int32 MappingContextPriority = 0;

private:
	/** Enhanced Input handlers. Each one only records a raw value; none of them decide anything. */
	void HandleThrottle(const struct FInputActionValue& Value);
	void HandleBrake(const struct FInputActionValue& Value);
	void HandleSteer(const struct FInputActionValue& Value);
	void HandleHandbrake(const struct FInputActionValue& Value);
	void HandleClutch(const struct FInputActionValue& Value);
	void HandleShiftUp(const struct FInputActionValue& Value);
	void HandleShiftDown(const struct FInputActionValue& Value);
	void HandleReset(const struct FInputActionValue& Value);

	/** Resolve one configured slot, or null. Synchronous; initialisation-time only. */
	UInputAction* ResolveAction(EVehicleInputAction Slot) const;

	/** All the rules live here. See the class comment. */
	FVehicleInputProcessor Processor;

	/**
	 * The mapping context this component itself last pushed onto the subsystem, so a
	 * device switch removes THAT context rather than the incoming one.
	 *
	 * VEH-002 finding (routed from VEH-001 MEDIUM-1): re-initialising with a
	 * RemoveMappingContext(Context) call on the NEW context only de-duplicates against
	 * re-adding the same context; it does nothing about a PREVIOUS device's context,
	 * which stays mapped at the same priority and keeps firing into PendingSample.
	 * Weak, not strong: a subsystem-driven context removal (e.g. another system
	 * clearing all contexts) must not keep this pointer alive past its owner's cleanup.
	 */
	TWeakObjectPtr<UInputMappingContext> PushedContext;

	/**
	 * Raw values accumulated by the handlers between Ticks.
	 *
	 * Enhanced Input fires Triggered/Completed callbacks within the frame; this
	 * struct is the buffer between those callbacks and the single Tick that consumes
	 * them, so exactly one command is produced per frame no matter how many times a
	 * handler fired. Without the buffer, a control that fired twice in a frame would
	 * advance the rate limiter twice on the same DeltaSeconds -- frame-rate
	 * dependence introduced by the plumbing rather than by the maths.
	 */
	FVehicleInputRawSample PendingSample;
};
