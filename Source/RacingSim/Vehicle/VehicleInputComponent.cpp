// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/VehicleInputComponent.h"

#include "Core/RacingSimLog.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"

UVehicleInputComponent::UVehicleInputComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// TG_PrePhysics, explicitly rather than by default.
	//
	// The command produced this frame must be visible to the Chaos movement
	// component before it steps, or every input is applied one frame late -- a
	// constant ~16 ms of added latency at 60 Hz that, over Pixel Streaming, stacks on
	// top of a network round trip that Gate F already budgets to 80 ms p50. A
	// one-frame input delay is also exactly the kind of defect that gets blamed on
	// the stream rather than on the game.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

bool UVehicleInputComponent::InitialiseForController(
	APlayerController* Controller,
	const ERacingInputDeviceType DeviceType)
{
	if (Config == nullptr)
	{
		// Warning, not Error, and the component keeps running. An unconfigured input
		// component is a content mistake, and a car that cannot be driven says so far
		// more usefully than a crash on possession does.
		UE_LOG(LogRacingVehicle, Warning,
			TEXT("UVehicleInputComponent on '%s' has no UVehicleInputConfigDataAsset; the vehicle will not respond to input."),
			*GetNameSafe(GetOwner()));
		return false;
	}

	const bool bHasProfile = Processor.ConfigureFromAsset(Config, DeviceType);

	if (!bHasProfile)
	{
		// Named explicitly, because the alternative failure mode -- silently using
		// another device's profile -- presents as "the gamepad feels laggy" with
		// nothing in any log to point at. See UVehicleInputConfigDataAsset::FindProfile.
		UE_LOG(LogRacingVehicle, Warning,
			TEXT("No input profile for device '%s' in '%s'; falling back to neutral shaping (no dead zone, linear, instant)."),
			*UEnum::GetValueAsString(DeviceType),
			*GetNameSafe(Config));
	}

	if (Controller == nullptr)
	{
		UE_LOG(LogRacingVehicle, Warning,
			TEXT("UVehicleInputComponent on '%s' initialised with a null controller; no mapping context was pushed."),
			*GetNameSafe(GetOwner()));
		return false;
	}

	// ULocalPlayer, not APlayerController, owns the Enhanced Input subsystem. A
	// remote or AI-controlled pawn legitimately has no local player and must not warn
	// loudly about it, so this is a Verbose line rather than a Warning.
	const ULocalPlayer* LocalPlayer = Controller->GetLocalPlayer();
	if (LocalPlayer == nullptr)
	{
		UE_LOG(LogRacingVehicle, Verbose,
			TEXT("Controller '%s' has no ULocalPlayer; no mapping context pushed (expected for non-local pawns)."),
			*GetNameSafe(Controller));
		return false;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();

	if (Subsystem == nullptr)
	{
		UE_LOG(LogRacingVehicle, Error,
			TEXT("UEnhancedInputLocalPlayerSubsystem is unavailable; the EnhancedInput plugin may be disabled."));
		return false;
	}

	const TSoftObjectPtr<UInputMappingContext>* ContextPtr = Config->MappingContexts.Find(DeviceType);
	if (ContextPtr == nullptr || ContextPtr->IsNull())
	{
		UE_LOG(LogRacingVehicle, Warning,
			TEXT("No UInputMappingContext configured for device '%s' in '%s'; no controls are bound."),
			*UEnum::GetValueAsString(DeviceType),
			*GetNameSafe(Config));
		return false;
	}

	// Synchronous load. See the header: this runs at possession, never during a race,
	// and an asynchronously-not-yet-bound input layer at lights-out is worse than a
	// hitch on a loading screen.
	UInputMappingContext* Context = ContextPtr->LoadSynchronous();
	if (Context == nullptr)
	{
		UE_LOG(LogRacingVehicle, Error,
			TEXT("Failed to load UInputMappingContext '%s'."), *ContextPtr->ToString());
		return false;
	}

	// Cleared first so re-initialising on a device change does not leave the previous
	// device's context stacked underneath, where its bindings would still fire.
	Subsystem->RemoveMappingContext(Context);
	Subsystem->AddMappingContext(Context, MappingContextPriority);

	UE_LOG(LogRacingVehicle, Log,
		TEXT("Vehicle input initialised for device '%s' using context '%s' at priority %d."),
		*UEnum::GetValueAsString(DeviceType), *GetNameSafe(Context), MappingContextPriority);

	return bHasProfile;
}

UInputAction* UVehicleInputComponent::ResolveAction(const EVehicleInputAction Slot) const
{
	if (Config == nullptr)
	{
		return nullptr;
	}

	const TSoftObjectPtr<UInputAction>* Binding = Config->ActionBindings.Find(Slot);
	if (Binding == nullptr || Binding->IsNull())
	{
		return nullptr;
	}

	return Binding->LoadSynchronous();
}

void UVehicleInputComponent::BindActions(UEnhancedInputComponent* EnhancedInput)
{
	if (EnhancedInput == nullptr || Config == nullptr)
	{
		return;
	}

	// Which trigger events each slot needs, and why they differ.
	//
	// Analog axes bind Triggered AND Completed. Triggered alone is not enough: when a
	// stick returns to exact centre or a trigger is fully released, Enhanced Input
	// fires Completed and STOPS firing Triggered, so a Triggered-only binding leaves
	// the last non-zero value latched -- a car stuck at part throttle with the pad on
	// the table. Completed is what writes the zero.
	auto BindAxis = [this, EnhancedInput](const EVehicleInputAction Slot, void (UVehicleInputComponent::*Handler)(const FInputActionValue&))
	{
		if (UInputAction* Action = ResolveAction(Slot))
		{
			EnhancedInput->BindAction(Action, ETriggerEvent::Triggered, this, Handler);
			EnhancedInput->BindAction(Action, ETriggerEvent::Completed, this, Handler);
		}
		else
		{
			// Silent for optional slots would hide a missing REQUIRED slot too, and
			// Validate() has already reported the required ones by name. This is the
			// runtime echo of that, at Verbose so an intentionally unbound clutch does
			// not spam a shipping log.
			UE_LOG(LogRacingVehicle, Verbose,
				TEXT("Input slot '%s' is not bound in '%s'."),
				*UEnum::GetValueAsString(Slot), *GetNameSafe(Config));
		}
	};

	BindAxis(EVehicleInputAction::Throttle,  &UVehicleInputComponent::HandleThrottle);
	BindAxis(EVehicleInputAction::Brake,     &UVehicleInputComponent::HandleBrake);
	BindAxis(EVehicleInputAction::Steer,     &UVehicleInputComponent::HandleSteer);
	BindAxis(EVehicleInputAction::Handbrake, &UVehicleInputComponent::HandleHandbrake);
	BindAxis(EVehicleInputAction::Clutch,    &UVehicleInputComponent::HandleClutch);

	// Digital slots bind the same two events for the same reason: the processor
	// derives its own edges from a HELD state (FVehicleInputRawSample), so it needs
	// the falling edge as much as the rising one. A shift key whose release is never
	// reported would never produce a second shift, and a reset key whose release is
	// never reported would stay latched forever.
	BindAxis(EVehicleInputAction::ShiftUp,   &UVehicleInputComponent::HandleShiftUp);
	BindAxis(EVehicleInputAction::ShiftDown, &UVehicleInputComponent::HandleShiftDown);
	BindAxis(EVehicleInputAction::Reset,     &UVehicleInputComponent::HandleReset);
}

// Handlers. Every one of these records a raw value and does nothing else -- see the
// class comment on why no decision may live in this file.
void UVehicleInputComponent::HandleThrottle(const FInputActionValue& Value)
{
	PendingSample.Throttle = Value.Get<float>();
}

void UVehicleInputComponent::HandleBrake(const FInputActionValue& Value)
{
	PendingSample.Brake = Value.Get<float>();
}

void UVehicleInputComponent::HandleSteer(const FInputActionValue& Value)
{
	PendingSample.Steer = Value.Get<float>();
}

void UVehicleInputComponent::HandleHandbrake(const FInputActionValue& Value)
{
	PendingSample.Handbrake = Value.Get<float>();
}

void UVehicleInputComponent::HandleClutch(const FInputActionValue& Value)
{
	PendingSample.Clutch = Value.Get<float>();
}

void UVehicleInputComponent::HandleShiftUp(const FInputActionValue& Value)
{
	// Get<bool>() on a UInputAction configured as Axis1D returns "magnitude != 0",
	// so this works whether the action is authored Digital or Axis1D. That matters
	// because the action assets do not exist yet and VEH-001 must not constrain how
	// whoever authors them sets the value type.
	PendingSample.bShiftUpHeld = Value.Get<bool>();
}

void UVehicleInputComponent::HandleShiftDown(const FInputActionValue& Value)
{
	PendingSample.bShiftDownHeld = Value.Get<bool>();
}

void UVehicleInputComponent::HandleReset(const FInputActionValue& Value)
{
	PendingSample.bResetHeld = Value.Get<bool>();
}

void UVehicleInputComponent::SetVehicleSpeedCms(const float SpeedCms)
{
	// Stored raw, including non-finite values. Sanitising here would hide a broken
	// movement component behind a plausible number; the processor and
	// GetSteerScaleForSpeedCms both treat a non-finite speed as stationary, which is
	// the safe reading, and the correction is recorded on the command.
	PendingSample.SpeedCms = SpeedCms;
}

void UVehicleInputComponent::NotifyVehicleReset()
{
	Processor.ResetState();

	// The buffered axis values are cleared too, and this is not redundant with
	// ResetState. PendingSample holds the last value Enhanced Input reported; after a
	// reset, the very next Tick would otherwise feed the pre-reset throttle straight
	// back into a freshly cleared rate limiter, undoing the clear within one frame.
	//
	// The button HELD flags are cleared here where the processor deliberately keeps
	// them, and that is safe for the opposite reason: Enhanced Input re-reports a
	// still-held key on the next Triggered callback, so a genuinely held button
	// restores itself immediately, whereas the processor has no such source of truth.
	PendingSample = FVehicleInputRawSample();
}

void UVehicleInputComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Exactly one command per frame, from the buffered sample. See PendingSample's
	// comment for why the buffer exists.
	//
	// FPlatformTime::Seconds() is the monotonic source RACE-001 uses for lap timing.
	// It is sampled here rather than inside the processor so a test can drive time
	// deterministically -- the processor takes the timestamp as a parameter.
	Processor.Tick(PendingSample, DeltaTime, FPlatformTime::Seconds());

	// Digital slots are NOT cleared here. Enhanced Input reports a held key by firing
	// Triggered every frame and a released one by firing Completed once, so the held
	// flags are edge-maintained by the handlers and clearing them each Tick would
	// make every held key look like a one-frame tap. The analog values are left
	// standing for the same reason.
}
