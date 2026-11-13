#pragma once

#include "maras/foundation/StrongIds.h"

#include <cstdint>

namespace MARAS::State {

    using Foundation::ActorId;
    using Foundation::AffairId;
    using Foundation::BaseId;
    using Foundation::CanonicalPairKey;
    using Foundation::CanonicalPairKeyHash;
    using Foundation::CellId;
    using Foundation::ChildId;
    using Foundation::FormRawId;
    using Foundation::HouseId;
    using Foundation::LocationId;
    using Foundation::RefId;
    using Foundation::SyntheticChildId;

    enum class SocialClass : std::uint8_t {
        Outcast = 0,
        Poverty = 1,
        Working = 2,
        Middle = 3,
        Wealthy = 4,
        Religious = 5,
        Nobles = 6,
        Rulers = 7,
        _Count
    };

    enum class SkillType : std::uint8_t {
        Warrior = 0,
        Mage = 1,
        Rogue = 2,
        Craftsman = 3,
        Ranger = 4,
        Orator = 5,
        _Count
    };

    enum class Temperament : std::uint8_t {
        Proud = 0,
        Humble = 1,
        Jealous = 2,
        Romantic = 3,
        Independent = 4,
        _Count
    };

    enum class RelationshipStatus : std::uint8_t {
        Candidate = 0,
        Engaged = 1,
        Married = 2,
        Divorced = 3,
        Jilted = 4,
        Dating = 5,
        Lover = 6,
        ExDating = 7,
        ExLover = 8,
        Unknown = 255
    };

    enum class AffairType : std::uint8_t {
        Casual = 0,
        Dating = 1,
        Lover = 2,
        _Count
    };

    enum class AffairStatus : std::uint8_t {
        Ongoing = 0,
        EndedBreakup = 1,
        EndedVictimSplit = 2,
        Allowed = 3,
        EndedConfronted = 4,
        _Count
    };

    enum class AffairDiscovery : std::uint8_t {
        None = 0,
        Confessed = 1,
        Exposed = 2,
        Caught = 3,
        _Count
    };

    enum class AffairResolution : std::uint8_t {
        None = 0,
        Confronted = 1,
        Allowed = 2,
        Breakup = 3,
        _Count
    };

    enum class AffairStage : std::uint8_t {
        Talking = 0,
        Physical = 1,
        _Count
    };

    enum class PartnerIntentType : std::uint8_t {
        None = 0,
        SuggestOpen = 1,
        SuggestClose = 2,
        ConfessTalking = 3,
        ConfessPhysical = 4,
        _Count
    };

    enum class ChildStage : std::uint8_t {
        Unborn = 0,
        Infant = 1,
        Child = 2,
        Training = 3,
        Adult = 4,
        _Count
    };

    enum class PhenotypeTier : std::uint8_t {
        PaleHuman = 0,
        Redguard = 1,
        Mer = 2,
        Orc = 3,
        Argonian = 4,
        Khajiit = 5,
        Unknown = 6
    };

    enum class ChildCustodyGenderPolicy : std::uint8_t {
        StayWithMother = 0,
        StayWithFather = 1
    };

    enum class ChildCustodyPlayerPolicy : std::uint8_t {
        UseGenderCustody = 0,
        AlwaysWithPlayer = 1,
        NeverWithPlayer = 2
    };

    enum class MatchmakingOrientation : std::uint8_t {
        Any = 0,
        Heterosexual = 1,
        Homosexual = 2,
        Gay = 3,
        Lesbian = 4,
        HeteroAndGay = 5,
        HeteroAndLesbian = 6
    };

    enum class ActorSex : std::int8_t {
        Unknown = -1,
        Male = 0,
        Female = 1
    };

    [[nodiscard]] constexpr bool IsMainTierStatus(RelationshipStatus status) noexcept {
        return status == RelationshipStatus::Dating ||
               status == RelationshipStatus::Engaged ||
               status == RelationshipStatus::Married;
    }

    [[nodiscard]] constexpr bool IsActivePartnerStatus(RelationshipStatus status) noexcept {
        return status == RelationshipStatus::Dating ||
               status == RelationshipStatus::Engaged ||
               status == RelationshipStatus::Married ||
               status == RelationshipStatus::Lover;
    }

    [[nodiscard]] constexpr bool IsBreakupStatus(RelationshipStatus status) noexcept {
        return status == RelationshipStatus::Divorced ||
               status == RelationshipStatus::Jilted ||
               status == RelationshipStatus::ExDating ||
               status == RelationshipStatus::ExLover;
    }

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
