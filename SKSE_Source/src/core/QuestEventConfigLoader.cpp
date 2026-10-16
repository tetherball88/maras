#include "core/QuestEventConfigLoader.h"

#include "utils/FormUtils.h"
#include "utils/JsonOverrideLoader.h"

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

        // Clear existing configurations before loading new ones
        manager.ClearConfig();

        const bool ok = Utils::JsonOverrideLoader::ScanFormDataJsonFolder(
            folderPath, "QuestEventConfigLoader", s_lastStats.totalFiles, s_lastStats.successfulFiles,
            s_lastStats.totalQuests, s_lastStats.validQuests, s_lastStats.invalidFormKeys,
            [&](const std::string& key, const Utils::ParsedFormKey& parsedKey, const nlohmann::json& value) {
                auto* quest =
                    Utils::LookupForm<RE::TESQuest>(parsedKey.localFormID, parsedKey.pluginName, "QuestEventConfigLoader");
                if (!quest) {
                    s_lastStats.unresolvedQuests++;
                    MARAS_LOG_WARN("Quest not found for key: {}", key);
                    return false;
                }

                const auto questFormID = quest->GetFormID();
                QuestEventConfig config = ParseQuestConfig(key, value);
                config.questFormID = questFormID;

                const bool hasHandlers = !config.onStartCommands.empty() || !config.onStopCommands.empty() ||
                                         !config.onStageChangeCommands.empty();

                if (!hasHandlers) {
                    MARAS_LOG_WARN("Quest config has no event handlers: {} (0x{:08X})", quest->GetName(), questFormID);
                    return false;
                }

                MARAS_LOG_DEBUG("Loaded quest event config for {} (0x{:08X}): onStart={}, onStop={}, stages={}",
                                quest->GetName(), questFormID, config.onStartCommands.size(),
                                config.onStopCommands.size(), config.onStageChangeCommands.size());

                manager.questConfigs_[questFormID] = std::move(config);
                return true;
            });

        MARAS_LOG_INFO("Quest event config loading complete. Files: {}/{}, Quests: {}/{}, Invalid keys: {}, "
                       "Unresolved: {}",
                       s_lastStats.successfulFiles, s_lastStats.totalFiles, s_lastStats.validQuests,
                       s_lastStats.totalQuests, s_lastStats.invalidFormKeys, s_lastStats.unresolvedQuests);

        return ok;
    }

    bool QuestEventConfigLoader::LoadFromFile(const std::filesystem::path& filePath, QuestEventManager& manager) {
        return Utils::JsonOverrideLoader::ScanFormDataJsonFile(
            filePath, "QuestEventConfigLoader", s_lastStats.totalQuests, s_lastStats.validQuests,
            s_lastStats.invalidFormKeys,
            [&](const std::string& key, const Utils::ParsedFormKey& parsedKey, const nlohmann::json& value) {
                auto* quest =
                    Utils::LookupForm<RE::TESQuest>(parsedKey.localFormID, parsedKey.pluginName, "QuestEventConfigLoader");
                if (!quest) {
                    s_lastStats.unresolvedQuests++;
                    MARAS_LOG_WARN("Quest not found for key: {}", key);
                    return false;
                }

                const auto questFormID = quest->GetFormID();
                QuestEventConfig config = ParseQuestConfig(key, value);
                config.questFormID = questFormID;

                const bool hasHandlers = !config.onStartCommands.empty() || !config.onStopCommands.empty() ||
                                         !config.onStageChangeCommands.empty();

                if (!hasHandlers) {
                    MARAS_LOG_WARN("Quest config has no event handlers: {} (0x{:08X})", quest->GetName(), questFormID);
                    return false;
                }

                MARAS_LOG_DEBUG("Loaded quest event config for {} (0x{:08X}): onStart={}, onStop={}, stages={}",
                                quest->GetName(), questFormID, config.onStartCommands.size(),
                                config.onStopCommands.size(), config.onStageChangeCommands.size());

                manager.questConfigs_[questFormID] = std::move(config);
                return true;
            });
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
            if (!command.commandType.empty() && !command.npcSpecifier.empty() && !command.argument.empty()) {
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
            if (commandValue.contains("arg")) {
                if (commandValue["arg"].is_string()) {
                    command.argument = commandValue["arg"].get<std::string>();
                } else if (commandValue["arg"].is_number() || commandValue["arg"].is_boolean()) {
                    command.argument = commandValue["arg"].dump();
                }
            }
        } else {
            MARAS_LOG_WARN("Command value is not a string or object");
        }

        return command;
    }

    bool QuestEventConfigLoader::IsValidJsonFile(const std::filesystem::path& filePath) {
        return Utils::JsonOverrideLoader::IsValidJsonFile(filePath);
    }

}  // namespace MARAS
