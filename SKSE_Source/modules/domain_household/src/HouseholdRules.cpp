#include "maras/domain/household/HouseholdRules.h"

#include "maras/foundation/Logging.h"

#include <algorithm>
#include <unordered_set>

namespace MARAS::Domain::Household {

    namespace {
        void MarkUniqueActor(std::vector<ActorId>& list, ActorId actor) {
            if (!actor.IsNull() && !actor.IsPlayer() && !actor.IsUnknownFather() &&
                std::find(list.begin(), list.end(), actor) == list.end()) {
                list.push_back(actor);
            }
        }
    }  // namespace

    bool ApplyRegisterHouse(
        State::WorldState& ws,
        HouseId houseId,
        RefId markerId,
        bool initialMoveDone) {
        if (houseId.IsNull()) {
            return false;
        }
        return ws.household.Write().RegisterHouse(houseId, markerId, initialMoveDone);
    }

    bool ApplyRegisterTenant(
        State::WorldState& ws,
        std::uint32_t tenantRawId,
        HouseId houseId,
        bool stayAtHome,
        bool isChildTenant) {
        if (tenantRawId == 0 || houseId.IsNull()) {
            return false;
        }
        return ws.household.Write().AssignTenant(tenantRawId, houseId, stayAtHome, isChildTenant);
    }

    bool ApplySetTenantStayAtHome(
        State::WorldState& ws,
        std::uint32_t tenantRawId,
        bool stayAtHome,
        HouseId* outHouseId) {
        if (tenantRawId == 0) {
            return false;
        }
        const HouseId houseId = ws.household.Read().GetTenantHouse(tenantRawId);
        if (houseId.IsNull()) {
            return false;
        }
        ws.household.Write().SetTenantStayAtHome(tenantRawId, stayAtHome);
        if (outHouseId != nullptr) {
            *outHouseId = houseId;
        }
        return true;
    }

    TenantRemovalEligibility CanRemoveTenant(
        const State::WorldStateSnapshot& snap,
        std::uint32_t tenantRawId) noexcept {
        TenantRemovalEligibility out{};
        if (tenantRawId == 0 || !snap.household) {
            return out;
        }
        const HouseId houseId = snap.household->GetTenantHouse(tenantRawId);
        if (houseId.IsNull()) {
            return out;
        }
        out.currentHouseId = houseId;
        if (snap.lineage) {
            if (const auto* childRec = snap.lineage->FindChild(ChildId{ tenantRawId });
                childRec && childRec->stage != Foundation::ChildStage::Adult) {
                out.isChild = true;
                out.eligible = !childRec->guardians.HasGuardian(ActorId::Player());
                return out;
            }
        }
        out.isChild = false;
        out.eligible = true;
        return out;
    }

    bool ApplyRemoveTenant(
        State::WorldState& ws,
        std::uint32_t tenantRawId,
        HouseId* outPreviousHouseId) {
        if (tenantRawId == 0) {
            return false;
        }
        const HouseId prevHouse = ws.household.Read().GetTenantHouse(tenantRawId);
        if (outPreviousHouseId != nullptr) {
            *outPreviousHouseId = prevHouse;
        }
        return ws.household.Write().EvictTenant(tenantRawId);
    }

    RelocateFamilyOutcome ApplyRelocateFamilyUnit(
        State::WorldState& ws,
        std::uint32_t anchorRawId,
        HouseId targetHouseId,
        bool stayAtHome) {
        RelocateFamilyOutcome out{};
        out.targetHouseId = targetHouseId;
        if (anchorRawId == 0 || targetHouseId.IsNull() || !ws.household.Read().FindHouse(targetHouseId)) {
            return out;
        }
        out.targetHouseExists = true;

        ActorId coParent{};
        bool groupBySolePlayer = false;
        std::vector<ActorId> candidateAdults;
        std::vector<ChildId> candidateChildren;

        const auto* anchorChild = ws.lineage.Read().FindChild(ChildId{ anchorRawId });
        if (anchorChild && anchorChild->stage != Foundation::ChildStage::Adult) {
            if (!anchorChild->guardians.HasGuardian(ActorId::Player())) {
                out.skippedChildCustody.push_back(anchorRawId);
                return out;
            }
            coParent = anchorChild->guardians.OtherGuardian(ActorId::Player());
            groupBySolePlayer = coParent.IsNull();
            if (!coParent.IsNull()) {
                candidateAdults.push_back(coParent);
            }
        } else {
            const ActorId anchorActor{ anchorRawId };
            candidateAdults.push_back(anchorActor);
            coParent = anchorActor;
        }

        const bool coParentValid = !coParent.IsNull() && !coParent.IsPlayer();
        for (const auto& [cid, rec] : ws.lineage.Read().AllChildren()) {
            if (rec.stage == Foundation::ChildStage::Adult || rec.stage == Foundation::ChildStage::Unborn) {
                continue;
            }
            if (!rec.guardians.HasGuardian(ActorId::Player())) {
                continue;
            }
            if (groupBySolePlayer) {
                if (rec.guardians.IsSoleGuardian(ActorId::Player())) {
                    candidateChildren.push_back(cid);
                }
            } else if (coParentValid) {
                if (rec.guardians.HasSameSet(ActorId::Player(), coParent)) {
                    candidateChildren.push_back(cid);
                } else if (rec.childId.raw() == anchorRawId) {
                    candidateChildren.push_back(cid);
                }
            }
        }

        for (ActorId adult : candidateAdults) {
            if (ws.household.Write().AssignTenant(adult.raw(), targetHouseId, stayAtHome, /*isChildTenant=*/false)) {
                out.movedAdults.push_back(adult);
                MarkUniqueActor(out.dirtyActors, adult);
            }
        }

        for (ChildId cid : candidateChildren) {
            const auto* rec = ws.lineage.Read().FindChild(cid);
            if (!rec || !rec->guardians.HasGuardian(ActorId::Player())) {
                out.skippedChildCustody.push_back(cid.raw());
                continue;
            }
            if (ws.household.Write().AssignTenant(cid.raw(), targetHouseId, /*stayAtHome=*/true, /*isChildTenant=*/true)) {
                out.movedChildren.push_back(cid);
                if (!cid.IsSynthetic()) {
                    MarkUniqueActor(out.dirtyActors, ActorId{ cid.raw() });
                }
            }
        }

        return out;
    }

}  // namespace MARAS::Domain::Household
