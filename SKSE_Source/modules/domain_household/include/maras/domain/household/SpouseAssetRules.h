#pragma once

#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"

#include <span>
#include <utility>
#include <vector>

namespace MARAS::Domain::Household {

    using Foundation::ActorId;
    using Foundation::CellId;
    using Foundation::FormRawId;
    using Foundation::RefId;

    struct ShareCellOutcome {
        bool shared{ false };
        std::vector<CellId> newlySharedCells{};
    };

    struct StopSharingOutcome {
        bool stopped{ false };
        std::vector<CellId> restoredPrivateCells{};
    };

    struct LoadedCellBedReconcilePlan {
        bool shouldEnsureCellPublic{ false };
        bool shouldRecordOriginalPublic{ false };
        std::vector<ActorId> sharingSpouses{};
        std::vector<std::pair<RefId, FormRawId>> staleTrackedBedsToRestore{};
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

    [[nodiscard]] LoadedCellBedReconcilePlan PlanLoadedCellBedReconciliation(
        const State::WorldStateSnapshot& snap,
        CellId cellId,
        std::span<const RefId> loadedCellBedRefs);

}  // namespace MARAS::Domain::Household
