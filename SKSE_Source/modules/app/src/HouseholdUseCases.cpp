#include "maras/app/HouseholdUseCases.h"

#include "maras/app/AppContext.h"
#include "maras/app/LineageUseCases.h"
#include "maras/domain/household/DeceasedQueries.h"
#include "maras/domain/household/HouseholdRules.h"
#include "maras/domain/household/SpouseAssetRules.h"
#include "maras/domain/household/TravelRules.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/BedOwnershipReconciler.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FactionReconciler.h"
#include "maras/engine/GlobalVarReconciler.h"
#include "maras/engine/HouseholdEngineOps.h"
#include "maras/engine/PackageReconciler.h"
#include "maras/engine/PlayerChildrenEngineOps.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/foundation/IRng.h"
#include "maras/foundation/StrongIds.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <random>
#include <unordered_set>
#include <utility>

namespace MARAS::App {

    namespace {

        constexpr float kArrivalDistanceUnits = 2500.0f;

        class LocalEngineRng final : public Foundation::IRng {
        public:
            LocalEngineRng()
                : engine_(std::random_device{}()) {}

            [[nodiscard]] float NextFloat(float minVal, float maxVal) override {
                if (maxVal <= minVal) {
                    return minVal;
                }
                std::uniform_real_distribution<float> dist(minVal, maxVal);
                return dist(engine_);
            }

            [[nodiscard]] std::int32_t NextInt(std::int32_t minVal, std::int32_t maxVal) override {
                if (maxVal <= minVal) {
                    return minVal;
                }
                std::uniform_int_distribution<std::int32_t> dist(minVal, maxVal);
                return dist(engine_);
            }

        private:
            std::mt19937 engine_;
        };

        [[nodiscard]] bool IsChildTenantEntry(
            const State::WorldStateSnapshot& snap,
            RE::FormID tenantFormID) noexcept {
            if (Foundation::IsSyntheticChildRawId(tenantFormID)) {
                return true;
            }
            return snap.lineage && snap.lineage->FindChild(State::ChildId{ tenantFormID }) != nullptr;
        }

        [[nodiscard]] bool HasLivingChildWithPlayer(
            const State::WorldStateSnapshot& snap,
            State::ActorId actorId) noexcept {
            if (!snap.lineage || actorId.IsNull() || actorId.IsPlayer()) {
                return false;
            }
            for (const auto& [cid, rec] : snap.lineage->AllChildren()) {
                (void)cid;
                if (rec.stage == Foundation::ChildStage::Adult ||
                    rec.stage == Foundation::ChildStage::Unborn) {
                    continue;
                }
                if (Domain::Household::IsDeceased(snap, rec.childId.AsActorId())) {
                    continue;
                }
                if (rec.guardians.HasGuardian(State::ActorId::Player()) &&
                    rec.guardians.HasGuardian(actorId)) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool HasActorReachedTravelDestination(
            RE::Actor* actor,
            const State::AdultChildTravelRecord& rec) {
            if (!actor || rec.targetMarkerId.IsNull()) {
                return false;
            }
            auto* targetRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(rec.targetMarkerId.raw());
            if (!targetRef) {
                return false;
            }

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

            if (!rec.targetCellId.IsNull() && actorCell &&
                actorCell->GetFormID() == rec.targetCellId.raw()) {
                return true;
            }

            return false;
        }

        [[nodiscard]] bool IsTravelEligibleAdultChild(
            const State::WorldStateSnapshot& snap,
            RE::FormID childID) noexcept {
            if (childID == 0 || Foundation::IsSyntheticChildRawId(childID) || !snap.lineage || !snap.household) {
                return false;
            }
            const auto* rec = snap.lineage->FindChild(State::ChildId{ childID });
            if (!rec || rec->stage != Foundation::ChildStage::Adult) {
                return false;
            }
            if (Domain::Household::IsDeceased(snap, State::ActorId{ childID })) {
                return false;
            }
            if (!snap.household->GetTenantHouse(childID).IsNull()) {
                return false;
            }
            if (snap.relationships &&
                snap.relationships->GetStatus(State::ActorId{ childID }, State::ActorId::Player()) ==
                    Foundation::RelationshipStatus::Married) {
                return false;
            }
            return true;
        }

        [[nodiscard]] std::string ResolveActorDisplayName(
            RE::FormID formID,
            const State::WorldStateSnapshot& world,
            const State::SpatialActorSnapshot& spatial) {
            if (formID == 0) {
                return {};
            }
            if (const auto* sample = spatial.Find(State::RefId{ formID })) {
                const auto sv = sample->DisplayNameView();
                if (!sv.empty()) {
                    return std::string(sv);
                }
            }
            if (world.lineage) {
                if (const auto* child = world.lineage->FindChild(State::ChildId{ formID });
                    child && !child->name.empty()) {
                    return child->name;
                }
            }
            std::string name = ActorSvc::GetName(formID);
            if (!name.empty()) {
                return name;
            }
            return std::format("{:08X}", formID);
        }

        [[nodiscard]] std::vector<Domain::Household::TravelDestinationCandidate> BuildFamilyVisitDestinations(
            const State::WorldStateSnapshot& world,
            const State::SpatialActorSnapshot& spatial,
            RE::FormID childID) {
            std::vector<Domain::Household::TravelDestinationCandidate> out;
            if (!world.lineage || !world.household) {
                return out;
            }

            const State::ChildId cid{ childID };
            const auto view = Projection::PerspectiveView::ForActor(
                std::make_shared<State::WorldStateSnapshot>(world),
                std::make_shared<State::SpatialActorSnapshot>(spatial),
                State::ActorId{ childID });

            const auto perceived = view.GetPerceivedChild(cid);
            if (!perceived || perceived->stage != Foundation::ChildStage::Adult) {
                return out;
            }

            std::unordered_set<std::uint32_t> seenMarkers;
            auto appendMarker = [&](RE::FormID markerID, RE::FormID explicitLocID, std::string label) {
                if (markerID == 0 || !seenMarkers.insert(markerID).second) {
                    return;
                }
                Domain::Household::TravelDestinationCandidate cand{};
                cand.targetMarkerId = State::RefId{ markerID };
                cand.targetLocationId = State::LocationId{ explicitLocID };
                cand.isFamilyVisit = true;
                cand.destinationName = label.empty() ? "Visiting Family Home" : std::move(label);

                if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(markerID)) {
                    if (auto* cell = ref->GetParentCell()) {
                        cand.targetCellId = State::CellId{ cell->GetFormID() };
                    }
                    if (cand.targetLocationId.IsNull()) {
                        if (auto* loc = ref->GetCurrentLocation()) {
                            cand.targetLocationId = State::LocationId{ loc->GetFormID() };
                        }
                    }
                }
                out.push_back(std::move(cand));
            };

            if (perceived->guardians.HasGuardian(State::ActorId::Player())) {
                for (const auto& houseRec : world.household->AllHouses()) {
                    if (houseRec.markerId.IsNull()) {
                        continue;
                    }
                    std::string houseName = HouseholdEngineOps::GetHouseName(houseRec.houseId.raw());
                    std::string label = houseName.empty()
                                            ? "Visiting Family Home"
                                            : ("Visiting " + std::move(houseName));
                    appendMarker(houseRec.markerId.raw(), houseRec.houseId.raw(), std::move(label));
                }
            }

            const auto visitableParents = view.GetVisitableFamilyParentIds(cid);
            for (const State::ActorId parentActor : visitableParents) {
                if (parentActor.IsNull() || parentActor.IsPlayer() || parentActor.IsUnknownFather()) {
                    continue;
                }
                const RE::FormID pid = parentActor.raw();

                const State::HouseId parentHouseId = world.household->GetTenantHouse(pid);
                if (!parentHouseId.IsNull()) {
                    if (const auto* houseRec = world.household->FindHouse(parentHouseId);
                        houseRec && !houseRec->markerId.IsNull()) {
                        std::string houseName = HouseholdEngineOps::GetHouseName(parentHouseId.raw());
                        std::string label = houseName.empty()
                                                ? "Visiting Parents' Home"
                                                : ("Visiting " + std::move(houseName));
                        appendMarker(houseRec->markerId.raw(), parentHouseId.raw(), std::move(label));
                        continue;
                    }
                }

                const std::string pName = ResolveActorDisplayName(pid, world, spatial);
                const std::string label = pName.empty()
                                              ? "Visiting Parents' Home"
                                              : ("Visiting " + pName);
                const RE::FormID origMarker =
                    SpatialWorldIndex::GetSingleton().GetNpcOriginalHouseCenterMarker(pid);
                if (origMarker != 0) {
                    appendMarker(origMarker, 0, label);
                    continue;
                }

                if (const auto* sample = spatial.Find(State::RefId{ pid });
                    sample && !sample->IsDead() && !sample->IsDeleted() && !sample->IsDisabled()) {
                    Domain::Household::TravelDestinationCandidate cand{};
                    cand.targetMarkerId = sample->refId;
                    cand.targetLocationId = sample->currentLocationId;
                    cand.targetCellId = sample->parentCellId;
                    cand.destinationName = label;
                    cand.isFamilyVisit = true;
                    if (seenMarkers.insert(cand.targetMarkerId.raw()).second) {
                        out.push_back(std::move(cand));
                    }
                }
            }

            return out;
        }

        void ReconcileTenantEngineAdded(RE::FormID tenantFormID, RE::FormID houseFormID, bool isChild) {
            if (Foundation::IsSyntheticChildRawId(tenantFormID)) {
                return;
            }
            if (!isChild) {
                HouseholdEngineOps::GetSingleton().UpdateSpouseHomeMarker(tenantFormID, houseFormID);
            }
            FactionReconciler::GetSingleton().ReconcileActor(tenantFormID);
            PackageReconciler::GetSingleton().ReconcileActor(tenantFormID);
        }

        void ReconcileTenantEngineRemoved(RE::FormID tenantFormID) {
            if (Foundation::IsSyntheticChildRawId(tenantFormID)) {
                return;
            }
            HouseholdEngineOps::GetSingleton().ClearHomeMarkerLinkedRef(tenantFormID);
            FactionReconciler::GetSingleton().ReconcileActor(tenantFormID);
            PackageReconciler::GetSingleton().ReconcileActor(tenantFormID);
        }

    }  // namespace

    HouseholdUseCases& HouseholdUseCases::GetSingleton() {
        static HouseholdUseCases instance;
        return instance;
    }

    // =========================================================================
    // Player Houses & Tenants (PlayerHouseService)
    // =========================================================================

    bool HouseholdUseCases::RegisterPlayerHouseCell(RE::FormID locationFormID, RE::FormID markerFormID) {
        if (locationFormID == 0 || markerFormID == 0) {
            MARAS_LOG_WARN(
                "HouseholdUseCases::RegisterPlayerHouseCell: invalid args location={:08X}, marker={:08X}",
                locationFormID, markerFormID);
            return false;
        }

        bool existedBefore = false;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            existedBefore = snap->household->FindHouse(State::HouseId{ locationFormID }) != nullptr;
        }

        bool registered = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            registered = Domain::Household::ApplyRegisterHouse(
                ws, State::HouseId{ locationFormID }, State::RefId{ markerFormID }, false);
        });

        if (registered) {
            if (!existedBefore) {
                GlobalVarReconciler::GetSingleton().ReconcileGlobals();
            }
            MARAS_LOG_INFO(
                "HouseholdUseCases::RegisterPlayerHouseCell: {} house {:08X} ('{}') with marker {:08X}",
                existedBefore ? "updated" : "registered",
                locationFormID,
                GetHouseName(locationFormID),
                markerFormID);
        }
        return registered;
    }

    std::vector<RE::FormID> HouseholdUseCases::GetAllPlayerHouses() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& houses = snap->household->AllHouses();
        std::vector<RE::FormID> out;
        out.reserve(houses.size());
        for (const auto& h : houses) {
            out.push_back(h.houseId.raw());
        }
        return out;
    }

    std::vector<std::string> HouseholdUseCases::GetAllPlayerHousesNames() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& houses = snap->household->AllHouses();
        std::vector<std::string> names;
        names.reserve(houses.size());
        for (const auto& h : houses) {
            std::string name = GetHouseName(h.houseId.raw());
            if (name.empty()) {
                name = std::format("{:08X}", h.houseId.raw());
            }
            names.push_back(std::move(name));
        }
        return names;
    }

    int HouseholdUseCases::CountPlayerHouses() const noexcept {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return static_cast<int>(snap->household->AllHouses().size());
    }

    RE::FormID HouseholdUseCases::GetHouseMarkerFormID(RE::FormID playerHouseFormID) const noexcept {
        return HouseholdEngineOps::GetSingleton().GetHouseMarkerFormID(playerHouseFormID);
    }

    RE::TESObjectREFR* HouseholdUseCases::GetHouseMarkerRef(RE::FormID playerHouseFormID) const {
        return HouseholdEngineOps::GetSingleton().GetHouseMarkerRef(playerHouseFormID);
    }

    std::string HouseholdUseCases::GetHouseName(RE::FormID houseFormID) {
        return HouseholdEngineOps::GetHouseName(houseFormID);
    }

    RE::FormID HouseholdUseCases::FindHouseByName(std::string_view name) const {
        return HouseholdEngineOps::GetSingleton().FindHouseByName(name);
    }

    bool HouseholdUseCases::RegisterTenantInPlayerHouse(
        RE::FormID tenantFormID,
        RE::FormID playerHouseFormID,
        bool stayAtHome) {
        if (tenantFormID == 0 || tenantFormID == Foundation::kPlayerRefRawId || playerHouseFormID == 0) {
            MARAS_LOG_WARN(
                "HouseholdUseCases::RegisterTenantInPlayerHouse: invalid args tenant={:08X}, house={:08X}",
                tenantFormID, playerHouseFormID);
            return false;
        }

        bool isChild = false;
        RE::FormID previousHouseID = 0;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            if (!snap->household->FindHouse(State::HouseId{ playerHouseFormID })) {
                MARAS_LOG_WARN(
                    "HouseholdUseCases::RegisterTenantInPlayerHouse: house {:08X} is not registered",
                    playerHouseFormID);
                return false;
            }
            isChild = IsChildTenantEntry(*snap, tenantFormID);
            if (!isChild && !ActorSvc::IsValid(tenantFormID)) {
                MARAS_LOG_WARN(
                    "HouseholdUseCases::RegisterTenantInPlayerHouse: invalid adult tenant {:08X}",
                    tenantFormID);
                return false;
            }
            previousHouseID = snap->household->GetTenantHouse(tenantFormID).raw();
        }

        bool assigned = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            assigned = Domain::Household::ApplyRegisterTenant(
                ws,
                tenantFormID,
                State::HouseId{ playerHouseFormID },
                stayAtHome,
                isChild);
        });

        if (!assigned) {
            return false;
        }

        if (isChild && previousHouseID != playerHouseFormID && !Foundation::IsSyntheticChildRawId(tenantFormID)) {
            PlayerChildrenEngineOps::GetSingleton().MoveChildAndMarkerToHouse(
                tenantFormID, playerHouseFormID, true);
        }
        ReconcileTenantEngineAdded(tenantFormID, playerHouseFormID, isChild);
        GlobalVarReconciler::GetSingleton().ReconcileGlobals();

        MARAS_LOG_INFO(
            "HouseholdUseCases::RegisterTenantInPlayerHouse: assigned tenant {:08X} (child={}) to house {:08X} (stayAtHome={})",
            tenantFormID, isChild, playerHouseFormID, stayAtHome);
        return true;
    }

    bool HouseholdUseCases::RemoveTenantFromPlayerHouse(RE::FormID tenantFormID) {
        if (tenantFormID == 0) {
            return false;
        }

        bool isChild = false;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            isChild = IsChildTenantEntry(*snap, tenantFormID);
        }

        State::HouseId prevHouse{};
        bool removed = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            removed = Domain::Household::ApplyRemoveTenant(ws, tenantFormID, &prevHouse);
            if (removed && !isChild && !Foundation::IsSyntheticChildRawId(tenantFormID)) {
                if (auto* prof = ws.actors.Write().FindMutable(State::RefId{ tenantFormID })) {
                    prof->legacyCurrentHome.reset();
                }
            }
        });

        if (!removed) {
            return false;
        }

        ReconcileTenantEngineRemoved(tenantFormID);
        GlobalVarReconciler::GetSingleton().ReconcileGlobals();

        MARAS_LOG_INFO(
            "HouseholdUseCases::RemoveTenantFromPlayerHouse: removed tenant {:08X} from house {:08X}",
            tenantFormID, prevHouse.raw());
        return true;
    }

    std::vector<RE::FormID> HouseholdUseCases::GetPlayerHouseTenants(RE::FormID playerHouseFormID) const {
        if (playerHouseFormID == 0) {
            return {};
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* h = snap->household->FindHouse(State::HouseId{ playerHouseFormID });
        if (!h) {
            return {};
        }
        return h->tenantOrder;
    }

    void HouseholdUseCases::SetTenantStayAtHome(RE::FormID tenantFormID, bool stayAtHome) {
        if (tenantFormID == 0) {
            return;
        }
        bool updated = false;
        State::HouseId houseId{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            updated = Domain::Household::ApplySetTenantStayAtHome(
                ws, tenantFormID, stayAtHome, &houseId);
        });
        if (updated && !Foundation::IsSyntheticChildRawId(tenantFormID)) {
            PackageReconciler::GetSingleton().ReconcileActor(tenantFormID);
        }
        MARAS_LOG_DEBUG(
            "HouseholdUseCases::SetTenantStayAtHome: tenant {:08X} stayAtHome={} (updated={})",
            tenantFormID, stayAtHome, updated);
    }

    bool HouseholdUseCases::GetTenantStayAtHome(RE::FormID tenantFormID) const {
        if (tenantFormID == 0) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->household->GetTenantStayAtHome(tenantFormID);
    }

    RE::FormID HouseholdUseCases::GetTenantHouseFormID(RE::FormID tenantFormID) const noexcept {
        if (tenantFormID == 0) {
            return 0;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->household->GetTenantHouse(tenantFormID).raw();
    }

    bool HouseholdUseCases::CanRemoveTenant(RE::FormID tenantFormID) const {
        if (tenantFormID == 0) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto elig = Domain::Household::CanRemoveTenant(*snap, tenantFormID);
        if (!elig.eligible) {
            return false;
        }

        if (snap->lineage) {
            if (const auto* childRec = snap->lineage->FindChild(State::ChildId{ tenantFormID });
                childRec && childRec->stage != Foundation::ChildStage::Adult) {
                for (const State::ActorId gid : { childRec->guardians.slotA, childRec->guardians.slotB }) {
                    if (!gid.IsNull() && snap->relationships &&
                        snap->relationships->GetStatus(gid, State::ActorId::Player()) ==
                            Foundation::RelationshipStatus::Married) {
                        return false;
                    }
                }
            }
        }

        if (HasLivingChildWithPlayer(*snap, State::ActorId{ tenantFormID })) {
            return false;
        }
        return true;
    }

    std::vector<RE::FormID> HouseholdUseCases::RelocateFamilyUnit(
        RE::FormID npcFormID,
        RE::FormID playerHouseFormID,
        bool moveParents) {
        std::vector<RE::FormID> relocatedMembers;
        if (npcFormID == 0 || playerHouseFormID == 0) {
            MARAS_LOG_WARN(
                "HouseholdUseCases::RelocateFamilyUnit: invalid args npc={:08X}, house={:08X}",
                npcFormID, playerHouseFormID);
            return relocatedMembers;
        }

        Domain::Household::RelocateFamilyOutcome outcome{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Household::ApplyRelocateFamilyUnit(
                ws, npcFormID, State::HouseId{ playerHouseFormID }, moveParents);
        });

        if (!outcome.targetHouseExists) {
            MARAS_LOG_WARN(
                "HouseholdUseCases::RelocateFamilyUnit: target house {:08X} is not registered",
                playerHouseFormID);
            return relocatedMembers;
        }
        if (outcome.ineligibleAdultAnchor) {
            MARAS_LOG_WARN(
                "HouseholdUseCases::RelocateFamilyUnit: adult {:08X} is ineligible for player family relocation",
                npcFormID);
            return relocatedMembers;
        }
        if (!outcome.skippedChildCustody.empty() &&
            outcome.movedAdults.empty() && outcome.movedChildren.empty()) {
            MARAS_LOG_WARN(
                "HouseholdUseCases::RelocateFamilyUnit: child {:08X} is not in player custody",
                npcFormID);
            return relocatedMembers;
        }

        std::unordered_set<std::uint32_t> changedChildSet;
        changedChildSet.reserve(outcome.changedHouseChildren.size());
        for (const State::ChildId cid : outcome.changedHouseChildren) {
            changedChildSet.insert(cid.raw());
        }

        for (const State::ActorId adultId : outcome.movedAdults) {
            ReconcileTenantEngineAdded(adultId.raw(), playerHouseFormID, false);
            relocatedMembers.push_back(adultId.raw());
        }

        for (const State::ChildId cid : outcome.movedChildren) {
            if (!cid.IsSynthetic()) {
                const bool shouldMove = changedChildSet.contains(cid.raw());
                PlayerChildrenEngineOps::GetSingleton().MoveChildAndMarkerToHouse(
                    cid.raw(), playerHouseFormID, shouldMove);
                ReconcileTenantEngineAdded(cid.raw(), playerHouseFormID, true);
            }
            relocatedMembers.push_back(cid.raw());
        }

        if (!relocatedMembers.empty()) {
            GlobalVarReconciler::GetSingleton().ReconcileGlobals();
            GetUiBridgeGateway().SendOverviewData();
            MARAS_LOG_INFO(
                "HouseholdUseCases::RelocateFamilyUnit: relocated {} adult(s) and {} child(ren) (anchor={:08X}) to house {:08X} (moveParents={})",
                outcome.movedAdults.size(),
                outcome.movedChildren.size(),
                npcFormID,
                playerHouseFormID,
                moveParents);
        }

        return relocatedMembers;
    }

    void HouseholdUseCases::SetHouseInitialMoveDone(RE::FormID houseFormID, bool done) {
        if (houseFormID == 0) {
            return;
        }
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* house = ws.household.Write().FindHouseMutable(State::HouseId{ houseFormID })) {
                house->initialMoveDone = done;
            }
        });
    }

    bool HouseholdUseCases::IsHouseInitialMoveDone(RE::FormID houseFormID) const {
        if (houseFormID == 0) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* house = snap->household->FindHouse(State::HouseId{ houseFormID });
        return house ? house->initialMoveDone : false;
    }

    // =========================================================================
    // Shared Spouse Assets (SpouseAssetsService)
    // =========================================================================

    bool HouseholdUseCases::ShareHouseWithPlayer(RE::FormID npcFormID) {
        return BedOwnershipReconciler::GetSingleton().ApplyShareHomeWithPlayer(npcFormID);
    }

    bool HouseholdUseCases::StopShareHouseWithPlayer(RE::FormID npcFormID, std::string_view reason) {
        return BedOwnershipReconciler::GetSingleton().StopShareHomeWithPlayer(npcFormID, reason);
    }

    bool HouseholdUseCases::IsHouseSharedWithPlayer(RE::FormID cellFormID) const {
        if (cellFormID == 0) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return Domain::Household::IsCellSharedWithPlayer(*snap, State::CellId{ cellFormID });
    }

    // =========================================================================
    // Adult Child Independent Travel (AdultChildTravelService)
    // =========================================================================

    void HouseholdUseCases::BuildDestinationIndex() {
        SpatialWorldIndex::GetSingleton().BuildInnDestinationIndex();
    }

    bool HouseholdUseCases::AssignOrUpdateDestination(RE::FormID childID, bool forceRotate) {
        if (childID == 0 || Foundation::IsSyntheticChildRawId(childID)) {
            return false;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!IsTravelEligibleAdultChild(*snap, childID)) {
            const bool hadTravel =
                snap->household &&
                snap->household->FindAdultChildTravel(State::ChildId{ childID }) != nullptr;
            if (hadTravel) {
                State::WorldStore::GetSingleton().Modify([childID](State::WorldState& ws) {
                    ws.household.Write().EraseAdultChildTravel(State::ChildId{ childID });
                });
                PackageReconciler::GetSingleton().ReconcileActor(childID);
            }
            return false;
        }

        auto* actor = SpatialWorldIndex::GetSingleton().GetActorFromFormId(childID);
        if (!actor) {
            actor = RE::TESForm::LookupByID<RE::Actor>(childID);
        }
        if (!actor || actor->IsDead() || actor->IsDeleted()) {
            return false;
        }

        const auto* existing = snap->household->FindAdultChildTravel(State::ChildId{ childID });
        if (existing && !forceRotate && !existing->targetMarkerId.IsNull()) {
            PackageReconciler::GetSingleton().ReconcileActor(childID);
            return true;
        }

        const State::RefId prevMarkerId = existing ? existing->targetMarkerId : State::RefId{};
        const auto spatial = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto familyDests = BuildFamilyVisitDestinations(*snap, *spatial, childID);
        const auto innDests = SpatialWorldIndex::GetSingleton().GetInnDestinations();

        LocalEngineRng rng;
        const auto chosen = Domain::Household::SelectNextTravelDestination(
            familyDests, innDests, prevMarkerId, rng);
        if (!chosen) {
            MARAS_LOG_WARN(
                "HouseholdUseCases::AssignOrUpdateDestination: no travel destinations found for adult child {:08X}",
                childID);
            return false;
        }

        auto* calendar = RE::Calendar::GetSingleton();
        const float currentDay = calendar ? calendar->GetDaysPassed() : 0.0f;

        State::AdultChildTravelRecord rec =
            Domain::Household::CreateTravelRecord(*chosen, currentDay, false, rng);
        if (HasActorReachedTravelDestination(actor, rec)) {
            rec.arrivedGameDay = currentDay;
        }

        bool upserted = false;
        bool alreadyAssigned = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            const auto* childRec = ws.lineage->FindChild(State::ChildId{ childID });
            if (!childRec || childRec->stage != Foundation::ChildStage::Adult ||
                !ws.household->GetTenantHouse(childID).IsNull()) {
                return;
            }
            const auto* cur = ws.household->FindAdultChildTravel(State::ChildId{ childID });
            if (cur && !forceRotate && !cur->targetMarkerId.IsNull()) {
                alreadyAssigned = true;
                return;
            }
            ws.household.Write().UpsertAdultChildTravel(State::ChildId{ childID }, rec);
            upserted = true;
        });

        if (!upserted) {
            if (alreadyAssigned) {
                PackageReconciler::GetSingleton().ReconcileActor(childID);
                return true;
            }
            return false;
        }

        // Ensure independent Stage 4 travelers clear stale player-household child custody flags/tenancy.
        LineageUseCases::GetSingleton().OnChildRemovedFromHousehold(childID);
        PackageReconciler::GetSingleton().ReconcileActor(childID);

        MARAS_LOG_INFO(
            "HouseholdUseCases::AssignOrUpdateDestination: adult child {:08X} -> '{}' (marker={:08X}, stayDays={:.2f}, familyVisit={})",
            childID,
            rec.destinationName,
            rec.targetMarkerId.raw(),
            rec.stayDurationDays,
            rec.isFamilyVisit);
        return true;
    }

    void HouseholdUseCases::UnregisterAdultChild(RE::FormID childID) {
        if (childID == 0) {
            return;
        }

        bool erased = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            erased = ws.household.Write().EraseAdultChildTravel(State::ChildId{ childID });
        });

        if (erased && !Foundation::IsSyntheticChildRawId(childID)) {
            PackageReconciler::GetSingleton().ReconcileActor(childID);
            MARAS_LOG_INFO(
                "HouseholdUseCases::UnregisterAdultChild: unregistered travel state for child {:08X}",
                childID);
        }
    }

    void HouseholdUseCases::TickAdultChildTravel(float currentGameDay) {
        const auto spatial = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        if (!spatial) {
            return;
        }

        Domain::Household::TravelTickOutcome outcome{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Household::EvaluateTravelTick(ws, currentGameDay, *spatial);
        });

        // Evaluate live actor/marker cell arrival outside WorldStore::Modify and locks
        // for static inn markers not indexed in SpatialActorSnapshot.
        std::vector<State::ChildId> fallbackArrivedIds;
        if (const auto snap = State::WorldStore::GetSingleton().Snapshot(); snap && snap->household) {
            for (const auto& [cid, rec] : snap->household->AllAdultChildTravel()) {
                if (rec.arrivedGameDay >= 0.0f) {
                    continue;
                }
                auto* actor = SpatialWorldIndex::GetSingleton().GetActorFromFormId(cid.raw());
                if (!actor) {
                    actor = RE::TESForm::LookupByID<RE::Actor>(cid.raw());
                }
                if (HasActorReachedTravelDestination(actor, rec)) {
                    fallbackArrivedIds.push_back(cid);
                }
            }
        }

        std::vector<RE::FormID> fallbackArrivedChildren;
        if (!fallbackArrivedIds.empty()) {
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                for (const State::ChildId cid : fallbackArrivedIds) {
                    const auto* existing = ws.household->FindAdultChildTravel(cid);
                    if (!existing || existing->arrivedGameDay >= 0.0f) {
                        continue;
                    }
                    State::AdultChildTravelRecord updated = *existing;
                    updated.arrivedGameDay = currentGameDay;
                    ws.household.Write().UpsertAdultChildTravel(cid, std::move(updated));
                    fallbackArrivedChildren.push_back(cid.raw());
                }
            });
        }

        for (const State::ChildId cid : outcome.unregisteredInvalidAdults) {
            if (!cid.IsSynthetic()) {
                PackageReconciler::GetSingleton().ReconcileActor(cid.raw());
            }
        }
        for (const State::ChildId cid : outcome.arrivedChildren) {
            MARAS_LOG_DEBUG(
                "HouseholdUseCases::TickAdultChildTravel: adult child {:08X} arrived at destination",
                cid.raw());
        }
        for (const RE::FormID childID : fallbackArrivedChildren) {
            MARAS_LOG_DEBUG(
                "HouseholdUseCases::TickAdultChildTravel: adult child {:08X} reached static marker destination",
                childID);
        }
        for (const State::ChildId cid : outcome.childrenNeedingRotation) {
            AssignOrUpdateDestination(cid.raw(), true);
        }
    }

    void HouseholdUseCases::ReassertAllTravelOnLoad() {
        std::vector<RE::FormID> invalidTravelers;
        std::vector<RE::FormID> activeTravelers;
        std::vector<RE::FormID> missingTravelers;

        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            if (!snap->household || !snap->lineage) {
                return;
            }
            for (const auto& [cid, tr] : snap->household->AllAdultChildTravel()) {
                (void)tr;
                if (!IsTravelEligibleAdultChild(*snap, cid.raw())) {
                    invalidTravelers.push_back(cid.raw());
                } else {
                    activeTravelers.push_back(cid.raw());
                }
            }
            for (const auto& [cid, childRec] : snap->lineage->AllChildren()) {
                (void)childRec;
                if (IsTravelEligibleAdultChild(*snap, cid.raw()) &&
                    !snap->household->FindAdultChildTravel(cid)) {
                    missingTravelers.push_back(cid.raw());
                }
            }
        }

        for (const RE::FormID cid : invalidTravelers) {
            UnregisterAdultChild(cid);
        }
        for (const RE::FormID cid : activeTravelers) {
            AssignOrUpdateDestination(cid, false);
        }
        for (const RE::FormID cid : missingTravelers) {
            AssignOrUpdateDestination(cid, false);
        }
    }

    RE::FormID HouseholdUseCases::GetCurrentTargetMarkerID(RE::FormID childID) const {
        if (childID == 0) {
            return 0;
        }
        std::shared_lock lock(travelMutex_);
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->household->FindAdultChildTravel(State::ChildId{ childID });
        return rec ? rec->targetMarkerId.raw() : 0;
    }

    std::string HouseholdUseCases::GetCurrentDestinationName(RE::FormID childID) const {
        if (childID == 0) {
            return {};
        }
        std::shared_lock lock(travelMutex_);
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->household->FindAdultChildTravel(State::ChildId{ childID });
        return rec ? rec->destinationName : std::string{};
    }

    // =========================================================================
    // Lifecycle & Serialization Hooks
    // =========================================================================

    void HouseholdUseCases::FinalizeLoad() {
        HouseholdEngineOps::GetSingleton().ReconcileTenantsOnLoad();
        BedOwnershipReconciler::GetSingleton().ReapplyAllSharedCellsOnLoad();
        ReassertAllTravelOnLoad();
    }

    void HouseholdUseCases::FinalizeCoSaveLoad(bool loadedAny) {
        (void)loadedAny;
        FinalizeLoad();
    }

    void HouseholdUseCases::PollTick(float currentGameDay) {
        TickAdultChildTravel(currentGameDay);
    }

    void HouseholdUseCases::ProcessDailyTick(float currentGameDay) {
        TickAdultChildTravel(currentGameDay);
    }

    void HouseholdUseCases::ProcessDayChange(float currentGameDay) {
        ProcessDailyTick(currentGameDay);
    }

    void HouseholdUseCases::Revert() {
        std::unique_lock lock(travelMutex_);
        State::WorldStore::GetSingleton().Modify([](State::WorldState& ws) {
            ws.household.Write().ClearAdultChildTravel();
        });
    }

    // =========================================================================
    // Read-Only Diagnostic Dumps (CQS)
    // =========================================================================

    nlohmann::json HouseholdUseCases::DumpHousesJson() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        nlohmann::json root = nlohmann::json::object();
        nlohmann::json housesArr = nlohmann::json::array();

        for (const auto& h : snap->household->AllHouses()) {
            nlohmann::json hj = nlohmann::json::object();
            hj["formID"] = h.houseId.raw();
            hj["formHex"] = std::format("{:08X}", h.houseId.raw());
            hj["name"] = GetHouseName(h.houseId.raw());
            hj["markerFormID"] = h.markerId.raw();
            hj["initialMoveDone"] = h.initialMoveDone;

            nlohmann::json tenantsArr = nlohmann::json::array();
            for (const RE::FormID tid : h.tenantOrder) {
                nlohmann::json tj = nlohmann::json::object();
                tj["formID"] = tid;
                tj["formHex"] = std::format("{:08X}", tid);
                tj["stayAtHome"] = snap->household->GetTenantStayAtHome(tid);
                tj["isChild"] = IsChildTenantEntry(*snap, tid);
                tenantsArr.push_back(std::move(tj));
            }
            hj["tenants"] = std::move(tenantsArr);
            housesArr.push_back(std::move(hj));
        }

        root["count"] = housesArr.size();
        root["houses"] = std::move(housesArr);
        return root;
    }

    nlohmann::json HouseholdUseCases::DumpSharedAssetsJson() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        nlohmann::json root = nlohmann::json::object();
        nlohmann::json cellsArr = nlohmann::json::array();

        for (const auto& [cellId, rec] : snap->household->AllSharedCells()) {
            nlohmann::json cj = nlohmann::json::object();
            cj["cellFormID"] = cellId.raw();
            cj["cellHex"] = std::format("{:08X}", cellId.raw());
            cj["cellName"] = BedOwnershipReconciler::GetCellName(cellId.raw());
            cj["sharedWithPlayer"] = rec.sharedWithPlayer;
            cj["originalPublicRecorded"] = rec.originalPublicRecorded;
            cj["originalPublicState"] = rec.originalPublicState;

            nlohmann::json spousesArr = nlohmann::json::array();
            for (const auto spouseId : rec.sharingSpouses) {
                spousesArr.push_back(spouseId.raw());
            }
            cj["sharingSpouses"] = std::move(spousesArr);
            cellsArr.push_back(std::move(cj));
        }

        nlohmann::json bedOwnersArr = nlohmann::json::array();
        std::vector<std::pair<RE::FormID, RE::FormID>> sortedBedOwners;
        sortedBedOwners.reserve(snap->household->AllOriginalBedOwners().size());
        for (const auto& [bedRef, origOwner] : snap->household->AllOriginalBedOwners()) {
            sortedBedOwners.emplace_back(bedRef.raw(), origOwner.raw());
        }
        std::sort(sortedBedOwners.begin(), sortedBedOwners.end());
        for (const auto& [bedRaw, ownerRaw] : sortedBedOwners) {
            nlohmann::json bj = nlohmann::json::object();
            bj["bedFormID"] = bedRaw;
            bj["bedHex"] = std::format("{:08X}", bedRaw);
            bj["originalOwnerFormID"] = ownerRaw;
            bj["originalOwnerHex"] = std::format("{:08X}", ownerRaw);
            bedOwnersArr.push_back(std::move(bj));
        }

        root["count"] = cellsArr.size();
        root["sharedCells"] = std::move(cellsArr);
        root["originalBedOwnerCount"] = sortedBedOwners.size();
        root["originalBedOwners"] = std::move(bedOwnersArr);
        return root;
    }

    nlohmann::json HouseholdUseCases::DumpTravelJson(const Projection::PerspectiveView& view) const {
        std::shared_lock lock(travelMutex_);
        const auto world = State::WorldStore::GetSingleton().Snapshot();
        const auto innDests = SpatialWorldIndex::GetSingleton().GetInnDestinations();

        nlohmann::json root = nlohmann::json::object();
        nlohmann::json entries = nlohmann::json::array();

        for (const auto& [cid, rec] : view.Household().AllAdultChildTravel()) {
            const auto perceived = view.GetPerceivedChild(cid);
            std::string displayName = perceived ? perceived->name : std::string{};
            if (displayName.empty()) {
                displayName = ResolveActorDisplayName(cid.raw(), *world, view.Spatial());
            }

            nlohmann::json ej = nlohmann::json::object();
            ej["childID"] = cid.raw();
            ej["childHex"] = std::format("{:08X}", cid.raw());
            ej["name"] = std::move(displayName);
            ej["targetMarkerID"] = rec.targetMarkerId.raw();
            ej["targetLocationID"] = rec.targetLocationId.raw();
            ej["targetCellID"] = rec.targetCellId.raw();
            ej["destinationName"] = rec.destinationName;
            ej["assignedGameDay"] = rec.assignedGameDay;
            ej["arrivedGameDay"] = rec.arrivedGameDay;
            ej["stayDurationDays"] = rec.stayDurationDays;
            ej["isFamilyVisit"] = rec.isFamilyVisit;
            entries.push_back(std::move(ej));
        }

        root["count"] = entries.size();
        root["innDestinationCount"] = innDests.size();
        root["travelers"] = std::move(entries);
        return root;
    }

    nlohmann::json HouseholdUseCases::DumpTravelJson() const {
        const auto world = State::WorldStore::GetSingleton().Snapshot();
        const auto spatial = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto view = Projection::PerspectiveView::ForDashboard(world, spatial);
        return DumpTravelJson(view);
    }

}  // namespace MARAS::App
