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
 * CLAUDE.md forbids noisy per-frame logging. Anything on a per-tick path must use
 * Verbose or VeryVerbose, not Log, so that it can be compiled out.
 *
 * ---------------------------------------------------------------------------
 * Compile-time stripping: how it actually works (CORE-002, finding N-1)
 * ---------------------------------------------------------------------------
 *
 * The previous version of this comment claimed the strip was controlled by the
 * *second* macro parameter. It is not, and the categories below were declared
 * `All`, which meant nothing was compiled out in any configuration. Verified in
 * engine source rather than assumed:
 *
 *   DECLARE_LOG_CATEGORY_EXTERN(CategoryName, DefaultVerbosity, CompileTimeVerbosity)
 *     -- Logging/LogCategory.h:133-134, expanding to
 *        FLogCategory<ELogVerbosity::DefaultVerbosity, ELogVerbosity::CompileTimeVerbosity>
 *        (LogCategory.h:118-119).
 *
 *   The 2nd parameter, DefaultVerbosity, is the *runtime* default. It is only a
 *   default: -LogCmds, DefaultEngine.ini [Core.Log] and `Log <cat> <verbosity>`
 *   all change it at runtime, so it strips nothing and cannot be relied on to.
 *
 *   The 3rd parameter, CompileTimeVerbosity, is the strip. UE_LOG expands to
 *   `if constexpr ((Verbosity & VerbosityMask) <= COMPILED_IN_MINIMUM_VERBOSITY)`
 *   and `if CategoryConst (... <= Category.GetCompileTimeVerbosity())`
 *   (LogMacros.h:351-353), so a call above the category's CompileTimeVerbosity
 *   is discarded by the compiler along with its argument evaluation.
 *
 *   ELogVerbosity ordering (LogVerbosity.h:16-61): Fatal < Error < Warning <
 *   Display < Log < Verbose < VeryVerbose, and `All = VeryVerbose`. So `All`
 *   compiles in *everything*, and `Log` keeps Fatal..Log while discarding
 *   Verbose and VeryVerbose.
 *
 * Policy chosen here: keep everything in Development/DebugGame, and strip
 * Verbose/VeryVerbose from Shipping and Test builds. Shipping is what runs on
 * the Pixel Streaming GPU worker, where a per-tick UE_LOG costs frame time on
 * the machine whose frame time is the product.
 *
 * Note the second, independent gate in the expansion above:
 * COMPILED_IN_MINIMUM_VERBOSITY (LogMacros.h:93-99) is a global ceiling that
 * defaults to VeryVerbose and can only be defined in a monolithic build. It is
 * not set by this project, so the per-category value below is the operative one.
 * NO_LOGGING is a third, blunter switch that removes Log and Warning too; it is
 * not used here.
 */

/**
 * Maximum verbosity compiled into the binary, per configuration.
 *
 * Passed as the CompileTimeVerbosity (third) argument below. It expands to a
 * bare enumerator name because the engine macro pastes it after
 * `::ELogVerbosity::`.
 *
 * UE_BUILD_TEST is included with UE_BUILD_SHIPPING deliberately: Test is the
 * configuration used for performance measurement, and measuring a build that
 * logs more than Shipping does measures the wrong build.
 */
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
	#define RACINGSIM_LOG_COMPILE_TIME_VERBOSITY Log
#else
	#define RACINGSIM_LOG_COMPILE_TIME_VERBOSITY All
#endif

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
RACINGSIM_API DECLARE_LOG_CATEGORY_EXTERN(LogRacingCore, Log, RACINGSIM_LOG_COMPILE_TIME_VERBOSITY);

/** Vehicle pawn, input, tune data, physics, assists, camera hooks. */
RACINGSIM_API DECLARE_LOG_CATEGORY_EXTERN(LogRacingVehicle, Log, RACINGSIM_LOG_COMPILE_TIME_VERBOSITY);

/** Track definition, checkpoints, lap validation, timing, race state, results. */
RACINGSIM_API DECLARE_LOG_CATEGORY_EXTERN(LogRacingRace, Log, RACINGSIM_LOG_COMPILE_TIME_VERBOSITY);

/** HUD view models, widgets, menus, countdown, results, input prompts. */
RACINGSIM_API DECLARE_LOG_CATEGORY_EXTERN(LogRacingUI, Log, RACINGSIM_LOG_COMPILE_TIME_VERBOSITY);

/** Pixel Streaming integration, browser messages, connection telemetry. */
RACINGSIM_API DECLARE_LOG_CATEGORY_EXTERN(LogRacingStreaming, Log, RACINGSIM_LOG_COMPILE_TIME_VERBOSITY);
