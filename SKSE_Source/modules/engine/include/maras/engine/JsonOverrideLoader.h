#pragma once

#include <RE/Skyrim.h>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MARAS::Engine {

    /// Raw NPC classification override values read from JSON.
    struct NPCOverrideData {
        std::optional<std::string> socialClass;
        std::optional<std::string> skillType;
        std::optional<std::string> temperament;

        [[nodiscard]] bool HasAnyOverride() const noexcept {
            return socialClass.has_value() || skillType.has_value() || temperament.has_value();
        }
    };

    /// Map from resolved NPC FormID to its JSON override attributes.
    using OverrideMap = std::unordered_map<RE::FormID, NPCOverrideData>;

    /// Callback invoked for each `__formData|...` entry inside a scanned JSON file.
    using FormDataJsonEntryCallback =
        std::function<void(const std::string& formKey, const nlohmann::json& entryJson, const std::filesystem::path& sourceFile)>;

    /// Loads JSON config files and `__formData|Plugin|0xFormID` NPC override definitions.
    class JsonOverrideLoader {
    public:
        struct LoadStatistics {
            std::size_t totalFiles{ 0 };
            std::size_t successfulFiles{ 0 };
            std::size_t totalEntries{ 0 };
            std::size_t validEntries{ 0 };
            std::size_t invalidFormKeys{ 0 };
            std::size_t unresolvedForms{ 0 };
        };

        /// Returns true if `filePath` exists, is a regular file, and has a `.json` extension.
        [[nodiscard]] static bool IsValidJsonFile(const std::filesystem::path& filePath);

        /// Parses `filePath` into `outJson` (allowing JSON comments). Logs context on failure.
        [[nodiscard]] static bool LoadJsonFile(const std::filesystem::path& filePath,
                                               nlohmann::json& outJson,
                                               std::string_view context = {});

        /// Returns sorted regular `.json` files inside `folderPath` (optionally recursive).
        [[nodiscard]] static std::vector<std::filesystem::path> CollectSortedJsonFiles(
            const std::filesystem::path& folderPath,
            bool recursive = false);

        /// Scans a single JSON file for `__formData|...` entries (directly or under `"NPCs"`).
        static bool ScanFormDataJsonFile(const std::filesystem::path& filePath,
                                         const FormDataJsonEntryCallback& callback,
                                         std::size_t* outTotalEntries = nullptr);

        /// Scans all valid `.json` files in `folderPath` for `__formData|...` entries.
        static bool ScanFormDataJsonFolder(const std::filesystem::path& folderPath,
                                           const FormDataJsonEntryCallback& callback,
                                           std::size_t* outTotalFiles = nullptr,
                                           std::size_t* outSuccessfulFiles = nullptr,
                                           std::size_t* outTotalEntries = nullptr);

        /// Loads and merges all valid NPC override files from `folderPath` into `outOverrides`.
        static bool LoadOverridesFromFolder(const std::filesystem::path& folderPath, OverrideMap& outOverrides);

        /// Loads NPC override entries from `filePath` into `outOverrides`.
        static bool LoadOverridesFromFile(const std::filesystem::path& filePath, OverrideMap& outOverrides);

        /// Validates optional socialClass, skillType, and temperament strings on `data`.
        [[nodiscard]] static bool ValidateOverrideData(const NPCOverrideData& data);

        /// Returns statistics from the most recent `LoadOverridesFromFolder` call.
        [[nodiscard]] static LoadStatistics GetLastLoadStatistics();

    private:
        static NPCOverrideData ParseOverrideEntry(const nlohmann::json& entry);

        static inline LoadStatistics lastStats_{};
    };

}  // namespace MARAS::Engine
