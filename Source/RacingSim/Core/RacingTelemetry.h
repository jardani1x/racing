// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/RacingSimTypes.h"
#include "Core/RacingSimUnits.h"
#include "RacingTelemetry.generated.h"

/**
 * CORE-002 telemetry data contracts.
 *
 * CONTRACTS ONLY. Nothing here samples, ticks, allocates, subscribes to a
 * delegate or touches a world. These are the structs that Vehicle/, Race/, UI/
 * and Streaming/ agree on, defined once in Core/ so those layers do not have to
 * include each other to exchange a number.
 *
 * Four rules hold across every struct in this file.
 *
 * 1. UNITS. Storage is Unreal centimetres and centimetres per second, always.
 *    Every field says so in its name. Conversion to km/h or metres happens in
 *    UI/ via RacingSimUnits.h, at the moment of display, never on the way in.
 *
 * 2. TIME. Durations and timestamps are seconds as double, taken from the
 *    authoritative monotonic race clock (RACE-001) -- never from
 *    FPlatformTime::Seconds() at a call site, never from world time (which a
 *    pause or time dilation moves), and never pre-formatted. A struct here holds
 *    83.456; the HUD renders "1:23.456".
 *
 * 3. NO RACE TRUTH OUTSIDE Race/. A telemetry frame carrying LapNumber is a
 *    *copy* of what Race/ decided. Nothing may derive a lap, a validity or a
 *    position from these structs. Spline distance in particular ranks cars; it
 *    never authorises a lap, which only ordered gate crossings can do.
 *
 * 4. STALENESS IS EXPLICIT. Every frame carries the timestamp it was produced
 *    at, and consumers check it. A HUD showing a plausible frozen number is
 *    worse than one showing nothing, so passive widgets are given the means to
 *    tell the difference (see FRacingTelemetryFrame::IsStaleAt).
 */

/**
 * One vehicle-state sample. Produced by Vehicle/ at
 * URacingSimSettings::TelemetrySampleRateHz.
 *
 * Trivially copyable and deliberately flat -- it is copied into a ring buffer at
 * sample rate, so it holds no pointers, no TArray and no UObject reference.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FRacingVehicleTelemetrySample
{
	GENERATED_BODY()

	/** Race-clock time this sample was taken, in SECONDS. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry")
	double TimestampSeconds = 0.0;

	/** World location, Unreal units == CENTIMETRES. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry")
	FVector LocationCm = FVector::ZeroVector;

	/** Linear velocity in CENTIMETRES PER SECOND, world space. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry")
	FVector VelocityCms = FVector::ZeroVector;

	/** Forward speed in CENTIMETRES PER SECOND. Signed: negative means reversing. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry")
	double ForwardSpeedCms = 0.0;

	/** Engine speed, revolutions per minute. Not an Unreal unit; RPM is RPM. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry")
	float EngineRPM = 0.0f;

	/** Current gear. 0 is neutral, negative is reverse, matching Chaos Vehicles' convention. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry")
	int32 GearIndex = 0;

	/** Throttle, normalised 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry")
	float ThrottleInput = 0.0f;

	/** Brake, normalised 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry")
	float BrakeInput = 0.0f;

	/** Steering, normalised -1 (full left) .. 1 (full right). */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry")
	float SteerInput = 0.0f;

	/** Handbrake, normalised 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry")
	float HandbrakeInput = 0.0f;

	/** Which device produced the inputs in this sample. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry")
	ERacingInputDeviceType InputDeviceType = ERacingInputDeviceType::Unknown;

	/** UNIT BOUNDARY: cm/s -> km/h, for display only. Never store the result. */
	double GetForwardSpeedKph() const
	{
		return RacingSim::Units::CmsToKilometresPerHour(ForwardSpeedCms);
	}

	/** UNIT BOUNDARY: cm/s -> mph, for display only. Never store the result. */
	double GetForwardSpeedMph() const
	{
		return RacingSim::Units::CmsToMilesPerHour(ForwardSpeedCms);
	}

	/** UNIT BOUNDARY: cm/s -> m/s, for display or SI-facing export only. */
	double GetForwardSpeedMetresPerSecond() const
	{
		return RacingSim::Units::CmsToMetresPerSecond(ForwardSpeedCms);
	}
};

/**
 * Timing for one completed or in-progress lap. Produced by Race/ only.
 *
 * A lap is authorised by ordered gate crossings, never by distance. This struct
 * records the outcome of that decision; it cannot make it.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FRacingLapTiming
{
	GENERATED_BODY()

	/** 1-based lap number. 0 means "no lap yet" (out lap / pre-start). */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Timing", meta = (ClampMin = "0"))
	int32 LapNumber = 0;

	/** Total lap duration in SECONDS. 0 while the lap is still running. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Timing", meta = (ClampMin = "0.0"))
	double LapDurationSeconds = 0.0;

	/**
	 * Per-sector durations in SECONDS, in gate order. Length equals the track's
	 * sector count once the lap completes, and is shorter while it is running --
	 * a consumer must not assume a fixed length.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Timing")
	TArray<double> SectorDurationsSeconds;

	/** Validity of this lap as decided by Race/. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Timing")
	ERacingRunValidity Validity = ERacingRunValidity::Pending;

	/** Penalties attributed to this lap. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Timing")
	FRacingPenaltySummary Penalties;

	/** True once the lap has a terminal validity and a duration. */
	bool IsComplete() const
	{
		return LapNumber > 0
			&& LapDurationSeconds > 0.0
			&& Validity != ERacingRunValidity::Pending
			&& Validity != ERacingRunValidity::Unknown;
	}

	/** Sum of the recorded sector durations, in SECONDS. */
	double GetSectorTotalSeconds() const;

	/**
	 * Do the sectors add up to the lap, within Tolerance seconds?
	 *
	 * A cross-check, not a definition: sectors and the lap are timed from the
	 * same clock, so a mismatch beyond floating-point noise means a gate was
	 * missed or double-counted. Intended for automation and for a check at lap
	 * close, not for the HUD. Returns false on an incomplete lap.
	 */
	bool AreSectorsConsistent(double ToleranceSeconds = 0.001) const;
};

/**
 * Where a competitor is on the track, right now. Produced by Race/.
 *
 * SPLINE DISTANCE IS FOR RANKING ONLY. It orders cars and drives a progress bar.
 * It never authorises a lap, and no consumer may infer a lap completion from it
 * wrapping past the track length -- a car that is reset, teleported, or driven
 * backwards past the line will do exactly that without having driven a lap.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FRacingProgressSample
{
	GENERATED_BODY()

	/** Race-clock time of this sample, in SECONDS. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Progress")
	double TimestampSeconds = 0.0;

	/** Current 1-based lap. 0 before the first valid start-line crossing. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Progress", meta = (ClampMin = "0"))
	int32 LapNumber = 0;

	/** Index of the last correctly-crossed ordered gate. INDEX_NONE before the first. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Progress")
	int32 LastCheckpointIndex = INDEX_NONE;

	/** Distance along the track centreline spline, in CENTIMETRES. Ranking only. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Progress", meta = (ClampMin = "0.0"))
	double SplineDistanceCm = 0.0;

	/** Fraction of the current lap completed, 0..1, derived from SplineDistanceCm. Ranking and HUD only. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Progress", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LapProgressFraction = 0.0f;

	/** 1-based race position. 0 means "not yet classified". */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Progress", meta = (ClampMin = "0"))
	int32 RacePosition = 0;
};

/**
 * Stream-health sample. Produced by Streaming/.
 *
 * Separated from vehicle and race telemetry on purpose: CLAUDE.md forbids race
 * truth in Streaming/, and the cleanest enforcement of that is that the struct
 * Streaming/ fills has nowhere to put a lap time.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FRacingStreamingTelemetrySample
{
	GENERATED_BODY()

	/** Race-clock time of this sample, in SECONDS. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Streaming")
	double TimestampSeconds = 0.0;

	/** Round-trip time in MILLISECONDS (network convention; not converted to seconds). */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Streaming", meta = (ClampMin = "0.0"))
	float RoundTripMs = 0.0f;

	/** Jitter in MILLISECONDS. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Streaming", meta = (ClampMin = "0.0"))
	float JitterMs = 0.0f;

	/** Packet loss as a fraction 0..1, not a percentage. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Streaming", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PacketLossFraction = 0.0f;

	/** Encoder output bitrate, kilobits per second. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Streaming", meta = (ClampMin = "0"))
	int32 BitrateKbps = 0;

	/** Time spent in the encoder for the last frame, in MILLISECONDS. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Streaming", meta = (ClampMin = "0.0"))
	float EncodeMs = 0.0f;

	/** Streamed resolution in pixels. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Streaming", meta = (ClampMin = "0"))
	int32 FrameWidth = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|Streaming", meta = (ClampMin = "0"))
	int32 FrameHeight = 0;
};

/**
 * The single frame the HUD is allowed to read.
 *
 * This is the explicit view model contract: UI-001 builds a widget-facing view
 * model on top of this, and UMG widgets stay passive -- they read fields and
 * format them. No widget queries a pawn, a race director or a subsystem.
 *
 * Assembled by Race/ (which owns the clock and the truth) from the samples
 * above. One struct, one timestamp, so the HUD can never show a lap counter from
 * one instant next to a speed from another.
 */
USTRUCT(BlueprintType)
struct RACINGSIM_API FRacingTelemetryFrame
{
	GENERATED_BODY()

	/** Race-clock time this frame describes, in SECONDS. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry")
	double TimestampSeconds = 0.0;

	/** Elapsed session time in SECONDS, monotonic, unaffected by pause or time dilation. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry", meta = (ClampMin = "0.0"))
	double SessionElapsedSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry")
	FRacingVehicleTelemetrySample Vehicle;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry")
	FRacingProgressSample Progress;

	/** The lap in progress. LapDurationSeconds is the running time. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry")
	FRacingLapTiming CurrentLap;

	/** Best VALID lap so far this session. LapNumber == 0 means there is none yet. */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry")
	FRacingLapTiming BestLap;

	/**
	 * Delta to the best lap at the same point on track, in SECONDS. Negative is
	 * faster. Meaningless unless bHasDelta -- 0.0 is a legitimate delta value,
	 * so absence needs its own flag rather than a sentinel.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry")
	double DeltaToBestSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Racing|Telemetry")
	bool bHasDelta = false;

	/**
	 * Has this frame gone stale?
	 *
	 * NowSeconds and MaxAgeSeconds are both on the race clock;
	 * URacingSimSettings::TelemetryStaleAfterSeconds is the project default for
	 * MaxAgeSeconds. A frame from the future (clock reset, restart) counts as
	 * stale too: after a restart the previous frame's numbers must not persist
	 * on the HUD, and a negative age is exactly that situation.
	 */
	bool IsStaleAt(double NowSeconds, double MaxAgeSeconds) const;
};
