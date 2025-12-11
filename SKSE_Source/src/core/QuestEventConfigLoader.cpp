#include "core/QuestEventConfigLoader.h"

#include <fstream>
#include <nlohmann/json.hpp>

#include "utils/FormUtils.h"

namespace MARAS {

    // Static member initialization
    QuestEventConfigLoader::LoadStatistics QuestEventConfigLoader::s_lastStats{};

    bool QuestEventConfigLoader::LoadFromFolder(const std::string& folderPath, QuestEventManager& manager) {
        namespace fs = std::filesystem;

        s_lastStats = LoadStatistics{};

        if (!fs::exists(folderPath) || !fs::is_directory(folderPath)) {
            MARAS_LOG_ERROR("Quest event config folder does not exist: {}", folderPath);
            return false;
        }

        MARAS_LOG_INFO("Loading quest event configurations from folder: {}", folderPath);

        // Clear existing configurations before loading new ones
        manager.ClearConfig();

        try {
            for (const auto& entry : fs::directory_iterator(folderPath)) {
                if (IsValidJsonFile(entry.path())) {
                    s_lastStats.totalFiles++;

                    if (LoadFromFile(entry.path(), manager)) {
                        s_lastStats.successfulFiles++;
                    }
                }
            }
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("Error scanning quest event config folder '{}': {}", folderPath, e.what());
            return false;
        }

        MARAS_LOG_INFO("Quest event config loading complete. Files: {}/{}, Quests: {}/{}, Invalid keys: {}, "
                       "Unresolved: {}",
                       s_lastStats.successfulFiles, s_lastStats.totalFiles, s_lastStats.validQuests,
                       s_lastStats.totalQuests, s_lastStats.invalidFormKeys, s_lastStats.unresolvedQuests);

        return s_lastStats.successfulFiles > 0;
    }

    bool QuestEventConfigLoader::LoadFromFile(const std::filesystem::path& filePath, QuestEventManager& manager) {
        if (!IsValidJsonFile(filePath)) {
            MARAS_LOG_WARN("Invalid JSON file: {}", filePath.string());
            return false;
        }

        MARAS_LOG_DEBUG("Loading quest event config from file: {}", filePath.string());

        try {
            std::ifstream file(filePath);
            if (!file.is_open()) {
                MARAS_LOG_ERROR("Cannot open file: {}", filePath.string());
                return false;
            }

            nlohmann::json json;
            file >> json;

            if (!json.is_object()) {
                MARAS_LOG_ERROR("JSON file is not an object: {}", filePath.string());
                return false;
            }

            size_t fileQuests = 0;

            for (const auto& [key, value] : json.items()) {
                // Skip metadata entries
                if (key.starts_with("__metaInfo")) {
                    continue;
                }

                // Only process form data keys
                if (!key.starts_with("__formData|")) {
                    MARAS_LOG_WARN("Ignoring non-form key: {}", key);
                    continue;
                }

                s_lastStats.totalQuests++;

                // Parse the form key
                auto questFormID = Utils::ParseAndResolveFormKey(key);
                if (!questFormID.has_value()) {
                    s_lastStats.invalidFormKeys++;
                    MARAS_LOG_WARN("Failed to parse form key: {}", key);
                    continue;
                }

                // Verify quest exists
                auto quest = RE::TESForm::LookupByID<RE::TESQuest>(questFormID.value());
                if (!quest) {
                    s_lastStats.unresolvedQuests++;
                    MARAS_LOG_WARN("Quest not found for ID {:08X} from key: {}", questFormID.value(), key);
                    continue;
                }

                // Parse quest event configuration
                QuestEventConfig config = ParseQuestConfig(key, value);
                config.questFormID = questFormID.value();

                // Only store if there are actual event handlers
                bool hasHandlers = !config.onStartCommands.empty() || !config.onStopCommands.empty() ||
                                   !config.onStageChangeCommands.empty();

                if (hasHandlers) {
                    MARAS_LOG_DEBUG(
                        "Loaded quest event config for {} (0x{:08X}): onStart={}, onStop={}, stages={}", quest->GetName(),
                        questFormID.value(), config.onStartCommands.size(), config.onStopCommands.size(),
                        config.onStageChangeCommands.size());

                    // Store in manager (direct access to internal map)
                    manager.questConfigs_[questFormID.value()] = std::move(config);
                    s_lastStats.validQuests++;
                    fileQuests++;
                } else {
                    MARAS_LOG_WARN("Quest config has no event handlers: {} (0x{:08X})", quest->GetName(),
                                   questFormID.value());
                }
            }

            MARAS_LOG_INFO("Loaded {} quest event configs from file: {}", fileQuests, filePath.string());
            return true;
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("Failed to load quest event config from '{}': {}", filePath.string(), e.what());
            return false;
        }
    }

    QuestEventConfig QuestEventConfigLoader::ParseQuestConfig(const std::string& formKey, const nlohmann::json& jsonObj) {
        QuestEventConfig config;

        if (!jsonObj.is_object()) {
            MARAS_LOG_WARN("Quest config is not an object for key: {}", formKey);
            return config;
        }

        // Parse onStart commands
        if (jsonObj.contains("onStart") && jsonObj["onStart"].is_array()) {
            config.onStartCommands = ParseCommandArray(jsonObj["onStart"]);
        }

        // Parse onStop commands
        if (jsonObj.contains("onStop") && jsonObj["onStop"].is_array()) {
            config.onStopCommands = ParseCommandArray(jsonObj["onStop"]);
        }

        // Parse onStageChange commands (can be multiple stages)
        for (const auto& [key, value] : jsonObj.items()) {
            if (key.starts_with("onStageChange:")) {
                std::string stageStr = key.substr(14);  // Remove "onStageChange:" prefix
                try {
                    uint16_t stage = static_cast<uint16_t>(std::stoul(stageStr));
                    if (value.is_array()) {
                        config.onStageChangeCommands[stage] = ParseCommandArray(value);
                    } else {
                        MARAS_LOG_WARN("onStageChange:{} value is not an array for key: {}", stage, formKey);
                    }
                } catch (const std::exception& e) {
                    MARAS_LOG_ERROR("Failed to parse stage number from '{}': {}", key, e.what());
                }
            }
        }

        return config;
    }

    std::vector<QuestCommand> QuestEventConfigLoader::ParseCommandArray(const nlohmann::json& commandArray) {
        std::vector<QuestCommand> commands;

        for (const auto& commandValue : commandArray) {
            QuestCommand command = ParseCommand(commandValue);
            if (!command.commandType.empty() && !command.npcSpecifier.empty()) {
                commands.push_back(std::move(command));
            } else {
                MARAS_LOG_WARN("Skipping invalid command: type='{}', npc='{}', arg='{}'", command.commandType,
                               command.npcSpecifier, command.argument);
            }
        }

        return commands;
    }

    QuestCommand QuestEventConfigLoader::ParseCommand(const nlohmann::json& commandValue) {
        QuestCommand command;

        if (commandValue.is_string()) {
            // Format: "commandType:npcSpecifier:argument"
            std::string commandStr = commandValue.get<std::string>();
            auto firstColon = commandStr.find(':');

            if (firstColon != std::string::npos) {
                command.commandType = commandStr.substr(0, firstColon);
                auto secondColon = commandStr.find(':', firstColon + 1);

                if (secondColon != std::string::npos) {
                    command.npcSpecifier = commandStr.substr(firstColon + 1, secondColon - firstColon - 1);
                    command.argument = commandStr.substr(secondColon + 1);
                } else {
                    MARAS_LOG_WARN("Command string missing second ':' separator (expected format "
                                   "'commandType:npcSpecifier:argument'): {}",
                                   commandStr);
                }
            } else {
                MARAS_LOG_WARN("Command string missing first ':' separator: {}", commandStr);
            }
        } else if (commandValue.is_object()) {
            // Alternative format: {"type": "commandType", "npc": "npcSpecifier", "arg": "argument"}
            if (commandValue.contains("type") && commandValue["type"].is_string()) {
                command.commandType = commandValue["type"].get<std::string>();
            }
            if (commandValue.contains("npc") && commandValue["npc"].is_string()) {
                command.npcSpecifier = commandValue["npc"].get<std::string>();
            }
            if (commandValue.contains("arg") && commandValue["arg"].is_string()) {
                command.argument = commandValue["arg"].get<std::string>();
            }
        } else {
            MARAS_LOG_WARN("Command value is not a string or object");
        }

        return command;
    }

    bool QuestEventConfigLoader::IsValidJsonFile(const std::filesystem::path& filePath) {
        return filePath.extension() == ".json" && std::filesystem::is_regular_file(filePath);
    }

}  // namespace MARAS
