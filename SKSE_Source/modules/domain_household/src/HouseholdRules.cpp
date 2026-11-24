#include "maras/domain/household/HouseholdRules.h"

#include "maras/domain/household/DeceasedQueries.h"
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

        [[nodiscard]] bool HasLivingChildWithPlayer(
            const State::WorldStateSnapshot& snap,
            ActorId actorId) noexcept {
            if (!snap.lineage || actorId.IsNull() || actorId.IsPlayer()) {
                return false;
            }
            for (const auto& [cid, rec] : snap.lineage->AllChildren()) {
                (void)cid;
                if (rec.stage == Foundation::ChildStage::Adult ||
                    rec.stage == Foundation::ChildStage::Training ||
                    rec.stage == Foundation::ChildStage::Unborn) {
                    continue;
                }
                if (IsDeceased(snap, rec.childId.AsActorId())) {
                    continue;
                }
                if (rec.guardians.HasGuardian(ActorId::Player()) &&
                    rec.guardians.HasGuardian(actorId)) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool IsEligibleRelocationAdult(
            const State::WorldStateSnapshot& snap,
            ActorId adultId) noexcept {
            if (adultId.IsNull() || adultId.IsPlayer() || adultId.IsUnknownFather()) {
                return false;
            }
            if (IsDeceased(snap, adultId)) {
                return false;
            }
            if (snap.relationships &&
                snap.relationships->GetStatus(adultId, ActorId::Player()) ==
                    Foundation::RelationshipStatus::Married) {
                return true;
            }
            return HasLivingChildWithPlayer(snap, adultId);
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
        auto& hh = ws.household.Write();
        if (auto* existing = hh.FindHouseMutable(houseId)) {
            if (!markerId.IsNull()) {
                existing->markerId = markerId;
            }
            return true;
        }
        return hh.RegisterHouse(houseId, markerId, initialMoveDone);
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
                bool hasMarriedCoGuardian = false;
                if (childRec->guardians.HasGuardian(ActorId::Player())) {
                    const ActorId otherGuardian = childRec->guardians.OtherGuardian(ActorId::Player());
                    if (!otherGuardian.IsNull() &&
                        !IsDeceased(snap, otherGuardian) &&
                        snap.relationships &&
                        snap.relationships->GetStatus(ActorId::Player(), otherGuardian) ==
                            Foundation::RelationshipStatus::Married) {
                        hasMarriedCoGuardian = true;
                    }
                }
                out.eligible = !hasMarriedCoGuardian;
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
        bool moveParents) {
        RelocateFamilyOutcome out{};
        out.targetHouseId = targetHouseId;
        if (anchorRawId == 0 || targetHouseId.IsNull() || !ws.household.Read().FindHouse(targetHouseId)) {
            return out;
        }
        out.targetHouseExists = true;

        const auto snap = ws.Snapshot();

        ActorId coParent{};
        bool groupBySolePlayer = false;
        std::vector<ActorId> candidateAdults;
        std::vector<ChildId> candidateChildren;

        const auto* anchorChild = ws.lineage.Read().FindChild(ChildId{ anchorRawId });
        const bool isAnchorChild =
            (anchorChild != nullptr && anchorChild->stage != Foundation::ChildStage::Adult);
        if (isAnchorChild) {
            if (!anchorChild->guardians.HasGuardian(ActorId::Player())) {
                out.skippedChildCustody.push_back(anchorRawId);
                return out;
            }
            coParent = anchorChild->guardians.OtherGuardian(ActorId::Player());
            groupBySolePlayer = coParent.IsNull();
            if (moveParents && IsEligibleRelocationAdult(snap, coParent)) {
                candidateAdults.push_back(coParent);
            }
        } else {
            const ActorId anchorActor{ anchorRawId };
            if (!IsEligibleRelocationAdult(snap, anchorActor)) {
                out.ineligibleAdultAnchor = true;
                return out;
            }
            coParent = anchorActor;
            if (moveParents) {
                candidateAdults.push_back(anchorActor);
            }
        }

        const bool coParentValid = !coParent.IsNull() && !coParent.IsPlayer() && !coParent.IsUnknownFather();
        for (const auto& [cid, rec] : ws.lineage.Read().AllChildren()) {
            if (rec.stage == Foundation::ChildStage::Adult ||
                rec.stage == Foundation::ChildStage::Unborn) {
                continue;
            }
            if (!rec.guardians.HasGuardian(ActorId::Player())) {
                continue;
            }
            if (IsDeceased(snap, cid.AsActorId())) {
                continue;
            }
            if (groupBySolePlayer) {
                if (rec.guardians.IsSoleGuardian(ActorId::Player()) || cid.raw() == anchorRawId) {
                    candidateChildren.push_back(cid);
                }
            } else if (coParentValid) {
                if (rec.guardians.HasSameSet(ActorId::Player(), coParent) ||
                    rec.childId.raw() == anchorRawId) {
                    candidateChildren.push_back(cid);
                }
            } else if (cid.raw() == anchorRawId) {
                candidateChildren.push_back(cid);
            }
        }

        std::sort(candidateChildren.begin(), candidateChildren.end(), [](ChildId a, ChildId b) {
            return a.raw() < b.raw();
        });

        struct PlannedAdultMove {
            ActorId actorId;
            bool stayAtHome;
        };
        std::vector<PlannedAdultMove> plannedAdults;
        plannedAdults.reserve(candidateAdults.size());
        for (ActorId adult : candidateAdults) {
            const bool hadTenant = !ws.household.Read().GetTenantHouse(adult.raw()).IsNull();
            // Unhoused co-parents pulled in via a child anchor default to their original schedule
            // (stayAtHome = false); unhoused adults relocated directly default to stayAtHome = true.
            const bool adultStay = hadTenant
                ? ws.household.Read().GetTenantStayAtHome(adult.raw())
                : !isAnchorChild;
            plannedAdults.push_back(PlannedAdultMove{ adult, adultStay });
        }

        for (const auto& plan : plannedAdults) {
            if (ws.household.Write().AssignTenant(
                    plan.actorId.raw(), targetHouseId, plan.stayAtHome, /*isChildTenant=*/false)) {
                out.movedAdults.push_back(plan.actorId);
                MarkUniqueActor(out.dirtyActors, plan.actorId);
            }
        }

        for (ChildId cid : candidateChildren) {
            const auto* rec = ws.lineage.Read().FindChild(cid);
            if (!rec || !rec->guardians.HasGuardian(ActorId::Player())) {
                out.skippedChildCustody.push_back(cid.raw());
                continue;
            }
            const HouseId prevHouse = ws.household.Read().GetTenantHouse(cid.raw());
            const auto* prevChildState = ws.household.Read().FindChildState(cid);
            const bool hadMarker =
                (prevChildState != nullptr && !prevChildState->homeMarkerId.IsNull());
            const bool wasUnhoused =
                prevHouse.IsNull() &&
                (prevChildState == nullptr || prevChildState->currentHouseId.IsNull());

            if (ws.household.Write().AssignTenant(cid.raw(), targetHouseId, /*stayAtHome=*/true, /*isChildTenant=*/true)) {
                out.movedChildren.push_back(cid);
                if (wasUnhoused) {
                    if (auto* st = ws.household.Write().FindChildStateMutable(cid)) {
                        st->isNewlyAdopted = true;
                    }
                    out.newlyHousedChildren.push_back(cid);
                }
                if (prevHouse != targetHouseId || !hadMarker) {
                    out.changedHouseChildren.push_back(cid);
                }
                if (!cid.IsSynthetic()) {
                    MarkUniqueActor(out.dirtyActors, ActorId{ cid.raw() });
                }
            }
        }

        return out;
    }

    bool HasExpiredChildOrders(
        const State::HouseholdTable& hh,
        float currentGameDay) noexcept {
        if (currentGameDay < 0.0f) {
            return false;
        }
        for (const auto& [cid, st] : hh.AllChildStates()) {
            (void)cid;
            if (st.orderExpiryGameDay >= 0.0f && currentGameDay >= st.orderExpiryGameDay) {
                return true;
            }
        }
        return false;
    }

    std::vector<ChildId> ApplyExpireChildOrders(
        State::WorldState& ws,
        float currentGameDay) {
        std::vector<ChildId> expiredIds;
        if (currentGameDay < 0.0f) {
            return expiredIds;
        }

        const auto& hhRead = ws.household.Read();
        for (const auto& [cid, st] : hhRead.AllChildStates()) {
            if (st.orderExpiryGameDay >= 0.0f && currentGameDay >= st.orderExpiryGameDay) {
                expiredIds.push_back(cid);
            }
        }
        if (expiredIds.empty()) {
            return expiredIds;
        }

        std::sort(expiredIds.begin(), expiredIds.end(), [](ChildId a, ChildId b) {
            return a.raw() < b.raw();
        });

        auto& hhWrite = ws.household.Write();
        for (const ChildId cid : expiredIds) {
            if (auto* st = hhWrite.FindChildStateMutable(cid)) {
                st->currentOrder = 0.0f;
                st->orderExpiryGameDay = -1.0f;
            }
        }
        return expiredIds;
    }

}  // namespace MARAS::Domain::Household
