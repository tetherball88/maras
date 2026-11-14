#pragma once

#include "maras/foundation/StrongIds.h"
#include "maras/state/DomainTypes.h"
#include "maras/state/WorldState.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace MARAS::State {

    enum class FactionMembershipOp : std::uint8_t {
        LeaveUntouched = 0,
        EnsurePresent = 1,
        EnsureAbsent = 2
    };

    enum class PackageRole : std::uint8_t {
        None = 0,
        HousedTenant = 1,
        PetOrCritter = 2,
        FollowParentChild = 3,
        AdultChildTravel = 4
    };

    struct DesiredGlobalsPlan {
        std::int32_t loveInterestsCount{ 0 };
        std::int32_t spousesCount{ 0 };
        std::int32_t playerHousesCount{ 0 };
        bool enablePolygamy{ false };
        bool hasSkyrimNet{ false };

        bool operator==(const DesiredGlobalsPlan&) const noexcept = default;
    };

    struct PlayerFactionPlan {
        bool hasMarriedFaction{ false };
        std::int8_t canFormNonCheatingMainRank{ 1 };

        bool operator==(const PlayerFactionPlan&) const noexcept = default;
    };

    struct AdultActorFactionPlan {
        bool isRelevantAdult{ false };
        bool isRegisteredAdult{ false };
        bool isGraduatedAdultChild{ false };
        bool isHousedAdultTenant{ false };

        // MARAS ranked factions (nullopt => EnsureAbsent / remove if present)
        std::optional<std::int8_t> trackedRank{};
        std::optional<std::int8_t> socialClassRank{};
        std::optional<std::int8_t> skillTypeRank{};
        std::optional<std::int8_t> temperamentRank{};
        std::optional<std::int8_t> hierarchyRank{};
        std::optional<std::int8_t> affectionRank{};
        std::optional<std::int8_t> spouseChildrenRank{};
        std::optional<std::int8_t> spouseCheatingRank{};
        std::optional<std::int8_t> spouseOpenRelationshipRank{};
        std::optional<std::int8_t> canFormNonCheatingMainRank{};
        std::optional<std::int8_t> housedRank{};

        // Vanilla marriage & follower/werewolf factions
        FactionMembershipOp marriedFactionOp{ FactionMembershipOp::LeaveUntouched };
        FactionMembershipOp playerFactionOp{ FactionMembershipOp::LeaveUntouched };
        FactionMembershipOp playerBedOwnershipOp{ FactionMembershipOp::LeaveUntouched };
        FactionMembershipOp marriageAskedOp{ FactionMembershipOp::LeaveUntouched };
        FactionMembershipOp courtingOp{ FactionMembershipOp::LeaveUntouched };
        FactionMembershipOp potentialHirelingOp{ FactionMembershipOp::LeaveUntouched };
        FactionMembershipOp marriagePotentialOp{ FactionMembershipOp::LeaveUntouched };
        bool clearIgnoreProposeKeyword{ false };

        // Graduated Stage 4 adult children flags
        bool clearYoungChildFactions{ false };
        bool ensurePotentialFollower{ false };
        bool ensurePlayerWerewolf{ false };
        bool ensureCompanionsCircle{ false };
        bool ensureFrostmoonWerewolves{ false };

        bool operator==(const AdultActorFactionPlan&) const noexcept = default;
    };

    struct ChildFactionPlan {
        bool isYoungChild{ false };
        bool inPlayerHousehold{ false };
        bool restoreSavedCrimeFaction{ false };

        std::optional<std::int8_t> playerChildRank{};
        std::optional<std::int8_t> byohChildOwnedRank{};
        std::optional<std::int8_t> byohAdoptionRank{};
        std::optional<std::int8_t> playerFactionRank{};
        std::optional<std::int8_t> bothParentsRank{};

        FactionMembershipOp adoptableOp{ FactionMembershipOp::LeaveUntouched };
        std::int8_t adoptableRank{ 0 };

        [[nodiscard]] constexpr std::optional<std::int8_t> DesiredAdoptableRank() const noexcept {
            if (adoptableOp == FactionMembershipOp::EnsurePresent) {
                return adoptableRank;
            }
            return std::nullopt;
        }

        bool operator==(const ChildFactionPlan&) const noexcept = default;
    };

    struct CompanionFactionPlan {
        bool isActiveCompanion{ false };
        ChildId ownerChildId{};
        std::optional<std::int8_t> playerChildPetRank{};
        std::optional<std::int8_t> playerFactionRank{};

        bool operator==(const CompanionFactionPlan&) const noexcept = default;
    };

    struct ActorPackagePlan {
        PackageRole role{ PackageRole::None };
        bool isTenant{ false };
        bool needsAdultHomeMarker{ false };
        RefId homeSandboxTargetId{};   // Keyword 0x6B target (home marker for tenant, ownerChildId for pet/critter)
        RefId childFollowTargetId{};   // Keyword 0x377 target (parent/orphanage for Stage 2 child, travel marker for Stage 4 adult child)

        bool operator==(const ActorPackagePlan&) const noexcept = default;
    };

    struct BuffMultipliersPlan {
        static constexpr std::size_t kSocialClassCount = static_cast<std::size_t>(SocialClass::_Count);
        static constexpr std::size_t kSkillTypeCount = static_cast<std::size_t>(SkillType::_Count);

        std::array<float, kSocialClassCount> permanentSocialClassMultipliers{};
        std::array<float, kSkillTypeCount> followerSkillTypeMultipliers{};
        int livingMarriedSpouseCount{ 0 };

        bool operator==(const BuffMultipliersPlan&) const noexcept = default;
    };

    struct BonusTargetValue {
        bool isActive{ false };
        float rawScaledValue{ 0.0f };
        float engineTargetValue{ 0.0f };

        bool operator==(const BonusTargetValue&) const noexcept = default;
    };

    // Pure affection & hierarchy multipliers shared between C++ buff reconciler and UI snapshots
    [[nodiscard]] constexpr float ComputeAffectionMultiplier(int permanentAffection) noexcept {
        if (permanentAffection >= 75) {
            return 1.25f;
        }
        if (permanentAffection >= 50) {
            return 1.0f;
        }
        if (permanentAffection >= 25) {
            return 0.25f;
        }
        return 0.0f;
    }

    [[nodiscard]] constexpr float ComputeHierarchyRankMultiplier(int rank, int marriedSpouseCount) noexcept {
        if (marriedSpouseCount <= 0) {
            return 0.0f;
        }
        if (marriedSpouseCount == 1) {
            return 2.0f;
        }
        switch (rank) {
            case 0:
                return 1.0f;
            case 1:
                return 0.5f;
            case 2:
                return 0.25f;
            default:
                return 0.1f;
        }
    }

    [[nodiscard]] DesiredGlobalsPlan ComputeDesiredGlobals(
        const WorldStateSnapshot& snap,
        bool skyrimNetDllLoaded) noexcept;

    [[nodiscard]] PlayerFactionPlan ComputePlayerFactionPlan(const WorldStateSnapshot& snap) noexcept;

    [[nodiscard]] AdultActorFactionPlan ComputeAdultActorFactionPlan(
        const WorldStateSnapshot& snap,
        ActorId actorId,
        std::string_view adultChildClassId = {});

    [[nodiscard]] ChildFactionPlan ComputeChildFactionPlan(
        const WorldStateSnapshot& snap,
        ChildId childId,
        ActorSex otherSpouseParentSex = ActorSex::Unknown,
        RefId resolvedFollowTargetId = {},
        RefId honorhallOrphanageMarkerId = {});

    [[nodiscard]] CompanionFactionPlan ComputeCompanionFactionPlan(
        const WorldStateSnapshot& snap,
        RefId companionId);

    [[nodiscard]] ActorPackagePlan ComputeActorPackagePlan(
        const WorldStateSnapshot& snap,
        RefId actorId,
        RefId resolvedChildFollowTargetId = {});

    [[nodiscard]] BuffMultipliersPlan ComputeBuffMultipliers(
        const WorldStateSnapshot& snap,
        std::span<const RefId> activeTeammates);

    [[nodiscard]] BonusTargetValue ComputeBonusTargetValue(
        std::string_view bonusType,
        float baseValue,
        float multiplier) noexcept;

}  // namespace MARAS::State
