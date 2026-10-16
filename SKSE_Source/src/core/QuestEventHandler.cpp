#include "core/QuestEventHandler.h"

#include "core/AffectionService.h"
#include "core/DeceasedRegistry.h"
#include "core/NPCRelationshipManager.h"
#include "core/NPCIndex.h"
#include "core/QuestEventConfigLoader.h"
#include "services/ActorService.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"
#include "utils/StringUtils.h"

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
        if (StringUtils::CaseInsensitiveEquals(command.commandType, "promoteToStatus")) {
            return ExecutePromoteToStatus(command.npcSpecifier, command.argument, quest, context);
        } else if (StringUtils::CaseInsensitiveEquals(command.commandType, "setAffection")) {
            return ExecuteSetAffection(command.npcSpecifier, command.argument, quest, context);
        } else if (StringUtils::CaseInsensitiveEquals(command.commandType, "changeAffection")) {
            return ExecuteChangeAffection(command.npcSpecifier, command.argument, quest, context);
        } else if (StringUtils::CaseInsensitiveEquals(command.commandType, "addDailyAffection")) {
            return ExecuteAddDailyAffection(command.npcSpecifier, command.argument, quest, context);
        }
        // NPC Attribute commands
        else if (StringUtils::CaseInsensitiveEquals(command.commandType, "setSocialClass")) {
            return ExecuteSetSocialClass(command.npcSpecifier, command.argument, quest, context);
        } else if (StringUtils::CaseInsensitiveEquals(command.commandType, "setSkillType")) {
            return ExecuteSetSkillType(command.npcSpecifier, command.argument, quest, context);
        } else if (StringUtils::CaseInsensitiveEquals(command.commandType, "setTemperament")) {
            return ExecuteSetTemperament(command.npcSpecifier, command.argument, quest, context);
        } else {
            MARAS_LOG_ERROR("Unknown command type: {}", command.commandType);
            return false;
        }
    }

    bool QuestEventManager::ExecutePromoteToStatus(const std::string& npcSpecifier, const std::string& statusKeyword,
                                                    RE::TESQuest* quest, const std::string& context) {
        const RE::FormID npcFormID = ResolveNPC(npcSpecifier, quest);
        if (npcFormID == 0) {
            MARAS_LOG_ERROR("promoteToStatus: Could not resolve NPC '{}' for quest 0x{:08X}", npcSpecifier,
                            quest->GetFormID());
            return false;
        }

        const std::string npcName = ActorSvc::GetName(npcFormID);
        auto& manager = NPCRelationshipManager::GetSingleton();

        // Handle "deceased" as a special case - archive in DeceasedRegistry and clean up active relationship state
        if (StringUtils::CaseInsensitiveEquals(statusKeyword, "deceased")) {
            if (auto* actor = NPCIndex::GetSingleton().GetActorFromFormId(npcFormID)) {
                DeceasedRegistry::GetSingleton().RecordDeceasedAdult(actor, false, nullptr);
            }
            bool success = manager.HandleNPCDeath(npcFormID);
            if (!success) {
                manager.EvaluateChildCustodyOnDeath(npcFormID);
            }
            MARAS_LOG_INFO("promoteToStatus: Processed deceased transition for NPC {} (0x{:08X}) via quest event ({})",
                           npcName, npcFormID, context);
            return true;
        }

        auto status = Utils::StringToRelationshipStatus(statusKeyword);

        bool success = false;
        switch (status) {
            case RelationshipStatus::Candidate:
                success = manager.RegisterAsCandidate(npcFormID);
                break;
            case RelationshipStatus::Dating:
                success = manager.PromoteToDating(npcFormID);
                break;
            case RelationshipStatus::Lover:
                success = manager.PromoteToLover(npcFormID);
                break;
            case RelationshipStatus::Engaged:
                success = manager.PromoteToEngaged(npcFormID);
                break;
            case RelationshipStatus::Married:
                success = manager.PromoteToMarried(npcFormID);
                break;
            case RelationshipStatus::Divorced:
                success = manager.PromoteToDivorced(npcFormID);
                break;
            case RelationshipStatus::Jilted:
                success = manager.PromoteToJilted(npcFormID);
                break;
            case RelationshipStatus::ExDating:
                success = manager.PromoteToExDating(npcFormID);
                break;
            case RelationshipStatus::ExLover:
                success = manager.PromoteToExLover(npcFormID);
                break;
            default:
                MARAS_LOG_WARN("promoteToStatus: Unknown status '{}' for NPC {} (0x{:08X})",
                               statusKeyword, npcName, npcFormID);
                return false;
        }

        if (success) {
            MARAS_LOG_INFO("promoteToStatus: Promoted {} (0x{:08X}) to status '{}' via quest event ({})",
                           npcName, npcFormID, statusKeyword, context);
        }

        return success;
    }

    bool QuestEventManager::ExecuteSetAffection(const std::string& npcSpecifier, const std::string& value,
                                                 RE::TESQuest* quest, const std::string& context) {
        const RE::FormID npcFormID = ResolveNPC(npcSpecifier, quest);
        if (npcFormID == 0) {
            MARAS_LOG_ERROR("setAffection: Could not resolve NPC '{}' for quest 0x{:08X}", npcSpecifier,
                            quest->GetFormID());
            return false;
        }

        try {
            int affectionValue = std::stoi(value);
            auto& affectionService = AffectionService::GetSingleton();
            affectionService.SetPermanentAffection(npcFormID, affectionValue);

            MARAS_LOG_INFO("setAffection: Set affection for {} (0x{:08X}) to {} via quest event ({})",
                           ActorSvc::GetName(npcFormID), npcFormID, affectionValue, context);
            return true;
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("setAffection: Invalid affection value '{}': {}", value, e.what());
            return false;
        }
    }

    bool QuestEventManager::ExecuteChangeAffection(const std::string& npcSpecifier, const std::string& delta,
                                                    RE::TESQuest* quest, const std::string& context) {
        const RE::FormID npcFormID = ResolveNPC(npcSpecifier, quest);
        if (npcFormID == 0) {
            MARAS_LOG_ERROR("changeAffection: Could not resolve NPC '{}' for quest 0x{:08X}", npcSpecifier,
                            quest->GetFormID());
            return false;
        }

        try {
            int deltaValue = std::stoi(delta);
            auto& affectionService = AffectionService::GetSingleton();

            int currentAffection = affectionService.GetPermanentAffection(npcFormID);
            int newAffection = currentAffection + deltaValue;

            affectionService.SetPermanentAffection(npcFormID, newAffection);

            MARAS_LOG_INFO("changeAffection: Changed affection for {} (0x{:08X}) by {} ({} -> {}) via quest event ({})",
                           ActorSvc::GetName(npcFormID), npcFormID, deltaValue, currentAffection, newAffection, context);
            return true;
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("changeAffection: Invalid delta value '{}': {}", delta, e.what());
            return false;
        }
    }

    bool QuestEventManager::ExecuteAddDailyAffection(const std::string& npcSpecifier, const std::string& amount,
                                                      RE::TESQuest* quest, const std::string& context) {
        const RE::FormID npcFormID = ResolveNPC(npcSpecifier, quest);
        if (npcFormID == 0) {
            MARAS_LOG_ERROR("addDailyAffection: Could not resolve NPC '{}' for quest 0x{:08X}", npcSpecifier,
                            quest->GetFormID());
            return false;
        }

        try {
            float affectionAmount = std::stof(amount);
            auto& affectionService = AffectionService::GetSingleton();

            // Add to daily affection with "quest" type
            affectionService.AddAffection(npcFormID, affectionAmount, "quest");

            MARAS_LOG_INFO("addDailyAffection: Added {} daily affection for {} (0x{:08X}) via quest event ({})",
                           affectionAmount, ActorSvc::GetName(npcFormID), npcFormID, context);
            return true;
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("addDailyAffection: Invalid amount value '{}': {}", amount, e.what());
            return false;
        }
    }

    bool QuestEventManager::ExecuteSetSocialClass(const std::string& npcSpecifier, const std::string& className,
                                                   RE::TESQuest* quest, const std::string& context) {
        const RE::FormID npcFormID = ResolveNPC(npcSpecifier, quest);
        if (npcFormID == 0) {
            MARAS_LOG_ERROR("setSocialClass: Could not resolve NPC '{}' for quest 0x{:08X}", npcSpecifier,
                            quest->GetFormID());
            return false;
        }

        const std::string npcName = ActorSvc::GetName(npcFormID);
        auto socialClass = Utils::StringToSocialClass(className);
        auto& manager = NPCRelationshipManager::GetSingleton();

        bool success = manager.SetSocialClass(npcFormID, static_cast<std::int8_t>(socialClass));

        if (success) {
            MARAS_LOG_INFO("setSocialClass: Set social class for {} (0x{:08X}) to '{}' via quest event ({})",
                           npcName, npcFormID, className, context);
        } else {
            MARAS_LOG_ERROR("setSocialClass: Failed to set social class for {} (0x{:08X}) to '{}'", npcName,
                            npcFormID, className);
        }

        return success;
    }

    bool QuestEventManager::ExecuteSetSkillType(const std::string& npcSpecifier, const std::string& skillName,
                                                 RE::TESQuest* quest, const std::string& context) {
        const RE::FormID npcFormID = ResolveNPC(npcSpecifier, quest);
        if (npcFormID == 0) {
            MARAS_LOG_ERROR("setSkillType: Could not resolve NPC '{}' for quest 0x{:08X}", npcSpecifier,
                            quest->GetFormID());
            return false;
        }

        const std::string npcName = ActorSvc::GetName(npcFormID);
        auto skillType = Utils::StringToSkillType(skillName);
        auto& manager = NPCRelationshipManager::GetSingleton();

        bool success = manager.SetSkillType(npcFormID, static_cast<std::int8_t>(skillType));

        if (success) {
            MARAS_LOG_INFO("setSkillType: Set skill type for {} (0x{:08X}) to '{}' via quest event ({})",
                           npcName, npcFormID, skillName, context);
        } else {
            MARAS_LOG_ERROR("setSkillType: Failed to set skill type for {} (0x{:08X}) to '{}'", npcName,
                            npcFormID, skillName);
        }

        return success;
    }

    bool QuestEventManager::ExecuteSetTemperament(const std::string& npcSpecifier, const std::string& temperamentName,
                                                   RE::TESQuest* quest, const std::string& context) {
        const RE::FormID npcFormID = ResolveNPC(npcSpecifier, quest);
        if (npcFormID == 0) {
            MARAS_LOG_ERROR("setTemperament: Could not resolve NPC '{}' for quest 0x{:08X}", npcSpecifier,
                            quest->GetFormID());
            return false;
        }

        const std::string npcName = ActorSvc::GetName(npcFormID);
        auto temperament = Utils::StringToTemperament(temperamentName);
        auto& manager = NPCRelationshipManager::GetSingleton();

        bool success = manager.SetTemperament(npcFormID, static_cast<std::int8_t>(temperament));

        if (success) {
            MARAS_LOG_INFO("setTemperament: Set temperament for {} (0x{:08X}) to '{}' via quest event ({})",
                           npcName, npcFormID, temperamentName, context);
        } else {
            MARAS_LOG_ERROR("setTemperament: Failed to set temperament for {} (0x{:08X}) to '{}'", npcName,
                            npcFormID, temperamentName);
        }

        return success;
    }

    RE::FormID QuestEventManager::ResolveNPC(const std::string& npcSpecifier, RE::TESQuest* quest) const {
        if (!quest) {
            return 0;
        }

        // Strategy 1: Check if it's a form key (starts with "__formData|")
        if (npcSpecifier.starts_with("__formData|")) {
            auto formID = Utils::ParseAndResolveFormKey(npcSpecifier);
            if (formID.has_value() && formID.value() != 0) {
                RE::FormID resolvedID = NPCIndex::GetSingleton().GetRefID(formID.value());
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

        // Strategy 3: Try resolving by name directly via NPCIndex
        if (auto* actor = NPCIndex::GetSingleton().FindActorByName(npcSpecifier)) {
            MARAS_LOG_DEBUG("Resolved NPC via name search '{}': {} (0x{:08X})", npcSpecifier,
                            actor->GetName(), actor->GetFormID());
            return actor->GetFormID();
        }
        if (auto entry = NPCIndex::GetSingleton().FindByName(npcSpecifier); entry && entry->refFormID != 0) {
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

        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(event->formID);
        if (!quest) {
            return RE::BSEventNotifyControl::kContinue;
        }

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

        auto& manager = QuestEventManager::GetSingleton();
        const auto* config = manager.GetConfigForQuest(event->formID);
        if (!config) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto stageIt = config->onStageChangeCommands.find(event->stage);
        if (stageIt == config->onStageChangeCommands.end() || stageIt->second.empty()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(event->formID);
        if (!quest) {
            return RE::BSEventNotifyControl::kContinue;
        }

        manager.ExecuteQuestStageCommands(quest, event->stage);

        return RE::BSEventNotifyControl::kContinue;
    }

}  // namespace MARAS
