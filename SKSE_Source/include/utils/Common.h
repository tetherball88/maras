#pragma once

// Common includes for the MARAS project
#include "PCH.h"
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <filesystem>

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

    // Global logger pointer - declared extern, defined in plugin.cpp
    // This ensures the SAME logger instance is used across ALL translation units
    extern std::shared_ptr<spdlog::logger> g_Logger;

    // Logger accessor - returns the global logger
    inline std::shared_ptr<spdlog::logger> GetLogger() {
        return g_Logger;
    }
}

// Logging macros specific to MARAS - uses cached logger to avoid conflicts
#define MARAS_LOG_TRACE(...) MARAS::GetLogger()->trace(__VA_ARGS__)
#define MARAS_LOG_DEBUG(...) MARAS::GetLogger()->debug(__VA_ARGS__)
#define MARAS_LOG_INFO(...) MARAS::GetLogger()->info(__VA_ARGS__)
#define MARAS_LOG_WARN(...) MARAS::GetLogger()->warn(__VA_ARGS__)
#define MARAS_LOG_ERROR(...) MARAS::GetLogger()->error(__VA_ARGS__)
