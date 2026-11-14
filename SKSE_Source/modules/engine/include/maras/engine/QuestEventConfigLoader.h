#pragma once

#include <RE/Skyrim.h>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace MARAS::Engine {

    /// Single quest event action parsed from quest_events JSON configs.
    struct QuestCommand {
        std::string commandType;
        std::string npcSpecifier;
        std::string argument;
    };

    /// Configured start/stop/stage commands bound to a resolved Skyrim quest FormID.
    struct QuestEventConfig {
        RE::FormID questFormID{ 0 };
        std::vector<QuestCommand> onStartCommands;
        std::vector<QuestCommand> onStopCommands;
        std::unordered_map<std::uint16_t, std::vector<QuestCommand>> onStageChangeCommands;
    };

    /// Map from resolved quest FormID to its configured quest event commands.
    using QuestConfigMap = std::unordered_map<RE::FormID, QuestEventConfig>;

    /// Loads `quest_events/*.json` definitions into a `QuestConfigMap`.
    class QuestEventConfigLoader {
    public:
        struct LoadStatistics {
            std::size_t totalFiles{ 0 };
            std::size_t successfulFiles{ 0 };
            std::size_t totalQuests{ 0 };
            std::size_t validQuests{ 0 };
            std::size_t invalidFormKeys{ 0 };
            std::size_t unresolvedQuests{ 0 };
        };

        /// Clears `outConfigs` and loads all `.json` files from `folderPath`.
        static bool LoadFromFolder(const std::filesystem::path& folderPath, QuestConfigMap& outConfigs);

        /// Loads quest event configs from a single JSON file into `outConfigs`.
        static bool LoadFromFile(const std::filesystem::path& filePath, QuestConfigMap& outConfigs);

        /// Returns statistics from the most recent `LoadFromFolder` or `LoadFromFile` call.
        [[nodiscard]] static LoadStatistics GetLastLoadStatistics();

        /// Parses a single `"__formData|..."` quest config block.
        [[nodiscard]] static std::optional<QuestEventConfig> ParseQuestConfig(const std::string& formKey,
                                                                              const nlohmann::json& questJson);

        /// Parses an array of command entries into `QuestCommand` values.
        [[nodiscard]] static std::vector<QuestCommand> ParseCommandArray(const nlohmann::json& cmdArray);

        /// Parses one `"commandType:npcSpecifier:argument"` string or `{"type","npc","arg"}` command entry.
        [[nodiscard]] static std::optional<QuestCommand> ParseCommand(const nlohmann::json& cmdJson);

    private:
        static bool LoadFromFileInternal(const std::filesystem::path& filePath,
                                         QuestConfigMap& outConfigs,
                                         LoadStatistics& stats);

        static inline LoadStatistics lastStats_{};
    };

}  // namespace MARAS::Engine
