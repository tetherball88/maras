#pragma once

#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldTables.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace MARAS::Domain::Lineage {

    using Foundation::ActorId;
    using Foundation::SkillType;
    using Foundation::SocialClass;

    enum class SkillIndex : std::uint8_t {
        OneHanded = 0,
        TwoHanded = 1,
        Archery = 2,
        Block = 3,
        Smithing = 4,
        HeavyArmor = 5,
        LightArmor = 6,
        Pickpocket = 7,
        Lockpicking = 8,
        Sneak = 9,
        Alchemy = 10,
        Speech = 11,
        Alteration = 12,
        Conjuration = 13,
        Destruction = 14,
        Illusion = 15,
        Restoration = 16,
        Enchanting = 17,
        _Count = 18
    };

    struct ParentLineageInput {
        ActorId actorId{};
        std::uint16_t level{ 1 };
        std::array<SkillIndex, 4> signatureSkills{};
    };

    [[nodiscard]] std::optional<SkillIndex> ParseSkillIndex(std::string_view rawSkillName);

    [[nodiscard]] std::array<SkillIndex, 4> GetSignatureSkillsForSkillType(SkillType skillType) noexcept;

    [[nodiscard]] std::array<SkillIndex, 4> ResolveSignatureSkillsFromArray(
        const std::array<float, 18>& baseSkills,
        SkillType fallbackSkillType) noexcept;

    [[nodiscard]] std::array<float, 3> GetHMSWeightsForSkillType(SkillType skillType) noexcept;

    [[nodiscard]] int GetSocialClassRankOrder(SocialClass socialClass) noexcept;

    [[nodiscard]] float NormalizeClassSkillWeight(float rawBonus) noexcept;

    [[nodiscard]] std::uint16_t ComputeEffectiveChildLevel(std::uint16_t playerLevel) noexcept;

    [[nodiscard]] std::uint8_t ComputeBioParentSkillBonus(std::uint16_t parentLevel) noexcept;

    [[nodiscard]] std::uint8_t ComputeGuardianSkillBonus(std::uint16_t guardianLevel) noexcept;

    void EvaluateChildBioLineageAtBirth(
        State::ChildRecord& record,
        const ParentLineageInput* bioMother,
        const ParentLineageInput* bioFather) noexcept;

    void EvaluateChildGuardianLineageAtGraduation(
        State::ChildRecord& record,
        const ParentLineageInput* bioMother,
        const ParentLineageInput* bioFather,
        std::span<const ParentLineageInput> guardians,
        std::span<const std::string> completedDaedricBlessingIds);

}  // namespace MARAS::Domain::Lineage
