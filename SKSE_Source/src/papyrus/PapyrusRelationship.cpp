#include "papyrus/PapyrusInterface.h"

#include "core/AffectionService.h"
#include "core/BonusesService.h"
#include "core/DeceasedRegistry.h"
#include "core/MarriageDifficulty.h"
#include "core/MatchmakingService.h"
#include "core/NPCIndex.h"
#include "core/PollingService.h"
#include "core/SkyrimNetIntegration.h"
#include "core/SpouseBuffService.h"
#include "core/SpouseHierarchyManager.h"
#include "services/ActorService.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"
#include "utils/StringUtils.h"

namespace MARAS::PapyrusInterface {

    namespace {
        std::vector<RE::Actor*> ConvertFormIDsToActors(const std::vector<RE::FormID>& formIDs) {
            std::vector<RE::Actor*> actors;
            actors.reserve(formIDs.size());

            for (auto formID : formIDs) {
                if (auto form = RE::TESForm::LookupByID(formID)) {
                    if (auto actor = form->As<RE::Actor>()) {
                        actors.push_back(actor);
                    }
                }
            }

            return actors;
        }
    }

    // ========================================
    // Registration functions
    // ========================================

    bool RegisterCandidate(RE::StaticFunctionTag*, RE::Actor* npc) {
        auto startTime = std::chrono::high_resolution_clock::now();

        if (!npc) {
            MARAS_LOG_WARN("RegisterCandidate: null actor provided");
            return false;
        }

        auto formID = npc->GetFormID();
        if (!ActorSvc::GetActorBase(formID)) {
            MARAS_LOG_WARN("RegisterCandidate: actor has no base data (possible invalid actor) for form {:08X}", formID);
            return false;
        }

        if (npc->IsPlayerRef()) {
            MARAS_LOG_WARN("RegisterCandidate: cannot register player (form {:08X}) as candidate", formID);
            return false;
        }

        std::lock_guard lock(GetPapyrusMutex());
        auto& manager = NPCRelationshipManager::GetSingleton();
        bool result = false;
        try {
            result = manager.RegisterAsCandidate(formID);
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("RegisterCandidate: exception when registering {:08X}: {}", formID, e.what());
            result = false;
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        MARAS_LOG_INFO("Papyrus RegisterCandidate for {} completed in {} microseconds (success: {})",
                       ActorSvc::GetName(formID), duration.count(), result);

        return result;
    }

    bool UnregisterNPC(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("UnregisterNPC: null actor provided");
            return false;
        }

        auto formID = npc->GetFormID();
        if (!ActorSvc::GetActorBase(formID)) {
            MARAS_LOG_WARN("UnregisterNPC: actor has no base data (possible invalid actor) for form {:08X}", formID);
            return false;
        }

        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().UnregisterNPC(formID);
    }

    // ========================================
    // Consolidated faction & archetype management
    // ========================================

    bool SetNpcCharacteristics(RE::StaticFunctionTag*, RE::Actor* npc, std::string factionType, std::int32_t rank) {
        if (!npc) {
            MARAS_LOG_WARN("SetNpcCharacteristics: null actor provided");
            return false;
        }

        std::lock_guard lock(GetPapyrusMutex());
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto lower = StringUtils::ToLowerStd(factionType);

        if (lower == "socialclass") {
            if (rank < 0 || rank >= static_cast<std::int32_t>(SocialClass::_Count)) {
                MARAS_LOG_WARN("SetNpcCharacteristics: out-of-range SocialClass rank {}", rank);
                return false;
            }
            return manager.SetSocialClass(npc->GetFormID(), static_cast<std::int8_t>(rank));
        } else if (lower == "skilltype") {
            if (rank < 0 || rank >= static_cast<std::int32_t>(SkillType::_Count)) {
                MARAS_LOG_WARN("SetNpcCharacteristics: out-of-range SkillType rank {}", rank);
                return false;
            }
            return manager.SetSkillType(npc->GetFormID(), static_cast<std::int8_t>(rank));
        } else if (lower == "temperament") {
            if (rank < 0 || rank >= static_cast<std::int32_t>(Temperament::_Count)) {
                MARAS_LOG_WARN("SetNpcCharacteristics: out-of-range Temperament rank {}", rank);
                return false;
            }
            return manager.SetTemperament(npc->GetFormID(), static_cast<std::int8_t>(rank));
        } else {
            MARAS_LOG_WARN("SetNpcCharacteristics (attribute change): invalid type '{}'", factionType);
            return false;
        }
    }

    std::vector<std::string> GetNpcTypes(RE::StaticFunctionTag*, std::string type) {
        auto lower = StringUtils::ToLowerStd(type);

        if (lower == "socialclass") {
            return {StringUtils::ToLowerStd(Utils::SocialClassToString(SocialClass::Outcast)),
                    StringUtils::ToLowerStd(Utils::SocialClassToString(SocialClass::Poverty)),
                    StringUtils::ToLowerStd(Utils::SocialClassToString(SocialClass::Working)),
                    StringUtils::ToLowerStd(Utils::SocialClassToString(SocialClass::Middle)),
                    StringUtils::ToLowerStd(Utils::SocialClassToString(SocialClass::Wealthy)),
                    StringUtils::ToLowerStd(Utils::SocialClassToString(SocialClass::Religious)),
                    StringUtils::ToLowerStd(Utils::SocialClassToString(SocialClass::Nobles)),
                    StringUtils::ToLowerStd(Utils::SocialClassToString(SocialClass::Rulers))};
        } else if (lower == "skilltype") {
            return {StringUtils::ToLowerStd(Utils::SkillTypeToString(SkillType::Warrior)),
                    StringUtils::ToLowerStd(Utils::SkillTypeToString(SkillType::Mage)),
                    StringUtils::ToLowerStd(Utils::SkillTypeToString(SkillType::Rogue)),
                    StringUtils::ToLowerStd(Utils::SkillTypeToString(SkillType::Craftsman)),
                    StringUtils::ToLowerStd(Utils::SkillTypeToString(SkillType::Ranger)),
                    StringUtils::ToLowerStd(Utils::SkillTypeToString(SkillType::Orator))};
        } else if (lower == "temperament") {
            return {StringUtils::ToLowerStd(Utils::TemperamentToString(Temperament::Proud)),
                    StringUtils::ToLowerStd(Utils::TemperamentToString(Temperament::Humble)),
                    StringUtils::ToLowerStd(Utils::TemperamentToString(Temperament::Jealous)),
                    StringUtils::ToLowerStd(Utils::TemperamentToString(Temperament::Romantic)),
                    StringUtils::ToLowerStd(Utils::TemperamentToString(Temperament::Independent))};
        }

        MARAS_LOG_WARN("GetNpcTypes: invalid type '{}'", type);
        return {};
    }

    std::int32_t GetNpcTypeEnum(RE::StaticFunctionTag*, std::string type, std::string name) {
        auto lower = StringUtils::ToLowerStd(type);

        if (lower == "socialclass") {
            return static_cast<std::int32_t>(Utils::StringToSocialClass(name));
        } else if (lower == "skilltype") {
            return static_cast<std::int32_t>(Utils::StringToSkillType(name));
        } else if (lower == "temperament") {
            return static_cast<std::int32_t>(Utils::StringToTemperament(name));
        }

        MARAS_LOG_WARN("GetNpcTypeEnum: invalid type '{}'", type);
        return -1;
    }

    std::int32_t GetNpcCurrentTypeEnum(RE::StaticFunctionTag*, RE::Actor* npc, std::string type) {
        if (!npc) {
            MARAS_LOG_WARN("GetNpcCurrentTypeEnum: null actor provided");
            return -1;
        }

        std::lock_guard lock(GetPapyrusMutex());
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto lower = StringUtils::ToLowerStd(type);

        if (lower == "socialclass") {
            return static_cast<std::int32_t>(manager.GetSocialClass(npc->GetFormID()));
        } else if (lower == "skilltype") {
            return static_cast<std::int32_t>(manager.GetSkillType(npc->GetFormID()));
        } else if (lower == "temperament") {
            return static_cast<std::int32_t>(manager.GetTemperament(npc->GetFormID()));
        }

        MARAS_LOG_WARN("GetNpcCurrentTypeEnum: invalid type '{}'", type);
        return -1;
    }

    std::string GetNpcCurrentTypeName(RE::StaticFunctionTag*, RE::Actor* npc, std::string type) {
        if (!npc) {
            MARAS_LOG_WARN("GetNpcCurrentTypeName: null actor provided");
            return "Unknown";
        }

        std::lock_guard lock(GetPapyrusMutex());
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto lower = StringUtils::ToLowerStd(type);

        if (lower == "socialclass") {
            return std::string(Utils::SocialClassToString(manager.GetSocialClass(npc->GetFormID())));
        } else if (lower == "skilltype") {
            return std::string(Utils::SkillTypeToString(manager.GetSkillType(npc->GetFormID())));
        } else if (lower == "temperament") {
            return std::string(Utils::TemperamentToString(manager.GetTemperament(npc->GetFormID())));
        }

        MARAS_LOG_WARN("GetNpcCurrentTypeName: invalid type '{}'", type);
        return "Unknown";
    }

    std::int32_t DetermineSocialClass(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("DetermineSocialClass: null actor provided");
            return static_cast<std::int32_t>(SocialClass::Outcast);
        }
        std::lock_guard lock(GetPapyrusMutex());
        return static_cast<std::int32_t>(NPCRelationshipManager::GetSingleton().DetermineSocialClass(npc->GetFormID()));
    }

    std::int32_t DetermineSkillType(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("DetermineSkillType: null actor provided");
            return static_cast<std::int32_t>(SkillType::Warrior);
        }
        std::lock_guard lock(GetPapyrusMutex());
        return static_cast<std::int32_t>(NPCRelationshipManager::GetSingleton().DetermineSkillType(npc->GetFormID()));
    }

    std::int32_t DetermineTemperament(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("DetermineTemperament: null actor provided");
            return static_cast<std::int32_t>(Temperament::Proud);
        }
        std::lock_guard lock(GetPapyrusMutex());
        return static_cast<std::int32_t>(NPCRelationshipManager::GetSingleton().DetermineTemperament(npc->GetFormID()));
    }

    // ========================================
    // Consolidated status checks & transitions
    // ========================================

    bool IsNPCStatus(RE::StaticFunctionTag*, RE::Actor* npc, std::string statusType) {
        if (!npc) {
            MARAS_LOG_WARN("IsNPCStatus: null actor provided");
            return false;
        }

        std::lock_guard lock(GetPapyrusMutex());
        auto lower = StringUtils::ToLowerStd(statusType);
        auto& manager = NPCRelationshipManager::GetSingleton();

        if (lower == "any") {
            return manager.IsRegistered(npc->GetFormID());
        }

        auto status = Utils::StringToRelationshipStatus(statusType);
        switch (status) {
            case RelationshipStatus::Candidate: return manager.IsCandidate(npc->GetFormID());
            case RelationshipStatus::Engaged:   return manager.IsEngaged(npc->GetFormID());
            case RelationshipStatus::Married:   return manager.IsMarried(npc->GetFormID());
            case RelationshipStatus::Divorced:  return manager.IsDivorced(npc->GetFormID());
            case RelationshipStatus::Jilted:    return manager.IsJilted(npc->GetFormID());
            case RelationshipStatus::Dating:    return manager.IsDating(npc->GetFormID());
            case RelationshipStatus::Lover:     return manager.IsLover(npc->GetFormID());
            case RelationshipStatus::ExLover:   return manager.IsExLover(npc->GetFormID());
            case RelationshipStatus::ExDating:  return manager.IsExDating(npc->GetFormID());
            default:                            return false;
        }
    }

    bool IsNPCStatusByEnum(RE::StaticFunctionTag*, RE::Actor* npc, std::int32_t statusEnum) {
        if (!npc) {
            MARAS_LOG_WARN("IsNPCStatusByEnum: null actor provided");
            return false;
        }

        std::lock_guard lock(GetPapyrusMutex());
        auto& manager = NPCRelationshipManager::GetSingleton();

        if (statusEnum == -1) {
            return manager.IsRegistered(npc->GetFormID());
        }

        if (statusEnum < 0 || statusEnum > static_cast<std::int32_t>(RelationshipStatus::ExDating)) {
            MARAS_LOG_WARN("IsNPCStatusByEnum: invalid status enum {}", statusEnum);
            return false;
        }

        switch (static_cast<RelationshipStatus>(statusEnum)) {
            case RelationshipStatus::Candidate: return manager.IsCandidate(npc->GetFormID());
            case RelationshipStatus::Engaged:   return manager.IsEngaged(npc->GetFormID());
            case RelationshipStatus::Married:   return manager.IsMarried(npc->GetFormID());
            case RelationshipStatus::Divorced:  return manager.IsDivorced(npc->GetFormID());
            case RelationshipStatus::Jilted:    return manager.IsJilted(npc->GetFormID());
            case RelationshipStatus::Dating:    return manager.IsDating(npc->GetFormID());
            case RelationshipStatus::Lover:     return manager.IsLover(npc->GetFormID());
            case RelationshipStatus::ExLover:   return manager.IsExLover(npc->GetFormID());
            case RelationshipStatus::ExDating:  return manager.IsExDating(npc->GetFormID());
            default:                            return false;
        }
    }

    bool IsNPCStatusWith(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target, std::string statusType) {
        if (!npc || !target) {
            MARAS_LOG_WARN("IsNPCStatusWith: null actor provided");
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        auto status = Utils::StringToRelationshipStatus(statusType);
        return NPCRelationshipManager::GetSingleton().GetRelationshipStatus(npc->GetFormID(), target->GetFormID()) == status;
    }

    bool IsNPCStatusWithByEnum(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target, std::int32_t statusEnum) {
        if (!npc || !target) {
            MARAS_LOG_WARN("IsNPCStatusWithByEnum: null actor provided");
            return false;
        }
        if (statusEnum < 0 || statusEnum > static_cast<std::int32_t>(RelationshipStatus::ExDating)) {
            MARAS_LOG_WARN("IsNPCStatusWithByEnum: invalid status enum {}", statusEnum);
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().GetRelationshipStatus(npc->GetFormID(), target->GetFormID()) == static_cast<RelationshipStatus>(statusEnum);
    }

    std::int32_t GetOriginalRelationshipStatus(RE::StaticFunctionTag*, RE::Actor* npc1, RE::Actor* npc2) {
        if (!npc1 || !npc2) return static_cast<std::int32_t>(RelationshipStatus::Unknown);
        std::lock_guard lock(GetPapyrusMutex());
        return static_cast<std::int32_t>(NPCRelationshipManager::GetSingleton().GetOriginalRelationshipStatus(npc1->GetFormID(), npc2->GetFormID()));
    }

    std::string GetOriginalRelationshipStatusString(RE::StaticFunctionTag*, RE::Actor* npc1, RE::Actor* npc2) {
        if (!npc1 || !npc2) return "unknown";
        std::lock_guard lock(GetPapyrusMutex());
        auto st = NPCRelationshipManager::GetSingleton().GetOriginalRelationshipStatus(npc1->GetFormID(), npc2->GetFormID());
        return std::string(Utils::RelationshipStatusToString(st));
    }

    std::string GetNpcStatusName(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("GetNpcStatusName: null actor provided");
            return "Unknown";
        }
        std::lock_guard lock(GetPapyrusMutex());
        auto status = NPCRelationshipManager::GetSingleton().GetRelationshipStatus(npc->GetFormID());
        return std::string(Utils::RelationshipStatusToString(status));
    }

    std::string GetNpcStatusNameWith(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target) {
        if (!npc || !target) {
            MARAS_LOG_WARN("GetNpcStatusNameWith: null actor provided");
            return "Unknown";
        }
        std::lock_guard lock(GetPapyrusMutex());
        auto status = NPCRelationshipManager::GetSingleton().GetRelationshipStatus(npc->GetFormID(), target->GetFormID());
        return std::string(Utils::RelationshipStatusToString(status));
    }

    std::int32_t GetNpcStatusEnum(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("GetNpcStatusEnum: null actor provided");
            return -1;
        }

        std::lock_guard lock(GetPapyrusMutex());
        auto& manager = NPCRelationshipManager::GetSingleton();
        if (!manager.IsRegistered(npc->GetFormID())) {
            return -1;
        }

        return static_cast<std::int32_t>(manager.GetRelationshipStatus(npc->GetFormID()));
    }

    std::int32_t GetNpcStatusEnumWith(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target) {
        if (!npc || !target) {
            MARAS_LOG_WARN("GetNpcStatusEnumWith: null actor provided");
            return -1;
        }

        std::lock_guard lock(GetPapyrusMutex());
        auto status = NPCRelationshipManager::GetSingleton().GetRelationshipStatus(npc->GetFormID(), target->GetFormID());
        if (status == RelationshipStatus::Unknown) {
            return -1;
        }
        return static_cast<std::int32_t>(status);
    }

    bool PromoteNPCToStatus(RE::StaticFunctionTag*, RE::Actor* npc, std::string statusType, bool sendEvent) {
        if (!npc) {
            MARAS_LOG_WARN("PromoteNPCToStatus: null actor provided");
            return false;
        }

        std::lock_guard lock(GetPapyrusMutex());
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto status = Utils::StringToRelationshipStatus(statusType);
        switch (status) {
            case RelationshipStatus::Candidate: return manager.DemoteToCandidate(npc->GetFormID(), kPlayerFormID, sendEvent);
            case RelationshipStatus::Engaged:   return manager.PromoteToEngaged(npc->GetFormID(), kPlayerFormID, sendEvent);
            case RelationshipStatus::Married:   return manager.PromoteToMarried(npc->GetFormID(), kPlayerFormID, sendEvent);
            case RelationshipStatus::Divorced:  return manager.PromoteToDivorced(npc->GetFormID(), kPlayerFormID, sendEvent);
            case RelationshipStatus::Jilted:    return manager.PromoteToJilted(npc->GetFormID(), kPlayerFormID, sendEvent);
            case RelationshipStatus::Dating:    return manager.PromoteToDating(npc->GetFormID(), kPlayerFormID, sendEvent);
            case RelationshipStatus::Lover:     return manager.PromoteToLover(npc->GetFormID(), kPlayerFormID, sendEvent);
            case RelationshipStatus::ExLover:   return manager.PromoteToExLover(npc->GetFormID(), kPlayerFormID, sendEvent);
            case RelationshipStatus::ExDating:  return manager.PromoteToExDating(npc->GetFormID(), kPlayerFormID, sendEvent);
            default:
                MARAS_LOG_WARN("PromoteNPCToStatus: invalid status '{}'", statusType);
                return false;
        }
    }

    bool PromoteNPCToStatusByEnum(RE::StaticFunctionTag*, RE::Actor* npc, std::int32_t statusEnum, bool sendEvent) {
        if (!npc) {
            MARAS_LOG_WARN("PromoteNPCToStatusByEnum: null actor provided");
            return false;
        }

        if (statusEnum < 0 || statusEnum > static_cast<std::int32_t>(RelationshipStatus::ExDating)) {
            MARAS_LOG_WARN("PromoteNPCToStatusByEnum: invalid status enum {}", statusEnum);
            return false;
        }

        std::lock_guard lock(GetPapyrusMutex());
        auto& manager = NPCRelationshipManager::GetSingleton();
        switch (static_cast<RelationshipStatus>(statusEnum)) {
            case RelationshipStatus::Candidate: return manager.DemoteToCandidate(npc->GetFormID(), kPlayerFormID, sendEvent);
            case RelationshipStatus::Engaged:   return manager.PromoteToEngaged(npc->GetFormID(), kPlayerFormID, sendEvent);
            case RelationshipStatus::Married:   return manager.PromoteToMarried(npc->GetFormID(), kPlayerFormID, sendEvent);
            case RelationshipStatus::Divorced:  return manager.PromoteToDivorced(npc->GetFormID(), kPlayerFormID, sendEvent);
            case RelationshipStatus::Jilted:    return manager.PromoteToJilted(npc->GetFormID(), kPlayerFormID, sendEvent);
            case RelationshipStatus::Dating:    return manager.PromoteToDating(npc->GetFormID(), kPlayerFormID, sendEvent);
            case RelationshipStatus::Lover:     return manager.PromoteToLover(npc->GetFormID(), kPlayerFormID, sendEvent);
            case RelationshipStatus::ExLover:   return manager.PromoteToExLover(npc->GetFormID(), kPlayerFormID, sendEvent);
            case RelationshipStatus::ExDating:  return manager.PromoteToExDating(npc->GetFormID(), kPlayerFormID, sendEvent);
            default:                            return false;
        }
    }

    bool PromoteNPCToStatusWith(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target, std::string statusType, bool sendEvent) {
        if (!npc || !target) {
            MARAS_LOG_WARN("PromoteNPCToStatusWith: null actor provided");
            return false;
        }

        std::lock_guard lock(GetPapyrusMutex());
        auto status = Utils::StringToRelationshipStatus(statusType);
        auto& manager = NPCRelationshipManager::GetSingleton();
        switch (status) {
            case RelationshipStatus::Candidate: return manager.DemoteToCandidate(npc->GetFormID(), target->GetFormID(), sendEvent);
            case RelationshipStatus::Engaged:   return manager.PromoteToEngaged(npc->GetFormID(), target->GetFormID(), sendEvent);
            case RelationshipStatus::Married:   return manager.PromoteToMarried(npc->GetFormID(), target->GetFormID(), sendEvent);
            case RelationshipStatus::Divorced:  return manager.PromoteToDivorced(npc->GetFormID(), target->GetFormID(), sendEvent);
            case RelationshipStatus::Jilted:    return manager.PromoteToJilted(npc->GetFormID(), target->GetFormID(), sendEvent);
            case RelationshipStatus::Dating:    return manager.PromoteToDating(npc->GetFormID(), target->GetFormID(), sendEvent);
            case RelationshipStatus::Lover:     return manager.PromoteToLover(npc->GetFormID(), target->GetFormID(), sendEvent);
            case RelationshipStatus::ExLover:   return manager.PromoteToExLover(npc->GetFormID(), target->GetFormID(), sendEvent);
            case RelationshipStatus::ExDating:  return manager.PromoteToExDating(npc->GetFormID(), target->GetFormID(), sendEvent);
            default:
                MARAS_LOG_WARN("PromoteNPCToStatusWith: invalid status '{}'", statusType);
                return false;
        }
    }

    bool PromoteNPCToStatusWithByEnum(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target, std::int32_t statusEnum, bool sendEvent) {
        if (!npc || !target) {
            MARAS_LOG_WARN("PromoteNPCToStatusWithByEnum: null actor provided");
            return false;
        }

        if (statusEnum < 0 || statusEnum > static_cast<std::int32_t>(RelationshipStatus::ExDating)) {
            MARAS_LOG_WARN("PromoteNPCToStatusWithByEnum: invalid status enum {}", statusEnum);
            return false;
        }

        std::lock_guard lock(GetPapyrusMutex());
        auto& manager = NPCRelationshipManager::GetSingleton();
        switch (static_cast<RelationshipStatus>(statusEnum)) {
            case RelationshipStatus::Candidate: return manager.DemoteToCandidate(npc->GetFormID(), target->GetFormID(), sendEvent);
            case RelationshipStatus::Engaged:   return manager.PromoteToEngaged(npc->GetFormID(), target->GetFormID(), sendEvent);
            case RelationshipStatus::Married:   return manager.PromoteToMarried(npc->GetFormID(), target->GetFormID(), sendEvent);
            case RelationshipStatus::Divorced:  return manager.PromoteToDivorced(npc->GetFormID(), target->GetFormID(), sendEvent);
            case RelationshipStatus::Jilted:    return manager.PromoteToJilted(npc->GetFormID(), target->GetFormID(), sendEvent);
            case RelationshipStatus::Dating:    return manager.PromoteToDating(npc->GetFormID(), target->GetFormID(), sendEvent);
            case RelationshipStatus::Lover:     return manager.PromoteToLover(npc->GetFormID(), target->GetFormID(), sendEvent);
            case RelationshipStatus::ExLover:   return manager.PromoteToExLover(npc->GetFormID(), target->GetFormID(), sendEvent);
            case RelationshipStatus::ExDating:  return manager.PromoteToExDating(npc->GetFormID(), target->GetFormID(), sendEvent);
            default:                            return false;
        }
    }

    bool EvaluateRelationshipStatusChange(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* partner, std::string targetStatus) {
        if (!npc || !partner) {
            MARAS_LOG_WARN("EvaluateRelationshipStatusChange: null actor provided");
            return false;
        }

        std::lock_guard lock(GetPapyrusMutex());
        return MARAS::EvaluateRelationshipStatusChange(npc->GetFormID(), partner->GetFormID(), targetStatus);
    }

    bool Breakup(RE::StaticFunctionTag*, RE::Actor* actorA, RE::Actor* actorB) {
        if (!actorA || !actorB) {
            MARAS_LOG_WARN("Breakup: null actor provided (actorA={}, actorB={})",
                           actorA ? ActorSvc::GetName(actorA->GetFormID()) : "null",
                           actorB ? ActorSvc::GetName(actorB->GetFormID()) : "null");
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().Breakup(actorA->GetFormID(), actorB->GetFormID());
    }

    std::int32_t BreakAllLoverConnections(RE::StaticFunctionTag*, RE::Actor* npc, bool onlyCheating) {
        if (!npc) {
            MARAS_LOG_WARN("BreakAllLoverConnections: null actor provided");
            return 0;
        }
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().BreakAllLoverConnections(npc->GetFormID(), onlyCheating);
    }

    // ========================================
    // Consolidated bulk retrieval, statistics & history
    // ========================================

    std::vector<RE::Actor*> GetNPCsByStatus(RE::StaticFunctionTag*, std::string statusType) {
        std::lock_guard lock(GetPapyrusMutex());
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto lower = StringUtils::ToLowerStd(statusType);

        if (lower == "all") {
            return ConvertFormIDsToActors(manager.GetAllRegisteredNPCs());
        }

        auto status = Utils::StringToRelationshipStatus(statusType);
        switch (status) {
            case RelationshipStatus::Candidate: return ConvertFormIDsToActors(manager.GetAllCandidates());
            case RelationshipStatus::Engaged:   return ConvertFormIDsToActors(manager.GetAllEngaged());
            case RelationshipStatus::Married:   return ConvertFormIDsToActors(manager.GetAllMarried());
            case RelationshipStatus::Divorced:  return ConvertFormIDsToActors(manager.GetAllDivorced());
            case RelationshipStatus::Jilted:    return ConvertFormIDsToActors(manager.GetAllJilted());
            case RelationshipStatus::Dating:    return ConvertFormIDsToActors(manager.GetAllDating());
            case RelationshipStatus::Lover:     return ConvertFormIDsToActors(manager.GetAllLovers());
            case RelationshipStatus::ExLover:   return ConvertFormIDsToActors(manager.GetAllExLovers());
            case RelationshipStatus::ExDating:  return ConvertFormIDsToActors(manager.GetAllExDating());
            default:                            return {};
        }
    }

    std::vector<RE::Actor*> GetNPCsByStatusEnum(RE::StaticFunctionTag*, std::int32_t statusEnum) {
        std::lock_guard lock(GetPapyrusMutex());
        auto& manager = NPCRelationshipManager::GetSingleton();

        if (statusEnum == -1) {
            return ConvertFormIDsToActors(manager.GetAllRegisteredNPCs());
        }

        if (statusEnum < 0 || statusEnum > static_cast<std::int32_t>(RelationshipStatus::ExDating)) {
            MARAS_LOG_WARN("GetNPCsByStatusEnum: invalid status enum {}", statusEnum);
            return {};
        }

        switch (static_cast<RelationshipStatus>(statusEnum)) {
            case RelationshipStatus::Candidate: return ConvertFormIDsToActors(manager.GetAllCandidates());
            case RelationshipStatus::Engaged:   return ConvertFormIDsToActors(manager.GetAllEngaged());
            case RelationshipStatus::Married:   return ConvertFormIDsToActors(manager.GetAllMarried());
            case RelationshipStatus::Divorced:  return ConvertFormIDsToActors(manager.GetAllDivorced());
            case RelationshipStatus::Jilted:    return ConvertFormIDsToActors(manager.GetAllJilted());
            case RelationshipStatus::Dating:    return ConvertFormIDsToActors(manager.GetAllDating());
            case RelationshipStatus::Lover:     return ConvertFormIDsToActors(manager.GetAllLovers());
            case RelationshipStatus::ExLover:   return ConvertFormIDsToActors(manager.GetAllExLovers());
            case RelationshipStatus::ExDating:  return ConvertFormIDsToActors(manager.GetAllExDating());
            default:                            return {};
        }
    }

    std::vector<RE::Actor*> GetNPCsByStatusWith(RE::StaticFunctionTag*, RE::Actor* target, std::string statusType) {
        if (!target) return {};
        std::lock_guard lock(GetPapyrusMutex());
        auto status = Utils::StringToRelationshipStatus(statusType);
        return ConvertFormIDsToActors(NPCRelationshipManager::GetSingleton().GetPartners(target->GetFormID(), status));
    }

    std::vector<RE::Actor*> GetRelationshipPartners(RE::StaticFunctionTag*, RE::Actor* npc, std::string statusType) {
        if (!npc) return {};
        std::lock_guard lock(GetPapyrusMutex());
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto lower = StringUtils::ToLowerStd(statusType);
        if (lower == "all") {
            return ConvertFormIDsToActors(manager.GetAllPartners(npc->GetFormID()));
        }
        auto status = Utils::StringToRelationshipStatus(statusType);
        return ConvertFormIDsToActors(manager.GetPartners(npc->GetFormID(), status));
    }

    std::vector<RE::Actor*> GetCurrentTeammates(RE::StaticFunctionTag*) {
        std::vector<RE::Actor*> result;
        auto idsSet = MARAS::PollingService::GetSingleton().GetCurrentTeammates();
        result.reserve(idsSet.size());

        for (auto id : idsSet) {
            if (auto form = RE::TESForm::LookupByID(id)) {
                if (auto actor = form->As<RE::Actor>()) result.push_back(actor);
            }
        }

        return result;
    }

    bool IsPlayerFollower(RE::StaticFunctionTag*, RE::Actor* npc) {
        return MARAS::Utils::IsPlayerFollower(npc);
    }

    int GetStatusCount(RE::StaticFunctionTag*, std::string statusType) {
        std::lock_guard lock(GetPapyrusMutex());
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto lower = StringUtils::ToLowerStd(statusType);

        if (lower == "all") {
            return static_cast<int>(manager.GetTotalRegisteredCount());
        }

        auto status = Utils::StringToRelationshipStatus(statusType);
        switch (status) {
            case RelationshipStatus::Candidate: return static_cast<int>(manager.GetCandidateCount());
            case RelationshipStatus::Engaged:   return static_cast<int>(manager.GetEngagedCount());
            case RelationshipStatus::Married:   return static_cast<int>(manager.GetMarriedCount());
            case RelationshipStatus::Divorced:  return static_cast<int>(manager.GetDivorcedCount());
            case RelationshipStatus::Jilted:    return static_cast<int>(manager.GetJiltedCount());
            case RelationshipStatus::Dating:    return static_cast<int>(manager.GetAllDating().size());
            case RelationshipStatus::Lover:     return static_cast<int>(manager.GetAllLovers().size());
            case RelationshipStatus::ExLover:   return static_cast<int>(manager.GetAllExLovers().size());
            case RelationshipStatus::ExDating:  return static_cast<int>(manager.GetAllExDating().size());
            default:                            return 0;
        }
    }

    int GetStatusCountByEnum(RE::StaticFunctionTag*, std::int32_t statusEnum) {
        std::lock_guard lock(GetPapyrusMutex());
        auto& manager = NPCRelationshipManager::GetSingleton();

        if (statusEnum == -1) {
            return static_cast<int>(manager.GetTotalRegisteredCount());
        }

        if (statusEnum < 0 || statusEnum > static_cast<std::int32_t>(RelationshipStatus::ExDating)) {
            MARAS_LOG_WARN("GetStatusCountByEnum: invalid status enum {}", statusEnum);
            return 0;
        }

        switch (static_cast<RelationshipStatus>(statusEnum)) {
            case RelationshipStatus::Candidate: return static_cast<int>(manager.GetCandidateCount());
            case RelationshipStatus::Engaged:   return static_cast<int>(manager.GetEngagedCount());
            case RelationshipStatus::Married:   return static_cast<int>(manager.GetMarriedCount());
            case RelationshipStatus::Divorced:  return static_cast<int>(manager.GetDivorcedCount());
            case RelationshipStatus::Jilted:    return static_cast<int>(manager.GetJiltedCount());
            case RelationshipStatus::Dating:    return static_cast<int>(manager.GetAllDating().size());
            case RelationshipStatus::Lover:     return static_cast<int>(manager.GetAllLovers().size());
            case RelationshipStatus::ExLover:   return static_cast<int>(manager.GetAllExLovers().size());
            case RelationshipStatus::ExDating:  return static_cast<int>(manager.GetAllExDating().size());
            default:                            return 0;
        }
    }

    int GetStatusHistoryCount(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target) {
        if (!npc) return 0;
        RE::FormID targetID = target ? target->GetFormID() : kPlayerFormID;
        std::lock_guard lock(GetPapyrusMutex());
        return static_cast<int>(NPCRelationshipManager::GetSingleton().GetStatusHistoryCount(npc->GetFormID(), targetID));
    }

    std::string GetStatusHistoryEntry(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target, int index) {
        if (!npc || index < 0) return std::string();
        RE::FormID targetID = target ? target->GetFormID() : kPlayerFormID;

        std::optional<StatusChangeRecord> optEntry;
        {
            std::lock_guard lock(GetPapyrusMutex());
            optEntry = NPCRelationshipManager::GetSingleton().GetStatusHistoryEntry(
                npc->GetFormID(), targetID, static_cast<std::size_t>(index));
        }

        if (!optEntry.has_value()) {
            return std::string();
        }

        const auto& entry = *optEntry;
        std::string fromStr(Utils::RelationshipStatusToString(entry.fromStatus));
        std::string toStr(Utils::RelationshipStatusToString(entry.toStatus));
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s|%s|%.2f|%08X", fromStr.c_str(), toStr.c_str(), entry.timestamp, entry.initiatorFormID);
        return std::string(buf);
    }

    // ========================================
    // Availability, Open Relationships & Infidelity
    // ========================================

    bool IsNPCInOpenRelationship(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().IsNPCInOpenRelationship(npc->GetFormID());
    }

    bool GetPairIsOpen(RE::StaticFunctionTag*, RE::Actor* actorA, RE::Actor* actorB) {
        if (!actorA || !actorB) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().GetPairIsOpen(actorA->GetFormID(), actorB->GetFormID());
    }

    bool SetPairIsOpen(RE::StaticFunctionTag*, RE::Actor* actorA, RE::Actor* actorB, bool isOpen) {
        if (!actorA || !actorB) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().SetPairIsOpen(actorA->GetFormID(), actorB->GetFormID(), isOpen);
    }

    bool IsAvailableForStatus(RE::StaticFunctionTag*, RE::Actor* npc, std::string status) {
        if (!npc) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().IsAvailableForStatus(npc->GetFormID(), status);
    }

    bool IsAvailableForStatusWith(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target, std::string status) {
        if (!npc) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().IsAvailableForStatus(npc->GetFormID(), target ? target->GetFormID() : 0, status);
    }

    bool CanFormRelationship(RE::StaticFunctionTag*, RE::Actor* actorA, RE::Actor* actorB, std::string status) {
        if (!actorA || !actorB) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().CanFormRelationship(actorA->GetFormID(), actorB->GetFormID(), status);
    }

    bool RecordCheating(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* victim, RE::Actor* affairPartner, bool isSecret) {
        if (!cheater || !victim || !affairPartner) {
            MARAS_LOG_WARN("RecordCheating: null argument(s) provided");
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().RecordCheating(
            cheater->GetFormID(), victim->GetFormID(), affairPartner->GetFormID(), isSecret);
    }

    bool SetCheatingDiscovered(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* affairPartner, RE::BSFixedString statusOrResolution) {
        if (!cheater) {
            MARAS_LOG_WARN("SetCheatingDiscovered: null cheater argument provided");
            return false;
        }
        RE::FormID cheaterID = cheater->GetFormID();
        RE::FormID partnerID = affairPartner ? affairPartner->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().SetCheatingDiscovered(cheaterID, partnerID, statusOrResolution.c_str());
    }

    bool SetCheatingAffairStatus(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* victim, RE::Actor* affairPartner, std::int32_t affairStatus) {
        if (!cheater || !victim || !affairPartner) {
            MARAS_LOG_WARN("SetCheatingAffairStatus: null argument(s) provided");
            return false;
        }
        if (affairStatus < 0 || affairStatus >= static_cast<std::int32_t>(AffairStatus::_Count)) {
            MARAS_LOG_WARN("SetCheatingAffairStatus: invalid affair status {}", affairStatus);
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().SetCheatingAffairStatus(
            cheater->GetFormID(), victim->GetFormID(), affairPartner->GetFormID(), static_cast<AffairStatus>(affairStatus));
    }

    std::int32_t GetCheatingAffairStatus(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* victim, RE::Actor* affairPartner) {
        if (!cheater || !victim || !affairPartner) {
            return static_cast<std::int32_t>(AffairStatus::Ongoing);
        }
        std::lock_guard lock(GetPapyrusMutex());
        return static_cast<std::int32_t>(NPCRelationshipManager::GetSingleton().GetCheatingAffairStatus(
            cheater->GetFormID(), victim->GetFormID(), affairPartner->GetFormID()));
    }

    std::int32_t GetAffairStage(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* victim, RE::Actor* affairPartner) {
        if (!cheater || !victim) {
            return static_cast<std::int32_t>(AffairStage::Physical);
        }
        RE::FormID partnerID = affairPartner ? affairPartner->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        return static_cast<std::int32_t>(NPCRelationshipManager::GetSingleton().GetCheatingAffairStage(
            cheater->GetFormID(), victim->GetFormID(), partnerID));
    }

    bool SetAffairStage(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* victim, RE::Actor* affairPartner, std::int32_t stage) {
        if (!cheater || !victim) {
            MARAS_LOG_WARN("SetAffairStage: null cheater or victim provided");
            return false;
        }
        if (stage < 0 || stage >= static_cast<std::int32_t>(AffairStage::_Count)) {
            MARAS_LOG_WARN("SetAffairStage: invalid stage {}", stage);
            return false;
        }
        RE::FormID partnerID = affairPartner ? affairPartner->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().SetCheatingAffairStage(
            cheater->GetFormID(), victim->GetFormID(), partnerID, static_cast<AffairStage>(stage));
    }

    std::string GetPartnerPendingIntent(RE::StaticFunctionTag*, RE::Actor* partner) {
        if (!partner) { MARAS_LOG_WARN("Papyrus GetPartnerPendingIntent called with null partner"); return "none"; }
        std::lock_guard lock(GetPapyrusMutex());
        auto intent = NPCRelationshipManager::GetSingleton().GetPartnerPendingIntent(partner->GetFormID());
        std::string res(Utils::PartnerIntentTypeToString(intent.type));
        MARAS_LOG_DEBUG("Papyrus GetPartnerPendingIntent({:08X} '{}') -> '{}'", partner->GetFormID(), partner->GetDisplayFullName(), res);
        return res;
    }

    void ClearPartnerIntent(RE::StaticFunctionTag*, RE::Actor* partner) {
        if (!partner) { MARAS_LOG_WARN("Papyrus ClearPartnerIntent called with null partner"); return; }
        MARAS_LOG_INFO("Papyrus ClearPartnerIntent called for {:08X} ('{}')", partner->GetFormID(), partner->GetDisplayFullName());
        std::lock_guard lock(GetPapyrusMutex());
        NPCRelationshipManager::GetSingleton().ClearPartnerIntent(partner->GetFormID());
    }

    bool EvaluatePartnerHomeArrival(RE::StaticFunctionTag*, RE::BGSLocation* akNewLoc) {
        std::lock_guard lock(GetPapyrusMutex());
        MARAS_LOG_DEBUG("Papyrus EvaluatePartnerHomeArrival invoked (akNewLoc={:08X} '{}')", akNewLoc ? akNewLoc->GetFormID() : 0, akNewLoc ? akNewLoc->GetName() : "None");
        return MatchmakingService::GetSingleton().EvaluatePartnerHomeArrival(akNewLoc);
    }

    bool HasCheatedOn(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* victim) {
        if (!cheater || !victim) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().HasCheatedOn(cheater->GetFormID(), victim->GetFormID());
    }

    bool IsNPCCheating(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().IsNPCCheating(npc->GetFormID());
    }

    bool IsRelationshipCheating(RE::StaticFunctionTag*, RE::Actor* actorA, RE::Actor* actorB) {
        if (!actorA || !actorB) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().IsRelationshipCheating(actorA->GetFormID(), actorB->GetFormID());
    }

    std::int32_t GetCheatingCount(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* victim) {
        if (!cheater || !victim) return 0;
        std::lock_guard lock(GetPapyrusMutex());
        return static_cast<std::int32_t>(NPCRelationshipManager::GetSingleton().GetCheatingCount(cheater->GetFormID(), victim->GetFormID()));
    }

    std::vector<RE::Actor*> GetCheatedOnPartners(RE::StaticFunctionTag*, RE::Actor* cheater) {
        std::vector<RE::Actor*> result;
        if (!cheater) return result;
        std::lock_guard lock(GetPapyrusMutex());
        auto ids = NPCRelationshipManager::GetSingleton().GetCheatedOnPartners(cheater->GetFormID());
        for (auto id : ids) {
            if (auto* a = NPCIndex::GetSingleton().GetActorFromFormId(id)) {
                result.push_back(a);
            }
        }
        return result;
    }

    std::vector<RE::Actor*> GetCheatersWhoCheatedOn(RE::StaticFunctionTag*, RE::Actor* victim) {
        std::vector<RE::Actor*> result;
        if (!victim) return result;
        std::lock_guard lock(GetPapyrusMutex());
        auto ids = NPCRelationshipManager::GetSingleton().GetCheatersWhoCheatedOn(victim->GetFormID());
        for (auto id : ids) {
            if (auto* a = NPCIndex::GetSingleton().GetActorFromFormId(id)) {
                result.push_back(a);
            }
        }
        return result;
    }

    // ========================================
    // Spouse hierarchy, marriage difficulty, buffs, bonuses & affection
    // ========================================

    bool SetHierarchyRank(RE::StaticFunctionTag*, RE::Actor* npc, std::int32_t rank) {
        if (!npc) {
            MARAS_LOG_WARN("SetHierarchyRank called with null actor");
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        return MARAS::SpouseHierarchyManager::GetSingleton().SetRank(npc->GetFormID(), static_cast<int>(rank));
    }

    std::int32_t GetHierarchyRank(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("GetHierarchyRank called with null actor");
            return 4;
        }
        std::lock_guard lock(GetPapyrusMutex());
        return MARAS::SpouseHierarchyManager::GetSingleton().GetRank(npc->GetFormID());
    }

    float CalculateMarriageSuccessChance(RE::StaticFunctionTag*, RE::Actor* npc, float intimacyAdjustment,
                                         float mostGold, float housesOwned, float horsesOwned, float questsCompleted,
                                         float dungeonsCleared, float dragonSoulsCollected, bool playerKiller) {
        if (!npc) {
            MARAS_LOG_WARN("CalculateMarriageSuccessChance called with null NPC");
            return 0.0f;
        }
        std::lock_guard lock(GetPapyrusMutex());
        return MarriageDifficulty::CalculateMarriageSuccessChance(npc, intimacyAdjustment, mostGold, housesOwned,
                                                                  horsesOwned, questsCompleted, dungeonsCleared,
                                                                  dragonSoulsCollected, playerKiller);
    }

    float GetSpouseMultiplier(RE::StaticFunctionTag*, RE::Actor* spouse) {
        if (!spouse) {
            MARAS_LOG_WARN("GetSpouseMultiplier called with null actor");
            return 0.0f;
        }
        std::lock_guard lock(GetPapyrusMutex());
        return MARAS::SpouseBuffService::GetSpouseMultiplier(spouse);
    }

    std::vector<float> GetFollowersMultipliers(RE::StaticFunctionTag*, std::vector<RE::Actor*> followers) {
        std::lock_guard lock(GetPapyrusMutex());
        return MARAS::SpouseBuffService::GetFollowersMultipliers(followers);
    }

    std::vector<float> GetPermanentMultipliers(RE::StaticFunctionTag*) {
        std::lock_guard lock(GetPapyrusMutex());
        return MARAS::SpouseBuffService::GetPermanentMultipliers();
    }

    int GetBonusCount(RE::StaticFunctionTag*, std::string type) {
        return MARAS::BonusesService::GetSingleton().GetBonusCount(StringUtils::ToLowerStd(type));
    }

    RE::BGSPerk* GetBonusPerk(RE::StaticFunctionTag*, std::string type, int index) {
        const auto* be = MARAS::BonusesService::GetSingleton().GetBonusEntry(StringUtils::ToLowerStd(type), index);
        if (!be || be->perk.empty()) return nullptr;

        auto parsed = MARAS::Utils::ParseFormKey(be->perk);
        if (!parsed.isValid) return nullptr;

        return MARAS::Utils::LookupForm<RE::BGSPerk>(parsed.localFormID, parsed.pluginName);
    }

    int GetBonusEffectIndex(RE::StaticFunctionTag*, std::string type, int index) {
        const auto* be = MARAS::BonusesService::GetSingleton().GetBonusEntry(StringUtils::ToLowerStd(type), index);
        return be ? be->effectIndex : 0;
    }

    float GetBonusPerkValue(RE::StaticFunctionTag*, std::string type, int index) {
        const auto* be = MARAS::BonusesService::GetSingleton().GetBonusEntry(StringUtils::ToLowerStd(type), index);
        return be ? be->value : 0.0f;
    }

    std::string GetBonusPerkType(RE::StaticFunctionTag*, std::string type, int index) {
        const auto* be = MARAS::BonusesService::GetSingleton().GetBonusEntry(StringUtils::ToLowerStd(type), index);
        return be ? be->type : std::string();
    }

    std::string GetBonusPerkUnit(RE::StaticFunctionTag*, std::string type, int index) {
        const auto* be = MARAS::BonusesService::GetSingleton().GetBonusEntry(StringUtils::ToLowerStd(type), index);
        return be ? be->unit : std::string();
    }

    std::string GetBonusPerkDescription(RE::StaticFunctionTag*, std::string type, int index) {
        const auto* be = MARAS::BonusesService::GetSingleton().GetBonusEntry(StringUtils::ToLowerStd(type), index);
        return be ? be->description : std::string();
    }

    void AddAffection(RE::StaticFunctionTag*, RE::Actor* npc, float amount, std::string type) {
        if (!npc) {
            MARAS_LOG_WARN("AddAffection: null actor provided");
            return;
        }
        std::lock_guard lock(GetPapyrusMutex());
        MARAS::AffectionService::GetSingleton().AddAffection(npc->GetFormID(), amount, type);
    }

    float GetAffection(RE::StaticFunctionTag*, RE::Actor* npc, std::string type) {
        if (!npc) {
            MARAS_LOG_WARN("GetAffection: null actor provided");
            return 0.0f;
        }
        std::lock_guard lock(GetPapyrusMutex());
        return MARAS::AffectionService::GetSingleton().GetDailyAffection(npc->GetFormID(), type);
    }

    int GetPermanentAffection(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("GetPermanentAffection: null actor provided");
            return 0;
        }
        std::lock_guard lock(GetPapyrusMutex());
        return MARAS::AffectionService::GetSingleton().GetPermanentAffection(npc->GetFormID());
    }

    void SetPermanentAffection(RE::StaticFunctionTag*, RE::Actor* npc, int amount, bool sendEvent) {
        if (!npc) {
            MARAS_LOG_WARN("SetPermanentAffection: null actor provided");
            return;
        }
        std::lock_guard lock(GetPapyrusMutex());
        MARAS::AffectionService::GetSingleton().SetPermanentAffection(npc->GetFormID(), amount, sendEvent);
    }

    void SetAffectionMinMax(RE::StaticFunctionTag*, std::string type, int minVal, int maxVal) {
        std::lock_guard lock(GetPapyrusMutex());
        MARAS::AffectionService::GetSingleton().SetAffectionMinMax(type, minVal, maxVal);
    }

    void ApplyDailyAffection(RE::StaticFunctionTag*) {
        std::lock_guard lock(GetPapyrusMutex());
        MARAS::AffectionService::GetSingleton().ApplyDailyAffectionsForAll();
    }

    void SetAffectionDecayMultiplier(RE::StaticFunctionTag*, float multiplier) {
        std::lock_guard lock(GetPapyrusMutex());
        MARAS::AffectionService::GetSingleton().SetDecayMultiplier(multiplier);
    }

    float GetAffectionDecayMultiplier(RE::StaticFunctionTag*) {
        std::lock_guard lock(GetPapyrusMutex());
        return MARAS::AffectionService::GetSingleton().GetDecayMultiplier();
    }

    // ========================================
    // Deceased Registry queries
    // ========================================

    bool IsWidow(RE::StaticFunctionTag*, RE::Actor* actor) {
        if (!actor) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return DeceasedRegistry::GetSingleton().IsWidowOrWidower(actor->GetFormID());
    }

    bool IsNPCDeceased(RE::StaticFunctionTag*, RE::Actor* actor) {
        if (!actor) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return DeceasedRegistry::GetSingleton().IsDeceased(actor->GetFormID());
    }

    bool WasMarriedTo(RE::StaticFunctionTag*, RE::Actor* deceased, RE::Actor* partner) {
        if (!deceased || !partner) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return DeceasedRegistry::GetSingleton().WasMarriedTo(deceased->GetFormID(), partner->GetFormID());
    }

    bool WasEngagedTo(RE::StaticFunctionTag*, RE::Actor* deceased, RE::Actor* partner) {
        if (!deceased || !partner) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return DeceasedRegistry::GetSingleton().WasEngagedTo(deceased->GetFormID(), partner->GetFormID());
    }

    bool WasExOf(RE::StaticFunctionTag*, RE::Actor* deceased, RE::Actor* partner) {
        if (!deceased || !partner) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return DeceasedRegistry::GetSingleton().WasExOf(deceased->GetFormID(), partner->GetFormID());
    }

    std::vector<std::string> GetDeceasedSpouseNames(RE::StaticFunctionTag*, RE::Actor* actor) {
        std::vector<std::string> result;
        if (!actor) return result;
        std::lock_guard lock(GetPapyrusMutex());
        auto records = DeceasedRegistry::GetSingleton().GetDeceasedSpousesOf(actor->GetFormID());
        for (const auto& rec : records) {
            result.push_back(rec.name);
        }
        return result;
    }

    std::vector<std::string> GetDeceasedChildrenNames(RE::StaticFunctionTag*, RE::Actor* actor) {
        std::vector<std::string> result;
        if (!actor) return result;
        std::lock_guard lock(GetPapyrusMutex());
        auto records = DeceasedRegistry::GetSingleton().GetDeceasedChildrenOf(actor->GetFormID());
        for (const auto& rec : records) {
            result.push_back(rec.name);
        }
        return result;
    }

    bool HasDeceasedChildren(RE::StaticFunctionTag*, RE::Actor* actor) {
        if (!actor) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return DeceasedRegistry::GetSingleton().HasDeceasedChildren(actor->GetFormID());
    }

    std::int32_t GetDeceasedCount(RE::StaticFunctionTag*) {
        std::lock_guard lock(GetPapyrusMutex());
        return static_cast<std::int32_t>(DeceasedRegistry::GetSingleton().GetDeceasedCount());
    }

}  // namespace MARAS::PapyrusInterface

