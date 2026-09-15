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
 * Why InitialiseForController could not complete, or that it did.
 *
 * VEH-004, closing VEH-001 LOW-2, which was routed here explicitly because "VEH-004
 * owns failure classification". The `bool` this replaces conflated two situations that
 * demand opposite responses: a MISCONFIGURED ASSET (a content bug someone must fix)
 * and a NON-LOCAL PAWN (entirely normal for an AI or remote car, and not a fault at
 * all). A caller given only `false` had no way to log the first loudly and the second
 * quietly, so it logged neither -- which is what the pawn did.
 *
 * The `bool` overload is deliberately NOT kept alongside this. Two return contracts for
 * one function is how the ambiguity started, and a caller that ignores the enum in
 * favour of a truthiness test is the same bug wearing a new type.
 */
UENUM(BlueprintType)
enum class EVehicleInputInitResult : uint8
{
	/** Profile resolved, mapping context pushed. The only success value. */
	Succeeded					UMETA(DisplayName = "Succeeded"),

	/** No UVehicleInputConfigDataAsset assigned. CONTENT FAULT. */
	NoConfig					UMETA(DisplayName = "No config asset"),

	/**
	 * The config has no FVehicleInputProfile for the requested device. CONTENT FAULT.
	 * Neutral shaping is used -- never another device's numbers.
	 */
	NoProfileForDevice			UMETA(DisplayName = "No profile for device"),

	/** Called with a null controller. PROGRAMMING FAULT. */
	NoController				UMETA(DisplayName = "No controller"),

	/**
	 * The controller has no ULocalPlayer. NOT A FAULT: expected for an AI-driven,
	 * spectated or remote pawn. This is the value the whole enum exists to separate
	 * from the others.
	 */
	NotLocalPlayer				UMETA(DisplayName = "Not a local player"),

	/** UEnhancedInputLocalPlayerSubsystem unavailable. ENVIRONMENT FAULT -- the plugin is disabled. */
	NoInputSubsystem			UMETA(DisplayName = "No Enhanced Input subsystem"),

	/** No UInputMappingContext configured for the device. CONTENT FAULT. */
	NoMappingContext			UMETA(DisplayName = "No mapping context"),

	/** The configured UInputMappingContext failed to load. CONTENT FAULT -- a broken or deleted asset reference. */
	MappingContextLoadFailed	UMETA(DisplayName = "Mapping context failed to load")
};

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
	 * @return EVehicleInputInitResult::Succeeded, or the specific reason it could not.
	 *         The component stays alive and keeps producing safe commands in EVERY
	 *         failure case -- a car that cannot be driven says so far more usefully
	 *         than a crash on possession does. See EVehicleInputInitResult for why this
	 *         is not a bool (VEH-004, closing VEH-001 LOW-2).
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicle|Input")
	EVehicleInputInitResult InitialiseForController(APlayerController* Controller, ERacingInputDeviceType DeviceType);

	/** True only for EVehicleInputInitResult::Succeeded. A named predicate, so no caller re-derives success from enum ordering. */
	UFUNCTION(BlueprintPure, Category = "Vehicle|Input")
	static bool IsVehicleInputInitSuccess(const EVehicleInputInitResult Result)
	{
		return Result == EVehicleInputInitResult::Succeeded;
	}

	/**
	 * True when this result describes a CONTENT/PROGRAMMING fault someone must fix,
	 * false for the expected NotLocalPlayer case (and for success).
	 *
	 * This predicate is the entire point of the enum: it is what lets the pawn log a
	 * broken asset as an Error and a remote pawn at Verbose, which the old bool could
	 * not express.
	 */
	UFUNCTION(BlueprintPure, Category = "Vehicle|Input")
	static bool IsVehicleInputInitFault(const EVehicleInputInitResult Result)
	{
		return Result != EVehicleInputInitResult::Succeeded
			&& Result != EVehicleInputInitResult::NotLocalPlayer;
	}

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
	 * Drop ALL input state, including the button HELD flags, on unpossession.
	 *
	 * Added on code review (VEH-005 MEDIUM-B): NotifyVehicleReset() deliberately
	 * PRESERVES bShiftUpHeld/bShiftDownHeld/bResetHeld across the call (see its own
	 * comment) because a still-held key's next Enhanced Input Triggered/Completed
	 * callback is what eventually corrects the flag. UnPossessed() has no such future
	 * callback -- unbinding the input component's actions means a key released after
	 * unpossession, or the pawn later being re-possessed by a different controller,
	 * never fires the event that would clear a stale true. Calling
	 * NotifyVehicleReset() from UnPossessed() would therefore leave a phantom
	 * "held" reset/shift flag latched with nothing left to unlatch it. This function
	 * is the unpossession-specific counterpart: same smoothing-state clear as
	 * NotifyVehicleReset(), but PendingSample is fully reset (FVehicleInputRawSample())
	 * rather than reconstructed field-by-field -- so unlike NotifyVehicleReset(), the
	 * held flags AND SpeedCms are both dropped, not preserved. Corrected on code review
	 * (VEH-005 LOW-4, repair cycle 2 re-review): SpeedCms is set fresh next Tick by
	 * whatever repossesses the pawn regardless, so dropping it here is harmless, but the
	 * comment previously undersold what actually gets cleared.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicle|Input")
	void NotifyUnpossessed();

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

#if WITH_AUTOMATION_TESTS
	/**
	 * VEH-006 TEST-ONLY SEAM: write a raw device sample directly, with no Enhanced Input.
	 *
	 * -----------------------------------------------------------------------
	 * Why this exists, and why it is not "just make the handlers public"
	 * -----------------------------------------------------------------------
	 *
	 * VEH-006 has to prove the whole input-to-physics chain -- shaping, rate limiting,
	 * the reset hold, the stale-sample guard -- against a car that is actually being
	 * simulated by Chaos. Every earlier vehicle spec tested FVehicleInputProcessor in
	 * isolation, which proves the arithmetic and proves nothing about the wiring.
	 *
	 * The chain's real entry point is eight private Enhanced Input handlers
	 * (HandleThrottle and friends), each reachable only through a UInputAction, a
	 * UInputMappingContext, a UEnhancedInputComponent and a possessing APlayerController.
	 * A test that stood all of that up would be testing Enhanced Input, not this project,
	 * and it would need content assets for the actions -- which CLAUDE.md's content rules
	 * would then require in the licence ledger, for a test.
	 *
	 * So the seam enters one level below the handlers, at the raw sample they all write.
	 * It is guarded by WITH_AUTOMATION_TESTS rather than merely documented as test-only,
	 * so it is compiled out of a shipping build entirely and no future gameplay code can
	 * come to depend on it.
	 *
	 * @param Sample  raw device values. May be non-finite or out of range on purpose --
	 *                the processor is the trust boundary and hostile values are a
	 *                legitimate thing for a test to inject.
	 */
	void InjectRawSampleForTesting(const FVehicleInputRawSample& Sample);
#endif

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

	/**
	 * VEH-004: stamp PendingSample with the current monotonic time.
	 *
	 * Called by EVERY handler. See FVehicleInputRawSample::SampleTimestampSeconds and
	 * EVehicleInputCorrection::StaleSample -- a handler that updates a value without
	 * refreshing the stamp would let the processor neutralise a control the driver is
	 * actively using.
	 */
	void MarkSampleFresh();

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
