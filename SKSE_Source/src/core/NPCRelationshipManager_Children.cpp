#include "core/NPCRelationshipManager.h"

#include "core/AdultChildProgression.h"
#include "core/AdultChildTravelService.h"
#include "core/DeceasedRegistry.h"
#include "core/DeferredActorResolver.h"
#include "core/FactionReconciler.h"
#include "core/FormCache.h"
#include "core/NPCIndex.h"
#include "core/NPCTypeDeterminer.h"
#include "core/PackageOverrideService.h"
#include "core/PackageReconciler.h"
#include "core/PlayerChildrenService.h"
#include "core/PlayerHouseService.h"
#include "core/SessionEpoch.h"
#include "core/SkyrimNetIntegration.h"
#include "core/SkyrimNetMemoryService.h"
#include "core/SkyrimNetProfileService.h"
#include "core/SpouseHierarchyManager.h"
#include "core/WorldStateLegacyAdapters.h"
#include "maras/state/WorldStore.h"
#include "papyrus/PapyrusInterface.h"
#include "services/ActorService.h"
#include "ui/PrismaUIDataBridge.h"
#include "ui/PrismaUIManager.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/FormUtils.h"
#include "utils/Settings.h"

#include <RE/E/ExtraCombatStyle.h>
#include <RE/S/SendHUDMessage.h>
#include <random>

namespace MARAS {

    namespace Detail {
        std::unique_ptr<State::IActorMetadataProvider> CaptureActorMetadata(const std::vector<RE::FormID>& actorIDs);
        std::unique_ptr<State::IActorMetadataProvider> CaptureWorldActorMetadata(
            const std::shared_ptr<const State::WorldStateSnapshot>& snap,
            const std::vector<RE::FormID>& extraActorIDs = {});
        Foundation::DeterministicRng MakeEngineRng();
    }

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

        void ClearAllPendingMarriageAdoptions() {
            std::lock_guard<std::mutex> lock(s_pendingMarriageAdoptionsMutex);
            s_pendingMarriageAdoptions.clear();
        }

        void SyncChildHouseholdState(RE::FormID childID, RE::FormID guardianAID, RE::FormID guardianBID, RE::FormID explicitHouseID = 0) {
            const bool hasPlayer = Utils::MatchesActor(guardianAID, kPlayerFormID) ||
                                   Utils::MatchesActor(guardianBID, kPlayerFormID);

            // A grown-up (Adult) child must not carry young-child household state, factions or child bed packages,
            // and is not registered as a house tenant — instead they travel Skyrim independently via AdultChildTravelService.
            if (NPCRelationshipManager::GetSingleton().GetChildStage(childID) == ChildStage::Adult) {
                PlayerChildrenService::GetSingleton().OnChildRemovedFromHousehold(childID);
                if (!IsSyntheticChildID(childID)) {
                    (void)PlayerHouseService::GetSingleton().RemoveTenantFromPlayerHouse(childID);
                }
                NPCRelationshipManager::GetSingleton().UpdateNonPlayerChildPackageOverride(childID);
                return;
            }

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
                : m_childID(childID), m_spouseID(spouseID), m_epoch(GetSessionEpoch()) {}
            ~MarriageAdoptionCallback() override = default;

            void operator()(RE::BSScript::Variable /*a_result*/) override {
                ClearMarriageAdoptionPending(m_childID);
                const RE::FormID cID = m_childID;
                const RE::FormID spID = m_spouseID;
                AddEpochTask(m_epoch, "MarriageAdoptionCallback::CompleteAdoption", [cID, spID]() {
                    std::lock_guard lock(PapyrusInterface::GetPapyrusMutex());
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

            void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>& /*a_object*/) override {}

        private:
            RE::FormID m_childID{ 0 };
            RE::FormID m_spouseID{ 0 };
            SessionEpoch m_epoch{ 0 };
        };
    }  // namespace

    void NPCRelationshipManager::ClearPendingMarriageAdoptions() {
        ClearAllPendingMarriageAdoptions();
    }

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
        RE::FormID bioF = (entry.bioFatherID != kUnknownFatherFormID) ? entry.bioFatherID : 0;

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
            thread_local std::mt19937 rng(std::random_device{}());
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
        const auto entryOpt = GetChildEntry(childID);
        if (entryOpt.has_value() && entryOpt->stage == ChildStage::Adult) {
            if (!IsSyntheticChildID(childID)) {
                AdultChildTravelService::GetSingleton().AssignOrUpdateDestination(childID, false);
            } else {
                AdultChildTravelService::GetSingleton().UnregisterAdultChild(childID);
            }
            FactionReconciler::GetSingleton().ReconcileActor(childID);
            return;
        }

        AdultChildTravelService::GetSingleton().UnregisterAdultChild(childID);
        PackageReconciler::GetSingleton().ReconcileActor(childID);
        FactionReconciler::GetSingleton().ReconcileActor(childID);
    }

    void NPCRelationshipManager::ReevaluateAllNonPlayerChildrenPackages() {
        std::vector<RE::FormID> childIDs;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& [cIdKey, rec] : snap->lineage->AllChildren()) {
                if (rec.stage == State::ChildStage::Adult) continue;
                childIDs.push_back(cIdKey.raw());
            }
        }
        for (const RE::FormID childID : childIDs) {
            PackageReconciler::GetSingleton().ReconcileActor(childID);
        }
        FactionReconciler::GetSingleton().ReconcileActors(childIDs);
    }

    void NPCRelationshipManager::SendChildEvent(const std::string& eventType, RE::FormID childID, RE::FormID guardianAID, RE::FormID guardianBID, RE::FormID otherID, const std::string& reason) {
        if (childID == 0) return;

        nlohmann::json payload;
        payload["eventType"] = eventType;
        payload["child"] = fmt::format("0x{:08X}", childID);
        payload["childId"] = static_cast<int32_t>(childID);
        std::string resolvedChildName = GetChildName(childID);
        if (resolvedChildName.empty() && !IsSyntheticChildID(childID)) {
            resolvedChildName = ActorSvc::GetName(childID);
        }
        payload["childName"] = resolvedChildName;

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

        std::string jsonStr = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        const RE::FormID senderID =
            (!IsSyntheticChildID(childID) && RE::TESForm::LookupByID(childID) != nullptr) ? childID : 0;
        EmitModCallbackEvent("maras_child_event", jsonStr, 0.0f, senderID);
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
        bool hasSynthErase = false;

        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto* preChildRec = preSnap->lineage->FindChild(State::ChildId{ childID });
        const bool wasAlreadyAtStage = (syntheticID == 0) &&
                                       (preChildRec != nullptr) &&
                                       (preChildRec->stage == static_cast<State::ChildStage>(childStage));

        // If promoting from an existing synthetic record, inherit its metadata and remove the old synthetic key
        if (syntheticID != 0) {
            RE::FormID synthFormID = static_cast<RE::FormID>(syntheticID);
            if (const auto* synthRec = preSnap->lineage->FindChild(State::ChildId{ synthFormID })) {
                hasSynthErase = true;
                hadPreExistingEntry = true;
                entry = LegacyAdapters::ToLegacyChildEntry(*synthRec);
                oldGuardianA = entry.guardianAID;
                oldGuardianB = entry.guardianBID;
                wasAlreadyAdopted = entry.isAdopted;
                entry.childID = childID;
                entry.stage = childStage;
                if (!childName.empty()) entry.name = childName;
                if (guardianAID != 0 || guardianBID != 0) {
                    entry.AssignGuardians(guardianAID, guardianBID);
                }
                if (bioMotherID != 0) entry.bioMotherID = bioMotherID;
                if (bioFatherID != 0) entry.bioFatherID = bioFatherID;
            } else {
                entry.AssignGuardians(guardianAID, guardianBID);
            }
        } else {
            if (preChildRec != nullptr) {
                hadPreExistingEntry = true;
                entry = LegacyAdapters::ToLegacyChildEntry(*preChildRec);
                oldGuardianA = entry.guardianAID;
                oldGuardianB = entry.guardianBID;
                wasAlreadyAdopted = entry.isAdopted;
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

        if (entry.savedRaceIndex > 9) {
            uint8_t resolvedRace = ResolveActorFMRRaceIndex(childID);
            if (resolvedRace > 9 && entry.bioMotherID != 0) resolvedRace = ResolveActorFMRRaceIndex(entry.bioMotherID);
            if (resolvedRace > 9 && entry.bioFatherID != 0 && entry.bioFatherID != kUnknownFatherFormID) resolvedRace = ResolveActorFMRRaceIndex(entry.bioFatherID);
            if (resolvedRace <= 9) {
                entry.savedRaceIndex = resolvedRace;
            }
        }

        EvaluateChildBeastbloodInheritance(entry);
        if (entry.stage != ChildStage::Unborn && !entry.bioLineageEvaluated) {
            AdultChildProgression::GetSingleton().EvaluateChildBioLineageAtBirth(*this, entry);
        }

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& lineageW = ws.lineage.Write();
            if (hasSynthErase) {
                const State::ChildId synthKey{ static_cast<RE::FormID>(syntheticID) };
                lineageW.EraseChild(synthKey);
                if (static_cast<RE::FormID>(syntheticID) != childID) {
                    ws.household.Write().EraseChildState(synthKey);
                    ws.household.Write().EraseChildFactionSnapshot(synthKey);
                }
            }
            lineageW.UpsertChild(LegacyAdapters::ToChildRecord(entry));
        });

        if (hasSynthErase) {
            RE::FormID synthFormID = static_cast<RE::FormID>(syntheticID);
            if (synthFormID != childID) {
                // The old ID may be a physical actor (e.g. a Beeing Female child replaced by its adult); release its household state
                if (!IsSyntheticChildID(synthFormID)) {
                    AdultChildProgression::GetSingleton().ReleaseRuntimeBaseForActor(synthFormID);
                }
                PlayerChildrenService::GetSingleton().OnChildRemovedFromHousehold(synthFormID);
                PackageReconciler::GetSingleton().ReconcileActor(synthFormID);
            }
            PrismaUIDataBridge::GetSingleton().OnChildRemoved(synthFormID);
            PrismaUIDataBridge::GetSingleton().OnChildPromoted(synthFormID, childID);
        }

        if (!IsSyntheticChildID(childID) && (entry.stage == ChildStage::Child || entry.stage == ChildStage::Adult)) {
            InitializeAndApplyChildVisualsAndRPG(childID);
            if (auto refreshed = GetChildEntry(childID)) {
                entry = *refreshed;
            }
        }

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

        if (sendEvent && PrismaUIManager::GetSingleton().IsDashboardOpen()) {
            PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
            PrismaUIDataBridge::GetSingleton().SendOverviewData();
            PrismaUIDataBridge::GetSingleton().SendNPCDetail(childID);
            if (entry.guardianAID != 0 && !Utils::MatchesActor(entry.guardianAID, kPlayerFormID)) {
                PrismaUIDataBridge::GetSingleton().SendNPCDetail(entry.guardianAID);
            }
            if (entry.guardianBID != 0 && !Utils::MatchesActor(entry.guardianBID, kPlayerFormID)) {
                PrismaUIDataBridge::GetSingleton().SendNPCDetail(entry.guardianBID);
            }
        }
        return true;
    }

    bool NPCRelationshipManager::RemoveChild(RE::FormID childID) {
        ClearMarriageAdoptionPending(childID);
        auto entryOpt = GetChildEntry(childID);
        RE::FormID guardianAID = entryOpt ? entryOpt->guardianAID : 0;
        RE::FormID guardianBID = entryOpt ? entryOpt->guardianBID : 0;

        // Release household custody and restore snapshotted factions before erasing snapshot records
        PlayerChildrenService::GetSingleton().OnChildRemovedFromHousehold(childID);

        bool removed = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            const State::ChildId key{ childID };
            removed = ws.lineage.Write().EraseChild(key);
            if (removed) {
                ws.household.Write().EraseChildState(key);
                ws.household.Write().EraseChildFactionSnapshot(key);
            }
            ws.household.Write().EraseAdultChildTravel(key);
        });

        if (!IsSyntheticChildID(childID)) {
            AdultChildProgression::GetSingleton().ReleaseRuntimeBaseForActor(childID);
            if ((childID >> 24) == 0xFF) {
                auto clearAdoptable = [childID]() {
                    if (auto* adoptableFaction = FormCache::GetSingleton().GetBYOHRelationshipAdoptableFaction()) {
                        FactionReconciler::InternalFactionScope scope;
                        Utils::RemoveFromFaction(childID, adoptableFaction);
                    }
                };
                if (DeferredActorResolver::GetSingleton().IsOnMainThread()) {
                    clearAdoptable();
                } else {
                    AddCurrentEpochTask("NPCRelationshipManager::ClearAdoptableOnRemove", [clearAdoptable]() {
                        std::lock_guard lock(PapyrusInterface::GetPapyrusMutex());
                        clearAdoptable();
                    });
                }
            }
            FactionReconciler::GetSingleton().ReconcileActor(childID);
            PackageReconciler::GetSingleton().ReconcileActor(childID);
        }

        if (removed) {
            if (guardianAID != 0) UpdateSpouseChildrenFaction(guardianAID);
            if (guardianBID != 0) UpdateSpouseChildrenFaction(guardianBID);
            PrismaUIDataBridge::GetSingleton().OnChildRemoved(childID);
        }
        return removed;
    }

    bool NPCRelationshipManager::SetChildGuardian(RE::FormID childID, int slot, RE::FormID guardianID) {
        auto preOpt = GetChildEntry(childID);
        if (!preOpt) return false;
        if (guardianID != 0) EnsureRegistered(guardianID);

        bool found = false;
        RE::FormID oldGA = 0;
        RE::FormID oldGB = 0;
        ChildRegistryEntry updatedEntry;
        Utils::GuardianSetDiff diff{};

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID });
            if (!rec) {
                return;
            }
            found = true;
            updatedEntry = LegacyAdapters::ToLegacyChildEntry(*rec);
            oldGA = updatedEntry.guardianAID;
            oldGB = updatedEntry.guardianBID;
            if (slot == 0) {
                if (guardianID != 0 && Utils::MatchesActor(updatedEntry.guardianBID, guardianID)) {
                    updatedEntry.guardianBID = updatedEntry.guardianAID;
                }
                updatedEntry.guardianAID = guardianID;
            } else {
                if (guardianID != 0 && Utils::MatchesActor(updatedEntry.guardianAID, guardianID)) {
                    updatedEntry.guardianAID = updatedEntry.guardianBID;
                }
                updatedEntry.guardianBID = guardianID;
            }
            diff = Utils::DiffGuardianSets(oldGA, oldGB, updatedEntry.guardianAID, updatedEntry.guardianBID);
            ws.lineage.Write().UpsertChild(LegacyAdapters::ToChildRecord(updatedEntry));
            ws.SyncChildHouseholdState(State::ChildId{ childID });
        });

        if (!found) return false;

        UpdateChildBothParentsFaction(childID);
        SyncChildHouseholdState(childID, updatedEntry.guardianAID, updatedEntry.guardianBID);
        UpdateSpouseChildrenFaction(oldGA);
        UpdateSpouseChildrenFaction(oldGB);
        UpdateSpouseChildrenFaction(updatedEntry.guardianAID);
        UpdateSpouseChildrenFaction(updatedEntry.guardianBID);
        PrismaUIDataBridge::GetSingleton().OnChildUpdated(updatedEntry);
        if (diff.changed) {
            SendChildEvent("guardians_changed", childID, updatedEntry.guardianAID, updatedEntry.guardianBID, diff.PrimaryRemoved(), "custody_change");
        }
        return true;
    }

    std::optional<ChildRegistryEntry> NPCRelationshipManager::GetChildEntry(RE::FormID childID) const {
        if (childID == 0) return std::nullopt;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* rec = snap->lineage->FindChild(State::ChildId{ childID })) {
            return LegacyAdapters::ToLegacyChildEntry(*rec);
        }
        return std::nullopt;
    }

    void NPCRelationshipManager::UpsertChildEntry(const ChildRegistryEntry& entry) {
        if (entry.childID == 0) return;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            ws.lineage.Write().UpsertChild(LegacyAdapters::ToChildRecord(entry));
        });
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetChildren(RE::FormID guardianAID, RE::FormID guardianBID) const {
        std::vector<RE::FormID> result;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [cID, rec] : snap->lineage->AllChildren()) {
            const auto entry = LegacyAdapters::ToLegacyChildEntry(rec);
            if (entry.HasSameGuardians(guardianAID, guardianBID)) {
                result.push_back(cID.raw());
            }
        }
        return result;
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetChildrenOfActor(RE::FormID parentID) const {
        std::vector<RE::FormID> result;
        if (parentID == 0) return result;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [cKey, rec] : snap->lineage->AllChildren()) {
            const RE::FormID cID = cKey.raw();
            const auto entry = LegacyAdapters::ToLegacyChildEntry(rec);
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

    std::unordered_map<RE::FormID, ChildRegistryEntry> NPCRelationshipManager::GetAllChildren() const {
        std::unordered_map<RE::FormID, ChildRegistryEntry> result;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        result.reserve(snap->lineage->AllChildren().size());
        for (const auto& [cKey, rec] : snap->lineage->AllChildren()) {
            result.emplace(cKey.raw(), LegacyAdapters::ToLegacyChildEntry(rec));
        }
        return result;
    }

    bool NPCRelationshipManager::IsChildNPC(RE::FormID formID) const {
        if (formID == 0) return false;

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* childRec = snap->lineage->FindChild(State::ChildId{ formID })) {
            return childRec->stage != State::ChildStage::Adult;
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

        if (snap->lineage->AllPendingChildren().contains(State::BaseId{ formID })) {
            return true;
        }

        return false;
    }

    namespace {
        struct PreservedMarriageChildState {
            State::ChildId childId;
            State::GuardianSlots guardians;
            State::ActorId bioMother;
            State::ActorId bioFather;
            std::optional<State::ChildHouseholdRecord> householdState;
        };

        void DispatchPlayerSpouseMarriageAdoptions(
            NPCRelationshipManager& mgr,
            const State::WorldStateSnapshot& snap,
            RE::FormID spouseID,
            std::unordered_set<RE::FormID>& skippedChildIds) {
            if (spouseID == 0 || Utils::MatchesActor(spouseID, kPlayerFormID)) {
                return;
            }
            for (const auto& [cKey, rec] : snap.lineage->AllChildren()) {
                if (rec.stage == State::ChildStage::Adult || rec.stage == State::ChildStage::Unborn) {
                    continue;
                }
                const RE::FormID cID = cKey.raw();
                if (IsSyntheticChildID(cID)) {
                    continue;
                }
                const auto entry = LegacyAdapters::ToLegacyChildEntry(rec);
                if (!entry.HasGuardian(spouseID) || entry.HasGuardian(kPlayerFormID)) {
                    continue;
                }
                const RE::FormID otherGuardianID = entry.GetOtherGuardian(spouseID);
                bool otherGuardianDead = false;
                if (otherGuardianID != 0) {
                    if (DeceasedRegistry::GetSingleton().IsDeceased(otherGuardianID)) {
                        otherGuardianDead = true;
                    } else if (auto* otherActor = NPCIndex::GetSingleton().GetActorFromFormId(otherGuardianID)) {
                        otherGuardianDead = otherActor->IsDead();
                    }
                }
                const bool canLinkPlayerAsCoParent = (otherGuardianID == 0) ||
                                                     Utils::MatchesActor(otherGuardianID, spouseID) ||
                                                     otherGuardianDead;
                if (!canLinkPlayerAsCoParent) {
                    continue;
                }
                if (IsMarriageAdoptionPending(cID)) {
                    skippedChildIds.insert(cID);
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
                    if (otherGuardianID != 0 && !Utils::MatchesActor(otherGuardianID, spouseID)) {
                        mgr.UpdateSpouseChildrenFaction(otherGuardianID);
                    }
                    DispatchPostLock([cID, spouseID]() {
                        auto* innerVm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
                        auto* cActor = cID != 0 ? RE::TESForm::LookupByID<RE::Actor>(cID) : nullptr;
                        auto* spActor = spouseID != 0 ? RE::TESForm::LookupByID<RE::Actor>(spouseID) : nullptr;
                        if (!innerVm || !cActor || !spActor) {
                            ClearMarriageAdoptionPending(cID);
                            return;
                        }
                        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(
                            new MarriageAdoptionCallback(cID, spouseID));
                        RE::TESForm* houseArg = nullptr;
                        auto args = RE::MakeFunctionArguments(std::move(cActor), std::move(spActor), std::move(houseArg));
                        if (!innerVm->DispatchStaticCall("TTM_ServiceChildrenLogic", "AdoptChildGlobal", args, callback)) {
                            ClearMarriageAdoptionPending(cID);
                        }
                    }, "EvaluateChildCustodyOnMarriage::AdoptChildGlobal");
                    skippedChildIds.insert(cID);
                }
            }
        }
    }  // namespace

    RE::FormID NPCRelationshipManager::ResolveSingleCustodian(RE::FormID cID, const ChildRegistryEntry& entry, RE::FormID parentA, RE::FormID parentB) const {
        if (parentA == 0) return parentB;
        if (parentB == 0 || Utils::MatchesActor(parentA, parentB)) return parentA;

        const auto sSnap = Settings::GetSingleton().Snapshot();
        const auto& s = *sSnap;

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

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        auto meta = Detail::CaptureWorldActorMetadata(snap, { childID, motherID, presumedFatherID, bioFatherID });
        auto rng = Detail::MakeEngineRng();

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            ws.lineage.Write().UpsertChild(LegacyAdapters::ToChildRecord(entry));
            ws.ApplyBirthCustodyRules(
                State::ChildId{ childID },
                State::ActorId{ motherID },
                State::ActorId{ presumedFatherID },
                State::ActorId{ bioFatherID },
                *meta,
                rng);
            if (const auto* updated = ws.lineage->FindChild(State::ChildId{ childID })) {
                entry = LegacyAdapters::ToLegacyChildEntry(*updated);
            }
        });
    }

    void NPCRelationshipManager::EvaluateChildCustodyOnMarriage(RE::FormID actorA, RE::FormID actorB) {
        if (actorA == 0 || actorB == 0 || Utils::MatchesActor(actorA, actorB)) return;

        const bool playerInvolved = Utils::MatchesActor(actorA, kPlayerFormID) || Utils::MatchesActor(actorB, kPlayerFormID);
        const RE::FormID spouseID = playerInvolved ? (Utils::MatchesActor(actorA, kPlayerFormID) ? actorB : actorA) : 0;

        if (playerInvolved) {
            EnsureRegistered(kPlayerFormID);
            EnsureRegistered(spouseID);
        } else {
            EnsureRegistered(actorA);
            EnsureRegistered(actorB);
        }

        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();

        // Preserve legacy bioParentsMarried secret resolution events before clearing presumedFather/paternitySecret
        std::vector<RE::FormID> bioMarriedSecretChildIDs;
        for (const auto& [cKey, rec] : preSnap->lineage->AllChildren()) {
            const bool bioParentsMarried =
                (MatchesActor(rec.bioMother.raw(), actorA) && MatchesActor(rec.bioFather.raw(), actorB)) ||
                (MatchesActor(rec.bioMother.raw(), actorB) && MatchesActor(rec.bioFather.raw(), actorA));
            if (bioParentsMarried && (rec.paternitySecret.HasActiveSecret() || rec.presumedFather.raw() != 0)) {
                bioMarriedSecretChildIDs.push_back(cKey.raw());
            }
        }
        for (const RE::FormID cID : bioMarriedSecretChildIDs) {
            ResolvePaternitySecret(cID, /*discoveredByPartner=*/false);
        }

        std::unordered_set<RE::FormID> skippedChildIds;
        const auto postSecretSnap = State::WorldStore::GetSingleton().Snapshot();
        if (playerInvolved) {
            DispatchPlayerSpouseMarriageAdoptions(*this, *postSecretSnap, spouseID, skippedChildIds);
        }

        auto meta = Detail::CaptureWorldActorMetadata(postSecretSnap, { actorA, actorB });

        struct MarriageChildDelta {
            ChildRegistryEntry pre;
            ChildRegistryEntry post;
        };
        std::vector<MarriageChildDelta> deltas;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            std::vector<PreservedMarriageChildState> preserved;
            preserved.reserve(skippedChildIds.size());
            for (const RE::FormID skipID : skippedChildIds) {
                const State::ChildId cKey{ skipID };
                if (const auto* rec = ws.lineage->FindChild(cKey)) {
                    PreservedMarriageChildState state;
                    state.childId = cKey;
                    state.guardians = rec->guardians;
                    state.bioMother = rec->bioMother;
                    state.bioFather = rec->bioFather;
                    if (const auto* hs = ws.household->FindChildState(cKey)) {
                        state.householdState = *hs;
                    }
                    preserved.push_back(state);
                }
            }

            std::unordered_map<RE::FormID, ChildRegistryEntry> preMap;
            preMap.reserve(ws.lineage->AllChildren().size());
            for (const auto& [cKey, rec] : ws.lineage->AllChildren()) {
                preMap.emplace(cKey.raw(), LegacyAdapters::ToLegacyChildEntry(rec));
            }

            ws.EvaluateCustodyOnMarriage(State::ActorId{ actorA }, State::ActorId{ actorB }, *meta);

            for (const auto& state : preserved) {
                if (auto* rec = ws.lineage.Write().FindChildMutable(state.childId)) {
                    rec->guardians = state.guardians;
                    rec->bioMother = state.bioMother;
                    rec->bioFather = state.bioFather;
                }
                if (state.householdState.has_value()) {
                    ws.household.Write().UpsertChildState(*state.householdState);
                } else {
                    ws.household.Write().EraseChildState(state.childId);
                }
            }

            for (const auto& [cKey, postRec] : ws.lineage->AllChildren()) {
                const auto preIt = preMap.find(cKey.raw());
                if (preIt == preMap.end()) continue;
                const auto postEntry = LegacyAdapters::ToLegacyChildEntry(postRec);
                const auto diff = Utils::DiffGuardianSets(preIt->second.guardianAID, preIt->second.guardianBID,
                                                          postEntry.guardianAID, postEntry.guardianBID);
                const bool paternityCleared = (preIt->second.presumedFatherID != postEntry.presumedFatherID) ||
                                              (preIt->second.paternitySecret.HasActiveSecret() && !postEntry.paternitySecret.HasActiveSecret());
                if (diff.changed || paternityCleared) {
                    deltas.push_back({ preIt->second, postEntry });
                }
            }
        });

        bool anyNpcUpdated = false;
        for (const auto& delta : deltas) {
            const RE::FormID cID = delta.post.childID;
            const auto diff = Utils::DiffGuardianSets(delta.pre.guardianAID, delta.pre.guardianBID,
                                                      delta.post.guardianAID, delta.post.guardianBID);
            const bool bioParentsMarried =
                (MatchesActor(delta.post.bioMotherID, actorA) && MatchesActor(delta.post.bioFatherID, actorB)) ||
                (MatchesActor(delta.post.bioMotherID, actorB) && MatchesActor(delta.post.bioFatherID, actorA));
            if (bioParentsMarried && (delta.pre.presumedFatherID != 0 || delta.pre.paternitySecret.HasActiveSecret())) {
                PrismaUIDataBridge::GetSingleton().OnChildUpdated(delta.post);
            }
            if (!diff.changed) {
                continue;
            }

            if (playerInvolved) {
                const bool isCase2 = delta.pre.HasGuardian(kPlayerFormID) && !delta.pre.HasGuardian(spouseID) &&
                                     delta.post.HasGuardian(kPlayerFormID) && delta.post.HasGuardian(spouseID);
                if (isCase2) {
                    MARAS_LOG_INFO("EvaluateChildCustodyOnMarriage: Linking player's child {:08X} ('{}') to new spouse {:08X}",
                                   cID, delta.post.name, spouseID);
                } else {
                    MARAS_LOG_INFO("Transferring custody of child {:08X} ('{}') to Player/{:08X}",
                                   cID, delta.post.name, spouseID);
                }

                UpdateChildBothParentsFaction(cID);
                if (diff.PrimaryRemoved() != 0) {
                    UpdateSpouseChildrenFaction(diff.PrimaryRemoved());
                }

                if (delta.post.stage != ChildStage::Unborn) {
                    RE::FormID childHouseID = 0;
                    if (isCase2) {
                        auto& houseSvc = PlayerHouseService::GetSingleton();
                        auto& childSvc = PlayerChildrenService::GetSingleton();
                        childHouseID = childSvc.GetChildHouseFormID(cID);
                        if (childHouseID != 0 && houseSvc.GetTenantHouseFormID(spouseID) == 0) {
                            houseSvc.RelocateFamilyUnit(cID, childHouseID, true);
                            MARAS_LOG_INFO("EvaluateChildCustodyOnMarriage: Auto-relocated unhoused spouse {:08X} to child {:08X}'s house {:08X}",
                                           spouseID, cID, childHouseID);
                        }
                    }
                    SyncChildHouseholdState(cID, delta.post.guardianAID, delta.post.guardianBID, childHouseID);
                    SendChildEvent("guardians_changed", cID, delta.post.guardianAID, delta.post.guardianBID, diff.PrimaryRemoved(), "marriage");
                } else {
                    PrismaUIDataBridge::GetSingleton().OnChildUpdated(delta.post);
                }
            } else {
                const RE::FormID newCoParent = delta.post.HasGuardian(actorB) && !delta.pre.HasGuardian(actorB) ? actorB : actorA;
                const RE::FormID existingParent = Utils::MatchesActor(newCoParent, actorB) ? actorA : actorB;
                MARAS_LOG_INFO("EvaluateChildCustodyOnMarriage: Linking child {:08X} ('{}') to married pair {:08X} / {:08X} (co-parent {:08X})",
                               cID, delta.post.name, existingParent, newCoParent, newCoParent);
                UpdateChildBothParentsFaction(cID);
                if (delta.post.stage != ChildStage::Unborn) {
                    SyncChildHouseholdState(cID, delta.post.guardianAID, delta.post.guardianBID, 0);
                    SendChildEvent("guardians_changed", cID, delta.post.guardianAID, delta.post.guardianBID, diff.PrimaryRemoved(), "marriage");
                } else {
                    PrismaUIDataBridge::GetSingleton().OnChildUpdated(delta.post);
                }
                anyNpcUpdated = true;
            }
        }

        if (playerInvolved) {
            UpdateSpouseChildrenFaction(spouseID);
            UpdateSpouseChildrenFaction(kPlayerFormID);
        } else if (anyNpcUpdated) {
            UpdateSpouseChildrenFaction(actorA);
            UpdateSpouseChildrenFaction(actorB);
        }
    }

    void NPCRelationshipManager::EvaluateChildCustodyOnDivorce(RE::FormID actorA, RE::FormID actorB) {
        if (actorA == 0 || actorB == 0 || Utils::MatchesActor(actorA, actorB)) return;

        const bool playerInvolved = Utils::MatchesActor(actorA, kPlayerFormID) || Utils::MatchesActor(actorB, kPlayerFormID);
        const RE::FormID spouseID = playerInvolved ? (Utils::MatchesActor(actorA, kPlayerFormID) ? actorB : actorA) : 0;
        const bool isAFemale = (ActorSvc::GetSex(actorA) == 1);
        const bool isBFemale = (ActorSvc::GetSex(actorB) == 1);

        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        auto meta = Detail::CaptureWorldActorMetadata(preSnap, { actorA, actorB });
        auto rng = Detail::MakeEngineRng();

        struct DivorceChildDelta {
            ChildRegistryEntry pre;
            ChildRegistryEntry post;
            bool paternityResolved{ false };
            bool wasActiveUndiscovered{ false };
            bool wasSecretPartner{ false };
            bool wasSecretBio{ false };
            bool wasPregSecretPartner{ false };
            bool isUnbornFormerSpouse{ false };
            bool isPresumedFatherCustodySplit{ false };
            RE::FormID formerSpouseID{ 0 };
        };
        std::vector<DivorceChildDelta> deltas;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            std::unordered_map<RE::FormID, ChildRegistryEntry> preMap;
            preMap.reserve(ws.lineage->AllChildren().size());
            for (const auto& [cKey, rec] : ws.lineage->AllChildren()) {
                preMap.emplace(cKey.raw(), LegacyAdapters::ToLegacyChildEntry(rec));
            }

            ws.EvaluateCustodyOnDivorce(State::ActorId{ actorA }, State::ActorId{ actorB }, *meta, rng);

            for (const auto& [cKey, preEntry] : preMap) {
                auto* postRec = ws.lineage.Write().FindChildMutable(State::ChildId{ cKey });
                if (!postRec) continue;

                const bool motherIsA = MatchesActor(preEntry.bioMotherID, actorA);
                const bool motherIsB = MatchesActor(preEntry.bioMotherID, actorB);
                const RE::FormID formerSpouse = motherIsA ? actorB : actorA;

                const bool isUnbornFormerSpouse = (preEntry.stage == ChildStage::Unborn) && (motherIsA || motherIsB) &&
                                                  (preEntry.HasGuardian(formerSpouse) || MatchesActor(preEntry.presumedFatherID, formerSpouse));
                const bool isPresumedFatherSplit = (preEntry.stage != ChildStage::Unborn && preEntry.stage != ChildStage::Adult) &&
                                                   (motherIsA || motherIsB) &&
                                                   MatchesActor(preEntry.presumedFatherID, formerSpouse) &&
                                                   preEntry.paternitySecret.HasActiveSecret();
                const bool isSharedCoupleBorn = (preEntry.stage != ChildStage::Unborn && preEntry.stage != ChildStage::Adult) &&
                                                preEntry.HasSameGuardians(actorA, actorB);

                if (!isUnbornFormerSpouse && !isPresumedFatherSplit && !isSharedCoupleBorn &&
                    postRec->paternitySecret == LegacyAdapters::ToChildRecord(preEntry).paternitySecret &&
                    postRec->guardians == LegacyAdapters::ToChildRecord(preEntry).guardians) {
                    continue;
                }

                DivorceChildDelta delta;
                delta.pre = preEntry;
                delta.formerSpouseID = (motherIsA || motherIsB) ? formerSpouse : 0;
                delta.isUnbornFormerSpouse = isUnbornFormerSpouse;
                delta.isPresumedFatherCustodySplit = isPresumedFatherSplit;

                if (isUnbornFormerSpouse || isPresumedFatherSplit || isSharedCoupleBorn) {
                    const bool hadSecretFlags = preEntry.paternitySecret.HasActiveSecret() ||
                                                preEntry.paternitySecret.isSecretForPartner ||
                                                preEntry.paternitySecret.isSecretForBioFather;
                    if (hadSecretFlags) {
                        delta.paternityResolved = true;
                        delta.wasActiveUndiscovered = preEntry.paternitySecret.HasActiveSecret() && !preEntry.paternitySecret.discovered;
                        delta.wasSecretPartner = preEntry.paternitySecret.isSecretForPartner;
                        delta.wasSecretBio = preEntry.paternitySecret.isSecretForBioFather;
                        delta.wasPregSecretPartner = (preEntry.stage == ChildStage::Unborn) &&
                                                     preEntry.paternitySecret.isSecretForPartner &&
                                                     preEntry.paternitySecret.isPregnancySecretForPartner;
                        postRec->paternitySecret.discovered = true;
                        postRec->paternitySecret.discoveredByPartner = true;
                        postRec->paternitySecret.isSecret = false;
                        postRec->paternitySecret.isSecretForPartner = false;
                        postRec->paternitySecret.isSecretForBioFather = false;
                        postRec->paternitySecret.isPregnancySecretForPartner = false;
                    }
                }

                delta.post = LegacyAdapters::ToLegacyChildEntry(*postRec);
                deltas.push_back(std::move(delta));
            }
        });

        bool anyNpcSharedUpdated = false;
        for (const auto& delta : deltas) {
            const RE::FormID cID = delta.post.childID;
            if (delta.paternityResolved) {
                MARAS_LOG_INFO("[Fertility] Fully resolved paternity secret for child {:08X} (discoveredByPartner: true)", cID);
                if (delta.wasSecretPartner || delta.wasSecretBio) {
                    std::string baseOutcome;
                    if (delta.wasSecretPartner && delta.wasSecretBio) {
                        baseOutcome = "mother_confessed_both";
                    } else if (delta.wasSecretPartner) {
                        baseOutcome = "presumed_father_learnt";
                    } else {
                        baseOutcome = "mother_confessed_bio";
                    }
                    std::string eventOutcome = (delta.wasSecretPartner && delta.wasPregSecretPartner) ? (baseOutcome + "_unannounced") : baseOutcome;
                    const float childIdFloat = static_cast<float>(static_cast<int32_t>(cID));
                    EmitModCallbackEvent("maras_paternity_secret_resolved", std::move(eventOutcome), childIdFloat, RE::FormID{ 0 });
                    SkyrimNetMemoryService::GetSingleton().RecordPaternityReveal(cID, baseOutcome, "none", delta.wasPregSecretPartner);
                }

                if (delta.wasActiveUndiscovered) {
                    RE::FormID motherID = delta.post.bioMotherID;
                    if (motherID == 0) {
                        if (isAFemale) motherID = actorA;
                        else if (isBFemale) motherID = actorB;
                        else motherID = actorA;
                    }
                    RE::FormID formerSpouseID = Utils::MatchesActor(motherID, actorA) ? actorB : actorA;
                    RE::FormID bioFatherID = (delta.post.bioFatherID != 0 && delta.post.bioFatherID != kUnknownFatherFormID) ? delta.post.bioFatherID : 0;

                    MARAS_LOG_INFO("[Fertility] Divorce/Breakup: Auto-resolved paternity secret for child {:08X} ('{}')", cID, delta.post.name);
                    const float childIdFloat = static_cast<float>(static_cast<int32_t>(cID));
                    EmitModCallbackEvent("maras_paternity_secret_resolved", "divorce_revealed", childIdFloat, RE::FormID{ 0 });
                    SkyrimNetMemoryService::GetSingleton().RecordPaternityReveal(cID, "divorce_revealed");

                    if (Utils::MatchesActor(motherID, kPlayerFormID) ||
                        Utils::MatchesActor(formerSpouseID, kPlayerFormID) ||
                        Utils::MatchesActor(bioFatherID, kPlayerFormID)) {
                        std::string notifyMsg = fmt::format("Paternity secret for {} has been resolved following breakup.",
                                                            delta.post.name.empty() ? "the child" : delta.post.name);
                        QueueHUDMessage(std::move(notifyMsg));
                    }
                }
            }

            if (delta.isUnbornFormerSpouse) {
                MARAS_LOG_INFO("[Fertility] Divorce/Breakup: Cleared presumed father {:08X} and resolved pregnancy secret of mother {:08X}",
                               delta.formerSpouseID, delta.post.bioMotherID);
                PrismaUIDataBridge::GetSingleton().OnChildUpdated(delta.post);
            }

            const auto diff = Utils::DiffGuardianSets(delta.pre.guardianAID, delta.pre.guardianBID,
                                                      delta.post.guardianAID, delta.post.guardianBID);
            if (delta.post.stage != ChildStage::Unborn && delta.post.stage != ChildStage::Adult) {
                const bool sharedSplit = delta.pre.HasSameGuardians(actorA, actorB);
                if (sharedSplit || (delta.isPresumedFatherCustodySplit && diff.changed)) {
                    UpdateChildBothParentsFaction(cID);
                    SyncChildHouseholdState(cID, delta.post.guardianAID, delta.post.guardianBID, 0);
                    if (delta.isPresumedFatherCustodySplit && !sharedSplit) {
                        UpdateSpouseChildrenFaction(delta.post.bioMotherID);
                        UpdateSpouseChildrenFaction(delta.formerSpouseID);
                    }
                    if (diff.changed) {
                        SendChildEvent("guardians_changed", cID, delta.post.guardianAID, delta.post.guardianBID, diff.PrimaryRemoved(), "divorce");
                    }
                    PrismaUIDataBridge::GetSingleton().OnChildUpdated(delta.post);
                    if (!playerInvolved && sharedSplit) {
                        anyNpcSharedUpdated = true;
                    }
                } else if (delta.paternityResolved || diff.changed) {
                    PrismaUIDataBridge::GetSingleton().OnChildUpdated(delta.post);
                }
            }
        }

        if (playerInvolved) {
            UpdateSpouseChildrenFaction(spouseID);
            UpdateSpouseChildrenFaction(kPlayerFormID);
        } else if (anyNpcSharedUpdated) {
            UpdateSpouseChildrenFaction(actorA);
            UpdateSpouseChildrenFaction(actorB);
        }

        // 2. Evaluate step-in for any biological mothers following divorce
        if (isAFemale) {
            EvaluateBioFatherStepIn(actorA, actorB);
        }
        if (isBFemale) {
            EvaluateBioFatherStepIn(actorB, actorA);
        }
    }

    void NPCRelationshipManager::SyncAllMarriedCouplesChildCustody() {
        SkyrimNetMemoryService::SuppressScope suppressMemories;

        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        std::unordered_set<RE::FormID> skippedChildIds;
        for (const auto& [key, edge] : preSnap->relationships->AllEdges()) {
            if (edge.status != State::RelationshipStatus::Married) continue;
            if (Utils::MatchesActor(key.first.raw(), kPlayerFormID)) {
                DispatchPlayerSpouseMarriageAdoptions(*this, *preSnap, key.second.raw(), skippedChildIds);
            } else if (Utils::MatchesActor(key.second.raw(), kPlayerFormID)) {
                DispatchPlayerSpouseMarriageAdoptions(*this, *preSnap, key.first.raw(), skippedChildIds);
            }
        }

        auto meta = Detail::CaptureWorldActorMetadata(preSnap);
        auto rng = Detail::MakeEngineRng();

        struct SyncChildDelta {
            ChildRegistryEntry pre;
            ChildRegistryEntry post;
            bool postGuardiansMarried{ false };
        };
        std::vector<SyncChildDelta> deltas;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            std::vector<PreservedMarriageChildState> preserved;
            preserved.reserve(skippedChildIds.size());
            for (const RE::FormID skipID : skippedChildIds) {
                const State::ChildId cKey{ skipID };
                if (const auto* rec = ws.lineage->FindChild(cKey)) {
                    PreservedMarriageChildState state;
                    state.childId = cKey;
                    state.guardians = rec->guardians;
                    state.bioMother = rec->bioMother;
                    state.bioFather = rec->bioFather;
                    if (const auto* hs = ws.household->FindChildState(cKey)) {
                        state.householdState = *hs;
                    }
                    preserved.push_back(state);
                }
            }

            std::unordered_map<RE::FormID, ChildRegistryEntry> preMap;
            preMap.reserve(ws.lineage->AllChildren().size());
            for (const auto& [cKey, rec] : ws.lineage->AllChildren()) {
                preMap.emplace(cKey.raw(), LegacyAdapters::ToLegacyChildEntry(rec));
            }

            ws.SyncAllMarriedCouplesChildCustody(*meta, rng);

            for (const auto& state : preserved) {
                if (auto* rec = ws.lineage.Write().FindChildMutable(state.childId)) {
                    rec->guardians = state.guardians;
                    rec->bioMother = state.bioMother;
                    rec->bioFather = state.bioFather;
                }
                if (state.householdState.has_value()) {
                    ws.household.Write().UpsertChildState(*state.householdState);
                } else {
                    ws.household.Write().EraseChildState(state.childId);
                }
            }

            for (const auto& [cKey, postRec] : ws.lineage->AllChildren()) {
                const auto preIt = preMap.find(cKey.raw());
                if (preIt == preMap.end()) continue;
                const auto postEntry = LegacyAdapters::ToLegacyChildEntry(postRec);
                const auto diff = Utils::DiffGuardianSets(preIt->second.guardianAID, preIt->second.guardianBID,
                                                          postEntry.guardianAID, postEntry.guardianBID);
                const bool presChanged = (preIt->second.presumedFatherID != postEntry.presumedFatherID) ||
                                         (preIt->second.paternitySecret.HasActiveSecret() && !postEntry.paternitySecret.HasActiveSecret());
                if (diff.changed || presChanged) {
                    bool bothMarried = false;
                    if (postEntry.GuardianCount() == 2) {
                        bothMarried = (ws.relationships->GetStatus(
                            State::ActorId{ postEntry.guardianAID }, State::ActorId{ postEntry.guardianBID }) ==
                            State::RelationshipStatus::Married);
                    }
                    deltas.push_back({ preIt->second, postEntry, bothMarried });
                }
            }
        });

        std::unordered_set<RE::FormID> spousesToRefresh;
        for (const auto& delta : deltas) {
            const RE::FormID cID = delta.post.childID;
            if (delta.pre.presumedFatherID != 0 && delta.post.presumedFatherID == 0) {
                MARAS_LOG_INFO("[Children] Repaired invalid presumedFatherID {:08X} on child {:08X} ('{}')",
                               delta.pre.presumedFatherID, cID, delta.post.name);
            }

            const auto diff = Utils::DiffGuardianSets(delta.pre.guardianAID, delta.pre.guardianBID,
                                                      delta.post.guardianAID, delta.post.guardianBID);
            if (diff.changed && delta.post.stage != ChildStage::Unborn && delta.post.stage != ChildStage::Adult) {
                UpdateChildBothParentsFaction(cID);

                RE::FormID childHouseID = 0;
                if (delta.postGuardiansMarried && delta.post.HasGuardian(kPlayerFormID)) {
                    const RE::FormID spouseID = delta.post.GetOtherGuardian(kPlayerFormID);
                    const bool wasCase2 = delta.pre.HasGuardian(kPlayerFormID) && !delta.pre.HasGuardian(spouseID);
                    if (wasCase2 && spouseID != 0) {
                        auto& houseSvc = PlayerHouseService::GetSingleton();
                        childHouseID = PlayerChildrenService::GetSingleton().GetChildHouseFormID(cID);
                        if (childHouseID != 0 && houseSvc.GetTenantHouseFormID(spouseID) == 0) {
                            houseSvc.RelocateFamilyUnit(cID, childHouseID, true);
                        }
                    }
                }

                SyncChildHouseholdState(cID, delta.post.guardianAID, delta.post.guardianBID, childHouseID);

                if (delta.pre.guardianAID != 0) spousesToRefresh.insert(delta.pre.guardianAID);
                if (delta.pre.guardianBID != 0) spousesToRefresh.insert(delta.pre.guardianBID);
                if (delta.post.guardianAID != 0) spousesToRefresh.insert(delta.post.guardianAID);
                if (delta.post.guardianBID != 0) spousesToRefresh.insert(delta.post.guardianBID);

                if (delta.postGuardiansMarried) {
                    SendChildEvent("guardians_changed", cID, delta.post.guardianAID, delta.post.guardianBID, diff.PrimaryRemoved(), "marriage");
                }
            }
            PrismaUIDataBridge::GetSingleton().OnChildUpdated(delta.post);
        }

        for (const RE::FormID spouseID : spousesToRefresh) {
            UpdateSpouseChildrenFaction(spouseID);
        }
    }

    void NPCRelationshipManager::EvaluateChildCustodyOnDeath(RE::FormID deceasedID) {
        if (deceasedID == 0 || Utils::MatchesActor(deceasedID, kPlayerFormID)) return;

        RE::FormID highestSurvivingSpouseID = 0;
        auto& hierMgr = SpouseHierarchyManager::GetSingleton();
        const auto marriedToPlayer = GetAllMarried(kPlayerFormID);
        for (int rank = 0; rank < 3; ++rank) {
            for (auto spouseFormID : marriedToPlayer) {
                if (!Utils::MatchesActor(spouseFormID, deceasedID) && hierMgr.GetRank(spouseFormID) == rank) {
                    highestSurvivingSpouseID = spouseFormID;
                    break;
                }
            }
            if (highestSurvivingSpouseID != 0) break;
        }
        if (highestSurvivingSpouseID == 0) {
            for (auto spouseFormID : marriedToPlayer) {
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

        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        auto meta = Detail::CaptureWorldActorMetadata(preSnap, { deceasedID });

        struct UnbornRemovedInfo {
            RE::FormID childID{ 0 };
            RE::FormID guardianAID{ 0 };
            RE::FormID guardianBID{ 0 };
        };
        struct DeathChildDelta {
            ChildRegistryEntry pre;
            ChildRegistryEntry post;
        };
        std::vector<UnbornRemovedInfo> unbornRemoved;
        std::vector<DeathChildDelta> deltas;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            std::unordered_map<RE::FormID, ChildRegistryEntry> preMap;
            preMap.reserve(ws.lineage->AllChildren().size());
            for (const auto& [cKey, rec] : ws.lineage->AllChildren()) {
                preMap.emplace(cKey.raw(), LegacyAdapters::ToLegacyChildEntry(rec));
            }

            ws.EvaluateCustodyOnDeath(State::ActorId{ deceasedID }, *meta);

            for (const auto& [cID, preEntry] : preMap) {
                if (preEntry.stage == ChildStage::Adult) continue;
                if (preEntry.stage == ChildStage::Unborn && MatchesActor(preEntry.bioMotherID, deceasedID)) {
                    if (!ws.lineage->FindChild(State::ChildId{ cID })) {
                        unbornRemoved.push_back({ cID, preEntry.guardianAID, preEntry.guardianBID });
                    }
                    continue;
                }

                const bool isDeceasedGuardian = preEntry.HasGuardian(deceasedID);
                const bool isBioParent = MatchesActor(preEntry.bioMotherID, deceasedID) || MatchesActor(preEntry.bioFatherID, deceasedID);
                const bool isPresumedFather = MatchesActor(preEntry.presumedFatherID, deceasedID);
                if (!isDeceasedGuardian && !isBioParent && !isPresumedFather) continue;

                if (const auto* postRec = ws.lineage->FindChild(State::ChildId{ cID })) {
                    deltas.push_back({ preEntry, LegacyAdapters::ToLegacyChildEntry(*postRec) });
                }
            }
        });

        std::unordered_set<RE::FormID> widowedMothers;

        for (const auto& delta : deltas) {
            const RE::FormID cID = delta.post.childID;
            const bool isDeceasedGuardian = delta.pre.HasGuardian(deceasedID);
            const bool isBioParent = MatchesActor(delta.pre.bioMotherID, deceasedID) || MatchesActor(delta.pre.bioFatherID, deceasedID);
            const bool isPresumedFather = MatchesActor(delta.pre.presumedFatherID, deceasedID);

            if (delta.pre.stage == ChildStage::Unborn) {
                RE::FormID survivingParent = delta.pre.GetOtherGuardian(deceasedID);
                if (isDeceasedGuardian && survivingParent != 0 &&
                    !Utils::MatchesActor(survivingParent, kPlayerFormID) && ActorSvc::GetSex(survivingParent) == 1) {
                    widowedMothers.insert(survivingParent);
                }
                PrismaUIDataBridge::GetSingleton().OnChildUpdated(delta.post);
                continue;
            }

            if (!isDeceasedGuardian && (isBioParent || isPresumedFather)) {
                if (!delta.post.HasAnyGuardian()) {
                    SyncChildHouseholdState(cID, 0, 0);
                }
                continue;
            }

            RE::FormID survivingParent = delta.pre.GetOtherGuardian(deceasedID);
            if (survivingParent != 0 && !Utils::MatchesActor(survivingParent, kPlayerFormID) && ActorSvc::GetSex(survivingParent) == 1) {
                widowedMothers.insert(survivingParent);
            }

            if (Utils::MatchesActor(survivingParent, kPlayerFormID)) {
                const RE::FormID reassignedSpouse = delta.post.GetOtherGuardian(kPlayerFormID);
                if (reassignedSpouse != 0 && !Utils::MatchesActor(reassignedSpouse, deceasedID)) {
                    const RE::FormID targetHouseID = Utils::MatchesActor(reassignedSpouse, highestSurvivingSpouseID)
                        ? nextSpouseHouseID
                        : PlayerHouseService::GetSingleton().GetTenantHouseFormID(reassignedSpouse);
                    MARAS_LOG_INFO("Death: Spouse {:08X} deceased. Child {:08X} ('{}') reassigned to highest-ranking spouse {:08X} (house {:08X})",
                                   deceasedID, cID, delta.post.name, reassignedSpouse, targetHouseID);
                    UpdateChildBothParentsFaction(cID);
                    SyncChildHouseholdState(cID, delta.post.guardianAID, delta.post.guardianBID, targetHouseID);
                    if (targetHouseID != 0) {
                        PlayerHouseService::GetSingleton().RelocateFamilyUnit(cID, targetHouseID, false);
                    }
                    UpdateSpouseChildrenFaction(reassignedSpouse);
                    SendChildEvent("inherited", cID, delta.post.guardianAID, delta.post.guardianBID, deceasedID, "spouse_death");
                    PrismaUIDataBridge::GetSingleton().OnChildUpdated(delta.post);
                } else {
                    MARAS_LOG_INFO("Death: Spouse {:08X} deceased. Child {:08X} ('{}') stays with Player as sole guardian",
                                   deceasedID, cID, delta.post.name);
                    UpdateChildBothParentsFaction(cID);
                    SyncChildHouseholdState(cID, delta.post.guardianAID, delta.post.guardianBID);
                    SendChildEvent("inherited", cID, delta.post.guardianAID, delta.post.guardianBID, deceasedID, "spouse_death");
                    PrismaUIDataBridge::GetSingleton().OnChildUpdated(delta.post);
                }
            } else if (survivingParent != 0) {
                MARAS_LOG_INFO("Death: Parent {:08X} deceased. Child {:08X} ('{}') stays with surviving parent {:08X} as sole guardian",
                               deceasedID, cID, delta.post.name, survivingParent);
                UpdateChildBothParentsFaction(cID);
                SyncChildHouseholdState(cID, delta.post.guardianAID, delta.post.guardianBID);
                UpdateSpouseChildrenFaction(survivingParent);
                SendChildEvent("inherited", cID, delta.post.guardianAID, delta.post.guardianBID, deceasedID, "parent_death");
                PrismaUIDataBridge::GetSingleton().OnChildUpdated(delta.post);
            } else {
                ChildRegistryEntry postEntry = delta.post;
                if (postEntry.paternitySecret.HasActiveSecret()) {
                    ResolvePaternitySecret(cID, false);
                    if (auto refreshed = GetChildEntry(cID)) {
                        postEntry = *refreshed;
                    }
                }
                MARAS_LOG_INFO("Death: Parent {:08X} deceased. Child {:08X} ('{}') has no surviving guardians",
                               deceasedID, cID, postEntry.name);
                UpdateChildBothParentsFaction(cID);
                SyncChildHouseholdState(cID, 0, 0);
                SendChildEvent("inherited", cID, 0, 0, deceasedID, "parent_death");
                PrismaUIDataBridge::GetSingleton().OnChildUpdated(postEntry);
            }
        }

        for (const auto& removed : unbornRemoved) {
            MARAS_LOG_INFO("Death: Pregnant mother {:08X} deceased; removing unborn child record {:08X}",
                           deceasedID, removed.childID);
            ClearMarriageAdoptionPending(removed.childID);
            PlayerChildrenService::GetSingleton().OnChildRemovedFromHousehold(removed.childID);
            if (removed.guardianAID != 0) UpdateSpouseChildrenFaction(removed.guardianAID);
            if (removed.guardianBID != 0) UpdateSpouseChildrenFaction(removed.guardianBID);
            PrismaUIDataBridge::GetSingleton().OnChildRemoved(removed.childID);
        }

        UpdateSpouseChildrenFaction(deceasedID);

        // Evaluate step-in only for mothers who actually lost deceasedID as a co-guardian
        for (RE::FormID motherID : widowedMothers) {
            EvaluateBioFatherStepIn(motherID, deceasedID);
        }
    }

    void NPCRelationshipManager::UpdateChildBothParentsFaction(RE::FormID childID) {
        if (childID == 0) return;

        const auto entryOpt = GetChildEntry(childID);
        FactionReconciler::GetSingleton().ReconcileActor(childID);
        if (entryOpt.has_value()) {
            const RE::FormID otherParentID = entryOpt->GetOtherGuardian(kPlayerFormID);
            if (otherParentID != 0) {
                UpdateSpouseChildrenFaction(otherParentID);
            }
        }
    }

    void NPCRelationshipManager::UpdateSpouseChildrenFaction(RE::FormID spouseID) {
        if (spouseID == 0 || Utils::MatchesActor(spouseID, kPlayerFormID)) return;
        FactionReconciler::GetSingleton().ReconcileActor(spouseID);
    }

    void NPCRelationshipManager::UpdateAllSpousesChildrenFaction() {
        std::unordered_set<RE::FormID> seen;
        std::vector<RE::FormID> candidates;
        for (auto spouseID : GetAllMarried(kPlayerFormID)) {
            if (spouseID != 0 && !Utils::MatchesActor(spouseID, kPlayerFormID) && seen.insert(spouseID).second) {
                candidates.push_back(spouseID);
            }
        }
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& [cKey, rec] : snap->lineage->AllChildren()) {
                (void)cKey;
                const auto entry = LegacyAdapters::ToLegacyChildEntry(rec);
                if (entry.HasGuardian(kPlayerFormID)) {
                    RE::FormID coGuardian = entry.GetOtherGuardian(kPlayerFormID);
                    if (coGuardian != 0 && !Utils::MatchesActor(coGuardian, kPlayerFormID) && seen.insert(coGuardian).second) {
                        candidates.push_back(coGuardian);
                    }
                }
            }
        }
        FactionReconciler::GetSingleton().ReconcileActors(candidates);
    }

    RE::FormID NPCRelationshipManager::GetGuardianA(RE::FormID childID) const {
        if (childID == 0) return 0;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec ? rec->guardians.slotA.raw() : 0;
    }

    RE::FormID NPCRelationshipManager::GetGuardianB(RE::FormID childID) const {
        if (childID == 0) return 0;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec ? rec->guardians.slotB.raw() : 0;
    }

    std::pair<RE::FormID, RE::FormID> NPCRelationshipManager::GetGuardians(RE::FormID childID) const {
        if (childID == 0) return { 0, 0 };
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* rec = snap->lineage->FindChild(State::ChildId{ childID })) {
            return { rec->guardians.slotA.raw(), rec->guardians.slotB.raw() };
        }
        return { 0, 0 };
    }

    RE::FormID NPCRelationshipManager::GetCoGuardian(RE::FormID childID, RE::FormID guardianID) const {
        if (childID == 0 || guardianID == 0) return 0;
        const auto entryOpt = GetChildEntry(childID);
        return entryOpt ? entryOpt->GetOtherGuardian(guardianID) : 0;
    }

    bool NPCRelationshipManager::IsGuardian(RE::FormID childID, RE::FormID actorID) const {
        if (childID == 0 || actorID == 0) return false;
        const auto entryOpt = GetChildEntry(childID);
        return entryOpt ? entryOpt->HasGuardian(actorID) : false;
    }

    RE::FormID NPCRelationshipManager::GetBioMother(RE::FormID childID) const {
        if (childID == 0) return 0;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec ? rec->bioMother.raw() : 0;
    }

    RE::FormID NPCRelationshipManager::GetBioFather(RE::FormID childID) const {
        if (childID == 0) return 0;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        if (!rec || rec->bioFather.raw() == kUnknownFatherFormID) return 0;
        return rec->bioFather.raw();
    }

    RE::FormID NPCRelationshipManager::GetPresumedFather(RE::FormID childID) const {
        if (childID == 0) return 0;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        if (!rec) return 0;
        if (!rec->paternitySecret.isSecret && !rec->paternitySecret.discovered) {
            return 0;
        }
        const RE::FormID presID = rec->presumedFather.raw();
        if (presID == 0 ||
            MatchesActor(presID, rec->bioMother.raw()) ||
            (rec->bioFather.raw() != 0 && MatchesActor(presID, rec->bioFather.raw()))) {
            return 0;
        }
        return presID;
    }

    RE::FormID NPCRelationshipManager::GetMotherPartner(RE::FormID childID) const {
        if (childID == 0) return 0;
        const auto entryOpt = GetChildEntry(childID);
        if (!entryOpt) return 0;
        RE::FormID presID = GetPresumedFather(childID);
        if (presID != 0) {
            return presID;
        }
        RE::FormID mID = entryOpt->bioMotherID;
        if (mID == 0) return 0;

        RE::FormID coGuardian = entryOpt->GetOtherGuardian(mID);
        if (coGuardian != 0 && !MatchesActor(coGuardian, mID)) {
            return coGuardian;
        }
        return GetPrimaryPartner(mID);
    }

    bool NPCRelationshipManager::SetBioMother(RE::FormID childID, RE::FormID motherID) {
        auto entryOpt = GetChildEntry(childID);
        if (!entryOpt) return false;
        if (motherID != 0) EnsureRegistered(motherID);

        ChildRegistryEntry updated = *entryOpt;
        updated.bioMotherID = motherID;
        if (motherID != 0) {
            updated.isBiologicalChild = true;
            updated.isAdopted = false;
        } else if (updated.bioFatherID == 0) {
            updated.isBiologicalChild = false;
        }
        updated.beastbloodEvaluated = false;
        EvaluateChildBeastbloodInheritance(updated);
        updated.bioLineageEvaluated = false;
        if (updated.stage != ChildStage::Unborn) {
            AdultChildProgression::GetSingleton().EvaluateChildBioLineageAtBirth(*this, updated);
        }

        bool found = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (!ws.lineage->FindChild(State::ChildId{ childID })) {
                return;
            }
            found = true;
            ws.lineage.Write().UpsertChild(LegacyAdapters::ToChildRecord(updated));
        });
        if (!found) return false;

        MARAS_LOG_INFO("[Lineage] Set child {:08X} bio mother to {:08X} (hasBeastblood={})", childID, motherID, updated.hasBeastblood);
        if (!IsSyntheticChildID(childID)) {
            FactionReconciler::GetSingleton().ReconcileActor(childID);
        }
        PrismaUIDataBridge::GetSingleton().OnChildUpdated(updated);
        return true;
    }

    bool NPCRelationshipManager::SetBioFather(RE::FormID childID, RE::FormID fatherID) {
        auto entryOpt = GetChildEntry(childID);
        if (!entryOpt) return false;
        if (fatherID != 0) EnsureRegistered(fatherID);

        ChildRegistryEntry updated = *entryOpt;
        updated.bioFatherID = fatherID;
        if (fatherID != 0) {
            updated.isBiologicalChild = true;
            updated.isAdopted = false;
        } else if (updated.bioMotherID == 0) {
            updated.isBiologicalChild = false;
        }
        updated.beastbloodEvaluated = false;
        EvaluateChildBeastbloodInheritance(updated);
        updated.bioLineageEvaluated = false;
        if (updated.stage != ChildStage::Unborn) {
            AdultChildProgression::GetSingleton().EvaluateChildBioLineageAtBirth(*this, updated);
        }

        auto& pSec = updated.paternitySecret;
        if (updated.stage == ChildStage::Unborn) {
            const RE::FormID presumedID = GetMotherPartner(childID);
            if (presumedID != 0 && fatherID != 0 && !MatchesActor(fatherID, presumedID)) {
                auto* calendar = RE::Calendar::GetSingleton();
                const float currentDay = calendar ? calendar->GetDaysPassed() : 0.0f;
                const bool wasAlreadyKnownPregnancy = pSec.isSecret ? !pSec.isPregnancySecretForPartner : (updated.conceptionTimestamp > 0.0f && (currentDay - updated.conceptionTimestamp) >= 4.0f);
                updated.presumedFatherID = presumedID;
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
                updated.presumedFatherID = 0;
                pSec = PaternitySecret{};
            }
        } else if (updated.presumedFatherID != 0) {
            if (fatherID != 0 && MatchesActor(fatherID, updated.presumedFatherID)) {
                updated.presumedFatherID = 0;
                pSec = PaternitySecret{};
            } else {
                pSec.bioFatherName = (fatherID != 0 && fatherID != kUnknownFatherFormID) ? ActorSvc::GetName(fatherID) : "";
                if (auto* r = (fatherID != 0 && fatherID != kUnknownFatherFormID) ? ActorSvc::GetRace(fatherID) : nullptr) {
                    pSec.bioFatherRaceID = r->GetFormID();
                } else {
                    pSec.bioFatherRaceID = 0;
                }
                pSec.hasPhenotypeMismatch = (fatherID != 0 && fatherID != kUnknownFatherFormID)
                    ? HasPhenotypeMismatch(updated.presumedFatherID, fatherID)
                    : false;
            }
        }

        bool found = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (!ws.lineage->FindChild(State::ChildId{ childID })) {
                return;
            }
            found = true;
            ws.lineage.Write().UpsertChild(LegacyAdapters::ToChildRecord(updated));
        });
        if (!found) return false;

        MARAS_LOG_INFO("[Lineage] Set child {:08X} bio father to {:08X}", childID, fatherID);
        if (!IsSyntheticChildID(childID)) {
            FactionReconciler::GetSingleton().ReconcileActor(childID);
        }
        PrismaUIDataBridge::GetSingleton().OnChildUpdated(updated);
        return true;
    }

    bool NPCRelationshipManager::SetPresumedFather(RE::FormID childID, RE::FormID fatherID) {
        auto entryOpt = GetChildEntry(childID);
        if (!entryOpt) return false;
        if (fatherID != 0) EnsureRegistered(fatherID);

        ChildRegistryEntry updated = *entryOpt;
        auto& pSec = updated.paternitySecret;
        if (fatherID == 0 ||
            (updated.bioMotherID != 0 && MatchesActor(fatherID, updated.bioMotherID)) ||
            (updated.bioFatherID != 0 && MatchesActor(fatherID, updated.bioFatherID))) {
            updated.presumedFatherID = 0;
            pSec = PaternitySecret{};
        } else {
            updated.presumedFatherID = fatherID;
            if (!pSec.isSecret && !pSec.discovered) {
                auto* calendar = RE::Calendar::GetSingleton();
                pSec.isSecret = true;
                pSec.isSecretForPartner = true;
                pSec.isSecretForBioFather = true;
                pSec.isPregnancySecretForPartner = (updated.stage == ChildStage::Unborn);
                pSec.lastCheckTimestamp = calendar ? calendar->GetDaysPassed() : 0.0f;
            }
            if (updated.bioFatherID != 0 && updated.bioFatherID != kUnknownFatherFormID) {
                pSec.bioFatherName = ActorSvc::GetName(updated.bioFatherID);
                if (auto* r = ActorSvc::GetRace(updated.bioFatherID)) {
                    pSec.bioFatherRaceID = r->GetFormID();
                }
                pSec.hasPhenotypeMismatch = HasPhenotypeMismatch(fatherID, updated.bioFatherID);
            } else {
                pSec.hasPhenotypeMismatch = false;
            }
        }

        bool found = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (!ws.lineage->FindChild(State::ChildId{ childID })) {
                return;
            }
            found = true;
            ws.lineage.Write().UpsertChild(LegacyAdapters::ToChildRecord(updated));
        });
        if (!found) return false;

        MARAS_LOG_INFO("[Lineage] Set child {:08X} presumed father to {:08X}", childID, updated.presumedFatherID);
        PrismaUIDataBridge::GetSingleton().OnChildUpdated(updated);
        return true;
    }

    std::string NPCRelationshipManager::GetChildName(RE::FormID childID) const {
        if (childID == 0) return "";
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec ? rec->name : "";
    }

    void NPCRelationshipManager::InitializeAndApplyChildVisualsAndRPG(RE::FormID childID) {
        AdultChildProgression::GetSingleton().InitializeAndApplyChildVisualsAndRPG(*this, childID);
    }

    void NPCRelationshipManager::ApplyChildVisualsToActor(RE::Actor* actor, ChildRegistryEntry& entry) {
        AdultChildProgression::GetSingleton().ApplyChildVisualsToActor(actor, entry);
    }

    void NPCRelationshipManager::ApplyChildVisualsOn3DLoad(RE::Actor* actor) {
        AdultChildProgression::GetSingleton().ApplyChildVisualsOn3DLoad(*this, actor);
    }

    void NPCRelationshipManager::ReassertAllChildVisualsOnLoad() {
        AdultChildProgression::GetSingleton().ReassertAllChildVisualsOnLoad(*this);
    }

    bool NPCRelationshipManager::IsActorWerewolf(RE::FormID actorID) {
        if (actorID == 0 || actorID == kUnknownFatherFormID) {
            return false;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* childRec = snap->lineage->FindChild(State::ChildId{ actorID });
            childRec && childRec->hasBeastblood) {
            return true;
        }

        auto& cache = FormCache::GetSingleton();
        auto* wwChangeSpell = cache.GetWerewolfChangeSpell();
        auto* wwImmunitySpell = cache.GetWerewolfImmunitySpell();
        auto* wwBeastRace = cache.GetWerewolfBeastRace();

        if (Utils::MatchesActor(actorID, kPlayerFormID)) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return false;
            if (wwBeastRace && player->GetRace() == wwBeastRace) return true;
            if (wwChangeSpell && player->HasSpell(wwChangeSpell)) return true;
            if (wwImmunitySpell && player->HasSpell(wwImmunitySpell)) return true;
            return false;
        }

        RE::Actor* actor = NPCIndex::GetSingleton().GetActorFromFormId(actorID);
        if (!actor) {
            if (auto* form = RE::TESForm::LookupByID(actorID)) {
                actor = form->As<RE::Actor>();
            }
        }
        RE::TESNPC* base = actor ? actor->GetActorBase() : ActorSvc::GetActorBase(actorID);
        const RE::FormID baseID = base ? base->GetFormID() : 0;
        const RE::FormID refID = actor ? actor->GetFormID() : actorID;

        // Farkas (0x01A692 / 0x01A693) and Vilkas (0x01A694 / 0x01A695) can be cured via Purity quests CR13 / CR14
        if (baseID == 0x0001A692 || refID == 0x0001A693) {
            if (auto* cr13 = cache.GetCR13Quest(); cr13 && cr13->IsCompleted()) {
                return false;
            }
        }
        if (baseID == 0x0001A694 || refID == 0x0001A695) {
            if (auto* cr14 = cache.GetCR14Quest(); cr14 && cr14->IsCompleted()) {
                return false;
            }
        }

        // Canon werewolf NPCs in Skyrim.esm whose base NPC records do not carry CompanionsCircle
        if (baseID == 0x0001BDB0 || refID == 0x0001BDE7 || // Arnbjorn (Dark Brotherhood)
            baseID == 0x000136AC || refID == 0x0002ABBD) { // Sinding (Ill Met By Moonlight)
            return true;
        }

        auto* playerWwFaction = cache.GetPlayerWerewolfFaction();
        auto* wwFaction = cache.GetWerewolfFaction();
        auto* circleFaction = cache.GetCompanionsCircleFaction();
        auto* frostmoonFaction = cache.GetFrostmoonWerewolvesFaction();

        if (actor) {
            if (wwBeastRace && actor->GetRace() == wwBeastRace) return true;
            if (wwChangeSpell && actor->HasSpell(wwChangeSpell)) return true;
            if (wwImmunitySpell && actor->HasSpell(wwImmunitySpell)) return true;
            if (playerWwFaction && actor->IsInFaction(playerWwFaction)) return true;
            if (wwFaction && actor->IsInFaction(wwFaction)) return true;
            if (circleFaction && actor->IsInFaction(circleFaction)) return true;
            if (frostmoonFaction && actor->IsInFaction(frostmoonFaction)) return true;
        }

        if (base) {
            if (wwBeastRace && base->race == wwBeastRace) return true;
            if (auto* spellData = base->GetSpellList()) {
                for (std::uint32_t i = 0; i < spellData->numSpells; ++i) {
                    auto* sp = spellData->spells ? spellData->spells[i] : nullptr;
                    if (sp && (sp == wwChangeSpell || sp == wwImmunitySpell)) {
                        return true;
                    }
                }
            }
            for (const auto& f : base->factions) {
                if (f.rank < 0 || !f.faction) continue;
                if (f.faction == playerWwFaction ||
                    f.faction == wwFaction ||
                    f.faction == circleFaction ||
                    f.faction == frostmoonFaction) {
                    return true;
                }
            }
        }

        return false;
    }

    void NPCRelationshipManager::EvaluateChildBeastbloodInheritance(ChildRegistryEntry& entry) {
        if (entry.beastbloodEvaluated) {
            return;
        }

        const bool motherHas = (entry.bioMotherID != 0) && IsActorWerewolf(entry.bioMotherID);
        const bool fatherHas = (entry.bioFatherID != 0 && entry.bioFatherID != kUnknownFatherFormID) && IsActorWerewolf(entry.bioFatherID);
        const int wwParentCount = (motherHas ? 1 : 0) + (fatherHas ? 1 : 0);

        if (wwParentCount >= 2) {
            entry.hasBeastblood = true;
            entry.beastbloodEvaluated = true;
            MARAS_LOG_INFO("[Beastblood] Child {:08X} ('{}') inherited Beastblood (100% — both bio parents {:08X} & {:08X} are werewolves)",
                           entry.childID, entry.name, entry.bioMotherID, entry.bioFatherID);
        } else if (wwParentCount == 1) {
            std::uint64_t seed = (static_cast<std::uint64_t>(entry.childID) * 0x9E3779B97F4A7C15ULL) ^
                                 (static_cast<std::uint64_t>(entry.bioMotherID) * 0xBF58476D1CE4E5B9ULL) ^
                                 (static_cast<std::uint64_t>(entry.bioFatherID) * 0x94D049BB133111EBULL);
            seed ^= (seed >> 30);
            seed *= 0xBF58476D1CE4E5B9ULL;
            seed ^= (seed >> 27);
            seed *= 0x94D049BB133111EBULL;
            seed ^= (seed >> 31);
            entry.hasBeastblood = ((seed & 1ULL) != 0);
            entry.beastbloodEvaluated = true;
            MARAS_LOG_INFO("[Beastblood] Child {:08X} ('{}') 50% Beastblood inheritance roll (mother={:08X}[{}], father={:08X}[{}]) -> {}",
                           entry.childID, entry.name, entry.bioMotherID, motherHas, entry.bioFatherID, fatherHas,
                           entry.hasBeastblood ? "INHERITED" : "MORTAL");
        } else {
            entry.hasBeastblood = false;
            if (entry.bioMotherID != 0 ||
                (entry.bioFatherID != 0 && entry.bioFatherID != kUnknownFatherFormID) ||
                entry.isAdopted) {
                entry.beastbloodEvaluated = true;
            }
        }
    }

    bool NPCRelationshipManager::HasChildBeastblood(RE::FormID childID) const {
        if (childID == 0) return false;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec ? rec->hasBeastblood : false;
    }

    bool NPCRelationshipManager::SetChildBeastblood(RE::FormID childID, bool hasBeastblood) {
        if (childID == 0) return false;
        std::optional<ChildRegistryEntry> updatedEntry;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID })) {
                rec->hasBeastblood = hasBeastblood;
                rec->beastbloodEvaluated = true;
                updatedEntry = LegacyAdapters::ToLegacyChildEntry(*rec);
            }
        });
        if (!updatedEntry) return false;
        if (!IsSyntheticChildID(childID)) {
            FactionReconciler::GetSingleton().ReconcileActor(childID);
        }
        PrismaUIDataBridge::GetSingleton().OnChildUpdated(*updatedEntry);
        return true;
    }

}  // namespace MARAS
