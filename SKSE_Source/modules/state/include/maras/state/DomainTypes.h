#pragma once

#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/StrongIds.h"

#include <cstdint>

namespace MARAS::State {

    using Foundation::ActorId;
    using Foundation::ActorSex;
    using Foundation::AffairDiscovery;
    using Foundation::AffairId;
    using Foundation::AffairResolution;
    using Foundation::AffairStage;
    using Foundation::AffairStatus;
    using Foundation::AffairType;
    using Foundation::BaseId;
    using Foundation::CanonicalPairKey;
    using Foundation::CanonicalPairKeyHash;
    using Foundation::CellId;
    using Foundation::ChildCustodyGenderPolicy;
    using Foundation::ChildCustodyPlayerPolicy;
    using Foundation::ChildId;
    using Foundation::ChildStage;
    using Foundation::FormRawId;
    using Foundation::HouseId;
    using Foundation::IsActivePartnerStatus;
    using Foundation::IsBreakupStatus;
    using Foundation::IsMainTierStatus;
    using Foundation::LocationId;
    using Foundation::MatchmakingOrientation;
    using Foundation::PartnerIntentType;
    using Foundation::PhenotypeTier;
    using Foundation::RefId;
    using Foundation::RelationshipStatus;
    using Foundation::SkillType;
    using Foundation::SocialClass;
    using Foundation::SyntheticChildId;
    using Foundation::Temperament;

    // Slot-neutral custodial guardian diff result
    struct GuardianSetDiff {
        bool changed{ false };
        ActorId removedA{};
        ActorId removedB{};
        ActorId addedA{};
        ActorId addedB{};

        [[nodiscard]] constexpr ActorId PrimaryRemoved() const noexcept {
            return !removedA.IsNull() ? removedA : removedB;
        }
        [[nodiscard]] constexpr ActorId PrimaryAdded() const noexcept {
            return !addedA.IsNull() ? addedA : addedB;
        }

        bool operator==(const GuardianSetDiff&) const noexcept = default;
    };

}  // namespace MARAS::State

namespace maras {
    namespace state = ::MARAS::State;
}
