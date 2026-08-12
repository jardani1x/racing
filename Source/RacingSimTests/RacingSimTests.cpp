// Copyright RacingSim. All Rights Reserved.

#include "RacingSimTestsLog.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogRacingTests);

// IMPLEMENT_MODULE, never IMPLEMENT_PRIMARY_GAME_MODULE. The primary game module is
// RacingSim (Source/RacingSim/RacingSim.cpp) and there must be exactly one.
IMPLEMENT_MODULE(FDefaultModuleImpl, RacingSimTests);
