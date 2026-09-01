// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "ChaosWheeledVehicleMovementComponent.h"
#include "CoreMinimal.h"
#include "Core/RacingSimTypes.h"
#include "GameFramework/Pawn.h"
#include "Vehicle/VehicleCameraMath.h"
#include "Vehicle/VehicleChaosInputMapping.h"
#include "Vehicle/VehicleChassisDataAsset.h"
#include "Vehicle/VehicleFailureDetection.h"
#include "Vehicle/VehicleTelemetryTypes.h"
#include "RacingVehiclePawn.generated.h"

class ATrackDefinitionActor;
class UBoxComponent;
class UCameraComponent;
class URaceLapTracker;
class URaceResultRecorder;
class USpringArmComponent;
class UVehicleCameraDataAsset;
class UVehicleFailureThresholdsDataAsset;
class UVehicleInputComponent;
class UVehicleTuneDataAsset;
struct FVehicleInputCommand;

/**
 * VEH-002: the first drivable prototype pawn.
 *
 * ---------------------------------------------------------------------------
 * What this ticket owns, and what it deliberately does not
 * ---------------------------------------------------------------------------
 *
 * This pawn is the ONE place that maps UVehicleChassisDataAsset's project-owned
 * EVehicleDrivetrainLayout onto Chaos' own EVehicleDifferential (see the DataAsset's
 * header for why that mapping lives here and not on the asset). It builds the
 * UChaosWheeledVehicleMovementComponent's WheelSetups from the chassis asset's
 * geometry and consumes VEH-001's FVehicleInputCommand every Tick. It does NOT own
 * the tune (VEH-003: torque curve, gear ratios, brake/steer response -- everything
 * this pawn sets below is a Phase-1-drivable placeholder, not a tuned value) and does
 * NOT own the reset pose (VEH-005).
 *
 * ---------------------------------------------------------------------------
 * Original, unbranded prototype, per CLAUDE.md
 * ---------------------------------------------------------------------------
 *
 * The chassis collision is a primitive UBoxComponent sized from the DataAsset's
 * half-extents -- no authored mesh, no licensed geometry, nothing that needs an
 * asset-ownership claim or a license-ledger entry. A visual mesh is a later content
 * task once Docs/13-AssetLicenseLedger.md records one.
 */
UCLASS()
class RACINGSIM_API ARacingVehiclePawn : public APawn
{
	GENERATED_BODY()

public:
	ARacingVehiclePawn(const FObjectInitializer& ObjectInitializer);

	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** The chassis asset in force. Set in the editor per-Blueprint; a null asset is refused at BeginPlay, not silently substituted. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vehicle")
	TObjectPtr<UVehicleChassisDataAsset> ChassisAsset;

	/**
	 * VEH-003 tune asset: engine, transmission, brakes, steering setup, suspension.
	 *
	 * Separate from ChassisAsset on purpose -- one car's geometry can carry several tunes
	 * (and one tune is meaningless on different geometry), and the two answer different
	 * questions. See UVehicleTuneDataAsset's header. A null tune is reported at BeginPlay
	 * and leaves Chaos' own defaults in place; it is not silently substituted.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vehicle")
	TObjectPtr<UVehicleTuneDataAsset> TuneAsset;

	/** Enhanced Input config, forwarded to the input component at possession. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vehicle")
	TObjectPtr<class UVehicleInputConfigDataAsset> InputConfigAsset;

	/** Device profile this pawn's input component initialises with. Runtime device switching is a later ticket's scope; see VEH-001's routed MEDIUM-1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vehicle")
	ERacingInputDeviceType InitialInputDeviceType = ERacingInputDeviceType::Keyboard;

	/**
	 * VEH-004 failure envelopes. Null is legal and uses FVehicleFailureThresholds'
	 * built-in defaults -- which are the same numbers the asset defaults to -- reported
	 * ONCE at BeginPlay rather than silently. No .uasset is authored by VEH-004
	 * (CLAUDE.md forbids editing Unreal binary assets from a worktree), so null is the
	 * expected configuration today.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vehicle|Telemetry")
	TObjectPtr<UVehicleFailureThresholdsDataAsset> FailureThresholdsAsset;

	/**
	 * VEH-005 camera tunables. Null is legal and uses FVehicleCameraSettings' own
	 * defaults, reported ONCE at BeginPlay -- the same fallback VEH-004 established for
	 * a null FailureThresholdsAsset. No .uasset is authored by this ticket (CLAUDE.md's
	 * binary-asset rule), so null is the expected configuration today.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vehicle|Camera")
	TObjectPtr<UVehicleCameraDataAsset> CameraAsset;

	/**
	 * Telemetry capture rate, HERTZ. 0 disables capture entirely.
	 *
	 * Docs/02-VehiclePhysics.md item 13: "Telemetry capture at the simulation rate or a
	 * DOCUMENTED DECIMATION RATE." This is the documented decimation rate, and it is a
	 * property rather than a constant because a soak run and a race want different
	 * answers. 60 Hz matches the Gate E frame budget: capture runs at most once per
	 * frame at the target rate and skips frames on a faster one.
	 *
	 * DECIMATION DOES NOT WEAKEN FAILURE DETECTION in the way it might appear to. The
	 * accumulating detectors integrate the MEASURED interval between captures, not a
	 * frame count, so a 60 Hz capture on a 144 Hz render still measures real seconds.
	 * What it does cost is resolution on single-step events -- a tunnelling step
	 * shorter than the capture interval is observed as part of a longer step -- which
	 * is why the tunnelling bound scales with the measured interval rather than an
	 * assumed one.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vehicle|Telemetry", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float TelemetrySampleRateHz = 60.0f;

	/** True once ChassisAsset has been applied to the movement component and wheel setups. False for a pawn spawned with no chassis (a validation failure, not a crash). */
	UFUNCTION(BlueprintPure, Category = "Vehicle")
	bool IsChassisApplied() const
	{
		return bChassisApplied;
	}

	/** The Chaos movement component, exposed for VEH-004's telemetry and VEH-003's tune application -- neither owns this Tick, both read from it. */
	UFUNCTION(BlueprintPure, Category = "Vehicle")
	UChaosWheeledVehicleMovementComponent* GetVehicleMovementComponent() const
	{
		return VehicleMovementComponent;
	}

	// =======================================================================
	// VEH-004 -- telemetry and failure detection
	// =======================================================================

	/**
	 * The most recent telemetry sample. bIsValid is false before the first capture.
	 *
	 * READ-ONLY BY CONTRACT and by return type: a copy, not a reference, so a consumer
	 * cannot retain a pointer into a struct this pawn overwrites every capture.
	 * Trivially copyable and fixed-size (see FVehicleTelemetrySnapshot), so the copy
	 * costs no allocation.
	 */
	UFUNCTION(BlueprintPure, Category = "Vehicle|Telemetry")
	FVehicleTelemetrySnapshot GetLastTelemetrySnapshot() const
	{
		return LastSnapshot;
	}

	/** How many samples have been captured this session. Gaps against the wall clock mean capture was decimated or skipped. */
	UFUNCTION(BlueprintPure, Category = "Vehicle|Telemetry")
	int64 GetTelemetryCaptureCount() const
	{
		return CaptureIndex;
	}

	/** Bitmask of EVehicleFailureFlag from the most recent evaluation. 0 when the last sample was clean. */
	UFUNCTION(BlueprintPure, Category = "Vehicle|Telemetry")
	uint8 GetLastFailureFlags() const
	{
		return LastFailureReport.Flags;
	}

	/** Human-readable reasoning for the most recent evaluation. Empty when clean. */
	UFUNCTION(BlueprintPure, Category = "Vehicle|Telemetry")
	FString GetLastFailureReason() const
	{
		return LastFailureReport.Reason;
	}

	/** The full report. Not a UFUNCTION: FVehicleFailureReport is a plain struct, not a USTRUCT, because the detector must stay UObject-free. */
	const FVehicleFailureReport& GetLastFailureReport() const
	{
		return LastFailureReport;
	}

	/**
	 * Drop accumulated failure history and the previous snapshot.
	 *
	 * MUST be called on any DELIBERATE discontinuity -- a reset, a teleport, a session
	 * restart -- for the same reason FVehicleInputProcessor::ResetState exists. Without
	 * it, VEH-005's reset would present to the detector as a teleport and be reported
	 * as tunnelling, which is correct behaviour applied to the wrong event.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicle|Telemetry")
	void NotifyTelemetryDiscontinuity();

	/**
	 * The car spec version this pawn may publish, or an UNPOPULATED version when the
	 * tune is missing or was never applied.
	 *
	 * The decision lives in RacingSim::Vehicle::ResolveCarSpecVersion, which is a pure
	 * function and therefore testable; this is the accessor that supplies it the one
	 * fact only the pawn knows (whether ApplyTuneAsset actually ran).
	 */
	UFUNCTION(BlueprintPure, Category = "Vehicle|Telemetry")
	bool HasPublishableCarSpecVersion() const;

	/**
	 * Hand this car's spec version to a race result recorder.
	 *
	 * ---------------------------------------------------------------------
	 * THIS IS THE PROJECT'S FIRST Vehicle/ -> Race/ DEPENDENCY. It is deliberate.
	 * ---------------------------------------------------------------------
	 *
	 * CORE-002 opened FRacingSimVersionStamp::CarSpecVersion and documented it as
	 * VEH-003's to fill. VEH-003 delivered the CAPABILITY
	 * (UVehicleTuneDataAsset::GetContentVersion) but nothing ever called
	 * URaceResultRecorder::SetCarSpecVersion, so IsPublishable() refused every real
	 * stamp -- a hole re-routed to "whichever ticket first wires a pawn's tune to a
	 * race result".
	 *
	 * The direction was a real choice. A Race-side pull (`Recorder->ReadCarFrom(Pawn)`)
	 * would keep Vehicle/ independent, but Race/ cannot answer the question that
	 * actually matters: whether the tune was APPLIED, as opposed to merely referenced.
	 * A pawn with a TuneAsset but no ChassisAsset never reaches ApplyTuneAsset() at all
	 * (see its early return), and a tune with an unusable torque curve has its engine
	 * write refused -- in both cases the car did not run that tune, and a result naming
	 * it would be plausible and wrong. Only the pawn knows. So the push direction is
	 * the one that can be correct, and the include is confined to this class's .cpp.
	 *
	 * @return true if a POPULATED version was published. False means the recorder was
	 *         null, or this pawn has nothing honest to publish -- in which case nothing
	 *         is written and the recorder keeps its unpopulated default, which
	 *         IsSubmittable() correctly refuses.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicle|Telemetry")
	bool PublishCarSpecVersionTo(URaceResultRecorder* Recorder);

	// =======================================================================
	// VEH-005 -- safe reset
	// =======================================================================

	/**
	 * Teleport this car back onto the track at or before its last valid progress, per
	 * `Docs/02-VehiclePhysics.md` item 12 ("reset/recovery that preserves race validity
	 * rules") and Gate B's "reset cannot award progress" rule.
	 *
	 * ---------------------------------------------------------------------
	 * PARAMETER INJECTION, NOT A STORED REFERENCE -- same shape as
	 * PublishCarSpecVersionTo(URaceResultRecorder*).
	 * ---------------------------------------------------------------------
	 *
	 * Vehicle must not hold a persistent pointer to a placed Race actor: a track or a
	 * lap tracker can be destroyed and recreated (session restart, level reload)
	 * independently of this pawn, and a pawn that cached one would either dangle or
	 * silently keep resetting onto a stale track. Both Track and LapTracker are
	 * forward-declared here and their real headers are included only in this class's
	 * .cpp, exactly as PublishCarSpecVersionTo confines its Race/ include.
	 *
	 * THIS PAWN DOES NOT CALL THIS METHOD ITSELF. Nothing in ApplyInputCommand invokes
	 * it when Command.bResetRequested is set -- per this ticket's acceptance criteria,
	 * a race-context owner (a GameMode/RaceDirector, which does not exist as a ticket
	 * yet) is the intended caller, because only that owner knows the current
	 * LastValidProgressDistanceCm and which LapTracker/Track are live. Wiring an
	 * internal call here would mean guessing at both.
	 *
	 * ---------------------------------------------------------------------
	 * Precondition this method trusts, and does not re-derive
	 * ---------------------------------------------------------------------
	 *
	 * RACE-002 `M3`: the caller's LastValidProgressDistanceCm must genuinely be the
	 * car's last-valid progress, not a distance ahead of it. This method sources the
	 * reset pose from ATrackDefinitionActor::GetResetPoseAtOrBeforeDistanceCm, which is
	 * ALREADY GUARANTEED, by construction and by TrackDefinitionActorSpec.cpp's own
	 * exhaustive tests, to return a distance at or before its input. If a future caller
	 * passes a distance ahead of the car's real last-valid progress, that is the
	 * caller's bug, not this method's -- this method adds a defence-in-depth check
	 * (RacingSim::Vehicle::IsResetDistanceAtOrBeforeQuery) that logs, rather than
	 * silently accepting, a returned distance that fails the bounded at-or-before test
	 * against the queried distance.
	 *
	 * RACE-002 `L1` (FindFirstGateCrossing vs EvaluateCrossings): N/A, verified -- this
	 * reset path reads track arc-length distance only and never touches checkpoint
	 * gate-crossing order.
	 *
	 * @param Track                        the track to reset onto. Null is a documented
	 *                                     no-op (nothing to reset onto), not a crash and
	 *                                     not a guessed pose.
	 * @param LapTracker                   notified of the reset when non-null, so
	 *                                     RACE-002's "reset cannot award progress"
	 *                                     mechanism actually runs.
	 * @param LastValidProgressDistanceCm  the car's own last-valid arc-length progress,
	 *                                     CENTIMETRES. See the precondition above.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vehicle")
	void ExecuteSafeReset(const ATrackDefinitionActor* Track, URaceLapTracker* LapTracker, double LastValidProgressDistanceCm);

protected:
	virtual void BeginPlay() override;

private:
	/** Chassis collision primitive. Root component; sized from ChassisAsset at BeginPlay. */
	UPROPERTY(VisibleAnywhere, Category = "Vehicle")
	TObjectPtr<UBoxComponent> ChassisCollision;

	UPROPERTY(VisibleAnywhere, Category = "Vehicle")
	TObjectPtr<UChaosWheeledVehicleMovementComponent> VehicleMovementComponent;

	/** VEH-001's input contract. Owned here rather than by the controller so a spectated or AI-driven pawn still has one. */
	UPROPERTY(VisibleAnywhere, Category = "Vehicle")
	TObjectPtr<UVehicleInputComponent> VehicleInputComp;

	/** VEH-005 camera hook. Root-attached, collision test enabled. VisibleAnywhere so a Blueprint child can retarget or extend it. */
	UPROPERTY(VisibleAnywhere, Category = "Vehicle|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** VEH-005 camera hook. Child of CameraBoom. VisibleAnywhere so a Blueprint child can retarget or extend it. */
	UPROPERTY(VisibleAnywhere, Category = "Vehicle|Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	/** Settings from CameraAsset, or FVehicleCameraSettings' own defaults when it is null. Mirrors ResolveFailureThresholds. */
	FVehicleCameraSettings ResolveCameraSettings() const;

	/** Applies ResolveCameraSettings() to CameraBoom/FollowCamera. Called once, from BeginPlay. */
	void ApplyCameraSettings();

	/**
	 * Builds WheelSetups and Mass/aerodynamics from ChassisAsset, and sets the Chaos
	 * differential from ChassisAsset's EVehicleDrivetrainLayout -- the one mapping this
	 * pawn exists to own. Called once, from BeginPlay. Idempotent guard via
	 * bChassisApplied: PossessedBy must not re-apply the chassis on every possession.
	 */
	void ApplyChassisAsset();

	/**
	 * VEH-003: writes TuneAsset onto the movement component's EngineSetup,
	 * TransmissionSetup and SteeringSetup, and maps EVehicleSteeringModel onto Chaos'
	 * ESteeringType -- the second project-enum-to-Chaos-enum mapping this pawn owns.
	 *
	 * Called from ApplyChassisAsset() immediately BEFORE RecreatePhysicsState(), because
	 * that call is what pushes the whole configuration into Chaos; a tune written after
	 * it would not take effect until something else recreated the state. Suspension and
	 * brake torques are NOT written here -- Chaos reads those from the wheel class default
	 * object, so they are cross-checked instead (PrototypeVehicleWheel.h).
	 *
	 * Guarded by its own bTuneApplied (code review, VEH-003 MEDIUM-4) -- previously relied
	 * only on its caller's bChassisApplied guard, which is correct today because
	 * ApplyChassisAsset() is this function's only caller, but left this function without
	 * a guard of its own despite being documented as "guarded and idempotent".
	 *
	 * `bTuneApplied` (re-entry guard, set unconditionally once this function has RUN) and
	 * `bTuneEngineApplied` (set only when the engine write actually SUCCEEDED) are
	 * deliberately two different flags -- corrected on code review (VEH-004 HIGH-2). The
	 * previous version used `bTuneApplied` for both purposes: it was set `true` on every
	 * path past the null-check, including when the torque-curve write was refused
	 * (VEH-003 HIGH-2's own gate), which meant `ResolveCarSpecVersion` -- and therefore a
	 * submitted race result -- would stamp a car-spec version for a tune whose engine was
	 * never actually written into Chaos. `bTuneEngineApplied` is the one that must gate
	 * publishability; `bTuneApplied` only stops this function from re-running.
	 */
	void ApplyTuneAsset();

	/** Maps FVehicleInputCommand onto the movement component's SetThrottleInput/SetBrakeInput/SetSteeringInput/SetHandbrakeInput. The one Tick-time consumer of VEH-001's contract. */
	/** @return the mapped axes actually pushed, so VEH-004's capture records those rather than re-deriving them. */
	FVehicleChaosInput ApplyInputCommand(const FVehicleInputCommand& Command);

	/**
	 * VEH-004: capture one snapshot and evaluate it, if the decimation clock allows.
	 *
	 * Called at the END of Tick, after ApplyInputCommand, so the recorded ChaosInput is
	 * the one actually pushed this frame rather than last frame's. Allocates nothing on
	 * the clean path and logs only on a CHANGE of failure flags -- a per-frame UE_LOG of
	 * a persistent fault is itself a frame-time defect, and it would also bury the
	 * moment the fault began under ten thousand identical lines.
	 */
	void CaptureAndEvaluateTelemetry(const FVehicleChaosInput& AppliedInput, const FVehicleInputCommand& Command, float DeltaSeconds);

	/** Thresholds from FailureThresholdsAsset, or FVehicleFailureThresholds' defaults when it is null. */
	FVehicleFailureThresholds ResolveFailureThresholds() const;

	bool bChassisApplied = false;
	bool bTuneApplied = false;
	bool bTuneEngineApplied = false;

	// -- VEH-004 telemetry state. All fixed-size; none of it allocates. ------

	/** The most recent capture. See GetLastTelemetrySnapshot. */
	FVehicleTelemetrySnapshot LastSnapshot;

	/**
	 * The capture before it, kept so the detector can measure rates.
	 *
	 * A separate member rather than a two-element ring: the detector needs exactly one
	 * step of history and nothing else, and a buffer would invite a consumer to start
	 * treating this pawn as a telemetry recorder -- which is VEH-006's job, not this
	 * ticket's.
	 */
	FVehicleTelemetrySnapshot PreviousSnapshot;

	/** The detector's accumulators. Held per pawn, so two cars cannot contaminate each other's history. */
	FVehicleFailureDetectorState FailureState;

	FVehicleFailureReport LastFailureReport;

	/** 1-based; see FVehicleTelemetrySnapshot::CaptureIndex. */
	int64 CaptureIndex = 0;

	/** Monotonic SECONDS at which the next capture is due. 0 means "capture on the next Tick". */
	double NextCaptureTimeSeconds = 0.0;

	/**
	 * Flags at the last log emission, so a persistent fault logs ONCE and a newly
	 * raised flag logs again. Edge-triggered logging, not level-triggered.
	 */
	uint8 LoggedFailureFlags = 0;
};
