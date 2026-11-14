#pragma once

#include "maras/foundation/DomainEnums.h"

#include <array>

namespace MARAS::Domain::Relationships {

    using Foundation::SkillType;
    using Foundation::SocialClass;
    using Foundation::Temperament;

    /// Computes canonical NPC temperament from SocialClass x SkillType matrix.
    [[nodiscard]] Temperament ComputeTemperament(SocialClass socialClass, SkillType skillType) noexcept;

    /// Computes SkillType from the 18 standard Skyrim actor skill values
    /// (OneHanded..Enchanting in fixed index order 0..17).
    [[nodiscard]] SkillType ComputeSkillTypeFromSkills(const std::array<float, 18>& skills) noexcept;

}  // namespace MARAS::Domain::Relationships
