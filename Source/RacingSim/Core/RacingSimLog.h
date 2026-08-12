// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/**
 * One logging category per architecture layer, per CORE-001.
 *
 * The layers are folders inside this single Runtime module, not separate modules
 * (decision recorded 2026-08-12 in Docs/15-ProjectStructure.md). That means the
 * boundaries between them are enforced by review rather than by the linker, and
 * these categories are the main mechanical aid: log output names the layer that
 * produced it, so a Streaming call sitting in Race code is visible at runtime.
 *
 * CLAUDE.md forbids noisy per-frame logging. Verbose and VeryVerbose are compiled
 * out of Shipping by the second parameter below; anything on a per-tick path must
 * use them, not Log.
 */

/*
 * RACINGSIM_API is required on every category below.
 *
 * DECLARE_LOG_CATEGORY_EXTERN emits a plain `extern`, which does not cross a DLL
 * boundary. Without the export macro these link fine inside this module and fail in
 * RacingSimTests with LNK2001. The engine does the same thing -- see
 * CORE_API DECLARE_LOG_CATEGORY_EXTERN(LogHAL, Log, All) in CoreGlobals.h.
 *
 * That link error is the module split doing its job: it is compile-time proof that
 * RacingSimTests is a genuinely separate binary, which is what keeps test code out of
 * a packaged build.
 */

/** Shared types, settings, telemetry contracts, state contracts. */
RACINGSIM_API DECLARE_LOG_CATEGORY_EXTERN(LogRacingCore, Log, All);

/** Vehicle pawn, input, tune data, physics, assists, camera hooks. */
RACINGSIM_API DECLARE_LOG_CATEGORY_EXTERN(LogRacingVehicle, Log, All);

/** Track definition, checkpoints, lap validation, timing, race state, results. */
RACINGSIM_API DECLARE_LOG_CATEGORY_EXTERN(LogRacingRace, Log, All);

/** HUD view models, widgets, menus, countdown, results, input prompts. */
RACINGSIM_API DECLARE_LOG_CATEGORY_EXTERN(LogRacingUI, Log, All);

/** Pixel Streaming integration, browser messages, connection telemetry. */
RACINGSIM_API DECLARE_LOG_CATEGORY_EXTERN(LogRacingStreaming, Log, All);
