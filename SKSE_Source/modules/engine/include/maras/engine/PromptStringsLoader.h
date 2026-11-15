#pragma once

#include <atomic>
#include <filesystem>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace MARAS::Engine {

    /// Loads `promptStrings.json` for Papyrus dialogue prompt lookups (`MARAS.GetDialoguePrompt`).
    class PromptStringsLoader {
    public:
        static PromptStringsLoader& GetSingleton();

        PromptStringsLoader(const PromptStringsLoader&) = delete;
        PromptStringsLoader& operator=(const PromptStringsLoader&) = delete;
        PromptStringsLoader(PromptStringsLoader&&) = delete;
        PromptStringsLoader& operator=(PromptStringsLoader&&) = delete;

        /// Loads `Data/SKSE/Plugins/MARAS/promptStrings.json` (or fallback `SKSE/Plugins/MARAS/promptStrings.json`).
        bool Initialize();

        /// Loads prompt key -> template mappings from an explicit JSON file path.
        bool LoadFromFile(const std::filesystem::path& filePath);

        /// Clears all loaded prompt templates.
        void Clear() noexcept;

        [[nodiscard]] bool IsLoaded() const noexcept { return loaded_.load(std::memory_order_acquire); }

        /// Returns the stored prompt string for `promptKey`, or an empty string if absent.
        [[nodiscard]] std::string GetPrompt(std::string_view promptKey) const;

    private:
        PromptStringsLoader() = default;
        ~PromptStringsLoader() = default;

        mutable std::shared_mutex mutex_{};
        std::unordered_map<std::string, std::string> prompts_{};
        std::atomic<bool> loaded_{ false };
    };

}  // namespace MARAS::Engine
