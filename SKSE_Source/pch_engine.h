#pragma once

// Engine precompiled header shared by engine, app, bridges, and the MARAS plugin root.
// Must NEVER include nlohmann/*, yaml-cpp/*, or any `using namespace` directive.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "pch_core.h"

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
