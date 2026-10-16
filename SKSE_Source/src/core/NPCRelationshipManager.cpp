#include "core/NPCRelationshipManager.h"

#include "core/AffectionService.h"
#include "core/DeceasedRegistry.h"
#include "core/FactionService.h"
#include "core/FormCache.h"
#include "core/NPCIndex.h"
#include "core/NPCTypeDeterminer.h"
#include "core/PackageOverrideService.h"
#include "core/PlayerChildrenService.h"
#include "core/PlayerHouseService.h"
#include "core/Serialization.h"
#include "core/SkyrimNetIntegration.h"
#include "ui/PrismaUIDataBridge.h"
#include "core/SpouseAssetsService.h"
#include "core/SpouseHierarchyManager.h"
#include "services/ActorService.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"
#include "utils/JsonOverrideLoader.h"
#include "utils/JsonService.h"
#include "utils/Settings.h"

#include <RE/E/ExtraLinkedRef.h>
#include <RE/S/SendHUDMessage.h>

namespace MARAS {

    RE::FormID NPCRelationshipManager::DetermineDefaultInitiator(RE::FormID actorA, RE::FormID actorB) {
        auto getSex = [](RE::FormID id) -> uint8_t {
            if (id == kPlayerFormID) {
                if (auto player = RE::PlayerCharacter::GetSingleton()) {
                    if (auto base = player->GetActorBase()) {
                        return static_cast<uint8_t>(base->GetSex());
                    }
                }
            }
            return ActorSvc::GetSex(id);
        };

        uint8_t sexA = getSex(actorA);
        uint8_t sexB = getSex(actorB);

        // 0 = Male, 1 = Female
        if (sexA == 0 && sexB != 0) return actorA;
        if (sexB == 0 && sexA != 0) return actorB;
        return (actorA != 0) ? actorA : actorB;
    }

    NPCRelationshipManager& NPCRelationshipManager::GetSingleton() {
        static NPCRelationshipManager instance;
        return instance;
    }

    // Private helper methods
    void NPCRelationshipManager::SetLinkedRefForHomeMarker(RE::FormID npcFormID, RE::FormID markerFormID) {
        auto homeSandboxKeyword = FormCache::GetSingleton().GetHomeSandboxKeyword();
        if (!Utils::SetLinkedRef(npcFormID, markerFormID, homeSandboxKeyword)) {
            MARAS_LOG_WARN("Failed to set linked ref for NPC {:08X}: marker={:08X}, keyword={}",
                          npcFormID, markerFormID, homeSandboxKeyword != nullptr);
        }
    }

    void NPCRelationshipManager::UpdateTrackedFactionRank(RE::FormID npcFormID, RelationshipStatus status) {
        auto trackedFaction = FormCache::GetSingleton().GetTrackedFaction();
        if (!trackedFaction) {
            return;
        }

        std::int8_t rank = static_cast<std::int8_t>(status);
        AddToFaction(npcFormID, trackedFaction, rank);

        MARAS_LOG_INFO("Set tracked faction rank {} for NPC {:08X} (status: {})", rank, npcFormID,
                       Utils::RelationshipStatusToString(status));
    }

    void NPCRelationshipManager::SendStatusChangedEvent(RE::FormID npcFormID, RE::FormID targetFormID, RelationshipStatus status) {
        auto npcForm = RE::TESForm::LookupByID(npcFormID);
        auto npcActor = npcForm ? npcForm->As<RE::Actor>() : nullptr;
        if (!npcActor) {
            MARAS_LOG_WARN("Could not find actor for FormID {:08X} to send status event", npcFormID);
            return;
        }

        std::int32_t statusEnum = static_cast<std::int32_t>(status);
        std::string statusString(Utils::RelationshipStatusToString(status));

        std::transform(statusString.begin(), statusString.end(), statusString.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        auto eventSource = SKSE::GetModCallbackEventSource();
        if (eventSource) {
            SKSE::ModCallbackEvent modEvent("maras_status_changed", statusString, static_cast<float>(statusEnum),
                                            npcActor);
            eventSource->SendEvent(&modEvent);
            MARAS_LOG_INFO("Sent maras_status_changed event for NPC {:08X} (target {:08X}): status={} ({})",
                           npcFormID, targetFormID, statusEnum, statusString);

            nlohmann::json fullJson;
            fullJson["initiator"] = fmt::format("0x{:08X}", npcFormID);
            fullJson["acceptor"] = fmt::format("0x{:08X}", targetFormID);
            fullJson["initiatorName"] = ActorSvc::GetName(npcFormID);
            fullJson["acceptorName"] = ActorSvc::GetName(targetFormID);
            fullJson["status"] = statusString;

            std::string fullJsonStr = fullJson.dump();
            SKSE::ModCallbackEvent fullModEvent("maras_status_changed_full", fullJsonStr, static_cast<float>(statusEnum),
                                                npcActor);
            eventSource->SendEvent(&fullModEvent);
            MARAS_LOG_INFO("Sent maras_status_changed_full event: {}", fullJsonStr);
        } else {
            MARAS_LOG_ERROR("Could not get ModCallbackEventSource to send event");
        }
    }

    void NPCRelationshipManager::IndexPair(const RelationshipPairKey& key) {
        auto addForActor = [this, &key](RE::FormID actorID) {
            if (actorID == 0) return;
            auto& vec = actorPairIndex_[actorID];
            if (std::find(vec.begin(), vec.end(), key) == vec.end()) {
                vec.push_back(key);
            }
        };
        addForActor(key.actorA);
        addForActor(key.actorB);
    }

    void NPCRelationshipManager::UnindexPair(const RelationshipPairKey& key) {
        auto removeForActor = [this, &key](RE::FormID actorID) {
            if (actorID == 0) return;
            auto it = actorPairIndex_.find(actorID);
            if (it == actorPairIndex_.end()) return;
            auto& vec = it->second;
            vec.erase(std::remove(vec.begin(), vec.end(), key), vec.end());
            if (vec.empty()) {
                actorPairIndex_.erase(it);
            }
        };
        removeForActor(key.actorA);
        removeForActor(key.actorB);
    }

    NPCRelationshipData& NPCRelationshipManager::GetOrCreatePairData(RE::FormID actorA, RE::FormID actorB) {
        RelationshipPairKey key(actorA, actorB);
        auto [it, inserted] = relationshipPairs.try_emplace(key, actorA, actorB);
        it->second.pairKey = key;
        if (inserted) {
            IndexPair(key);
        }
        return it->second;
    }

    const std::vector<RelationshipPairKey>& NPCRelationshipManager::GetActorPairKeys(RE::FormID actorID) const {
        static const std::vector<RelationshipPairKey> emptyKeys;
        if (actorID == 0) return emptyKeys;
        auto it = actorPairIndex_.find(actorID);
        return (it != actorPairIndex_.end()) ? it->second : emptyKeys;
    }

    size_t NPCRelationshipManager::CountPartnersWithStatus(RE::FormID npcFormID, RelationshipStatus status) const {
        size_t count = 0;
        for (const auto& key : GetActorPairKeys(npcFormID)) {
            auto it = relationshipPairs.find(key);
            if (it != relationshipPairs.end() && it->second.status == status && key.GetOther(npcFormID) != 0) {
                ++count;
            }
        }
        return count;
    }

    void NPCRelationshipManager::RefreshPartnerRelationshipFactions(RE::FormID partnerID) {
        if (partnerID == 0) return;

        if (partnerID == kPlayerFormID) {
            if (GetMarriedCount(kPlayerFormID) == 0) {
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    if (auto* marriedFaction = FormCache::GetSingleton().GetMarriedFaction()) {
                        FactionService::InternalFactionScope scope;
                        Utils::RemoveFromFaction(player, marriedFaction);
                    }
                }
            }
            UpdateCanFormNonCheatingMainFaction(kPlayerFormID);
            return;
        }

        if (!IsRegistered(partnerID)) {
            return;
        }

        RelationshipStatus playerStatus = GetRelationshipStatus(partnerID, kPlayerFormID);
        if (playerStatus != RelationshipStatus::Unknown) {
            UpdateTrackedFactionRank(partnerID, playerStatus);
            ManageFactions(partnerID, kPlayerFormID, playerStatus);
        }

        UpdateSpouseCheatingFaction(partnerID);
        UpdateSpouseOpenRelationshipFaction(partnerID);
        UpdateCanFormNonCheatingMainFaction(partnerID);
        UpdateSpouseChildrenFaction(partnerID);
    }

    bool NPCRelationshipManager::IsMainTier(RelationshipStatus status) {
        return status == RelationshipStatus::Dating ||
               status == RelationshipStatus::Engaged ||
               status == RelationshipStatus::Married;
    }

    bool NPCRelationshipManager::IsActivePartnerStatus(RelationshipStatus status) {
        return IsMainTier(status) || status == RelationshipStatus::Lover;
    }

    bool NPCRelationshipManager::IsBreakupStatus(RelationshipStatus status) {
        return status == RelationshipStatus::Divorced ||
               status == RelationshipStatus::Jilted ||
               status == RelationshipStatus::ExDating ||
               status == RelationshipStatus::ExLover ||
               status == RelationshipStatus::Candidate;
    }

    bool NPCRelationshipManager::HasAnyOpenMainPair(RE::FormID actorID) const {
        if (actorID == 0) return false;
        for (const auto& key : GetActorPairKeys(actorID)) {
            auto it = relationshipPairs.find(key);
            if (it != relationshipPairs.end() && IsMainTier(it->second.status) && it->second.isOpen) {
                return true;
            }
        }
        return false;
    }

    int NPCRelationshipManager::BreakAllLoverConnections(RE::FormID actorID, bool onlyCheating) {
        if (actorID == 0) return 0;
        std::vector<RE::FormID> loversToBreak;
        for (const auto& key : GetActorPairKeys(actorID)) {
            auto it = relationshipPairs.find(key);
            if (it == relationshipPairs.end()) continue;
            const auto& data = it->second;
            if (data.status == RelationshipStatus::Lover) {
                RE::FormID partnerID = key.GetOther(actorID);
                if (partnerID == 0) continue;

                if (onlyCheating) {
                    if (IsRelationshipCheating(actorID, partnerID)) {
                        loversToBreak.push_back(partnerID);
                    }
                } else {
                    loversToBreak.push_back(partnerID);
                }
            }
        }

        int brokenCount = 0;
        for (RE::FormID partnerID : loversToBreak) {
            if (PromoteToExLover(actorID, partnerID, /*sendEvent=*/true)) {
                ++brokenCount;
            }
        }
        MARAS_LOG_INFO("BreakAllLoverConnections for {:08X}: brokenCount={}, onlyCheating={}", actorID, brokenCount, onlyCheating);
        return brokenCount;
    }

    bool NPCRelationshipManager::ChangeStatusCommon(RE::FormID npcFormID, RE::FormID targetFormID, RelationshipStatus status,
                                                    const std::function<void(RE::FormID, RE::FormID)>& postAction,
                                                    bool sendEvent) {
        if (npcFormID != kPlayerFormID && npcFormID != 0 && npcFormID != 0x07) {
            if (!EnsureRegistered(npcFormID)) {
                MARAS_LOG_WARN("Cannot change status for unregistered NPC {:08X}", npcFormID);
                return false;
            }
        }

        if (targetFormID != kPlayerFormID && targetFormID != 0 && targetFormID != 0x07) {
            if (!EnsureRegistered(targetFormID)) {
                MARAS_LOG_WARN("Cannot change status for unregistered NPC {:08X}", targetFormID);
                return false;
            }
        }

        RelationshipPairKey key(npcFormID, targetFormID);
        auto existingIt = relationshipPairs.find(key);
        RelationshipStatus oldStatus = (existingIt != relationshipPairs.end()) ? existingIt->second.status : RelationshipStatus::Candidate;
        if (oldStatus != status) {
            std::string blockReason;
            if (!CanFormRelationship(npcFormID, targetFormID, status, &blockReason)) {
                MARAS_LOG_WARN("ChangeStatusCommon blocked for {:08X} / {:08X} ({}) -> {}",
                               npcFormID, targetFormID, Utils::RelationshipStatusToString(status), blockReason);
                if (!blockReason.empty() && (npcFormID == kPlayerFormID || targetFormID == kPlayerFormID)) {
                    RE::SendHUDMessage::ShowHUDMessage(blockReason.c_str());
                }
                return false;
            }
        }

        bool shouldAutoOpen = false;
        if (oldStatus != status && IsMainTier(status)) {
            shouldAutoOpen = HasAnyOpenMainPair(npcFormID) || HasAnyOpenMainPair(targetFormID);
        }

        auto& data = GetOrCreatePairData(npcFormID, targetFormID);
        if (oldStatus != status) {
            auto calendar = RE::Calendar::GetSingleton();
            float currentDays = calendar ? calendar->GetDaysPassed() : 0.0f;
            RE::FormID initiator = 0;
            if (currentDays >= 0.0f) {
                if (npcFormID == kPlayerFormID || targetFormID == kPlayerFormID) {
                    initiator = kPlayerFormID;
                } else if (npcFormID != 0) {
                    initiator = npcFormID;
                } else {
                    initiator = DetermineDefaultInitiator(npcFormID, targetFormID);
                }
            } else {
                initiator = DetermineDefaultInitiator(npcFormID, targetFormID);
            }
            data.statusHistory.emplace_back(oldStatus, status, currentDays, initiator);

            if (status == RelationshipStatus::Engaged && npcFormID != kPlayerFormID && targetFormID != kPlayerFormID && npcFormID != 0 && targetFormID != 0) {
                MARAS_LOG_INFO("NPC-NPC pair {:08X} - {:08X} became engaged at game day {:.2f}; scheduled for auto-marriage in 5 days",
                               npcFormID, targetFormID, currentDays);
            }

            if (IsMainTier(status) && !data.isOpen) {
                if (shouldAutoOpen) {
                    data.isOpen = true;
                    MARAS_LOG_INFO("Forward-only auto-open applied for pair {:08X} - {:08X}", npcFormID, targetFormID);
                }
            } else if (IsBreakupStatus(status) && data.isOpen) {
                data.isOpen = false;
                MARAS_LOG_INFO("Reset isOpen to false on breakup ({}) for pair {:08X} - {:08X}",
                               Utils::RelationshipStatusToString(status), npcFormID, targetFormID);
            }
        }

        data.status = status;

        if (targetFormID == kPlayerFormID) {
            UpdateTrackedFactionRank(npcFormID, status);
            UpdateSpouseCheatingFaction(npcFormID);
            UpdateSpouseOpenRelationshipFaction(npcFormID);
            UpdateCanFormNonCheatingMainFaction(npcFormID);
            UpdateCanFormNonCheatingMainFaction(kPlayerFormID);
            ManageFactions(npcFormID, kPlayerFormID, status);
        } else if (npcFormID == kPlayerFormID) {
            UpdateTrackedFactionRank(targetFormID, status);
            UpdateSpouseCheatingFaction(targetFormID);
            UpdateSpouseOpenRelationshipFaction(targetFormID);
            UpdateCanFormNonCheatingMainFaction(targetFormID);
            UpdateCanFormNonCheatingMainFaction(kPlayerFormID);
            ManageFactions(targetFormID, kPlayerFormID, status);
        } else {
            UpdateCanFormNonCheatingMainFaction(npcFormID);
            UpdateCanFormNonCheatingMainFaction(targetFormID);
            ManageFactions(npcFormID, targetFormID, status);
            ManageFactions(targetFormID, npcFormID, status);
        }

        if (sendEvent) {
            SendStatusChangedEvent(npcFormID, targetFormID, status);
        }

        if (postAction) {
            try {
                postAction(npcFormID, targetFormID);
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("Exception in ChangeStatusCommon postAction for {:08X} / {:08X}: {}",
                               npcFormID, targetFormID, e.what());
            }
        }

        MARAS_LOG_INFO("Set status between {:08X} and {:08X} to {}", npcFormID, targetFormID,
                       Utils::RelationshipStatusToString(status));
        EvaluateCheatingOnStatusChange(npcFormID, targetFormID, status);
        RecalculateAndUpdateGlobals();
        PrismaUIDataBridge::GetSingleton().OnStatusChanged(npcFormID, targetFormID, static_cast<uint8_t>(status));
        return true;
    }

    // Registration and unregistration
    bool NPCRelationshipManager::RegisterAsCandidate(RE::FormID inputFormID) {
        MARAS_LOG_INFO("RegisterAsCandidate entered: inputFormID={:08X}", inputFormID);
        if (inputFormID == kPlayerFormID || inputFormID == 0 || inputFormID == 0x07) {
            return false;
        }

        auto& npcIndex = NPCIndex::GetSingleton();
        RE::FormID npcFormID = npcIndex.GetRefID(inputFormID);
        if (npcFormID == 0) {
            npcFormID = inputFormID;
        }

        if (npcFormID == kPlayerFormID || npcFormID == 0 || npcFormID == 0x07) {
            return false;
        }

        if (!EnsureRegistered(npcFormID)) {
            return false;
        }

        RelationshipStatus currentStatus = GetRelationshipStatus(npcFormID, kPlayerFormID);
        if (currentStatus != RelationshipStatus::Unknown && currentStatus != RelationshipStatus::Candidate) {
            MARAS_LOG_INFO("RegisterAsCandidate: NPC {:08X} already has relationship status {} with player",
                           npcFormID, Utils::RelationshipStatusToString(currentStatus));
            UpdateTrackedFactionRank(npcFormID, currentStatus);
            return false;
        }

        return DemoteToCandidate(npcFormID, kPlayerFormID, /*sendEvent=*/true);
    }

    bool NPCRelationshipManager::UnregisterNPC(MARAS::FormID npcFormID) {
        if (!IsRegistered(npcFormID)) {
            MARAS_LOG_WARN("NPC {:08X} is not registered", npcFormID);
            return false;
        }

        std::unordered_set<RE::FormID> survivingPartners;
        auto pairKeys = GetActorPairKeys(npcFormID);
        for (const auto& key : pairKeys) {
            RE::FormID otherID = key.GetOther(npcFormID);
            if (otherID != 0) {
                survivingPartners.insert(otherID);
            }
            UnindexPair(key);
            relationshipPairs.erase(key);
        }

        allRegistered.erase(npcFormID);
        npcProfiles.erase(npcFormID);

        // Clean up engine factions on the unregistered NPC
        {
            FactionService::InternalFactionScope scope;
            auto& cache = FormCache::GetSingleton();
            Utils::RemoveFromFaction(npcFormID, cache.GetTrackedFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseSocialClassFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseSkillTypeFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseTemperamentFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseCheatingFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseOpenRelationshipFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetCanFormNonCheatingMainFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseChildrenFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetMarriageAskedFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetCourtingFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetMarriedFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetPlayerFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetPlayerBedOwnershipFaction());
        }

        for (RE::FormID partnerID : survivingPartners) {
            RefreshPartnerRelationshipFactions(partnerID);
        }

        SpouseHierarchyManager::GetSingleton().OnSpouseRemoved(npcFormID);
        SpouseAssetsService::GetSingleton().StopShareHouseWithPlayer(npcFormID);
        (void)PlayerHouseService::GetSingleton().RemoveTenantFromPlayerHouse(npcFormID);
        AffectionService::GetSingleton().RemoveNPCData(npcFormID);

        RecalculateAndUpdateGlobals();
        MARAS_LOG_INFO("Unregistered NPC {} ({:08X})", ActorSvc::GetName(npcFormID), npcFormID);
        return true;
    }

    bool NPCRelationshipManager::HandleNPCDeath(MARAS::FormID npcFormID) {
        if (!IsRegistered(npcFormID)) {
            MARAS_LOG_WARN("HandleNPCDeath: NPC {:08X} is not registered", npcFormID);
            return false;
        }

        // Evaluate custody of any children left behind by the deceased parent
        EvaluateChildCustodyOnDeath(npcFormID);

        std::unordered_set<RE::FormID> survivingPartners;
        auto pairKeys = GetActorPairKeys(npcFormID);
        for (const auto& key : pairKeys) {
            RE::FormID otherID = key.GetOther(npcFormID);
            if (otherID != 0) {
                survivingPartners.insert(otherID);
            }
            UnindexPair(key);
            relationshipPairs.erase(key);
        }

        allRegistered.erase(npcFormID);
        npcProfiles.erase(npcFormID);

        // Clean up engine factions on the deceased NPC
        {
            FactionService::InternalFactionScope scope;
            auto& cache = FormCache::GetSingleton();
            Utils::RemoveFromFaction(npcFormID, cache.GetTrackedFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseSocialClassFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseSkillTypeFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseTemperamentFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseCheatingFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseOpenRelationshipFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetCanFormNonCheatingMainFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseChildrenFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetMarriageAskedFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetCourtingFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetMarriedFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetPlayerFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetPlayerBedOwnershipFaction());
        }

        for (RE::FormID partnerID : survivingPartners) {
            RefreshPartnerRelationshipFactions(partnerID);
        }

        SpouseHierarchyManager::GetSingleton().OnSpouseRemoved(npcFormID);
        SpouseAssetsService::GetSingleton().StopShareHouseWithPlayer(npcFormID);
        (void)PlayerHouseService::GetSingleton().RemoveTenantFromPlayerHouse(npcFormID);
        AffectionService::GetSingleton().RemoveNPCData(npcFormID);

        RecalculateAndUpdateGlobals();
        MARAS_LOG_INFO("HandleNPCDeath: Cleaned up active state for deceased NPC {} ({:08X})",
                       ActorSvc::GetName(npcFormID), npcFormID);
        return true;
    }

    std::vector<std::pair<RE::FormID, RelationshipStatus>> NPCRelationshipManager::GetActiveRelationships(RE::FormID actorID) const {
        std::vector<std::pair<RE::FormID, RelationshipStatus>> result;
        if (actorID == 0) return result;

        for (const auto& key : GetActorPairKeys(actorID)) {
            auto it = relationshipPairs.find(key);
            if (it == relationshipPairs.end()) continue;
            const auto& data = it->second;
            if (IsActivePartnerStatus(data.status)) {
                RE::FormID partnerID = key.GetOther(actorID);
                if (partnerID != 0) {
                    result.emplace_back(partnerID, data.status);
                }
            }
        }
        return result;
    }

    void NPCRelationshipManager::AddPendingRelationship(RE::FormID baseID, const PendingRelationshipData& data) {
        if (baseID == 0) return;
        auto& list = pendingRelationshipsByBaseID[baseID];
        for (const auto& existing : list) {
            if (existing.partnerBaseID == data.partnerBaseID &&
                existing.partnerRefID == data.partnerRefID &&
                existing.targetStatus == data.targetStatus) {
                return; // Skip duplicate entry
            }
        }
        list.push_back(data);
    }

    void NPCRelationshipManager::AddPendingChild(RE::FormID childBaseID, const PendingChildData& data) {
        if (childBaseID == 0) return;
        auto it = pendingChildrenByBaseID.find(childBaseID);
        if (it != pendingChildrenByBaseID.end()) {
            auto& existing = it->second;
            if (existing.childRefID == 0 && data.childRefID != 0) {
                existing.childRefID = data.childRefID;
            }
            if (existing.parentABaseID == 0 && data.parentABaseID != 0) {
                existing.parentABaseID = data.parentABaseID;
                existing.parentARefID = data.parentARefID;
            } else if (existing.parentBBaseID == 0 && data.parentABaseID != 0 && data.parentABaseID != existing.parentABaseID) {
                existing.parentBBaseID = data.parentABaseID;
                existing.parentBRefID = data.parentARefID;
            }
            if (existing.parentBBaseID == 0 && data.parentBBaseID != 0 && data.parentBBaseID != existing.parentABaseID) {
                existing.parentBBaseID = data.parentBBaseID;
                existing.parentBRefID = data.parentBRefID;
            }
            if (existing.parentARefID == 0 && data.parentARefID != 0 && existing.parentABaseID == data.parentABaseID) {
                existing.parentARefID = data.parentARefID;
            }
            if (existing.parentBRefID == 0 && data.parentBRefID != 0 && existing.parentBBaseID == data.parentBBaseID) {
                existing.parentBRefID = data.parentBRefID;
            }
            existing.isAdopted = existing.isAdopted && data.isAdopted;
        } else {
            pendingChildrenByBaseID[childBaseID] = data;
        }
        MARAS_LOG_INFO("AddPendingChild: Deferred child Base {:08X} (parents {:08X} / {:08X}, adopted: {}) to pending",
                       childBaseID, pendingChildrenByBaseID[childBaseID].parentABaseID,
                       pendingChildrenByBaseID[childBaseID].parentBBaseID,
                       pendingChildrenByBaseID[childBaseID].isAdopted);
    }

    void NPCRelationshipManager::TryResolvePendingChildren(RE::FormID baseID, RE::FormID refID) {
        if (baseID == 0 || refID == 0) return;

        auto resolveBioParents = [](RE::FormID guardianA, RE::FormID guardianB, bool isAdopted) -> std::pair<RE::FormID, RE::FormID> {
            if (isAdopted) {
                return { 0, 0 };
            }
            RE::FormID bioMother = 0;
            RE::FormID bioFather = 0;
            auto assignParentBySex = [&](RE::FormID parentID) {
                if (parentID == 0) return;
                uint8_t sex = ActorSvc::GetSex(parentID);
                if (sex == 1 && bioMother == 0) {
                    bioMother = parentID;
                } else if (sex == 0 && bioFather == 0) {
                    bioFather = parentID;
                }
            };
            assignParentBySex(guardianA);
            assignParentBySex(guardianB);
            return { bioMother, bioFather };
        };

        // 1. Check if incoming actor is a pending child
        auto& npcIndex = NPCIndex::GetSingleton();
        auto childIt = pendingChildrenByBaseID.find(baseID);
        if (childIt != pendingChildrenByBaseID.end()) {
            auto pending = childIt->second;
            pending.childRefID = refID;

            RE::FormID pARef = pending.parentARefID != 0 ? pending.parentARefID : npcIndex.GetRefID(pending.parentABaseID);
            RE::FormID pBRef = pending.parentBRefID != 0 ? pending.parentBRefID : npcIndex.GetRefID(pending.parentBBaseID);

            if (pARef == 0 && pending.parentABaseID == kPlayerFormID) pARef = kPlayerFormID;
            if (pBRef == 0 && pending.parentBBaseID == kPlayerFormID) pBRef = kPlayerFormID;

            pending.parentARefID = pARef;
            pending.parentBRefID = pBRef;

            if (pARef != 0 || pBRef != 0) {
                bool awaitingOtherParent = (pending.parentABaseID != 0 && pARef == 0) ||
                                           (pending.parentBBaseID != 0 && pBRef == 0);
                if (awaitingOtherParent) {
                    pendingChildrenByBaseID[baseID] = pending;
                } else {
                    pendingChildrenByBaseID.erase(childIt);
                }

                auto [bioMother, bioFather] = resolveBioParents(pARef, pBRef, pending.isAdopted);
                RegisterChildActor(refID, pARef, pBRef, 0, bioMother, bioFather, 0, static_cast<uint8_t>(ChildStage::Child), pending.isAdopted, /*sendEvent=*/false);
                MARAS_LOG_INFO("TryResolvePendingChildren: Resolved pending child {:08X} for guardians {:08X} / {:08X} (bioM: {:08X}, bioF: {:08X}, adopted: {})",
                               refID, pARef, pBRef, bioMother, bioFather, pending.isAdopted);
            } else {
                pendingChildrenByBaseID[baseID] = pending;
            }
        }

        // 2. Check if incoming actor is a parent of any pending children
        std::vector<std::pair<RE::FormID, PendingChildData>> snapshot(pendingChildrenByBaseID.begin(), pendingChildrenByBaseID.end());
        for (auto& [cBaseID, pending] : snapshot) {
            bool matchesParent = false;
            if (pending.parentABaseID == baseID) {
                pending.parentARefID = refID;
                matchesParent = true;
            }
            if (pending.parentBBaseID == baseID) {
                pending.parentBRefID = refID;
                matchesParent = true;
            }

            if (!matchesParent) {
                continue;
            }

            RE::FormID cRef = pending.childRefID != 0 ? pending.childRefID : npcIndex.GetRefID(pending.childBaseID);
            pending.childRefID = cRef;

            if (cRef != 0 && (pending.parentARefID != 0 || pending.parentBRefID != 0)) {
                bool awaitingOtherParent = (pending.parentABaseID != 0 && pending.parentARefID == 0) ||
                                           (pending.parentBBaseID != 0 && pending.parentBRefID == 0);
                if (awaitingOtherParent) {
                    pendingChildrenByBaseID[cBaseID] = pending;
                } else {
                    pendingChildrenByBaseID.erase(cBaseID);
                }

                auto [bioMother, bioFather] = resolveBioParents(pending.parentARefID, pending.parentBRefID, pending.isAdopted);
                RegisterChildActor(cRef, pending.parentARefID, pending.parentBRefID, 0, bioMother, bioFather, 0, static_cast<uint8_t>(ChildStage::Child), pending.isAdopted, /*sendEvent=*/false);
                MARAS_LOG_INFO("TryResolvePendingChildren: Resolved pending child {:08X} on parent load (guardians {:08X} / {:08X}, bioM: {:08X}, bioF: {:08X}, adopted: {})",
                               cRef, pending.parentARefID, pending.parentBRefID, bioMother, bioFather, pending.isAdopted);
            } else {
                pendingChildrenByBaseID[cBaseID] = pending;
            }
        }
    }

    void NPCRelationshipManager::TryResolvePendingRelationships(RE::FormID baseID, RE::FormID refID) {
        if (baseID == 0 || refID == 0) return;

        if (IsRegistered(refID)) {
            UpdateCanFormNonCheatingMainFaction(refID);
        }

        TryResolvePendingChildren(baseID, refID);

        auto it = pendingRelationshipsByBaseID.find(baseID);
        if (it == pendingRelationshipsByBaseID.end()) {
            return;
        }

        auto pendingList = std::move(it->second);
        pendingRelationshipsByBaseID.erase(it);

        MARAS_LOG_INFO("TryResolvePendingRelationships: Resolving {} pending relationship(s) for '{}' (Base ID {:08X} -> Ref ID {:08X})",
                       pendingList.size(), ActorSvc::GetName(refID), baseID, refID);

        EnsureRegistered(refID);

        auto& npcIndex = NPCIndex::GetSingleton();
        for (const auto& pending : pendingList) {
            RE::FormID partnerRefID = pending.partnerRefID;

            if (partnerRefID == 0 && pending.partnerBaseID != 0) {
                partnerRefID = npcIndex.GetRefID(pending.partnerBaseID);
                if (partnerRefID == pending.partnerBaseID) {
                    if (auto* form = RE::TESForm::LookupByID(partnerRefID)) {
                        if (form->GetFormType() != RE::FormType::ActorCharacter && form->GetFormType() != RE::FormType::Reference) {
                            partnerRefID = 0;
                        }
                    } else {
                        partnerRefID = 0;
                    }
                }
            }

            if (partnerRefID != 0) {
                EnsureRegistered(partnerRefID);
                SetOriginalRelationshipStatus(refID, partnerRefID, pending.originalStatus);

                auto currentStatus = GetRelationshipStatus(refID, partnerRefID);
                if (currentStatus == RelationshipStatus::Candidate || currentStatus == RelationshipStatus::Unknown) {
                    if (pending.targetStatus != RelationshipStatus::Unknown) {
                        ApplyImportedVanillaStatus(refID, partnerRefID, pending.originalStatus, pending.targetStatus);

                        auto& pairData = GetOrCreatePairData(refID, partnerRefID);
                        for (auto& hist : pairData.statusHistory) {
                            if (hist.timestamp >= 0) {
                                hist.timestamp = -1;
                            }
                        }

                        MARAS_LOG_INFO("TryResolvePendingRelationships: Assigned status {} between '{}' ({:08X}) and '{}' ({:08X})",
                                       Utils::RelationshipStatusToString(pending.targetStatus),
                                       ActorSvc::GetName(refID), refID,
                                       ActorSvc::GetName(partnerRefID), partnerRefID);
                    }
                }
            } else {
                PendingRelationshipData remaining(baseID, refID, pending.targetStatus, pending.originalStatus);
                AddPendingRelationship(pending.partnerBaseID, remaining);
                MARAS_LOG_INFO("TryResolvePendingRelationships: Resolved actor '{}' ({:08X}), partner base '{}' ({:08X}) remains pending",
                               ActorSvc::GetName(refID), refID,
                               ActorSvc::GetName(pending.partnerBaseID), pending.partnerBaseID);
            }
        }
    }

    void NPCRelationshipManager::TryResolvePendingRelationships(RE::Actor* actor) {
        if (!actor) {
            MARAS_LOG_WARN("TryResolvePendingRelationships: null actor passed");
            return;
        }
        if (auto* base = actor->GetActorBase()) {
            RE::FormID baseID = base->GetFormID();
            RE::FormID refID = actor->GetFormID();
            TryResolvePendingRelationships(baseID, refID);
        } else {
            MARAS_LOG_WARN("TryResolvePendingRelationships: actor '{}' ({:08X}) has null actorBase", actor->GetName(), actor->GetFormID());
        }
    }

    // Status checks
    bool NPCRelationshipManager::IsRegistered(RE::FormID npcFormID) const {
        if (npcFormID == kPlayerFormID || npcFormID == 0 || npcFormID == 0x07) return false;
        return allRegistered.contains(npcFormID);
    }

    bool NPCRelationshipManager::IsCandidate(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Candidate;
    }

    bool NPCRelationshipManager::IsEngaged(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Engaged;
    }

    bool NPCRelationshipManager::IsMarried(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Married;
    }

    bool NPCRelationshipManager::IsDivorced(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Divorced;
    }

    bool NPCRelationshipManager::IsMostRecentRelationshipDivorced(RE::FormID actorA, RE::FormID actorB) const {
        if (actorA == 0 || actorB == 0 || actorA == actorB) return false;

        const auto* data = GetNPCData(actorA, actorB);
        if (!data) return false;

        // Inspect only the single most recent status transition if history exists, otherwise current status
        if (!data->statusHistory.empty()) {
            return data->statusHistory.back().toStatus == RelationshipStatus::Divorced;
        }

        return data->status == RelationshipStatus::Divorced;
    }

    bool NPCRelationshipManager::IsJilted(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Jilted;
    }

    bool NPCRelationshipManager::IsDating(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Dating;
    }

    bool NPCRelationshipManager::IsLover(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Lover;
    }

    bool NPCRelationshipManager::IsExDating(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::ExDating;
    }

    bool NPCRelationshipManager::IsExLover(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::ExLover;
    }

    bool NPCRelationshipManager::IsPlayerActiveMainPartner(RE::FormID npcFormID) const {
        if (npcFormID == 0 || npcFormID == kPlayerFormID || npcFormID == 0x07) return false;

        auto isMainWithPlayer = [this](RE::FormID id) {
            if (id == 0 || id == kPlayerFormID || id == 0x07) return false;
            auto st = GetRelationshipStatus(id, kPlayerFormID);
            return st == RelationshipStatus::Dating ||
                   st == RelationshipStatus::Engaged ||
                   st == RelationshipStatus::Married;
        };

        if (isMainWithPlayer(npcFormID)) return true;

        // Check resolved ref ID if npcFormID is base ID
        RE::FormID ref = NPCIndex::GetSingleton().GetRefID(npcFormID);
        if (ref != 0 && ref != npcFormID && isMainWithPlayer(ref)) return true;

        // Check resolved base ID if npcFormID is ref ID
        RE::FormID base = NPCIndex::GetSingleton().GetBaseID(npcFormID);
        if (base != 0 && base != npcFormID && isMainWithPlayer(base)) return true;

        return false;
    }

    // Status availability checks
    bool NPCRelationshipManager::IsAvailableForDating(RE::FormID npcFormID) const {
        if (npcFormID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) {
            return true;
        }
        for (const auto& key : GetActorPairKeys(npcFormID)) {
            auto it = relationshipPairs.find(key);
            if (it != relationshipPairs.end() && IsMainTier(it->second.status)) {
                return false;
            }
        }
        return true;
    }

    bool NPCRelationshipManager::IsAvailableForDating(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForDating(npcFormID);
        }
        return CanFormRelationship(npcFormID, targetFormID, RelationshipStatus::Dating);
    }

    bool NPCRelationshipManager::IsAvailableForEngaged(RE::FormID npcFormID) const {
        if (npcFormID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) {
            return true;
        }
        for (const auto& key : GetActorPairKeys(npcFormID)) {
            auto it = relationshipPairs.find(key);
            if (it != relationshipPairs.end() &&
                (it->second.status == RelationshipStatus::Engaged || it->second.status == RelationshipStatus::Married)) {
                return false;
            }
        }
        return true;
    }

    bool NPCRelationshipManager::IsAvailableForEngaged(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForEngaged(npcFormID);
        }
        return CanFormRelationship(npcFormID, targetFormID, RelationshipStatus::Engaged);
    }

    bool NPCRelationshipManager::IsAvailableForMarried(RE::FormID npcFormID) const {
        if (npcFormID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) {
            return true;
        }
        for (const auto& key : GetActorPairKeys(npcFormID)) {
            auto it = relationshipPairs.find(key);
            if (it != relationshipPairs.end() && it->second.status == RelationshipStatus::Married) {
                return false;
            }
        }
        return true;
    }

    bool NPCRelationshipManager::IsAvailableForMarried(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForMarried(npcFormID);
        }
        return CanFormRelationship(npcFormID, targetFormID, RelationshipStatus::Married);
    }

    bool NPCRelationshipManager::IsAvailableForLover(RE::FormID /*npcFormID*/) const {
        return true;
    }

    bool NPCRelationshipManager::IsAvailableForLover(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForLover(npcFormID);
        }
        return CanFormRelationship(npcFormID, targetFormID, RelationshipStatus::Lover);
    }

    bool NPCRelationshipManager::IsAvailableForDivorced(RE::FormID npcFormID) const {
        for (const auto& key : GetActorPairKeys(npcFormID)) {
            auto it = relationshipPairs.find(key);
            if (it != relationshipPairs.end() && it->second.status == RelationshipStatus::Married) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::IsAvailableForDivorced(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForDivorced(npcFormID);
        }
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Married;
    }

    bool NPCRelationshipManager::IsAvailableForJilted(RE::FormID npcFormID) const {
        for (const auto& key : GetActorPairKeys(npcFormID)) {
            auto it = relationshipPairs.find(key);
            if (it != relationshipPairs.end() && it->second.status == RelationshipStatus::Engaged) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::IsAvailableForJilted(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForJilted(npcFormID);
        }
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Engaged;
    }

    bool NPCRelationshipManager::IsAvailableForExDating(RE::FormID npcFormID) const {
        for (const auto& key : GetActorPairKeys(npcFormID)) {
            auto it = relationshipPairs.find(key);
            if (it != relationshipPairs.end() && it->second.status == RelationshipStatus::Dating) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::IsAvailableForExDating(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForExDating(npcFormID);
        }
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Dating;
    }

    bool NPCRelationshipManager::IsAvailableForExLover(RE::FormID npcFormID) const {
        for (const auto& key : GetActorPairKeys(npcFormID)) {
            auto it = relationshipPairs.find(key);
            if (it != relationshipPairs.end() && it->second.status == RelationshipStatus::Lover) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::IsAvailableForExLover(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForExLover(npcFormID);
        }
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Lover;
    }

    bool NPCRelationshipManager::IsAvailableForBreakup(RE::FormID npcFormID) const {
        for (const auto& key : GetActorPairKeys(npcFormID)) {
            auto it = relationshipPairs.find(key);
            if (it != relationshipPairs.end() && IsActivePartnerStatus(it->second.status)) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::IsAvailableForBreakup(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForBreakup(npcFormID);
        }
        return IsActivePartnerStatus(GetRelationshipStatus(npcFormID, targetFormID));
    }

    bool NPCRelationshipManager::IsAvailableForBreakupDating(RE::FormID npcFormID) const {
        for (const auto& key : GetActorPairKeys(npcFormID)) {
            auto it = relationshipPairs.find(key);
            if (it != relationshipPairs.end() && it->second.status == RelationshipStatus::Dating) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::IsAvailableForBreakupDating(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForBreakupDating(npcFormID);
        }
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Dating;
    }

    bool NPCRelationshipManager::IsAvailableForBreakupLover(RE::FormID npcFormID) const {
        for (const auto& key : GetActorPairKeys(npcFormID)) {
            auto it = relationshipPairs.find(key);
            if (it != relationshipPairs.end() && it->second.status == RelationshipStatus::Lover) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::IsAvailableForBreakupLover(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForBreakupLover(npcFormID);
        }
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Lover;
    }

    bool NPCRelationshipManager::IsAvailableForOpenRelationship(RE::FormID npcFormID) const {
        for (const auto& key : GetActorPairKeys(npcFormID)) {
            auto it = relationshipPairs.find(key);
            if (it != relationshipPairs.end() && IsMainTier(it->second.status) && !it->second.isOpen) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::IsAvailableForOpenRelationship(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForOpenRelationship(npcFormID);
        }
        return CanOpenRelationship(npcFormID, targetFormID);
    }

    bool NPCRelationshipManager::IsAvailableForCloseRelationship(RE::FormID npcFormID) const {
        for (const auto& key : GetActorPairKeys(npcFormID)) {
            auto it = relationshipPairs.find(key);
            if (it != relationshipPairs.end() && IsMainTier(it->second.status) && it->second.isOpen) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::IsAvailableForCloseRelationship(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForCloseRelationship(npcFormID);
        }
        return CanCloseRelationship(npcFormID, targetFormID);
    }

    bool NPCRelationshipManager::IsAvailableForStatus(RE::FormID npcFormID, RelationshipStatus status) const {
        switch (status) {
            case RelationshipStatus::Dating:
                return IsAvailableForDating(npcFormID);
            case RelationshipStatus::Engaged:
                return IsAvailableForEngaged(npcFormID);
            case RelationshipStatus::Married:
                return IsAvailableForMarried(npcFormID);
            case RelationshipStatus::Lover:
                return IsAvailableForLover(npcFormID);
            case RelationshipStatus::Divorced:
                return IsAvailableForDivorced(npcFormID);
            case RelationshipStatus::Jilted:
                return IsAvailableForJilted(npcFormID);
            case RelationshipStatus::ExDating:
                return IsAvailableForExDating(npcFormID);
            case RelationshipStatus::ExLover:
                return IsAvailableForExLover(npcFormID);
            case RelationshipStatus::Candidate:
                return true;
            default:
                return true;
        }
    }

    bool NPCRelationshipManager::IsAvailableForStatus(RE::FormID npcFormID, const std::string& statusName) const {
        if (StringUtils::CaseInsensitiveEquals(statusName, "breakup")) {
            return IsAvailableForBreakup(npcFormID);
        }
        if (StringUtils::CaseInsensitiveEquals(statusName, "open_relationship") ||
            StringUtils::CaseInsensitiveEquals(statusName, "open")) {
            return IsAvailableForOpenRelationship(npcFormID);
        }
        if (StringUtils::CaseInsensitiveEquals(statusName, "close_relationship") ||
            StringUtils::CaseInsensitiveEquals(statusName, "close")) {
            return IsAvailableForCloseRelationship(npcFormID);
        }
        return IsAvailableForStatus(npcFormID, Utils::StringToRelationshipStatus(statusName));
    }

    bool NPCRelationshipManager::IsAvailableForStatus(RE::FormID npcFormID, RE::FormID targetFormID, RelationshipStatus status) const {
        if (targetFormID == 0) {
            return IsAvailableForStatus(npcFormID, status);
        }
        switch (status) {
            case RelationshipStatus::Dating:
                return IsAvailableForDating(npcFormID, targetFormID);
            case RelationshipStatus::Engaged:
                return IsAvailableForEngaged(npcFormID, targetFormID);
            case RelationshipStatus::Married:
                return IsAvailableForMarried(npcFormID, targetFormID);
            case RelationshipStatus::Lover:
                return IsAvailableForLover(npcFormID, targetFormID);
            case RelationshipStatus::Divorced:
                return IsAvailableForDivorced(npcFormID, targetFormID);
            case RelationshipStatus::Jilted:
                return IsAvailableForJilted(npcFormID, targetFormID);
            case RelationshipStatus::ExDating:
                return IsAvailableForExDating(npcFormID, targetFormID);
            case RelationshipStatus::ExLover:
                return IsAvailableForExLover(npcFormID, targetFormID);
            case RelationshipStatus::Candidate:
                return true;
            default:
                return true;
        }
    }

    bool NPCRelationshipManager::IsAvailableForStatus(RE::FormID npcFormID, RE::FormID targetFormID, const std::string& statusName) const {
        if (targetFormID == 0) {
            return IsAvailableForStatus(npcFormID, statusName);
        }
        if (StringUtils::CaseInsensitiveEquals(statusName, "breakup")) {
            return IsAvailableForBreakup(npcFormID, targetFormID);
        }
        if (StringUtils::CaseInsensitiveEquals(statusName, "open_relationship") ||
            StringUtils::CaseInsensitiveEquals(statusName, "open")) {
            return CanOpenRelationship(npcFormID, targetFormID);
        }
        if (StringUtils::CaseInsensitiveEquals(statusName, "close_relationship") ||
            StringUtils::CaseInsensitiveEquals(statusName, "close")) {
            return CanCloseRelationship(npcFormID, targetFormID);
        }
        return IsAvailableForStatus(npcFormID, targetFormID, Utils::StringToRelationshipStatus(statusName));
    }

    bool NPCRelationshipManager::CanOpenRelationship(RE::FormID actorA, RE::FormID actorB) const {
        RelationshipPairKey key(actorA, actorB);
        auto it = relationshipPairs.find(key);
        if (it != relationshipPairs.end()) {
            return IsMainTier(it->second.status) && !it->second.isOpen;
        }
        return false;
    }

    bool NPCRelationshipManager::CanCloseRelationship(RE::FormID actorA, RE::FormID actorB) const {
        RelationshipPairKey key(actorA, actorB);
        auto it = relationshipPairs.find(key);
        if (it != relationshipPairs.end()) {
            return IsMainTier(it->second.status) && it->second.isOpen;
        }
        return false;
    }

    bool NPCRelationshipManager::CanFormRelationship(RE::FormID actorA, RE::FormID actorB, RelationshipStatus status, std::string* outReason) const {
        if (outReason) {
            outReason->clear();
        }

        if (actorA == actorB && actorA != 0) {
            if (outReason) {
                *outReason = "An actor cannot form a relationship with themselves.";
            }
            return false;
        }

        // Child check
        if (IsChildNPC(actorA) || IsChildNPC(actorB)) {
            if (outReason) {
                *outReason = "Children cannot enter adult relationships.";
            }
            return false;
        }

        // Family / consanguinity check for active romantic relationships
        if (IsActivePartnerStatus(status) && AreFamilyMembers(actorA, actorB, outReason)) {
            return false;
        }

        std::string nameA = ActorSvc::GetName(actorA);
        std::string nameB = ActorSvc::GetName(actorB);
        if (nameA.empty() || nameA == "None") nameA = (actorA == kPlayerFormID) ? "Player" : fmt::format("0x{:08X}", actorA);
        if (nameB.empty() || nameB == "None") nameB = (actorB == kPlayerFormID) ? "Player" : fmt::format("0x{:08X}", actorB);

        // Validate existing direct relationship between actorA and actorB
        RelationshipPairKey directKey(actorA, actorB);
        auto directIt = relationshipPairs.find(directKey);
        if (directIt != relationshipPairs.end()) {
            const auto directStatus = directIt->second.status;
            if (status == RelationshipStatus::Lover) {
                if (directStatus == RelationshipStatus::Lover) {
                    if (outReason) {
                        *outReason = fmt::format("{} and {} are already lovers.", nameA, nameB);
                    }
                    return false;
                }
                if (IsMainTier(directStatus)) {
                    if (outReason) {
                        *outReason = fmt::format("{} and {} already have an active relationship ({}).", nameA, nameB, Utils::RelationshipStatusToString(directStatus));
                    }
                    return false;
                }
            }
            if (status == RelationshipStatus::Dating && IsMainTier(directStatus)) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} already have an active relationship ({}).", nameA, nameB, Utils::RelationshipStatusToString(directStatus));
                }
                return false;
            }
            if (status == RelationshipStatus::Engaged && (directStatus == RelationshipStatus::Engaged || directStatus == RelationshipStatus::Married)) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} are already engaged or married.", nameA, nameB);
                }
                return false;
            }
            if (status == RelationshipStatus::Married && directStatus == RelationshipStatus::Married) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} are already married.", nameA, nameB);
                }
                return false;
            }
        }

        if (status == RelationshipStatus::Lover) {
            return true;
        }

        if (status == RelationshipStatus::Divorced) {
            if (directIt == relationshipPairs.end() || directIt->second.status != RelationshipStatus::Married) {
                if (outReason) *outReason = "Can only divorce if currently Married.";
                return false;
            }
            return true;
        }
        if (status == RelationshipStatus::Jilted) {
            if (directIt == relationshipPairs.end() || directIt->second.status != RelationshipStatus::Engaged) {
                if (outReason) *outReason = "Can only be jilted if currently Engaged.";
                return false;
            }
            return true;
        }
        if (status == RelationshipStatus::ExDating) {
            if (directIt == relationshipPairs.end() || directIt->second.status != RelationshipStatus::Dating) {
                if (outReason) *outReason = "Can only transition to Ex-Dating if currently Dating.";
                return false;
            }
            return true;
        }
        if (status == RelationshipStatus::ExLover) {
            if (directIt == relationshipPairs.end() || directIt->second.status != RelationshipStatus::Lover) {
                if (outReason) *outReason = "Can only transition to Ex-Lover if currently Lovers.";
                return false;
            }
            return true;
        }

        if (!IsMainTier(status)) {
            return true;
        }

        bool aIsPlayer = (actorA == kPlayerFormID);
        bool bIsPlayer = (actorB == kPlayerFormID);
        bool polygamyEnabled = Settings::GetSingleton().GetEnablePolygamy();

        auto validateActorWithOtherPartners = [&](RE::FormID self, RE::FormID other, bool isPlayer, const std::string& name) -> bool {
            if (isPlayer && polygamyEnabled) {
                return true;
            }

            for (const auto& key : GetActorPairKeys(self)) {
                if (!key.Contains(other)) {
                    auto it = relationshipPairs.find(key);
                    if (it == relationshipPairs.end()) continue;
                    const auto& data = it->second;

                    RE::FormID existingPartnerID = key.GetOther(self);
                    std::string partnerName = ActorSvc::GetName(existingPartnerID);
                    if (partnerName.empty() || partnerName == "None") {
                        partnerName = (existingPartnerID == kPlayerFormID) ? "Player" : fmt::format("0x{:08X}", existingPartnerID);
                    }

                    if (status == RelationshipStatus::Dating || status == RelationshipStatus::Married) {
                        if (IsMainTier(data.status)) {
                            if (outReason) {
                                *outReason = isPlayer
                                    ? "Cannot enter another main relationship: Polygamy is disabled in settings."
                                    : fmt::format("{} already has a committed main partner ({}).", name, partnerName);
                            }
                            return false;
                        }
                    } else if (status == RelationshipStatus::Engaged) {
                        if (data.status == RelationshipStatus::Engaged || data.status == RelationshipStatus::Married) {
                            if (outReason) {
                                *outReason = isPlayer
                                    ? "Cannot enter another engagement/marriage: Polygamy is disabled in settings."
                                    : fmt::format("{} is already engaged or married to {}.", name, partnerName);
                            }
                            return false;
                        }
                        if (data.status == RelationshipStatus::Dating) {
                            if (outReason) {
                                *outReason = isPlayer
                                    ? "Cannot propose engagement to this actor while dating someone else."
                                    : fmt::format("{} is dating {} and can only get engaged to their dating partner.", name, partnerName);
                            }
                            return false;
                        }
                    }
                }
            }
            return true;
        };

        if (!validateActorWithOtherPartners(actorA, actorB, aIsPlayer, nameA)) {
            return false;
        }
        if (!validateActorWithOtherPartners(actorB, actorA, bIsPlayer, nameB)) {
            return false;
        }

        return true;
    }

    bool NPCRelationshipManager::CanFormRelationship(RE::FormID actorA, RE::FormID actorB, const std::string& statusName, std::string* outReason) const {
        if (outReason) {
            outReason->clear();
        }
        if (StringUtils::CaseInsensitiveEquals(statusName, "breakup")) {
            if (!IsAvailableForBreakup(actorA, actorB)) {
                if (outReason) {
                    *outReason = "Actors do not have an active relationship to break up.";
                }
                return false;
            }
            return true;
        }
        if (StringUtils::CaseInsensitiveEquals(statusName, "open_relationship") ||
            StringUtils::CaseInsensitiveEquals(statusName, "open")) {
            if (!CanOpenRelationship(actorA, actorB)) {
                if (outReason) {
                    *outReason = "Can only open an active main relationship that is currently closed.";
                }
                return false;
            }
            return true;
        }
        if (StringUtils::CaseInsensitiveEquals(statusName, "close_relationship") ||
            StringUtils::CaseInsensitiveEquals(statusName, "close")) {
            if (!CanCloseRelationship(actorA, actorB)) {
                if (outReason) {
                    *outReason = "Can only close an active main relationship that is currently open.";
                }
                return false;
            }
            return true;
        }
        return CanFormRelationship(actorA, actorB, Utils::StringToRelationshipStatus(statusName), outReason);
    }

    bool NPCRelationshipManager::AreFamilyMembers(RE::FormID actorA, RE::FormID actorB, std::string* outReason) const {
        if (outReason) {
            outReason->clear();
        }

        if (actorA == 0 || actorB == 0 || actorA == actorB) {
            return false;
        }

        std::string nameA = ActorSvc::GetName(actorA);
        std::string nameB = ActorSvc::GetName(actorB);
        if (nameA.empty() || nameA == "None") nameA = (actorA == kPlayerFormID) ? "Player" : fmt::format("0x{:08X}", actorA);
        if (nameB.empty() || nameB == "None") nameB = (actorB == kPlayerFormID) ? "Player" : fmt::format("0x{:08X}", actorB);

        // 1. MARAS Children Registry checks
        auto collectNormalizedParentIDs = [](RE::FormID actorID) -> std::unordered_set<RE::FormID> {
            std::unordered_set<RE::FormID> ids;
            if (actorID == 0 || actorID == kUnknownFatherFormID) {
                return ids;
            }
            ids.insert(actorID);
            if (actorID == kPlayerFormID || actorID == kPlayerBaseID) {
                ids.insert(kPlayerFormID);
                ids.insert(kPlayerBaseID);
                return ids;
            }
            RE::FormID refID = NPCIndex::GetSingleton().GetRefID(actorID);
            if (refID != 0) ids.insert(refID);
            RE::FormID baseID = NPCIndex::GetSingleton().GetBaseID(actorID);
            if (baseID != 0) ids.insert(baseID);
            if (auto* base = ActorSvc::GetActorBase(actorID)) {
                if (base->GetFormID() != 0) ids.insert(base->GetFormID());
            }
            return ids;
        };

        auto collectEntryParentIDs = [&](const ChildRegistryEntry& entry) -> std::unordered_set<RE::FormID> {
            std::unordered_set<RE::FormID> allIDs;
            for (RE::FormID rawParentID : { entry.guardianAID, entry.guardianBID, entry.bioMotherID, entry.bioFatherID }) {
                for (RE::FormID normID : collectNormalizedParentIDs(rawParentID)) {
                    allIDs.insert(normID);
                }
            }
            return allIDs;
        };

        auto checkParentChildMaras = [&](RE::FormID childID, RE::FormID possibleParentID) -> bool {
            auto entryOpt = GetChildEntry(childID);
            if (!entryOpt.has_value()) return false;
            const auto entryParents = collectEntryParentIDs(*entryOpt);
            for (RE::FormID normID : collectNormalizedParentIDs(possibleParentID)) {
                if (entryParents.contains(normID)) {
                    return true;
                }
            }
            return false;
        };

        if (checkParentChildMaras(actorA, actorB) || checkParentChildMaras(actorB, actorA)) {
            if (outReason) {
                *outReason = fmt::format("{} and {} have a parent/child relationship.", nameA, nameB);
            }
            return true;
        }

        auto entryA = GetChildEntry(actorA);
        auto entryB = GetChildEntry(actorB);
        if (entryA.has_value() && entryB.has_value()) {
            const auto parentsA = collectEntryParentIDs(*entryA);
            const auto parentsB = collectEntryParentIDs(*entryB);

            bool shareParent = false;
            for (RE::FormID pID : parentsB) {
                if (parentsA.contains(pID)) {
                    shareParent = true;
                    break;
                }
            }

            if (shareParent) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} are siblings (brother/sister).", nameA, nameB);
                }
                return true;
            }
        }

        // 2. Skyrim Engine BGSRelationship checks
        auto* baseA = ActorSvc::GetActorBase(actorA);
        auto* baseB = ActorSvc::GetActorBase(actorB);
        if (!baseA || !baseB) return false;
        if (baseA == baseB) {
            if (outReason) {
                *outReason = fmt::format("{} and {} share the exact same NPC base form.", nameA, nameB);
            }
            return true;
        }

        constexpr RE::FormID kSiblingsAssocFormID = 0x000142C5;
        constexpr RE::FormID kParentChildAssocFormID = 0x000142C6;
        constexpr RE::FormID kAuntUncleAssocFormID = 0x000142C7;
        constexpr RE::FormID kCousinsAssocFormID = 0x000142C8;
        constexpr RE::FormID kGrandparentGrandchildAssocFormID = 0x0001E803;
        constexpr RE::FormID kGrandAuntUncleAssocFormID = 0x0001E80B;
        constexpr RE::FormID kGreatGrandparentAssocFormID = 0x0001E90B;

        auto checkDirectRel = [&](const RE::BGSRelationship* rel) -> bool {
            if (!rel || !rel->assocType) return false;
            uint32_t assocID = rel->assocType->GetFormID();
            if (assocID == kSiblingsAssocFormID) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} are siblings (brother/sister).", nameA, nameB);
                }
                return true;
            }
            if (assocID == kParentChildAssocFormID) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} have a parent/child relationship.", nameA, nameB);
                }
                return true;
            }
            if (assocID == kAuntUncleAssocFormID) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} have an aunt/uncle and niece/nephew relationship.", nameA, nameB);
                }
                return true;
            }
            if (assocID == kCousinsAssocFormID) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} are cousins.", nameA, nameB);
                }
                return true;
            }
            if (assocID == kGrandparentGrandchildAssocFormID) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} have a grandparent/grandchild relationship.", nameA, nameB);
                }
                return true;
            }
            if (assocID == kGrandAuntUncleAssocFormID) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} have a grand-aunt/uncle relationship.", nameA, nameB);
                }
                return true;
            }
            if (assocID == kGreatGrandparentAssocFormID) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} have a great-grandparent/grandchild relationship.", nameA, nameB);
                }
                return true;
            }
            return false;
        };

        if (auto* rel = RE::BGSRelationship::GetRelationship(baseA, baseB)) {
            if (checkDirectRel(rel)) return true;
        }

        if (baseA->relationships) {
            for (auto* rel : *baseA->relationships) {
                if (!rel) continue;
                if ((rel->npc1 == baseA && rel->npc2 == baseB) || (rel->npc2 == baseA && rel->npc1 == baseB)) {
                    if (checkDirectRel(rel)) return true;
                }
            }
        }

        // 3. Shared parents in engine relationships (identifies siblings without direct Sibling link)
        auto getEngineParents = [&](const RE::TESNPC* npc) -> std::unordered_set<const RE::TESNPC*> {
            std::unordered_set<const RE::TESNPC*> parents;
            if (!npc || !npc->relationships) return parents;
            for (auto* rel : *npc->relationships) {
                if (!rel || !rel->assocType) continue;
                if (rel->assocType->GetFormID() == kParentChildAssocFormID) {
                    if (rel->npc2 == npc && rel->npc1 && rel->npc1 != npc) {
                        parents.insert(rel->npc1);
                    }
                }
            }
            return parents;
        };

        auto parentsA = getEngineParents(baseA);
        auto parentsB = getEngineParents(baseB);

        if (parentsA.count(baseB) > 0 || parentsB.count(baseA) > 0) {
            if (outReason) {
                *outReason = fmt::format("{} and {} have a parent/child relationship.", nameA, nameB);
            }
            return true;
        }

        for (auto* p : parentsA) {
            if (parentsB.count(p) > 0) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} are siblings (share a parent).", nameA, nameB);
                }
                return true;
            }
        }

        return false;
    }

    // Status transitions
    bool NPCRelationshipManager::PromoteToEngaged(RE::FormID npcFormID, RE::FormID targetFormID, bool sendEvent, bool breakLovers) {
        RelationshipStatus oldStatus = GetRelationshipStatus(npcFormID, targetFormID);
        bool res = ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::Engaged, nullptr, sendEvent);
        if (res && breakLovers && !IsMainTier(oldStatus)) {
            if (!(npcFormID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) && !HasAnyOpenMainPair(npcFormID)) {
                BreakAllLoverConnections(npcFormID);
            }
            if (targetFormID != 0 && !(targetFormID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) && !HasAnyOpenMainPair(targetFormID)) {
                BreakAllLoverConnections(targetFormID);
            }
        }
        return res;
    }

    bool NPCRelationshipManager::PromoteToMarried(RE::FormID npcFormID, RE::FormID targetFormID, bool sendEvent, bool breakLovers) {
        RelationshipStatus oldStatus = GetRelationshipStatus(npcFormID, targetFormID);
        bool res = ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::Married,
                                  [this](RE::FormID id, RE::FormID target) {
                                      if (target == kPlayerFormID) {
                                          SpouseHierarchyManager::GetSingleton().OnSpouseAdded(id);
                                      } else if (id == kPlayerFormID) {
                                          SpouseHierarchyManager::GetSingleton().OnSpouseAdded(target);
                                      }
                                      EvaluateChildCustodyOnMarriage(id, target);
                                  },
                                  sendEvent);
        if (res && breakLovers && !IsMainTier(oldStatus)) {
            if (!(npcFormID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) && !HasAnyOpenMainPair(npcFormID)) {
                BreakAllLoverConnections(npcFormID);
            }
            if (targetFormID != 0 && !(targetFormID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) && !HasAnyOpenMainPair(targetFormID)) {
                BreakAllLoverConnections(targetFormID);
            }
        }
        return res;
    }

    bool NPCRelationshipManager::PromoteToDivorced(RE::FormID npcFormID, RE::FormID targetFormID, bool sendEvent) {
        return ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::Divorced,
                                  [this](RE::FormID id, RE::FormID target) {
                                      if (target == kPlayerFormID) {
                                          SpouseHierarchyManager::GetSingleton().OnSpouseRemoved(id);
                                          MARAS::SpouseAssetsService::GetSingleton().StopShareHouseWithPlayer(id);
                                      } else if (id == kPlayerFormID) {
                                          SpouseHierarchyManager::GetSingleton().OnSpouseRemoved(target);
                                          MARAS::SpouseAssetsService::GetSingleton().StopShareHouseWithPlayer(target);
                                      }
                                      EvaluateChildCustodyOnDivorce(id, target);
                                  },
                                  sendEvent);
    }

    bool NPCRelationshipManager::PromoteToJilted(RE::FormID npcFormID, RE::FormID targetFormID, bool sendEvent) {
        return ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::Jilted, nullptr, sendEvent);
    }

    bool NPCRelationshipManager::PromoteToDating(RE::FormID npcFormID, RE::FormID targetFormID, bool sendEvent, bool breakLovers) {
        RelationshipStatus oldStatus = GetRelationshipStatus(npcFormID, targetFormID);
        bool res = ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::Dating, nullptr, sendEvent);
        if (res && breakLovers && !IsMainTier(oldStatus)) {
            if (!(npcFormID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) && !HasAnyOpenMainPair(npcFormID)) {
                BreakAllLoverConnections(npcFormID);
            }
            if (targetFormID != 0 && !(targetFormID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) && !HasAnyOpenMainPair(targetFormID)) {
                BreakAllLoverConnections(targetFormID);
            }
        }
        return res;
    }

    bool NPCRelationshipManager::PromoteToLover(RE::FormID npcFormID, RE::FormID targetFormID, bool sendEvent) {
        return ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::Lover, nullptr, sendEvent);
    }

    bool NPCRelationshipManager::PromoteToExDating(RE::FormID npcFormID, RE::FormID targetFormID, bool sendEvent) {
        return ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::ExDating, nullptr, sendEvent);
    }

    bool NPCRelationshipManager::PromoteToExLover(RE::FormID npcFormID, RE::FormID targetFormID, bool sendEvent) {
        return ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::ExLover, nullptr, sendEvent);
    }

    bool NPCRelationshipManager::DemoteToCandidate(RE::FormID npcFormID, RE::FormID targetFormID, bool sendEvent) {
        return ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::Candidate, nullptr, sendEvent);
    }

    bool NPCRelationshipManager::Breakup(RE::FormID actorA, RE::FormID actorB, bool sendEvent) {
        if (actorA == 0 || actorB == 0 || actorA == actorB) {
            MARAS_LOG_WARN("NPCRelationshipManager::Breakup: Invalid actor arguments (actorA={:08X}, actorB={:08X})", actorA, actorB);
            return false;
        }

        RelationshipStatus currentStatus = GetRelationshipStatus(actorA, actorB);
        switch (currentStatus) {
            case RelationshipStatus::Married:
                return PromoteToDivorced(actorA, actorB, sendEvent);
            case RelationshipStatus::Engaged:
                return PromoteToJilted(actorA, actorB, sendEvent);
            case RelationshipStatus::Dating:
                return PromoteToExDating(actorA, actorB, sendEvent);
            case RelationshipStatus::Lover:
                return PromoteToExLover(actorA, actorB, sendEvent);
            default:
                MARAS_LOG_WARN("NPCRelationshipManager::Breakup: Cannot breakup actors {:08X} and {:08X} - current status is {} ({})",
                               actorA, actorB, static_cast<int>(currentStatus), Utils::RelationshipStatusToString(currentStatus));
                return false;
        }
    }

    bool NPCRelationshipManager::PromoteToStatusWith(RE::FormID npcFormID, RE::FormID targetFormID, RelationshipStatus status, bool sendEvent, bool breakLovers) {
        switch (status) {
            case RelationshipStatus::Candidate:
                return DemoteToCandidate(npcFormID, targetFormID, sendEvent);
            case RelationshipStatus::Dating:
                return PromoteToDating(npcFormID, targetFormID, sendEvent, breakLovers);
            case RelationshipStatus::Lover:
                return PromoteToLover(npcFormID, targetFormID, sendEvent);
            case RelationshipStatus::Engaged:
                return PromoteToEngaged(npcFormID, targetFormID, sendEvent, breakLovers);
            case RelationshipStatus::Married:
                return PromoteToMarried(npcFormID, targetFormID, sendEvent, breakLovers);
            case RelationshipStatus::Divorced:
                return PromoteToDivorced(npcFormID, targetFormID, sendEvent);
            case RelationshipStatus::Jilted:
                return PromoteToJilted(npcFormID, targetFormID, sendEvent);
            case RelationshipStatus::ExDating:
                return PromoteToExDating(npcFormID, targetFormID, sendEvent);
            case RelationshipStatus::ExLover:
                return PromoteToExLover(npcFormID, targetFormID, sendEvent);
            default:
                return false;
        }
    }

    // Bulk retrievals
    std::vector<RE::FormID> NPCRelationshipManager::GetAllRegisteredNPCs() const {
        return std::vector<RE::FormID>(allRegistered.begin(), allRegistered.end());
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllCandidates(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, RelationshipStatus::Candidate);
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllEngaged(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, RelationshipStatus::Engaged);
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllMarried(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, RelationshipStatus::Married);
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllDivorced(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, RelationshipStatus::Divorced);
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllJilted(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, RelationshipStatus::Jilted);
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllDating(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, RelationshipStatus::Dating);
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllLovers(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, RelationshipStatus::Lover);
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllExDating(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, RelationshipStatus::ExDating);
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllExLovers(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, RelationshipStatus::ExLover);
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetPartners(RE::FormID npcFormID, RelationshipStatus status) const {
        std::vector<RE::FormID> result;
        for (const auto& key : GetActorPairKeys(npcFormID)) {
            auto it = relationshipPairs.find(key);
            if (it != relationshipPairs.end() && it->second.status == status) {
                RE::FormID partner = key.GetOther(npcFormID);
                if (partner != 0) result.push_back(partner);
            }
        }
        return result;
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllPartners(RE::FormID npcFormID) const {
        std::vector<RE::FormID> result;
        for (const auto& key : GetActorPairKeys(npcFormID)) {
            auto it = relationshipPairs.find(key);
            if (it != relationshipPairs.end() &&
                it->second.status != RelationshipStatus::Unknown &&
                it->second.status != RelationshipStatus::Candidate) {
                RE::FormID partner = key.GetOther(npcFormID);
                if (partner != 0) result.push_back(partner);
            }
        }
        return result;
    }

    // Data access
    const NPCProfileData* NPCRelationshipManager::GetNPCProfile(RE::FormID npcFormID) const {
        auto it = npcProfiles.find(npcFormID);
        return (it != npcProfiles.end()) ? &it->second : nullptr;
    }

    const NPCRelationshipData* NPCRelationshipManager::GetNPCData(RE::FormID npcFormID, RE::FormID targetFormID) const {
        RelationshipPairKey key(npcFormID, targetFormID);
        auto it = relationshipPairs.find(key);
        return (it != relationshipPairs.end()) ? &it->second : nullptr;
    }

    RelationshipStatus NPCRelationshipManager::GetRelationshipStatus(RE::FormID npcFormID, RE::FormID targetFormID) const {
        auto data = GetNPCData(npcFormID, targetFormID);
        return data ? data->status : RelationshipStatus::Unknown;
    }

    SocialClass NPCRelationshipManager::GetSocialClass(RE::FormID npcFormID) const {
        auto profile = GetNPCProfile(npcFormID);
        if (profile) return profile->socialClass;
        if (auto dec = DeceasedRegistry::GetSingleton().FindRecord(npcFormID)) {
            return dec->socialClass;
        }
        return SocialClass::Outcast;
    }

    SkillType NPCRelationshipManager::GetSkillType(RE::FormID npcFormID) const {
        auto profile = GetNPCProfile(npcFormID);
        if (profile) return profile->skillType;
        if (auto dec = DeceasedRegistry::GetSingleton().FindRecord(npcFormID)) {
            return dec->skillType;
        }
        return SkillType::Warrior;
    }

    Temperament NPCRelationshipManager::GetTemperament(RE::FormID npcFormID) const {
        auto profile = GetNPCProfile(npcFormID);
        if (profile) return profile->temperament;
        if (auto dec = DeceasedRegistry::GetSingleton().FindRecord(npcFormID)) {
            return dec->temperament;
        }
        return Temperament::Proud;
    }

    // Home Marker Management
    bool NPCRelationshipManager::SetHomeMarker(RE::FormID npcFormID, RE::FormID markerFormID) {
        if (!EnsureRegistered(npcFormID)) {
            MARAS_LOG_WARN("SetHomeMarker: NPC {:08X} not registered", npcFormID);
            return false;
        }

        auto& profile = npcProfiles[npcFormID];
        profile.homeMarker = markerFormID;

        if (markerFormID != 0) {
            SetLinkedRefForHomeMarker(npcFormID, markerFormID);
            MARAS_LOG_DEBUG("Set home marker {:08X} for NPC {:08X} and created linked ref", markerFormID, npcFormID);
        }

        return true;
    }

    std::optional<RE::FormID> NPCRelationshipManager::GetHomeMarker(RE::FormID npcFormID) const {
        auto profile = GetNPCProfile(npcFormID);
        return profile ? profile->homeMarker : std::nullopt;
    }

    RE::TESObjectREFR* NPCRelationshipManager::GetOrCreateHomeMarker(RE::Actor* npcActor) {
        if (!npcActor) return nullptr;

        RE::FormID npcFormID = npcActor->GetFormID();
        auto markerOpt = GetHomeMarker(npcFormID);
        if (markerOpt.has_value() && markerOpt.value() != 0) {
            if (auto* existingMarker = RE::TESForm::LookupByID<RE::TESObjectREFR>(markerOpt.value())) {
                SetLinkedRefForHomeMarker(npcFormID, markerOpt.value());
                return existingMarker;
            }
        }

        auto* homeMarkerStatic = FormCache::GetSingleton().GetHomeSandboxMarkerStatic();
        if (!homeMarkerStatic) {
            MARAS_LOG_WARN("GetOrCreateHomeMarker: homeMarkerStatic form not found");
            return nullptr;
        }

        auto markerRef = npcActor->PlaceObjectAtMe(homeMarkerStatic, true);
        if (!markerRef) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                markerRef = player->PlaceObjectAtMe(homeMarkerStatic, true);
            }
        }
        if (!markerRef) {
            MARAS_LOG_WARN("GetOrCreateHomeMarker: PlaceObjectAtMe failed for NPC {:08X}", npcFormID);
            return nullptr;
        }

        RE::FormID markerID = markerRef->GetFormID();
        SetHomeMarker(npcFormID, markerID);

        MARAS_LOG_INFO("GetOrCreateHomeMarker: Placed new home marker {:08X} for NPC {:08X}", markerID, npcFormID);
        return markerRef.get();
    }

    bool NPCRelationshipManager::AddTrackedKeyword(RE::FormID npcFormID, RE::FormID keywordFormID) {
        auto it = npcProfiles.find(npcFormID);
        if (it == npcProfiles.end()) {
            return false;
        }
        it->second.addedKeywords.insert(keywordFormID);
        return true;
    }

    bool NPCRelationshipManager::RemoveTrackedKeyword(RE::FormID npcFormID, RE::FormID keywordFormID) {
        auto it = npcProfiles.find(npcFormID);
        if (it == npcProfiles.end()) {
            return false;
        }
        it->second.addedKeywords.erase(keywordFormID);
        return true;
    }

    // Status change history management
    const std::vector<StatusChangeRecord>& NPCRelationshipManager::GetStatusHistory(RE::FormID npcFormID, RE::FormID targetFormID) const {
        static const std::vector<StatusChangeRecord> emptyHistory;
        auto data = GetNPCData(npcFormID, targetFormID);
        return data ? data->statusHistory : emptyHistory;
    }

    std::optional<StatusChangeRecord> NPCRelationshipManager::GetStatusHistoryEntry(RE::FormID npcFormID, RE::FormID targetFormID, std::size_t index) const {
        auto data = GetNPCData(npcFormID, targetFormID);
        if (!data || index >= data->statusHistory.size()) {
            return std::nullopt;
        }
        return data->statusHistory[index];
    }

    size_t NPCRelationshipManager::GetStatusHistoryCount(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetStatusHistory(npcFormID, targetFormID).size();
    }

    void NPCRelationshipManager::ClearStatusHistory(RE::FormID npcFormID, RE::FormID targetFormID) {
        RelationshipPairKey key(npcFormID, targetFormID);
        auto it = relationshipPairs.find(key);
        if (it != relationshipPairs.end()) {
            it->second.statusHistory.clear();
        }
    }

    // Statistics
    size_t NPCRelationshipManager::GetTotalRegisteredCount() const { return allRegistered.size(); }

    size_t NPCRelationshipManager::GetCandidateCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, RelationshipStatus::Candidate);
    }

    size_t NPCRelationshipManager::GetEngagedCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, RelationshipStatus::Engaged);
    }

    size_t NPCRelationshipManager::GetMarriedCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, RelationshipStatus::Married);
    }

    size_t NPCRelationshipManager::GetDivorcedCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, RelationshipStatus::Divorced);
    }

    size_t NPCRelationshipManager::GetJiltedCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, RelationshipStatus::Jilted);
    }

    size_t NPCRelationshipManager::GetExDatingCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, RelationshipStatus::ExDating);
    }

    size_t NPCRelationshipManager::GetExLoverCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, RelationshipStatus::ExLover);
    }

    // Override management
    const Utils::NPCOverrideData* NPCRelationshipManager::FindOverrideData(RE::FormID npcFormID) const {
        auto it = npcOverrides.find(npcFormID);
        if (it != npcOverrides.end()) {
            return &it->second;
        }

        auto npcBase = ActorSvc::GetActorBase(npcFormID);
        if (npcBase) {
            RE::FormID baseFormID = npcBase->GetFormID();
            if (baseFormID != npcFormID) {
                auto baseIt = npcOverrides.find(baseFormID);
                if (baseIt != npcOverrides.end()) {
                    return &baseIt->second;
                }
            }
        }

        return nullptr;
    }

    bool NPCRelationshipManager::LoadOverridesFromFolder(const std::string& folderPath) {
        MARAS_LOG_INFO("Loading NPC type overrides from folder: {}", folderPath);
        npcOverrides.clear();
        return Utils::JsonOverrideLoader::LoadOverridesFromFolder(folderPath, npcOverrides);
    }

    bool NPCRelationshipManager::HasSocialClassOverride(RE::FormID npcFormID) const {
        auto data = FindOverrideData(npcFormID);
        return data && data->HasSocialClassOverride();
    }

    bool NPCRelationshipManager::HasSkillTypeOverride(RE::FormID npcFormID) const {
        auto data = FindOverrideData(npcFormID);
        return data && data->HasSkillTypeOverride();
    }

    bool NPCRelationshipManager::HasTemperamentOverride(RE::FormID npcFormID) const {
        auto data = FindOverrideData(npcFormID);
        return data && data->HasTemperamentOverride();
    }

    std::optional<std::string> NPCRelationshipManager::GetSocialClassOverride(RE::FormID npcFormID) const {
        auto data = FindOverrideData(npcFormID);
        return (data && data->HasSocialClassOverride()) ? std::make_optional(data->socialClass) : std::nullopt;
    }

    std::optional<std::string> NPCRelationshipManager::GetSkillTypeOverride(RE::FormID npcFormID) const {
        auto data = FindOverrideData(npcFormID);
        return (data && data->HasSkillTypeOverride()) ? std::make_optional(data->skillType) : std::nullopt;
    }

    std::optional<std::string> NPCRelationshipManager::GetTemperamentOverride(RE::FormID npcFormID) const {
        auto data = FindOverrideData(npcFormID);
        return (data && data->HasTemperamentOverride()) ? std::make_optional(data->temperament) : std::nullopt;
    }

    size_t NPCRelationshipManager::GetOverrideCount() const { return npcOverrides.size(); }

    Utils::JsonOverrideLoader::LoadStatistics NPCRelationshipManager::GetLastOverrideLoadStats() const {
        return Utils::JsonOverrideLoader::GetLastLoadStatistics();
    }

    // Type Determination Methods
    SocialClass NPCRelationshipManager::DetermineSocialClass(RE::FormID npcFormID) {
        return NPCTypeDeterminer::DetermineSocialClass(npcFormID,
                                                       [this](RE::FormID id) { return GetSocialClassOverride(id); });
    }

    SkillType NPCRelationshipManager::DetermineSkillType(RE::FormID npcFormID) {
        return NPCTypeDeterminer::DetermineSkillType(npcFormID,
                                                     [this](RE::FormID id) { return GetSkillTypeOverride(id); });
    }

    Temperament NPCRelationshipManager::DetermineTemperament(RE::FormID npcFormID) {
        return NPCTypeDeterminer::DetermineTemperament(
            npcFormID, [this](RE::FormID id) { return GetTemperamentOverride(id); },
            [this](RE::FormID id) { return GetSocialClass(id); }, [this](RE::FormID id) { return GetSkillType(id); });
    }

    // Faction Management Methods
    bool NPCRelationshipManager::AddToFaction(RE::FormID npcFormID, RE::TESFaction* faction, std::int8_t rank) {
        if (!faction) return false;
        auto actor = NPCIndex::GetSingleton().GetActorFromFormId(npcFormID);
        if (!actor) return false;
        FactionService::InternalFactionScope scope;
        Utils::AddToFaction(actor, faction, rank);
        return true;
    }

    bool NPCRelationshipManager::AddToSocialClassFaction(RE::FormID npcFormID, std::int8_t rank) {
        auto faction = FormCache::GetSingleton().GetSpouseSocialClassFaction();
        return AddToFaction(npcFormID, faction, rank);
    }

    bool NPCRelationshipManager::AddToSkillTypeFaction(RE::FormID npcFormID, std::int8_t rank) {
        auto faction = FormCache::GetSingleton().GetSpouseSkillTypeFaction();
        return AddToFaction(npcFormID, faction, rank);
    }

    bool NPCRelationshipManager::AddToTemperamentFaction(RE::FormID npcFormID, std::int8_t rank) {
        auto faction = FormCache::GetSingleton().GetSpouseTemperamentFaction();
        return AddToFaction(npcFormID, faction, rank);
    }

    namespace {
        void SafeAddToFaction(RE::Actor* actor, RE::TESFaction* faction) {
            if (!actor || !faction) return;
            MARAS_LOG_INFO("Adding faction {:08X} to actor {}({:08X})", faction->GetFormID(), actor->GetName(), actor->GetFormID());
            Utils::AddToFaction(actor, faction, 0);
        }

        void SafeRemoveFromFaction(RE::Actor* actor, RE::TESFaction* faction) {
            if (!actor || !faction) return;
            MARAS_LOG_INFO("Removing faction {:08X} from actor {}({:08X})", faction->GetFormID(), actor->GetName(), actor->GetFormID());
            Utils::RemoveFromFaction(actor, faction);
        }

        void SafeRemoveKeyword(RE::Actor* actor, RE::BGSKeyword* keyword) {
            if (!actor || !keyword) return;
            MARAS_LOG_INFO("Removing keyword {:08X} from actor {}({:08X})", keyword->GetFormID(), actor->GetName(), actor->GetFormID());
            auto actorBase = ActorSvc::GetActorBase(actor->GetFormID());
            if (actorBase && actorBase->HasKeyword(keyword)) {
                actorBase->RemoveKeyword(keyword);
            }
        }
    }  // namespace

    void NPCRelationshipManager::ManageFactions(RE::FormID npcFormID, RE::FormID targetFormID, RelationshipStatus status) {
        if (npcFormID == kPlayerFormID || npcFormID == 0) return;

        auto actor = NPCIndex::GetSingleton().GetActorFromFormId(npcFormID);
        if (!actor) return;

        FactionService::InternalFactionScope scope;
        auto& cache = FormCache::GetSingleton();

        if (targetFormID == kPlayerFormID) {
            switch (status) {
                case RelationshipStatus::Candidate:
                    SafeAddToFaction(actor, cache.GetMarriagePotentialFaction());
                    break;
                case RelationshipStatus::Dating:
                case RelationshipStatus::Lover:
                    SafeRemoveKeyword(actor, cache.GetIgnoreProposeKeyword());
                    break;
                case RelationshipStatus::Engaged:
                    SafeAddToFaction(actor, cache.GetMarriageAskedFaction());
                    SafeAddToFaction(actor, cache.GetCourtingFaction());
                    SafeRemoveKeyword(actor, cache.GetIgnoreProposeKeyword());
                    break;
                case RelationshipStatus::Married:
                    SafeRemoveFromFaction(actor, cache.GetMarriageAskedFaction());
                    SafeRemoveFromFaction(actor, cache.GetCourtingFaction());
                    SafeRemoveFromFaction(actor, cache.GetPotentialHirelingFaction());
                    SafeAddToFaction(actor, cache.GetMarriedFaction());
                    SafeAddToFaction(actor, cache.GetPlayerFaction());
                    SafeAddToFaction(actor, cache.GetPlayerBedOwnershipFaction());
                    SafeRemoveKeyword(actor, cache.GetIgnoreProposeKeyword());
                    if (!Settings::GetSingleton().GetEnablePolygamy()) {
                        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                            SafeAddToFaction(player, cache.GetMarriedFaction());
                        }
                    }
                    break;
                case RelationshipStatus::Divorced:
                    SafeRemoveFromFaction(actor, cache.GetMarriedFaction());
                    SafeRemoveFromFaction(actor, cache.GetPlayerFaction());
                    SafeRemoveFromFaction(actor, cache.GetPlayerBedOwnershipFaction());
                    SafeRemoveKeyword(actor, cache.GetIgnoreProposeKeyword());
                    if (GetMarriedCount(kPlayerFormID) == 0) {
                        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                            SafeRemoveFromFaction(player, cache.GetMarriedFaction());
                        }
                    }
                    break;
                case RelationshipStatus::Jilted:
                    SafeRemoveFromFaction(actor, cache.GetMarriageAskedFaction());
                    SafeRemoveFromFaction(actor, cache.GetCourtingFaction());
                    SafeRemoveKeyword(actor, cache.GetIgnoreProposeKeyword());
                    break;
                case RelationshipStatus::Unknown:
                default:
                    break;
            }
        } else {
            // NPC x NPC relationship - strictly ensure player marriage factions are NOT applied!
            switch (status) {
                case RelationshipStatus::Married:
                    SafeRemoveFromFaction(actor, cache.GetMarriageAskedFaction());
                    SafeRemoveFromFaction(actor, cache.GetCourtingFaction());
                    SafeRemoveFromFaction(actor, cache.GetMarriedFaction());
                    SafeRemoveFromFaction(actor, cache.GetPlayerFaction());
                    SafeRemoveFromFaction(actor, cache.GetPlayerBedOwnershipFaction());
                    SafeRemoveKeyword(actor, cache.GetIgnoreProposeKeyword());
                    if (!Settings::GetSingleton().GetEnablePolygamy()) {
                        SafeRemoveFromFaction(actor, cache.GetMarriagePotentialFaction());
                    }
                    break;
                case RelationshipStatus::Divorced:
                    SafeRemoveFromFaction(actor, cache.GetMarriedFaction());
                    SafeRemoveFromFaction(actor, cache.GetPlayerFaction());
                    SafeRemoveFromFaction(actor, cache.GetPlayerBedOwnershipFaction());
                    SafeRemoveKeyword(actor, cache.GetIgnoreProposeKeyword());
                    SafeAddToFaction(actor, cache.GetMarriagePotentialFaction());
                    break;
                case RelationshipStatus::Engaged:
                    SafeRemoveFromFaction(actor, cache.GetMarriageAskedFaction());
                    SafeRemoveFromFaction(actor, cache.GetCourtingFaction());
                    SafeRemoveKeyword(actor, cache.GetIgnoreProposeKeyword());
                    break;
                case RelationshipStatus::Candidate:
                    SafeAddToFaction(actor, cache.GetMarriagePotentialFaction());
                    break;
                default:
                    break;
            }
        }
    }

    bool NPCRelationshipManager::SetSocialClass(RE::FormID npcFormID, std::int8_t socialClass) {
        if (socialClass < 0 || socialClass >= static_cast<std::int8_t>(SocialClass::_Count)) return false;
        if (!EnsureRegistered(npcFormID)) return false;
        auto& profile = npcProfiles[npcFormID];
        profile.socialClass = static_cast<SocialClass>(socialClass);
        AddToSocialClassFaction(npcFormID, socialClass);
        return true;
    }

    bool NPCRelationshipManager::SetSkillType(RE::FormID npcFormID, std::int8_t skillType) {
        if (skillType < 0 || skillType >= static_cast<std::int8_t>(SkillType::_Count)) return false;
        if (!EnsureRegistered(npcFormID)) return false;
        auto& profile = npcProfiles[npcFormID];
        profile.skillType = static_cast<SkillType>(skillType);
        AddToSkillTypeFaction(npcFormID, skillType);
        return true;
    }

    bool NPCRelationshipManager::SetTemperament(RE::FormID npcFormID, std::int8_t temperament) {
        if (temperament < 0 || temperament >= static_cast<std::int8_t>(Temperament::_Count)) return false;
        if (!EnsureRegistered(npcFormID)) return false;
        auto& profile = npcProfiles[npcFormID];
        profile.temperament = static_cast<Temperament>(temperament);
        AddToTemperamentFaction(npcFormID, temperament);
        return true;
    }

    bool NPCRelationshipManager::EnsureRegistered(RE::FormID inputFormID) {
        MARAS_LOG_TRACE("EnsureRegistered(FormID={:08X}) entered", inputFormID);
        if (inputFormID == kPlayerFormID || inputFormID == 0 || inputFormID == 0x07) return true;

        auto& npcIndex = NPCIndex::GetSingleton();
        RE::FormID npcFormID = npcIndex.GetRefID(inputFormID);
        if (npcFormID == 0) {
            npcFormID = inputFormID;
        }

        if (npcFormID == kPlayerFormID || npcFormID == 0 || npcFormID == 0x07) return true;
        if (IsRegistered(npcFormID)) {
            MARAS_LOG_TRACE("EnsureRegistered: {:08X} is already registered", npcFormID);
            return true;
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        if (!ActorSvc::IsValid(npcFormID)) {
            MARAS_LOG_WARN("EnsureRegistered: Cannot find valid actor/base for FormID {:08X}", npcFormID);
            return false;
        }

        SocialClass socialClass = DetermineSocialClass(npcFormID);
        SkillType skillType = DetermineSkillType(npcFormID);
        Temperament temperament = [this, npcFormID, socialClass, skillType]() {
            if (auto ov = GetTemperamentOverride(npcFormID); ov.has_value()) {
                return Utils::StringToTemperament(ov.value());
            }
            return NPCTypeDeterminer::ComputeTemperament(socialClass, skillType);
        }();

        allRegistered.insert(npcFormID);
        MARAS_LOG_INFO("EnsureRegistered: inserted {:08X} into allRegistered", npcFormID);

        try {
            npcProfiles[npcFormID] = NPCProfileData(npcFormID, socialClass, skillType, temperament);
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("Exception while storing NPC profile for {:08X}: {}", npcFormID, e.what());
            allRegistered.erase(npcFormID);
            return false;
        }

        try {
            AddToSocialClassFaction(npcFormID, static_cast<std::int8_t>(socialClass));
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("AddToSocialClassFaction exception for {:08X}: {}", npcFormID, e.what());
        }
        try {
            AddToSkillTypeFaction(npcFormID, static_cast<std::int8_t>(skillType));
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("AddToSkillTypeFaction exception for {:08X}: {}", npcFormID, e.what());
        }
        try {
            AddToTemperamentFaction(npcFormID, static_cast<std::int8_t>(temperament));
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("AddToTemperamentFaction exception for {:08X}: {}", npcFormID, e.what());
        }
        try {
            UpdateCanFormNonCheatingMainFaction(npcFormID);
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("UpdateCanFormNonCheatingMainFaction exception for {:08X}: {}", npcFormID, e.what());
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        MARAS_LOG_INFO("Registered NPC {} ({:08X}) profile in {} microseconds (SC: {}, ST: {}, T: {})",
                       ActorSvc::GetName(npcFormID), npcFormID, duration.count(),
                       Utils::SocialClassToString(socialClass), Utils::SkillTypeToString(skillType),
                       Utils::TemperamentToString(temperament));

        RE::FormID baseID = npcIndex.GetBaseID(npcFormID);
        if (baseID != 0 && baseID != npcFormID) {
            TryResolvePendingRelationships(baseID, npcFormID);
        }

        RecalculateAndUpdateGlobals();
        return true;
    }

    bool NPCRelationshipManager::HasMarasMaritalHistory(RE::FormID npcFormID) const {
        for (const auto& [key, data] : relationshipPairs) {
            if (key.Contains(npcFormID)) {
                if (data.status == RelationshipStatus::Engaged ||
                    data.status == RelationshipStatus::Married ||
                    data.status == RelationshipStatus::Divorced ||
                    data.status == RelationshipStatus::Jilted) {
                    return true;
                }
                for (const auto& entry : data.statusHistory) {
                    if (entry.fromStatus == RelationshipStatus::Engaged || entry.fromStatus == RelationshipStatus::Married ||
                        entry.fromStatus == RelationshipStatus::Divorced || entry.fromStatus == RelationshipStatus::Jilted ||
                        entry.toStatus == RelationshipStatus::Engaged || entry.toStatus == RelationshipStatus::Married ||
                        entry.toStatus == RelationshipStatus::Divorced || entry.toStatus == RelationshipStatus::Jilted) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    RelationshipStatus NPCRelationshipManager::GetOriginalRelationshipStatus(RE::FormID actorA, RE::FormID actorB) const {
        RelationshipPairKey key(actorA, actorB);
        auto it = relationshipPairs.find(key);
        if (it != relationshipPairs.end()) {
            return it->second.originalStatus;
        }
        return RelationshipStatus::Unknown;
    }

    bool NPCRelationshipManager::SetOriginalRelationshipStatus(RE::FormID actorA, RE::FormID actorB, RelationshipStatus status) {
        EnsureRegistered(actorA);
        EnsureRegistered(actorB);
        auto& pair = GetOrCreatePairData(actorA, actorB);
        pair.originalStatus = status;

        MARAS_LOG_INFO("Set original relationship status between {:08X} and {:08X} to {}",
                       actorA, actorB, Utils::RelationshipStatusToString(status));
        return true;
    }

    bool NPCRelationshipManager::ApplyImportedVanillaStatus(RE::FormID actorA, RE::FormID actorB,
                                                            RelationshipStatus vanillaStatus, RelationshipStatus targetStatus) {
        // Divorced / ExDating / ExLover are only reachable from Married / Dating / Lover. An imported vanilla
        // relationship that was superseded (e.g. by the Player) must pass through its vanilla state first.
        if (targetStatus == RelationshipStatus::Divorced || targetStatus == RelationshipStatus::ExDating ||
            targetStatus == RelationshipStatus::ExLover) {
            GetOrCreatePairData(actorA, actorB).status = vanillaStatus;
        }
        return PromoteToStatusWith(actorA, actorB, targetStatus, /*sendEvent=*/false, /*breakLovers=*/false);
    }

    void NPCRelationshipManager::ScanExistingRelationships() {
        auto startTime = std::chrono::steady_clock::now();
        MARAS_LOG_INFO("Starting scanning of existing in-game relationships (Courting, Spouse, Lover, Parent-Child)...");

        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            MARAS_LOG_ERROR("ScanExistingRelationships: TESDataHandler unavailable");
            return;
        }

        constexpr RE::FormID kCourtingAssocFormID    = 0x0001EE23;
        constexpr RE::FormID kParentChildAssocFormID = 0x000142C6;
        constexpr RE::FormID kSpouseAssocFormID      = 0x000142CA;

        const auto& relArray = dataHandler->GetFormArray<RE::BGSRelationship>();
        auto& npcIndex = NPCIndex::GetSingleton();

        // Buffer for collecting and merging child-parent records during scan
        struct ScannedChildEntry {
            RE::FormID childBaseID{ 0 };
            RE::FormID parentABaseID{ 0 };
            RE::FormID parentBBaseID{ 0 };
            bool isAdopted{ false };
        };
        std::vector<ScannedChildEntry> scannedChildren;

        // Pre-collect set of all vanilla relationship pair keys
        std::unordered_set<RelationshipPairKey, RelationshipPairKeyHash> vanillaPairKeys;
        for (auto* rel : relArray) {
            if (!rel || !rel->npc1 || !rel->npc2) continue;
            RE::FormID id1 = npcIndex.GetRefID(rel->npc1->GetFormID());
            RE::FormID id2 = npcIndex.GetRefID(rel->npc2->GetFormID());
            if (id1 != 0 && id2 != 0) {
                vanillaPairKeys.emplace(id1, id2);
            }
        }

        // Check if an actor has an existing MARAS relationship (not Candidate/Unknown) with a non-vanilla partner (e.g. Player)
        auto hasNonVanillaMarasRelationship = [this, &vanillaPairKeys](RE::FormID npcFormID) -> bool {
            for (const auto& key : GetActorPairKeys(npcFormID)) {
                auto it = relationshipPairs.find(key);
                if (it != relationshipPairs.end() &&
                    it->second.status != RelationshipStatus::Candidate &&
                    it->second.status != RelationshipStatus::Unknown) {
                    // If this pair is NOT a vanilla relationship (e.g. Player or custom mod partner)
                    if (vanillaPairKeys.find(key) == vanillaPairKeys.end()) {
                        return true;
                    }
                }
            }
            return false;
        };

        // Check if an actor is in an active, non-open main-tier relationship (Dating/Engaged/Married) with a non-vanilla partner
        auto hasNonVanillaMainTierRelationship = [this, &vanillaPairKeys](RE::FormID npcFormID) -> bool {
            for (const auto& key : GetActorPairKeys(npcFormID)) {
                auto it = relationshipPairs.find(key);
                if (it != relationshipPairs.end() && IsMainTier(it->second.status) && !it->second.isOpen &&
                    vanillaPairKeys.find(key) == vanillaPairKeys.end()) {
                    return true;
                }
            }
            return false;
        };

        size_t scannedCount = 0;
        size_t importedCount = 0;

        for (auto* rel : relArray) {
            if (!rel) continue;
            scannedCount++;

            auto* npc1Base = rel->npc1;
            auto* npc2Base = rel->npc2;
            if (!npc1Base || !npc2Base) continue;

            RE::FormID baseID1 = npc1Base->GetFormID();
            RE::FormID baseID2 = npc2Base->GetFormID();

            auto entry1 = npcIndex.FindByFormID(baseID1);
            auto entry2 = npcIndex.FindByFormID(baseID2);

            // Check if either participant is a child (uses cached isChild flag from index)
            bool isChild1 = entry1 ? entry1->isChild : (npc1Base->GetRace() && npc1Base->GetRace()->data.flags.any(RE::RACE_DATA::Flag::kChild));
            bool isChild2 = entry2 ? entry2->isChild : (npc2Base->GetRace() && npc2Base->GetRace()->data.flags.any(RE::RACE_DATA::Flag::kChild));

            if (isChild1 || isChild2) {
                // Strictly require an adult-child pair (isChild1 != isChild2) AND a Parent/Child association type (kParentChildAssocFormID)
                if (isChild1 != isChild2 && rel->assocType && rel->assocType->GetFormID() == kParentChildAssocFormID) {
                    RE::FormID cBaseID = (isChild1 ? npc1Base : npc2Base)->GetFormID();
                    RE::FormID pBaseID = (isChild1 ? npc2Base : npc1Base)->GetFormID();

                    auto it = std::find_if(scannedChildren.begin(), scannedChildren.end(),
                                           [cBaseID](const ScannedChildEntry& e) { return e.childBaseID == cBaseID; });
                    if (it == scannedChildren.end()) {
                        scannedChildren.push_back({ cBaseID, pBaseID, 0, false });
                    } else if (it->parentABaseID != pBaseID && it->parentBBaseID == 0) {
                        it->parentBBaseID = pBaseID;
                    }
                }
                // Skip adult relationship processing for child NPCs
                continue;
            }

            // Determine vanilla relationship type
            RelationshipStatus vanillaStatus = RelationshipStatus::Unknown;

            if (rel->assocType) {
                RE::FormID assocID = rel->assocType->GetFormID();
                if (assocID == kCourtingAssocFormID) {
                    vanillaStatus = RelationshipStatus::Dating;
                } else if (assocID == kSpouseAssocFormID) {
                    vanillaStatus = RelationshipStatus::Married;
                }
            }

            if (vanillaStatus == RelationshipStatus::Unknown) {
                // Check if rank 4 lover (kLover = 0 in CommonLib RELATIONSHIP_LEVEL enum)
                if (rel->level.get() == RE::BGSRelationship::RELATIONSHIP_LEVEL::kLover) {
                    vanillaStatus = RelationshipStatus::Lover;
                }
            }

            // Only process Courting, Spouse, or Lover relationships
            if (vanillaStatus == RelationshipStatus::Unknown) {
                continue;
            }

            RE::FormID refID1 = entry1 ? entry1->refFormID : npcIndex.GetRefID(baseID1);
            RE::FormID refID2 = entry2 ? entry2->refFormID : npcIndex.GetRefID(baseID2);

            bool isRef1 = (refID1 != 0 && refID1 != baseID1);
            bool isRef2 = (refID2 != 0 && refID2 != baseID2);

            // Check if either actor is currently in a MARAS relationship with a non-vanilla partner (e.g. Player)
            bool hasNonVanillaHistory = hasNonVanillaMarasRelationship(isRef1 ? refID1 : baseID1) ||
                                         hasNonVanillaMarasRelationship(isRef2 ? refID2 : baseID2);

            RelationshipStatus targetMarasStatus = RelationshipStatus::Unknown;
            if (!hasNonVanillaHistory) {
                targetMarasStatus = vanillaStatus;
            } else {
                if (vanillaStatus == RelationshipStatus::Dating) {
                    targetMarasStatus = RelationshipStatus::ExDating;
                } else if (vanillaStatus == RelationshipStatus::Married) {
                    targetMarasStatus = RelationshipStatus::Divorced;
                } else if (vanillaStatus == RelationshipStatus::Lover) {
                    // A vanilla lover whose partner is committed to a non-vanilla main partner (e.g. Player)
                    // is broken up, mirroring BreakAllLoverConnections on a MARAS marriage/engagement/dating.
                    const bool committedElsewhere = hasNonVanillaMainTierRelationship(isRef1 ? refID1 : baseID1) ||
                                                    hasNonVanillaMainTierRelationship(isRef2 ? refID2 : baseID2);
                    targetMarasStatus = committedElsewhere ? RelationshipStatus::ExLover : RelationshipStatus::Lover;
                }
            }

            // If either NPC is not yet spawned in world as an Actor ref, defer the relationship!
            if (!isRef1 || !isRef2) {
                RE::FormID r1 = isRef1 ? refID1 : 0;
                RE::FormID r2 = isRef2 ? refID2 : 0;

                if (!isRef1) {
                    AddPendingRelationship(baseID1, PendingRelationshipData(baseID2, r2, targetMarasStatus, vanillaStatus));
                }
                if (!isRef2) {
                    AddPendingRelationship(baseID2, PendingRelationshipData(baseID1, r1, targetMarasStatus, vanillaStatus));
                }
                MARAS_LOG_INFO("ScanExistingRelationships: Deferred relationship between Base {:08X} (ref {:08X}) and Base {:08X} (ref {:08X})",
                               baseID1, r1, baseID2, r2);
                continue;
            }

            // Both are placed Actor Ref IDs: process immediately
            EnsureRegistered(refID1);
            EnsureRegistered(refID2);

            SetOriginalRelationshipStatus(refID1, refID2, vanillaStatus);

            auto currentStatus = GetRelationshipStatus(refID1, refID2);
            if (currentStatus == RelationshipStatus::Candidate || currentStatus == RelationshipStatus::Unknown) {
                if (targetMarasStatus != RelationshipStatus::Unknown) {
                    ApplyImportedVanillaStatus(refID1, refID2, vanillaStatus, targetMarasStatus);

                    auto& pairData = GetOrCreatePairData(refID1, refID2);
                    RE::FormID defaultInit = DetermineDefaultInitiator(refID1, refID2);
                    for (auto& hist : pairData.statusHistory) {
                        hist.timestamp = -1.0f;
                        hist.fromStatus = RelationshipStatus::Unknown;
                        hist.initiatorFormID = defaultInit;
                    }

                    MARAS_LOG_INFO("ScanExistingRelationships: Assigned MARAS status {} (original {}) between {} ({:08X}) and {} ({:08X})",
                                   Utils::RelationshipStatusToString(targetMarasStatus),
                                   Utils::RelationshipStatusToString(vanillaStatus),
                                   ActorSvc::GetName(refID1), refID1,
                                   ActorSvc::GetName(refID2), refID2);
                    importedCount++;
                }
            }
        }

        // 2. Process collected child-parent relationships
        size_t childImportedCount = 0;
        for (const auto& entry : scannedChildren) {
            RE::FormID childRefID = npcIndex.GetRefID(entry.childBaseID);
            bool hasChildRef = (childRefID != 0 && childRefID != entry.childBaseID);
            if (!hasChildRef) {
                childRefID = 0;
            }

            RE::FormID parentARefID = entry.parentABaseID != 0 ? npcIndex.GetRefID(entry.parentABaseID) : 0;
            if (parentARefID == entry.parentABaseID && entry.parentABaseID != kPlayerFormID) {
                parentARefID = 0;
            }

            RE::FormID parentBBaseID = entry.parentBBaseID;
            RE::FormID parentBRefID = parentBBaseID != 0 ? npcIndex.GetRefID(parentBBaseID) : 0;
            if (parentBRefID == parentBBaseID && parentBBaseID != kPlayerFormID) {
                parentBRefID = 0;
            }

            // If a second ParentChild BGSRelationship record was not present, look for a romantic partner of parent A
            if (parentBBaseID == 0 && parentBRefID == 0) {
                RE::FormID lookupParentA = parentARefID != 0 ? parentARefID : entry.parentABaseID;
                for (const auto& [key, data] : relationshipPairs) {
                    if (key.Contains(lookupParentA)) {
                        if (data.status == RelationshipStatus::Married || data.status == RelationshipStatus::Dating || data.status == RelationshipStatus::Lover) {
                            parentBRefID = key.GetOther(lookupParentA);
                            parentBBaseID = npcIndex.GetBaseID(parentBRefID);
                            break;
                        }
                    }
                }
            }

            // Assign bioMotherID and bioFatherID strictly by actor sex, never by slot position
            RE::FormID bioMotherID = 0;
            RE::FormID bioFatherID = 0;
            if (!entry.isAdopted) {
                auto assignBioBySex = [&](RE::FormID refID, RE::FormID baseID) {
                    RE::FormID effectiveID = refID != 0 ? refID : baseID;
                    if (effectiveID == 0) return;
                    RE::FormID storedID = refID != 0 ? refID : baseID;
                    int sex = ActorSvc::GetSex(effectiveID);
                    if (sex == 1 && bioMotherID == 0) {
                        bioMotherID = storedID;
                    } else if (sex == 0 && bioFatherID == 0) {
                        bioFatherID = storedID;
                    } else if (bioMotherID == 0) {
                        bioMotherID = storedID;
                    } else if (bioFatherID == 0) {
                        bioFatherID = storedID;
                    }
                };
                assignBioBySex(parentARefID, entry.parentABaseID);
                assignBioBySex(parentBRefID, parentBBaseID);
            }

            // If the parents were divorced by this import (one of them committed to Player / non-vanilla partner),
            // the child stays only with the committed parent; bio parents are unchanged.
            if (parentARefID != 0 && parentBRefID != 0 &&
                GetRelationshipStatus(parentARefID, parentBRefID) == RelationshipStatus::Divorced) {
                const bool aCommitted = hasNonVanillaMainTierRelationship(parentARefID);
                const bool bCommitted = hasNonVanillaMainTierRelationship(parentBRefID);
                if (aCommitted && !bCommitted) {
                    parentBRefID = 0;
                    parentBBaseID = 0;
                } else if (bCommitted && !aCommitted) {
                    parentARefID = parentBRefID;
                    parentBRefID = 0;
                    parentBBaseID = 0;
                }
            }

            const bool awaitingParentA = (entry.parentABaseID != 0 && parentARefID == 0);
            const bool awaitingParentB = (parentBBaseID != 0 && parentBRefID == 0);

            if (hasChildRef && (parentARefID != 0 || parentBRefID != 0)) {
                RegisterChildActor(childRefID, parentARefID, parentBRefID, 0,
                                   bioMotherID, bioFatherID, 0,
                                   static_cast<uint8_t>(ChildStage::Child),
                                   entry.isAdopted,
                                   /*sendEvent=*/false);
                childImportedCount++;
                if (!awaitingParentA && !awaitingParentB) {
                    continue;
                }
            }

            // If child is unspawned or either parent is not yet spawned, register pending entry
            AddPendingChild(entry.childBaseID,
                            PendingChildData(entry.childBaseID, childRefID,
                                             entry.parentABaseID, parentARefID,
                                             parentBBaseID, parentBRefID,
                                             entry.isAdopted));
        }

        SyncAllMarriedCouplesChildCustody();

        auto endTime = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        MARAS_LOG_INFO("ScanExistingRelationships: Complete in {} ms — Scanned {} vanilla relationships, imported {} adult pairs, {} child records, deferred {} pending pairs and {} pending children",
                       elapsedMs, scannedCount, importedCount, childImportedCount, pendingRelationshipsByBaseID.size(), pendingChildrenByBaseID.size());
    }

    float NPCRelationshipManager::GetEngagementTimestamp(RE::FormID actorA, RE::FormID actorB) const {
        RelationshipPairKey key(actorA, actorB);
        auto it = relationshipPairs.find(key);
        if (it == relationshipPairs.end() || it->second.status != RelationshipStatus::Engaged) {
            return -1.0f;
        }

        const auto& data = it->second;
        for (auto hIt = data.statusHistory.rbegin(); hIt != data.statusHistory.rend(); ++hIt) {
            if (hIt->toStatus == RelationshipStatus::Engaged) {
                return hIt->timestamp;
            }
        }
        return -1.0f;
    }

    void NPCRelationshipManager::CheckAutoMarriageForEngagedNPCs(float currentDay) {
        constexpr float kAutoMarriageDaysThreshold = 5.0f;

        // Collect couples ready for auto-marriage first to prevent iterator invalidation
        std::vector<std::pair<RE::FormID, RE::FormID>> couplesToMarry;

        for (auto& [key, data] : relationshipPairs) {
            // Strictly NPC-NPC only: ignore if either partner is player or invalid
            if (key.actorA == kPlayerFormID || key.actorB == kPlayerFormID || key.actorA == 0 || key.actorB == 0) {
                continue;
            }

            if (data.status != RelationshipStatus::Engaged) {
                continue;
            }

            // Find when they became engaged from status history
            float engagementTime = -1.0f;
            for (auto hIt = data.statusHistory.rbegin(); hIt != data.statusHistory.rend(); ++hIt) {
                if (hIt->toStatus == RelationshipStatus::Engaged) {
                    engagementTime = hIt->timestamp;
                    break;
                }
            }

            // If engaged but missing an engagement record, backfill timestamp to current day so they marry in 5 days
            if (engagementTime < 0.0f) {
                data.statusHistory.emplace_back(RelationshipStatus::Unknown, RelationshipStatus::Engaged, currentDay, key.actorA);
                MARAS_LOG_INFO("Recorded initial engagement timestamp {:.2f} for NPC pair {:08X} - {:08X}",
                               currentDay, key.actorA, key.actorB);
                continue;
            }

            float elapsed = currentDay - engagementTime;
            if (elapsed >= kAutoMarriageDaysThreshold) {
                MARAS_LOG_INFO("Engaged NPC pair {:08X} - {:08X} reached {:.2f} days engaged (threshold {:.1f}); queueing for auto-marriage",
                               key.actorA, key.actorB, elapsed, kAutoMarriageDaysThreshold);
                couplesToMarry.emplace_back(key.actorA, key.actorB);
            }
        }

        for (const auto& [actorA, actorB] : couplesToMarry) {
            // Validate both actors are still alive and not deleted
            auto* a = RE::TESForm::LookupByID<RE::Actor>(actorA);
            auto* b = RE::TESForm::LookupByID<RE::Actor>(actorB);
            if (!a || !b || a->IsDead() || b->IsDead() || a->IsDeleted() || b->IsDeleted()) {
                MARAS_LOG_WARN("Skipping auto-marriage for pair {:08X} - {:08X}: actor missing, dead, or deleted",
                               actorA, actorB);
                continue;
            }

            // Ensure they are still engaged (not broken up meanwhile)
            if (GetRelationshipStatus(actorA, actorB) != RelationshipStatus::Engaged) {
                MARAS_LOG_DEBUG("Pair {:08X} - {:08X} is no longer engaged; skipping auto-marriage", actorA, actorB);
                continue;
            }

            MARAS_LOG_INFO("Auto-marrying engaged NPC couple {:08X} ({}) and {:08X} ({}) after 5 days",
                           actorA, ActorSvc::GetName(actorA), actorB, ActorSvc::GetName(actorB));

            bool success = PromoteToMarried(actorA, actorB, /*sendEvent=*/true, /*breakLovers=*/true);
            if (!success) {
                MARAS_LOG_ERROR("Failed to auto-marry engaged NPC couple {:08X} and {:08X}", actorA, actorB);
            }
        }
    }

}  // namespace MARAS


