#pragma once

#include "maras/domain/relationships/BonusesDomain.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/EngineSyncPlan.h"
#include "maras/state/WorldState.h"

#include <array>
#include <span>
#include <string>
#include <vector>

namespace MARAS::Domain::Household {

    using Foundation::ActorId;
    using Foundation::RefId;
    using Foundation::SkillType;
    using Foundation::SocialClass;

    struct EvaluatedBonusEntry {
        std::string categoryKey{};
        std::string bonusName{};
        std::string description{};
        std::string unit{};
        std::string bonusType{};
        float baseValue{ 0.0f };
        float multiplier{ 0.0f };
        float effectiveValue{ 0.0f };
        float engineTargetValue{ 0.0f };
        bool isActive{ false };
    };

    struct HouseholdBuffReport {
        State::BuffMultipliersPlan rawMultipliers{};
        std::vector<EvaluatedBonusEntry> permanentEntries{};
        std::vector<EvaluatedBonusEntry> followerEntries{};
        int activePermanentCount{ 0 };
        int activeFollowerCount{ 0 };
    };

    [[nodiscard]] float ComputeSpouseMultiplier(
        const State::WorldStateSnapshot& snap,
        ActorId spouseId) noexcept;

    [[nodiscard]] std::array<float, State::BuffMultipliersPlan::kSkillTypeCount> ComputeFollowerMultipliers(
        const State::WorldStateSnapshot& snap,
        std::span<const RefId> followerIds) noexcept;

    [[nodiscard]] std::array<float, State::BuffMultipliersPlan::kSocialClassCount> ComputePermanentMultipliers(
        const State::WorldStateSnapshot& snap) noexcept;

    [[nodiscard]] HouseholdBuffReport EvaluateHouseholdBuffs(
        const State::WorldStateSnapshot& snap,
        std::span<const RefId> activeTeammates,
        const Relationships::BonusCatalog& catalog);

}  // namespace MARAS::Domain::Household
