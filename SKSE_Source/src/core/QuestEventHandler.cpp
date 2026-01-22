#include "core/QuestEventHandler.h"

#include "core/AffectionService.h"
#include "core/NPCRelationshipManager.h"
#include "core/QuestEventConfigLoader.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"

namespace MARAS {

    QuestEventManager& QuestEventManager::GetSingleton() {
        static QuestEventManager instance;
        return instance;
    }

    bool QuestEventManager::LoadConfigFromFolder(const std::string& folderPath) {
        return QuestEventConfigLoader::LoadFromFolder(folderPath, *this);
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

    void QuestEventManager::ExecuteQuestStageCommands(RE::TESQuest* quest, uint16_t stage) {
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

    void QuestEventManager::ExecuteCommands(const std::vector<QuestCommand>& commands, RE::TESQuest* quest,
                                            const std::string& context) {
        for (const auto& command : commands) {
            if (!ExecuteCommand(command, quest, context)) {
                MARAS_LOG_WARN("Failed to execute command '{}:{}:{}' in context '{}' for quest 0x{:08X}",
                               command.commandType, command.npcSpecifier, command.argument, context,
                               quest->GetFormID());
            }
        }
    }

    bool QuestEventManager::ExecuteCommand(const QuestCommand& command, RE::TESQuest* quest,
                                           const std::string& context) {
        MARAS_LOG_DEBUG("Executing command: {}:{}:{} (quest: 0x{:08X}, context: {})", command.commandType,
                        command.npcSpecifier, command.argument, quest->GetFormID(), context);

        // Relationship & Affection commands
        if (command.commandType == "promoteToStatus") {
            return ExecutePromoteToStatus(command.npcSpecifier, command.argument, quest, context);
        } else if (command.commandType == "setAffection") {
            return ExecuteSetAffection(command.npcSpecifier, command.argument, quest, context);
        } else if (command.commandType == "changeAffection") {
            return ExecuteChangeAffection(command.npcSpecifier, command.argument, quest, context);
        } else if (command.commandType == "addDailyAffection") {
            return ExecuteAddDailyAffection(command.npcSpecifier, command.argument, quest, context);
        }
        // NPC Attribute commands
        else if (command.commandType == "setSocialClass") {
            return ExecuteSetSocialClass(command.npcSpecifier, command.argument, quest, context);
        } else if (command.commandType == "setSkillType") {
            return ExecuteSetSkillType(command.npcSpecifier, command.argument, quest, context);
        } else if (command.commandType == "setTemperament") {
            return ExecuteSetTemperament(command.npcSpecifier, command.argument, quest, context);
        } else {
            MARAS_LOG_ERROR("Unknown command type: {}", command.commandType);
            return false;
        }
    }

    bool QuestEventManager::ExecutePromoteToStatus(const std::string& npcSpecifier, const std::string& statusKeyword,
                                                    RE::TESQuest* quest, const std::string& context) {
        auto npc = ResolveNPC(npcSpecifier, quest);
        if (!npc) {
            MARAS_LOG_ERROR("promoteToStatus: Could not resolve NPC '{}' for quest 0x{:08X}", npcSpecifier,
                            quest->GetFormID());
            return false;
        }

        auto& manager = NPCRelationshipManager::GetSingleton();

        // Handle "deceased" as a special case - unregister the NPC entirely
        if (Utils::ToLower(statusKeyword) == "deceased") {
            bool success = manager.UnregisterNPC(npc->GetFormID());
            if (success) {
                MARAS_LOG_INFO("promoteToStatus: Unregistered deceased NPC {} (0x{:08X}) via quest event ({})",
                               npc->GetName(), npc->GetFormID(), context);
            }
            return success;
        }

        auto status = Utils::StringToRelationshipStatus(statusKeyword);

        bool success = false;
        switch (status) {
            case RelationshipStatus::Candidate:
                success = manager.RegisterAsCandidate(npc->GetFormID());
                break;
            case RelationshipStatus::Engaged:
                success = manager.PromoteToEngaged(npc->GetFormID());
                break;
            case RelationshipStatus::Married:
                success = manager.PromoteToMarried(npc->GetFormID());
                break;
            case RelationshipStatus::Divorced:
                success = manager.PromoteToDivorced(npc->GetFormID());
                break;
            case RelationshipStatus::Jilted:
                success = manager.PromoteToJilted(npc->GetFormID());
                break;
            default:
                MARAS_LOG_WARN("promoteToStatus: Unknown status '{}' for NPC {} (0x{:08X})",
                               statusKeyword, npc->GetName(), npc->GetFormID());
                return false;
        }

        if (success) {
            MARAS_LOG_INFO("promoteToStatus: Promoted {} (0x{:08X}) to status '{}' via quest event ({})",
                           npc->GetName(), npc->GetFormID(), statusKeyword, context);
        }

        return success;
    }

    bool QuestEventManager::ExecuteSetAffection(const std::string& npcSpecifier, const std::string& value,
                                                 RE::TESQuest* quest, const std::string& context) {
        auto npc = ResolveNPC(npcSpecifier, quest);
        if (!npc) {
            MARAS_LOG_ERROR("setAffection: Could not resolve NPC '{}' for quest 0x{:08X}", npcSpecifier,
                            quest->GetFormID());
            return false;
        }

        try {
            int affectionValue = std::stoi(value);
            auto& affectionService = AffectionService::GetSingleton();
            affectionService.SetPermanentAffection(npc->GetFormID(), affectionValue);

            MARAS_LOG_INFO("setAffection: Set affection for {} (0x{:08X}) to {} via quest event ({})", npc->GetName(),
                           npc->GetFormID(), affectionValue, context);
            return true;
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("setAffection: Invalid affection value '{}': {}", value, e.what());
            return false;
        }
    }

    bool QuestEventManager::ExecuteChangeAffection(const std::string& npcSpecifier, const std::string& delta,
                                                    RE::TESQuest* quest, const std::string& context) {
        auto npc = ResolveNPC(npcSpecifier, quest);
        if (!npc) {
            MARAS_LOG_ERROR("changeAffection: Could not resolve NPC '{}' for quest 0x{:08X}", npcSpecifier,
                            quest->GetFormID());
            return false;
        }

        try {
            int deltaValue = std::stoi(delta);
            auto& affectionService = AffectionService::GetSingleton();

            int currentAffection = affectionService.GetPermanentAffection(npc->GetFormID());
            int newAffection = currentAffection + deltaValue;

            affectionService.SetPermanentAffection(npc->GetFormID(), newAffection);

            MARAS_LOG_INFO("changeAffection: Changed affection for {} (0x{:08X}) by {} ({} -> {}) via quest event ({})",
                           npc->GetName(), npc->GetFormID(), deltaValue, currentAffection, newAffection, context);
            return true;
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("changeAffection: Invalid delta value '{}': {}", delta, e.what());
            return false;
        }
    }

    bool QuestEventManager::ExecuteAddDailyAffection(const std::string& npcSpecifier, const std::string& amount,
                                                      RE::TESQuest* quest, const std::string& context) {
        auto npc = ResolveNPC(npcSpecifier, quest);
        if (!npc) {
            MARAS_LOG_ERROR("addDailyAffection: Could not resolve NPC '{}' for quest 0x{:08X}", npcSpecifier,
                            quest->GetFormID());
            return false;
        }

        try {
            float affectionAmount = std::stof(amount);
            auto& affectionService = AffectionService::GetSingleton();

            // Add to daily affection with "quest" type
            affectionService.AddAffection(npc->GetFormID(), affectionAmount, "quest");

            MARAS_LOG_INFO("addDailyAffection: Added {} daily affection for {} (0x{:08X}) via quest event ({})",
                           affectionAmount, npc->GetName(), npc->GetFormID(), context);
            return true;
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("addDailyAffection: Invalid amount value '{}': {}", amount, e.what());
            return false;
        }
    }

    bool QuestEventManager::ExecuteSetSocialClass(const std::string& npcSpecifier, const std::string& className,
                                                   RE::TESQuest* quest, const std::string& context) {
        auto npc = ResolveNPC(npcSpecifier, quest);
        if (!npc) {
            MARAS_LOG_ERROR("setSocialClass: Could not resolve NPC '{}' for quest 0x{:08X}", npcSpecifier,
                            quest->GetFormID());
            return false;
        }

        auto socialClass = Utils::StringToSocialClass(className);
        auto& manager = NPCRelationshipManager::GetSingleton();

        bool success = manager.SetSocialClass(npc->GetFormID(), static_cast<std::int8_t>(socialClass));

        if (success) {
            MARAS_LOG_INFO("setSocialClass: Set social class for {} (0x{:08X}) to '{}' via quest event ({})",
                           npc->GetName(), npc->GetFormID(), className, context);
        } else {
            MARAS_LOG_ERROR("setSocialClass: Failed to set social class for {} (0x{:08X}) to '{}'", npc->GetName(),
                            npc->GetFormID(), className);
        }

        return success;
    }

    bool QuestEventManager::ExecuteSetSkillType(const std::string& npcSpecifier, const std::string& skillName,
                                                 RE::TESQuest* quest, const std::string& context) {
        auto npc = ResolveNPC(npcSpecifier, quest);
        if (!npc) {
            MARAS_LOG_ERROR("setSkillType: Could not resolve NPC '{}' for quest 0x{:08X}", npcSpecifier,
                            quest->GetFormID());
            return false;
        }

        auto skillType = Utils::StringToSkillType(skillName);
        auto& manager = NPCRelationshipManager::GetSingleton();

        bool success = manager.SetSkillType(npc->GetFormID(), static_cast<std::int8_t>(skillType));

        if (success) {
            MARAS_LOG_INFO("setSkillType: Set skill type for {} (0x{:08X}) to '{}' via quest event ({})",
                           npc->GetName(), npc->GetFormID(), skillName, context);
        } else {
            MARAS_LOG_ERROR("setSkillType: Failed to set skill type for {} (0x{:08X}) to '{}'", npc->GetName(),
                            npc->GetFormID(), skillName);
        }

        return success;
    }

    bool QuestEventManager::ExecuteSetTemperament(const std::string& npcSpecifier, const std::string& temperamentName,
                                                   RE::TESQuest* quest, const std::string& context) {
        auto npc = ResolveNPC(npcSpecifier, quest);
        if (!npc) {
            MARAS_LOG_ERROR("setTemperament: Could not resolve NPC '{}' for quest 0x{:08X}", npcSpecifier,
                            quest->GetFormID());
            return false;
        }

        auto temperament = Utils::StringToTemperament(temperamentName);
        auto& manager = NPCRelationshipManager::GetSingleton();

        bool success = manager.SetTemperament(npc->GetFormID(), static_cast<std::int8_t>(temperament));

        if (success) {
            MARAS_LOG_INFO("setTemperament: Set temperament for {} (0x{:08X}) to '{}' via quest event ({})",
                           npc->GetName(), npc->GetFormID(), temperamentName, context);
        } else {
            MARAS_LOG_ERROR("setTemperament: Failed to set temperament for {} (0x{:08X}) to '{}'", npc->GetName(),
                            npc->GetFormID(), temperamentName);
        }

        return success;
    }

    RE::Actor* QuestEventManager::ResolveNPC(const std::string& npcSpecifier, RE::TESQuest* quest) const {
        if (!quest) {
            return nullptr;
        }

        // Strategy 1: Check if it's a form key (starts with "__formData|")
        if (npcSpecifier.starts_with("__formData|")) {
            auto formID = Utils::ParseAndResolveFormKey(npcSpecifier);
            if (formID.has_value()) {
                auto* form = RE::TESForm::LookupByID(formID.value());
                if (form) {
                    // Try as direct Actor reference first
                    auto* actor = form->As<RE::Actor>();
                    if (actor) {
                        MARAS_LOG_DEBUG("Resolved NPC via form key (reference) '{}': {} (0x{:08X})", npcSpecifier,
                                        actor->GetName(), actor->GetFormID());
                        return actor;
                    }

                    // Try as base actor (ACHR base) and find a reference in the world
                    auto* actorBase = form->As<RE::TESNPC>();
                    if (actorBase) {
                        MARAS_LOG_DEBUG("Form key '{}' is a base actor, searching for reference...", npcSpecifier);

                        // Search through all actor process lists to find one with this base
                        auto* processLists = RE::ProcessLists::GetSingleton();
                        if (processLists) {
                            // Helper lambda to search through an actor handle array
                            auto searchHandles = [&](const RE::BSTArray<RE::ActorHandle>& handles) -> RE::Actor* {
                                for (auto& actorHandle : handles) {
                                    auto foundActorPtr = actorHandle.get();
                                    auto* foundActor = foundActorPtr.get();
                                    if (foundActor && foundActor->GetActorBase() == actorBase) {
                                        return foundActor;
                                    }
                                }
                                return nullptr;
                            };

                            // Search all four actor lists (high priority first for performance)
                            RE::Actor* foundActor = nullptr;
                            if (!foundActor)
                                foundActor = searchHandles(processLists->highActorHandles);
                            if (!foundActor)
                                foundActor = searchHandles(processLists->middleHighActorHandles);
                            if (!foundActor)
                                foundActor = searchHandles(processLists->middleLowActorHandles);
                            if (!foundActor)
                                foundActor = searchHandles(processLists->lowActorHandles);

                            if (foundActor) {
                                MARAS_LOG_DEBUG("Found reference for base actor '{}': {} (0x{:08X})", npcSpecifier,
                                                foundActor->GetName(), foundActor->GetFormID());
                                return foundActor;
                            }
                        }

                        MARAS_LOG_ERROR("Form key '{}' is a base actor (0x{:08X}) but no active reference found in world",
                                        npcSpecifier, formID.value());
                    } else {
                        MARAS_LOG_ERROR("Form key '{}' resolved to 0x{:08X} but is neither Actor nor TESNPC",
                                        npcSpecifier, formID.value());
                    }
                } else {
                    MARAS_LOG_ERROR("Form key '{}' resolved to 0x{:08X} but form not found", npcSpecifier,
                                    formID.value());
                }
            } else {
                MARAS_LOG_ERROR("Failed to parse form key: {}", npcSpecifier);
            }
            return nullptr;
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
                            return actor;
                        } else {
                            MARAS_LOG_ERROR("Alias '{}' reference is not an Actor", npcSpecifier);
                        }
                    } else {
                        MARAS_LOG_ERROR("Alias '{}' has no reference", npcSpecifier);
                    }
                } else {
                    MARAS_LOG_ERROR("Alias '{}' is not a reference alias", npcSpecifier);
                }
                return nullptr;
            }
        }

        MARAS_LOG_ERROR("Could not find alias named '{}' in quest {} (0x{:08X})", npcSpecifier, quest->GetName(),
                        quest->GetFormID());
        return nullptr;
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

        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(event->formID);
        if (!quest) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto& manager = QuestEventManager::GetSingleton();

        if (event->started) {
            manager.ExecuteQuestStartCommands(quest);
        } else {
            manager.ExecuteQuestStopCommands(quest);
        }

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

        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(event->formID);
        if (!quest) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto& manager = QuestEventManager::GetSingleton();
        manager.ExecuteQuestStageCommands(quest, event->stage);

        return RE::BSEventNotifyControl::kContinue;
    }

}  // namespace MARAS
