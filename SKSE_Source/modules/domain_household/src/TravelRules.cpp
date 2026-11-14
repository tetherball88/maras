#include "maras/domain/household/TravelRules.h"

#include <algorithm>
#include <string>

namespace MARAS::Domain::Household {

    std::vector<TravelDestinationCandidate> BuildFamilyVisitCandidates(
        const Projection::PerspectiveView& view,
        ChildId childId) {
        std::vector<TravelDestinationCandidate> result;
        if (childId.IsNull()) {
            return result;
        }

        const auto parentIds = view.GetVisitableFamilyParentIds(childId);
        const auto& spatial = view.Spatial();

        for (ActorId pid : parentIds) {
            if (pid.IsNull() || pid.IsPlayer() || pid.IsUnknownFather()) {
                continue;
            }
            const auto* sample = spatial.Find(pid.AsRefId());
            if (!sample || sample->IsDead() || sample->IsDeleted() || sample->IsDisabled()) {
                continue;
            }
            TravelDestinationCandidate cand{};
            cand.targetMarkerId = sample->refId;
            cand.targetLocationId = sample->currentLocationId;
            cand.targetCellId = sample->parentCellId;
            cand.destinationName = std::string(sample->DisplayNameView());
            if (cand.destinationName.empty()) {
                cand.destinationName = "Family Home";
            }
            cand.isFamilyVisit = true;
            result.push_back(std::move(cand));
        }
        return result;
    }

    std::optional<TravelDestinationCandidate> SelectNextTravelDestination(
        const std::vector<TravelDestinationCandidate>& familyDestinations,
        const std::vector<TravelDestinationCandidate>& innDestinations,
        RefId previousMarkerId,
        Foundation::IRng& rng) {
        if (familyDestinations.empty() && innDestinations.empty()) {
            return std::nullopt;
        }

        auto filterPool = [&](const std::vector<TravelDestinationCandidate>& pool) {
            std::vector<TravelDestinationCandidate> filtered;
            filtered.reserve(pool.size());
            for (const auto& d : pool) {
                if (!previousMarkerId.IsNull() && d.targetMarkerId == previousMarkerId) {
                    continue;
                }
                filtered.push_back(d);
            }
            if (filtered.empty()) {
                filtered = pool;
            }
            return filtered;
        };

        std::vector<TravelDestinationCandidate> chosenPool;
        if (!familyDestinations.empty() &&
            (innDestinations.empty() || rng.RollPercent(static_cast<float>(kFamilyVisitChancePercent)))) {
            chosenPool = filterPool(familyDestinations);
        } else {
            chosenPool = filterPool(innDestinations);
        }

        if (chosenPool.empty()) {
            return std::nullopt;
        }
        const auto idx = static_cast<std::size_t>(
            rng.NextInt(0, static_cast<std::int32_t>(chosenPool.size() - 1)));
        return chosenPool[idx];
    }

    State::AdultChildTravelRecord CreateTravelRecord(
        const TravelDestinationCandidate& dest,
        float currentGameDay,
        bool alreadyAtDestination,
        Foundation::IRng& rng) {
        State::AdultChildTravelRecord rec{};
        rec.targetMarkerId = dest.targetMarkerId;
        rec.targetLocationId = dest.targetLocationId;
        rec.targetCellId = dest.targetCellId;
        rec.destinationName = dest.destinationName;
        rec.assignedGameDay = currentGameDay;
        rec.arrivedGameDay = alreadyAtDestination ? currentGameDay : -1.0f;
        rec.stayDurationDays = rng.NextFloat(kMinTravelStayDays, kMaxTravelStayDays);
        rec.isFamilyVisit = dest.isFamilyVisit;
        return rec;
    }

    TravelTickOutcome EvaluateTravelTick(
        State::WorldState& ws,
        float currentGameDay,
        const State::SpatialActorSnapshot& spatial) {
        TravelTickOutcome out{};

        std::vector<ChildId> travelIds;
        for (const auto& [cid, tr] : ws.household.Read().AllAdultChildTravel()) {
            (void)tr;
            travelIds.push_back(cid);
        }

        for (ChildId cid : travelIds) {
            const auto* childRec = ws.lineage.Read().FindChild(cid);
            const bool stillValidAdult =
                childRec &&
                childRec->stage == Foundation::ChildStage::Adult &&
                !cid.IsSynthetic() &&
                ws.household.Read().GetTenantHouse(cid.raw()).IsNull() &&
                !ws.IsMarried(ActorId{ cid.raw() }, ActorId::Player());
            if (!stillValidAdult) {
                if (ws.household.Write().EraseAdultChildTravel(cid)) {
                    out.unregisteredInvalidAdults.push_back(cid);
                }
                continue;
            }

            auto* rec = const_cast<State::AdultChildTravelRecord*>(
                ws.household.Read().FindAdultChildTravel(cid));
            if (!rec) {
                continue;
            }

            bool justArrived = false;
            if (rec->arrivedGameDay < 0.0f) {
                if (const auto* actorSample = spatial.Find(RefId{ cid.raw() })) {
                    bool reached = false;
                    if (!rec->targetLocationId.IsNull() &&
                        actorSample->currentLocationId == rec->targetLocationId) {
                        reached = true;
                    } else if (!rec->targetCellId.IsNull() &&
                               actorSample->parentCellId == rec->targetCellId) {
                        reached = true;
                    } else if (!rec->targetMarkerId.IsNull()) {
                        if (const auto distSq = spatial.GetDistanceSquared(RefId{ cid.raw() }, rec->targetMarkerId)) {
                            reached = (*distSq <= 300.0f * 300.0f);
                        }
                    }
                    if (reached) {
                        State::AdultChildTravelRecord updated = *rec;
                        updated.arrivedGameDay = currentGameDay;
                        ws.household.Write().UpsertAdultChildTravel(cid, std::move(updated));
                        rec = const_cast<State::AdultChildTravelRecord*>(
                            ws.household.Read().FindAdultChildTravel(cid));
                        justArrived = true;
                        out.arrivedChildren.push_back(cid);
                    }
                }
            }

            if (rec) {
                const bool stayElapsed =
                    (rec->arrivedGameDay >= 0.0f) &&
                    ((currentGameDay - rec->arrivedGameDay) >= rec->stayDurationDays);
                const bool travelTimedOut =
                    (rec->assignedGameDay >= 0.0f) &&
                    ((currentGameDay - rec->assignedGameDay) >= kMaxTravelTimeoutDays);
                if (!justArrived && (stayElapsed || travelTimedOut)) {
                    out.childrenNeedingRotation.push_back(cid);
                }
            }
        }

        return out;
    }

}  // namespace MARAS::Domain::Household
