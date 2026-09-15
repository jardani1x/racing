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

EVehicleInputInitResult UVehicleInputComponent::InitialiseForController(
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
		return EVehicleInputInitResult::NoConfig;
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
		return EVehicleInputInitResult::NoController;
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
		return EVehicleInputInitResult::NotLocalPlayer;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();

	if (Subsystem == nullptr)
	{
		UE_LOG(LogRacingVehicle, Error,
			TEXT("UEnhancedInputLocalPlayerSubsystem is unavailable; the EnhancedInput plugin may be disabled."));
		return EVehicleInputInitResult::NoInputSubsystem;
	}

	const TSoftObjectPtr<UInputMappingContext>* ContextPtr = Config->MappingContexts.Find(DeviceType);
	if (ContextPtr == nullptr || ContextPtr->IsNull())
	{
		UE_LOG(LogRacingVehicle, Warning,
			TEXT("No UInputMappingContext configured for device '%s' in '%s'; no controls are bound."),
			*UEnum::GetValueAsString(DeviceType),
			*GetNameSafe(Config));
		return EVehicleInputInitResult::NoMappingContext;
	}

	// Synchronous load. See the header: this runs at possession, never during a race,
	// and an asynchronously-not-yet-bound input layer at lights-out is worse than a
	// hitch on a loading screen.
	UInputMappingContext* Context = ContextPtr->LoadSynchronous();
	if (Context == nullptr)
	{
		UE_LOG(LogRacingVehicle, Error,
			TEXT("Failed to load UInputMappingContext '%s'."), *ContextPtr->ToString());
		return EVehicleInputInitResult::MappingContextLoadFailed;
	}

	// Remove the PREVIOUS context this component pushed (VEH-002, fixing VEH-001
	// MEDIUM-1): removing the incoming Context only de-duplicates against re-adding
	// the same context and does nothing about a prior device's context, which would
	// otherwise stay mapped at the same priority and keep firing into PendingSample
	// after a device switch. IsValid() guards the case where nothing has been pushed
	// yet, or where the previous context has already been GC'd/unloaded.
	if (PushedContext.IsValid())
	{
		Subsystem->RemoveMappingContext(PushedContext.Get());
	}
	Subsystem->AddMappingContext(Context, MappingContextPriority);
	PushedContext = Context;

	UE_LOG(LogRacingVehicle, Log,
		TEXT("Vehicle input initialised for device '%s' using context '%s' at priority %d."),
		*UEnum::GetValueAsString(DeviceType), *GetNameSafe(Context), MappingContextPriority);

	// The context IS pushed and the component IS usable at this point even with no
	// profile -- neutral shaping still drives a car. But the missing profile is a
	// content fault and must not be reported as success, which is exactly what the old
	// `return bHasProfile` did while every other failure path returned a bare `false`:
	// one function, two meanings of the same value.
	return bHasProfile ? EVehicleInputInitResult::Succeeded : EVehicleInputInitResult::NoProfileForDevice;
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

void UVehicleInputComponent::MarkSampleFresh()
{
	// VEH-004. The one line that makes the stale-sample guard work, and it must run in
	// EVERY handler -- a slot that updates its value without refreshing the stamp would
	// be neutralised while the driver was actively using it.
	//
	// FPlatformTime::Seconds() is the same monotonic source RACE-001 uses for lap
	// timing and TickComponent already passes to the processor. Sampled here rather
	// than in Tick because the question is "when did a DEVICE last speak", which a
	// per-frame timestamp cannot answer: Tick runs whether or not anything was received.
	PendingSample.SampleTimestampSeconds = FPlatformTime::Seconds();
}

// Handlers. Every one of these records a raw value and stamps the sample, and does
// nothing else -- see the class comment on why no decision may live in this file.
void UVehicleInputComponent::HandleThrottle(const FInputActionValue& Value)
{
	PendingSample.Throttle = Value.Get<float>();
	MarkSampleFresh();
}

void UVehicleInputComponent::HandleBrake(const FInputActionValue& Value)
{
	PendingSample.Brake = Value.Get<float>();
	MarkSampleFresh();
}

void UVehicleInputComponent::HandleSteer(const FInputActionValue& Value)
{
	PendingSample.Steer = Value.Get<float>();
	MarkSampleFresh();
}

void UVehicleInputComponent::HandleHandbrake(const FInputActionValue& Value)
{
	PendingSample.Handbrake = Value.Get<float>();
	MarkSampleFresh();
}

void UVehicleInputComponent::HandleClutch(const FInputActionValue& Value)
{
	PendingSample.Clutch = Value.Get<float>();
	MarkSampleFresh();
}

void UVehicleInputComponent::HandleShiftUp(const FInputActionValue& Value)
{
	// Get<bool>() on a UInputAction configured as Axis1D returns "magnitude != 0",
	// so this works whether the action is authored Digital or Axis1D. That matters
	// because the action assets do not exist yet and VEH-001 must not constrain how
	// whoever authors them sets the value type.
	PendingSample.bShiftUpHeld = Value.Get<bool>();
	MarkSampleFresh();
}

void UVehicleInputComponent::HandleShiftDown(const FInputActionValue& Value)
{
	PendingSample.bShiftDownHeld = Value.Get<bool>();
	MarkSampleFresh();
}

void UVehicleInputComponent::HandleReset(const FInputActionValue& Value)
{
	PendingSample.bResetHeld = Value.Get<bool>();
	MarkSampleFresh();
}

void UVehicleInputComponent::SetVehicleSpeedCms(const float SpeedCms)
{
	// Stored raw, including non-finite values. Sanitising here would hide a broken
	// movement component behind a plausible number; the processor and
	// GetSteerScaleForSpeedCms both treat a non-finite speed as stationary, which is
	// the safe reading, and the correction is recorded on the command.
	PendingSample.SpeedCms = SpeedCms;
}

#if WITH_AUTOMATION_TESTS
void UVehicleInputComponent::InjectRawSampleForTesting(const FVehicleInputRawSample& Sample)
{
	// SpeedCms is deliberately NOT taken from the injected sample. It is the one field
	// of PendingSample the component does not own: the pawn pushes it every Tick via
	// SetVehicleSpeedCms, from the movement component's real forward speed. Letting a
	// test overwrite it would silently disable speed-sensitive steering -- the test
	// would drive at 200 km/h while the steering curve was told the car was stationary,
	// and would then "prove" a steering authority the game never grants.
	const double PreservedSpeedCms = PendingSample.SpeedCms;
	PendingSample = Sample;
	PendingSample.SpeedCms = PreservedSpeedCms;

	// Same call every real handler makes. Without it the stale-sample guard would treat
	// injected input as never-spoken and neutralise it after InputStaleAfterSeconds.
	MarkSampleFresh();
}
#endif

void UVehicleInputComponent::NotifyVehicleReset()
{
	Processor.ResetState();

	// The buffered AXIS values are cleared, and this is not redundant with
	// ResetState. PendingSample holds the last value Enhanced Input reported; after a
	// reset, the very next Tick would otherwise feed the pre-reset throttle straight
	// back into a freshly cleared rate limiter, undoing the clear within one frame.
	//
	// The button HELD flags are PRESERVED here -- corrected on code review (VEH-005
	// MEDIUM-1). They used to be cleared on the theory that Enhanced Input re-reports a
	// still-held key before this component's own next TickComponent runs, but nothing
	// enforces that ordering: Enhanced Input's Triggered callback and this component's
	// TG_PrePhysics tick have no declared prerequisite between them. If TickComponent
	// ran first, a genuinely still-held reset key would read as bResetHeld == false for
	// one frame, which drives the processor's "released" branch and clears BOTH
	// ResetHeldSeconds and bResetLatched -- exactly the re-arm ResetState()'s own
	// comment says must never happen (it lets a continued hold fire a second reset one
	// HoldThreshold later, repeating forever). Preserving the held flags here, the same
	// way ResetState() preserves the processor's own latch, closes that race instead of
	// depending on tick order to avoid it.
	const bool bShiftUpHeld = PendingSample.bShiftUpHeld;
	const bool bShiftDownHeld = PendingSample.bShiftDownHeld;
	const bool bResetHeld = PendingSample.bResetHeld;
	const float SpeedCms = PendingSample.SpeedCms;

	PendingSample = FVehicleInputRawSample();

	PendingSample.bShiftUpHeld = bShiftUpHeld;
	PendingSample.bShiftDownHeld = bShiftDownHeld;
	PendingSample.bResetHeld = bResetHeld;
	PendingSample.SpeedCms = SpeedCms;
}

void UVehicleInputComponent::NotifyUnpossessed()
{
	Processor.ResetState();

	// Unlike NotifyVehicleReset(), the held flags are NOT preserved -- see this
	// function's header comment (VEH-005 MEDIUM-B). Unbinding the input actions means
	// no future Enhanced Input callback will ever correct a stale true here.
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
