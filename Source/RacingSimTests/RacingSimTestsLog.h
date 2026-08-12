// Copyright RacingSim. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/**
 * Logging category for the test module itself, per CORE-001.
 *
 * Separate from the five runtime layer categories so that diagnostics emitted by test
 * scaffolding -- fixture setup, harness failures, skipped preconditions -- are
 * distinguishable from output produced by the code under test. Without this, a test
 * that logs through LogRacingRace is indistinguishable from the Race layer logging on
 * its own behalf.
 *
 * No export macro is needed here: nothing links against this module.
 */
DECLARE_LOG_CATEGORY_EXTERN(LogRacingTests, Log, All);
