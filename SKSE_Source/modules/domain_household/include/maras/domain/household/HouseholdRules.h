#pragma once

#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"

#include <cstdint>
#include <vector>

namespace MARAS::Domain::Household {

    using Foundation::ActorId;
    using Foundation::ChildId;
    using Foundation::HouseId;
    using Foundation::RefId;

    struct RelocateFamilyOutcome {
        bool targetHouseExists{ false };
        bool ineligibleAdultAnchor{ false };
        HouseId targetHouseId{};
        std::vector<ActorId> movedAdults{};
        std::vector<ChildId> movedChildren{};
        std::vector<ChildId> changedHouseChildren{};
        std::vector<ChildId> newlyHousedChildren{};
        std::vector<std::uint32_t> skippedChildCustody{};
        std::vector<ActorId> dirtyActors{};
    };

    struct TenantRemovalEligibility {
        bool eligible{ false };
        bool isChild{ false };
        HouseId currentHouseId{};
    };

    bool ApplyRegisterHouse(
        State::WorldState& ws,
        HouseId houseId,
        RefId markerId,
        bool initialMoveDone = false);

    bool ApplyRegisterTenant(
        State::WorldState& ws,
        std::uint32_t tenantRawId,
        HouseId houseId,
        bool stayAtHome = true,
        bool isChildTenant = false);

    bool ApplySetTenantStayAtHome(
        State::WorldState& ws,
        std::uint32_t tenantRawId,
        bool stayAtHome,
        HouseId* outHouseId = nullptr);

    [[nodiscard]] TenantRemovalEligibility CanRemoveTenant(
        const State::WorldStateSnapshot& snap,
        std::uint32_t tenantRawId) noexcept;

    bool ApplyRemoveTenant(
        State::WorldState& ws,
        std::uint32_t tenantRawId,
        HouseId* outPreviousHouseId = nullptr);

    RelocateFamilyOutcome ApplyRelocateFamilyUnit(
        State::WorldState& ws,
        std::uint32_t anchorRawId,
        HouseId targetHouseId,
        bool moveParents = true);

}  // namespace MARAS::Domain::Household
