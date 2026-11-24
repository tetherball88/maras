#pragma once

#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"
#include "maras/state/WorldTables.h"

#include <string>
#include <string_view>
#include <utility>
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

    [[nodiscard]] std::string BuildAllowedOperationsText(
        bool allowBreakups,
        bool allowOpenClose);

    [[nodiscard]] std::string BuildAllowedActionsString(
        bool allowBreakups,
        bool allowOpenClose);

    [[nodiscard]] std::string_view BuildCheatingRuleText(bool allowCheating) noexcept;

    [[nodiscard]] std::string_view BuildBreakupRuleText(bool allowBreakups) noexcept;

    [[nodiscard]] std::string_view BuildOpenCloseRuleText(bool allowOpenClose) noexcept;

    [[nodiscard]] std::pair<std::string_view, RelationshipStatus> GetBreakupTargetStatus(
        RelationshipStatus currentStatus) noexcept;

}  // namespace MARAS::Domain::Agency
