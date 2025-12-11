#pragma once

#include <filesystem>
#include <string>
#include <nlohmann/json.hpp>

#include "core/QuestEventHandler.h"

namespace MARAS {

    // Helper class to load quest event configurations from JSON files
    class QuestEventConfigLoader {
    public:
        struct LoadStatistics {
            size_t totalFiles = 0;
            size_t successfulFiles = 0;
            size_t totalQuests = 0;
            size_t validQuests = 0;
            size_t invalidFormKeys = 0;
            size_t unresolvedQuests = 0;
        };

        // Load all JSON files from folder and populate the manager
        static bool LoadFromFolder(const std::string& folderPath, QuestEventManager& manager);

        // Get statistics from last load operation
        static LoadStatistics GetLastLoadStatistics() { return s_lastStats; }

    private:
        // Load a single JSON file
        static bool LoadFromFile(const std::filesystem::path& filePath, QuestEventManager& manager);

        // Parse quest event config from JSON object
        static QuestEventConfig ParseQuestConfig(const std::string& formKey, const nlohmann::json& jsonObj);

        // Parse command array from JSON
        static std::vector<QuestCommand> ParseCommandArray(const nlohmann::json& commandArray);

        // Parse a single command from JSON value
        static QuestCommand ParseCommand(const nlohmann::json& commandValue);

        // Validate JSON file
        static bool IsValidJsonFile(const std::filesystem::path& filePath);

        static LoadStatistics s_lastStats;
    };

}  // namespace MARAS
