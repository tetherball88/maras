#include "core/AdultChildTravelService.h"

#include <random>
#include <unordered_set>

#include "core/DeceasedRegistry.h"
#include "core/FormCache.h"
#include "core/HomeCellService.h"
#include "core/NPCIndex.h"
#include "core/NPCRelationshipManager.h"
#include "core/PackageOverrideService.h"
#include "core/PackageReconciler.h"
#include "core/PerspectiveEngineAdapters.h"
#include "core/PlayerChildrenService.h"
#include "core/PlayerHouseService.h"
#include "core/WorldStateLegacyAdapters.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"
#include "services/ActorService.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"

namespace MARAS {

    namespace {
        constexpr float kMinStayDurationDays = 2.0f;
        constexpr float kMaxStayDurationDays = 4.0f;
        constexpr float kMaxTravelTimeoutDays = 10.0f;
        constexpr float kArrivalDistanceUnits = 2500.0f;
        constexpr int kFamilyVisitChancePercent = 30;

        bool HasActorReachedDestination(RE::Actor* actor, const AdultChildTravelState& state) {
            if (!actor || state.targetMarkerID == 0) return false;
            auto* targetRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(state.targetMarkerID);
            if (!targetRef) return false;

            auto* actorCell = actor->GetParentCell();
            auto* targetCell = targetRef->GetParentCell();

            if (actorCell && targetCell && actorCell == targetCell) {
                if (actorCell->IsInteriorCell()) {
                    return true;
                }
                const float distSq = actor->GetPosition().GetSquaredDistance(targetRef->GetPosition());
                if (distSq <= (kArrivalDistanceUnits * kArrivalDistanceUnits)) {
                    return true;
                }
            }

            if (state.targetCellID != 0 && actorCell && actorCell->GetFormID() == state.targetCellID) {
                return true;
            }

            return false;
        }

        std::string GetFormDisplayName(const RE::TESForm* form) {
            if (!form) return {};
            if (const auto* full = form->As<RE::TESFullName>()) {
                const char* n = full->GetFullName();
                if (n && n[0] != '\0') {
                    return n;
                }
            }
            const char* edid = form->GetFormEditorID();
            if (edid && edid[0] != '\0') {
                return edid;
            }
            return {};
        }
    }  // namespace

    AdultChildTravelService& AdultChildTravelService::GetSingleton() {
        static AdultChildTravelService instance;
        return instance;
    }

    void AdultChildTravelService::BuildDestinationIndex() {
        std::unique_lock lock(m_mutex);
        m_innDestinations.clear();

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            MARAS_LOG_WARN("AdultChildTravelService: TESDataHandler unavailable during BuildDestinationIndex");
            return;
        }

        auto* kwInn = FormCache::GetSingleton().GetLocTypeInnKeyword();
        if (!kwInn) {
            MARAS_LOG_WARN("AdultChildTravelService: LocTypeInn keyword unavailable");
            return;
        }

        auto* centerLCRT = FormCache::GetSingleton().GetLocationCenterMarkerRefType();
        std::unordered_set<RE::FormID> seenLocations;

        for (auto* cell : dataHandler->interiorCells) {
            if (!cell) continue;

            auto* location = cell->GetLocation();
            if (!location || !location->HasKeyword(kwInn)) continue;

            RE::FormID locID = location->GetFormID();
            if (locID == 0 || seenLocations.contains(locID)) continue;

            RE::FormID centerMarkerID = 0;
            RE::FormID fallbackMarkerID = 0;

            cell->ForEachReference([&](RE::TESObjectREFR* ref) {
                if (!ref || ref->IsDisabled() || ref->IsDeleted()) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                if (centerLCRT) {
                    if (auto* lrt = ref->extraList.GetByType<RE::ExtraLocationRefType>()) {
                        if (lrt->locRefType == centerLCRT) {
                            centerMarkerID = ref->GetFormID();
                            return RE::BSContainer::ForEachResult::kStop;
                        }
                    }
                }
                if (fallbackMarkerID == 0 && ref->IsPersistent()) {
                    fallbackMarkerID = ref->GetFormID();
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });

            RE::FormID chosenMarkerID = (centerMarkerID != 0) ? centerMarkerID : fallbackMarkerID;
            if (chosenMarkerID == 0) continue;

            std::string innName = GetFormDisplayName(location);
            if (innName.empty()) {
                innName = GetFormDisplayName(cell);
            }
            if (innName.empty()) {
                innName = "Local Inn";
            }

            seenLocations.insert(locID);
            m_innDestinations.push_back(TravelDestination{
                chosenMarkerID,
                locID,
                cell->GetFormID(),
                std::move(innName),
                false});
        }

        MARAS_LOG_INFO("AdultChildTravelService: Indexed {} Inn/Tavern destinations across Skyrim",
                       m_innDestinations.size());
    }

    std::vector<TravelDestination> AdultChildTravelService::BuildFamilyVisitDestinations(RE::FormID childID) const {
        std::vector<TravelDestination> familyDests;
        std::unordered_set<RE::FormID> seenMarkers;

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto childView = Projection::PerspectiveView::ForActor(
            worldSnap, spatialSnap, State::ActorId{ childID }, &GetEngineWerewolfStateProvider());

        const auto perceivedOpt = childView.GetPerceivedChild(State::ChildId{ childID });
        if (!perceivedOpt.has_value() || perceivedOpt->stage == State::ChildStage::Unborn) {
            return familyDests;
        }
        const auto& perceived = *perceivedOpt;

        auto addDest = [&](RE::FormID markerID, RE::FormID locID, std::string label) {
            if (markerID == 0 || !HasPhysicalActor(markerID) || seenMarkers.contains(markerID)) {
                return;
            }
            RE::FormID cellID = 0;
            if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(markerID)) {
                if (auto* cell = ref->GetParentCell()) {
                    cellID = cell->GetFormID();
                }
            }
            seenMarkers.insert(markerID);
            familyDests.push_back(TravelDestination{
                markerID,
                locID,
                cellID,
                std::move(label),
                true});
        };

        const auto visitableParents = childView.GetVisitableFamilyParentIds(State::ChildId{ childID });
        auto isSlotMarriedToPlayer = [&](State::ActorId slot) -> bool {
            if (slot.IsNull() || slot.IsPlayer()) {
                return false;
            }
            const auto pairOpt = childView.GetPerceivedPair(State::ActorId::Player(), slot);
            return pairOpt.has_value() && pairOpt->status == State::RelationshipStatus::Married;
        };

        const bool isPlayerRelated =
            std::find(visitableParents.begin(), visitableParents.end(), State::ActorId::Player()) != visitableParents.end() ||
            perceived.guardians.HasGuardian(State::ActorId::Player()) ||
            perceived.bioMother.IsPlayer() ||
            perceived.perceivedBioFather.IsPlayer() ||
            isSlotMarriedToPlayer(perceived.guardians.slotA) ||
            isSlotMarriedToPlayer(perceived.guardians.slotB);

        if (isPlayerRelated) {
            for (const auto& houseRec : childView.Household().AllHouses()) {
                if (!houseRec.markerId.IsNull()) {
                    std::string hName = PlayerHouseService::GetHouseName(houseRec.houseId.raw());
                    addDest(houseRec.markerId.raw(), houseRec.houseId.raw(), hName.empty() ? "Family Home" : std::move(hName));
                }
            }
        }

        for (const State::ActorId pActor : visitableParents) {
            const RE::FormID pID = pActor.raw();
            if (pActor.IsNull() || pActor.IsPlayer()) {
                continue;
            }

            const State::HouseId pHouseId = childView.Household().GetTenantHouse(pID);
            if (!pHouseId.IsNull()) {
                if (const auto* houseRec = childView.Household().FindHouse(pHouseId); houseRec && !houseRec->markerId.IsNull()) {
                    std::string hName = PlayerHouseService::GetHouseName(pHouseId.raw());
                    addDest(houseRec->markerId.raw(), pHouseId.raw(), hName.empty() ? "Parents' Home" : std::move(hName));
                    continue;
                }
            }

            std::string pName = ResolveActorNameSafe(pID, *worldSnap, *spatialSnap);
            if (auto* pActorRef = RE::TESForm::LookupByID<RE::Actor>(pID)) {
                const RE::FormID origMarker = HomeCellService::GetSingleton().GetNpcOriginalHouseCenterMarker(pActorRef);
                if (origMarker != 0) {
                    addDest(origMarker, 0, pName.empty() ? "Parents' Home" : std::move(pName));
                    continue;
                }
            }

            addDest(pID, 0, pName.empty() ? "Parents" : std::move(pName));
        }

        return familyDests;
    }

    TravelDestination AdultChildTravelService::PickNextDestination(RE::FormID childID, RE::FormID currentMarkerID) const {
        auto familyDests = BuildFamilyVisitDestinations(childID);

        std::vector<TravelDestination> innCopy;
        {
            std::shared_lock lock(m_mutex);
            innCopy = m_innDestinations;
        }

        thread_local std::mt19937 rng(std::random_device{}());

        bool chooseFamily = false;
        if (!familyDests.empty() && !innCopy.empty()) {
            std::uniform_int_distribution<int> rollDist(1, 100);
            chooseFamily = (rollDist(rng) <= kFamilyVisitChancePercent);
        } else if (!familyDests.empty()) {
            chooseFamily = true;
        }

        const auto& pool = chooseFamily ? familyDests : innCopy;
        if (pool.empty()) {
            return {};
        }

        std::vector<TravelDestination> candidates;
        candidates.reserve(pool.size());
        for (const auto& d : pool) {
            if (d.markerRefID != 0 && d.markerRefID != currentMarkerID && HasPhysicalActor(d.markerRefID)) {
                candidates.push_back(d);
            }
        }

        if (candidates.empty()) {
            for (const auto& d : pool) {
                if (d.markerRefID != 0 && HasPhysicalActor(d.markerRefID)) {
                    candidates.push_back(d);
                }
            }
        }

        if (candidates.empty()) {
            return {};
        }

        std::uniform_int_distribution<size_t> idxDist(0, candidates.size() - 1);
        return candidates[idxDist(rng)];
    }

    bool AdultChildTravelService::AssignOrUpdateDestination(RE::FormID childID, bool forceRotate) {
        if (childID == 0 || IsSyntheticChildID(childID)) return false;

        auto& relMgr = NPCRelationshipManager::GetSingleton();
        if (relMgr.GetChildStage(childID) != ChildStage::Adult) {
            return false;
        }

        // Ensure adult child is never enrolled as a young household child
        PlayerChildrenService::GetSingleton().OnChildRemovedFromHousehold(childID);

        // Guard against evicting a housed adult tenant or player spouse
        if (PlayerHouseService::GetSingleton().GetTenantHouseFormID(childID) != 0 ||
            relMgr.IsMarried(childID, kPlayerFormID)) {
            const bool hadTravel =
                State::WorldStore::GetSingleton().Snapshot()->household->FindAdultChildTravel(State::ChildId{ childID }) != nullptr;
            if (hadTravel) {
                State::WorldStore::GetSingleton().Modify([childID](State::WorldState& ws) {
                    ws.household.Write().EraseAdultChildTravel(State::ChildId{ childID });
                });
            }
            PackageReconciler::GetSingleton().ReconcileActor(childID);
            return false;
        }

        RE::FormID existingMarkerID = 0;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            if (const auto* rec = snap->household->FindAdultChildTravel(State::ChildId{ childID })) {
                existingMarkerID = rec->targetMarkerId.raw();
            }
        }

        auto* keyword = FormCache::GetSingleton().GetChildSandboxAroundParentKeyword();
        auto* travelPkg = FormCache::GetSingleton().GetAdultChildTravelSandboxPackage();
        if (!keyword || !travelPkg) {
            MARAS_LOG_WARN("AdultChildTravelService: Missing 0x377 keyword or 0xDD3 travel package for adult child {:08X}", childID);
            return false;
        }

        if (!forceRotate && existingMarkerID != 0 && HasPhysicalActor(existingMarkerID)) {
            PackageReconciler::GetSingleton().ReconcileActor(childID);
            return true;
        }

        TravelDestination nextDest = PickNextDestination(childID, existingMarkerID);
        if (nextDest.markerRefID == 0) {
            MARAS_LOG_WARN("AdultChildTravelService: No valid travel destination found for adult child {:08X}", childID);
            return false;
        }

        auto* calendar = RE::Calendar::GetSingleton();
        float currentDay = calendar ? calendar->GetDaysPassed() : 0.0f;

        thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> stayDist(kMinStayDurationDays, kMaxStayDurationDays);
        const float rolledStayDays = stayDist(rng);

        AdultChildTravelState newState{
            nextDest.markerRefID,
            nextDest.locationID,
            nextDest.cellID,
            nextDest.name,
            currentDay,
            -1.0f,
            rolledStayDays,
            nextDest.isFamilyVisit};

        if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(childID)) {
            if (HasActorReachedDestination(actor, newState)) {
                newState.arrivedGameDay = currentDay;
            }
        }

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            ws.household.Write().UpsertAdultChildTravel(
                State::ChildId{ childID },
                LegacyAdapters::ToAdultChildTravelRecord(newState));
        });

        PackageReconciler::GetSingleton().ReconcileActor(childID);

        MARAS_LOG_INFO("AdultChildTravelService: Assigned adult child {:08X} ('{}') -> '{}' (marker {:08X}, stayDays={:.1f}, arrived={}, familyVisit={})",
                       childID, ActorSvc::GetName(childID), nextDest.name, nextDest.markerRefID,
                       rolledStayDays, newState.arrivedGameDay >= 0.0f, nextDest.isFamilyVisit);
        return true;
    }

    void AdultChildTravelService::UnregisterAdultChild(RE::FormID childID) {
        if (childID == 0) return;
        const bool hadTravel =
            State::WorldStore::GetSingleton().Snapshot()->household->FindAdultChildTravel(State::ChildId{ childID }) != nullptr;
        if (hadTravel) {
            State::WorldStore::GetSingleton().Modify([childID](State::WorldState& ws) {
                ws.household.Write().EraseAdultChildTravel(State::ChildId{ childID });
            });
        }
        if (!IsSyntheticChildID(childID)) {
            PackageReconciler::GetSingleton().ReconcileActor(childID);
        }
    }

    void AdultChildTravelService::Tick(float currentGameDay) {
        if (currentGameDay < 0.0f) return;

        std::vector<RE::FormID> adultChildIDs;
        for (const auto& [cID, entry] : NPCRelationshipManager::GetSingleton().GetAllChildren()) {
            if (!IsSyntheticChildID(cID) && entry.stage == ChildStage::Adult) {
                adultChildIDs.push_back(cID);
            }
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto& activeTravel = worldSnap->household->AllAdultChildTravel();

        for (RE::FormID cID : adultChildIDs) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(cID);
            if (!actor || actor->IsDead() || actor->IsDisabled() || actor->IsDeleted()) continue;
            if (Utils::IsPlayerFollower(actor) || actor->IsInCombat() || actor->GetCurrentScene() != nullptr) {
                continue;
            }

            bool shouldRotate = false;
            bool markArrived = false;
            AdultChildTravelState updatedState{};

            auto it = activeTravel.find(State::ChildId{ cID });
            if (it == activeTravel.end() || it->second.targetMarkerId.raw() == 0) {
                shouldRotate = true;
            } else {
                updatedState = LegacyAdapters::ToLegacyAdultChildTravelState(it->second);
                if (updatedState.arrivedGameDay < 0.0f) {
                    // Still walking to destination — check if they have arrived
                    if (HasActorReachedDestination(actor, updatedState)) {
                        updatedState.arrivedGameDay = currentGameDay;
                        markArrived = true;
                        MARAS_LOG_INFO("AdultChildTravelService: Adult child {:08X} ('{}') reached '{}' on day {:.2f} — staying for {:.1f} days",
                                       cID, ActorSvc::GetName(cID), updatedState.destinationName, currentGameDay, updatedState.stayDurationDays);
                    } else if (updatedState.assignedGameDay >= 0.0f &&
                               (currentGameDay - updatedState.assignedGameDay) >= kMaxTravelTimeoutDays) {
                        MARAS_LOG_INFO("AdultChildTravelService: Adult child {:08X} ('{}') travel to '{}' timed out after {:.1f} days; picking new destination",
                                       cID, ActorSvc::GetName(cID), updatedState.destinationName, currentGameDay - updatedState.assignedGameDay);
                        shouldRotate = true;
                    }
                } else {
                    // Already arrived — stay at location for stayDurationDays (2-4 days) before rotating
                    const float stayDays = std::clamp(updatedState.stayDurationDays, kMinStayDurationDays, kMaxStayDurationDays);
                    if ((currentGameDay - updatedState.arrivedGameDay) >= stayDays) {
                        shouldRotate = true;
                    }
                }
            }

            if (markArrived) {
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    if (const auto* existing = ws.household.Read().FindAdultChildTravel(State::ChildId{ cID })) {
                        if (existing->targetMarkerId.raw() == updatedState.targetMarkerID) {
                            ws.household.Write().UpsertAdultChildTravel(
                                State::ChildId{ cID },
                                LegacyAdapters::ToAdultChildTravelRecord(updatedState));
                        }
                    }
                });
            }

            if (shouldRotate) {
                AssignOrUpdateDestination(cID, /*forceRotate=*/true);
            }
        }
    }

    void AdultChildTravelService::OnDayChanged(float currentGameDay) {
        Tick(currentGameDay);
    }

    void AdultChildTravelService::ReassertAllOnLoad() {
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        std::vector<RE::FormID> adultChildIDs;
        for (const auto& [cKey, rec] : worldSnap->lineage->AllChildren()) {
            if (!cKey.IsSynthetic() && rec.stage == State::ChildStage::Adult) {
                adultChildIDs.push_back(cKey.raw());
            }
        }

        for (RE::FormID cID : adultChildIDs) {
            AssignOrUpdateDestination(cID, /*forceRotate=*/false);
        }
    }

    RE::FormID AdultChildTravelService::GetCurrentTargetMarkerID(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->household->FindAdultChildTravel(State::ChildId{ childID });
        return rec ? rec->targetMarkerId.raw() : 0;
    }

    std::string AdultChildTravelService::GetCurrentDestinationName(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->household->FindAdultChildTravel(State::ChildId{ childID });
        if (!rec || rec->destinationName.empty()) {
            return "Traveling Skyrim";
        }
        if (rec->arrivedGameDay < 0.0f) {
            return "Traveling to " + rec->destinationName;
        }
        if (rec->isFamilyVisit) {
            return "Visiting " + rec->destinationName;
        }
        return "Staying at " + rec->destinationName;
    }

    void AdultChildTravelService::Revert() {
        MARAS_LOG_INFO("AdultChildTravelService: reverted runtime travel states");
    }

    nlohmann::json AdultChildTravelService::DumpJson(const Projection::PerspectiveView& view) const {
        std::size_t indexedInnCount = 0;
        {
            std::shared_lock lock(m_mutex);
            indexedInnCount = m_innDestinations.size();
        }

        const auto& activeTravel = view.Household().AllAdultChildTravel();

        nlohmann::json j = nlohmann::json::object();
        j["indexedInnCount"] = indexedInnCount;

        auto isFamilyDestinationVisible = [&](State::ChildId childKey, const AdultChildTravelState& state) -> bool {
            if (!state.isFamilyVisit || view.CanViewerKnowTrueBioFather(childKey)) {
                return true;
            }
            const auto perceivedOpt = view.GetPerceivedChild(childKey);
            if (!perceivedOpt.has_value()) {
                return false;
            }
            const auto& perceived = *perceivedOpt;
            const auto visitableParents = view.GetVisitableFamilyParentIds(childKey);

            auto isSlotMarriedToPlayer = [&](State::ActorId slot) -> bool {
                if (slot.IsNull() || slot.IsPlayer()) {
                    return false;
                }
                const auto pairOpt = view.GetPerceivedPair(State::ActorId::Player(), slot);
                return pairOpt.has_value() && pairOpt->status == State::RelationshipStatus::Married;
            };

            const bool isPlayerRelated =
                std::find(visitableParents.begin(), visitableParents.end(), State::ActorId::Player()) != visitableParents.end() ||
                perceived.guardians.HasGuardian(State::ActorId::Player()) ||
                perceived.bioMother.IsPlayer() ||
                perceived.perceivedBioFather.IsPlayer() ||
                isSlotMarriedToPlayer(perceived.guardians.slotA) ||
                isSlotMarriedToPlayer(perceived.guardians.slotB);

            if (isPlayerRelated) {
                for (const auto& houseRec : view.Household().AllHouses()) {
                    if ((state.targetMarkerID != 0 && houseRec.markerId.raw() == state.targetMarkerID) ||
                        (state.targetLocationID != 0 && houseRec.houseId.raw() == state.targetLocationID)) {
                        return true;
                    }
                }
            }

            std::vector<State::ActorId> visibleParents = visitableParents;
            auto appendVisible = [&](State::ActorId pid) {
                if (pid.IsNull() || pid.IsPlayer()) {
                    return;
                }
                if (std::find(visibleParents.begin(), visibleParents.end(), pid) == visibleParents.end()) {
                    visibleParents.push_back(pid);
                }
            };
            appendVisible(perceived.guardians.slotA);
            appendVisible(perceived.guardians.slotB);
            appendVisible(perceived.bioMother);
            appendVisible(perceived.perceivedBioFather);
            if (!perceived.hideSecretFromViewer) {
                appendVisible(perceived.presumedFather);
            }

            for (const State::ActorId pId : visibleParents) {
                if (pId.IsNull() || pId.IsPlayer()) {
                    continue;
                }
                if (state.targetMarkerID == pId.raw()) {
                    return true;
                }
                const State::HouseId tenantHouseId = view.Household().GetTenantHouse(pId.raw());
                if (!tenantHouseId.IsNull()) {
                    if (state.targetLocationID == tenantHouseId.raw()) {
                        return true;
                    }
                    if (const auto* houseRec = view.Household().FindHouse(tenantHouseId);
                        houseRec && !houseRec->markerId.IsNull() && houseRec->markerId.raw() == state.targetMarkerID) {
                        return true;
                    }
                }
                if (auto* pActor = RE::TESForm::LookupByID<RE::Actor>(pId.raw())) {
                    const RE::FormID origMarker = HomeCellService::GetSingleton().GetNpcOriginalHouseCenterMarker(pActor);
                    if (origMarker != 0 && origMarker == state.targetMarkerID) {
                        return true;
                    }
                }
            }
            return false;
        };

        nlohmann::json activeArray = nlohmann::json::array();
        for (const auto& [childKey, rec] : activeTravel) {
            if (!view.IsChildVisible(childKey)) {
                continue;
            }
            const RE::FormID childID = childKey.raw();
            AdultChildTravelState state = LegacyAdapters::ToLegacyAdultChildTravelState(rec);
            if (!isFamilyDestinationVisible(childKey, state)) {
                state.targetMarkerID = 0;
                state.targetLocationID = 0;
                state.targetCellID = 0;
                state.destinationName = "Traveling";
                state.isFamilyVisit = false;
            }

            const auto perceivedOpt = view.GetPerceivedChild(childKey);
            std::string childName = (perceivedOpt && !perceivedOpt->name.empty())
                                        ? perceivedOpt->name
                                        : ActorSvc::GetName(childID);

            nlohmann::json entry = nlohmann::json::object();
            entry["childFormId"] = std::format("{:08X}", childID);
            entry["childName"] = std::move(childName);
            entry["targetMarkerFormId"] = std::format("{:08X}", state.targetMarkerID);
            entry["targetLocationFormId"] = std::format("{:08X}", state.targetLocationID);
            entry["targetCellFormId"] = std::format("{:08X}", state.targetCellID);
            entry["destinationName"] = state.destinationName;
            entry["assignedGameDay"] = state.assignedGameDay;
            entry["arrivedGameDay"] = state.arrivedGameDay;
            entry["stayDurationDays"] = state.stayDurationDays;
            entry["isFamilyVisit"] = state.isFamilyVisit;
            activeArray.push_back(std::move(entry));
        }
        j["activeDestinations"] = std::move(activeArray);
        return j;
    }

    nlohmann::json AdultChildTravelService::DumpJson() const {
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto view = Projection::PerspectiveView::ForDashboard(
            worldSnap, spatialSnap, &GetEngineWerewolfStateProvider());
        return DumpJson(view);
    }

}  // namespace MARAS
