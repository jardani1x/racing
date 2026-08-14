// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * RACE-001: the authoritative race clock.
 *
 * ---------------------------------------------------------------------------
 * Why this is not an accumulator
 * ---------------------------------------------------------------------------
 *
 * The obvious implementation is `Elapsed += DeltaTime` in Tick. Gate B forbids it
 * in effect ("timer is monotonic and independent of render frame rate"), and it
 * fails in four separate ways that all look like a working timer until they do not:
 *
 *   1. it accumulates float/double rounding error once per frame, so the same lap
 *      measures differently at 30 fps and at 144 fps;
 *   2. DeltaTime is clamped by the engine (t.MaxFPS, MaxSmoothedFrameRate, and
 *      AWorldSettings::MaxUndilatedFrameTime), so a hitch under-reports real time
 *      by exactly the amount the hitch cost;
 *   3. it is scaled by time dilation, so a slomo console command is a lap-time
 *      cheat;
 *   4. it stops entirely if the owner's Tick is disabled, throttled, or the actor
 *      is temporarily not ticked -- and nothing reports that it stopped.
 *
 * So the clock stores two timestamps taken from a monotonic source and subtracts
 * them. Elapsed time is computed on demand, never accumulated. That makes it
 * exactly independent of frame rate, tick order, tick count, time dilation and
 * hitches: sampling once per lap and sampling ten thousand times per lap give bit-
 * identical answers for the same pair of timestamps.
 *
 * The direct consequence, and it is the point: this struct has no Tick, and the
 * state machine that owns it needs no timer handle. There is no timer to leak, to
 * fire after a restart, or to go stale against a torn-down owner.
 *
 * ---------------------------------------------------------------------------
 * Why there is still a ratchet
 * ---------------------------------------------------------------------------
 *
 * FPlatformTime::Seconds() is QueryPerformanceCounter on Windows, which is
 * monotonic. That is a property of one platform and one implementation, not a
 * guarantee this project may rest a leaderboard on: the time source here is
 * pluggable (see FRaceTimeSourceFn), other platforms are not all equally careful,
 * and a virtualised GPU worker is exactly the environment where clock sources
 * misbehave. HighWaterElapsedSeconds makes "the timer never moves backward"
 * structural rather than inherited. It costs one FMath::Max per sample.
 *
 * The ratchet is deliberately per-session. Reset() drops it to zero, because
 * re-zeroing on restart is required (Gate B: "reset cannot award progress") and is
 * not the same event as time running backwards. URaceStateMachine's session id is
 * what lets an observer tell those two apart.
 *
 * ---------------------------------------------------------------------------
 * Units and threading
 * ---------------------------------------------------------------------------
 *
 * Seconds, double, always. No milliseconds, no floats, no formatted strings --
 * formatting is UI/'s (Docs/03-TrackRaceUI.md: "Store raw duration with high
 * precision; format only in UI"). double gives ~15 significant digits, so a
 * sub-microsecond resolution on an hour-long session is not the limiting factor;
 * the time source is.
 *
 * Not thread-safe and not intended to be. Sample() mutates the ratchet, so all
 * access is game-thread only, like the state machine that owns it.
 *
 * Plain C++ struct, not a USTRUCT: it is internal race truth with a mutating
 * accessor, not a Blueprint-facing contract. URaceStateMachine exposes the two
 * numbers Blueprint/UMG actually needs.
 */

/**
 * A monotonic time source. Returns seconds in an arbitrary epoch -- only
 * *differences* between two calls are meaningful, never the absolute value.
 *
 * A raw function pointer rather than TFunction or a delegate, on purpose: it is
 * called on the HUD's read path, and a function pointer neither heap-allocates on
 * assignment (TFunction with a capturing lambda does) nor walks an invocation list.
 * The cost is that a test seam must be captureless, which every seam here is.
 */
using FRaceTimeSourceFn = double (*)();

namespace RacingSim::Race
{
	/**
	 * The production time source: FPlatformTime::Seconds().
	 *
	 * Wrapped in a named function rather than taking the address of the engine
	 * static directly, so that the one place the project chooses a clock is
	 * greppable and has somewhere to put a platform caveat.
	 */
	RACINGSIM_API double PlatformMonotonicSeconds();
}

/**
 * Start/stop/sample over a monotonic time source. See the file comment for why it
 * subtracts timestamps instead of accumulating deltas.
 *
 * Every mutating call takes the current monotonic reading as an argument rather
 * than fetching it. That keeps the struct free of any opinion about *which* clock
 * is authoritative -- URaceStateMachine owns that decision and passes one reading
 * through a whole transition, so a transition that touches both clocks stamps them
 * with the same instant instead of two readings microseconds apart.
 */
struct RACINGSIM_API FRaceClock
{
	/**
	 * Back to "never started". Idempotent, and the only operation that may lower a
	 * previously reported elapsed time.
	 */
	void Reset();

	/**
	 * Begin accruing from NowSeconds.
	 *
	 * @return true if this call started the clock; false if it was already running
	 *         (idempotent no-op) or NowSeconds was not finite (refused).
	 */
	bool Start(double NowSeconds);

	/**
	 * Freeze the elapsed time at NowSeconds. Idempotent: a second Stop cannot
	 * extend a frozen duration, which is what makes ERaceState::Finished able to
	 * "freeze the result once" even if the finish path is entered twice.
	 *
	 * @return true if this call stopped a running clock; false otherwise.
	 */
	bool Stop(double NowSeconds);

	/**
	 * Elapsed seconds, ratcheted so the value can never decrease within a session.
	 * Returns 0.0 before Start(). Mutates the ratchet -- game thread only.
	 */
	double Sample(double NowSeconds);

	/**
	 * The highest elapsed value ever returned by Sample(), without taking a new
	 * reading. This is the const read for anything that must not advance the clock
	 * (a HUD repaint, a log line, an assertion).
	 */
	double Peek() const { return HighWaterElapsedSeconds; }

	bool IsRunning() const { return bRunning; }

	/** True once Start() has succeeded and Reset() has not been called since. */
	bool HasStarted() const { return bHasStarted; }

private:
	/** Time source reading at Start(). Meaningless on its own; only Now - this matters. */
	double StartTimestampSeconds = 0.0;

	/** Elapsed at Stop(). Read instead of the live subtraction while !bRunning. */
	double FrozenElapsedSeconds = 0.0;

	/** The ratchet. See the file comment. */
	double HighWaterElapsedSeconds = 0.0;

	bool bRunning = false;
	bool bHasStarted = false;
};
