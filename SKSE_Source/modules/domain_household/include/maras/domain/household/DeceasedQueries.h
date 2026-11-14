#pragma once

#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"

#include <vector>

namespace MARAS::Domain::Household {

    using Foundation::ActorId;
    using Foundation::BaseId;
    using Foundation::ChildId;
    using Foundation::RefId;
    using Foundation::RelationshipStatus;

    [[nodiscard]] bool IsDeceased(
        const State::WorldStateSnapshot& snap,
        ActorId actorId,
        BaseId optionalKnownBase = BaseId{}) noexcept;

    [[nodiscard]] bool IsWidowOrWidower(
        const State::WorldStateSnapshot& snap,
        ActorId survivingActor) noexcept;

    [[nodiscard]] bool WasMarriedTo(
        const State::WorldStateSnapshot& snap,
        ActorId deceasedActor,
        ActorId partnerActor,
        BaseId deceasedBase = BaseId{},
        BaseId partnerBase = BaseId{},
        RefId partnerRef = RefId{}) noexcept;

    [[nodiscard]] bool WasEngagedTo(
        const State::WorldStateSnapshot& snap,
        ActorId deceasedActor,
        ActorId partnerActor,
        BaseId deceasedBase = BaseId{},
        BaseId partnerBase = BaseId{},
        RefId partnerRef = RefId{}) noexcept;

    [[nodiscard]] bool WasExOf(
        const State::WorldStateSnapshot& snap,
        ActorId deceasedActor,
        ActorId partnerActor,
        BaseId deceasedBase = BaseId{},
        BaseId partnerBase = BaseId{},
        RefId partnerRef = RefId{}) noexcept;

    [[nodiscard]] std::vector<BaseId> GetDeceasedSpousesOf(
        const State::WorldStateSnapshot& snap,
        ActorId survivingPartner,
        BaseId partnerBase = BaseId{},
        RefId partnerRef = RefId{});

    [[nodiscard]] std::vector<BaseId> GetDeceasedPartnersOf(
        const State::WorldStateSnapshot& snap,
        ActorId survivingPartner,
        BaseId partnerBase = BaseId{},
        RefId partnerRef = RefId{});

    [[nodiscard]] std::vector<ChildId> GetDeceasedChildrenOf(
        const State::WorldStateSnapshot& snap,
        ActorId parentActor,
        BaseId parentBase = BaseId{},
        RefId parentRef = RefId{});

}  // namespace MARAS::Domain::Household
