#pragma once

#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"
#include "maras/state/WorldTables.h"

#include <string_view>
#include <vector>

namespace MARAS::Domain::Agency {

    using Foundation::ActorId;
    using Foundation::ActorSex;
    using Foundation::MatchmakingOrientation;
    using Foundation::RelationshipStatus;
    using Foundation::SkillType;
    using Foundation::SocialClass;
    using Foundation::Temperament;

    struct NpcMatchmakingProfile {
        ActorId actorId{};
        std::string name{};
        ActorSex sex{ ActorSex::Unknown };
        SocialClass socialClass{ SocialClass::Outcast };
        SkillType skillType{ SkillType::Warrior };
        Temperament temperament{ Temperament::Proud };
    };

    [[nodiscard]] bool IsOrientationCompatible(
        MatchmakingOrientation orientation,
        ActorSex sexA,
        ActorSex sexB) noexcept;

    [[nodiscard]] bool IsNpcOnMatchmakingCooldown(
        const State::WorldStateSnapshot& snap,
        ActorId actorId,
        float currentGameDay) noexcept;

    [[nodiscard]] bool IsEligibleForScheduledMatchmaking(
        const State::WorldStateSnapshot& snap,
        ActorId actorId,
        float currentGameDay) noexcept;

    [[nodiscard]] std::string_view GetEligibilityCategoryString(
        bool canFormMain,
        bool hasOpenMain) noexcept;

    [[nodiscard]] std::string_view GetOrientationRuleText(
        MatchmakingOrientation orientation) noexcept;

}  // namespace MARAS::Domain::Agency
