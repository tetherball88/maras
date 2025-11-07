#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

#include "utils/Common.h"

namespace MARAS::Utils {

    // Structure to hold override data for a single NPC
    struct NPCOverrideData {
        std::string comment;      // Optional comment field
        std::string socialClass;  // Empty if no override
        std::string skillType;    // Empty if no override
        std::string temperament;  // Empty if no override

        NPCOverrideData() = default;

        bool HasSocialClassOverride() const { return !socialClass.empty(); }
        bool HasSkillTypeOverride() const { return !skillType.empty(); }
        bool HasTemperamentOverride() const { return !temperament.empty(); }
        bool HasAnyOverride() const {
            return HasSocialClassOverride() || HasSkillTypeOverride() || HasTemperamentOverride();
        }
    };

    // Container for all loaded overrides
    using OverrideMap = std::unordered_map<FormID, NPCOverrideData>;

    class JsonOverrideLoader {
    public:
        // Load all override files from a directory
        static bool LoadOverridesFromFolder(const std::string& folderPath, OverrideMap& outOverrides);

        // Load overrides from a single JSON file
        static bool LoadOverridesFromFile(const std::string& filePath, OverrideMap& outOverrides);

        // Validate override values against known enum types
        static bool ValidateOverrideData(const NPCOverrideData& data);

        // Get statistics about loaded overrides
        struct LoadStatistics {
            size_t totalFiles;
            size_t successfulFiles;
            size_t totalOverrides;
            size_t validOverrides;
            size_t invalidFormKeys;
            size_t unresolvedForms;
        };

        static LoadStatistics GetLastLoadStatistics();

    private:
        static LoadStatistics s_lastStats;

        // Helper methods
        static bool ParseJsonFile(const std::string& filePath, OverrideMap& outOverrides);
        static NPCOverrideData ParseOverrideObject(const nlohmann::json& jsonObj);
        static bool IsValidJsonFile(const std::filesystem::path& filePath);
    };

}  // namespace MARAS::Utils
