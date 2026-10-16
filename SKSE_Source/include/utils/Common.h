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

    // Player constants
    constexpr RE::FormID kPlayerRefID = 0x00000014;
    constexpr RE::FormID kPlayerBaseID = 0x00000007;
    constexpr RE::FormID kPlayerFormID = kPlayerRefID;

    // Unknown father sentinel constant (used when biological father cannot be pinpointed to an Actor)
    constexpr RE::FormID kUnknownFatherFormID = 0xFFFFFFFE;

    // Synthetic child FormID range (0xFF700000 - 0xFF7FFFFF) for biological children without a physical Actor
    constexpr RE::FormID kSyntheticChildIDMin = 0xFF700000;
    constexpr RE::FormID kSyntheticChildIDMax = 0xFF7FFFFF;

    constexpr bool IsSyntheticChildID(RE::FormID id) noexcept {
        return id >= kSyntheticChildIDMin && id <= kSyntheticChildIDMax;
    }

    constexpr bool HasPhysicalActor(RE::FormID id) noexcept {
        return id != 0 && !IsSyntheticChildID(id);
    }

    // Forward declarations for commonly used classes
    class NPCRelationshipManager;

    // Global logger pointer - declared extern, defined in plugin.cpp
    // This ensures the SAME logger instance is used across ALL translation units
    extern std::shared_ptr<spdlog::logger> g_Logger;

    // Logger accessor - returns the global logger
    inline const std::shared_ptr<spdlog::logger>& GetLogger() noexcept {
        return g_Logger;
    }

    namespace Utils {
        inline float RandomFloat(float min = 0.0f, float max = 1.0f) {
            static thread_local std::mt19937 generator(std::random_device{}());
            std::uniform_real_distribution<float> distribution(min, max);
            return distribution(generator);
        }
    }
}

// Logging macros specific to MARAS - null-safe against uninitialized g_Logger
#define MARAS_LOG_TRACE(...) do { if (const auto& _logger = MARAS::GetLogger()) _logger->trace(__VA_ARGS__); } while (0)
#define MARAS_LOG_DEBUG(...) do { if (const auto& _logger = MARAS::GetLogger()) _logger->debug(__VA_ARGS__); } while (0)
#define MARAS_LOG_INFO(...)  do { if (const auto& _logger = MARAS::GetLogger()) _logger->info(__VA_ARGS__); } while (0)
#define MARAS_LOG_WARN(...)  do { if (const auto& _logger = MARAS::GetLogger()) _logger->warn(__VA_ARGS__); } while (0)
#define MARAS_LOG_ERROR(...) do { if (const auto& _logger = MARAS::GetLogger()) _logger->error(__VA_ARGS__); } while (0)
