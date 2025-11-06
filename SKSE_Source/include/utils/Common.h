#pragma once

// Common includes for the MARAS project
#include "PCH.h"

namespace MARAS {
    // Version information
    constexpr auto PROJECT_NAME = "MARAS"sv;
    constexpr auto PROJECT_VERSION = "1.0.0"sv;

    // Common type aliases
    using FormID = RE::FormID;
    using ActorPtr = RE::Actor*;
    using GameDay = uint32_t;

    // Forward declarations for commonly used classes
    class NPCRelationshipManager;
}

// Logging macros specific to MARAS
#define MARAS_LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define MARAS_LOG_WARN(...) spdlog::warn(__VA_ARGS__)
#define MARAS_LOG_ERROR(...) spdlog::error(__VA_ARGS__)
#define MARAS_LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
