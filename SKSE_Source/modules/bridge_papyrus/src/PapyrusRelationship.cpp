#include "maras/bridge_papyrus/PapyrusInterface.h"

#include "maras/app/AgencyWorkflowCoordinator.h"
#include "maras/app/LineageUseCases.h"
#include "maras/app/RelationshipInteractionCoordinator.h"
#include "maras/app/RelationshipUseCases.h"
#include "maras/domain/household/SpouseBuffRules.h"
#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/BonusesConfigLoader.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FormUtils.h"
#include "maras/engine/PollingService.h"
#include "maras/engine/RelationshipEngineReconciler.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/StringUtils.h"
#include "maras/state/WorldStore.h"

#include <algorithm>
#include <format>

namespace MARAS::PapyrusInterface {

    namespace {
        [[nodiscard]] bool IsValidRelationshipStatusInt(int32_t statusEnum) noexcept {
            return statusEnum >= static_cast<int32_t>(Foundation::RelationshipStatus::Candidate) &&
                   statusEnum <= static_cast<int32_t>(Foundation::RelationshipStatus::ExLover);
        }

        [[nodiscard]] std::vector<RE::Actor*> ResolveActorsById(const std::vector<RE::FormID>& formIds) {
            std::vector<RE::Actor*> result;
            result.reserve(formIds.size());
            const auto& index = SpatialWorldIndex::GetSingleton();
            for (const RE::FormID id : formIds) {
                if (id == 0) {
                    continue;
                }
                if (auto* actor = index.GetActorFromFormId(id)) {
                    result.push_back(actor);
                }
            }
            return result;
        }

        [[nodiscard]] const Domain::Relationships::BonusEntry* LookupBonusEntry(
            const std::string& type,
            int32_t index) {
            if (index < 0) {
                return nullptr;
            }
            return Engine::BonusesConfigLoader::GetSingleton().GetBonusEntry(type, index);
        }
    }  // namespace

    // ========================================
    // Core Actor Registration & Removal
    // ========================================

    bool RegisterCandidate(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("RegisterCandidate: null NPC provided");
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().RegisterAsCandidate(npc->GetFormID());
    }

    bool UnregisterNPC(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("UnregisterNPC: null NPC provided");
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().UnregisterNPC(npc->GetFormID());
    }

    bool SetNpcCharacteristics(RE::StaticFunctionTag*, RE::Actor* npc, std::string factionType, int32_t rank) {
        if (!npc) {
            MARAS_LOG_WARN("SetNpcCharacteristics: null NPC provided");
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        auto& relUseCases = App::RelationshipUseCases::GetSingleton();
        const std::string lowerType = Foundation::StringUtils::ToLowerStd(factionType);

        if (lowerType == "socialclass" || lowerType == "social") {
            if (rank < 0 || rank >= static_cast<int32_t>(Foundation::SocialClass::_Count)) {
                MARAS_LOG_WARN("SetNpcCharacteristics: out-of-range SocialClass rank {} for {:08X}",
                               rank, npc->GetFormID());
                return false;
            }
            return relUseCases.SetSocialClass(npc->GetFormID(), static_cast<std::int8_t>(rank));
        }

        if (lowerType == "skilltype" || lowerType == "skill") {
            if (rank < 0 || rank >= static_cast<int32_t>(Foundation::SkillType::_Count)) {
                MARAS_LOG_WARN("SetNpcCharacteristics: out-of-range SkillType rank {} for {:08X}",
                               rank, npc->GetFormID());
                return false;
            }
            return relUseCases.SetSkillType(npc->GetFormID(), static_cast<std::int8_t>(rank));
        }

        if (lowerType == "temperament") {
            if (rank < 0 || rank >= static_cast<int32_t>(Foundation::Temperament::_Count)) {
                MARAS_LOG_WARN("SetNpcCharacteristics: out-of-range Temperament rank {} for {:08X}",
                               rank, npc->GetFormID());
                return false;
            }
            return relUseCases.SetTemperament(npc->GetFormID(), static_cast<std::int8_t>(rank));
        }

        MARAS_LOG_WARN("SetNpcCharacteristics: unknown factionType '{}'", factionType);
        return false;
    }

    // ========================================
    // Relationship Status Checks
    // ========================================

    bool IsNPCStatus(RE::StaticFunctionTag* tag, RE::Actor* npc, std::string statusType) {
        return IsNPCStatusWith(tag, npc, nullptr, std::move(statusType));
    }

    bool IsNPCStatusByEnum(RE::StaticFunctionTag* tag, RE::Actor* npc, int32_t statusEnum) {
        return IsNPCStatusWithByEnum(tag, npc, nullptr, statusEnum);
    }

    bool IsNPCStatusWith(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target, std::string statusType) {
        if (!npc) {
            return false;
        }

        const bool anyStatus =
            Foundation::StringUtils::CaseInsensitiveEquals(statusType, "any") ||
            Foundation::StringUtils::CaseInsensitiveEquals(statusType, "all");
        const auto parsed = anyStatus
            ? Foundation::RelationshipStatus::Unknown
            : Foundation::StringToRelationshipStatus(statusType);
        if (!anyStatus && parsed == Foundation::RelationshipStatus::Unknown) {
            MARAS_LOG_WARN("IsNPCStatusWith: unknown statusType '{}'", statusType);
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID targetID = target ? target->GetFormID() : kPlayerFormID;
        const auto current = App::RelationshipUseCases::GetSingleton().GetRelationshipStatus(npc->GetFormID(), targetID);
        return anyStatus ? (current != Foundation::RelationshipStatus::Unknown) : (current == parsed);
    }

    bool IsNPCStatusWithByEnum(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target, int32_t statusEnum) {
        if (!npc || !IsValidRelationshipStatusInt(statusEnum)) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID targetID = target ? target->GetFormID() : kPlayerFormID;
        return App::RelationshipUseCases::GetSingleton().GetRelationshipStatus(npc->GetFormID(), targetID) ==
               static_cast<Foundation::RelationshipStatus>(statusEnum);
    }

    std::string GetNpcStatusName(RE::StaticFunctionTag* tag, RE::Actor* npc) {
        return GetNpcStatusNameWith(tag, npc, nullptr);
    }

    int32_t GetNpcStatusEnum(RE::StaticFunctionTag* tag, RE::Actor* npc) {
        return GetNpcStatusEnumWith(tag, npc, nullptr);
    }

    std::string GetNpcStatusNameWith(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target) {
        if (!npc) {
            return std::string(Foundation::RelationshipStatusToString(Foundation::RelationshipStatus::Unknown));
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID targetID = target ? target->GetFormID() : kPlayerFormID;
        const auto status = App::RelationshipUseCases::GetSingleton().GetRelationshipStatus(npc->GetFormID(), targetID);
        return std::string(Foundation::RelationshipStatusToString(status));
    }

    int32_t GetNpcStatusEnumWith(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target) {
        if (!npc) {
            return static_cast<int32_t>(Foundation::RelationshipStatus::Unknown);
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID targetID = target ? target->GetFormID() : kPlayerFormID;
        const auto status = App::RelationshipUseCases::GetSingleton().GetRelationshipStatus(npc->GetFormID(), targetID);
        return static_cast<int32_t>(status);
    }

    // ========================================
    // Original Relationship Tracking
    // ========================================

    int32_t GetOriginalRelationshipStatus(RE::StaticFunctionTag*, RE::Actor* npc1, RE::Actor* npc2) {
        if (!npc1 || !npc2) {
            return static_cast<int32_t>(Foundation::RelationshipStatus::Unknown);
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(App::RelationshipUseCases::GetSingleton().GetOriginalRelationshipStatus(
            npc1->GetFormID(), npc2->GetFormID()));
    }

    std::string GetOriginalRelationshipStatusString(RE::StaticFunctionTag*, RE::Actor* npc1, RE::Actor* npc2) {
        if (!npc1 || !npc2) {
            return std::string(Foundation::RelationshipStatusToString(Foundation::RelationshipStatus::Unknown));
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto orig = App::RelationshipUseCases::GetSingleton().GetOriginalRelationshipStatus(
            npc1->GetFormID(), npc2->GetFormID());
        return std::string(Foundation::RelationshipStatusToString(orig));
    }

    // ========================================
    // Status Transitions (Promotions)
    // ========================================

    bool PromoteNPCToStatus(RE::StaticFunctionTag* tag, RE::Actor* npc, std::string statusType, bool sendEvent) {
        return PromoteNPCToStatusWith(tag, npc, nullptr, std::move(statusType), sendEvent);
    }

    bool PromoteNPCToStatusByEnum(RE::StaticFunctionTag* tag, RE::Actor* npc, int32_t statusEnum, bool sendEvent) {
        return PromoteNPCToStatusWithByEnum(tag, npc, nullptr, statusEnum, sendEvent);
    }

    bool PromoteNPCToStatusWith(RE::StaticFunctionTag*,
                                RE::Actor* npc,
                                RE::Actor* target,
                                std::string statusType,
                                bool sendEvent) {
        if (!npc) {
            MARAS_LOG_WARN("PromoteNPCToStatusWith: null NPC provided");
            return false;
        }

        const auto parsed = Foundation::StringToRelationshipStatus(statusType);
        if (parsed == Foundation::RelationshipStatus::Unknown) {
            MARAS_LOG_WARN("PromoteNPCToStatusWith: unknown statusType '{}'", statusType);
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID targetID = target ? target->GetFormID() : kPlayerFormID;
        return App::RelationshipUseCases::GetSingleton().PromoteToStatusWith(
            npc->GetFormID(), targetID, parsed, sendEvent);
    }

    bool PromoteNPCToStatusWithByEnum(RE::StaticFunctionTag*,
                                      RE::Actor* npc,
                                      RE::Actor* target,
                                      int32_t statusEnum,
                                      bool sendEvent) {
        if (!npc || !IsValidRelationshipStatusInt(statusEnum)) {
            MARAS_LOG_WARN("PromoteNPCToStatusWithByEnum: invalid args (npc={:08X}, statusEnum={})",
                           npc ? npc->GetFormID() : 0, statusEnum);
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID targetID = target ? target->GetFormID() : kPlayerFormID;
        return App::RelationshipUseCases::GetSingleton().PromoteToStatusWith(
            npc->GetFormID(), targetID, static_cast<Foundation::RelationshipStatus>(statusEnum), sendEvent);
    }

    // ========================================
    // Breakup & Open Relationships & Infidelity
    // ========================================

    bool Breakup(RE::StaticFunctionTag*, RE::Actor* actorA, RE::Actor* actorB) {
        if (!actorA || !actorB) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().Breakup(actorA->GetFormID(), actorB->GetFormID(), true);
    }

    int32_t BreakAllLoverConnections(RE::StaticFunctionTag*, RE::Actor* npc, bool onlyCheating) {
        if (!npc) {
            return 0;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().BreakAllLoverConnections(
            npc->GetFormID(), onlyCheating, true);
    }

    bool IsNPCInOpenRelationship(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().IsNPCInOpenRelationship(npc->GetFormID());
    }

    bool GetPairIsOpen(RE::StaticFunctionTag*, RE::Actor* actorA, RE::Actor* actorB) {
        if (!actorA || !actorB) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().GetPairIsOpen(actorA->GetFormID(), actorB->GetFormID());
    }

    bool SetPairIsOpen(RE::StaticFunctionTag*, RE::Actor* actorA, RE::Actor* actorB, bool isOpen) {
        if (!actorA || !actorB) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().SetPairIsOpen(
            actorA->GetFormID(), actorB->GetFormID(), isOpen, true);
    }

    bool RecordCheating(RE::StaticFunctionTag*,
                        RE::Actor* cheater,
                        RE::Actor* victim,
                        RE::Actor* affairPartner,
                        bool isSecret) {
        if (!cheater || !victim || !affairPartner) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().RecordCheating(
            cheater->GetFormID(), victim->GetFormID(), affairPartner->GetFormID(), isSecret);
    }

    bool SetCheatingDiscovered(RE::StaticFunctionTag*,
                               RE::Actor* cheater,
                               RE::Actor* affairPartner,
                               std::string statusOrResolution,
                               RE::Actor* victim) {
        if (!cheater) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().SetCheatingDiscovered(
            cheater->GetFormID(),
            affairPartner ? affairPartner->GetFormID() : 0,
            statusOrResolution,
            victim ? victim->GetFormID() : 0);
    }

    bool SetCheatingAffairStatus(RE::StaticFunctionTag*,
                                 RE::Actor* cheater,
                                 RE::Actor* victim,
                                 RE::Actor* affairPartner,
                                 int32_t affairStatus) {
        if (!cheater || !victim || !affairPartner) {
            return false;
        }
        if (affairStatus < 0 || affairStatus >= static_cast<int32_t>(Foundation::AffairStatus::_Count)) {
            MARAS_LOG_WARN("SetCheatingAffairStatus: out-of-range affairStatus {}", affairStatus);
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().SetCheatingAffairStatus(
            cheater->GetFormID(),
            victim->GetFormID(),
            affairPartner->GetFormID(),
            static_cast<Foundation::AffairStatus>(affairStatus));
    }

    int32_t GetCheatingAffairStatus(RE::StaticFunctionTag*,
                                    RE::Actor* cheater,
                                    RE::Actor* victim,
                                    RE::Actor* affairPartner) {
        if (!cheater || !victim || !affairPartner) {
            return static_cast<int32_t>(Foundation::AffairStatus::Ongoing);
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(App::RelationshipUseCases::GetSingleton().GetCheatingAffairStatus(
            cheater->GetFormID(), victim->GetFormID(), affairPartner->GetFormID()));
    }

    int32_t GetAffairStage(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* victim, RE::Actor* affairPartner) {
        if (!cheater || !victim) {
            return static_cast<int32_t>(Foundation::AffairStage::Physical);
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto& relUseCases = App::RelationshipUseCases::GetSingleton();
        if (affairPartner) {
            return static_cast<int32_t>(relUseCases.GetCheatingAffairStage(
                cheater->GetFormID(), victim->GetFormID(), affairPartner->GetFormID()));
        }

        const auto records = relUseCases.GetCheatingRecords(cheater->GetFormID(), victim->GetFormID());
        for (const auto& rec : records) {
            if (rec.cheaterID == cheater->GetFormID() &&
                rec.victimID == victim->GetFormID() &&
                rec.affairStatus == Foundation::AffairStatus::Ongoing) {
                return static_cast<int32_t>(rec.affairStage);
            }
        }
        return static_cast<int32_t>(Foundation::AffairStage::Physical);
    }

    bool SetAffairStage(RE::StaticFunctionTag*,
                        RE::Actor* cheater,
                        RE::Actor* victim,
                        RE::Actor* affairPartner,
                        int32_t stage) {
        if (!cheater || !victim || !affairPartner) {
            return false;
        }
        if (stage < 0 || stage >= static_cast<int32_t>(Foundation::AffairStage::_Count)) {
            MARAS_LOG_WARN("SetAffairStage: out-of-range stage {}", stage);
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().SetCheatingAffairStage(
            cheater->GetFormID(),
            victim->GetFormID(),
            affairPartner->GetFormID(),
            static_cast<Foundation::AffairStage>(stage));
    }

    std::string GetPartnerPendingIntent(RE::StaticFunctionTag*, RE::Actor* partner) {
        if (!partner) {
            return "none";
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto intent = App::RelationshipUseCases::GetSingleton().GetPartnerPendingIntent(partner->GetFormID());
        return std::string(Foundation::PartnerIntentTypeToString(intent.type));
    }

    void ClearPartnerIntent(RE::StaticFunctionTag*, RE::Actor* partner) {
        if (!partner) {
            return;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::RelationshipUseCases::GetSingleton().ClearPartnerIntent(partner->GetFormID());
    }

    bool EvaluatePartnerHomeArrival(RE::StaticFunctionTag*, RE::BGSLocation* akNewLoc) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::AgencyWorkflowCoordinator::GetSingleton().EvaluatePartnerHomeArrival(akNewLoc);
    }

    bool HasCheatedOn(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* victim) {
        if (!cheater || !victim) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().HasCheatedOn(cheater->GetFormID(), victim->GetFormID());
    }

    bool IsNPCCheating(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().IsNPCCheating(npc->GetFormID());
    }

    bool IsRelationshipCheating(RE::StaticFunctionTag*, RE::Actor* actorA, RE::Actor* actorB) {
        if (!actorA || !actorB) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().IsRelationshipCheating(
            actorA->GetFormID(), actorB->GetFormID());
    }

    int32_t GetCheatingCount(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* victim) {
        if (!cheater || !victim) {
            return 0;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(App::RelationshipUseCases::GetSingleton().GetCheatingCount(
            cheater->GetFormID(), victim->GetFormID()));
    }

    std::vector<RE::Actor*> GetCheatedOnPartners(RE::StaticFunctionTag*, RE::Actor* cheater) {
        if (!cheater) {
            return {};
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorsById(
            App::RelationshipUseCases::GetSingleton().GetCheatedOnPartners(cheater->GetFormID()));
    }

    std::vector<RE::Actor*> GetCheatersWhoCheatedOn(RE::StaticFunctionTag*, RE::Actor* victim) {
        if (!victim) {
            return {};
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorsById(
            App::RelationshipUseCases::GetSingleton().GetCheatersWhoCheatedOn(victim->GetFormID()));
    }

    // ========================================
    // Deceased NPC Registry
    // ========================================

    bool IsWidow(RE::StaticFunctionTag*, RE::Actor* akActor) {
        if (!akActor) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().IsWidowOrWidower(akActor->GetFormID());
    }

    bool IsNPCDeceased(RE::StaticFunctionTag*, RE::Actor* akActor) {
        if (!akActor) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().IsDeceased(akActor->GetFormID());
    }

    bool WasMarriedTo(RE::StaticFunctionTag*, RE::Actor* akDeceased, RE::Actor* akPartner) {
        if (!akDeceased || !akPartner) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().WasMarriedTo(akDeceased->GetFormID(), akPartner->GetFormID());
    }

    bool WasEngagedTo(RE::StaticFunctionTag*, RE::Actor* akDeceased, RE::Actor* akPartner) {
        if (!akDeceased || !akPartner) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().WasEngagedTo(akDeceased->GetFormID(), akPartner->GetFormID());
    }

    bool WasExOf(RE::StaticFunctionTag*, RE::Actor* akDeceased, RE::Actor* akPartner) {
        if (!akDeceased || !akPartner) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().WasExOf(akDeceased->GetFormID(), akPartner->GetFormID());
    }

    std::vector<std::string> GetDeceasedSpouseNames(RE::StaticFunctionTag*, RE::Actor* akActor) {
        if (!akActor) {
            return {};
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto records = App::LineageUseCases::GetSingleton().GetDeceasedSpousesOf(akActor->GetFormID());
        std::vector<std::string> names;
        names.reserve(records.size());
        for (const auto& rec : records) {
            if (!rec.name.empty()) {
                names.push_back(rec.name);
            }
        }
        return names;
    }

    std::vector<std::string> GetDeceasedChildrenNames(RE::StaticFunctionTag*, RE::Actor* akActor) {
        if (!akActor) {
            return {};
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto records = App::LineageUseCases::GetSingleton().GetDeceasedChildrenOf(akActor->GetFormID());
        std::vector<std::string> names;
        names.reserve(records.size());
        for (const auto& rec : records) {
            if (!rec.name.empty()) {
                names.push_back(rec.name);
            }
        }
        return names;
    }

    bool HasDeceasedChildren(RE::StaticFunctionTag*, RE::Actor* akActor) {
        if (!akActor) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().HasDeceasedChildren(akActor->GetFormID());
    }

    int32_t GetDeceasedCount(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(App::LineageUseCases::GetSingleton().GetDeceasedCount());
    }

    // ========================================
    // Relationship Availability & Family Checks
    // ========================================

    bool IsAvailableForStatus(RE::StaticFunctionTag* tag, RE::Actor* npc, std::string status) {
        return IsAvailableForStatusWith(tag, npc, nullptr, std::move(status));
    }

    bool IsAvailableForStatusWith(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target, std::string status) {
        if (!npc) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID targetID = target ? target->GetFormID() : kPlayerFormID;
        return App::RelationshipUseCases::GetSingleton().IsAvailableForStatus(npc->GetFormID(), targetID, status);
    }

    bool CanFormRelationship(RE::StaticFunctionTag*, RE::Actor* actorA, RE::Actor* actorB, std::string status) {
        if (!actorA || !actorB) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().CanFormRelationship(
            actorA->GetFormID(), actorB->GetFormID(), status);
    }

    // ========================================
    // Bulk Retrieval
    // ========================================

    std::vector<RE::Actor*> GetNPCsByStatus(RE::StaticFunctionTag*, std::string statusType) {
        const bool anyStatus =
            Foundation::StringUtils::CaseInsensitiveEquals(statusType, "any") ||
            Foundation::StringUtils::CaseInsensitiveEquals(statusType, "all");
        if (anyStatus) {
            std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
            return ResolveActorsById(App::RelationshipUseCases::GetSingleton().GetAllRegisteredNPCs());
        }

        const auto parsed = Foundation::StringToRelationshipStatus(statusType);
        if (parsed == Foundation::RelationshipStatus::Unknown) {
            MARAS_LOG_WARN("GetNPCsByStatus: unknown statusType '{}'", statusType);
            return {};
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorsById(App::RelationshipUseCases::GetSingleton().GetPartners(kPlayerFormID, parsed));
    }

    std::vector<RE::Actor*> GetNPCsByStatusEnum(RE::StaticFunctionTag*, int32_t statusEnum) {
        if (!IsValidRelationshipStatusInt(statusEnum)) {
            return {};
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorsById(App::RelationshipUseCases::GetSingleton().GetPartners(
            kPlayerFormID, static_cast<Foundation::RelationshipStatus>(statusEnum)));
    }

    std::vector<RE::Actor*> GetNPCsByStatusWith(RE::StaticFunctionTag*, RE::Actor* target, std::string statusType) {
        const bool anyStatus =
            Foundation::StringUtils::CaseInsensitiveEquals(statusType, "any") ||
            Foundation::StringUtils::CaseInsensitiveEquals(statusType, "all");
        if (anyStatus) {
            std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
            const RE::FormID targetID = target ? target->GetFormID() : kPlayerFormID;
            return ResolveActorsById(App::RelationshipUseCases::GetSingleton().GetAllPartners(targetID));
        }

        const auto parsed = Foundation::StringToRelationshipStatus(statusType);
        if (parsed == Foundation::RelationshipStatus::Unknown) {
            MARAS_LOG_WARN("GetNPCsByStatusWith: unknown statusType '{}'", statusType);
            return {};
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID targetID = target ? target->GetFormID() : kPlayerFormID;
        return ResolveActorsById(App::RelationshipUseCases::GetSingleton().GetPartners(targetID, parsed));
    }

    std::vector<RE::Actor*> GetRelationshipPartners(RE::StaticFunctionTag*, RE::Actor* npc, std::string statusType) {
        if (!npc) {
            return {};
        }

        const bool anyStatus =
            Foundation::StringUtils::CaseInsensitiveEquals(statusType, "any") ||
            Foundation::StringUtils::CaseInsensitiveEquals(statusType, "all");
        if (anyStatus) {
            std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
            return ResolveActorsById(App::RelationshipUseCases::GetSingleton().GetAllPartners(npc->GetFormID()));
        }

        const auto parsed = Foundation::StringToRelationshipStatus(statusType);
        if (parsed == Foundation::RelationshipStatus::Unknown) {
            MARAS_LOG_WARN("GetRelationshipPartners: unknown statusType '{}'", statusType);
            return {};
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorsById(App::RelationshipUseCases::GetSingleton().GetPartners(npc->GetFormID(), parsed));
    }

    std::vector<RE::Actor*> GetCurrentTeammates(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto teammateIds = PollingService::GetSingleton().GetCurrentTeammates();
        std::vector<RE::FormID> ordered(teammateIds.begin(), teammateIds.end());
        std::sort(ordered.begin(), ordered.end());
        return ResolveActorsById(ordered);
    }

    bool IsPlayerFollower(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return Utils::IsPlayerFollower(npc);
    }

    // ========================================
    // Statistics & Status History
    // ========================================

    int32_t GetStatusCount(RE::StaticFunctionTag*, std::string statusType) {
        const auto parsed = Foundation::StringToRelationshipStatus(statusType);
        if (parsed == Foundation::RelationshipStatus::Unknown) {
            return 0;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(
            App::RelationshipUseCases::GetSingleton().GetPartners(kPlayerFormID, parsed).size());
    }

    int32_t GetStatusCountByEnum(RE::StaticFunctionTag*, int32_t statusEnum) {
        if (!IsValidRelationshipStatusInt(statusEnum)) {
            return 0;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(App::RelationshipUseCases::GetSingleton()
                                        .GetPartners(kPlayerFormID, static_cast<Foundation::RelationshipStatus>(statusEnum))
                                        .size());
    }

    int32_t GetStatusHistoryCount(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target) {
        if (!npc) {
            return 0;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID targetID = target ? target->GetFormID() : kPlayerFormID;
        return static_cast<int32_t>(
            App::RelationshipUseCases::GetSingleton().GetStatusHistoryCount(npc->GetFormID(), targetID));
    }

    std::string GetStatusHistoryEntry(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target, int32_t index) {
        if (!npc || index < 0) {
            return "";
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID targetID = target ? target->GetFormID() : kPlayerFormID;
        const auto entryOpt = App::RelationshipUseCases::GetSingleton().GetStatusHistoryEntry(
            npc->GetFormID(), targetID, static_cast<std::size_t>(index));
        if (!entryOpt.has_value()) {
            return "";
        }

        return std::format(
            "{}|{}|{:.2f}|{:08X}",
            Foundation::RelationshipStatusToString(entryOpt->fromStatus),
            Foundation::RelationshipStatusToString(entryOpt->toStatus),
            entryOpt->timestamp,
            entryOpt->initiatorFormID);
    }

    // ========================================
    // NPC Types / Traits / Determination
    // ========================================

    std::vector<std::string> GetNpcTypes(RE::StaticFunctionTag*, std::string type) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const std::string lowerType = Foundation::StringUtils::ToLowerStd(type);

        std::vector<std::string> result;
        if (lowerType == "socialclass" || lowerType == "social") {
            constexpr auto count = static_cast<std::uint8_t>(Foundation::SocialClass::_Count);
            result.reserve(count);
            for (std::uint8_t i = 0; i < count; ++i) {
                result.emplace_back(Foundation::SocialClassToString(static_cast<Foundation::SocialClass>(i)));
            }
        } else if (lowerType == "skilltype" || lowerType == "skill") {
            constexpr auto count = static_cast<std::uint8_t>(Foundation::SkillType::_Count);
            result.reserve(count);
            for (std::uint8_t i = 0; i < count; ++i) {
                result.emplace_back(Foundation::SkillTypeToString(static_cast<Foundation::SkillType>(i)));
            }
        } else if (lowerType == "temperament") {
            constexpr auto count = static_cast<std::uint8_t>(Foundation::Temperament::_Count);
            result.reserve(count);
            for (std::uint8_t i = 0; i < count; ++i) {
                result.emplace_back(Foundation::TemperamentToString(static_cast<Foundation::Temperament>(i)));
            }
        }
        return result;
    }

    int32_t GetNpcTypeEnum(RE::StaticFunctionTag*, std::string type, std::string name) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const std::string lowerType = Foundation::StringUtils::ToLowerStd(type);
        const std::string lowerName = Foundation::StringUtils::ToLowerStd(name);

        if (lowerType == "socialclass" || lowerType == "social") {
            constexpr auto count = static_cast<std::uint8_t>(Foundation::SocialClass::_Count);
            for (std::uint8_t i = 0; i < count; ++i) {
                if (Foundation::StringUtils::CaseInsensitiveEquals(
                        Foundation::SocialClassToString(static_cast<Foundation::SocialClass>(i)), lowerName)) {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

        if (lowerType == "skilltype" || lowerType == "skill") {
            constexpr auto count = static_cast<std::uint8_t>(Foundation::SkillType::_Count);
            for (std::uint8_t i = 0; i < count; ++i) {
                if (Foundation::StringUtils::CaseInsensitiveEquals(
                        Foundation::SkillTypeToString(static_cast<Foundation::SkillType>(i)), lowerName)) {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

        if (lowerType == "temperament") {
            constexpr auto count = static_cast<std::uint8_t>(Foundation::Temperament::_Count);
            for (std::uint8_t i = 0; i < count; ++i) {
                if (Foundation::StringUtils::CaseInsensitiveEquals(
                        Foundation::TemperamentToString(static_cast<Foundation::Temperament>(i)), lowerName)) {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

        return -1;
    }

    int32_t GetNpcCurrentTypeEnum(RE::StaticFunctionTag*, RE::Actor* npc, std::string type) {
        if (!npc) {
            return -1;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto& relUseCases = App::RelationshipUseCases::GetSingleton();
        const std::string lowerType = Foundation::StringUtils::ToLowerStd(type);

        if (lowerType == "socialclass" || lowerType == "social") {
            return static_cast<int32_t>(relUseCases.GetSocialClass(npc->GetFormID()));
        }
        if (lowerType == "skilltype" || lowerType == "skill") {
            return static_cast<int32_t>(relUseCases.GetSkillType(npc->GetFormID()));
        }
        if (lowerType == "temperament") {
            return static_cast<int32_t>(relUseCases.GetTemperament(npc->GetFormID()));
        }
        return -1;
    }

    std::string GetNpcCurrentTypeName(RE::StaticFunctionTag*, RE::Actor* npc, std::string type) {
        if (!npc) {
            return "";
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto& relUseCases = App::RelationshipUseCases::GetSingleton();
        const std::string lowerType = Foundation::StringUtils::ToLowerStd(type);

        if (lowerType == "socialclass" || lowerType == "social") {
            return std::string(Foundation::SocialClassToString(relUseCases.GetSocialClass(npc->GetFormID())));
        }
        if (lowerType == "skilltype" || lowerType == "skill") {
            return std::string(Foundation::SkillTypeToString(relUseCases.GetSkillType(npc->GetFormID())));
        }
        if (lowerType == "temperament") {
            return std::string(Foundation::TemperamentToString(relUseCases.GetTemperament(npc->GetFormID())));
        }
        return "";
    }

    int32_t DetermineSocialClass(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            return static_cast<int32_t>(Foundation::SocialClass::Outcast);
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(
            App::RelationshipUseCases::GetSingleton().DetermineSocialClass(npc->GetFormID()));
    }

    int32_t DetermineSkillType(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            return static_cast<int32_t>(Foundation::SkillType::Warrior);
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(
            App::RelationshipUseCases::GetSingleton().DetermineSkillType(npc->GetFormID()));
    }

    int32_t DetermineTemperament(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            return static_cast<int32_t>(Foundation::Temperament::Proud);
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(
            App::RelationshipUseCases::GetSingleton().DetermineTemperament(npc->GetFormID()));
    }

    // ========================================
    // Spouse Hierarchy
    // ========================================

    bool SetHierarchyRank(RE::StaticFunctionTag*, RE::Actor* npc, int32_t rank) {
        if (!npc) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().SetSpouseRank(npc->GetFormID(), rank);
    }

    int32_t GetHierarchyRank(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            return -1;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().GetSpouseRank(npc->GetFormID());
    }

    // ========================================
    // Relationship Interactions & Cooldowns
    // ========================================

    void ShareSpouseIncome(RE::StaticFunctionTag*, RE::Actor* spouse) {
        if (!spouse) {
            return;
        }
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::RelationshipInteractionCoordinator::GetSingleton().ShareSpouseIncome(spouse->GetFormID());
    }

    void ProcessDivorceSettlement(RE::StaticFunctionTag*, RE::Actor* exSpouse, RE::TESQuest* divorceQuest) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID spouseID = exSpouse ? exSpouse->GetFormID() : 0;
        App::RelationshipInteractionCoordinator::GetSingleton().ProcessDivorceSettlement(spouseID, divorceQuest);
    }

    float GetBrokeupTime(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            return -1.0f;
        }
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipInteractionCoordinator::GetSingleton().GetBrokeupTime(npc->GetFormID());
    }

    bool GetPlayerKiller(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipInteractionCoordinator::GetSingleton().GetPlayerKiller();
    }

    void SetPlayerKiller(RE::StaticFunctionTag*, bool value) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::RelationshipInteractionCoordinator::GetSingleton().SetPlayerKiller(value);
    }

    bool GetPlayerHadWedding(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipInteractionCoordinator::GetSingleton().GetPlayerHadWedding();
    }

    void SetPlayerHadWedding(RE::StaticFunctionTag*, bool value) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::RelationshipInteractionCoordinator::GetSingleton().SetPlayerHadWedding(value);
    }

    bool ShouldPromptSkipWedding(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipInteractionCoordinator::GetSingleton().ShouldPromptSkipWedding();
    }

    void SetDontShowSkipWeddingMessage(RE::StaticFunctionTag*, bool value) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::RelationshipInteractionCoordinator::GetSingleton().SetDontShowSkipWeddingMessage(value);
    }

    void NotifyEngagementCeremonyStarted(RE::StaticFunctionTag*, RE::Actor* fiance) {
        if (!fiance) {
            return;
        }
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::RelationshipInteractionCoordinator::GetSingleton().NotifyEngagementCeremonyStarted(fiance->GetFormID());
    }

    void ApplyDialogueLoversLedgerBonus(
        RE::StaticFunctionTag*,
        RE::Actor* npc,
        int32_t exclusiveSex,
        int32_t groupSex,
        int32_t playerTopLoverRank) {
        if (!npc) {
            return;
        }
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::RelationshipInteractionCoordinator::GetSingleton().ApplyDialogueLoversLedgerBonus(
            npc->GetFormID(),
            static_cast<int>(exclusiveSex),
            static_cast<int>(groupSex),
            static_cast<int>(playerTopLoverRank));
    }

    void PacifyFianceOrSpouse(RE::StaticFunctionTag*, RE::Actor* npc, bool enable) {
        if (!npc) {
            return;
        }
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        RelationshipEngineReconciler::GetSingleton().PacifyFianceOrSpouse(npc->GetFormID(), enable);
    }

    void SeedLegacyUpgradeState(
        RE::StaticFunctionTag*,
        float lastPaidDivorceFee,
        int32_t childPoorCounter,
        std::vector<RE::Actor*> npcs,
        std::vector<float> brokeupTimes,
        std::vector<float> sharedIncomeTimes) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (lastPaidDivorceFee >= 0.0f && ws.cooldowns->lastTimePaidDivorceFee < 0.0f) {
                ws.cooldowns.Write().lastTimePaidDivorceFee = lastPaidDivorceFee;
            }
            if (childPoorCounter > 0 && ws.household->GetPoorCounter() == 0) {
                ws.household.Write().SetPoorCounter(childPoorCounter);
            }
            for (std::size_t i = 0; i < npcs.size(); ++i) {
                auto* npc = npcs[i];
                if (!npc) {
                    continue;
                }
                const State::ActorId aid{ npc->GetFormID() };
                if (aid.IsNull()) {
                    continue;
                }
                if (i < brokeupTimes.size() && brokeupTimes[i] >= 0.0f &&
                    ws.cooldowns->GetBrokeupTime(aid) < 0.0f) {
                    ws.cooldowns.Write().SetBrokeupTime(aid, brokeupTimes[i]);
                }
                if (i < sharedIncomeTimes.size() && sharedIncomeTimes[i] >= 0.0f &&
                    ws.cooldowns->GetLastTimeSharedIncome(aid) < 0.0f) {
                    ws.cooldowns.Write().SetLastTimeSharedIncome(aid, sharedIncomeTimes[i]);
                }
            }
        });
    }

    // ========================================
    // Buffs & Multipliers
    // ========================================

    float GetSpouseMultiplier(RE::StaticFunctionTag*, RE::Actor* spouse) {
        if (!spouse) {
            return 0.0f;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return Domain::Household::ComputeSpouseMultiplier(*snap, Foundation::ActorId{ spouse->GetFormID() });
    }

    std::vector<float> GetFollowersMultipliers(RE::StaticFunctionTag*, std::vector<RE::Actor*> followers) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        std::vector<Foundation::RefId> followerIds;
        followerIds.reserve(followers.size());
        for (const auto* actor : followers) {
            if (actor) {
                followerIds.emplace_back(actor->GetFormID());
            }
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto arr = Domain::Household::ComputeFollowerMultipliers(*snap, followerIds);
        return std::vector<float>(arr.begin(), arr.end());
    }

    std::vector<float> GetPermanentMultipliers(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto arr = Domain::Household::ComputePermanentMultipliers(*snap);
        return std::vector<float>(arr.begin(), arr.end());
    }

    int32_t GetBonusCount(RE::StaticFunctionTag*, std::string type) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(Engine::BonusesConfigLoader::GetSingleton().GetBonusCount(type));
    }

    RE::BGSPerk* GetBonusPerk(RE::StaticFunctionTag*, std::string type, int32_t index) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto* entry = LookupBonusEntry(type, index);
        if (!entry || entry->perk.empty()) {
            return nullptr;
        }
        const auto resolvedId = Utils::ParseAndResolveFormKey(entry->perk);
        if (!resolvedId.has_value() || *resolvedId == 0) {
            return nullptr;
        }
        return RE::TESForm::LookupByID<RE::BGSPerk>(*resolvedId);
    }

    int32_t GetBonusEffectIndex(RE::StaticFunctionTag*, std::string type, int32_t index) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto* entry = LookupBonusEntry(type, index);
        return entry ? entry->effectIndex : -1;
    }

    float GetBonusPerkValue(RE::StaticFunctionTag*, std::string type, int32_t index) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto* entry = LookupBonusEntry(type, index);
        return entry ? entry->value : 0.0f;
    }

    std::string GetBonusPerkType(RE::StaticFunctionTag*, std::string type, int32_t index) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto* entry = LookupBonusEntry(type, index);
        return entry ? entry->type : "";
    }

    std::string GetBonusPerkUnit(RE::StaticFunctionTag*, std::string type, int32_t index) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto* entry = LookupBonusEntry(type, index);
        return entry ? entry->unit : "";
    }

    std::string GetBonusPerkDescription(RE::StaticFunctionTag*, std::string type, int32_t index) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto* entry = LookupBonusEntry(type, index);
        return entry ? entry->description : "";
    }

    // ========================================
    // Affection Progression
    // ========================================

    void AddAffection(RE::StaticFunctionTag*, RE::Actor* npc, float amount, std::string type) {
        if (!npc) {
            return;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::RelationshipUseCases::GetSingleton().AddAffection(npc->GetFormID(), amount, type);
    }

    float GetAffection(RE::StaticFunctionTag*, RE::Actor* npc, std::string type) {
        if (!npc) {
            return 0.0f;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().GetDailyAffection(npc->GetFormID(), type);
    }

    int32_t GetPermanentAffection(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            return 0;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().GetPermanentAffection(npc->GetFormID());
    }

    void SetPermanentAffection(RE::StaticFunctionTag*, RE::Actor* npc, int32_t amount, bool sendEvent) {
        if (!npc) {
            return;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::RelationshipUseCases::GetSingleton().SetPermanentAffection(npc->GetFormID(), amount, sendEvent);
    }

    void SetAffectionMinMax(RE::StaticFunctionTag*, std::string type, int32_t minVal, int32_t maxVal) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::RelationshipUseCases::GetSingleton().SetAffectionMinMax(type, minVal, maxVal);
    }

    void ApplyDailyAffection(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::RelationshipUseCases::GetSingleton().ApplyDailyAffectionsForAll();
    }

    void SetAffectionDecayMultiplier(RE::StaticFunctionTag*, float multiplier) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::RelationshipUseCases::GetSingleton().SetAffectionDecayMultiplier(multiplier);
    }

    float GetAffectionDecayMultiplier(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().GetAffectionDecayMultiplier();
    }

}  // namespace MARAS::PapyrusInterface
