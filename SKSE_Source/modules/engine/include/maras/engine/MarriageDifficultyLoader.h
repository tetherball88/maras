#pragma once

#include "maras/domain/relationships/MarriageDifficultyRules.h"

#include <atomic>
#include <filesystem>

namespace MARAS::Engine {

    /// Loads `marriage_difficulty.json` / `marriageDifficultyConfig.json` into `MarriageDifficultyConfig`.
    class MarriageDifficultyLoader {
    public:
        static MarriageDifficultyLoader& GetSingleton();

        MarriageDifficultyLoader(const MarriageDifficultyLoader&) = delete;
        MarriageDifficultyLoader& operator=(const MarriageDifficultyLoader&) = delete;
        MarriageDifficultyLoader(MarriageDifficultyLoader&&) = delete;
        MarriageDifficultyLoader& operator=(MarriageDifficultyLoader&&) = delete;

        /// Loads the default config file (falling back to defaults if unavailable).
        bool LoadConfig(const std::filesystem::path& customPath = {});

        [[nodiscard]] bool IsLoaded() const noexcept { return loaded_.load(std::memory_order_acquire); }

        [[nodiscard]] const MARAS::Domain::Relationships::MarriageDifficultyConfig& GetConfig() const noexcept {
            return config_;
        }

    private:
        MarriageDifficultyLoader() = default;
        ~MarriageDifficultyLoader() = default;

        MARAS::Domain::Relationships::MarriageDifficultyConfig config_{
            MARAS::Domain::Relationships::MarriageDifficultyConfig::CreateDefault()
        };
        std::atomic<bool> loaded_{ false };
    };

}  // namespace MARAS::Engine
