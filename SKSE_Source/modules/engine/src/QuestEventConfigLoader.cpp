#include "maras/engine/QuestEventConfigLoader.h"

#include "maras/engine/EngineLogging.h"
#include "maras/engine/FormUtils.h"
#include "maras/engine/JsonOverrideLoader.h"
#include "maras/foundation/StringUtils.h"

#include <charconv>
#include <system_error>

namespace MARAS::Engine {

    namespace {

        [[nodiscard]] std::optional<std::uint16_t> ParseStageNumber(std::string_view raw) {
            const std::string_view trimmed = MARAS::Foundation::StringUtils::TrimView(raw);
            if (trimmed.empty()) {
                return std::nullopt;
            }
            std::uint16_t stage = 0;
            const char* begin = trimmed.data();
            const char* end = begin + trimmed.size();
            const auto [ptr, ec] = std::from_chars(begin, end, stage, 10);
            if (ec != std::errc{} || ptr != end) {
                return std::nullopt;
            }
            return stage;
        }

        [[nodiscard]] std::string JsonArgToString(const nlohmann::json& arg) {
            if (arg.is_string()) {
                return arg.get<std::string>();
            }
            if (arg.is_boolean()) {
                return arg.get<bool>() ? "true" : "false";
            }
            if (arg.is_number_integer()) {
                return std::to_string(arg.get<std::int64_t>());
            }
            if (arg.is_number_unsigned()) {
                return std::to_string(arg.get<std::uint64_t>());
            }
            if (arg.is_number_float()) {
                return std::to_string(arg.get<double>());
            }
            return arg.dump();
        }

    }  // namespace

    bool QuestEventConfigLoader::LoadFromFolder(const std::filesystem::path& folderPath, QuestConfigMap& outConfigs) {
        outConfigs.clear();
        lastStats_ = LoadStatistics{};

        std::error_code ec;
        if (!std::filesystem::exists(folderPath, ec) || !std::filesystem::is_directory(folderPath, ec)) {
            MARAS_LOG_WARN("[QuestEventConfigLoader] Folder does not exist: {}", folderPath.string());
            return false;
        }

        const auto files = JsonOverrideLoader::CollectSortedJsonFiles(folderPath, false);
        lastStats_.totalFiles = files.size();
        for (const auto& file : files) {
            if (LoadFromFileInternal(file, outConfigs, lastStats_)) {
                ++lastStats_.successfulFiles;
            }
        }

        MARAS_LOG_INFO(
            "[QuestEventConfigLoader] Loaded {} valid quest configs from {}/{} files in '{}' "
            "(totalEntries={}, invalidKeys={}, unresolved={})",
            lastStats_.validQuests, lastStats_.successfulFiles, lastStats_.totalFiles, folderPath.string(),
            lastStats_.totalQuests, lastStats_.invalidFormKeys, lastStats_.unresolvedQuests);
        return lastStats_.successfulFiles > 0 || files.empty();
    }

    bool QuestEventConfigLoader::LoadFromFile(const std::filesystem::path& filePath, QuestConfigMap& outConfigs) {
        lastStats_ = LoadStatistics{};
        lastStats_.totalFiles = 1;
        const bool ok = LoadFromFileInternal(filePath, outConfigs, lastStats_);
        if (ok) {
            ++lastStats_.successfulFiles;
        }
        return ok;
    }

    bool QuestEventConfigLoader::LoadFromFileInternal(const std::filesystem::path& filePath,
                                                      QuestConfigMap& outConfigs,
                                                      LoadStatistics& stats) {
        nlohmann::json root;
        if (!JsonOverrideLoader::LoadJsonFile(filePath, root, "QuestEventConfigLoader")) {
            return false;
        }
        if (!root.is_object()) {
            MARAS_LOG_WARN("[QuestEventConfigLoader] Root is not a JSON object in '{}'", filePath.string());
            return false;
        }

        const nlohmann::json* questsContainer = &root;
        if (root.contains("quests") && root["quests"].is_object()) {
            questsContainer = &root["quests"];
        }

        for (const auto& [formKey, questJson] : questsContainer->items()) {
            if (!MARAS::Foundation::StringUtils::StartsWith(formKey, "__formData|", false)) {
                continue;
            }

            ++stats.totalQuests;

            const auto parsedKey = MARAS::Utils::ParseFormKey(formKey);
            if (!parsedKey.isValid) {
                ++stats.invalidFormKeys;
                MARAS_LOG_WARN("[QuestEventConfigLoader] Invalid quest form key '{}' in '{}'",
                               formKey, filePath.string());
                continue;
            }

            auto* quest = MARAS::Utils::LookupForm<RE::TESQuest>(
                parsedKey.localFormID, parsedKey.pluginName, "QuestEventConfigLoader");
            if (!quest) {
                ++stats.unresolvedQuests;
                MARAS_LOG_DEBUG("[QuestEventConfigLoader] Quest '{}' in '{}' did not resolve in active load order",
                                formKey, filePath.string());
                continue;
            }

            auto parsedConfig = ParseQuestConfig(formKey, questJson);
            if (!parsedConfig.has_value()) {
                continue;
            }

            const bool hasHandlers = !parsedConfig->onStartCommands.empty() ||
                                     !parsedConfig->onStopCommands.empty() ||
                                     !parsedConfig->onStageChangeCommands.empty();
            if (!hasHandlers) {
                MARAS_LOG_WARN("[QuestEventConfigLoader] Quest config '{}' ({:08X}) has no event handlers",
                               formKey, quest->GetFormID());
                continue;
            }

            parsedConfig->questFormID = quest->GetFormID();
            outConfigs[parsedConfig->questFormID] = std::move(*parsedConfig);
            ++stats.validQuests;
        }

        return true;
    }

    QuestEventConfigLoader::LoadStatistics QuestEventConfigLoader::GetLastLoadStatistics() {
        return lastStats_;
    }

    std::optional<QuestEventConfig> QuestEventConfigLoader::ParseQuestConfig(const std::string& formKey,
                                                                             const nlohmann::json& questJson) {
        if (!questJson.is_object()) {
            MARAS_LOG_WARN("[QuestEventConfigLoader] Quest entry '{}' is not a JSON object", formKey);
            return std::nullopt;
        }

        QuestEventConfig config;
        if (const auto resolved = MARAS::Utils::ParseAndResolveFormKey(formKey)) {
            config.questFormID = *resolved;
        }

        if (questJson.contains("onStart") && questJson["onStart"].is_array()) {
            config.onStartCommands = ParseCommandArray(questJson["onStart"]);
        }
        if (questJson.contains("onStop") && questJson["onStop"].is_array()) {
            config.onStopCommands = ParseCommandArray(questJson["onStop"]);
        }

        // Primary legacy schema: "onStageChange:<stage>": [ ... ]
        for (const auto& [key, value] : questJson.items()) {
            if (key.starts_with("onStageChange:")) {
                const std::string_view stageStr = std::string_view(key).substr(14);
                const auto stage = ParseStageNumber(stageStr);
                if (!stage.has_value()) {
                    MARAS_LOG_WARN("[QuestEventConfigLoader] Invalid stage number '{}' for quest '{}'",
                                   stageStr, formKey);
                    continue;
                }
                if (!value.is_array()) {
                    MARAS_LOG_WARN("[QuestEventConfigLoader] '{}' value is not an array for quest '{}'",
                                   key, formKey);
                    continue;
                }
                config.onStageChangeCommands[*stage] = ParseCommandArray(value);
            }
        }

        // Optional nested schema: "onStageChange": { "<stage>": [ ... ] }
        if (questJson.contains("onStageChange") && questJson["onStageChange"].is_object()) {
            for (const auto& [stageStr, cmdArray] : questJson["onStageChange"].items()) {
                const auto stage = ParseStageNumber(stageStr);
                if (!stage.has_value()) {
                    MARAS_LOG_WARN("[QuestEventConfigLoader] Invalid stage number '{}' for quest '{}'",
                                   stageStr, formKey);
                    continue;
                }
                config.onStageChangeCommands[*stage] = ParseCommandArray(cmdArray);
            }
        }

        return config;
    }

    std::vector<QuestCommand> QuestEventConfigLoader::ParseCommandArray(const nlohmann::json& cmdArray) {
        std::vector<QuestCommand> commands;
        if (!cmdArray.is_array()) {
            return commands;
        }

        commands.reserve(cmdArray.size());
        for (const auto& entry : cmdArray) {
            if (auto cmd = ParseCommand(entry)) {
                commands.push_back(std::move(*cmd));
            }
        }
        return commands;
    }

    std::optional<QuestCommand> QuestEventConfigLoader::ParseCommand(const nlohmann::json& cmdJson) {
        QuestCommand cmd;

        if (cmdJson.is_string()) {
            // Legacy format: "commandType:npcSpecifier:argument"
            const std::string commandStr = cmdJson.get<std::string>();
            const auto firstColon = commandStr.find(':');
            if (firstColon == std::string::npos) {
                MARAS_LOG_WARN("[QuestEventConfigLoader] Command string missing first ':' separator: '{}'",
                               commandStr);
                return std::nullopt;
            }
            const auto secondColon = commandStr.find(':', firstColon + 1);
            if (secondColon == std::string::npos) {
                MARAS_LOG_WARN("[QuestEventConfigLoader] Command string missing second ':' separator: '{}'",
                               commandStr);
                return std::nullopt;
            }
            cmd.commandType = MARAS::Foundation::StringUtils::TrimStd(commandStr.substr(0, firstColon));
            cmd.npcSpecifier = MARAS::Foundation::StringUtils::TrimStd(
                commandStr.substr(firstColon + 1, secondColon - firstColon - 1));
            cmd.argument = MARAS::Foundation::StringUtils::TrimStd(commandStr.substr(secondColon + 1));
        } else if (cmdJson.is_object()) {
            // Legacy object format: {"type": "...", "npc": "...", "arg": ...}
            if (cmdJson.contains("type") && cmdJson["type"].is_string()) {
                cmd.commandType = MARAS::Foundation::StringUtils::TrimStd(cmdJson["type"].get<std::string>());
            } else if (cmdJson.contains("action") && cmdJson["action"].is_string()) {
                cmd.commandType = MARAS::Foundation::StringUtils::TrimStd(cmdJson["action"].get<std::string>());
            }

            if (cmdJson.contains("npc") && cmdJson["npc"].is_string()) {
                cmd.npcSpecifier = MARAS::Foundation::StringUtils::TrimStd(cmdJson["npc"].get<std::string>());
            }

            if (cmdJson.contains("arg")) {
                cmd.argument = MARAS::Foundation::StringUtils::TrimStd(JsonArgToString(cmdJson["arg"]));
            } else if (cmdJson.contains("args") && cmdJson["args"].is_array()) {
                const auto& args = cmdJson["args"];
                if (!args.empty() && cmd.npcSpecifier.empty()) {
                    cmd.npcSpecifier = MARAS::Foundation::StringUtils::TrimStd(JsonArgToString(args[0]));
                }
                if (args.size() > 1) {
                    cmd.argument = MARAS::Foundation::StringUtils::TrimStd(JsonArgToString(args[1]));
                }
            }
        } else {
            MARAS_LOG_WARN("[QuestEventConfigLoader] Command entry is not a string or object");
            return std::nullopt;
        }

        if (cmd.commandType.empty() || cmd.npcSpecifier.empty() || cmd.argument.empty()) {
            MARAS_LOG_WARN("[QuestEventConfigLoader] Skipping invalid command: type='{}', npc='{}', arg='{}'",
                           cmd.commandType, cmd.npcSpecifier, cmd.argument);
            return std::nullopt;
        }

        return cmd;
    }

}  // namespace MARAS::Engine
