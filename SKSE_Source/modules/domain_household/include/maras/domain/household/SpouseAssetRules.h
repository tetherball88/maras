#pragma once

#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"

#include <vector>

namespace MARAS::Domain::Household {

    using Foundation::ActorId;
    using Foundation::CellId;

    struct ShareCellOutcome {
        bool shared{ false };
        std::vector<CellId> newlySharedCells{};
    };

    struct StopSharingOutcome {
        bool stopped{ false };
        std::vector<CellId> restoredPrivateCells{};
    };

    ShareCellOutcome ApplyShareCellsWithPlayer(
        State::WorldState& ws,
        ActorId spouseId,
        const std::vector<CellId>& cellIds);

    StopSharingOutcome ApplyStopSharingForSpouse(
        State::WorldState& ws,
        ActorId spouseId);

    [[nodiscard]] bool IsCellSharedWithPlayer(
        const State::WorldStateSnapshot& snap,
        CellId cellId) noexcept;

}  // namespace MARAS::Domain::Household
