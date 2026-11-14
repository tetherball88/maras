#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include "maras/foundation/Logging.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <memory>
#include <random>

#include <cstdint>
#include <memory>
#include <random>
#include <string_view>

namespace MARAS {

    inline constexpr std::string_view PROJECT_NAME = "MARAS";
    inline constexpr std::string_view PROJECT_VERSION = "1.0.0";

    using FormID = RE::FormID;
    using ActorPtr = RE::Actor*;
    using GameDay = std::uint32_t;

    inline constexpr RE::FormID kPlayerRefID = 0x00000014u;
    inline constexpr RE::FormID kPlayerBaseID = 0x00000007u;
    inline constexpr RE::FormID kPlayerFormID = kPlayerRefID;
    inline constexpr RE::FormID kUnknownFatherFormID = 0xFFFFFFFEu;

    inline constexpr RE::FormID kSyntheticChildIDMin = 0xFF700000u;
    inline constexpr RE::FormID kSyntheticChildIDMax = 0xFF7FFFFFu;

    [[nodiscard]] constexpr bool IsSyntheticChildID(RE::FormID id) noexcept {
        return id >= kSyntheticChildIDMin && id <= kSyntheticChildIDMax;
    }

    [[nodiscard]] constexpr bool HasPhysicalActor(RE::FormID id) noexcept {
        return id != 0 && !IsSyntheticChildID(id);
    }

    // Defined in EngineLogging.cpp via function-local storage to avoid startup dynamic initializers.
    [[nodiscard]] const std::shared_ptr<spdlog::logger>& GetLogger() noexcept;

    namespace Utils {
        [[nodiscard]] inline float RandomFloat(float min = 0.0f, float max = 1.0f) {
            static thread_local std::mt19937 generator(std::random_device{}());
            std::uniform_real_distribution<float> distribution(min, max);
            return distribution(generator);
        }
    }  // namespace Utils

    namespace Engine {
        void InitializeFileLogger() noexcept;
        void InitializeFoundationLogSink() noexcept;
        void ApplyLogLevel(std::int32_t level) noexcept;
    }  // namespace Engine

}  // namespace MARAS

#define MARAS_LOG_TRACE(...)                                                      \
    do {                                                                          \
        if (const auto& _logger = MARAS::GetLogger()) _logger->trace(__VA_ARGS__); \
    } while (0)
#define MARAS_LOG_DEBUG(...)                                                      \
    do {                                                                          \
        if (const auto& _logger = MARAS::GetLogger()) _logger->debug(__VA_ARGS__); \
    } while (0)
#define MARAS_LOG_INFO(...)                                                      \
    do {                                                                         \
        if (const auto& _logger = MARAS::GetLogger()) _logger->info(__VA_ARGS__); \
    } while (0)
#define MARAS_LOG_WARN(...)                                                      \
    do {                                                                         \
        if (const auto& _logger = MARAS::GetLogger()) _logger->warn(__VA_ARGS__); \
    } while (0)
#define MARAS_LOG_ERROR(...)                                                       \
    do {                                                                          \
        if (const auto& _logger = MARAS::GetLogger()) _logger->error(__VA_ARGS__); \
    } while (0)
