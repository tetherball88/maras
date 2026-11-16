#pragma once

#include "maras/foundation/IRng.h"
#include "maras/foundation/StrongIds.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/WorldState.h"
#include "maras/state/WorldTables.h"

#include <optional>
#include <string>
#include <vector>

namespace MARAS::Domain::Household {

    using Foundation::ActorId;
    using Foundation::CellId;
    using Foundation::ChildId;
    using Foundation::LocationId;
    using Foundation::RefId;

    inline constexpr float kMinTravelStayDays = 2.0f;
    inline constexpr float kMaxTravelStayDays = 4.0f;
    inline constexpr float kMaxTravelTimeoutDays = 10.0f;
    inline constexpr int kFamilyVisitChancePercent = 30;

    struct TravelDestinationCandidate {
        RefId targetMarkerId{};
        LocationId targetLocationId{};
        CellId targetCellId{};
        std::string destinationName{};
        bool isFamilyVisit{ false };
    };

    struct TravelTickOutcome {
        std::vector<ChildId> arrivedChildren{};
        std::vector<ChildId> childrenNeedingRotation{};
        std::vector<ChildId> unregisteredInvalidAdults{};
        std::vector<ChildId> unassignedEligibleAdults{};
    };

    [[nodiscard]] bool IsTravelEligibleAdultChild(
        const State::WorldState& ws,
        ChildId childId) noexcept;

    [[nodiscard]] bool IsTravelEligibleAdultChild(
        const State::WorldStateSnapshot& snap,
        ChildId childId) noexcept;

    [[nodiscard]] std::vector<TravelDestinationCandidate> BuildFamilyVisitCandidates(
        const Projection::PerspectiveView& view,
        ChildId childId);

    [[nodiscard]] std::optional<TravelDestinationCandidate> SelectNextTravelDestination(
        const std::vector<TravelDestinationCandidate>& familyDestinations,
        const std::vector<TravelDestinationCandidate>& innDestinations,
        RefId previousMarkerId,
        Foundation::IRng& rng);

    [[nodiscard]] State::AdultChildTravelRecord CreateTravelRecord(
        const TravelDestinationCandidate& dest,
        float currentGameDay,
        bool alreadyAtDestination,
        Foundation::IRng& rng);

    TravelTickOutcome EvaluateTravelTick(
        State::WorldState& ws,
        float currentGameDay,
        const State::SpatialActorSnapshot& spatial);

}  // namespace MARAS::Domain::Household
