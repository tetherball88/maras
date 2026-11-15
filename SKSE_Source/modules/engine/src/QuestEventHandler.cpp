#include "maras/engine/QuestEventHandler.h"

#include "maras/engine/ActorService.h"
#include "maras/engine/EngineEventHooks.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FormUtils.h"
#include "maras/engine/QuestEventConfigLoader.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/StringUtils.h"

#include <filesystem>
#include <mutex>

namespace MARAS {

    namespace {

        [[nodiscard]] QuestCommand ConvertCommand(const Engine::QuestCommand& src) {
            return QuestCommand{ src.commandType, src.npcSpecifier, src.argument };
        }

        [[nodiscard]] std::vector<QuestCommand> ConvertCommandList(const std::vector<Engine::QuestCommand>& src) {
            std::vector<QuestCommand> out;
            out.reserve(src.size());
            for (const auto& cmd : src) {
                out.push_back(ConvertCommand(cmd));
            }
            return out;
        }

        [[nodiscard]] bool IsKnownQuestCommandType(std::string_view commandType) {
            return Foundation::StringUtils::CaseInsensitiveEquals(commandType, "promoteToStatus") ||
                   Foundation::StringUtils::CaseInsensitiveEquals(commandType, "setAffection") ||
                   Foundation::StringUtils::CaseInsensitiveEquals(commandType, "changeAffection") ||
                   Foundation::StringUtils::CaseInsensitiveEquals(commandType, "addDailyAffection") ||
                   Foundation::StringUtils::CaseInsensitiveEquals(commandType, "setSocialClass") ||
                   Foundation::StringUtils::CaseInsensitiveEquals(commandType, "setSkillType") ||
                   Foundation::StringUtils::CaseInsensitiveEquals(commandType, "setTemperament") ||
                   Foundation::StringUtils::CaseInsensitiveEquals(commandType, "onDeath");
        }

    }  // namespace

    QuestEventManager& QuestEventManager::GetSingleton() {
        static QuestEventManager instance;
        return instance;
    }

    bool QuestEventManager::LoadConfigFromFolder(const std::string& folderPath) {
        Engine::QuestConfigMap loadedConfigs;
        const bool ok = Engine::QuestEventConfigLoader::LoadFromFolder(
            std::filesystem::path(folderPath), loadedConfigs);

        questConfigs_.clear();
        questConfigs_.reserve(loadedConfigs.size());
        for (const auto& [questID, engineCfg] : loadedConfigs) {
            QuestEventConfig cfg{};
            cfg.questFormID = engineCfg.questFormID != 0 ? engineCfg.questFormID : questID;
            cfg.onStartCommands = ConvertCommandList(engineCfg.onStartCommands);
            cfg.onStopCommands = ConvertCommandList(engineCfg.onStopCommands);
            for (const auto& [stage, stageCmds] : engineCfg.onStageChangeCommands) {
                cfg.onStageChangeCommands.emplace(stage, ConvertCommandList(stageCmds));
            }
            questConfigs_.emplace(questID, std::move(cfg));
        }

        return ok;
    }

    void QuestEventManager::ClearConfig() {
        questConfigs_.clear();
        MARAS_LOG_INFO("Cleared all quest event configurations");
    }

    const QuestEventConfig* QuestEventManager::GetConfigForQuest(RE::FormID questFormID) const {
        auto it = questConfigs_.find(questFormID);
        if (it != questConfigs_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    void QuestEventManager::ExecuteQuestStartCommands(RE::TESQuest* quest) {
        if (!quest) {
            return;
        }

        auto it = questConfigs_.find(quest->GetFormID());
        if (it != questConfigs_.end() && !it->second.onStartCommands.empty()) {
            MARAS_LOG_INFO("Quest started: {} (0x{:08X}), executing {} commands", quest->GetName(),
                           quest->GetFormID(), it->second.onStartCommands.size());
            ExecuteCommands(it->second.onStartCommands, quest, "onStart");
        }
    }

    void QuestEventManager::ExecuteQuestStopCommands(RE::TESQuest* quest) {
        if (!quest) {
            return;
        }

        auto it = questConfigs_.find(quest->GetFormID());
        if (it != questConfigs_.end() && !it->second.onStopCommands.empty()) {
            MARAS_LOG_INFO("Quest stopped: {} (0x{:08X}), executing {} commands", quest->GetName(),
                           quest->GetFormID(), it->second.onStopCommands.size());
            ExecuteCommands(it->second.onStopCommands, quest, "onStop");
        }
    }

    void QuestEventManager::ExecuteQuestStageCommands(RE::TESQuest* quest, std::uint16_t stage) {
        if (!quest) {
            return;
        }

        auto it = questConfigs_.find(quest->GetFormID());
        if (it != questConfigs_.end()) {
            auto stageIt = it->second.onStageChangeCommands.find(stage);
            if (stageIt != it->second.onStageChangeCommands.end() && !stageIt->second.empty()) {
                MARAS_LOG_INFO("Quest stage changed: {} (0x{:08X}) stage {}, executing {} commands",
                               quest->GetName(), quest->GetFormID(), stage, stageIt->second.size());
                ExecuteCommands(stageIt->second, quest, fmt::format("onStageChange:{}", stage));
            }
        }
    }

    void QuestEventManager::ExecuteCommands(const std::vector<QuestCommand>& commands,
                                            RE::TESQuest* quest,
                                            const std::string& context) {
        for (const auto& command : commands) {
            if (!ExecuteCommand(command, quest, context)) {
                MARAS_LOG_WARN("Failed to execute command '{}:{}:{}' in context '{}' for quest 0x{:08X}",
                               command.commandType, command.npcSpecifier, command.argument, context,
                               quest->GetFormID());
            }
        }
    }

    bool QuestEventManager::ExecuteCommand(const QuestCommand& command,
                                           RE::TESQuest* quest,
                                           const std::string& context) {
        if (!quest) {
            return false;
        }

        MARAS_LOG_DEBUG("Executing command: {}:{}:{} (quest: 0x{:08X}, context: {})", command.commandType,
                        command.npcSpecifier, command.argument, quest->GetFormID(), context);

        const RE::FormID npcFormID = ResolveNPC(command.npcSpecifier, quest);
        if (npcFormID == 0) {
            MARAS_LOG_ERROR("{}: Could not resolve NPC '{}' for quest 0x{:08X}", command.commandType,
                            command.npcSpecifier, quest->GetFormID());
            return false;
        }

        auto* hooks = Engine::GetEngineEventHooks();
        if (!hooks) {
            MARAS_LOG_WARN("{}: EngineEventHooks unavailable for NPC {:08X} in quest 0x{:08X}",
                           command.commandType, npcFormID, quest->GetFormID());
            return false;
        }

        if (Foundation::StringUtils::CaseInsensitiveEquals(command.commandType, "onDeath")) {
            hooks->OnActorDeath(npcFormID, RE::FormID{ 0 }, false);
            return true;
        }

        if (hooks->OnQuestCommand(command.commandType, npcFormID, command.argument, context)) {
            return true;
        }

        if (Foundation::StringUtils::CaseInsensitiveEquals(command.commandType, "promoteToStatus") &&
            Foundation::StringUtils::CaseInsensitiveEquals(command.argument, "deceased")) {
            hooks->OnActorDeath(npcFormID, RE::FormID{ 0 }, false);
            return true;
        }

        if (!IsKnownQuestCommandType(command.commandType)) {
            MARAS_LOG_ERROR("Unknown command type: {}", command.commandType);
        }
        return false;
    }

    RE::FormID QuestEventManager::ResolveNPC(const std::string& npcSpecifier, RE::TESQuest* quest) const {
        if (!quest) {
            return 0;
        }

        // Strategy 1: Check if it's a form key (starts with "__formData|")
        if (npcSpecifier.starts_with("__formData|")) {
            auto formID = Utils::ParseAndResolveFormKey(npcSpecifier);
            if (formID.has_value() && formID.value() != 0) {
                RE::FormID resolvedID = SpatialWorldIndex::GetSingleton().GetRefID(formID.value());
                if (resolvedID == 0) {
                    resolvedID = formID.value();
                }
                MARAS_LOG_DEBUG("Resolved NPC via form key '{}': {} (0x{:08X})", npcSpecifier,
                                ActorSvc::GetName(resolvedID), resolvedID);
                return resolvedID;
            }
            MARAS_LOG_ERROR("Failed to parse or resolve form key: {}", npcSpecifier);
            return 0;
        }

        // Strategy 2: Treat it as an alias name and search quest aliases
        for (auto* currentAlias : quest->aliases) {
            if (currentAlias && currentAlias->aliasName == npcSpecifier.c_str()) {
                auto* refAlias = skyrim_cast<RE::BGSRefAlias*>(currentAlias);
                if (refAlias) {
                    auto* ref = refAlias->GetReference();
                    if (ref) {
                        auto* actor = ref->As<RE::Actor>();
                        if (actor) {
                            MARAS_LOG_DEBUG("Resolved NPC via alias '{}': {} (0x{:08X})", npcSpecifier,
                                            actor->GetName(), actor->GetFormID());
                            return actor->GetFormID();
                        } else {
                            MARAS_LOG_ERROR("Alias '{}' reference is not an Actor", npcSpecifier);
                        }
                    } else {
                        MARAS_LOG_ERROR("Alias '{}' has no reference", npcSpecifier);
                    }
                } else {
                    MARAS_LOG_ERROR("Alias '{}' is not a reference alias", npcSpecifier);
                }
                return 0;
            }
        }

        // Strategy 3: Try resolving by name directly via SpatialWorldIndex
        if (auto* actor = SpatialWorldIndex::GetSingleton().FindActorByName(npcSpecifier)) {
            MARAS_LOG_DEBUG("Resolved NPC via name search '{}': {} (0x{:08X})", npcSpecifier,
                            actor->GetName(), actor->GetFormID());
            return actor->GetFormID();
        }
        if (auto entry = SpatialWorldIndex::GetSingleton().FindByName(npcSpecifier); entry && entry->refFormID != 0) {
            MARAS_LOG_DEBUG("Resolved NPC via index name search '{}': {} (0x{:08X})", npcSpecifier,
                            entry->nameDisplay, entry->refFormID);
            return entry->refFormID;
        }

        MARAS_LOG_ERROR("Could not resolve NPC '{}' in quest {} (0x{:08X}) via form key, alias, or name",
                        npcSpecifier, quest->GetName(), quest->GetFormID());
        return 0;
    }

    // ========================================
    // Quest Start/Stop Event Sink
    // ========================================

    QuestStartStopEventSink* QuestStartStopEventSink::GetSingleton() {
        static QuestStartStopEventSink singleton;
        return &singleton;
    }

    RE::BSEventNotifyControl QuestStartStopEventSink::ProcessEvent(const RE::TESQuestStartStopEvent* event,
                                                                   RE::BSTEventSource<RE::TESQuestStartStopEvent>*) {
        if (!event || !event->formID) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto& manager = QuestEventManager::GetSingleton();
        const auto* config = manager.GetConfigForQuest(event->formID);
        if (!config) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (event->started ? config->onStartCommands.empty() : config->onStopCommands.empty()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const RE::FormID questFormID = event->formID;
        const bool started = event->started;
        const SessionEpoch expectedEpoch = GetSessionEpoch();
        AddEpochTask(expectedEpoch, "QuestStartStopEventSink::ProcessEvent", [questFormID, started, expectedEpoch]() {
            std::lock_guard papyrusLock(GetPapyrusMutex());
            if (!IsSessionEpochCurrent(expectedEpoch)) {
                return;
            }
            auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(questFormID);
            if (!quest || quest->IsDeleted()) {
                return;
            }
            auto& mgr = QuestEventManager::GetSingleton();
            if (started) {
                mgr.ExecuteQuestStartCommands(quest);
            } else {
                mgr.ExecuteQuestStopCommands(quest);
            }
        });

        return RE::BSEventNotifyControl::kContinue;
    }

    // ========================================
    // Quest Stage Event Sink
    // ========================================

    QuestStageEventSink* QuestStageEventSink::GetSingleton() {
        static QuestStageEventSink singleton;
        return &singleton;
    }

    RE::BSEventNotifyControl QuestStageEventSink::ProcessEvent(const RE::TESQuestStageEvent* event,
                                                               RE::BSTEventSource<RE::TESQuestStageEvent>*) {
        if (!event || !event->formID) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto& manager = QuestEventManager::GetSingleton();
        const auto* config = manager.GetConfigForQuest(event->formID);
        if (!config) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto stageIt = config->onStageChangeCommands.find(event->stage);
        if (stageIt == config->onStageChangeCommands.end() || stageIt->second.empty()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const RE::FormID questFormID = event->formID;
        const std::uint16_t stage = event->stage;
        const SessionEpoch expectedEpoch = GetSessionEpoch();
        AddEpochTask(expectedEpoch, "QuestStageEventSink::ProcessEvent", [questFormID, stage, expectedEpoch]() {
            std::lock_guard papyrusLock(GetPapyrusMutex());
            if (!IsSessionEpochCurrent(expectedEpoch)) {
                return;
            }
            auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(questFormID);
            if (!quest || quest->IsDeleted()) {
                return;
            }
            QuestEventManager::GetSingleton().ExecuteQuestStageCommands(quest, stage);
        });

        return RE::BSEventNotifyControl::kContinue;
    }

}  // namespace MARAS
