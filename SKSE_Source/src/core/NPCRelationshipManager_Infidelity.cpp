#include "core/NPCRelationshipManager.h"

#include "core/AffectionService.h"
#include "core/FormCache.h"
#include "core/NPCIndex.h"
#include "core/SkyrimNetMemoryService.h"
#include "core/WorldStateLegacyAdapters.h"
#include "maras/state/WorldStore.h"
#include "services/ActorService.h"
#include "ui/PrismaUIDataBridge.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"
#include "utils/Settings.h"

namespace MARAS {

    namespace {

        bool IsPlayerActiveMainPartnerInSnapshot(const State::WorldStateSnapshot& snap, RE::FormID actorID) {
            if (actorID == 0 || actorID == kPlayerFormID) return false;
            const auto status = static_cast<RelationshipStatus>(
                snap.relationships->GetStatus(Foundation::ActorId{ actorID }, Foundation::ActorId::Player()));
            return NPCRelationshipManager::IsMainTier(status);
        }

        void DispatchPartnerIntentChangedEvent(RE::FormID partnerID, PartnerIntentType type) {
            if (partnerID == 0 || partnerID == kPlayerFormID) return;
            if (auto* eventSource = SKSE::GetModCallbackEventSource()) {
                std::string intentStr = (type == PartnerIntentType::None)
                    ? std::string("none")
                    : std::string(Utils::PartnerIntentTypeToString(type));
                RE::Actor* partnerActor = NPCIndex::GetSingleton().GetActorFromFormId(partnerID);
                RE::TESForm* senderForm = partnerActor ? static_cast<RE::TESForm*>(partnerActor) : nullptr;
                SKSE::ModCallbackEvent modEvent(
                    "maras_partner_intent_changed",
                    intentStr,
                    static_cast<float>(static_cast<uint8_t>(type)),
                    senderForm);
                eventSource->SendEvent(&modEvent);
            }
        }

    }  // namespace

    bool NPCRelationshipManager::IsNPCInOpenRelationship(RE::FormID actorID) const {
        if (actorID == 0) return false;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->relationships->IsInAnyOpenRelationship(Foundation::ActorId{ actorID });
    }

    bool NPCRelationshipManager::GetPairIsOpen(RE::FormID actorA, RE::FormID actorB) const {
        if (actorA == 0 || actorB == 0) return false;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->relationships->IsPairOpen(Foundation::ActorId{ actorA }, Foundation::ActorId{ actorB });
    }

    bool NPCRelationshipManager::SetPairIsOpen(RE::FormID actorA, RE::FormID actorB, bool isOpen, bool sendEvent) {
        if (!EnsureRegistered(actorA) || !EnsureRegistered(actorB)) return false;

        auto* calendar = RE::Calendar::GetSingleton();
        const float currentDay = calendar ? calendar->GetDaysPassed() : 0.0f;

        struct BrokenLoverPair {
            RE::FormID memberID{ 0 };
            RE::FormID loverID{ 0 };
        };
        bool wasOpen = false;
        RelationshipStatus pairStatus = RelationshipStatus::Candidate;
        std::vector<BrokenLoverPair> brokenLovers;
        std::vector<RE::FormID> clearedIntentActors;
        std::unordered_set<RE::FormID> allowedCheatersToRefresh;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            const Foundation::ActorId idA{ actorA };
            const Foundation::ActorId idB{ actorB };

            if (const auto* existingEdge = ws.relationships->FindEdge(idA, idB)) {
                wasOpen = existingEdge->isOpen;
            }

            std::vector<BrokenLoverPair> candidateLovers;
            std::unordered_set<RE::FormID> candidateIntentHolders{ actorA, actorB };
            if (!isOpen) {
                for (RE::FormID memberID : { actorA, actorB }) {
                    if (memberID == 0) continue;
                    for (const Foundation::ActorId lover :
                         ws.relationships->GetPartnersWithStatus(Foundation::ActorId{ memberID }, State::RelationshipStatus::Lover)) {
                        candidateLovers.push_back({ memberID, lover.raw() });
                        candidateIntentHolders.insert(lover.raw());
                    }
                }
            } else {
                for (const auto& rec : ws.affairs->GetForCouplePair(Foundation::CanonicalPairKey{ idA, idB })) {
                    if (rec.affairStatus == State::AffairStatus::Ongoing && rec.deceivedPartner.IsPlayer()) {
                        allowedCheatersToRefresh.insert(rec.cheater.raw());
                    }
                }
            }

            std::vector<RE::FormID> activeIntentBefore;
            for (RE::FormID holder : candidateIntentHolders) {
                if (holder != 0 && holder != kPlayerFormID &&
                    ws.relationships->GetPendingIntent(Foundation::ActorId{ holder }).type != State::PartnerIntentType::None) {
                    activeIntentBefore.push_back(holder);
                }
            }

            (void)ws.SetPairOpen(idA, idB, isOpen, currentDay);

            if (const auto* updatedEdge = ws.relationships->FindEdge(idA, idB)) {
                pairStatus = static_cast<RelationshipStatus>(updatedEdge->status);
            }

            for (const auto& cand : candidateLovers) {
                const auto st = ws.relationships->GetStatus(Foundation::ActorId{ cand.memberID }, Foundation::ActorId{ cand.loverID });
                if (st == State::RelationshipStatus::ExLover) {
                    brokenLovers.push_back(cand);
                }
            }

            for (RE::FormID holder : activeIntentBefore) {
                if (ws.relationships->GetPendingIntent(Foundation::ActorId{ holder }).type == State::PartnerIntentType::None) {
                    clearedIntentActors.push_back(holder);
                }
            }
        });

        for (RE::FormID clearedActor : clearedIntentActors) {
            MARAS_LOG_INFO("ClearPartnerIntent: Cleared pending intent for partner {:08X} ({})",
                           clearedActor, ActorSvc::GetName(clearedActor));
            DispatchPartnerIntentChangedEvent(clearedActor, PartnerIntentType::None);
        }

        if (!brokenLovers.empty()) {
            const bool allowMemory = (sendEvent || SkyrimNetMemoryService::IsMatchmakingApplyActive());
            for (const auto& broken : brokenLovers) {
                if (broken.memberID == kPlayerFormID || broken.loverID == kPlayerFormID) {
                    const RE::FormID npcID = (broken.memberID == kPlayerFormID) ? broken.loverID : broken.memberID;
                    if (npcID != 0) {
                        UpdateTrackedFactionRank(npcID, RelationshipStatus::ExLover);
                        UpdateSpouseCheatingFaction(npcID);
                        UpdateSpouseOpenRelationshipFaction(npcID);
                        UpdateCanFormNonCheatingMainFaction(npcID);
                        UpdateCanFormNonCheatingMainFaction(kPlayerFormID);
                        ManageFactions(npcID, kPlayerFormID, RelationshipStatus::ExLover);

                        RE::Actor* partnerActor = RE::TESForm::LookupByID<RE::Actor>(npcID);
                        if (!partnerActor) {
                            partnerActor = NPCIndex::GetSingleton().GetActorFromFormId(npcID);
                        }
                        if (!sendEvent || !partnerActor) {
                            AffectionService::GetSingleton().SetPermanentAffection(npcID, 0, /*sendEvent=*/false);
                        }
                    }
                } else {
                    UpdateCanFormNonCheatingMainFaction(broken.memberID);
                    UpdateCanFormNonCheatingMainFaction(broken.loverID);
                    ManageFactions(broken.memberID, broken.loverID, RelationshipStatus::ExLover);
                    ManageFactions(broken.loverID, broken.memberID, RelationshipStatus::ExLover);
                }

                if (sendEvent) {
                    SendStatusChangedEvent(broken.memberID, broken.loverID, RelationshipStatus::ExLover);
                }
                if (allowMemory) {
                    SkyrimNetMemoryService::GetSingleton().RecordRelationshipStatusChange(
                        broken.memberID, broken.loverID, RelationshipStatus::Lover, RelationshipStatus::ExLover);
                }
                PrismaUIDataBridge::GetSingleton().OnStatusChanged(
                    broken.memberID, broken.loverID, static_cast<uint8_t>(RelationshipStatus::ExLover));
            }
            RecalculateAndUpdateGlobals();
        }

        for (RE::FormID cheaterID : allowedCheatersToRefresh) {
            UpdateSpouseCheatingFaction(cheaterID);
        }
        if (actorA == kPlayerFormID) {
            UpdateSpouseOpenRelationshipFaction(actorB);
            UpdateSpouseCheatingFaction(actorB);
        } else if (actorB == kPlayerFormID) {
            UpdateSpouseOpenRelationshipFaction(actorA);
            UpdateSpouseCheatingFaction(actorA);
        }
        UpdateCanFormNonCheatingMainFaction(actorA);
        UpdateCanFormNonCheatingMainFaction(actorB);
        PrismaUIDataBridge::GetSingleton().OnStatusChanged(actorA, actorB, static_cast<uint8_t>(pairStatus));
        if (wasOpen != isOpen && (sendEvent || SkyrimNetMemoryService::IsMatchmakingApplyActive())) {
            SkyrimNetMemoryService::GetSingleton().RecordOpenRelationshipToggle(actorA, actorB, isOpen);
        }
        if (!sendEvent) {
            return true;
        }

        if (auto* eventSource = SKSE::GetModCallbackEventSource()) {
            nlohmann::json jsonPayload;
            jsonPayload["actorA"] = fmt::format("0x{:08X}", actorA);
            jsonPayload["actorB"] = fmt::format("0x{:08X}", actorB);
            jsonPayload["actorAName"] = ActorSvc::GetName(actorA);
            jsonPayload["actorBName"] = ActorSvc::GetName(actorB);
            jsonPayload["isOpen"] = isOpen;

            std::string jsonStr = jsonPayload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            RE::Actor* senderActor = NPCIndex::GetSingleton().GetActorFromFormId(actorA);
            RE::TESForm* senderForm = senderActor ? static_cast<RE::TESForm*>(senderActor) : nullptr;
            SKSE::ModCallbackEvent modEvent("maras_open_relationship_changed", jsonStr, 0.0f, senderForm);
            eventSource->SendEvent(&modEvent);
            MARAS_LOG_INFO("Sent maras_open_relationship_changed event: {}", jsonStr);
        } else {
            MARAS_LOG_ERROR("Could not get ModCallbackEventSource to send maras_open_relationship_changed event");
        }

        return true;
    }

    bool NPCRelationshipManager::RecordCheating(RE::FormID cheaterID, RE::FormID victimID, RE::FormID affairPartnerID, bool isSecret,
                                                AffairType affairType, AffairStatus affairStatus,
                                                AffairStage affairStage) {
        if (!EnsureRegistered(cheaterID) || !EnsureRegistered(victimID) || !EnsureRegistered(affairPartnerID)) {
            return false;
        }

        if (cheaterID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) {
            MARAS_LOG_INFO("RecordCheating ignored for player {:08X}: player polygamy is enabled", cheaterID);
            return false;
        }

        if (victimID == kPlayerFormID && IsPlayerActiveMainPartner(cheaterID) && IsPlayerActiveMainPartner(affairPartnerID)) {
            MARAS_LOG_INFO("RecordCheating ignored for co-spouses {:08X} and {:08X}: co-spouse intimacy is not cheating on player",
                           cheaterID, affairPartnerID);
            return false;
        }

        // Dual-stage affair applies strictly to Player's partners cheating on the Player;
        // when the Player is not the victim, default to Physical (sex) stage right away.
        if (victimID != kPlayerFormID) {
            affairStage = AffairStage::Physical;
        }

        auto calendar = RE::Calendar::GetSingleton();
        float currentFloatDay = calendar ? calendar->GetDaysPassed() : 0.0f;

        bool recorded = false;
        bool intentUpgraded = false;
        State::AffairStage recordedStage = static_cast<State::AffairStage>(affairStage);

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            const Foundation::ActorId cheater{ cheaterID };
            const auto intentBefore = ws.relationships->GetPendingIntent(cheater);

            recorded = ws.RecordCheatingIncident(
                cheater,
                Foundation::ActorId{ victimID },
                Foundation::ActorId{ affairPartnerID },
                isSecret,
                static_cast<State::AffairType>(affairType),
                static_cast<State::AffairStatus>(affairStatus),
                static_cast<State::AffairStage>(affairStage),
                currentFloatDay);

            if (recorded) {
                const auto intentAfter = ws.relationships->GetPendingIntent(cheater);
                if (intentBefore.type == State::PartnerIntentType::ConfessTalking &&
                    intentAfter.type == State::PartnerIntentType::ConfessPhysical) {
                    intentUpgraded = true;
                }
                if (const auto* rec = ws.affairs->FindTriple(
                        cheater, Foundation::ActorId{ victimID }, Foundation::ActorId{ affairPartnerID })) {
                    recordedStage = rec->affairStage;
                }
            }
        });

        if (!recorded) {
            return false;
        }

        if (intentUpgraded) {
            MARAS_LOG_INFO("RecordCheating: Auto-upgrading PendingPartnerIntent from ConfessTalking to ConfessPhysical for cheater {:08X} with lover {:08X}",
                           cheaterID, affairPartnerID);
            MARAS_LOG_INFO("SetPartnerPendingIntent: Partner {:08X} ({}) -> {} (lover {:08X}, day {:.2f})",
                           cheaterID, ActorSvc::GetName(cheaterID),
                           Utils::PartnerIntentTypeToString(PartnerIntentType::ConfessPhysical),
                           affairPartnerID, currentFloatDay);
            DispatchPartnerIntentChangedEvent(cheaterID, PartnerIntentType::ConfessPhysical);
        }

        MARAS_LOG_INFO("Recorded cheating event: {:08X} cheated on {:08X} with {:08X} (isSecret: {}, type: {}, status: {}, stage: {})",
                       cheaterID, victimID, affairPartnerID, isSecret,
                       Utils::AffairTypeToString(affairType), Utils::AffairStatusToString(affairStatus),
                       Utils::AffairStageToString(static_cast<AffairStage>(recordedStage)));
        if (victimID == kPlayerFormID) {
            UpdateSpouseCheatingFaction(cheaterID);
        }
        return true;
    }

    bool NPCRelationshipManager::HasCheatedOn(RE::FormID cheaterID, RE::FormID victimID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->affairs->HasCheatedOn(Foundation::ActorId{ cheaterID }, Foundation::ActorId{ victimID });
    }

    bool NPCRelationshipManager::IsNPCCheating(RE::FormID actorID) const {
        if (actorID == 0) return false;

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const Foundation::ActorId actor{ actorID };

        // Player polygamy exemption
        if (actorID == kPlayerFormID && snap->settings->enablePlayerPolygamy) {
            return false;
        }

        // 1. Check if actorID is in any closed main-tier relationship (Married, Engaged, Dating)
        // and has an active relationship with someone else, or has multiple closed main-tier relationships
        std::vector<RE::FormID> closedMainPartners;
        for (const auto& key : snap->relationships->GetAdjacentPairs(actor)) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (edge && IsMainTier(static_cast<RelationshipStatus>(edge->status)) && !edge->isOpen) {
                closedMainPartners.push_back(key.Other(actor).raw());
            }
        }

        if (closedMainPartners.size() > 1) {
            return true;
        }

        if (!closedMainPartners.empty()) {
            const bool isActorPlayerPartner = IsPlayerActiveMainPartnerInSnapshot(*snap, actorID);
            for (const auto& [partnerId, st] : snap->relationships->GetActivePartners(actor)) {
                (void)st;
                const RE::FormID partnerID = partnerId.raw();
                if (partnerID == 0 || partnerID == actorID) continue;
                if (std::find(closedMainPartners.begin(), closedMainPartners.end(), partnerID) == closedMainPartners.end()) {
                    // Co-spouse exemption: a secondary bond with another active main partner of the Player
                    // is not cheating on the Player
                    if (isActorPlayerPartner && IsPlayerActiveMainPartnerInSnapshot(*snap, partnerID) &&
                        closedMainPartners.size() == 1 && closedMainPartners.front() == kPlayerFormID) {
                        continue;
                    }
                    return true;
                }
            }
        }

        // 2. Check if actorID is specifically the cheater in an ongoing affair record
        for (const auto& rec : snap->affairs->GetByCheater(actor)) {
            if (rec.affairStatus == State::AffairStatus::Ongoing) {
                return true;
            }
        }

        return false;
    }

    bool NPCRelationshipManager::IsRelationshipCheating(RE::FormID actorA, RE::FormID actorB) const {
        if (actorA == 0 || actorB == 0 || actorA == actorB) {
            return false;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const Foundation::ActorId idA{ actorA };
        const Foundation::ActorId idB{ actorB };
        const bool bothCoSpouses = IsPlayerActiveMainPartnerInSnapshot(*snap, actorA) && IsPlayerActiveMainPartnerInSnapshot(*snap, actorB);

        auto hasClosedMainWithOther = [&](Foundation::ActorId self, Foundation::ActorId other) -> bool {
            for (const auto& key : snap->relationships->GetAdjacentPairs(self)) {
                if (key.Contains(other)) continue;
                const auto* edge = snap->relationships->FindEdge(key);
                if (!edge || !IsMainTier(static_cast<RelationshipStatus>(edge->status)) || edge->isOpen) {
                    continue;
                }
                if (self.IsPlayer() && snap->settings->enablePlayerPolygamy) {
                    continue;
                }
                if (bothCoSpouses && key.Other(self).IsPlayer()) {
                    continue;
                }
                return true;
            }
            return false;
        };

        if (hasClosedMainWithOther(idA, idB) || hasClosedMainWithOther(idB, idA)) {
            return true;
        }

        // 3. Check if there are active or confronted cheating records involving actorA and actorB
        for (const auto& rec : snap->affairs->GetByCheater(idA)) {
            if (rec.paramour == idB &&
                (rec.affairStatus == State::AffairStatus::Ongoing ||
                 rec.affairStatus == State::AffairStatus::EndedConfronted)) {
                return true;
            }
        }
        for (const auto& rec : snap->affairs->GetByCheater(idB)) {
            if (rec.paramour == idA &&
                (rec.affairStatus == State::AffairStatus::Ongoing ||
                 rec.affairStatus == State::AffairStatus::EndedConfronted)) {
                return true;
            }
        }

        return false;
    }

    uint32_t NPCRelationshipManager::GetCheatingCount(RE::FormID cheaterID, RE::FormID victimID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        uint32_t total = 0;
        for (const auto& rec : snap->affairs->GetForCouplePair(
                 Foundation::CanonicalPairKey{ Foundation::ActorId{ cheaterID }, Foundation::ActorId{ victimID } })) {
            if (rec.cheater.raw() == cheaterID && rec.deceivedPartner.raw() == victimID) {
                total += rec.incidentCount;
            }
        }
        return total;
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetCheatedOnPartners(RE::FormID cheaterID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::unordered_set<RE::FormID> victims;
        for (const auto& rec : snap->affairs->GetByCheater(Foundation::ActorId{ cheaterID })) {
            victims.insert(rec.deceivedPartner.raw());
        }
        return std::vector<RE::FormID>(victims.begin(), victims.end());
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetCheatersWhoCheatedOn(RE::FormID victimID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::unordered_set<RE::FormID> cheaters;
        for (const auto& rec : snap->affairs->GetByDeceivedPartner(Foundation::ActorId{ victimID })) {
            cheaters.insert(rec.cheater.raw());
        }
        return std::vector<RE::FormID>(cheaters.begin(), cheaters.end());
    }

    std::vector<CheatingRecord> NPCRelationshipManager::GetCheatingRecords(RE::FormID actorA, RE::FormID actorB) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto affairs = snap->affairs->GetForCouplePair(
            Foundation::CanonicalPairKey{ Foundation::ActorId{ actorA }, Foundation::ActorId{ actorB } });
        std::vector<CheatingRecord> result;
        result.reserve(affairs.size());
        for (const auto& rec : affairs) {
            result.push_back(LegacyAdapters::ToLegacyCheatingRecord(rec));
        }
        return result;
    }

    std::vector<CheatingRecord> NPCRelationshipManager::GetCheatingRecordsForActor(RE::FormID actorID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto affairs = snap->affairs->GetAllInvolving(Foundation::ActorId{ actorID });
        std::vector<CheatingRecord> result;
        result.reserve(affairs.size());
        for (const auto& rec : affairs) {
            result.push_back(LegacyAdapters::ToLegacyCheatingRecord(rec));
        }
        return result;
    }

    bool NPCRelationshipManager::SetCheatingAffairStatus(RE::FormID cheaterID, RE::FormID victimID, RE::FormID affairPartnerID, AffairStatus status) {
        const bool updated = State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            return ws.affairs.Write().SetStatus(
                Foundation::ActorId{ cheaterID },
                Foundation::ActorId{ victimID },
                Foundation::ActorId{ affairPartnerID },
                static_cast<State::AffairStatus>(status));
        });
        if (!updated) return false;

        MARAS_LOG_INFO("Set cheating affair status for cheater {:08X} on victim {:08X} with {:08X} to {}",
                       cheaterID, victimID, affairPartnerID, Utils::AffairStatusToString(status));
        if (victimID == kPlayerFormID) {
            UpdateSpouseCheatingFaction(cheaterID);
        }
        return true;
    }

    AffairStatus NPCRelationshipManager::GetCheatingAffairStatus(RE::FormID cheaterID, RE::FormID victimID, RE::FormID affairPartnerID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->affairs->FindTriple(
            Foundation::ActorId{ cheaterID },
            Foundation::ActorId{ victimID },
            Foundation::ActorId{ affairPartnerID });
        return rec ? static_cast<AffairStatus>(rec->affairStatus) : AffairStatus::Ongoing;
    }

    bool NPCRelationshipManager::SetCheatingAffairStage(RE::FormID cheaterID, RE::FormID victimID, RE::FormID affairPartnerID, AffairStage stage) {
        if (victimID != kPlayerFormID) {
            stage = AffairStage::Physical;
        }

        auto* calendar = RE::Calendar::GetSingleton();
        float currentFloatDay = calendar ? calendar->GetDaysPassed() : 0.0f;

        std::vector<RE::FormID> updatedParamours;
        bool intentUpgraded = false;
        RE::FormID upgradedLoverID = 0;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            const Foundation::ActorId cheater{ cheaterID };
            const Foundation::ActorId victim{ victimID };
            const Foundation::ActorId paramourFilter{ affairPartnerID };
            const State::AffairStage targetStage = static_cast<State::AffairStage>(stage);

            const auto coupleRecords = ws.affairs->GetForCouplePair(Foundation::CanonicalPairKey{ cheater, victim });
            for (const auto& rec : coupleRecords) {
                if (rec.cheater != cheater || rec.deceivedPartner != victim) {
                    continue;
                }
                if (!paramourFilter.IsNull() && rec.paramour != paramourFilter) {
                    continue;
                }
                if (rec.affairStage != targetStage) {
                    (void)ws.affairs.Write().SetStage(cheater, victim, rec.paramour, targetStage, currentFloatDay);
                    if (targetStage == State::AffairStage::Physical) {
                        const auto existingIntent = ws.relationships->GetPendingIntent(cheater);
                        if (existingIntent.type == State::PartnerIntentType::ConfessTalking &&
                            (existingIntent.relatedLover.IsNull() || existingIntent.relatedLover == rec.paramour)) {
                            ws.relationships.Write().SetPendingIntent(
                                cheater,
                                State::PendingPartnerIntentRecord{
                                    .type = State::PartnerIntentType::ConfessPhysical,
                                    .createdGameDay = currentFloatDay,
                                    .relatedLover = rec.paramour,
                                });
                            intentUpgraded = true;
                            upgradedLoverID = rec.paramour.raw();
                        }
                    }
                }
                updatedParamours.push_back(rec.paramour.raw());
                if (!paramourFilter.IsNull()) {
                    break;
                }
            }
        });

        for (RE::FormID pID : updatedParamours) {
            MARAS_LOG_INFO("Set cheating affair stage for cheater {:08X} on victim {:08X} with {:08X} to {}",
                           cheaterID, victimID, pID, Utils::AffairStageToString(stage));
        }
        if (intentUpgraded) {
            MARAS_LOG_INFO("SetPartnerPendingIntent: Partner {:08X} ({}) -> {} (lover {:08X}, day {:.2f})",
                           cheaterID, ActorSvc::GetName(cheaterID),
                           Utils::PartnerIntentTypeToString(PartnerIntentType::ConfessPhysical),
                           upgradedLoverID, currentFloatDay);
            DispatchPartnerIntentChangedEvent(cheaterID, PartnerIntentType::ConfessPhysical);
        }
        if (updatedParamours.empty()) {
            MARAS_LOG_WARN("SetCheatingAffairStage: No matching cheating record found for cheater {:08X} on victim {:08X} with affairPartner {:08X}",
                           cheaterID, victimID, affairPartnerID);
            return false;
        }
        return true;
    }

    AffairStage NPCRelationshipManager::GetCheatingAffairStage(RE::FormID cheaterID, RE::FormID victimID, RE::FormID affairPartnerID) const {
        if (victimID != kPlayerFormID) {
            return AffairStage::Physical;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const Foundation::ActorId cheater{ cheaterID };
        const Foundation::ActorId victim{ victimID };
        for (const auto& rec : snap->affairs->GetForCouplePair(Foundation::CanonicalPairKey{ cheater, victim })) {
            if (rec.cheater == cheater && rec.deceivedPartner == victim &&
                (affairPartnerID == 0 || rec.paramour.raw() == affairPartnerID)) {
                if (affairPartnerID != 0 || rec.affairStatus == State::AffairStatus::Ongoing) {
                    return static_cast<AffairStage>(rec.affairStage);
                }
            }
        }
        return AffairStage::Physical;
    }

    void NPCRelationshipManager::SetPartnerPendingIntent(RE::FormID partnerID, PartnerIntentType type, RE::FormID relatedLoverID) {
        if (partnerID == 0 || partnerID == kPlayerFormID) return;
        if (type == PartnerIntentType::None) {
            ClearPartnerIntent(partnerID);
            return;
        }
        auto* calendar = RE::Calendar::GetSingleton();
        float currentDay = calendar ? calendar->GetDaysPassed() : 0.0f;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            ws.relationships.Write().SetPendingIntent(
                Foundation::ActorId{ partnerID },
                State::PendingPartnerIntentRecord{
                    .type = static_cast<State::PartnerIntentType>(type),
                    .createdGameDay = currentDay,
                    .relatedLover = Foundation::ActorId{ relatedLoverID },
                });
        });

        std::string intentStr(Utils::PartnerIntentTypeToString(type));
        MARAS_LOG_INFO("SetPartnerPendingIntent: Partner {:08X} ({}) -> {} (lover {:08X}, day {:.2f})",
                       partnerID, ActorSvc::GetName(partnerID), intentStr, relatedLoverID, currentDay);
        DispatchPartnerIntentChangedEvent(partnerID, type);
    }

    PendingPartnerIntent NPCRelationshipManager::GetPartnerPendingIntent(RE::FormID partnerID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return LegacyAdapters::ToLegacyPendingPartnerIntent(
            snap->relationships->GetPendingIntent(Foundation::ActorId{ partnerID }));
    }

    void NPCRelationshipManager::ClearPartnerIntent(RE::FormID partnerID) {
        if (partnerID == 0) return;

        const bool cleared = State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            return ws.relationships.Write().ClearPendingIntent(Foundation::ActorId{ partnerID });
        });
        if (!cleared) {
            MARAS_LOG_DEBUG("ClearPartnerIntent: Partner {:08X} ({}) had no pending intent to clear",
                            partnerID, ActorSvc::GetName(partnerID));
            return;
        }

        MARAS_LOG_INFO("ClearPartnerIntent: Cleared pending intent for partner {:08X} ({})",
                       partnerID, ActorSvc::GetName(partnerID));
        DispatchPartnerIntentChangedEvent(partnerID, PartnerIntentType::None);
    }

    void NPCRelationshipManager::MarkCheatingAllowedForPair(RE::FormID actorA, RE::FormID actorB) {
        struct AllowedLogEntry {
            RE::FormID cheaterID{ 0 };
            RE::FormID victimID{ 0 };
            RE::FormID paramourID{ 0 };
        };
        std::vector<AllowedLogEntry> allowedEntries;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            const Foundation::ActorId idA{ actorA };
            const Foundation::ActorId idB{ actorB };
            for (const auto& rec : ws.affairs->GetForCouplePair(Foundation::CanonicalPairKey{ idA, idB })) {
                if (rec.affairStatus == State::AffairStatus::Ongoing) {
                    allowedEntries.push_back({ rec.cheater.raw(), rec.deceivedPartner.raw(), rec.paramour.raw() });
                }
            }
            if (!allowedEntries.empty()) {
                ws.affairs.Write().MarkAllowedForPair(idA, idB);
            }
        });

        for (const auto& entry : allowedEntries) {
            MARAS_LOG_INFO("Marked cheating record as Allowed on pair {:08X} - {:08X} for cheater {:08X} / victim {:08X} with {:08X}",
                           actorA, actorB, entry.cheaterID, entry.victimID, entry.paramourID);
            if (entry.victimID == kPlayerFormID) {
                UpdateSpouseCheatingFaction(entry.cheaterID);
            }
        }
    }

    void NPCRelationshipManager::MarkPlayerCheatingAllowedOnPolygamy() {
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            const auto playerRecords = ws.affairs.Read().GetByCheater(State::ActorId::Player());
            for (const auto& rec : playerRecords) {
                if (rec.affairStatus == State::AffairStatus::Ongoing) {
                    ws.affairs.Write().SetStatus(
                        rec.cheater,
                        rec.deceivedPartner,
                        rec.paramour,
                        State::AffairStatus::Allowed);
                    MARAS_LOG_INFO("Marked player cheating record as Allowed due to polygamy: victim {:08X} with {:08X}",
                                   rec.deceivedPartner.raw(), rec.paramour.raw());
                }
            }
        });
    }

    void NPCRelationshipManager::CleanupActorIntentsAndThirdPartyAffairs(
        RE::FormID actorID,
        bool eraseRecords,
        std::unordered_set<RE::FormID>& outAffectedActors) {
        if (actorID == 0) {
            return;
        }

        const State::ActorId target{ actorID };
        struct ClearedIntentDelta {
            RE::FormID partnerID{ 0 };
            PartnerIntentType previousType{ PartnerIntentType::None };
        };
        std::vector<ClearedIntentDelta> clearedIntents;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            for (const auto& [partner, intent] : ws.relationships.Read().AllPendingIntents()) {
                if (partner == target || intent.relatedLover == target) {
                    clearedIntents.push_back({ partner.raw(), static_cast<PartnerIntentType>(intent.type) });
                }
            }
            ws.relationships.Write().ClearIntentsInvolving(target);

            if (eraseRecords) {
                const auto affected = ws.affairs.Write().EraseAllInvolvingActor(target);
                for (const auto& aff : affected) {
                    if (!aff.IsNull() && aff != target) {
                        outAffectedActors.insert(aff.raw());
                    }
                }
                if (!affected.empty()) {
                    MARAS_LOG_INFO("Cleaned cheating record(s) involving {:08X}", actorID);
                }
            } else {
                for (const auto& rec : ws.affairs.Read().GetAllInvolving(target)) {
                    if (rec.affairStatus == State::AffairStatus::Ongoing) {
                        MARAS_LOG_INFO("Marking third-party affair closed due to death of {:08X}: cheater {:08X} / victim {:08X} with {:08X}",
                                       actorID, rec.cheater.raw(), rec.deceivedPartner.raw(), rec.paramour.raw());
                    }
                    if (!rec.cheater.IsNull() && rec.cheater != target) outAffectedActors.insert(rec.cheater.raw());
                    if (!rec.deceivedPartner.IsNull() && rec.deceivedPartner != target) outAffectedActors.insert(rec.deceivedPartner.raw());
                    if (!rec.paramour.IsNull() && rec.paramour != target) outAffectedActors.insert(rec.paramour.raw());
                }
                const auto affected = ws.affairs.Write().CloseOngoingForDeceasedActor(target);
                for (const auto& aff : affected) {
                    if (!aff.IsNull() && aff != target) {
                        outAffectedActors.insert(aff.raw());
                    }
                }
            }
        });

        for (const auto& cleared : clearedIntents) {
            MARAS_LOG_INFO("Cleared pending partner intent for {:08X} (was {})",
                           cleared.partnerID, static_cast<int>(cleared.previousType));
            DispatchPartnerIntentChangedEvent(cleared.partnerID, PartnerIntentType::None);
        }
    }

    void NPCRelationshipManager::EvaluateCheatingOnStatusChange(RE::FormID npcFormID, RE::FormID targetFormID, RelationshipStatus newStatus) {
        if (npcFormID == 0) return;

        auto handleDatingOrLover = [this](RE::FormID cheaterID, RE::FormID affairPartnerID, AffairType aType) {
            if (cheaterID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) {
                return;
            }

            auto partnerIDs = GetAllPartners(cheaterID);
            for (auto pID : partnerIDs) {
                if (pID == affairPartnerID || pID == 0) continue;
                if (pID == kPlayerFormID && IsPlayerActiveMainPartner(cheaterID) && IsPlayerActiveMainPartner(affairPartnerID)) {
                    continue;
                }
                const auto rData = GetNPCData(cheaterID, pID);
                if (rData && IsMainTier(rData->status) && !rData->isOpen) {
                    AffairStage initialStage = (pID == kPlayerFormID) ? AffairStage::Talking : AffairStage::Physical;
                    RecordCheating(cheaterID, pID, affairPartnerID, /*isSecret=*/true, aType, AffairStatus::Ongoing, initialStage);
                }
            }
        };

        if (newStatus == RelationshipStatus::Dating || newStatus == RelationshipStatus::Lover) {
            AffairType aType = (newStatus == RelationshipStatus::Dating) ? AffairType::Dating : AffairType::Lover;
            handleDatingOrLover(npcFormID, targetFormID, aType);
            if (targetFormID != 0) {
                handleDatingOrLover(targetFormID, npcFormID, aType);
            }
        } else if (IsBreakupStatus(newStatus)) {
            struct ClearedIntentDelta {
                RE::FormID partnerID{ 0 };
                PartnerIntentType previousType{ PartnerIntentType::None };
            };
            std::vector<ClearedIntentDelta> clearedIntents;
            auto recordIntentClear = [&](State::WorldState& ws, State::ActorId partner) {
                const auto prev = ws.relationships.Read().GetPendingIntent(partner);
                if (prev.type != State::PartnerIntentType::None &&
                    ws.relationships.Write().ClearPendingIntent(partner)) {
                    clearedIntents.push_back({ partner.raw(), static_cast<PartnerIntentType>(prev.type) });
                }
            };

            const State::ActorId actorA{ npcFormID };
            const State::ActorId actorB{ targetFormID };

            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                // 1. Collect confess intents before closing affairs between cheater and paramour
                std::vector<State::ActorId> confessIntentCheaters;
                auto scanDirection = [&](State::ActorId cheater, State::ActorId paramour) {
                    if (cheater.IsNull() || paramour.IsNull()) return;
                    for (const auto& rec : ws.affairs.Read().GetByCheater(cheater)) {
                        if (rec.paramour == paramour && rec.affairStatus == State::AffairStatus::Ongoing) {
                            const auto intent = ws.relationships.Read().GetPendingIntent(cheater);
                            if ((intent.relatedLover.IsNull() || intent.relatedLover == paramour) &&
                                (intent.type == State::PartnerIntentType::ConfessTalking ||
                                 intent.type == State::PartnerIntentType::ConfessPhysical)) {
                                confessIntentCheaters.push_back(cheater);
                            }
                            MARAS_LOG_INFO("Affair marked EndedBreakup: cheater {:08X} / victim {:08X} with {:08X}",
                                           rec.cheater.raw(), rec.deceivedPartner.raw(), rec.paramour.raw());
                        }
                    }
                };
                scanDirection(actorA, actorB);
                scanDirection(actorB, actorA);
                ws.affairs.Write().CloseOngoingBetweenCheaterAndParamour(actorA, actorB);
                for (const auto cheater : confessIntentCheaters) {
                    recordIntentClear(ws, cheater);
                }

                // 2. Collect victim-split affairs and close them
                if (!actorA.IsNull() && !actorB.IsNull()) {
                    std::vector<State::ActorId> victimSplitCheaters;
                    for (const auto& rec : ws.affairs.Read().GetForCouplePair(State::CanonicalPairKey{ actorA, actorB })) {
                        if (rec.affairStatus == State::AffairStatus::Ongoing) {
                            victimSplitCheaters.push_back(rec.cheater);
                            MARAS_LOG_INFO("Affair marked EndedVictimSplit: cheater {:08X} / victim {:08X} with {:08X}",
                                           rec.cheater.raw(), rec.deceivedPartner.raw(), rec.paramour.raw());
                        }
                    }
                    ws.affairs.Write().CloseOngoingOnVictimSplit(actorA, actorB);
                    for (const auto cheater : victimSplitCheaters) {
                        recordIntentClear(ws, cheater);
                    }
                }

                // 3. Clear any remaining intent for an NPC breaking up directly with Player
                if (npcFormID == kPlayerFormID && targetFormID != 0) {
                    recordIntentClear(ws, actorB);
                } else if (targetFormID == kPlayerFormID && npcFormID != 0) {
                    recordIntentClear(ws, actorA);
                }
            });

            for (const auto& cleared : clearedIntents) {
                MARAS_LOG_INFO("Cleared pending partner intent for {:08X} (was {})",
                               cleared.partnerID, static_cast<int>(cleared.previousType));
                DispatchPartnerIntentChangedEvent(cleared.partnerID, PartnerIntentType::None);
            }
        }

        UpdateSpouseCheatingFaction(npcFormID);
        if (targetFormID != 0) {
            UpdateSpouseCheatingFaction(targetFormID);
        }
    }

    void NPCRelationshipManager::UpdateSpouseCheatingFaction(RE::FormID npcFormID) {
        if (npcFormID == 0 || npcFormID == kPlayerFormID) return;

        auto* faction = FormCache::GetSingleton().GetSpouseCheatingFaction();
        if (!faction) return;

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const State::ActorId npc{ npcFormID };
        const auto relStatus = static_cast<RelationshipStatus>(snap->relationships->GetStatus(npc, State::ActorId::Player()));
        if (!IsMainTier(relStatus)) {
            Utils::RemoveFromFaction(npcFormID, faction);
            return;
        }

        bool hasOngoingSecret = false;
        bool hasOngoingKnown = false;
        for (const auto& rec : snap->affairs->GetForCouplePair(State::CanonicalPairKey{ npc, State::ActorId::Player() })) {
            if (rec.cheater == npc &&
                rec.deceivedPartner.IsPlayer() &&
                rec.affairStatus == State::AffairStatus::Ongoing) {
                if (rec.isSecret) {
                    hasOngoingSecret = true;
                } else {
                    hasOngoingKnown = true;
                }
            }
        }

        std::int8_t targetRank = -1;
        if (hasOngoingKnown) {
            targetRank = 1;
        } else if (hasOngoingSecret) {
            targetRank = 0;
        }

        if (targetRank >= 0) {
            Utils::AddToFaction(npcFormID, faction, targetRank);
            MARAS_LOG_INFO("UpdateSpouseCheatingFaction: Set TTM_SpouseCheating faction on {:08X} to rank {}", npcFormID, targetRank);
        } else {
            Utils::RemoveFromFaction(npcFormID, faction);
            MARAS_LOG_INFO("UpdateSpouseCheatingFaction: Removed TTM_SpouseCheating faction from {:08X}", npcFormID);
        }
    }

    void NPCRelationshipManager::UpdateSpouseOpenRelationshipFaction(RE::FormID npcFormID) {
        if (npcFormID == 0 || npcFormID == kPlayerFormID) return;

        auto* faction = FormCache::GetSingleton().GetSpouseOpenRelationshipFaction();
        if (!faction) return;

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const State::ActorId npc{ npcFormID };
        const auto relStatus = static_cast<RelationshipStatus>(snap->relationships->GetStatus(npc, State::ActorId::Player()));
        const bool isOpen = IsMainTier(relStatus) && snap->relationships->IsPairOpen(npc, State::ActorId::Player());

        if (isOpen) {
            Utils::AddToFaction(npcFormID, faction, 0);
            MARAS_LOG_INFO("UpdateSpouseOpenRelationshipFaction: Set TTM_SpouseOpenRelationship faction on {:08X} to rank 0", npcFormID);
        } else {
            Utils::RemoveFromFaction(npcFormID, faction);
            MARAS_LOG_INFO("UpdateSpouseOpenRelationshipFaction: Removed TTM_SpouseOpenRelationship faction from {:08X}", npcFormID);
        }
    }

    void NPCRelationshipManager::UpdateCanFormNonCheatingMainFaction(RE::FormID npcFormID) {
        if (npcFormID == 0 || npcFormID == 0x07) return;

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const State::ActorId actor{ npcFormID };
        if (npcFormID != kPlayerFormID && !snap->actors->IsRegisteredAdult(State::RefId{ npcFormID })) return;

        auto* faction = FormCache::GetSingleton().GetCanFormNonCheatingMainFaction();
        if (!faction) return;

        bool hasClosedMainRelationship = false;
        if (!(npcFormID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy())) {
            for (const auto& pairKey : snap->relationships->GetAdjacentPairs(actor)) {
                const auto* edge = snap->relationships->FindEdge(pairKey);
                if (edge && IsMainTier(static_cast<RelationshipStatus>(edge->status)) && !edge->isOpen) {
                    hasClosedMainRelationship = true;
                    break;
                }
            }
        }

        // Rank 0 = can't form relationship without cheating (in closed main-tier relationship)
        // Rank 1 = can form relationship without cheating (polygamous player, open main-tier, lover, or single/candidate/ex)
        const std::int8_t targetRank = hasClosedMainRelationship ? 0 : 1;
        Utils::AddToFaction(npcFormID, faction, targetRank);
    }

    void NPCRelationshipManager::UpdateAllSpousesCheatingAndOpenFactions() {
        std::unordered_set<RE::FormID> candidates;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& pairKey : snap->relationships->GetAdjacentPairs(State::ActorId::Player())) {
                const RE::FormID partnerID = pairKey.Other(State::ActorId::Player()).raw();
                if (partnerID != 0 && partnerID != kPlayerFormID) {
                    candidates.insert(partnerID);
                }
            }
        }
        for (const RE::FormID partnerID : candidates) {
            UpdateSpouseCheatingFaction(partnerID);
            UpdateSpouseOpenRelationshipFaction(partnerID);
        }
    }

    void NPCRelationshipManager::UpdateAllCanFormNonCheatingMainFactions() {
        UpdateCanFormNonCheatingMainFaction(kPlayerFormID);
        std::vector<RE::FormID> registered;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& [refId, profile] : snap->actors->AllProfiles()) {
                if (profile.isRegisteredAdult && !refId.IsNull()) {
                    registered.push_back(refId.raw());
                }
            }
        }
        for (const RE::FormID npcID : registered) {
            UpdateCanFormNonCheatingMainFaction(npcID);
        }
    }

    bool NPCRelationshipManager::SetCheatingDiscovered(RE::FormID cheaterID, RE::FormID affairPartnerID, AffairDiscovery discovery) {
        std::string discStr = std::string(Utils::AffairDiscoveryToString(discovery));
        return SetCheatingDiscovered(cheaterID, affairPartnerID, discStr);
    }

    bool NPCRelationshipManager::SetCheatingDiscovered(RE::FormID cheaterID, RE::FormID affairPartnerID, AffairResolution resolution) {
        std::string resStr = std::string(Utils::AffairResolutionToString(resolution));
        return SetCheatingDiscovered(cheaterID, affairPartnerID, resStr);
    }

    bool NPCRelationshipManager::SetCheatingDiscovered(RE::FormID cheaterID, RE::FormID affairPartnerID, const std::string& statusOrResolution, RE::FormID targetVictimID) {
        if (cheaterID == 0) return false;

        bool updated = false;
        std::unordered_set<RE::FormID> victimsToUpdate;
        std::unordered_set<RE::FormID> partnersToBreak;
        std::unordered_set<RE::FormID> victimsToOpen;
        std::unordered_set<RE::FormID> victimsToSplit;

        const AffairDiscovery discovery = Utils::StringToAffairDiscovery(statusOrResolution);
        const AffairResolution resolution = Utils::StringToAffairResolution(statusOrResolution);
        const bool isDiscovery = (discovery != AffairDiscovery::None);

        struct EventInfo {
            RE::FormID cheaterID{ 0 };
            RE::FormID victimID{ 0 };
            RE::FormID affairPartnerID{ 0 };
        };
        std::vector<EventInfo> eventsToSend;

        struct ClearedIntentDelta {
            RE::FormID partnerID{ 0 };
            PartnerIntentType previousType{ PartnerIntentType::None };
        };
        std::vector<ClearedIntentDelta> clearedIntents;

        const State::ActorId cheater{ cheaterID };

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto recordIntentClear = [&](State::ActorId partner) {
                const auto prev = ws.relationships.Read().GetPendingIntent(partner);
                if (prev.type != State::PartnerIntentType::None &&
                    ws.relationships.Write().ClearPendingIntent(partner)) {
                    clearedIntents.push_back({ partner.raw(), static_cast<PartnerIntentType>(prev.type) });
                }
            };

            // Snapshot ongoing matches first so ExposeAndResolve operates strictly on matched ongoing triples
            const auto cheaterRecords = ws.affairs.Read().GetByCheater(cheater);
            for (const auto& rec : cheaterRecords) {
                if (rec.cheater != cheater || rec.affairStatus != State::AffairStatus::Ongoing) {
                    continue;
                }
                if (targetVictimID != 0 && rec.deceivedPartner.raw() != targetVictimID) {
                    continue;
                }
                if (affairPartnerID != 0 && rec.paramour.raw() != affairPartnerID) {
                    continue;
                }

                if (isDiscovery) {
                    if (!rec.isSecret) {
                        continue;
                    }
                    ws.affairs.Write().ExposeAndResolve(
                        cheater, rec.paramour, rec.deceivedPartner, State::AffairResolution::None);
                    updated = true;
                    victimsToUpdate.insert(rec.deceivedPartner.raw());
                    eventsToSend.push_back({ cheaterID, rec.deceivedPartner.raw(), rec.paramour.raw() });
                    MARAS_LOG_INFO("SetCheatingDiscovered: Affair of {:08X} with {:08X} (victim: {:08X}) discovered (type: {})",
                                   cheaterID, rec.paramour.raw(), rec.deceivedPartner.raw(), statusOrResolution);
                } else if (resolution != AffairResolution::None) {
                    ws.affairs.Write().ExposeAndResolve(
                        cheater, rec.paramour, rec.deceivedPartner, static_cast<State::AffairResolution>(resolution));
                    updated = true;
                    victimsToUpdate.insert(rec.deceivedPartner.raw());

                    switch (resolution) {
                        case AffairResolution::Confronted:
                            if (!rec.paramour.IsNull()) {
                                partnersToBreak.insert(rec.paramour.raw());
                            }
                            eventsToSend.push_back({ cheaterID, rec.deceivedPartner.raw(), rec.paramour.raw() });
                            MARAS_LOG_INFO("SetCheatingDiscovered: Affair of {:08X} with {:08X} (victim: {:08X}) resolved to EndedConfronted",
                                           cheaterID, rec.paramour.raw(), rec.deceivedPartner.raw());
                            break;

                        case AffairResolution::Allowed:
                            if (!rec.deceivedPartner.IsNull()) {
                                victimsToOpen.insert(rec.deceivedPartner.raw());
                            }
                            eventsToSend.push_back({ cheaterID, rec.deceivedPartner.raw(), rec.paramour.raw() });
                            MARAS_LOG_INFO("SetCheatingDiscovered: Affair of {:08X} with {:08X} (victim: {:08X}) resolved to Allowed (pair opened)",
                                           cheaterID, rec.paramour.raw(), rec.deceivedPartner.raw());
                            break;

                        case AffairResolution::Breakup:
                            recordIntentClear(cheater);
                            if (!rec.deceivedPartner.IsNull()) {
                                victimsToSplit.insert(rec.deceivedPartner.raw());
                            }
                            eventsToSend.push_back({ cheaterID, rec.deceivedPartner.raw(), rec.paramour.raw() });
                            MARAS_LOG_INFO("SetCheatingDiscovered: Affair of {:08X} with {:08X} (victim: {:08X}) resolved to EndedVictimSplit (separated)",
                                           cheaterID, rec.paramour.raw(), rec.deceivedPartner.raw());
                            break;

                        default:
                            break;
                    }
                } else {
                    if (!rec.isSecret) {
                        continue;
                    }
                    ws.affairs.Write().ExposeAndResolve(
                        cheater, rec.paramour, rec.deceivedPartner, State::AffairResolution::None);
                    updated = true;
                    victimsToUpdate.insert(rec.deceivedPartner.raw());
                    MARAS_LOG_INFO("SetCheatingDiscovered: Affair of {:08X} with {:08X} (victim: {:08X}) marked known (silent)",
                                   cheaterID, rec.paramour.raw(), rec.deceivedPartner.raw());
                }
            }

            if (updated) {
                const auto curIntent = ws.relationships.Read().GetPendingIntent(cheater);
                if (curIntent.type == State::PartnerIntentType::ConfessTalking ||
                    curIntent.type == State::PartnerIntentType::ConfessPhysical) {
                    recordIntentClear(cheater);
                }
            }
        });

        for (const auto& cleared : clearedIntents) {
            MARAS_LOG_INFO("Cleared pending partner intent for {:08X} (was {})",
                           cleared.partnerID, static_cast<int>(cleared.previousType));
            DispatchPartnerIntentChangedEvent(cleared.partnerID, PartnerIntentType::None);
        }

        // Apply deferred open-relationship transitions outside Modify
        for (const RE::FormID victimID : victimsToOpen) {
            SetPairIsOpen(cheaterID, victimID, true);
        }

        // Apply deferred victim-split breakups outside Modify
        for (const RE::FormID victimID : victimsToSplit) {
            const auto curStatus = GetRelationshipStatus(cheaterID, victimID);
            if (curStatus == RelationshipStatus::Married) {
                PromoteToDivorced(cheaterID, victimID, /*sendEvent=*/true);
            } else if (curStatus == RelationshipStatus::Engaged) {
                PromoteToJilted(cheaterID, victimID, /*sendEvent=*/true);
            } else if (curStatus == RelationshipStatus::Dating) {
                PromoteToExDating(cheaterID, victimID, /*sendEvent=*/true);
            } else if (curStatus == RelationshipStatus::Lover) {
                PromoteToExLover(cheaterID, victimID, /*sendEvent=*/true);
            }
        }

        // Break actual relationship between cheater and affair partner(s) whose affairs ended
        for (const RE::FormID partnerID : partnersToBreak) {
            const auto currentStatus = GetRelationshipStatus(cheaterID, partnerID);
            if (currentStatus == RelationshipStatus::Lover) {
                PromoteToExLover(cheaterID, partnerID, /*sendEvent=*/true);
            } else if (currentStatus == RelationshipStatus::Dating) {
                PromoteToExDating(cheaterID, partnerID, /*sendEvent=*/true);
            }
        }

        if (updated) {
            UpdateSpouseCheatingFaction(cheaterID);
            for (const RE::FormID vID : victimsToUpdate) {
                UpdateSpouseCheatingFaction(vID);
            }

            if (!eventsToSend.empty()) {
                if (auto* eventSource = SKSE::GetModCallbackEventSource()) {
                    for (const auto& ev : eventsToSend) {
                        nlohmann::json jsonPayload;
                        jsonPayload["cheater"] = fmt::format("0x{:08X}", ev.cheaterID);
                        jsonPayload["cheaterName"] = ActorSvc::GetName(ev.cheaterID);
                        jsonPayload["victim"] = fmt::format("0x{:08X}", ev.victimID);
                        jsonPayload["victimName"] = ActorSvc::GetName(ev.victimID);
                        jsonPayload["affairPartner"] = fmt::format("0x{:08X}", ev.affairPartnerID);
                        jsonPayload["affairPartnerName"] = ActorSvc::GetName(ev.affairPartnerID);

                        if (isDiscovery) {
                            jsonPayload["type"] = Utils::AffairDiscoveryToString(discovery);
                            std::string jsonStr = jsonPayload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                            RE::Actor* senderActor = NPCIndex::GetSingleton().GetActorFromFormId(discovery == AffairDiscovery::Exposed ? ev.affairPartnerID : ev.cheaterID);
                            RE::TESForm* senderForm = senderActor ? static_cast<RE::TESForm*>(senderActor) : nullptr;
                            SKSE::ModCallbackEvent modEvent("maras_affair_exposed", jsonStr, 0.0f, senderForm);
                            eventSource->SendEvent(&modEvent);
                            MARAS_LOG_INFO("Sent maras_affair_exposed event (type: {}): {}", Utils::AffairDiscoveryToString(discovery), jsonStr);
                            if (discovery == AffairDiscovery::Confessed || discovery == AffairDiscovery::Exposed) {
                                SkyrimNetMemoryService::GetSingleton().RecordInfidelityEvent(
                                    ev.cheaterID, ev.victimID, ev.affairPartnerID, std::string(Utils::AffairDiscoveryToString(discovery)));
                            }
                        } else if (resolution != AffairResolution::None) {
                            jsonPayload["resolution"] = statusOrResolution;
                            std::string jsonStr = jsonPayload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                            RE::Actor* senderActor = NPCIndex::GetSingleton().GetActorFromFormId(ev.cheaterID);
                            RE::TESForm* senderForm = senderActor ? static_cast<RE::TESForm*>(senderActor) : nullptr;
                            SKSE::ModCallbackEvent modEvent("maras_affair_resolved", jsonStr, 0.0f, senderForm);
                            eventSource->SendEvent(&modEvent);
                            MARAS_LOG_INFO("Sent maras_affair_resolved event: {}", jsonStr);
                            SkyrimNetMemoryService::GetSingleton().RecordInfidelityEvent(
                                ev.cheaterID, ev.victimID, ev.affairPartnerID, statusOrResolution);
                        }
                    }
                } else {
                    MARAS_LOG_ERROR("Could not get ModCallbackEventSource to send affair event");
                }
            }
        }

        if (!updated) {
            MARAS_LOG_DEBUG("SetCheatingDiscovered: No matching ongoing cheating record updated for cheater {:08X} with affairPartner {:08X} (statusOrResolution={})",
                            cheaterID, affairPartnerID, statusOrResolution);
        }
        return updated;
    }

}  // namespace MARAS

