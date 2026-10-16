#include "core/NPCRelationshipManager.h"

#include "core/FormCache.h"
#include "services/ActorService.h"
#include "ui/PrismaUIDataBridge.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"
#include "utils/Settings.h"

namespace MARAS {

    bool NPCRelationshipManager::IsNPCInOpenRelationship(RE::FormID actorID) const {
        if (actorID == 0) return false;
        for (const auto& key : GetActorPairKeys(actorID)) {
            auto it = relationshipPairs.find(key);
            if (it != relationshipPairs.end() && it->second.isOpen) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::GetPairIsOpen(RE::FormID actorA, RE::FormID actorB) const {
        if (actorA == 0 || actorB == 0) return false;
        RelationshipPairKey key(actorA, actorB);
        auto it = relationshipPairs.find(key);
        return (it != relationshipPairs.end()) ? it->second.isOpen : false;
    }

    bool NPCRelationshipManager::SetPairIsOpen(RE::FormID actorA, RE::FormID actorB, bool isOpen) {
        if (!EnsureRegistered(actorA) || !EnsureRegistered(actorB)) return false;
        auto& pairData = GetOrCreatePairData(actorA, actorB);
        pairData.isOpen = isOpen;
        RelationshipStatus pairStatus = pairData.status;
        if (isOpen) {
            MarkCheatingAllowedForPair(actorA, actorB);
        } else {
            if (!(actorA == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) && !HasAnyOpenMainPair(actorA)) {
                BreakAllLoverConnections(actorA);
            }
            if (actorB != 0 && !(actorB == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) && !HasAnyOpenMainPair(actorB)) {
                BreakAllLoverConnections(actorB);
            }
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
                                                AffairType affairType, AffairStatus affairStatus) {
        if (!EnsureRegistered(cheaterID) || !EnsureRegistered(victimID) || !EnsureRegistered(affairPartnerID)) {
            return false;
        }

        if (cheaterID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) {
            MARAS_LOG_INFO("RecordCheating ignored for player {:08X}: player polygamy is enabled", cheaterID);
            return false;
        }

        auto calendar = RE::Calendar::GetSingleton();
        int32_t currentDay = calendar ? static_cast<int32_t>(calendar->GetDaysPassed()) : 0;

        auto& pairData = GetOrCreatePairData(cheaterID, victimID);

        bool found = false;
        for (auto& record : pairData.cheatingRecords) {
            if (record.cheaterID == cheaterID && record.victimID == victimID && record.affairPartnerID == affairPartnerID) {
                record.count++;
                record.lastTimestamp = currentDay;
                // If the affair was finished and is being re-opened, reset secrecy and set Ongoing
                if (record.affairStatus != AffairStatus::Ongoing && affairStatus == AffairStatus::Ongoing) {
                    record.affairStatus = AffairStatus::Ongoing;
                    record.isSecret = isSecret; // reset secrecy on re-opening
                } else if (!isSecret) {
                    // If a public (non-secret) incident occurs while ongoing, permanently expose
                    record.isSecret = false;
                }
                if (affairType != AffairType::Casual) {
                    record.affairType = affairType;
                }
                found = true;
                break;
            }
        }
        if (!found) {
            pairData.cheatingRecords.emplace_back(cheaterID, victimID, affairPartnerID, 1, currentDay, isSecret, affairType, affairStatus);
        }

        MARAS_LOG_INFO("Recorded cheating event: {:08X} cheated on {:08X} with {:08X} (isSecret: {}, type: {}, status: {})",
                       cheaterID, victimID, affairPartnerID, isSecret,
                       Utils::AffairTypeToString(affairType), Utils::AffairStatusToString(affairStatus));
        if (victimID == kPlayerFormID) {
            UpdateSpouseCheatingFaction(cheaterID);
        }
        return true;
    }

    bool NPCRelationshipManager::HasCheatedOn(RE::FormID cheaterID, RE::FormID victimID) const {
        RelationshipPairKey key(cheaterID, victimID);
        auto it = relationshipPairs.find(key);
        if (it == relationshipPairs.end()) return false;

        for (const auto& rec : it->second.cheatingRecords) {
            if (rec.cheaterID == cheaterID && rec.victimID == victimID) return true;
        }
        return false;
    }

    bool NPCRelationshipManager::IsNPCCheating(RE::FormID actorID) const {
        if (actorID == 0) return false;

        // Player polygamy exemption
        if (actorID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) {
            return false;
        }

        // 1. Check if actorID is in any closed main-tier relationship (Married, Engaged, Dating)
        // and has an active relationship with someone else, or has multiple closed main-tier relationships
        std::vector<RE::FormID> closedMainPartners;
        for (const auto& [key, data] : relationshipPairs) {
            if (key.Contains(actorID) && IsMainTier(data.status) && !data.isOpen) {
                closedMainPartners.push_back(key.GetOther(actorID));
            }
        }

        if (closedMainPartners.size() > 1) {
            return true;
        }

        if (!closedMainPartners.empty()) {
            auto activeRelationships = GetActiveRelationships(actorID);
            for (const auto& [partnerID, status] : activeRelationships) {
                if (partnerID == 0 || partnerID == actorID) continue;
                if (std::find(closedMainPartners.begin(), closedMainPartners.end(), partnerID) == closedMainPartners.end()) {
                    return true;
                }
            }
        }

        // 2. Check if actorID is specifically the cheater in an ongoing affair record
        for (const auto& [cKey, cData] : relationshipPairs) {
            if (cKey.Contains(actorID)) {
                for (const auto& rec : cData.cheatingRecords) {
                    if (rec.affairStatus == AffairStatus::Ongoing && rec.cheaterID == actorID) {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    bool NPCRelationshipManager::IsRelationshipCheating(RE::FormID actorA, RE::FormID actorB) const {
        if (actorA == 0 || actorB == 0 || actorA == actorB) {
            return false;
        }

        // 1. Check if actorA is in any closed main-tier relationship (Married, Engaged, Dating) with someone else
        for (const auto& [mKey, mData] : relationshipPairs) {
            if (mKey.Contains(actorA) && !mKey.Contains(actorB) && IsMainTier(mData.status) && !mData.isOpen) {
                if (actorA == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) {
                    // player polygamy allows multiple partners
                } else {
                    return true;
                }
            }
        }

        // 2. Check if actorB is in any closed main-tier relationship with someone else
        for (const auto& [mKey, mData] : relationshipPairs) {
            if (mKey.Contains(actorB) && !mKey.Contains(actorA) && IsMainTier(mData.status) && !mData.isOpen) {
                if (actorB == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) {
                    // player polygamy allows multiple partners
                } else {
                    return true;
                }
            }
        }

        // 3. Check if there are active or confronted cheating records involving actorA and actorB
        for (const auto& [cKey, cData] : relationshipPairs) {
            for (const auto& rec : cData.cheatingRecords) {
                if (rec.affairStatus == AffairStatus::Ongoing || rec.affairStatus == AffairStatus::EndedConfronted) {
                    if ((rec.cheaterID == actorA && rec.affairPartnerID == actorB) ||
                        (rec.cheaterID == actorB && rec.affairPartnerID == actorA)) {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    uint32_t NPCRelationshipManager::GetCheatingCount(RE::FormID cheaterID, RE::FormID victimID) const {
        RelationshipPairKey key(cheaterID, victimID);
        auto it = relationshipPairs.find(key);
        if (it == relationshipPairs.end()) return 0;

        uint32_t total = 0;
        for (const auto& rec : it->second.cheatingRecords) {
            if (rec.cheaterID == cheaterID && rec.victimID == victimID) {
                total += rec.count;
            }
        }
        return total;
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetCheatedOnPartners(RE::FormID cheaterID) const {
        std::vector<RE::FormID> result;
        std::unordered_set<RE::FormID> victims;
        for (const auto& [key, pairData] : relationshipPairs) {
            if (key.Contains(cheaterID)) {
                for (const auto& rec : pairData.cheatingRecords) {
                    if (rec.cheaterID == cheaterID) {
                        victims.insert(rec.victimID);
                    }
                }
            }
        }
        result.assign(victims.begin(), victims.end());
        return result;
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetCheatersWhoCheatedOn(RE::FormID victimID) const {
        std::vector<RE::FormID> result;
        std::unordered_set<RE::FormID> cheaters;
        for (const auto& [key, pairData] : relationshipPairs) {
            if (key.Contains(victimID)) {
                for (const auto& rec : pairData.cheatingRecords) {
                    if (rec.victimID == victimID) {
                        cheaters.insert(rec.cheaterID);
                    }
                }
            }
        }
        result.assign(cheaters.begin(), cheaters.end());
        return result;
    }

    const std::vector<CheatingRecord>& NPCRelationshipManager::GetCheatingRecords(RE::FormID actorA, RE::FormID actorB) const {
        static const std::vector<CheatingRecord> empty;
        RelationshipPairKey key(actorA, actorB);
        auto it = relationshipPairs.find(key);
        return (it != relationshipPairs.end()) ? it->second.cheatingRecords : empty;
    }

    std::vector<CheatingRecord> NPCRelationshipManager::GetCheatingRecordsForActor(RE::FormID actorID) const {
        std::vector<CheatingRecord> result;
        for (const auto& [key, pairData] : relationshipPairs) {
            for (const auto& record : pairData.cheatingRecords) {
                if (record.cheaterID == actorID || record.victimID == actorID || record.affairPartnerID == actorID) {
                    result.push_back(record);
                }
            }
        }
        return result;
    }

    bool NPCRelationshipManager::SetCheatingAffairStatus(RE::FormID cheaterID, RE::FormID victimID, RE::FormID affairPartnerID, AffairStatus status) {
        RelationshipPairKey key(cheaterID, victimID);
        auto it = relationshipPairs.find(key);
        if (it == relationshipPairs.end()) return false;

        for (auto& rec : it->second.cheatingRecords) {
            if (rec.cheaterID == cheaterID && rec.victimID == victimID && rec.affairPartnerID == affairPartnerID) {
                rec.affairStatus = status;
                MARAS_LOG_INFO("Set cheating affair status for cheater {:08X} on victim {:08X} with {:08X} to {}",
                               cheaterID, victimID, affairPartnerID, Utils::AffairStatusToString(status));
                if (victimID == kPlayerFormID) {
                    UpdateSpouseCheatingFaction(cheaterID);
                }
                return true;
            }
        }
        return false;
    }

    AffairStatus NPCRelationshipManager::GetCheatingAffairStatus(RE::FormID cheaterID, RE::FormID victimID, RE::FormID affairPartnerID) const {
        RelationshipPairKey key(cheaterID, victimID);
        auto it = relationshipPairs.find(key);
        if (it == relationshipPairs.end()) return AffairStatus::Ongoing;

        for (const auto& rec : it->second.cheatingRecords) {
            if (rec.cheaterID == cheaterID && rec.victimID == victimID && rec.affairPartnerID == affairPartnerID) {
                return rec.affairStatus;
            }
        }
        return AffairStatus::Ongoing;
    }

    void NPCRelationshipManager::MarkCheatingAllowedForPair(RE::FormID actorA, RE::FormID actorB) {
        RelationshipPairKey key(actorA, actorB);
        auto it = relationshipPairs.find(key);
        if (it == relationshipPairs.end()) return;

        for (auto& rec : it->second.cheatingRecords) {
            if (rec.affairStatus == AffairStatus::Ongoing) {
                rec.affairStatus = AffairStatus::Allowed;
                MARAS_LOG_INFO("Marked cheating record as Allowed on pair {:08X} - {:08X} for cheater {:08X} / victim {:08X} with {:08X}",
                               actorA, actorB, rec.cheaterID, rec.victimID, rec.affairPartnerID);
                if (rec.victimID == kPlayerFormID) {
                    UpdateSpouseCheatingFaction(rec.cheaterID);
                }
            }
        }
    }

    void NPCRelationshipManager::MarkPlayerCheatingAllowedOnPolygamy() {
        for (auto& [key, pairData] : relationshipPairs) {
            if (key.Contains(kPlayerFormID)) {
                for (auto& rec : pairData.cheatingRecords) {
                    if (rec.cheaterID == kPlayerFormID && rec.affairStatus == AffairStatus::Ongoing) {
                        rec.affairStatus = AffairStatus::Allowed;
                        MARAS_LOG_INFO("Marked player cheating record as Allowed due to polygamy: victim {:08X} with {:08X}",
                                       rec.victimID, rec.affairPartnerID);
                    }
                }
            }
        }
    }

    void NPCRelationshipManager::EvaluateCheatingOnStatusChange(RE::FormID npcFormID, RE::FormID targetFormID, RelationshipStatus newStatus) {
        if (npcFormID == 0) return;

        // Helper lambda to process when (cheater, affairPartner) enter Dating or Lover
        auto handleDatingOrLover = [this](RE::FormID cheaterID, RE::FormID affairPartnerID, AffairType aType) {
            // If the cheater is the Player and player polygamy is enabled, do not record cheating for the player
            if (cheaterID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) {
                return;
            }

            auto partnerIDs = GetAllPartners(cheaterID);
            for (auto pID : partnerIDs) {
                if (pID == affairPartnerID || pID == 0) continue;
                const auto* rData = GetNPCData(cheaterID, pID);
                if (rData && IsMainTier(rData->status) && !rData->isOpen) {
                    RecordCheating(cheaterID, pID, affairPartnerID, /*isSecret=*/true, aType, AffairStatus::Ongoing);
                }
            }

            // Sync affairType on existing cheating records where cheaterID cheated with affairPartnerID
            for (auto& [key, pairData] : relationshipPairs) {
                if (key.Contains(cheaterID)) {
                    for (auto& rec : pairData.cheatingRecords) {
                        if (rec.cheaterID == cheaterID && rec.affairPartnerID == affairPartnerID) {
                            rec.affairType = aType;
                        }
                    }
                }
            }
        };

        // Helper lambda to handle when relationship between cheater and affair partner ends (Breakup)
        auto handleBreakup = [this](RE::FormID actor1, RE::FormID actor2) {
            for (auto& [key, pairData] : relationshipPairs) {
                for (auto& rec : pairData.cheatingRecords) {
                    if ((rec.cheaterID == actor1 && rec.affairPartnerID == actor2) ||
                        (rec.cheaterID == actor2 && rec.affairPartnerID == actor1)) {
                        if (rec.affairStatus == AffairStatus::Ongoing) {
                            rec.affairStatus = AffairStatus::EndedBreakup;
                            MARAS_LOG_INFO("Affair marked EndedBreakup: cheater {:08X} / victim {:08X} with {:08X}",
                                           rec.cheaterID, rec.victimID, rec.affairPartnerID);
                        }
                    }
                }
            }
        };

        // Helper lambda to handle when main relationship with victim ends (VictimSplit)
        auto handleVictimSplit = [this](RE::FormID actor1, RE::FormID actor2) {
            RelationshipPairKey pairKey(actor1, actor2);
            auto it = relationshipPairs.find(pairKey);
            if (it != relationshipPairs.end()) {
                for (auto& rec : it->second.cheatingRecords) {
                    if (rec.affairStatus == AffairStatus::Ongoing) {
                        rec.affairStatus = AffairStatus::EndedVictimSplit;
                        MARAS_LOG_INFO("Affair marked EndedVictimSplit: cheater {:08X} / victim {:08X} with {:08X}",
                                       rec.cheaterID, rec.victimID, rec.affairPartnerID);
                    }
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
            handleBreakup(npcFormID, targetFormID);
            handleVictimSplit(npcFormID, targetFormID);
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

        auto relStatus = GetRelationshipStatus(npcFormID, kPlayerFormID);
        if (!IsMainTier(relStatus)) {
            Utils::RemoveFromFaction(npcFormID, faction);
            return;
        }

        RelationshipPairKey key(npcFormID, kPlayerFormID);
        auto it = relationshipPairs.find(key);

        bool hasOngoingSecret = false;
        bool hasOngoingKnown = false;

        if (it != relationshipPairs.end()) {
            for (const auto& rec : it->second.cheatingRecords) {
                if (rec.cheaterID == npcFormID && rec.victimID == kPlayerFormID && rec.affairStatus == AffairStatus::Ongoing) {
                    if (rec.isSecret) {
                        hasOngoingSecret = true;
                    } else {
                        hasOngoingKnown = true;
                    }
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

        auto relStatus = GetRelationshipStatus(npcFormID, kPlayerFormID);
        bool isPartner = IsMainTier(relStatus);
        bool isOpen = isPartner && GetPairIsOpen(kPlayerFormID, npcFormID);

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
        if (npcFormID != kPlayerFormID && !IsRegistered(npcFormID)) return;

        auto* faction = FormCache::GetSingleton().GetCanFormNonCheatingMainFaction();
        if (!faction) return;

        bool hasClosedMainRelationship = false;
        if (!(npcFormID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy())) {
            for (const auto& key : GetActorPairKeys(npcFormID)) {
                auto it = relationshipPairs.find(key);
                if (it != relationshipPairs.end() && IsMainTier(it->second.status) && !it->second.isOpen) {
                    hasClosedMainRelationship = true;
                    break;
                }
            }
        }

        // Rank 0 = can't form relationship without cheating (in closed main-tier relationship)
        // Rank 1 = can form relationship without cheating (polygamous player, open main-tier, lover, or single/candidate/ex)
        std::int8_t targetRank = hasClosedMainRelationship ? 0 : 1;

        Utils::AddToFaction(npcFormID, faction, targetRank);
    }

    void NPCRelationshipManager::UpdateAllSpousesCheatingAndOpenFactions() {
        std::unordered_set<RE::FormID> candidates;
        for (const auto& [key, data] : relationshipPairs) {
            if (key.Contains(kPlayerFormID)) {
                RE::FormID partnerID = key.GetOther(kPlayerFormID);
                if (partnerID != 0 && partnerID != kPlayerFormID) {
                    candidates.insert(partnerID);
                }
            }
        }
        for (auto partnerID : candidates) {
            UpdateSpouseCheatingFaction(partnerID);
            UpdateSpouseOpenRelationshipFaction(partnerID);
        }
    }

    void NPCRelationshipManager::UpdateAllCanFormNonCheatingMainFactions() {
        UpdateCanFormNonCheatingMainFaction(kPlayerFormID);
        std::vector<RE::FormID> registered(allRegistered.begin(), allRegistered.end());
        for (RE::FormID npcID : registered) {
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

    bool NPCRelationshipManager::SetCheatingDiscovered(RE::FormID cheaterID, RE::FormID affairPartnerID, const std::string& statusOrResolution) {
        if (cheaterID == 0) return false;

        bool updated = false;
        std::unordered_set<RE::FormID> victimsToUpdate;
        std::unordered_set<RE::FormID> partnersToBreak;
        std::unordered_set<RE::FormID> victimsToOpen;
        std::unordered_set<RE::FormID> victimsToSplit;

        AffairDiscovery discovery = Utils::StringToAffairDiscovery(statusOrResolution);
        AffairResolution resolution = Utils::StringToAffairResolution(statusOrResolution);
        bool isDiscovery = (discovery != AffairDiscovery::None);

        struct EventInfo {
            RE::FormID cheaterID{ 0 };
            RE::FormID victimID{ 0 };
            RE::FormID affairPartnerID{ 0 };
        };
        std::vector<EventInfo> eventsToSend;

        for (auto& [key, pairData] : relationshipPairs) {
            if (!key.Contains(cheaterID)) continue;

            for (auto& rec : pairData.cheatingRecords) {
                if (rec.cheaterID == cheaterID && rec.affairStatus == AffairStatus::Ongoing) {
                    if (affairPartnerID != 0 && rec.affairPartnerID != affairPartnerID) {
                        continue;
                    }

                    if (isDiscovery) {
                        if (!rec.isSecret) {
                            // Already marked known/discovered
                            continue;
                        }
                        rec.isSecret = false;
                        updated = true;
                        victimsToUpdate.insert(rec.victimID);
                        eventsToSend.push_back({ cheaterID, rec.victimID, rec.affairPartnerID });
                        MARAS_LOG_INFO("SetCheatingDiscovered: Affair of {:08X} with {:08X} (victim: {:08X}) discovered (type: {})",
                                       cheaterID, rec.affairPartnerID, rec.victimID, statusOrResolution);
                    } else if (resolution != AffairResolution::None) {
                        rec.isSecret = false;
                        updated = true;
                        victimsToUpdate.insert(rec.victimID);

                        switch (resolution) {
                            case AffairResolution::Confronted:
                                rec.affairStatus = AffairStatus::EndedConfronted;
                                if (rec.affairPartnerID != 0) {
                                    partnersToBreak.insert(rec.affairPartnerID);
                                }
                                eventsToSend.push_back({ cheaterID, rec.victimID, rec.affairPartnerID });
                                MARAS_LOG_INFO("SetCheatingDiscovered: Affair of {:08X} with {:08X} (victim: {:08X}) resolved to EndedConfronted",
                                               cheaterID, rec.affairPartnerID, rec.victimID);
                                break;

                            case AffairResolution::Allowed:
                                rec.affairStatus = AffairStatus::Allowed;
                                if (rec.victimID != 0) {
                                    victimsToOpen.insert(rec.victimID);
                                }
                                eventsToSend.push_back({ cheaterID, rec.victimID, rec.affairPartnerID });
                                MARAS_LOG_INFO("SetCheatingDiscovered: Affair of {:08X} with {:08X} (victim: {:08X}) resolved to Allowed (pair opened)",
                                               cheaterID, rec.affairPartnerID, rec.victimID);
                                break;

                            case AffairResolution::Breakup:
                                rec.affairStatus = AffairStatus::EndedVictimSplit;
                                if (rec.victimID != 0) {
                                    victimsToSplit.insert(rec.victimID);
                                }
                                eventsToSend.push_back({ cheaterID, rec.victimID, rec.affairPartnerID });
                                MARAS_LOG_INFO("SetCheatingDiscovered: Affair of {:08X} with {:08X} (victim: {:08X}) resolved to EndedVictimSplit (separated)",
                                               cheaterID, rec.affairPartnerID, rec.victimID);
                                break;

                            default:
                                break;
                        }
                    } else {
                        // Empty / no resolution string: simply mark known in DB without sending an event
                        if (!rec.isSecret) {
                            continue;
                        }
                        rec.isSecret = false;
                        updated = true;
                        victimsToUpdate.insert(rec.victimID);
                        MARAS_LOG_INFO("SetCheatingDiscovered: Affair of {:08X} with {:08X} (victim: {:08X}) marked known (silent)",
                                       cheaterID, rec.affairPartnerID, rec.victimID);
                    }
                }
            }
        }

        // Apply deferred open-relationship transitions outside the relationshipPairs loop
        for (RE::FormID victimID : victimsToOpen) {
            SetPairIsOpen(cheaterID, victimID, true);
        }

        // Apply deferred victim-split breakups outside the relationshipPairs loop
        for (RE::FormID victimID : victimsToSplit) {
            auto curStatus = GetRelationshipStatus(cheaterID, victimID);
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
        for (RE::FormID partnerID : partnersToBreak) {
            auto currentStatus = GetRelationshipStatus(cheaterID, partnerID);
            if (currentStatus == RelationshipStatus::Lover) {
                PromoteToExLover(cheaterID, partnerID, /*sendEvent=*/true);
            } else if (currentStatus == RelationshipStatus::Dating) {
                PromoteToExDating(cheaterID, partnerID, /*sendEvent=*/true);
            }
        }

        if (updated) {
            UpdateSpouseCheatingFaction(cheaterID);
            for (auto vID : victimsToUpdate) {
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
                        } else if (resolution != AffairResolution::None) {
                            jsonPayload["resolution"] = statusOrResolution;
                            std::string jsonStr = jsonPayload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                            RE::Actor* senderActor = NPCIndex::GetSingleton().GetActorFromFormId(ev.cheaterID);
                            RE::TESForm* senderForm = senderActor ? static_cast<RE::TESForm*>(senderActor) : nullptr;
                            SKSE::ModCallbackEvent modEvent("maras_affair_resolved", jsonStr, 0.0f, senderForm);
                            eventSource->SendEvent(&modEvent);
                            MARAS_LOG_INFO("Sent maras_affair_resolved event: {}", jsonStr);
                        }
                    }
                } else {
                    MARAS_LOG_ERROR("Could not get ModCallbackEventSource to send affair event");
                }
            }
        }

        return updated;
    }

}  // namespace MARAS

