// Copyright RacingSim. All Rights Reserved.

#include "Vehicle/VehicleTelemetryTypes.h"

#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Core/RacingSimLog.h"
#include "Vehicle/VehicleTuneDataAsset.h"

FRacingVehicleTelemetrySample FVehicleTelemetrySnapshot::ToRacingVehicleSample() const
{
	// The lossy, one-directional bridge described in the header. Everything CORE-002's
	// HUD contract needs is present here; nothing here is derived from it.
	FRacingVehicleTelemetrySample Sample;
	Sample.TimestampSeconds = TimestampSeconds;
	Sample.LocationCm = LocationCm;
	Sample.VelocityCms = VelocityCms;
	Sample.ForwardSpeedCms = ForwardSpeedCms;
	Sample.EngineRPM = EngineRpm;
	Sample.GearIndex = GearIndex;
	Sample.ThrottleInput = ChaosInput.Throttle;
	Sample.BrakeInput = ChaosInput.Brake;
	Sample.SteerInput = ChaosInput.Steering;

	// Chaos takes a bool handbrake, the HUD contract stores a normalised float, and the
	// analog value the driver actually applied is not recoverable from the bool. 1.0/0.0
	// is therefore the honest widening: the HUD can say "handbrake on" and cannot claim
	// a lever position that was thresholded away by MapCommandToChaosInput.
	Sample.HandbrakeInput = ChaosInput.bHandbrake ? 1.0f : 0.0f;
	Sample.InputDeviceType = InputDeviceType;

	return Sample;
}

namespace RacingSim::Vehicle
{
	FVehicleTelemetrySnapshot CaptureVehicleTelemetry(const FVehicleTelemetryCaptureInput& Input)
	{
		FVehicleTelemetrySnapshot Snapshot;

		if (Input.Movement == nullptr)
		{
			// Invalid, not empty. bIsValid == false is the only way a consumer can tell
			// "no sample was taken" from "a stationary car at the origin", which is an
			// entirely ordinary reading. See the struct comment.
			return Snapshot;
		}

		Snapshot.bIsValid = true;
		Snapshot.SchemaVersion = VehicleTelemetrySchemaVersion;
		Snapshot.CarSpecVersion = Input.CarSpecVersion;
		Snapshot.TimestampSeconds = Input.TimestampSeconds;
		Snapshot.SimulationTimeSeconds = Input.SimulationTimeSeconds;
		Snapshot.FrameDeltaSeconds = Input.FrameDeltaSeconds;
		Snapshot.CaptureIndex = Input.CaptureIndex;

		Snapshot.ChaosInput = Input.ChaosInput;
		Snapshot.ClutchInput = Input.ClutchInput;
		Snapshot.InputCorrections = Input.InputCorrections;
		Snapshot.InputDeviceType = Input.InputDeviceType;

		// -- Drivetrain. All public const accessors on the component.
		Snapshot.ForwardSpeedCms = Input.Movement->GetForwardSpeed();
		Snapshot.EngineRpm = Input.Movement->GetEngineRotationSpeed();
		Snapshot.GearIndex = Input.Movement->GetCurrentGear();
		Snapshot.TargetGearIndex = Input.Movement->GetTargetGear();

		// -- Chassis rigid body, from the primitive rather than Chaos' protected
		// FVehicleState. A null chassis is survivable: the drivetrain and wheel state
		// below are still worth recording, and a movement component with no updated
		// component is a configuration fault a reader should be able to see the rest of.
		if (Input.Chassis != nullptr)
		{
			Snapshot.LocationCm = Input.Chassis->GetComponentLocation();
			Snapshot.Rotation = Input.Chassis->GetComponentRotation();
			Snapshot.VelocityCms = Input.Chassis->GetPhysicsLinearVelocity();
			Snapshot.AngularVelocityDegreesPerSecond = Input.Chassis->GetPhysicsAngularVelocityInDegrees();
		}

		// -- Per wheel. BOUNDED, because GetWheelState does not bound itself.
		const int32 ReportedWheels = Input.Movement->GetNumWheels();
		const int32 CapturedWheels = FMath::Clamp(ReportedWheels, 0, MaxVehicleTelemetryWheels);

		if (ReportedWheels > MaxVehicleTelemetryWheels)
		{
			// Named rather than dropped. A six-wheeled rig whose telemetry silently
			// showed four wheels would read as a correct recording of the wrong vehicle.
			UE_LOG(LogRacingVehicle, Warning,
				TEXT("Vehicle telemetry truncated: the movement component reports %d wheels but FVehicleTelemetrySnapshot holds %d. Raise MaxVehicleTelemetryWheels and bump VehicleTelemetrySchemaVersion."),
				ReportedWheels, MaxVehicleTelemetryWheels);
		}

		Snapshot.NumWheels = CapturedWheels;

		for (int32 WheelIndex = 0; WheelIndex < CapturedWheels; ++WheelIndex)
		{
			const FWheelStatus& Status = Input.Movement->GetWheelState(WheelIndex);
			FVehicleWheelTelemetry& Wheel = Snapshot.Wheels[WheelIndex];

			Wheel.bInContact = Status.bInContact;
			Wheel.NormalisedSuspensionLength = Status.NormalizedSuspensionLength;
			Wheel.SpringForceN = Status.SpringForce;
			Wheel.SlipAngleDegrees = Status.SlipAngle;
			Wheel.bIsSlipping = Status.bIsSlipping;
			Wheel.SlipMagnitudeCms = Status.SlipMagnitude;
			Wheel.bIsSkidding = Status.bIsSkidding;
			Wheel.SkidMagnitudeCms = Status.SkidMagnitude;
			Wheel.DriveTorqueNm = Status.DriveTorque;
			Wheel.BrakeTorqueNm = Status.BrakeTorque;
			Wheel.bAbsActive = Status.bABSActivated;
			Wheel.ContactPointCm = Status.ContactPoint;

			// The weak pointer itself is deliberately NOT stored -- a snapshot is copied
			// into a ring buffer at sample rate and must hold no UObject reference. Only
			// the fact that a material was resolved survives; naming the surface needs a
			// material-to-id table no ticket has authored.
			Wheel.bHasContactMaterial = Status.PhysMaterial.IsValid();

			// Non-const accessor, which is why FVehicleTelemetryCaptureInput::Movement
			// is a non-const pointer. Chaos returns centimetres here, matching every
			// other distance in this project.
			Wheel.SuspensionOffsetCm = Input.Movement->GetSuspensionOffset(WheelIndex);
		}

		return Snapshot;
	}

	FRacingContentVersion ResolveCarSpecVersion(
		const UVehicleTuneDataAsset* Tune,
		const bool bTuneWasApplied)
	{
		// Unpopulated is the honest answer in both refusal cases, and it is the answer
		// FRacingSimVersionStamp::IsPublishable() already knows how to refuse. See the
		// header for why "applied" and "referenced" are different questions.
		if (Tune == nullptr || !bTuneWasApplied)
		{
			return FRacingContentVersion();
		}

		return Tune->GetContentVersion();
	}
}
