#include "maras/domain/relationships/SpouseHierarchyRules.h"

#include <algorithm>
#include <unordered_set>

namespace MARAS::Domain::Relationships {

    namespace {
        constexpr int kSlotsCount = static_cast<int>(State::SpouseHierarchyTable::kSlotCount);
        constexpr int kOutOfTopRank = 4;

        void PopulateSlotDeltas(
            const std::array<ActorId, State::SpouseHierarchyTable::kSlotCount>& oldSlots,
            const std::array<ActorId, State::SpouseHierarchyTable::kSlotCount>& newSlots,
            ActorId skipActorInShiftEvents,
            HierarchyMutationOutcome& out) {
            out.oldSlots = oldSlots;
            out.newSlots = newSlots;
            out.slotsChanged = (oldSlots != newSlots);

            std::unordered_set<ActorId> dirtySet;
            for (const ActorId oldId : oldSlots) {
                if (oldId.IsNull()) {
                    continue;
                }
                dirtySet.insert(oldId);
                const bool stillPresent = std::any_of(newSlots.begin(), newSlots.end(), [&](ActorId s) {
                    return s == oldId;
                });
                if (!stillPresent && oldId != skipActorInShiftEvents) {
                    out.clearedActors.push_back(oldId);
                }
            }

            for (std::size_t newIdx = 0; newIdx < newSlots.size(); ++newIdx) {
                const ActorId id = newSlots[newIdx];
                if (id.IsNull()) {
                    continue;
                }
                dirtySet.insert(id);
                if (id == skipActorInShiftEvents) {
                    continue;
                }
                int prevIdx = kOutOfTopRank;
                for (std::size_t j = 0; j < oldSlots.size(); ++j) {
                    if (oldSlots[j] == id) {
                        prevIdx = static_cast<int>(j);
                        break;
                    }
                }
                const int curIdx = static_cast<int>(newIdx);
                if (prevIdx != curIdx) {
                    out.events.push_back(HierarchyShiftEvent{
                        .spouse = id,
                        .isPromotion = (curIdx < prevIdx),
                        .rankDiff = static_cast<float>(prevIdx - curIdx)
                    });
                }
            }

            out.dirtyActors.assign(dirtySet.begin(), dirtySet.end());
        }
    }  // namespace

    std::vector<ActorId> CollectLivingMarriedSpouses(const State::WorldState& ws) {
        const auto married = ws.GetPlayerSpousesInMarriageOrder();
        std::vector<ActorId> living;
        living.reserve(married.size());
        const auto& deceased = ws.deceased.Read();
        for (const ActorId id : married) {
            if (!id.IsNull() && !deceased.IsDeceased(id)) {
                living.push_back(id);
            }
        }
        return living;
    }

    HierarchyMutationOutcome ApplySetHierarchyRank(
        State::WorldState& ws,
        ActorId npcId,
        int rank) {
        HierarchyMutationOutcome outcome;
        if (npcId.IsNull()) {
            return outcome;
        }
        outcome.validRequest = true;

        const auto oldSlots = ws.hierarchy.Read().Slots();
        const int oldRank = ws.hierarchy.Read().GetRank(npcId);

        if (rank < 0 || rank >= kSlotsCount) {
            if (oldRank >= 0 && oldRank < kSlotsCount) {
                const auto liveMarried = CollectLivingMarriedSpouses(ws);
                ws.hierarchy.Write().SetRank(npcId, rank, liveMarried);
                const auto newSlots = ws.hierarchy.Read().Slots();

                outcome.clearedActors.push_back(npcId);
                outcome.events.push_back(HierarchyShiftEvent{
                    .spouse = npcId,
                    .isPromotion = false,
                    .rankDiff = static_cast<float>(oldRank - kSlotsCount)
                });
                PopulateSlotDeltas(oldSlots, newSlots, npcId, outcome);
                if (std::find(outcome.dirtyActors.begin(), outcome.dirtyActors.end(), npcId) == outcome.dirtyActors.end()) {
                    outcome.dirtyActors.push_back(npcId);
                }
            } else {
                outcome.oldSlots = oldSlots;
                outcome.newSlots = oldSlots;
            }
            return outcome;
        }

        if (oldRank == rank) {
            outcome.oldSlots = oldSlots;
            outcome.newSlots = oldSlots;
            return outcome;
        }

        const ActorId occupant = oldSlots[static_cast<std::size_t>(rank)];
        ws.hierarchy.Write().SetRank(npcId, rank, {});
        const auto newSlots = ws.hierarchy.Read().Slots();

        outcome.oldSlots = oldSlots;
        outcome.newSlots = newSlots;
        outcome.slotsChanged = true;

        if (occupant.IsNull()) {
            outcome.dirtyActors.push_back(npcId);
            outcome.events.push_back(HierarchyShiftEvent{
                .spouse = npcId,
                .isPromotion = (rank < oldRank),
                .rankDiff = static_cast<float>(oldRank - rank)
            });
        } else if (oldRank >= 0 && oldRank < kSlotsCount) {
            outcome.dirtyActors.push_back(npcId);
            outcome.dirtyActors.push_back(occupant);
            outcome.events.push_back(HierarchyShiftEvent{
                .spouse = npcId,
                .isPromotion = (rank < oldRank),
                .rankDiff = static_cast<float>(oldRank - rank)
            });
            outcome.events.push_back(HierarchyShiftEvent{
                .spouse = occupant,
                .isPromotion = (oldRank < rank),
                .rankDiff = static_cast<float>(rank - oldRank)
            });
        } else {
            outcome.dirtyActors.push_back(npcId);
            outcome.dirtyActors.push_back(occupant);
            outcome.clearedActors.push_back(occupant);
            outcome.events.push_back(HierarchyShiftEvent{
                .spouse = npcId,
                .isPromotion = true,
                .rankDiff = static_cast<float>(oldRank - rank)
            });
            outcome.events.push_back(HierarchyShiftEvent{
                .spouse = occupant,
                .isPromotion = false,
                .rankDiff = static_cast<float>(rank - kSlotsCount)
            });
        }

        return outcome;
    }

    HierarchyMutationOutcome ApplyOnSpouseAdded(
        State::WorldState& ws,
        ActorId npcId) {
        HierarchyMutationOutcome outcome;
        if (npcId.IsNull()) {
            return outcome;
        }
        outcome.validRequest = true;
        outcome.oldSlots = ws.hierarchy.Read().Slots();

        const int before = ws.hierarchy.Read().GetRank(npcId);
        if (before <= 2) {
            outcome.newSlots = outcome.oldSlots;
            return outcome;
        }

        ws.hierarchy.Write().OnSpouseAdded(npcId);
        const int after = ws.hierarchy.Read().GetRank(npcId);
        outcome.newSlots = ws.hierarchy.Read().Slots();
        outcome.slotsChanged = (outcome.oldSlots != outcome.newSlots);

        if (after <= 2) {
            outcome.assignedSlot = after;
            outcome.dirtyActors.push_back(npcId);
        }
        return outcome;
    }

    HierarchyMutationOutcome ApplyOnSpouseRemoved(
        State::WorldState& ws,
        ActorId npcId) {
        HierarchyMutationOutcome outcome;
        if (npcId.IsNull()) {
            return outcome;
        }
        outcome.validRequest = true;

        const auto oldSlots = ws.hierarchy.Read().Slots();
        const bool wasPresent = std::any_of(oldSlots.begin(), oldSlots.end(), [&](ActorId id) {
            return id == npcId;
        });
        if (!wasPresent) {
            outcome.oldSlots = oldSlots;
            outcome.newSlots = oldSlots;
            return outcome;
        }

        const auto married = ws.GetPlayerSpousesInMarriageOrder();
        const auto& deceased = ws.deceased.Read();
        auto& hier = ws.hierarchy.Write();
        for (std::size_t i = 0; i < State::SpouseHierarchyTable::kSlotCount; ++i) {
            if (hier.GetSpouseAtSlot(i) == npcId) {
                hier.SetRawSlot(i, ActorId{});
            }
        }
        hier.FillGaps(married, npcId, [&](ActorId id) {
            return deceased.IsDeceased(id);
        });
        const auto newSlots = hier.Slots();

        outcome.clearedActors.push_back(npcId);
        PopulateSlotDeltas(oldSlots, newSlots, npcId, outcome);
        if (std::find(outcome.dirtyActors.begin(), outcome.dirtyActors.end(), npcId) == outcome.dirtyActors.end()) {
            outcome.dirtyActors.push_back(npcId);
        }
        return outcome;
    }

    HierarchyMutationOutcome ApplyFillHierarchyGaps(
        State::WorldState& ws,
        ActorId excludeActor,
        bool emitEvents) {
        HierarchyMutationOutcome outcome;
        outcome.validRequest = true;

        const auto oldSlots = ws.hierarchy.Read().Slots();
        const auto married = ws.GetPlayerSpousesInMarriageOrder();
        const auto& deceased = ws.deceased.Read();
        ws.hierarchy.Write().FillGaps(married, excludeActor, [&](ActorId id) {
            return deceased.IsDeceased(id);
        });
        const auto newSlots = ws.hierarchy.Read().Slots();

        PopulateSlotDeltas(oldSlots, newSlots, ActorId{}, outcome);
        if (!emitEvents) {
            outcome.events.clear();
        }
        return outcome;
    }

}  // namespace MARAS::Domain::Relationships
