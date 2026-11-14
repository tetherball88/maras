#pragma once

#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"

#include <array>
#include <cstddef>
#include <vector>

namespace MARAS::Domain::Relationships {

    using Foundation::ActorId;

    struct HierarchyShiftEvent {
        ActorId spouse{};
        bool isPromotion{ false };
        float rankDiff{ 0.0f };

        bool operator==(const HierarchyShiftEvent&) const noexcept = default;
    };

    struct HierarchyMutationOutcome {
        bool validRequest{ false };
        bool slotsChanged{ false };
        int assignedSlot{ -1 };
        std::array<ActorId, State::SpouseHierarchyTable::kSlotCount> oldSlots{};
        std::array<ActorId, State::SpouseHierarchyTable::kSlotCount> newSlots{};
        std::vector<ActorId> clearedActors{};
        std::vector<ActorId> dirtyActors{};
        std::vector<HierarchyShiftEvent> events{};
    };

    [[nodiscard]] std::vector<ActorId> CollectLivingMarriedSpouses(const State::WorldState& ws);

    HierarchyMutationOutcome ApplySetHierarchyRank(
        State::WorldState& ws,
        ActorId npcId,
        int rank);

    HierarchyMutationOutcome ApplyOnSpouseAdded(
        State::WorldState& ws,
        ActorId npcId);

    HierarchyMutationOutcome ApplyOnSpouseRemoved(
        State::WorldState& ws,
        ActorId npcId);

    HierarchyMutationOutcome ApplyFillHierarchyGaps(
        State::WorldState& ws,
        ActorId excludeActor,
        bool emitEvents);

}  // namespace MARAS::Domain::Relationships
