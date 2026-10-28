#include "core/NPCRelationshipManager.h"

#include "core/DeceasedRegistry.h"
#include "core/FormCache.h"
#include "core/NPCIndex.h"
#include "core/PackageOverrideService.h"
#include "core/PlayerChildrenService.h"
#include "core/PlayerHouseService.h"
#include "core/SkyrimNetIntegration.h"
#include "core/SkyrimNetMemoryService.h"
#include "core/SkyrimNetProfileService.h"
#include "core/SpouseHierarchyManager.h"
#include "services/ActorService.h"
#include "ui/PrismaUIDataBridge.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/FormUtils.h"
#include "utils/Settings.h"

#include <RE/S/SendHUDMessage.h>

namespace MARAS {

    bool ChildRegistryEntry::HasGuardian(RE::FormID actorID) const {
        return actorID != 0 && (Utils::MatchesActor(guardianAID, actorID) || Utils::MatchesActor(guardianBID, actorID));
    }

    int ChildRegistryEntry::GuardianCount() const {
        int count = 0;
        if (guardianAID != 0) ++count;
        if (guardianBID != 0 && (guardianAID == 0 || !Utils::MatchesActor(guardianAID, guardianBID))) ++count;
        return count;
    }

    RE::FormID ChildRegistryEntry::GetSingleGuardian() const {
        if (!IsSingleGuardian()) return 0;
        return guardianAID != 0 ? guardianAID : guardianBID;
    }

    RE::FormID ChildRegistryEntry::GetOtherGuardian(RE::FormID actorID) const {
        if (actorID == 0) return 0;
        if (Utils::MatchesActor(guardianAID, actorID)) return guardianBID;
        if (Utils::MatchesActor(guardianBID, actorID)) return guardianAID;
        return 0;
    }

    bool ChildRegistryEntry::HasSameGuardians(RE::FormID gA, RE::FormID gB) const {
        return Utils::SameGuardianSet(guardianAID, guardianBID, gA, gB);
    }

    GuardianSetDiff ChildRegistryEntry::AssignGuardians(RE::FormID newA, RE::FormID newB) {
        if (newA != 0 && newB != 0 && Utils::MatchesActor(newA, newB)) {
            newB = 0;
        }

        auto diff = Utils::DiffGuardianSets(guardianAID, guardianBID, newA, newB);

        if (!diff.changed && HasAnyGuardian()) {
            // Guardian set is unchanged: preserve existing slot positions so slots never flip!
            // Only update a slot in-place if the incoming FormID resolves the same actor (e.g. Base -> Placed Ref).
            if (guardianAID != 0) {
                if (newA != 0 && Utils::MatchesActor(guardianAID, newA)) guardianAID = newA;
                else if (newB != 0 && Utils::MatchesActor(guardianAID, newB)) guardianAID = newB;
            }
            if (guardianBID != 0) {
                if (newA != 0 && Utils::MatchesActor(guardianBID, newA)) guardianBID = newA;
                else if (newB != 0 && Utils::MatchesActor(guardianBID, newB)) guardianBID = newB;
            }
            return diff;
        }

        // Guardian set changed: if one of the incoming guardians already occupies a slot, keep them in that slot
        if (guardianAID != 0 && newA != 0 && Utils::MatchesActor(guardianAID, newA)) {
            guardianAID = newA;
            guardianBID = newB;
        } else if (guardianAID != 0 && newB != 0 && Utils::MatchesActor(guardianAID, newB)) {
            guardianAID = newB;
            guardianBID = newA;
        } else if (guardianBID != 0 && newA != 0 && Utils::MatchesActor(guardianBID, newA)) {
            guardianBID = newA;
            guardianAID = newB;
        } else if (guardianBID != 0 && newB != 0 && Utils::MatchesActor(guardianBID, newB)) {
            guardianBID = newB;
            guardianAID = newA;
        } else {
            guardianAID = newA;
            guardianBID = newB;
        }

        return diff;
    }

    GuardianSetDiff ChildRegistryEntry::RemoveGuardian(RE::FormID actorID) {
        if (actorID == 0) return {};
        RE::FormID newA = Utils::MatchesActor(guardianAID, actorID) ? 0 : guardianAID;
        RE::FormID newB = Utils::MatchesActor(guardianBID, actorID) ? 0 : guardianBID;
        return AssignGuardians(newA, newB);
    }

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

            const bool hasPlayer = Utils::MatchesActor(guardianAID, kPlayerFormID) ||
                                   Utils::MatchesActor(guardianBID, kPlayerFormID);

            if (hasPlayer) {
                RE::FormID houseID = explicitHouseID;
                RE::FormID coGuardianID = Utils::MatchesActor(guardianAID, kPlayerFormID) ? guardianBID : guardianAID;
                if (houseID == 0 && coGuardianID != 0 && !Utils::MatchesActor(coGuardianID, kPlayerFormID)) {
                    houseID = PlayerHouseService::GetSingleton().GetTenantHouseFormID(coGuardianID);
                    MARAS_LOG_DEBUG("SyncChildHouseholdState: Co-guardian {:08X} assigned to house {:08X}", coGuardianID, houseID);
                }
                if (houseID == 0) {
                    MARAS_LOG_DEBUG("SyncChildHouseholdState: No parent assigned to house {:08X}", houseID);
                    houseID = PlayerHouseService::GetSingleton().GetTenantHouseFormID(kPlayerFormID);
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
                        if (!entryOpt->HasGuardian(kPlayerFormID)) {
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
        if (entry.HasGuardian(kPlayerFormID)) {
            RE::FormID npcCoGuardian = entry.GetOtherGuardian(kPlayerFormID);
            if (npcCoGuardian != 0 && !Utils::MatchesActor(npcCoGuardian, kPlayerFormID) && isActorAlive(npcCoGuardian)) {
                return npcCoGuardian;
            }
            return kPlayerFormID;
        }

        // 1. Check if guardian A/B is bio-mother
        if (gA != 0 && Utils::MatchesActor(gA, bioM) && gAAlive) return gA;
        if (gB != 0 && Utils::MatchesActor(gB, bioM) && gBAlive) return gB;

        // 2. Check if guardian A/B is bio-father
        if (gA != 0 && Utils::MatchesActor(gA, bioF) && gAAlive) return gA;
        if (gB != 0 && Utils::MatchesActor(gB, bioF) && gBAlive) return gB;

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
        bool isPlayerHousehold = entry.HasGuardian(kPlayerFormID);
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
        } else if (entry.stage == ChildStage::Child && isPlayerHousehold && !IsSyntheticChildID(childID)) {
            RemoveChildSandboxAroundParentLinkedRef(childID);
            if (PlayerHouseService::GetSingleton().GetTenantStayAtHome(childID)) {
                PackageOverrideService::GetSingleton().RegisterActor(childID);
            } else {
                PackageOverrideService::GetSingleton().UnregisterActor(childID);
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
        SkyrimNetMemoryService::GetSingleton().RecordChildCustodyEvent(
            eventType, childID, guardianAID, guardianBID, otherID, reason);
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

        ChildRegistryEntry entry(childID, 0, 0, childName, currentDay, false, bioMotherID, bioFatherID);
        entry.stage = childStage;

        RE::FormID oldGuardianA = 0;
        RE::FormID oldGuardianB = 0;
        bool wasAlreadyAdopted = false;
        bool hadPreExistingEntry = false;
        const auto preExistingIt = childrenByID.find(childID);
        const bool wasAlreadyAtStage = (syntheticID == 0) &&
                                       (preExistingIt != childrenByID.end()) &&
                                       (preExistingIt->second.stage == childStage);

        // If promoting from an existing synthetic record, inherit its metadata and remove the old synthetic key
        if (syntheticID != 0) {
            RE::FormID synthFormID = static_cast<RE::FormID>(syntheticID);
            auto synthIt = childrenByID.find(synthFormID);
            if (synthIt != childrenByID.end()) {
                hadPreExistingEntry = true;
                oldGuardianA = synthIt->second.guardianAID;
                oldGuardianB = synthIt->second.guardianBID;
                wasAlreadyAdopted = synthIt->second.isAdopted;
                entry = synthIt->second;
                entry.childID = childID;
                entry.stage = childStage;
                if (!childName.empty()) entry.name = childName;
                if (guardianAID != 0 || guardianBID != 0) {
                    entry.AssignGuardians(guardianAID, guardianBID);
                }
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
            } else {
                entry.AssignGuardians(guardianAID, guardianBID);
            }
        } else {
            auto existingIt = childrenByID.find(childID);
            if (existingIt != childrenByID.end()) {
                hadPreExistingEntry = true;
                oldGuardianA = existingIt->second.guardianAID;
                oldGuardianB = existingIt->second.guardianBID;
                wasAlreadyAdopted = existingIt->second.isAdopted;
                entry = existingIt->second;
                entry.stage = childStage;
                if (!childName.empty()) entry.name = childName;
                entry.AssignGuardians(guardianAID, guardianBID);
                if (bioMotherID != 0) entry.bioMotherID = bioMotherID;
                if (bioFatherID != 0) entry.bioFatherID = bioFatherID;
            } else {
                entry.AssignGuardians(guardianAID, guardianBID);
            }
        }

        if (explicitIsAdopted.has_value()) {
            entry.isAdopted = *explicitIsAdopted;
        } else if (syntheticID != 0) {
            entry.isAdopted = false;
        } else {
            const bool hasAnyGuardian = entry.HasAnyGuardian();
            const bool gAIsBio = (entry.guardianAID != 0) &&
                                 (MatchesActor(entry.guardianAID, entry.bioMotherID) || MatchesActor(entry.guardianAID, entry.bioFatherID));
            const bool gBIsBio = (entry.guardianBID != 0) &&
                                 (MatchesActor(entry.guardianBID, entry.bioMotherID) || MatchesActor(entry.guardianBID, entry.bioFatherID));
            entry.isAdopted = hasAnyGuardian && !gAIsBio && !gBIsBio;
        }

        if (entry.stage != ChildStage::Unborn && entry.stage != ChildStage::Adult &&
            entry.GuardianCount() == 2 &&
            !IsMarried(entry.guardianAID, entry.guardianBID)) {
            RE::FormID soleCustodian = ResolveSingleCustodian(childID, entry, entry.guardianAID, entry.guardianBID);
            entry.AssignGuardians(soleCustodian, 0);
        }
        if (entry.guardianAID != 0) EnsureRegistered(entry.guardianAID);
        if (entry.guardianBID != 0) EnsureRegistered(entry.guardianBID);
        if (entry.bioMotherID != 0) EnsureRegistered(entry.bioMotherID);
        if (entry.bioFatherID != 0 && entry.bioFatherID != kUnknownFatherFormID) EnsureRegistered(entry.bioFatherID);
        if (entry.presumedFatherID != 0) EnsureRegistered(entry.presumedFatherID);
        childrenByID[childID] = entry;

        MARAS_LOG_INFO("[Children] Registered child actor {:08X} ('{}', stage: {}, adopted: {}, bioM: {:08X}, bioF: {:08X}) to guardians {:08X} / {:08X}",
                       childID, entry.name, static_cast<int>(entry.stage), entry.isAdopted, entry.bioMotherID, entry.bioFatherID, entry.guardianAID, entry.guardianBID);

        const auto guardianDiff = Utils::DiffGuardianSets(oldGuardianA, oldGuardianB, entry.guardianAID, entry.guardianBID);

        UpdateChildBothParentsFaction(childID);
        if (guardianDiff.removedA != 0) {
            UpdateSpouseChildrenFaction(guardianDiff.removedA);
        }
        if (guardianDiff.removedB != 0) {
            UpdateSpouseChildrenFaction(guardianDiff.removedB);
        }
        if (entry.guardianAID != 0) UpdateSpouseChildrenFaction(entry.guardianAID);
        if (entry.guardianBID != 0) UpdateSpouseChildrenFaction(entry.guardianBID);

        // Sync household state: gates Player vs NPC internally
        SyncChildHouseholdState(childID, entry.guardianAID, entry.guardianBID, houseLocationID);

        // Generate/update SkyrimNet character profile strictly when a placed Young Child (Stage 2)
        // or Adult Child (Stage 4) actor is newly spawned/promoted (never on synthetic records or relocations).
        if (sendEvent && !IsSyntheticChildID(childID) && !wasAlreadyAtStage &&
            (entry.stage == ChildStage::Child || entry.stage == ChildStage::Adult) &&
            (syntheticID != 0 || entry.bioMotherID != 0 || entry.bioFatherID != 0)) {
            if (IsSkyrimNetAvailable()) {
                SkyrimNetProfileService::GetSingleton().RequestChildProfileUpdate(childID, entry.stage);
            }
        }

        if (sendEvent && entry.isAdopted && !wasAlreadyAdopted) {
            SendChildEvent("adopted", childID, entry.guardianAID, entry.guardianBID, 0, "adoption");
        } else if (sendEvent && hadPreExistingEntry && guardianDiff.changed) {
            SendChildEvent("guardians_changed", childID, entry.guardianAID, entry.guardianBID, guardianDiff.PrimaryRemoved(), "custody_updated");
        }

        PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
        PrismaUIDataBridge::GetSingleton().SendOverviewData();
        PrismaUIDataBridge::GetSingleton().SendNPCDetail(childID);
        if (entry.guardianAID != 0 && !Utils::MatchesActor(entry.guardianAID, kPlayerFormID)) {
            PrismaUIDataBridge::GetSingleton().SendNPCDetail(entry.guardianAID);
        }
        if (entry.guardianBID != 0 && !Utils::MatchesActor(entry.guardianBID, kPlayerFormID)) {
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
        auto& entry = it->second;
        RE::FormID oldGA = entry.guardianAID;
        RE::FormID oldGB = entry.guardianBID;
        if (guardianID != 0) EnsureRegistered(guardianID);
        if (slot == 0) {
            if (guardianID != 0 && Utils::MatchesActor(entry.guardianBID, guardianID)) {
                entry.guardianBID = entry.guardianAID;
            }
            entry.guardianAID = guardianID;
        } else {
            if (guardianID != 0 && Utils::MatchesActor(entry.guardianAID, guardianID)) {
                entry.guardianAID = entry.guardianBID;
            }
            entry.guardianBID = guardianID;
        }
        const auto diff = Utils::DiffGuardianSets(oldGA, oldGB, entry.guardianAID, entry.guardianBID);
        UpdateChildBothParentsFaction(childID);
        SyncChildHouseholdState(childID, entry.guardianAID, entry.guardianBID);
        UpdateSpouseChildrenFaction(oldGA);
        UpdateSpouseChildrenFaction(oldGB);
        UpdateSpouseChildrenFaction(entry.guardianAID);
        UpdateSpouseChildrenFaction(entry.guardianBID);
        PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
        if (diff.changed) {
            SendChildEvent("guardians_changed", childID, entry.guardianAID, entry.guardianBID, diff.PrimaryRemoved(), "custody_change");
        }
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
            if (entry.HasSameGuardians(guardianAID, guardianBID)) {
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
            if (entry.HasGuardian(parentID) ||
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
        if (parentB == 0 || Utils::MatchesActor(parentA, parentB)) return parentA;

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
        if (Utils::MatchesActor(parentA, kPlayerFormID) || Utils::MatchesActor(parentB, kPlayerFormID)) {
            RE::FormID otherID = Utils::MatchesActor(parentA, kPlayerFormID) ? parentB : parentA;
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
            entry.AssignGuardians(motherID, presumedFatherID);
            return;
        }
        if (bioFatherID != 0 && bioFatherID != kUnknownFatherFormID && IsMarried(motherID, bioFatherID)) {
            entry.AssignGuardians(motherID, bioFatherID);
            return;
        }
        if (auto marriedSpouses = GetPartners(motherID, RelationshipStatus::Married); !marriedSpouses.empty()) {
            entry.AssignGuardians(motherID, marriedSpouses.front());
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

        if (otherParentID == 0 || Utils::MatchesActor(otherParentID, motherID)) {
            entry.AssignGuardians(motherID, 0);
            return;
        }

        RE::FormID custodian = ResolveSingleCustodian(childID, entry, motherID, otherParentID);
        RE::FormID coGuardian = 0;

        // If custody went to an NPC father who is married to an NPC spouse, link his spouse as co-guardian
        if (!Utils::MatchesActor(custodian, motherID) && !Utils::MatchesActor(custodian, kPlayerFormID)) {
            auto fatherSpouses = GetPartners(custodian, RelationshipStatus::Married);
            if (!fatherSpouses.empty()) {
                coGuardian = fatherSpouses.front();
            }
        }

        entry.AssignGuardians(custodian, coGuardian);

        MARAS_LOG_INFO("[Fertility] Unmarried birth custody for child {:08X} ('{}'): mother={:08X}, otherParent={:08X} -> guardianA={:08X}, guardianB={:08X}",
                       childID, entry.name, motherID, otherParentID, entry.guardianAID, entry.guardianBID);
    }

    void NPCRelationshipManager::EvaluateChildCustodyOnMarriage(RE::FormID actorA, RE::FormID actorB) {
        if (actorA == 0 || actorB == 0 || Utils::MatchesActor(actorA, actorB)) return;

        // If the biological mother and biological father marry each other, any remaining paternity secret on their shared children is resolved
        for (auto& [cID, entry] : childrenByID) {
            const bool bioParentsMarried =
                (MatchesActor(entry.bioMotherID, actorA) && MatchesActor(entry.bioFatherID, actorB)) ||
                (MatchesActor(entry.bioMotherID, actorB) && MatchesActor(entry.bioFatherID, actorA));
            if (bioParentsMarried && (entry.paternitySecret.HasActiveSecret() || entry.presumedFatherID != 0)) {
                ResolvePaternitySecret(cID, /*discoveredByPartner=*/false);
                entry.presumedFatherID = 0;
                entry.paternitySecret = PaternitySecret{};
                PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
            }
        }

        if (Utils::MatchesActor(actorA, kPlayerFormID) || Utils::MatchesActor(actorB, kPlayerFormID)) {
            RE::FormID spouseID = Utils::MatchesActor(actorA, kPlayerFormID) ? actorB : actorA;

            EnsureRegistered(kPlayerFormID);
            EnsureRegistered(spouseID);

            for (auto& [cID, entry] : childrenByID) {
                if (entry.stage == ChildStage::Adult) continue;

                // Case 1: Spouse had single-parent or widowed children prior to marriage -> transfer custody to Player + Spouse
                if (entry.HasGuardian(spouseID) && !entry.HasGuardian(kPlayerFormID)) {
                    RE::FormID otherGuardianID = entry.GetOtherGuardian(spouseID);
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

                    const auto diff = entry.AssignGuardians(kPlayerFormID, spouseID);
                    MARAS_LOG_INFO("Transferring custody of child {:08X} ('{}') to Player/{:08X}", cID, entry.name, spouseID);
                    UpdateChildBothParentsFaction(cID);
                    if (diff.PrimaryRemoved() != 0) {
                        UpdateSpouseChildrenFaction(diff.PrimaryRemoved());
                    }
                    if (entry.stage != ChildStage::Unborn) {
                        SyncChildHouseholdState(cID, entry.guardianAID, entry.guardianBID, 0);
                        if (diff.changed) {
                            SendChildEvent("guardians_changed", cID, entry.guardianAID, entry.guardianBID, diff.PrimaryRemoved(), "marriage");
                        }
                    } else {
                        PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
                    }
                }
                // Case 2: Player has single-parent/single-adopted children prior to marriage (or marries the biological parent of a child in Player's household) -> link new spouse as co-parent
                else if (entry.HasGuardian(kPlayerFormID) && !entry.HasGuardian(spouseID)) {
                    if (entry.stage == ChildStage::Unborn && !MatchesActor(entry.bioMotherID, kPlayerFormID) && !MatchesActor(entry.bioMotherID, spouseID)) {
                        continue;
                    }
                    const bool isSinglePlayerChild = entry.IsSingleGuardian();
                    const bool spouseIsBioParent = !entry.isAdopted &&
                                                   (MatchesActor(entry.bioMotherID, spouseID) || MatchesActor(entry.bioFatherID, spouseID));
                    if (!isSinglePlayerChild && !spouseIsBioParent) {
                        continue;
                    }

                    const auto diff = entry.AssignGuardians(kPlayerFormID, spouseID);
                    MARAS_LOG_INFO("EvaluateChildCustodyOnMarriage: Linking player's child {:08X} ('{}') to new spouse {:08X}",
                                   cID, entry.name, spouseID);
                    UpdateChildBothParentsFaction(cID);
                    if (diff.PrimaryRemoved() != 0) {
                        UpdateSpouseChildrenFaction(diff.PrimaryRemoved());
                    }

                    if (entry.stage != ChildStage::Unborn) {
                        // Auto-assign unhoused spouse to child's house using RelocateFamilyUnit
                        auto& houseSvc = PlayerHouseService::GetSingleton();
                        auto& childSvc = PlayerChildrenService::GetSingleton();
                        RE::FormID childHouseID = childSvc.GetChildHouseFormID(cID);
                        if (childHouseID != 0 && houseSvc.GetTenantHouseFormID(spouseID) == 0) {
                            houseSvc.RelocateFamilyUnit(cID, childHouseID, true);
                            MARAS_LOG_INFO("EvaluateChildCustodyOnMarriage: Auto-relocated unhoused spouse {:08X} to child {:08X}'s house {:08X}",
                                           spouseID, cID, childHouseID);
                        }

                        SyncChildHouseholdState(cID, entry.guardianAID, entry.guardianBID, childHouseID);
                        if (diff.changed) {
                            SendChildEvent("guardians_changed", cID, entry.guardianAID, entry.guardianBID, diff.PrimaryRemoved(), "marriage");
                        }
                    } else {
                        PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
                    }
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

                bool matchesA = entry.HasGuardian(actorA);
                bool matchesB = entry.HasGuardian(actorB);

                // If already associated with both partners, nothing to do
                if (matchesA && matchesB) {
                    continue;
                }

                // Check if child has actorA or actorB as sole guardian (single-parent child)
                if ((matchesA || matchesB) && entry.IsSingleGuardian()) {
                    RE::FormID existingParent = entry.GetSingleGuardian();
                    RE::FormID newCoParent = matchesA ? actorB : actorA;
                    if (!entry.isAdopted && entry.bioMotherID == 0 && entry.bioFatherID == 0) {
                        if (ActorSvc::GetSex(existingParent) == 1) entry.bioMotherID = existingParent;
                        else entry.bioFatherID = existingParent;
                    }
                    const auto diff = entry.AssignGuardians(existingParent, newCoParent);
                    MARAS_LOG_INFO("EvaluateChildCustodyOnMarriage: Linking child {:08X} ('{}') to married pair {:08X} / {:08X} (co-parent {:08X})",
                                   cID, entry.name, existingParent, newCoParent, newCoParent);
                    UpdateChildBothParentsFaction(cID);
                    if (entry.stage != ChildStage::Unborn) {
                        SyncChildHouseholdState(cID, entry.guardianAID, entry.guardianBID, 0);
                        if (diff.changed) {
                            SendChildEvent("guardians_changed", cID, entry.guardianAID, entry.guardianBID, diff.PrimaryRemoved(), "marriage");
                        }
                    } else {
                        PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
                    }
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
        if (actorA == 0 || actorB == 0 || Utils::MatchesActor(actorA, actorB)) return;

        auto processChildPaternityAndCustody = [&](RE::FormID cID, ChildRegistryEntry& entry) {
            if (entry.paternitySecret.HasActiveSecret() && !entry.paternitySecret.discovered) {
                RE::FormID motherID = GetBioMother(cID);
                if (motherID == 0) {
                    if (ActorSvc::GetSex(actorA) == 1) motherID = actorA;
                    else if (ActorSvc::GetSex(actorB) == 1) motherID = actorB;
                    else motherID = actorA;
                }
                RE::FormID formerSpouseID = Utils::MatchesActor(motherID, actorA) ? actorB : actorA;
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
                SkyrimNetMemoryService::GetSingleton().RecordPaternityReveal(cID, "divorce_revealed");

                // On-screen HUD notification if player is involved
                if (Utils::MatchesActor(motherID, kPlayerFormID) ||
                    Utils::MatchesActor(formerSpouseID, kPlayerFormID) ||
                    Utils::MatchesActor(bioFatherID, kPlayerFormID)) {
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
                bool hasFormerSpouse = entry.HasGuardian(formerSpouseID) ||
                                       MatchesActor(entry.presumedFatherID, formerSpouseID);
                if (hasFormerSpouse) {
                    processChildPaternityAndCustody(cID, entry);
                    entry.RemoveGuardian(formerSpouseID);
                    if (!entry.HasGuardian(entry.bioMotherID) && entry.bioMotherID != 0) {
                        entry.AssignGuardians(entry.bioMotherID, 0);
                    }
                    if (MatchesActor(entry.presumedFatherID, formerSpouseID)) {
                        entry.presumedFatherID = 0;
                    }
                    MARAS_LOG_INFO("[Fertility] Divorce/Breakup: Cleared presumed father {:08X} and resolved pregnancy secret of mother {:08X}",
                                   formerSpouseID, entry.bioMotherID);
                    PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
                }
            } else if (entry.stage != ChildStage::Adult && MatchesActor(entry.presumedFatherID, formerSpouseID) &&
                       entry.paternitySecret.HasActiveSecret()) {
                processChildPaternityAndCustody(cID, entry);
                // If an unmarried presumed father held sole custody, re-evaluate single custodian now that the secret is exposed
                if (entry.IsSoleGuardian(formerSpouseID)) {
                    RE::FormID custodian = ResolveSingleCustodian(cID, entry, entry.bioMotherID, formerSpouseID);
                    const auto diff = entry.AssignGuardians(custodian, 0);
                    if (diff.changed) {
                        UpdateChildBothParentsFaction(cID);
                        SyncChildHouseholdState(cID, entry.guardianAID, entry.guardianBID, 0);
                        UpdateSpouseChildrenFaction(entry.bioMotherID);
                        UpdateSpouseChildrenFaction(formerSpouseID);
                        SendChildEvent("guardians_changed", cID, entry.guardianAID, entry.guardianBID, diff.PrimaryRemoved(), "divorce");
                    }
                }
                PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
            }
        }

        if (Utils::MatchesActor(actorA, kPlayerFormID) || Utils::MatchesActor(actorB, kPlayerFormID)) {
            RE::FormID spouseID = Utils::MatchesActor(actorA, kPlayerFormID) ? actorB : actorA;

            for (auto& [cID, entry] : childrenByID) {
                if (entry.stage == ChildStage::Adult || entry.stage == ChildStage::Unborn) continue;

                if (entry.HasSameGuardians(kPlayerFormID, spouseID)) {
                    processChildPaternityAndCustody(cID, entry);

                    RE::FormID custodian = ResolveSingleCustodian(cID, entry, kPlayerFormID, spouseID);
                    const auto diff = entry.AssignGuardians(custodian, 0);

                    UpdateChildBothParentsFaction(cID);
                    SyncChildHouseholdState(cID, entry.guardianAID, entry.guardianBID, 0);
                    if (diff.changed) {
                        SendChildEvent("guardians_changed", cID, entry.guardianAID, entry.guardianBID, diff.PrimaryRemoved(), "divorce");
                    }
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

                if (!entry.HasSameGuardians(actorA, actorB)) {
                    continue;
                }

                processChildPaternityAndCustody(cID, entry);

                RE::FormID custodian = ResolveSingleCustodian(cID, entry, actorA, actorB);
                const auto diff = entry.AssignGuardians(custodian, 0);

                UpdateChildBothParentsFaction(cID);
                SyncChildHouseholdState(cID, entry.guardianAID, entry.guardianBID, 0);
                if (diff.changed) {
                    SendChildEvent("guardians_changed", cID, entry.guardianAID, entry.guardianBID, diff.PrimaryRemoved(), "divorce");
                }
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
        SkyrimNetMemoryService::SuppressScope suppressMemories;
        // 1. Normalize born children whose guardians are not married to each other,
        //    and self-heal legacy unmarried Player+NPC births where AdoptChild overwrote NPC custody.
        for (auto& [cID, entry] : childrenByID) {
            if (entry.presumedFatherID != 0 &&
                ((!entry.paternitySecret.isSecret && !entry.paternitySecret.discovered) ||
                 MatchesActor(entry.presumedFatherID, entry.bioMotherID) ||
                 (entry.bioFatherID != 0 && MatchesActor(entry.presumedFatherID, entry.bioFatherID)))) {
                MARAS_LOG_INFO("[Children] Repaired invalid presumedFatherID {:08X} on child {:08X} ('{}')",
                               entry.presumedFatherID, cID, entry.name);
                entry.presumedFatherID = 0;
            }
            if (entry.stage == ChildStage::Unborn || entry.stage == ChildStage::Adult) continue;

            if (entry.GuardianCount() == 2) {
                if (!IsMarried(entry.guardianAID, entry.guardianBID)) {
                    RE::FormID gA = entry.guardianAID;
                    RE::FormID gB = entry.guardianBID;
                    RE::FormID custodian = ResolveSingleCustodian(cID, entry, gA, gB);
                    const auto diff = entry.AssignGuardians(custodian, 0);

                    UpdateChildBothParentsFaction(cID);
                    SyncChildHouseholdState(cID, entry.guardianAID, entry.guardianBID, 0);
                    UpdateSpouseChildrenFaction(custodian);
                    if (diff.PrimaryRemoved() != 0) {
                        UpdateSpouseChildrenFaction(diff.PrimaryRemoved());
                    }
                    PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
                }
            } else if (!entry.isAdopted && entry.isBiologicalChild &&
                       entry.IsSoleGuardian(kPlayerFormID) &&
                       entry.bioMotherID != 0 && entry.bioFatherID != 0 &&
                       entry.bioFatherID != kUnknownFatherFormID &&
                       (Utils::MatchesActor(entry.bioMotherID, kPlayerFormID) || Utils::MatchesActor(entry.bioFatherID, kPlayerFormID))) {
                RE::FormID npcBioParent = Utils::MatchesActor(entry.bioMotherID, kPlayerFormID) ? entry.bioFatherID : entry.bioMotherID;
                if (npcBioParent != 0 && !Utils::MatchesActor(npcBioParent, kPlayerFormID) &&
                    !IsMarried(kPlayerFormID, npcBioParent) && !IsDivorced(kPlayerFormID, npcBioParent)) {
                    auto* npcActor = RE::TESForm::LookupByID<RE::Actor>(npcBioParent);
                    if (npcActor && !npcActor->IsDead()) {
                        RE::FormID expectedCustodian = ResolveSingleCustodian(
                            cID, entry, entry.bioMotherID, entry.bioFatherID);
                        if (Utils::MatchesActor(expectedCustodian, npcBioParent)) {
                            entry.AssignGuardians(npcBioParent, 0);

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
        if (deceasedID == 0 || Utils::MatchesActor(deceasedID, kPlayerFormID)) return;

        // Find highest-ranking surviving spouse of Player (if any)
        RE::FormID highestSurvivingSpouseID = 0;
        auto& hierMgr = SpouseHierarchyManager::GetSingleton();
        for (int rank = 0; rank < 3; ++rank) {
            for (auto spouseFormID : GetAllMarried(kPlayerFormID)) {
                if (!Utils::MatchesActor(spouseFormID, deceasedID) && hierMgr.GetRank(spouseFormID) == rank) {
                    highestSurvivingSpouseID = spouseFormID;
                    break;
                }
            }
            if (highestSurvivingSpouseID != 0) break;
        }
        if (highestSurvivingSpouseID == 0) {
            for (auto spouseFormID : GetAllMarried(kPlayerFormID)) {
                if (!Utils::MatchesActor(spouseFormID, deceasedID)) {
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

            bool isDeceasedGuardian = entry.HasGuardian(deceasedID);
            bool isBioParent = MatchesActor(entry.bioMotherID, deceasedID) || MatchesActor(entry.bioFatherID, deceasedID);

            if (!isDeceasedGuardian && !isBioParent) {
                continue;
            }

            if (!isDeceasedGuardian && isBioParent) {
                // If neither guardian was the deceased, but a bio-parent died and guardians are empty, re-evaluate target
                if (!entry.HasAnyGuardian()) {
                    SyncChildHouseholdState(cID, 0, 0);
                }
                continue;
            }

            RE::FormID survivingParent = entry.GetOtherGuardian(deceasedID);

            if (survivingParent != 0 && !Utils::MatchesActor(survivingParent, kPlayerFormID) && ActorSvc::GetSex(survivingParent) == 1) {
                widowedMothers.insert(survivingParent);
            }

            if (entry.stage == ChildStage::Unborn) {
                entry.RemoveGuardian(deceasedID);
                if (entry.paternitySecret.HasActiveSecret()) {
                    ResolvePaternitySecretForPartner(cID, /*discoveredByPartner=*/false, /*sendEvent=*/false);
                }
                PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
                continue;
            }

            if (Utils::MatchesActor(survivingParent, kPlayerFormID)) {
                if (highestSurvivingSpouseID != 0) {
                    entry.AssignGuardians(kPlayerFormID, highestSurvivingSpouseID);
                    MARAS_LOG_INFO("Death: Spouse {:08X} deceased. Child {:08X} ('{}') reassigned to highest-ranking spouse {:08X} (house {:08X})",
                                   deceasedID, cID, entry.name, highestSurvivingSpouseID, nextSpouseHouseID);
                    UpdateChildBothParentsFaction(cID);
                    SyncChildHouseholdState(cID, entry.guardianAID, entry.guardianBID, nextSpouseHouseID);
                    if (nextSpouseHouseID != 0) {
                        PlayerHouseService::GetSingleton().RelocateFamilyUnit(cID, nextSpouseHouseID, false);
                    }
                    UpdateSpouseChildrenFaction(highestSurvivingSpouseID);
                    SendChildEvent("inherited", cID, entry.guardianAID, entry.guardianBID, deceasedID, "spouse_death");
                } else {
                    entry.RemoveGuardian(deceasedID);
                    if (entry.paternitySecret.HasActiveSecret()) {
                        ResolvePaternitySecretForPartner(cID, /*discoveredByPartner=*/false, /*sendEvent=*/false);
                    }
                    MARAS_LOG_INFO("Death: Spouse {:08X} deceased. Child {:08X} ('{}') stays with Player as sole guardian",
                                   deceasedID, cID, entry.name);
                    UpdateChildBothParentsFaction(cID);
                    SyncChildHouseholdState(cID, entry.guardianAID, entry.guardianBID);
                    SendChildEvent("inherited", cID, entry.guardianAID, entry.guardianBID, deceasedID, "spouse_death");
                }
            } else if (survivingParent != 0) {
                entry.RemoveGuardian(deceasedID);
                if (entry.paternitySecret.HasActiveSecret()) {
                    ResolvePaternitySecretForPartner(cID, /*discoveredByPartner=*/false, /*sendEvent=*/false);
                }
                MARAS_LOG_INFO("Death: Parent {:08X} deceased. Child {:08X} ('{}') stays with surviving parent {:08X} as sole guardian",
                               deceasedID, cID, entry.name, survivingParent);
                UpdateChildBothParentsFaction(cID);
                SyncChildHouseholdState(cID, entry.guardianAID, entry.guardianBID);
                UpdateSpouseChildrenFaction(survivingParent);
                SendChildEvent("inherited", cID, entry.guardianAID, entry.guardianBID, deceasedID, "parent_death");
            } else {
                entry.AssignGuardians(0, 0);
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
        if (entry.stage == ChildStage::Adult || !entry.HasGuardian(kPlayerFormID)) {
            if (auto* faction = FormCache::GetSingleton().GetPlayerChildBothParentsFaction()) {
                Utils::RemoveFromFaction(childID, faction);
            }
            return;
        }

        RE::FormID otherParentID = entry.GetOtherGuardian(kPlayerFormID);
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
        if (spouseID == 0 || Utils::MatchesActor(spouseID, kPlayerFormID)) return;

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
            if (entry.HasGuardian(kPlayerFormID)) {
                RE::FormID coGuardian = entry.GetOtherGuardian(kPlayerFormID);
                if (coGuardian != 0) {
                    candidates.insert(coGuardian);
                }
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
        return it->second.GetOtherGuardian(guardianID);
    }

    bool NPCRelationshipManager::IsGuardian(RE::FormID childID, RE::FormID actorID) const {
        if (actorID == 0) return false;
        auto it = childrenByID.find(childID);
        if (it == childrenByID.end()) return false;
        return it->second.HasGuardian(actorID);
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
        if (!it->second.paternitySecret.isSecret && !it->second.paternitySecret.discovered) {
            return 0;
        }
        if (it->second.presumedFatherID == 0 ||
            MatchesActor(it->second.presumedFatherID, it->second.bioMotherID) ||
            (it->second.bioFatherID != 0 && MatchesActor(it->second.presumedFatherID, it->second.bioFatherID))) {
            return 0;
        }
        return it->second.presumedFatherID;
    }

    RE::FormID NPCRelationshipManager::GetMotherPartner(RE::FormID childID) const {
        auto it = childrenByID.find(childID);
        if (it == childrenByID.end()) return 0;
        RE::FormID presID = GetPresumedFather(childID);
        if (presID != 0) {
            return presID;
        }
        RE::FormID mID = it->second.bioMotherID;
        if (mID == 0) return 0;

        RE::FormID coGuardian = it->second.GetOtherGuardian(mID);
        if (coGuardian != 0 && !MatchesActor(coGuardian, mID)) {
            return coGuardian;
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
                const float currentDay = calendar ? calendar->GetDaysPassed() : 0.0f;
                const bool wasAlreadyKnownPregnancy = pSec.isSecret ? !pSec.isPregnancySecretForPartner : (it->second.conceptionTimestamp > 0.0f && (currentDay - it->second.conceptionTimestamp) >= 4.0f);
                it->second.presumedFatherID = presumedID;
                pSec = PaternitySecret{};
                pSec.isSecret = true;
                pSec.isSecretForPartner = true;
                pSec.isSecretForBioFather = true;
                pSec.isPregnancySecretForPartner = !wasAlreadyKnownPregnancy;
                pSec.lastCheckTimestamp = currentDay;
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
        if (fatherID == 0 ||
            (entry.bioMotherID != 0 && MatchesActor(fatherID, entry.bioMotherID)) ||
            (entry.bioFatherID != 0 && MatchesActor(fatherID, entry.bioFatherID))) {
            entry.presumedFatherID = 0;
            pSec = PaternitySecret{};
        } else {
            entry.presumedFatherID = fatherID;
            if (!pSec.isSecret && !pSec.discovered) {
                auto* calendar = RE::Calendar::GetSingleton();
                pSec.isSecret = true;
                pSec.isSecretForPartner = true;
                pSec.isSecretForBioFather = true;
                pSec.isPregnancySecretForPartner = (entry.stage == ChildStage::Unborn);
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
