#include "core/NPCRelationshipManager.h"

#include "core/DeceasedRegistry.h"
#include "core/FormCache.h"
#include "core/NPCIndex.h"
#include "core/PackageOverrideService.h"
#include "core/PlayerChildrenService.h"
#include "core/PlayerHouseService.h"
#include "core/SpouseHierarchyManager.h"
#include "services/ActorService.h"
#include "ui/PrismaUIDataBridge.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/FormUtils.h"
#include "utils/Settings.h"

#include <RE/S/SendHUDMessage.h>

namespace MARAS {

    namespace {
        std::mutex s_pendingMarriageAdoptionsMutex;
        std::unordered_set<RE::FormID> s_pendingMarriageAdoptions;

        void MarkMarriageAdoptionPending(RE::FormID childID) {
            std::lock_guard<std::mutex> lock(s_pendingMarriageAdoptionsMutex);
            s_pendingMarriageAdoptions.insert(childID);
        }

        void ClearMarriageAdoptionPending(RE::FormID childID) {
            std::lock_guard<std::mutex> lock(s_pendingMarriageAdoptionsMutex);
            s_pendingMarriageAdoptions.erase(childID);
        }

        bool IsMarriageAdoptionPending(RE::FormID childID) {
            std::lock_guard<std::mutex> lock(s_pendingMarriageAdoptionsMutex);
            return s_pendingMarriageAdoptions.contains(childID);
        }

        void SyncChildHouseholdState(RE::FormID childID, RE::FormID guardianAID, RE::FormID guardianBID, RE::FormID explicitHouseID = 0) {
            // A grown-up (Adult) child must not carry child household state, factions or child sandbox/bed packages
            if (NPCRelationshipManager::GetSingleton().GetChildStage(childID) == ChildStage::Adult) {
                PlayerChildrenService::GetSingleton().OnChildRemovedFromHousehold(childID);
                NPCRelationshipManager::GetSingleton().RemoveChildSandboxAroundParentLinkedRef(childID);
                PackageOverrideService::GetSingleton().UnregisterActor(childID);
                return;
            }

            RE::FormID playerID = kPlayerFormID;

            if (guardianAID == playerID || guardianBID == playerID) {
                RE::FormID houseID = explicitHouseID;
                if (houseID == 0 && guardianBID != 0 && guardianBID != playerID) {
                    houseID = PlayerHouseService::GetSingleton().GetTenantHouseFormID(guardianBID);
                    MARAS_LOG_DEBUG("SyncChildHouseholdState: Guardian B is {:08X} assigned to house {:08X}", guardianBID, houseID);
                }
                if (houseID == 0 && guardianAID != 0 && guardianAID != playerID) {
                    houseID = PlayerHouseService::GetSingleton().GetTenantHouseFormID(guardianAID);
                    MARAS_LOG_DEBUG("SyncChildHouseholdState: Guardian A is {:08X} assigned to house {:08X}", guardianAID, houseID);
                }
                if (houseID == 0) {
                    MARAS_LOG_DEBUG("SyncChildHouseholdState: No parent assigned to house {:08X}", houseID);
                    houseID = PlayerHouseService::GetSingleton().GetTenantHouseFormID(playerID);
                }
                PlayerChildrenService::GetSingleton().OnChildAddedToHousehold(childID, houseID);
                NPCRelationshipManager::GetSingleton().UpdateNonPlayerChildPackageOverride(childID);
            } else {
                PlayerChildrenService::GetSingleton().OnChildRemovedFromHousehold(childID);
                NPCRelationshipManager::GetSingleton().UpdateNonPlayerChildPackageOverride(childID);
            }
        }

        class MarriageAdoptionCallback : public RE::BSScript::IStackCallbackFunctor {
        public:
            MarriageAdoptionCallback(RE::FormID childID, RE::FormID spouseID)
                : m_childID(childID), m_spouseID(spouseID) {}
            ~MarriageAdoptionCallback() override = default;

            void operator()(RE::BSScript::Variable /*a_result*/) override {
                ClearMarriageAdoptionPending(m_childID);
                if (auto* taskIF = SKSE::GetTaskInterface()) {
                    const RE::FormID cID = m_childID;
                    const RE::FormID spID = m_spouseID;
                    taskIF->AddTask([cID, spID]() {
                        auto& relMgr = NPCRelationshipManager::GetSingleton();
                        if (!relMgr.IsMarried(kPlayerFormID, spID)) return;
                        auto entryOpt = relMgr.GetChildEntry(cID);
                        if (!entryOpt) return;
                        if (!MatchesActor(entryOpt->guardianAID, kPlayerFormID) &&
                            !MatchesActor(entryOpt->guardianBID, kPlayerFormID)) {
                            relMgr.RegisterChildActor(
                                cID, kPlayerFormID, spID, 0,
                                entryOpt->bioMotherID, entryOpt->bioFatherID, 0,
                                static_cast<uint8_t>(entryOpt->stage),
                                entryOpt->isAdopted, true);
                        }
                    });
                }
            }

            void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>& /*a_object*/) override {}

        private:
            RE::FormID m_childID{ 0 };
            RE::FormID m_spouseID{ 0 };
        };
    }  // namespace

    RE::FormID NPCRelationshipManager::ResolveNonPlayerChildTarget(const ChildRegistryEntry& entry) const {
        auto isActorAlive = [](RE::FormID actorID) -> bool {
            if (actorID == 0) return false;
            if (DeceasedRegistry::GetSingleton().IsDeceased(actorID)) return false;
            if (auto* actor = NPCIndex::GetSingleton().GetActorFromFormId(actorID)) {
                return !actor->IsDead();
            }
            if (auto* form = RE::TESForm::LookupByID(actorID)) {
                if (auto* actor = form->As<RE::Actor>()) {
                    return !actor->IsDead();
                }
            }
            return true;
        };

        RE::FormID gA = entry.guardianAID;
        RE::FormID gB = entry.guardianBID;
        RE::FormID bioM = entry.bioMotherID;
        RE::FormID bioF = entry.bioFatherID;

        bool gAAlive = isActorAlive(gA);
        bool gBAlive = isActorAlive(gB);
        bool bioMAlive = isActorAlive(bioM);
        bool bioFAlive = isActorAlive(bioF);

        // 0. If in Player household (unhoused), prefer the NPC co-guardian spouse first so the child stays with their NPC parent
        if (gA == kPlayerFormID || gB == kPlayerFormID) {
            RE::FormID npcCoGuardian = (gA == kPlayerFormID) ? gB : gA;
            if (npcCoGuardian != 0 && npcCoGuardian != kPlayerFormID && isActorAlive(npcCoGuardian)) {
                return npcCoGuardian;
            }
            return kPlayerFormID;
        }

        // 1. Check if guardian A/B is bio-mother
        if (gA != 0 && gA == bioM && gAAlive) return gA;
        if (gB != 0 && gB == bioM && gBAlive) return gB;

        // 2. Check if guardian A/B is bio-father
        if (gA != 0 && gA == bioF && gAAlive) return gA;
        if (gB != 0 && gB == bioF && gBAlive) return gB;

        // 3. Pick randomly between guardian A or B if both present & alive
        if (gA != 0 && gB != 0 && gAAlive && gBAlive) {
            static std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<int> dist(0, 1);
            return (dist(rng) == 0) ? gA : gB;
        }

        // 4. If only one guardian present & alive
        if (gA != 0 && gAAlive) return gA;
        if (gB != 0 && gBAlive) return gB;

        // 5. Fallback to bio-mother
        if (bioM != 0 && bioMAlive) return bioM;

        // 6. Fallback to bio-father
        if (bioF != 0 && bioFAlive) return bioF;

        // 7. Fallback to Honorhall Orphanage center marker
        if (auto* marker = FormCache::GetSingleton().GetHonorhallOrphanageCenterMarker()) {
            return marker->GetFormID();
        }
        return 0;
    }

    void NPCRelationshipManager::SetChildSandboxAroundParentLinkedRef(RE::FormID childID, RE::FormID targetRefID) {
        auto* keyword = FormCache::GetSingleton().GetChildSandboxAroundParentKeyword();
        if (Utils::SetLinkedRef(childID, targetRefID, keyword)) {
            MARAS_LOG_INFO("SetChildSandboxAroundParentLinkedRef: Set linked ref on child {:08X} to target {:08X} (keyword {:08X})",
                           childID, targetRefID, keyword ? keyword->GetFormID() : 0);
        } else {
            MARAS_LOG_WARN("Failed to set child sandbox linked ref for child {:08X}: target={:08X}, keyword={}",
                          childID, targetRefID, keyword != nullptr);
        }
    }

    void NPCRelationshipManager::RemoveChildSandboxAroundParentLinkedRef(RE::FormID childID) {
        auto* keyword = FormCache::GetSingleton().GetChildSandboxAroundParentKeyword();
        if (Utils::RemoveLinkedRef(childID, keyword)) {
            MARAS_LOG_INFO("RemoveChildSandboxAroundParentLinkedRef: Removed 0x377 linked ref from child {:08X}", childID);
        }
    }

    void NPCRelationshipManager::UpdateNonPlayerChildPackageOverride(RE::FormID childID) {
        if (childID == 0) return;
        auto it = childrenByID.find(childID);
        if (it == childrenByID.end()) {
            RemoveChildSandboxAroundParentLinkedRef(childID);
            PackageOverrideService::GetSingleton().UnregisterActor(childID);
            return;
        }

        const auto& entry = it->second;

        // Manage dynamic spawned children (0xFFxxxxxx) at Stage 2 who are not in player household,
        // OR any spawned Stage 2 player household child who is currently unhoused (house == 0)
        bool isTemporaryID = ((childID >> 24) == 0xFF) && !IsSyntheticChildID(childID);
        bool isPlayerHousehold = (entry.guardianAID == kPlayerFormID || entry.guardianBID == kPlayerFormID);
        bool isUnhousedPlayerChild = isPlayerHousehold && !IsSyntheticChildID(childID) &&
                                     (PlayerChildrenService::GetSingleton().GetChildHouseFormID(childID) == 0);

        if (entry.stage == ChildStage::Child && ((isTemporaryID && !isPlayerHousehold) || isUnhousedPlayerChild)) {
            RE::FormID targetRefID = ResolveNonPlayerChildTarget(entry);
            auto* keyword = FormCache::GetSingleton().GetChildSandboxAroundParentKeyword();
            RE::FormID childFollowPkgID = 0;
            if (auto* childPkg = FormCache::GetSingleton().GetChildrenNonPlayerFollowParentPackage()) {
                childFollowPkgID = childPkg->GetFormID();
            }
            Utils::AssignPackageWithLinkedRef(childID, targetRefID, keyword, childFollowPkgID);

            if (!isPlayerHousehold) {
                auto* orphanageMarker = FormCache::GetSingleton().GetHonorhallOrphanageCenterMarker();
                RE::FormID orphanageMarkerID = orphanageMarker ? orphanageMarker->GetFormID() : 0;
                auto* adoptableFaction = FormCache::GetSingleton().GetBYOHRelationshipAdoptableFaction();

                if (orphanageMarkerID != 0 && targetRefID == orphanageMarkerID) {
                    if (adoptableFaction) {
                        Utils::AddToFaction(childID, adoptableFaction, 0);
                        MARAS_LOG_INFO("UpdateNonPlayerChildPackageOverride: Child {:08X} parents deceased, sent to Honorhall Orphanage. Assigned BYOHRelationshipAdoptableFaction", childID);
                    }
                } else {
                    if (adoptableFaction) {
                        Utils::RemoveFromFaction(childID, adoptableFaction);
                    }
                }
            }
        } else {
            RemoveChildSandboxAroundParentLinkedRef(childID);
            PackageOverrideService::GetSingleton().UnregisterActor(childID);
        }
    }

    void NPCRelationshipManager::ReevaluateAllNonPlayerChildrenPackages() {
        for (const auto& [childID, entry] : childrenByID) {
            UpdateNonPlayerChildPackageOverride(childID);
        }
    }

    void NPCRelationshipManager::SendChildEvent(const std::string& eventType, RE::FormID childID, RE::FormID guardianAID, RE::FormID guardianBID, RE::FormID otherID, const std::string& reason) {
        if (childID == 0) return;

        auto* eventSource = SKSE::GetModCallbackEventSource();
        if (!eventSource) {
            MARAS_LOG_WARN("Could not get ModCallbackEventSource to send child event");
            return;
        }

        nlohmann::json payload;
        payload["eventType"] = eventType;
        payload["child"] = fmt::format("0x{:08X}", childID);
        payload["childName"] = ActorSvc::GetName(childID);

        if (guardianAID != 0) {
            payload["guardianA"] = fmt::format("0x{:08X}", guardianAID);
            payload["guardianAName"] = ActorSvc::GetName(guardianAID);
        } else {
            payload["guardianA"] = "";
            payload["guardianAName"] = "";
        }

        if (guardianBID != 0) {
            payload["guardianB"] = fmt::format("0x{:08X}", guardianBID);
            payload["guardianBName"] = ActorSvc::GetName(guardianBID);
        } else {
            payload["guardianB"] = "";
            payload["guardianBName"] = "";
        }

        if (otherID != 0) {
            payload["other"] = fmt::format("0x{:08X}", otherID);
            payload["otherName"] = ActorSvc::GetName(otherID);
        } else {
            payload["other"] = "";
            payload["otherName"] = "";
        }

        payload["reason"] = reason;

        std::string jsonStr = payload.dump();
        auto* childForm = RE::TESForm::LookupByID(childID);
        SKSE::ModCallbackEvent modEvent("maras_child_event", jsonStr, 0.0f, childForm);
        eventSource->SendEvent(&modEvent);
        MARAS_LOG_INFO("Sent maras_child_event: {}", jsonStr);
    }

    bool NPCRelationshipManager::IsChildBlockedFromAdoption(RE::FormID childID, std::string* outReason) const {
        if (childID == 0) return false;

        // Check if childID is Aventus Aretino (RefID 0x1B121 or BaseID 0x14132)
        bool isAventus = (childID == 0x1B121 || childID == 0x14132);
        if (!isAventus) {
            if (auto* form = RE::TESForm::LookupByID(childID)) {
                if (form->GetFormID() == 0x1B121 || form->GetFormID() == 0x14132) {
                    isAventus = true;
                } else if (auto* refr = form->As<RE::TESObjectREFR>()) {
                    if (refr->GetFormID() == 0x1B121 || (refr->GetBaseObject() && refr->GetBaseObject()->GetFormID() == 0x14132)) {
                        isAventus = true;
                    }
                } else if (auto* npc = form->As<RE::TESNPC>()) {
                    if (npc->GetFormID() == 0x14132) {
                        isAventus = true;
                    }
                }
            }
        }

        if (isAventus) {
            auto* db01 = FormCache::GetSingleton().GetDB01();
            if (db01) {
                // DB01 is active/in-progress and not completed (running or stage > 0 and < 200)
                bool isDB01Active = (db01->IsRunning() || (db01->GetCurrentStageID() > 0 && db01->GetCurrentStageID() < 200)) && !db01->IsCompleted();
                if (isDB01Active) {
                    if (outReason) {
                        *outReason = "Performing the Black Sacrament (Innocence Lost in progress)";
                    }
                    return true;
                }
            }
        }

        return false;
    }

    bool NPCRelationshipManager::RegisterChildActor(RE::FormID childID, RE::FormID guardianAID, RE::FormID guardianBID, RE::FormID houseLocationID,
                                                    RE::FormID bioMotherID, RE::FormID bioFatherID, int32_t syntheticID, uint8_t stage,
                                                    std::optional<bool> explicitIsAdopted,
                                                    bool sendEvent) {
        if (childID == 0) return false;
        ClearMarriageAdoptionPending(childID);

        std::string blockReason;
        if (IsChildBlockedFromAdoption(childID, &blockReason)) {
            MARAS_LOG_WARN("[Children] RegisterChildActor blocked - child {:08X} cannot be adopted: {}", childID, blockReason);
            return false;
        }

        if (guardianAID != 0) EnsureRegistered(guardianAID);
        if (guardianBID != 0) EnsureRegistered(guardianBID);

        auto calendar = RE::Calendar::GetSingleton();
        int32_t currentDay = calendar ? static_cast<int32_t>(calendar->GetDaysPassed()) : 0;
        std::string childName = ActorSvc::GetName(childID);
        ChildStage childStage = static_cast<ChildStage>(stage);

        ChildRegistryEntry entry(childID, guardianAID, guardianBID, childName, currentDay, false, bioMotherID, bioFatherID);
        entry.stage = childStage;

        RE::FormID oldGuardianA = 0;
        RE::FormID oldGuardianB = 0;
        bool wasAlreadyAdopted = false;

        // If promoting from an existing synthetic record, inherit its metadata and remove the old synthetic key
        if (syntheticID != 0) {
            RE::FormID synthFormID = static_cast<RE::FormID>(syntheticID);
            auto synthIt = childrenByID.find(synthFormID);
            if (synthIt != childrenByID.end()) {
                oldGuardianA = synthIt->second.guardianAID;
                oldGuardianB = synthIt->second.guardianBID;
                wasAlreadyAdopted = synthIt->second.isAdopted;
                entry = synthIt->second;
                entry.childID = childID;
                entry.stage = childStage;
                if (!childName.empty()) entry.name = childName;
                if (guardianAID != 0 || guardianBID != 0) entry.guardianAID = guardianAID;
                if (guardianAID != 0 || guardianBID != 0) entry.guardianBID = guardianBID;
                if (bioMotherID != 0) entry.bioMotherID = bioMotherID;
                if (bioFatherID != 0) entry.bioFatherID = bioFatherID;
                childrenByID.erase(synthIt);
                if (synthFormID != childID) {
                    // The old ID may be a physical actor (e.g. a Beeing Female child replaced by its adult); release its household state
                    PlayerChildrenService::GetSingleton().OnChildRemovedFromHousehold(synthFormID);
                    RemoveChildSandboxAroundParentLinkedRef(synthFormID);
                    PackageOverrideService::GetSingleton().UnregisterActor(synthFormID);
                }
                PrismaUIDataBridge::GetSingleton().OnChildRemoved(synthFormID);
                PrismaUIDataBridge::GetSingleton().OnChildPromoted(synthFormID, childID);
            }
        } else {
            auto existingIt = childrenByID.find(childID);
            if (existingIt != childrenByID.end()) {
                oldGuardianA = existingIt->second.guardianAID;
                oldGuardianB = existingIt->second.guardianBID;
                wasAlreadyAdopted = existingIt->second.isAdopted;
                entry = existingIt->second;
                entry.stage = childStage;
                if (!childName.empty()) entry.name = childName;
                entry.guardianAID = guardianAID;
                entry.guardianBID = guardianBID;
                if (bioMotherID != 0) entry.bioMotherID = bioMotherID;
                if (bioFatherID != 0) entry.bioFatherID = bioFatherID;
            }
        }

        if (explicitIsAdopted.has_value()) {
            entry.isAdopted = *explicitIsAdopted;
        } else if (syntheticID != 0) {
            entry.isAdopted = false;
        } else {
            const bool hasAnyGuardian = (entry.guardianAID != 0 || entry.guardianBID != 0);
            const bool gAIsBio = (entry.guardianAID != 0) &&
                                 (MatchesActor(entry.guardianAID, entry.bioMotherID) || MatchesActor(entry.guardianAID, entry.bioFatherID));
            const bool gBIsBio = (entry.guardianBID != 0) &&
                                 (MatchesActor(entry.guardianBID, entry.bioMotherID) || MatchesActor(entry.guardianBID, entry.bioFatherID));
            entry.isAdopted = hasAnyGuardian && !gAIsBio && !gBIsBio;
        }

        if (entry.stage != ChildStage::Unborn && entry.stage != ChildStage::Adult &&
            entry.guardianAID != 0 && entry.guardianBID != 0 && entry.guardianAID != entry.guardianBID &&
            !IsMarried(entry.guardianAID, entry.guardianBID)) {
            entry.guardianAID = ResolveSingleCustodian(childID, entry, entry.guardianAID, entry.guardianBID);
            entry.guardianBID = 0;
        }
        childrenByID[childID] = entry;

        MARAS_LOG_INFO("[Children] Registered child actor {:08X} ('{}', stage: {}, adopted: {}, bioM: {:08X}, bioF: {:08X}) to guardians {:08X} / {:08X}",
                       childID, entry.name, static_cast<int>(entry.stage), entry.isAdopted, entry.bioMotherID, entry.bioFatherID, entry.guardianAID, entry.guardianBID);

        UpdateChildBothParentsFaction(childID);
        if (oldGuardianA != 0 && oldGuardianA != entry.guardianAID && oldGuardianA != entry.guardianBID) {
            UpdateSpouseChildrenFaction(oldGuardianA);
        }
        if (oldGuardianB != 0 && oldGuardianB != entry.guardianAID && oldGuardianB != entry.guardianBID) {
            UpdateSpouseChildrenFaction(oldGuardianB);
        }
        if (entry.guardianAID != 0) UpdateSpouseChildrenFaction(entry.guardianAID);
        if (entry.guardianBID != 0) UpdateSpouseChildrenFaction(entry.guardianBID);

        // Sync household state: gates Player vs NPC internally
        SyncChildHouseholdState(childID, entry.guardianAID, entry.guardianBID, houseLocationID);

        if (sendEvent && entry.isAdopted && !wasAlreadyAdopted) {
            SendChildEvent("adopted", childID, entry.guardianAID, entry.guardianBID, 0, "adoption");
        } else if (sendEvent && (oldGuardianA != entry.guardianAID || oldGuardianB != entry.guardianBID)) {
            SendChildEvent("guardians_changed", childID, entry.guardianAID, entry.guardianBID, 0, "custody_updated");
        }

        PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
        PrismaUIDataBridge::GetSingleton().SendOverviewData();
        PrismaUIDataBridge::GetSingleton().SendNPCDetail(childID);
        if (entry.guardianAID != 0 && entry.guardianAID != kPlayerFormID) {
            PrismaUIDataBridge::GetSingleton().SendNPCDetail(entry.guardianAID);
        }
        if (entry.guardianBID != 0 && entry.guardianBID != kPlayerFormID) {
            PrismaUIDataBridge::GetSingleton().SendNPCDetail(entry.guardianBID);
        }
        return true;
    }

    bool NPCRelationshipManager::RemoveChild(RE::FormID childID) {
        ClearMarriageAdoptionPending(childID);
        auto entryOpt = GetChildEntry(childID);
        RE::FormID guardianAID = entryOpt ? entryOpt->guardianAID : 0;
        RE::FormID guardianBID = entryOpt ? entryOpt->guardianBID : 0;

        if (!IsSyntheticChildID(childID)) {
            if (auto* faction = FormCache::GetSingleton().GetPlayerChildBothParentsFaction()) {
                Utils::RemoveFromFaction(childID, faction);
            }
            if (auto* adoptableFaction = FormCache::GetSingleton().GetBYOHRelationshipAdoptableFaction()) {
                Utils::RemoveFromFaction(childID, adoptableFaction);
            }
            RemoveChildSandboxAroundParentLinkedRef(childID);
            PackageOverrideService::GetSingleton().UnregisterActor(childID);
        }
        PlayerChildrenService::GetSingleton().OnChildRemovedFromHousehold(childID);
        bool removed = childrenByID.erase(childID) > 0;
        if (removed) {
            if (guardianAID != 0) UpdateSpouseChildrenFaction(guardianAID);
            if (guardianBID != 0) UpdateSpouseChildrenFaction(guardianBID);
            PrismaUIDataBridge::GetSingleton().OnChildRemoved(childID);
        }
        return removed;
    }

    bool NPCRelationshipManager::SetChildGuardian(RE::FormID childID, int slot, RE::FormID guardianID) {
        auto it = childrenByID.find(childID);
        if (it == childrenByID.end()) return false;
        RE::FormID oldGA = it->second.guardianAID;
        RE::FormID oldGB = it->second.guardianBID;
        if (guardianID != 0) EnsureRegistered(guardianID);
        if (slot == 0) {
            it->second.guardianAID = guardianID;
        } else {
            it->second.guardianBID = guardianID;
        }
        UpdateChildBothParentsFaction(childID);
        SyncChildHouseholdState(childID, it->second.guardianAID, it->second.guardianBID);
        UpdateSpouseChildrenFaction(oldGA);
        UpdateSpouseChildrenFaction(oldGB);
        UpdateSpouseChildrenFaction(it->second.guardianAID);
        UpdateSpouseChildrenFaction(it->second.guardianBID);
        PrismaUIDataBridge::GetSingleton().OnChildUpdated(it->second);
        return true;
    }

    std::optional<ChildRegistryEntry> NPCRelationshipManager::GetChildEntry(RE::FormID childID) const {
        auto it = childrenByID.find(childID);
        if (it != childrenByID.end()) return it->second;
        return std::nullopt;
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetChildren(RE::FormID guardianAID, RE::FormID guardianBID) const {
        std::vector<RE::FormID> result;
        for (const auto& [cID, entry] : childrenByID) {
            if ((MatchesActor(entry.guardianAID, guardianAID) && MatchesActor(entry.guardianBID, guardianBID)) ||
                (MatchesActor(entry.guardianAID, guardianBID) && MatchesActor(entry.guardianBID, guardianAID))) {
                result.push_back(cID);
            }
        }
        return result;
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetChildrenOfActor(RE::FormID parentID) const {
        std::vector<RE::FormID> result;
        if (parentID == 0) return result;
        for (const auto& [cID, entry] : childrenByID) {
            bool isPresumedFather = entry.paternitySecret.isSecretForPartner &&
                                    ((entry.presumedFatherID != 0 && MatchesActor(entry.presumedFatherID, parentID)) ||
                                     MatchesActor(GetMotherPartner(cID), parentID));
            if (MatchesActor(entry.guardianAID, parentID) || MatchesActor(entry.guardianBID, parentID) ||
                MatchesActor(entry.bioMotherID, parentID) || MatchesActor(entry.bioFatherID, parentID) ||
                isPresumedFather) {
                result.push_back(cID);
            }
        }
        return result;
    }

    const std::unordered_map<RE::FormID, ChildRegistryEntry>& NPCRelationshipManager::GetAllChildren() const {
        return childrenByID;
    }

    bool NPCRelationshipManager::IsChildNPC(RE::FormID formID) const {
        if (formID == 0) return false;

        auto itChild = childrenByID.find(formID);
        if (itChild != childrenByID.end()) {
            if (itChild->second.stage == ChildStage::Adult) {
                return false;
            }
            return true;
        }

        if (auto entry = NPCIndex::GetSingleton().FindByFormID(formID)) {
            if (entry->isChild) {
                return true;
            }
        } else if (auto* npcBase = ActorSvc::GetActorBase(formID)) {
            if (npcBase->race && npcBase->race->data.flags.any(RE::RACE_DATA::Flag::kChild)) {
                return true;
            }
        }

        auto it = pendingChildrenByBaseID.find(formID);
        if (it != pendingChildrenByBaseID.end()) {
            return true;
        }

        return false;
    }

    RE::FormID NPCRelationshipManager::ResolveSingleCustodian(RE::FormID cID, const ChildRegistryEntry& entry, RE::FormID parentA, RE::FormID parentB) const {
        if (parentA == 0) return parentB;
        if (parentB == 0 || parentA == parentB) return parentA;

        const auto& s = Settings::GetSingleton();

        // 1. Biological parent priority (if enabled)
        if (s.childCustodyPrioritizeBioParent) {
            RE::FormID presumedID = (entry.presumedFatherID != 0) ? entry.presumedFatherID : GetMotherPartner(cID);
            bool isSecretPresumedA = entry.paternitySecret.isSecretForPartner && presumedID != 0 && MatchesActor(presumedID, parentA);
            bool isSecretPresumedB = entry.paternitySecret.isSecretForPartner && presumedID != 0 && MatchesActor(presumedID, parentB);
            bool bioA = MatchesActor(entry.bioMotherID, parentA) || MatchesActor(entry.bioFatherID, parentA) || isSecretPresumedA;
            bool bioB = MatchesActor(entry.bioMotherID, parentB) || MatchesActor(entry.bioFatherID, parentB) || isSecretPresumedB;

            if (bioA && !bioB) {
                MARAS_LOG_INFO("Custody resolution: Child {:08X} ('{}') awarded to sole biological parent {:08X}", cID, entry.name, parentA);
                return parentA;
            }
            if (bioB && !bioA) {
                MARAS_LOG_INFO("Custody resolution: Child {:08X} ('{}') awarded to sole biological parent {:08X}", cID, entry.name, parentB);
                return parentB;
            }
        }

        // 2. Player Custody Override (if Player is involved)
        if (parentA == kPlayerFormID || parentB == kPlayerFormID) {
            RE::FormID otherID = (parentA == kPlayerFormID) ? parentB : parentA;
            if (s.childCustodyPlayerPolicy == Settings::ChildCustodyPlayerPolicy::AlwaysWithPlayer) {
                MARAS_LOG_INFO("Custody resolution: Child {:08X} ('{}') stays with Player (AlwaysWithPlayer)", cID, entry.name);
                return kPlayerFormID;
            }
            if (s.childCustodyPlayerPolicy == Settings::ChildCustodyPlayerPolicy::NeverWithPlayer) {
                MARAS_LOG_INFO("Custody resolution: Child {:08X} ('{}') assigned to NPC parent {:08X} (NeverWithPlayer)", cID, entry.name, otherID);
                return otherID;
            }
            // If UseGenderPolicy, fall through to Gender Custody
        }

        // 3. Gender Custody Policy
        bool isAFemale = (ActorSvc::GetSex(parentA) == 1);
        bool isBFemale = (ActorSvc::GetSex(parentB) == 1);

        if (isAFemale != isBFemale) {
            // Opposite-sex couple
            RE::FormID mother = isAFemale ? parentA : parentB;
            RE::FormID father = isAFemale ? parentB : parentA;

            if (s.childCustodyGenderPolicy == Settings::ChildCustodyGenderPolicy::StayWithFather) {
                MARAS_LOG_INFO("Custody resolution: Child {:08X} ('{}') assigned to father {:08X} (StayWithFather)", cID, entry.name, father);
                return father;
            } else {
                MARAS_LOG_INFO("Custody resolution: Child {:08X} ('{}') assigned to mother {:08X} (StayWithMother)", cID, entry.name, mother);
                return mother;
            }
        } else {
            // Same-sex couple (or matching sex): 50/50 Coin Flip
            float roll = Utils::RandomFloat(0.0f, 100.0f);
            RE::FormID chosen = (roll < 50.0f) ? parentA : parentB;
            MARAS_LOG_INFO("Custody resolution: Same-sex parents ({:08X} / {:08X}), 50/50 coin flip ({:.1f}) awarded child {:08X} ('{}') to {:08X}",
                           parentA, parentB, roll, cID, entry.name, chosen);
            return chosen;
        }
    }

    void NPCRelationshipManager::ApplyBirthCustodyRules(RE::FormID childID, ChildRegistryEntry& entry, RE::FormID motherID, RE::FormID presumedFatherID, RE::FormID bioFatherID) {
        if (motherID == 0) return;

        // 1. If mother is married to presumedFatherID or bioFatherID (or another spouse), they share a married household
        if (presumedFatherID != 0 && IsMarried(motherID, presumedFatherID)) {
            entry.guardianAID = motherID;
            entry.guardianBID = presumedFatherID;
            return;
        }
        if (bioFatherID != 0 && bioFatherID != kUnknownFatherFormID && IsMarried(motherID, bioFatherID)) {
            entry.guardianAID = motherID;
            entry.guardianBID = bioFatherID;
            return;
        }
        if (auto marriedSpouses = GetPartners(motherID, RelationshipStatus::Married); !marriedSpouses.empty()) {
            entry.guardianAID = motherID;
            entry.guardianBID = marriedSpouses.front();
            return;
        }

        // 2. Mother is NOT married: parents do not share a household, so resolve a single custodial guardian
        RE::FormID otherParentID = 0;
        if (presumedFatherID != 0 && entry.paternitySecret.isSecretForPartner) {
            // Mother's unmarried partner believes the child is theirs
            otherParentID = presumedFatherID;
        } else if (bioFatherID != 0 && bioFatherID != kUnknownFatherFormID && !entry.paternitySecret.isSecretForBioFather) {
            // Biological father is known and aware of the child
            otherParentID = bioFatherID;
        }

        if (otherParentID == 0 || otherParentID == motherID) {
            entry.guardianAID = motherID;
            entry.guardianBID = 0;
            return;
        }

        RE::FormID custodian = ResolveSingleCustodian(childID, entry, motherID, otherParentID);
        entry.guardianAID = custodian;
        entry.guardianBID = 0;

        // If custody went to an NPC father who is married to an NPC spouse, link his spouse as co-guardian
        if (custodian != motherID && custodian != kPlayerFormID) {
            auto fatherSpouses = GetPartners(custodian, RelationshipStatus::Married);
            if (!fatherSpouses.empty()) {
                entry.guardianBID = fatherSpouses.front();
            }
        }

        MARAS_LOG_INFO("[Fertility] Unmarried birth custody for child {:08X} ('{}'): mother={:08X}, otherParent={:08X} -> guardianA={:08X}, guardianB={:08X}",
                       childID, entry.name, motherID, otherParentID, entry.guardianAID, entry.guardianBID);
    }

    void NPCRelationshipManager::EvaluateChildCustodyOnMarriage(RE::FormID actorA, RE::FormID actorB) {
        if (actorA == 0 || actorB == 0 || actorA == actorB) return;

        // If the biological mother and biological father marry each other, any remaining paternity secret on their shared children is resolved
        for (auto& [cID, entry] : childrenByID) {
            const bool bioParentsMarried =
                (MatchesActor(entry.bioMotherID, actorA) && MatchesActor(entry.bioFatherID, actorB)) ||
                (MatchesActor(entry.bioMotherID, actorB) && MatchesActor(entry.bioFatherID, actorA));
            if (bioParentsMarried && entry.paternitySecret.HasActiveSecret()) {
                ResolvePaternitySecret(cID, /*discoveredByPartner=*/false);
                PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
            }
        }

        if (actorA == kPlayerFormID || actorB == kPlayerFormID) {
            RE::FormID spouseID = (actorA == kPlayerFormID) ? actorB : actorA;

            EnsureRegistered(kPlayerFormID);
            EnsureRegistered(spouseID);

            for (auto& [cID, entry] : childrenByID) {
                if (entry.stage == ChildStage::Adult) continue;

                // Case 1: Spouse had single-parent or widowed children prior to marriage -> transfer custody to Player + Spouse
                if ((MatchesActor(entry.guardianAID, spouseID) || MatchesActor(entry.guardianBID, spouseID)) &&
                    !MatchesActor(entry.guardianAID, kPlayerFormID) && !MatchesActor(entry.guardianBID, kPlayerFormID)) {
                    RE::FormID otherGuardianID = MatchesActor(entry.guardianAID, spouseID) ? entry.guardianBID : entry.guardianAID;
                    bool otherGuardianDead = false;
                    if (otherGuardianID != 0) {
                        if (DeceasedRegistry::GetSingleton().IsDeceased(otherGuardianID)) {
                            otherGuardianDead = true;
                        } else if (auto* otherActor = NPCIndex::GetSingleton().GetActorFromFormId(otherGuardianID)) {
                            otherGuardianDead = otherActor->IsDead();
                        }
                    }
                    const bool canLinkPlayerAsCoParent = (otherGuardianID == 0) ||
                                                         MatchesActor(otherGuardianID, spouseID) ||
                                                         otherGuardianDead;
                    if (!canLinkPlayerAsCoParent) {
                        continue;
                    }

                    if (!entry.isAdopted && entry.bioMotherID == 0 && entry.bioFatherID == 0) {
                        RE::FormID bioP = MatchesActor(entry.guardianAID, spouseID) ? entry.guardianAID : entry.guardianBID;
                        if (ActorSvc::GetSex(bioP) == 1) entry.bioMotherID = bioP;
                        else entry.bioFatherID = bioP;
                    }

                    if (!IsSyntheticChildID(cID) && entry.stage == ChildStage::Child) {
                        if (IsMarriageAdoptionPending(cID)) {
                            continue;
                        }
                        auto* childActor = RE::TESForm::LookupByID<RE::Actor>(cID);
                        if (!childActor) {
                            childActor = NPCIndex::GetSingleton().GetActorFromFormId(cID);
                        }
                        auto* spouseActor = RE::TESForm::LookupByID<RE::Actor>(spouseID);
                        if (!spouseActor) {
                            spouseActor = NPCIndex::GetSingleton().GetActorFromFormId(spouseID);
                        }
                        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
                        if (childActor && spouseActor && vm) {
                            MarkMarriageAdoptionPending(cID);
                            MARAS_LOG_INFO("EvaluateChildCustodyOnMarriage: Dispatching Papyrus AdoptChildGlobal for spawned child {:08X} ('{}') with spouse {:08X}",
                                           cID, entry.name, spouseID);
                            if (otherGuardianID != 0 && !MatchesActor(otherGuardianID, spouseID)) {
                                UpdateSpouseChildrenFaction(otherGuardianID);
                            }
                            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(
                                new MarriageAdoptionCallback(cID, spouseID));
                            auto childArg = childActor;
                            auto spouseArg = spouseActor;
                            RE::TESForm* houseArg = nullptr;
                            auto args = RE::MakeFunctionArguments(std::move(childArg), std::move(spouseArg), std::move(houseArg));
                            if (!vm->DispatchStaticCall("TTM_ServiceChildrenLogic", "AdoptChildGlobal", args, callback)) {
                                ClearMarriageAdoptionPending(cID);
                            }
                            continue;
                        }
                    }

                    entry.guardianAID = kPlayerFormID;
                    entry.guardianBID = spouseID;
                    MARAS_LOG_INFO("Transferring custody of child {:08X} ('{}') to Player/{:08X}", cID, entry.name, spouseID);
                    UpdateChildBothParentsFaction(cID);
                    if (otherGuardianID != 0 && !MatchesActor(otherGuardianID, spouseID)) {
                        UpdateSpouseChildrenFaction(otherGuardianID);
                    }
                    SyncChildHouseholdState(cID, kPlayerFormID, spouseID, 0);
                    SendChildEvent("guardians_changed", cID, kPlayerFormID, spouseID, 0, "marriage");
                }
                // Case 2: Player has single-parent/single-adopted children prior to marriage (or marries the biological parent of a child in Player's household) -> link new spouse as co-parent
                else if ((MatchesActor(entry.guardianAID, kPlayerFormID) || MatchesActor(entry.guardianBID, kPlayerFormID)) &&
                         !MatchesActor(entry.guardianAID, spouseID) && !MatchesActor(entry.guardianBID, spouseID)) {
                    if (entry.stage == ChildStage::Unborn && !MatchesActor(entry.bioMotherID, kPlayerFormID) && !MatchesActor(entry.bioMotherID, spouseID)) {
                        continue;
                    }
                    const bool isSinglePlayerChild = (entry.guardianAID == 0 || entry.guardianBID == 0 || entry.guardianAID == entry.guardianBID);
                    const bool spouseIsBioParent = !entry.isAdopted &&
                                                   (MatchesActor(entry.bioMotherID, spouseID) || MatchesActor(entry.bioFatherID, spouseID));
                    if (!isSinglePlayerChild && !spouseIsBioParent) {
                        continue;
                    }

                    RE::FormID prevCoGuardian = MatchesActor(entry.guardianAID, kPlayerFormID) ? entry.guardianBID : entry.guardianAID;
                    entry.guardianAID = kPlayerFormID;
                    entry.guardianBID = spouseID;
                    MARAS_LOG_INFO("EvaluateChildCustodyOnMarriage: Linking player's child {:08X} ('{}') to new spouse {:08X}",
                                   cID, entry.name, spouseID);
                    UpdateChildBothParentsFaction(cID);
                    if (prevCoGuardian != 0 && prevCoGuardian != kPlayerFormID && prevCoGuardian != spouseID) {
                        UpdateSpouseChildrenFaction(prevCoGuardian);
                    }

                    // Auto-assign unhoused spouse to child's house using RelocateFamilyUnit
                    auto& houseSvc = PlayerHouseService::GetSingleton();
                    auto& childSvc = PlayerChildrenService::GetSingleton();
                    RE::FormID childHouseID = childSvc.GetChildHouseFormID(cID);
                    if (childHouseID != 0 && houseSvc.GetTenantHouseFormID(spouseID) == 0) {
                        houseSvc.RelocateFamilyUnit(cID, childHouseID, true);
                        MARAS_LOG_INFO("EvaluateChildCustodyOnMarriage: Auto-relocated unhoused spouse {:08X} to child {:08X}'s house {:08X}",
                                       spouseID, cID, childHouseID);
                    }

                    SyncChildHouseholdState(cID, kPlayerFormID, spouseID, childHouseID);
                    SendChildEvent("guardians_changed", cID, kPlayerFormID, spouseID, 0, "marriage");
                }
            }
            UpdateSpouseChildrenFaction(spouseID);
            UpdateSpouseChildrenFaction(kPlayerFormID);
        } else {
            // NPC x NPC Marriage
            EnsureRegistered(actorA);
            EnsureRegistered(actorB);

            bool anyUpdated = false;

            for (auto& [cID, entry] : childrenByID) {
                if (entry.stage == ChildStage::Adult) continue;

                bool matchesA = MatchesActor(entry.guardianAID, actorA) || MatchesActor(entry.guardianBID, actorA);
                bool matchesB = MatchesActor(entry.guardianAID, actorB) || MatchesActor(entry.guardianBID, actorB);

                // If already associated with both partners, nothing to do
                if (matchesA && matchesB) {
                    continue;
                }

                // Check if child has actorA as parent and no other parent (single-parent child)
                if (matchesA && !matchesB && (entry.guardianAID == 0 || entry.guardianBID == 0)) {
                    if (!entry.isAdopted && entry.bioMotherID == 0 && entry.bioFatherID == 0) {
                        RE::FormID bioP = MatchesActor(entry.guardianAID, actorA) ? entry.guardianAID : entry.guardianBID;
                        if (ActorSvc::GetSex(bioP) == 1) entry.bioMotherID = bioP;
                        else entry.bioFatherID = bioP;
                    }
                    entry.guardianAID = actorA;
                    entry.guardianBID = actorB;
                    MARAS_LOG_INFO("EvaluateChildCustodyOnMarriage: Linking child {:08X} ('{}') to married pair {:08X} / {:08X} (co-parent {:08X})",
                                   cID, entry.name, actorA, actorB, actorB);
                    UpdateChildBothParentsFaction(cID);
                    SyncChildHouseholdState(cID, actorA, actorB, 0);
                    SendChildEvent("guardians_changed", cID, actorA, actorB, 0, "marriage");
                    anyUpdated = true;
                }
                // Check if child has actorB as parent and no other parent (single-parent child)
                else if (matchesB && !matchesA && (entry.guardianAID == 0 || entry.guardianBID == 0)) {
                    if (!entry.isAdopted && entry.bioMotherID == 0 && entry.bioFatherID == 0) {
                        RE::FormID bioP = MatchesActor(entry.guardianAID, actorB) ? entry.guardianAID : entry.guardianBID;
                        if (ActorSvc::GetSex(bioP) == 1) entry.bioMotherID = bioP;
                        else entry.bioFatherID = bioP;
                    }
                    entry.guardianAID = actorB;
                    entry.guardianBID = actorA;
                    MARAS_LOG_INFO("EvaluateChildCustodyOnMarriage: Linking child {:08X} ('{}') to married pair {:08X} / {:08X} (co-parent {:08X})",
                                   cID, entry.name, actorB, actorA, actorA);
                    UpdateChildBothParentsFaction(cID);
                    SyncChildHouseholdState(cID, actorB, actorA, 0);
                    SendChildEvent("guardians_changed", cID, actorB, actorA, 0, "marriage");
                    anyUpdated = true;
                }
            }

            if (anyUpdated) {
                UpdateSpouseChildrenFaction(actorA);
                UpdateSpouseChildrenFaction(actorB);
            }
        }
    }

    void NPCRelationshipManager::EvaluateChildCustodyOnDivorce(RE::FormID actorA, RE::FormID actorB) {
        if (actorA == 0 || actorB == 0 || actorA == actorB) return;

        auto processChildPaternityAndCustody = [&](RE::FormID cID, ChildRegistryEntry& entry) {
            if (entry.paternitySecret.HasActiveSecret() && !entry.paternitySecret.discovered) {
                RE::FormID motherID = GetBioMother(cID);
                if (motherID == 0) {
                    if (ActorSvc::GetSex(actorA) == 1) motherID = actorA;
                    else if (ActorSvc::GetSex(actorB) == 1) motherID = actorB;
                    else motherID = actorA;
                }
                RE::FormID formerSpouseID = (motherID == actorA) ? actorB : actorA;
                RE::FormID bioFatherID = GetBioFather(cID);
                if (bioFatherID == 0) {
                    if (entry.bioFatherID != 0 && !MatchesActor(entry.bioFatherID, formerSpouseID) && !MatchesActor(entry.bioFatherID, motherID)) {
                        bioFatherID = entry.bioFatherID;
                    }
                }

                ResolvePaternitySecret(cID, /*discoveredByPartner=*/true);
                MARAS_LOG_INFO("[Fertility] Divorce/Breakup: Auto-resolved paternity secret for child {:08X} ('{}')", cID, entry.name);

                // Fire SKSE ModCallbackEvent for SkyrimNet AI context and Papyrus listeners
                if (auto* source = SKSE::GetModCallbackEventSource()) {
                    float childIdFloat = static_cast<float>(static_cast<int32_t>(cID));
                    SKSE::ModCallbackEvent modEvent("maras_paternity_secret_resolved", "divorce_revealed", childIdFloat, nullptr);
                    source->SendEvent(&modEvent);
                }

                // On-screen HUD notification if player is involved
                if (motherID == kPlayerFormID || formerSpouseID == kPlayerFormID || bioFatherID == kPlayerFormID) {
                    std::string notifyMsg = fmt::format("Paternity secret for {} has been resolved following breakup.",
                                                        entry.name.empty() ? "the child" : entry.name);
                    RE::SendHUDMessage::ShowHUDMessage(notifyMsg.c_str());
                }
            } else if (entry.paternitySecret.isSecretForPartner || entry.paternitySecret.isSecretForBioFather) {
                ResolvePaternitySecret(cID, /*discoveredByPartner=*/true);
            }
        };

        // 1. Clear assumed father and resolve pregnancy secrets on divorce/breakup for unborn children,
        //    and resolve paternity secrets for born children where the breaking-up partner was the presumedFatherID
        for (auto& [cID, entry] : childrenByID) {
            bool motherIsA = MatchesActor(entry.bioMotherID, actorA);
            bool motherIsB = MatchesActor(entry.bioMotherID, actorB);
            if (!motherIsA && !motherIsB) continue;

            RE::FormID formerSpouseID = motherIsA ? actorB : actorA;
            if (entry.stage == ChildStage::Unborn) {
                bool hasFormerSpouse = MatchesActor(entry.guardianAID, formerSpouseID) ||
                                       MatchesActor(entry.guardianBID, formerSpouseID) ||
                                       MatchesActor(entry.presumedFatherID, formerSpouseID);
                if (hasFormerSpouse) {
                    processChildPaternityAndCustody(cID, entry);
                    if (MatchesActor(entry.guardianAID, formerSpouseID)) {
                        entry.guardianAID = entry.bioMotherID;
                    }
                    if (MatchesActor(entry.guardianBID, formerSpouseID)) {
                        entry.guardianBID = 0;
                    }
                    if (MatchesActor(entry.presumedFatherID, formerSpouseID)) {
                        entry.presumedFatherID = 0;
                    }
                    if (entry.guardianAID == entry.guardianBID) {
                        entry.guardianBID = 0;
                    }
                    MARAS_LOG_INFO("[Fertility] Divorce/Breakup: Cleared presumed father {:08X} and resolved pregnancy secret of mother {:08X}",
                                   formerSpouseID, entry.bioMotherID);
                    PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
                }
            } else if (entry.stage != ChildStage::Adult && MatchesActor(entry.presumedFatherID, formerSpouseID) &&
                       entry.paternitySecret.HasActiveSecret()) {
                processChildPaternityAndCustody(cID, entry);
                // If an unmarried presumed father held sole custody, re-evaluate single custodian now that the secret is exposed
                if (MatchesActor(entry.guardianAID, formerSpouseID) && entry.guardianBID == 0) {
                    RE::FormID custodian = ResolveSingleCustodian(cID, entry, entry.bioMotherID, formerSpouseID);
                    if (custodian != entry.guardianAID) {
                        entry.guardianAID = custodian;
                        UpdateChildBothParentsFaction(cID);
                        SyncChildHouseholdState(cID, entry.guardianAID, entry.guardianBID, 0);
                        UpdateSpouseChildrenFaction(entry.bioMotherID);
                        UpdateSpouseChildrenFaction(formerSpouseID);
                        SendChildEvent("guardians_changed", cID, entry.guardianAID, 0, formerSpouseID, "divorce");
                    }
                }
                PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
            }
        }

        if (actorA == kPlayerFormID || actorB == kPlayerFormID) {
            RE::FormID spouseID = (actorA == kPlayerFormID) ? actorB : actorA;

            for (auto& [cID, entry] : childrenByID) {
                if (entry.stage == ChildStage::Adult || entry.stage == ChildStage::Unborn) continue;

                bool hasPlayer = MatchesActor(entry.guardianAID, kPlayerFormID) || MatchesActor(entry.guardianBID, kPlayerFormID);
                bool hasSpouse = MatchesActor(entry.guardianAID, spouseID) || MatchesActor(entry.guardianBID, spouseID);
                if (hasPlayer && hasSpouse) {
                    processChildPaternityAndCustody(cID, entry);

                    RE::FormID custodian = ResolveSingleCustodian(cID, entry, kPlayerFormID, spouseID);
                    entry.guardianAID = custodian;
                    entry.guardianBID = 0;

                    UpdateChildBothParentsFaction(cID);
                    SyncChildHouseholdState(cID, entry.guardianAID, entry.guardianBID, 0);
                    RE::FormID formerOther = (entry.guardianAID == kPlayerFormID ? spouseID : kPlayerFormID);
                    SendChildEvent("guardians_changed", cID, entry.guardianAID, entry.guardianBID, formerOther, "divorce");
                    PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
                }
            }
            UpdateSpouseChildrenFaction(spouseID);
            UpdateSpouseChildrenFaction(kPlayerFormID);
        } else {
            // NPC x NPC Divorce
            bool anyUpdated = false;

            for (auto& [cID, entry] : childrenByID) {
                if (entry.stage == ChildStage::Adult || entry.stage == ChildStage::Unborn) continue;

                bool hasA = MatchesActor(entry.guardianAID, actorA) || MatchesActor(entry.guardianBID, actorA);
                bool hasB = MatchesActor(entry.guardianBID, actorB) || MatchesActor(entry.guardianAID, actorB);

                if (!hasA || !hasB) {
                    continue;
                }

                processChildPaternityAndCustody(cID, entry);

                RE::FormID custodian = ResolveSingleCustodian(cID, entry, actorA, actorB);
                entry.guardianAID = custodian;
                entry.guardianBID = 0;

                UpdateChildBothParentsFaction(cID);
                SyncChildHouseholdState(cID, entry.guardianAID, entry.guardianBID, 0);
                RE::FormID formerOther = (entry.guardianAID == actorA ? actorB : actorA);
                SendChildEvent("guardians_changed", cID, entry.guardianAID, entry.guardianBID, formerOther, "divorce");
                PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
                anyUpdated = true;
            }

            if (anyUpdated) {
                UpdateSpouseChildrenFaction(actorA);
                UpdateSpouseChildrenFaction(actorB);
            }
        }

        // 2. Evaluate step-in for any biological mothers following divorce
        if (ActorSvc::GetSex(actorA) == 1) {
            EvaluateBioFatherStepIn(actorA, actorB);
        }
        if (ActorSvc::GetSex(actorB) == 1) {
            EvaluateBioFatherStepIn(actorB, actorA);
        }
    }

    void NPCRelationshipManager::SyncAllMarriedCouplesChildCustody() {
        // 1. Normalize born children whose guardians are not married to each other,
        //    and self-heal legacy unmarried Player+NPC births where AdoptChild overwrote NPC custody.
        for (auto& [cID, entry] : childrenByID) {
            if (entry.stage == ChildStage::Unborn || entry.stage == ChildStage::Adult) continue;

            if (entry.guardianAID != 0 && entry.guardianBID != 0 && entry.guardianAID != entry.guardianBID) {
                if (!IsMarried(entry.guardianAID, entry.guardianBID)) {
                    RE::FormID gA = entry.guardianAID;
                    RE::FormID gB = entry.guardianBID;
                    RE::FormID custodian = ResolveSingleCustodian(cID, entry, gA, gB);
                    RE::FormID formerOther = (custodian == gA) ? gB : gA;
                    entry.guardianAID = custodian;
                    entry.guardianBID = 0;

                    UpdateChildBothParentsFaction(cID);
                    SyncChildHouseholdState(cID, entry.guardianAID, entry.guardianBID, 0);
                    UpdateSpouseChildrenFaction(custodian);
                    UpdateSpouseChildrenFaction(formerOther);
                    PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
                }
            } else if (!entry.isAdopted && entry.isBiologicalChild &&
                       entry.guardianAID == kPlayerFormID && entry.guardianBID == 0 &&
                       entry.bioMotherID != 0 && entry.bioFatherID != 0 &&
                       entry.bioFatherID != kUnknownFatherFormID &&
                       (entry.bioMotherID == kPlayerFormID || entry.bioFatherID == kPlayerFormID)) {
                RE::FormID npcBioParent = (entry.bioMotherID == kPlayerFormID) ? entry.bioFatherID : entry.bioMotherID;
                if (npcBioParent != 0 && npcBioParent != kPlayerFormID &&
                    !IsMarried(kPlayerFormID, npcBioParent) && !IsDivorced(kPlayerFormID, npcBioParent)) {
                    auto* npcActor = RE::TESForm::LookupByID<RE::Actor>(npcBioParent);
                    if (npcActor && !npcActor->IsDead()) {
                        RE::FormID expectedCustodian = ResolveSingleCustodian(
                            cID, entry, entry.bioMotherID, entry.bioFatherID);
                        if (expectedCustodian == npcBioParent) {
                            entry.guardianAID = npcBioParent;
                            entry.guardianBID = 0;

                            UpdateChildBothParentsFaction(cID);
                            SyncChildHouseholdState(cID, entry.guardianAID, entry.guardianBID, 0);
                            UpdateSpouseChildrenFaction(npcBioParent);
                            UpdateSpouseChildrenFaction(kPlayerFormID);
                            PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
                            MARAS_LOG_INFO("[Children] Repaired unmarried birth custody for child {:08X} ('{}'): restored custody from Player to biological parent {:08X}",
                                           cID, entry.name, npcBioParent);
                        }
                    }
                }
            }
        }

        // 2. Synchronize shared custody across all married couples
        for (const auto& [key, data] : relationshipPairs) {
            if (data.status != RelationshipStatus::Married) continue;

            RE::FormID a = key.actorA;
            RE::FormID b = key.actorB;
            if (a == 0 || b == 0) continue;

            EvaluateChildCustodyOnMarriage(a, b);
        }
    }

    void NPCRelationshipManager::EvaluateChildCustodyOnDeath(RE::FormID deceasedID) {
        if (deceasedID == 0 || deceasedID == kPlayerFormID) return;

        // Find highest-ranking surviving spouse of Player (if any)
        RE::FormID highestSurvivingSpouseID = 0;
        auto& hierMgr = SpouseHierarchyManager::GetSingleton();
        for (int rank = 0; rank < 3; ++rank) {
            for (auto spouseFormID : GetAllMarried(kPlayerFormID)) {
                if (spouseFormID != deceasedID && hierMgr.GetRank(spouseFormID) == rank) {
                    highestSurvivingSpouseID = spouseFormID;
                    break;
                }
            }
            if (highestSurvivingSpouseID != 0) break;
        }
        if (highestSurvivingSpouseID == 0) {
            for (auto spouseFormID : GetAllMarried(kPlayerFormID)) {
                if (spouseFormID != deceasedID) {
                    highestSurvivingSpouseID = spouseFormID;
                    break;
                }
            }
        }

        RE::FormID nextSpouseHouseID = 0;
        if (highestSurvivingSpouseID != 0) {
            nextSpouseHouseID = PlayerHouseService::GetSingleton().GetTenantHouseFormID(highestSurvivingSpouseID);
        }

        std::unordered_set<RE::FormID> widowedMothers;

        for (auto& [cID, entry] : childrenByID) {
            if (entry.stage == ChildStage::Adult) continue;

            bool isGuardianA = MatchesActor(entry.guardianAID, deceasedID);
            bool isGuardianB = MatchesActor(entry.guardianBID, deceasedID);
            bool isBioParent = MatchesActor(entry.bioMotherID, deceasedID) || MatchesActor(entry.bioFatherID, deceasedID);

            if (!isGuardianA && !isGuardianB && !isBioParent) {
                continue;
            }

            if (!isGuardianA && !isGuardianB && isBioParent) {
                // If neither guardian was the deceased, but a bio-parent died and guardians are empty, re-evaluate target
                if (entry.guardianAID == 0 && entry.guardianBID == 0) {
                    SyncChildHouseholdState(cID, 0, 0);
                }
                continue;
            }

            RE::FormID survivingParent = isGuardianA ? entry.guardianBID : entry.guardianAID;

            if (survivingParent != 0 && survivingParent != kPlayerFormID && ActorSvc::GetSex(survivingParent) == 1) {
                widowedMothers.insert(survivingParent);
            }

            if (entry.stage == ChildStage::Unborn) {
                entry.guardianAID = survivingParent;
                entry.guardianBID = 0;
                if (entry.paternitySecret.HasActiveSecret()) {
                    ResolvePaternitySecretForPartner(cID, /*discoveredByPartner=*/false, /*sendEvent=*/false);
                }
                PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
                continue;
            }

            if (survivingParent == kPlayerFormID) {
                if (highestSurvivingSpouseID != 0) {
                    entry.guardianAID = kPlayerFormID;
                    entry.guardianBID = highestSurvivingSpouseID;
                    MARAS_LOG_INFO("Death: Spouse {:08X} deceased. Child {:08X} ('{}') reassigned to highest-ranking spouse {:08X} (house {:08X})",
                                   deceasedID, cID, entry.name, highestSurvivingSpouseID, nextSpouseHouseID);
                    UpdateChildBothParentsFaction(cID);
                    SyncChildHouseholdState(cID, kPlayerFormID, highestSurvivingSpouseID, nextSpouseHouseID);
                    if (nextSpouseHouseID != 0) {
                        PlayerHouseService::GetSingleton().RelocateFamilyUnit(cID, nextSpouseHouseID, false);
                    }
                    UpdateSpouseChildrenFaction(highestSurvivingSpouseID);
                    SendChildEvent("inherited", cID, highestSurvivingSpouseID, kPlayerFormID, deceasedID, "spouse_death");
                } else {
                    entry.guardianAID = kPlayerFormID;
                    entry.guardianBID = 0;
                    if (entry.paternitySecret.HasActiveSecret()) {
                        ResolvePaternitySecretForPartner(cID, /*discoveredByPartner=*/false, /*sendEvent=*/false);
                    }
                    MARAS_LOG_INFO("Death: Spouse {:08X} deceased. Child {:08X} ('{}') stays with Player as sole guardian",
                                   deceasedID, cID, entry.name);
                    UpdateChildBothParentsFaction(cID);
                    SyncChildHouseholdState(cID, kPlayerFormID, 0);
                    SendChildEvent("inherited", cID, kPlayerFormID, 0, deceasedID, "spouse_death");
                }
            } else if (survivingParent != 0) {
                entry.guardianAID = survivingParent;
                entry.guardianBID = 0;
                if (entry.paternitySecret.HasActiveSecret()) {
                    ResolvePaternitySecretForPartner(cID, /*discoveredByPartner=*/false, /*sendEvent=*/false);
                }
                MARAS_LOG_INFO("Death: Parent {:08X} deceased. Child {:08X} ('{}') stays with surviving parent {:08X} as sole guardian",
                               deceasedID, cID, entry.name, survivingParent);
                UpdateChildBothParentsFaction(cID);
                SyncChildHouseholdState(cID, survivingParent, 0);
                UpdateSpouseChildrenFaction(survivingParent);
                SendChildEvent("inherited", cID, survivingParent, 0, deceasedID, "parent_death");
            } else {
                entry.guardianAID = 0;
                entry.guardianBID = 0;
                if (entry.paternitySecret.HasActiveSecret()) {
                    ResolvePaternitySecret(cID, false);
                }
                MARAS_LOG_INFO("Death: Parent {:08X} deceased. Child {:08X} ('{}') has no surviving guardians",
                               deceasedID, cID, entry.name);
                UpdateChildBothParentsFaction(cID);
                SyncChildHouseholdState(cID, 0, 0);
                SendChildEvent("inherited", cID, 0, 0, deceasedID, "parent_death");
            }
            PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
        }
        UpdateSpouseChildrenFaction(deceasedID);

        // Evaluate step-in only for mothers who actually lost deceasedID as a co-guardian
        for (RE::FormID motherID : widowedMothers) {
            EvaluateBioFatherStepIn(motherID, deceasedID);
        }
    }

    void NPCRelationshipManager::UpdateChildBothParentsFaction(RE::FormID childID) {
        if (childID == 0) return;

        auto entryOpt = GetChildEntry(childID);
        if (!entryOpt) {
            if (auto* faction = FormCache::GetSingleton().GetPlayerChildBothParentsFaction()) {
                Utils::RemoveFromFaction(childID, faction);
            }
            return;
        }

        const auto& entry = *entryOpt;
        if (entry.stage == ChildStage::Adult || (entry.guardianAID != kPlayerFormID && entry.guardianBID != kPlayerFormID)) {
            if (auto* faction = FormCache::GetSingleton().GetPlayerChildBothParentsFaction()) {
                Utils::RemoveFromFaction(childID, faction);
            }
            return;
        }

        RE::FormID otherParentID = (entry.guardianAID == kPlayerFormID) ? entry.guardianBID : entry.guardianAID;
        bool hasSpouseParent = (otherParentID != 0) && IsMarried(otherParentID, kPlayerFormID);

        if (auto* faction = FormCache::GetSingleton().GetPlayerChildBothParentsFaction()) {
            if (hasSpouseParent) {
                std::int8_t rank = 1;
                auto* parentActor = NPCIndex::GetSingleton().GetActorFromFormId(otherParentID);
                auto* parentBase = parentActor ? parentActor->GetActorBase() : ActorSvc::GetActorBase(otherParentID);
                if (parentBase) {
                    rank = (parentBase->GetSex() == RE::SEX::kFemale) ? 1 : 2;
                }
                Utils::AddToFaction(childID, faction, rank);
                MARAS_LOG_INFO("UpdateChildBothParentsFaction: Added child {:08X} ('{}') to TTM_PlayerChildBothParents (other parent {:08X} is spouse, rank={})", childID, entry.name, otherParentID, rank);
            } else {
                Utils::RemoveFromFaction(childID, faction);
                MARAS_LOG_INFO("UpdateChildBothParentsFaction: Removed child {:08X} ('{}') from TTM_PlayerChildBothParents (other parent {:08X}, isSpouse: false)", childID, entry.name, otherParentID);
            }
        }

        if (otherParentID != 0) {
            UpdateSpouseChildrenFaction(otherParentID);
        }
    }

    void NPCRelationshipManager::UpdateSpouseChildrenFaction(RE::FormID spouseID) {
        if (spouseID == 0 || spouseID == kPlayerFormID) return;

        auto* spouseActor = NPCIndex::GetSingleton().GetActorFromFormId(spouseID);
        auto* faction = FormCache::GetSingleton().GetSpouseChildrenFaction();
        if (!faction) return;

        bool isSpouse = IsMarried(spouseID, kPlayerFormID);
        size_t childCount = GetChildren(kPlayerFormID, spouseID).size();
        bool shouldHaveFaction = isSpouse && (childCount >= 1);
        std::int8_t rank = static_cast<std::int8_t>(childCount);

        if (shouldHaveFaction) {
            if (spouseActor) {
                spouseActor->AddToFaction(faction, rank);
            } else {
                Utils::AddToFaction(spouseID, faction, rank);
            }
            MARAS_LOG_INFO("UpdateSpouseChildrenFaction: Set TTM_SpouseChildren faction on spouse {:08X} to rank {}", spouseID, rank);
        } else {
            if (spouseActor) {
                if (spouseActor->IsInFaction(faction)) {
                    spouseActor->RemoveFromFaction(faction);
                    MARAS_LOG_INFO("UpdateSpouseChildrenFaction: Removed TTM_SpouseChildren faction from spouse {:08X} (isSpouse: {}, child count: {})", spouseID, isSpouse, childCount);
                }
            } else {
                Utils::RemoveFromFaction(spouseID, faction);
            }
        }
    }

    void NPCRelationshipManager::UpdateAllSpousesChildrenFaction() {
        std::unordered_set<RE::FormID> candidates;
        for (auto spouseID : GetAllMarried(kPlayerFormID)) {
            candidates.insert(spouseID);
        }
        for (const auto& [cID, entry] : childrenByID) {
            if (entry.guardianAID == kPlayerFormID && entry.guardianBID != 0) {
                candidates.insert(entry.guardianBID);
            } else if (entry.guardianBID == kPlayerFormID && entry.guardianAID != 0) {
                candidates.insert(entry.guardianAID);
            }
        }
        for (auto spouseID : candidates) {
            UpdateSpouseChildrenFaction(spouseID);
        }
    }

    RE::FormID NPCRelationshipManager::GetGuardianA(RE::FormID childID) const {
        auto it = childrenByID.find(childID);
        return (it != childrenByID.end()) ? it->second.guardianAID : 0;
    }

    RE::FormID NPCRelationshipManager::GetGuardianB(RE::FormID childID) const {
        auto it = childrenByID.find(childID);
        return (it != childrenByID.end()) ? it->second.guardianBID : 0;
    }

    std::pair<RE::FormID, RE::FormID> NPCRelationshipManager::GetGuardians(RE::FormID childID) const {
        auto it = childrenByID.find(childID);
        if (it != childrenByID.end()) {
            return { it->second.guardianAID, it->second.guardianBID };
        }
        return { 0, 0 };
    }

    RE::FormID NPCRelationshipManager::GetCoGuardian(RE::FormID childID, RE::FormID guardianID) const {
        if (guardianID == 0) return 0;
        auto it = childrenByID.find(childID);
        if (it == childrenByID.end()) return 0;
        if (MatchesActor(it->second.guardianAID, guardianID)) return it->second.guardianBID;
        if (MatchesActor(it->second.guardianBID, guardianID)) return it->second.guardianAID;
        return 0;
    }

    bool NPCRelationshipManager::IsGuardian(RE::FormID childID, RE::FormID actorID) const {
        if (actorID == 0) return false;
        auto it = childrenByID.find(childID);
        if (it == childrenByID.end()) return false;
        return MatchesActor(it->second.guardianAID, actorID) || MatchesActor(it->second.guardianBID, actorID);
    }

    RE::FormID NPCRelationshipManager::GetBioMother(RE::FormID childID) const {
        auto it = childrenByID.find(childID);
        if (it == childrenByID.end()) return 0;
        return it->second.bioMotherID;
    }

    RE::FormID NPCRelationshipManager::GetBioFather(RE::FormID childID) const {
        auto it = childrenByID.find(childID);
        if (it == childrenByID.end()) return 0;
        if (it->second.bioFatherID == kUnknownFatherFormID) return 0;
        return it->second.bioFatherID;
    }

    RE::FormID NPCRelationshipManager::GetPresumedFather(RE::FormID childID) const {
        auto it = childrenByID.find(childID);
        if (it == childrenByID.end()) return 0;
        return it->second.presumedFatherID;
    }

    RE::FormID NPCRelationshipManager::GetMotherPartner(RE::FormID childID) const {
        auto it = childrenByID.find(childID);
        if (it == childrenByID.end()) return 0;
        if (it->second.presumedFatherID != 0) {
            return it->second.presumedFatherID;
        }
        RE::FormID mID = it->second.bioMotherID;
        if (mID == 0) return 0;

        if (MatchesActor(it->second.guardianAID, mID) && it->second.guardianBID != 0) {
            return it->second.guardianBID;
        }
        if (MatchesActor(it->second.guardianBID, mID) && it->second.guardianAID != 0) {
            return it->second.guardianAID;
        }
        return GetPrimaryPartner(mID);
    }

    bool NPCRelationshipManager::SetBioMother(RE::FormID childID, RE::FormID motherID) {
        auto it = childrenByID.find(childID);
        if (it == childrenByID.end()) return false;
        if (motherID != 0) EnsureRegistered(motherID);
        it->second.bioMotherID = motherID;
        if (motherID != 0) {
            it->second.isBiologicalChild = true;
            it->second.isAdopted = false;
        } else if (it->second.bioFatherID == 0) {
            it->second.isBiologicalChild = false;
        }
        MARAS_LOG_INFO("[Lineage] Set child {:08X} bio mother to {:08X}", childID, motherID);
        PrismaUIDataBridge::GetSingleton().OnChildUpdated(it->second);
        return true;
    }

    bool NPCRelationshipManager::SetBioFather(RE::FormID childID, RE::FormID fatherID) {
        auto it = childrenByID.find(childID);
        if (it == childrenByID.end()) return false;
        if (fatherID != 0) EnsureRegistered(fatherID);
        it->second.bioFatherID = fatherID;
        if (fatherID != 0) {
            it->second.isBiologicalChild = true;
            it->second.isAdopted = false;
        } else if (it->second.bioMotherID == 0) {
            it->second.isBiologicalChild = false;
        }

        auto& pSec = it->second.paternitySecret;
        if (it->second.stage == ChildStage::Unborn) {
            const RE::FormID presumedID = GetMotherPartner(childID);
            if (presumedID != 0 && fatherID != 0 && !MatchesActor(fatherID, presumedID)) {
                auto* calendar = RE::Calendar::GetSingleton();
                it->second.presumedFatherID = presumedID;
                pSec = PaternitySecret{};
                pSec.isSecret = true;
                pSec.isSecretForPartner = true;
                pSec.isSecretForBioFather = true;
                pSec.lastCheckTimestamp = calendar ? calendar->GetDaysPassed() : 0.0f;
                if (fatherID != kUnknownFatherFormID) {
                    pSec.bioFatherName = ActorSvc::GetName(fatherID);
                    if (auto* r = ActorSvc::GetRace(fatherID)) {
                        pSec.bioFatherRaceID = r->GetFormID();
                    }
                }
                pSec.hasPhenotypeMismatch = HasPhenotypeMismatch(presumedID, fatherID);
            } else {
                it->second.presumedFatherID = 0;
                pSec = PaternitySecret{};
            }
        } else if (it->second.presumedFatherID != 0) {
            if (fatherID != 0 && MatchesActor(fatherID, it->second.presumedFatherID)) {
                it->second.presumedFatherID = 0;
                pSec = PaternitySecret{};
            } else {
                pSec.bioFatherName = (fatherID != 0 && fatherID != kUnknownFatherFormID) ? ActorSvc::GetName(fatherID) : "";
                if (auto* r = (fatherID != 0 && fatherID != kUnknownFatherFormID) ? ActorSvc::GetRace(fatherID) : nullptr) {
                    pSec.bioFatherRaceID = r->GetFormID();
                } else {
                    pSec.bioFatherRaceID = 0;
                }
                pSec.hasPhenotypeMismatch = (fatherID != 0 && fatherID != kUnknownFatherFormID)
                    ? HasPhenotypeMismatch(it->second.presumedFatherID, fatherID)
                    : false;
            }
        }

        MARAS_LOG_INFO("[Lineage] Set child {:08X} bio father to {:08X}", childID, fatherID);
        PrismaUIDataBridge::GetSingleton().OnChildUpdated(it->second);
        return true;
    }

    bool NPCRelationshipManager::SetPresumedFather(RE::FormID childID, RE::FormID fatherID) {
        auto it = childrenByID.find(childID);
        if (it == childrenByID.end()) return false;
        if (fatherID != 0) EnsureRegistered(fatherID);

        auto& entry = it->second;
        auto& pSec = entry.paternitySecret;
        if (fatherID == 0 || (entry.bioFatherID != 0 && MatchesActor(fatherID, entry.bioFatherID))) {
            entry.presumedFatherID = 0;
            pSec = PaternitySecret{};
        } else {
            entry.presumedFatherID = fatherID;
            if (!pSec.isSecret && !pSec.discovered) {
                auto* calendar = RE::Calendar::GetSingleton();
                pSec.isSecret = true;
                pSec.isSecretForPartner = true;
                pSec.isSecretForBioFather = true;
                pSec.lastCheckTimestamp = calendar ? calendar->GetDaysPassed() : 0.0f;
            }
            if (entry.bioFatherID != 0 && entry.bioFatherID != kUnknownFatherFormID) {
                pSec.bioFatherName = ActorSvc::GetName(entry.bioFatherID);
                if (auto* r = ActorSvc::GetRace(entry.bioFatherID)) {
                    pSec.bioFatherRaceID = r->GetFormID();
                }
                pSec.hasPhenotypeMismatch = HasPhenotypeMismatch(fatherID, entry.bioFatherID);
            } else {
                pSec.hasPhenotypeMismatch = false;
            }
        }

        MARAS_LOG_INFO("[Lineage] Set child {:08X} presumed father to {:08X}", childID, entry.presumedFatherID);
        PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
        return true;
    }

    std::string NPCRelationshipManager::GetChildName(RE::FormID childID) const {
        auto it = childrenByID.find(childID);
        if (it != childrenByID.end()) {
            return it->second.name;
        }
        return "";
    }

}  // namespace MARAS

