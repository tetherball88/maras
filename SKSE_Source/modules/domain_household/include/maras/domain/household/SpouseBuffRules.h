#pragma once

#include "maras/domain/relationships/BonusesDomain.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/EngineSyncPlan.h"
#include "maras/state/WorldState.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace MARAS::Domain::Household {

    using Foundation::ActorId;
    using Foundation::ActorSex;
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

    enum class PartnerSleepBuff : std::uint8_t {
        None = 0,
        LoversRadiance = 1,
        MarriageSleep = 2
    };

    enum class ChildSleepBuff : std::uint8_t {
        None = 0,
        AdoptionMale = 1,
        AdoptionFemale = 2
    };

    struct SleepRestedPlan {
        PartnerSleepBuff partnerBuff{ PartnerSleepBuff::None };
        ChildSleepBuff childBuff{ ChildSleepBuff::None };

        bool operator==(const SleepRestedPlan&) const noexcept = default;
    };

    [[nodiscard]] std::vector<ActorId> EvaluateSleepPaidAffectionPartners(
        const State::WorldStateSnapshot& snap,
        std::span<const ActorId> sameCellActors);

    [[nodiscard]] SleepRestedPlan EvaluateSleepRestedPlan(
        const State::WorldStateSnapshot& snap,
        State::HouseId currentLocationId,
        std::span<const std::uint32_t> presentAtLocationRawIds,
        ActorSex playerSex);

}  // namespace MARAS::Domain::Household
