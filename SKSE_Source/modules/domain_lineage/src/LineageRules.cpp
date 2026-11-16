#include "maras/domain/lineage/LineageRules.h"

#include "maras/foundation/Logging.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace MARAS::Domain::Lineage {

    namespace {
        [[nodiscard]] bool IsGiftOrderValue(float val) noexcept {
            return std::abs(val - 1.1f) < 0.01f || std::abs(val - 1.2f) < 0.01f;
        }
    }  // namespace

    bool IsChildBlockedFromAdoption(
        RefId childRefId,
        BaseId childBaseId,
        bool db01ActiveUncompleted,
        std::string* outReason) {
        const bool isAventus =
            (childRefId.raw() == kAventusAretinoRefRawId) ||
            (childBaseId.raw() == kAventusAretinoBaseRawId);
        if (isAventus && db01ActiveUncompleted) {
            if (outReason != nullptr) {
                *outReason = "Performing the Black Sacrament (Innocence Lost in progress)";
            }
            return true;
        }
        return false;
    }

    void EvaluateChildBeastbloodInheritance(
        State::ChildRecord& record,
        bool motherIsWerewolf,
        bool fatherIsWerewolf) noexcept {
        if (record.beastbloodEvaluated) {
            return;
        }

        const int wwParentCount = (motherIsWerewolf ? 1 : 0) + (fatherIsWerewolf ? 1 : 0);
        if (wwParentCount >= 2) {
            record.hasBeastblood = true;
            record.beastbloodEvaluated = true;
            Foundation::LogInfo(
                "[Beastblood] Child {:08X} ('{}') inherited Beastblood (both bio parents are werewolves)",
                record.childId.raw(), record.name);
        } else if (wwParentCount == 1) {
            std::uint64_t seed =
                (static_cast<std::uint64_t>(record.childId.raw()) * 0x9E3779B97F4A7C15ULL) ^
                (static_cast<std::uint64_t>(record.bioMother.raw()) * 0xBF58476D1CE4E5B9ULL) ^
                (static_cast<std::uint64_t>(record.bioFather.raw()) * 0x94D049BB133111EBULL);
            seed ^= (seed >> 30);
            seed *= 0xBF58476D1CE4E5B9ULL;
            seed ^= (seed >> 27);
            seed *= 0x94D049BB133111EBULL;
            seed ^= (seed >> 31);
            record.hasBeastblood = ((seed & 1ULL) != 0);
            record.beastbloodEvaluated = true;
            Foundation::LogInfo(
                "[Beastblood] Child {:08X} ('{}') 50% Beastblood inheritance roll -> {}",
                record.childId.raw(), record.name, record.hasBeastblood ? "INHERITED" : "MORTAL");
        } else {
            record.hasBeastblood = false;
            if (!record.bioMother.IsNull() ||
                (!record.bioFather.IsNull() && !record.bioFather.IsUnknownFather()) ||
                record.isAdopted) {
                record.beastbloodEvaluated = true;
            }
        }
    }

    bool IsCompanionRetryCooldownElapsed(
        float storedTimestamp,
        float currentGameDays,
        float cooldownDays) noexcept {
        if (storedTimestamp <= 0.0f) {
            return true;
        }
        return (currentGameDays - storedTimestamp) >= cooldownDays;
    }

    bool IsPetBannedForChild(
        const State::WorldStateSnapshot& snap,
        ChildId childId,
        float currentGameDays) noexcept {
        if (!snap.settings || !snap.household) {
            return false;
        }
        if (snap.settings->bGlobalBanPet) {
            return true;
        }
        if (const auto* st = snap.household->FindChildState(childId)) {
            if (st->petBanned) {
                return true;
            }
            if (!IsCompanionRetryCooldownElapsed(st->petRetryTimestamp, currentGameDays)) {
                return true;
            }
        }
        return false;
    }

    bool IsCritterBannedForChild(
        const State::WorldStateSnapshot& snap,
        ChildId childId,
        float currentGameDays) noexcept {
        if (!snap.settings || !snap.household) {
            return false;
        }
        if (snap.settings->bGlobalBanCritter) {
            return true;
        }
        if (const auto* st = snap.household->FindChildState(childId)) {
            if (st->critterBanned) {
                return true;
            }
            if (!IsCompanionRetryCooldownElapsed(st->critterRetryTimestamp, currentGameDays)) {
                return true;
            }
        }
        return false;
    }

    float CalculateChildInteractionFamilyAffection(Temperament temperament) noexcept {
        if (temperament == Temperament::Romantic || temperament == Temperament::Humble) {
            return 3.0f;
        }
        if (temperament == Temperament::Independent) {
            return 1.0f;
        }
        return 2.0f;
    }

    ChildGuardianMutationOutcome ApplyRegisterChild(
        State::WorldState& ws,
        const ChildRegistrationInput& input,
        const State::IActorMetadataProvider& meta) {
        ChildGuardianMutationOutcome out{};
        if (input.childId.IsNull()) {
            return out;
        }

        State::ChildRecord rec{};
        if (const auto* existing = ws.lineage.Read().FindChild(input.childId)) {
            rec = *existing;
        }
        rec.childId = input.childId;
        if (!input.name.empty()) {
            rec.name = input.name;
        }
        if (input.timestamp != 0 || rec.timestamp == 0) {
            rec.timestamp = input.timestamp;
        }
        rec.isAdopted = input.isAdopted;
        if (!input.bioMother.IsNull()) {
            rec.bioMother = input.bioMother;
            rec.isBiologicalChild = true;
        }
        if (!input.bioFather.IsNull()) {
            rec.bioFather = input.bioFather;
            rec.isBiologicalChild = true;
        }

        const auto ensureRegisteredProfile = [&](ActorId id) {
            if (id.IsNull() || id.IsUnknownFather() || id.IsPlayer() || id.IsSyntheticChild()) {
                return;
            }
            if (!ws.actors.Read().Find(id.AsRefId())) {
                State::ActorProfileRecord prof{};
                prof.refId = id.AsRefId();
                prof.baseId = meta.GetActorBaseId(id);
                prof.isRegisteredAdult = true;
                ws.actors.Write().Upsert(std::move(prof));
            }
        };

        out.guardianDiff = rec.guardians.Assign(input.guardianA, input.guardianB);
        out.changed = true;

        ensureRegisteredProfile(rec.guardians.slotA);
        ensureRegisteredProfile(rec.guardians.slotB);
        ensureRegisteredProfile(rec.bioMother);
        ensureRegisteredProfile(rec.bioFather);

        EvaluateChildBeastbloodInheritance(rec, input.motherIsWerewolf, input.fatherIsWerewolf);
        ws.lineage.Write().UpsertChild(std::move(rec));
        (void)ws.SyncChildHouseholdState(input.childId);

        if (!out.guardianDiff.removedA.IsNull()) out.affectedActors.push_back(out.guardianDiff.removedA);
        if (!out.guardianDiff.removedB.IsNull()) out.affectedActors.push_back(out.guardianDiff.removedB);
        if (!out.guardianDiff.addedA.IsNull()) out.affectedActors.push_back(out.guardianDiff.addedA);
        if (!out.guardianDiff.addedB.IsNull()) out.affectedActors.push_back(out.guardianDiff.addedB);

        return out;
    }

    ChildGuardianMutationOutcome ApplyRemoveChild(
        State::WorldState& ws,
        ChildId childId) {
        ChildGuardianMutationOutcome out{};
        if (childId.IsNull()) {
            return out;
        }

        const auto* existing = ws.lineage.Read().FindChild(childId);
        if (!existing) {
            return out;
        }

        for (ActorId g : existing->guardians.AsArray()) {
            if (!g.IsNull()) {
                out.affectedActors.push_back(g);
            }
        }
        if (!existing->bioMother.IsNull() &&
            std::find(out.affectedActors.begin(), out.affectedActors.end(), existing->bioMother) == out.affectedActors.end()) {
            out.affectedActors.push_back(existing->bioMother);
        }
        if (!existing->bioFather.IsNull() && !existing->bioFather.IsUnknownFather() &&
            std::find(out.affectedActors.begin(), out.affectedActors.end(), existing->bioFather) == out.affectedActors.end()) {
            out.affectedActors.push_back(existing->bioFather);
        }

        out.changed = ws.lineage.Write().EraseChild(childId);
        if (out.changed) {
            ws.household.Write().EvictTenant(childId.raw());
            ws.household.Write().EraseChildState(childId);
            ws.household.Write().EraseChildFactionSnapshot(childId);
            ws.household.Write().EraseAdultChildTravel(childId);
        }
        return out;
    }

    ChildGuardianMutationOutcome ApplySetChildGuardian(
        State::WorldState& ws,
        ChildId childId,
        ActorId guardianId,
        bool isAdd,
        const State::IActorMetadataProvider& meta) {
        ChildGuardianMutationOutcome out{};
        if (childId.IsNull() || guardianId.IsNull() || guardianId.IsUnknownFather()) {
            return out;
        }

        auto* rec = ws.lineage.Write().FindChildMutable(childId);
        if (!rec) {
            return out;
        }

        if (isAdd) {
            if (rec->guardians.HasGuardian(guardianId)) {
                return out;
            }
            if (rec->guardians.slotA.IsNull()) {
                rec->guardians.slotA = guardianId;
                out.guardianDiff.addedA = guardianId;
                out.guardianDiff.changed = true;
            } else if (rec->guardians.slotB.IsNull()) {
                rec->guardians.slotB = guardianId;
                out.guardianDiff.addedB = guardianId;
                out.guardianDiff.changed = true;
            } else {
                const ActorId keepAnchor = rec->guardians.HasGuardian(ActorId::Player())
                    ? ActorId::Player()
                    : rec->guardians.slotA;
                out.guardianDiff = rec->guardians.Assign(keepAnchor, guardianId);
            }
            if (rec->guardians.IsSoleGuardian(ActorId::Player()) &&
                rec->stage != Foundation::ChildStage::Unborn &&
                rec->stage != Foundation::ChildStage::Adult) {
                const ActorId leadSpouse = ws.GetHighestRankingSurvivingPlayerSpouse();
                if (!leadSpouse.IsNull() && leadSpouse != guardianId) {
                    const State::GuardianSetDiff extra = rec->guardians.Assign(ActorId::Player(), leadSpouse);
                    if (extra.changed) {
                        out.guardianDiff.changed = true;
                        out.affectedActors.push_back(leadSpouse);
                    }
                }
            }
            if (!guardianId.IsPlayer() && !ws.actors.Read().Find(guardianId.AsRefId())) {
                State::ActorProfileRecord prof{};
                prof.refId = guardianId.AsRefId();
                prof.baseId = meta.GetActorBaseId(guardianId);
                prof.isRegisteredAdult = true;
                ws.actors.Write().Upsert(std::move(prof));
            }
        } else {
            if (!guardianId.IsPlayer() &&
                rec->guardians.HasGuardian(guardianId) &&
                rec->guardians.OtherGuardian(guardianId).IsPlayer() &&
                rec->stage != Foundation::ChildStage::Unborn &&
                rec->stage != Foundation::ChildStage::Adult) {
                const ActorId fallbackSpouse = ws.GetHighestRankingSurvivingPlayerSpouse(guardianId);
                if (!fallbackSpouse.IsNull()) {
                    out.guardianDiff = rec->guardians.Assign(ActorId::Player(), fallbackSpouse);
                } else if (ws.IsMarried(ActorId::Player(), guardianId) &&
                           !ws.deceased.Read().IsDeceased(guardianId)) {
                    return out;
                } else {
                    out.guardianDiff = rec->guardians.Remove(guardianId);
                }
            } else {
                out.guardianDiff = rec->guardians.Remove(guardianId);
            }
        }

        if (!out.guardianDiff.changed) {
            return out;
        }
        out.changed = true;

        (void)ws.SyncChildHouseholdState(childId);
        if (!out.guardianDiff.removedA.IsNull()) out.affectedActors.push_back(out.guardianDiff.removedA);
        if (!out.guardianDiff.removedB.IsNull()) out.affectedActors.push_back(out.guardianDiff.removedB);
        if (!out.guardianDiff.addedA.IsNull()) out.affectedActors.push_back(out.guardianDiff.addedA);
        if (!out.guardianDiff.addedB.IsNull()) out.affectedActors.push_back(out.guardianDiff.addedB);
        return out;
    }

    ChildGuardianMutationOutcome ApplySetChildGuardiansPair(
        State::WorldState& ws,
        ChildId childId,
        ActorId guardianA,
        ActorId guardianB,
        const State::IActorMetadataProvider& meta) {
        ChildGuardianMutationOutcome out{};
        if (childId.IsNull()) {
            return out;
        }

        auto* rec = ws.lineage.Write().FindChildMutable(childId);
        if (!rec) {
            return out;
        }

        if (guardianA.IsUnknownFather()) {
            guardianA = ActorId{};
        }
        if (guardianB.IsUnknownFather()) {
            guardianB = ActorId{};
        }
        if (!guardianA.IsNull() && guardianA == guardianB) {
            guardianB = ActorId{};
        }

        if (rec->stage != Foundation::ChildStage::Unborn &&
            rec->stage != Foundation::ChildStage::Adult &&
            ((guardianA.IsPlayer() && guardianB.IsNull()) ||
             (guardianB.IsPlayer() && guardianA.IsNull()))) {
            const ActorId leadSpouse = ws.GetHighestRankingSurvivingPlayerSpouse();
            if (!leadSpouse.IsNull()) {
                if (guardianA.IsPlayer()) {
                    guardianB = leadSpouse;
                } else {
                    guardianA = leadSpouse;
                }
            }
        }

        out.guardianDiff = rec->guardians.Assign(guardianA, guardianB);
        if (!out.guardianDiff.changed) {
            return out;
        }
        out.changed = true;

        for (const ActorId gid : { rec->guardians.slotA, rec->guardians.slotB }) {
            if (!gid.IsNull() && !gid.IsPlayer() && !ws.actors.Read().Find(gid.AsRefId())) {
                State::ActorProfileRecord prof{};
                prof.refId = gid.AsRefId();
                prof.baseId = meta.GetActorBaseId(gid);
                prof.isRegisteredAdult = true;
                ws.actors.Write().Upsert(std::move(prof));
            }
        }

        (void)ws.SyncChildHouseholdState(childId);
        if (!out.guardianDiff.removedA.IsNull()) out.affectedActors.push_back(out.guardianDiff.removedA);
        if (!out.guardianDiff.removedB.IsNull()) out.affectedActors.push_back(out.guardianDiff.removedB);
        if (!out.guardianDiff.addedA.IsNull()) out.affectedActors.push_back(out.guardianDiff.addedA);
        if (!out.guardianDiff.addedB.IsNull()) out.affectedActors.push_back(out.guardianDiff.addedB);
        return out;
    }

    ChildOrderOutcome ApplyOrderIssue(
        State::WorldState& ws,
        ChildId childId,
        float orderValue,
        float durationGameDays,
        float currentGameDays) {
        ChildOrderOutcome out{};
        if (childId.IsNull()) {
            return out;
        }

        auto& hh = ws.household.Write();
        auto* targetState = hh.FindChildStateMutable(childId);
        if (!targetState) {
            return out;
        }

        const HouseId houseId = !targetState->currentHouseId.IsNull()
                                    ? targetState->currentHouseId
                                    : hh.GetTenantHouse(childId.raw());
        const float expiry = (durationGameDays > 0.0f) ? (currentGameDays + durationGameDays) : -1.0f;

        if (IsGiftOrderValue(orderValue) && !houseId.IsNull()) {
            std::vector<ChildId> conflictingSiblings;
            for (const auto& [otherId, st] : hh.AllChildStates()) {
                if (otherId == childId) {
                    continue;
                }
                const HouseId otherHouse = !st.currentHouseId.IsNull()
                                               ? st.currentHouseId
                                               : hh.GetTenantHouse(otherId.raw());
                if (otherHouse != houseId) {
                    continue;
                }
                if (std::abs(st.currentOrder - orderValue) < 0.01f &&
                    (st.orderExpiryGameDay < 0.0f || currentGameDays < st.orderExpiryGameDay)) {
                    conflictingSiblings.push_back(otherId);
                }
            }
            for (ChildId siblingId : conflictingSiblings) {
                if (auto* sib = hh.FindChildStateMutable(siblingId)) {
                    sib->currentOrder = 0.0f;
                    sib->orderExpiryGameDay = -1.0f;
                    out.previousOrderClearedSameType = true;
                    out.updatedChildren.push_back(siblingId);
                }
            }
        }

        targetState->currentOrder = orderValue;
        targetState->orderExpiryGameDay = expiry;
        out.applied = true;
        out.orderToApply = orderValue;
        out.updatedChildren.push_back(childId);
        return out;
    }

    std::vector<ChildId> ApplyExpiredOrders(
        State::WorldState& ws,
        float currentGameDays) {
        std::vector<ChildId> expiredIds;
        auto& hh = ws.household.Write();
        for (const auto& [cid, st] : hh.AllChildStates()) {
            if (st.currentOrder != 0.0f && st.orderExpiryGameDay > 0.0f && currentGameDays >= st.orderExpiryGameDay) {
                expiredIds.push_back(cid);
            }
        }
        for (ChildId cid : expiredIds) {
            if (auto* mut = hh.FindChildStateMutable(cid)) {
                mut->currentOrder = 0.0f;
                mut->orderExpiryGameDay = -1.0f;
            }
        }
        return expiredIds;
    }

    std::vector<ChildId> SelectForcegreetChildrenByHouse(
        const State::WorldStateSnapshot& snap,
        HouseId houseId,
        bool onlyPhysicalActors) noexcept {
        std::vector<ChildId> result;
        if (houseId.IsNull() || !snap.household) {
            return result;
        }
        for (const auto& [cid, st] : snap.household->AllChildStates()) {
            const HouseId resolvedHouse = !st.currentHouseId.IsNull()
                                              ? st.currentHouseId
                                              : snap.household->GetTenantHouse(cid.raw());
            if (resolvedHouse == houseId && st.forcegreetPending &&
                (!onlyPhysicalActors || !cid.IsSynthetic())) {
                result.push_back(cid);
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    ChildId PickWeightedChild(
        const State::WorldStateSnapshot& snap,
        const std::vector<ChildId>& candidates,
        float currentGameDays,
        Foundation::IRng& rng) noexcept {
        std::vector<ChildId> valid;
        valid.reserve(candidates.size());
        for (ChildId cid : candidates) {
            if (!cid.IsNull() && !cid.IsSynthetic()) {
                valid.push_back(cid);
            }
        }
        if (valid.empty()) {
            return ChildId{};
        }
        if (valid.size() == 1u) {
            return valid.front();
        }

        double totalWeight = 0.0;
        std::vector<double> weights;
        weights.reserve(valid.size());
        for (ChildId cid : valid) {
            float lastTs = 0.0f;
            if (snap.household) {
                if (const auto* st = snap.household->FindChildState(cid)) {
                    lastTs = st->lastInteractionTimestamp;
                }
            }
            const float daysElapsed = (lastTs > 0.0f) ? (currentGameDays - lastTs) : 30.0f;
            const double w = static_cast<double>(std::max(0.1f, daysElapsed));
            weights.push_back(w);
            totalWeight += w;
        }
        if (totalWeight <= 0.0) {
            const auto idx = static_cast<std::size_t>(
                rng.NextInt(0, static_cast<std::int32_t>(valid.size() - 1)));
            return valid[idx];
        }

        const float pick = rng.NextFloat(0.0f, static_cast<float>(totalWeight));
        double accum = 0.0;
        for (std::size_t i = 0; i < valid.size(); ++i) {
            accum += weights[i];
            if (static_cast<double>(pick) <= accum) {
                return valid[i];
            }
        }
        return valid.back();
    }

}  // namespace MARAS::Domain::Lineage
