#include "maras/domain/relationships/NpcTypeRules.h"

#include <iterator>

namespace MARAS::Domain::Relationships {

    Temperament ComputeTemperament(SocialClass socialClass, SkillType skillType) noexcept {
        static constexpr Temperament kTemperamentMatrix[8][6] = {
            // Outcast (0)
            { Temperament::Independent, Temperament::Jealous,     Temperament::Jealous,     Temperament::Humble,      Temperament::Independent, Temperament::Romantic    },
            // Poverty (1)
            { Temperament::Humble,      Temperament::Romantic,    Temperament::Jealous,     Temperament::Independent, Temperament::Proud,       Temperament::Romantic    },
            // Working (2)
            { Temperament::Proud,       Temperament::Humble,      Temperament::Romantic,    Temperament::Independent, Temperament::Independent, Temperament::Proud       },
            // Middle (3)
            { Temperament::Proud,       Temperament::Romantic,    Temperament::Independent, Temperament::Romantic,    Temperament::Humble,      Temperament::Jealous     },
            // Wealthy (4)
            { Temperament::Proud,       Temperament::Jealous,     Temperament::Romantic,    Temperament::Romantic,    Temperament::Independent, Temperament::Jealous     },
            // Religious (5)
            { Temperament::Independent, Temperament::Humble,      Temperament::Romantic,    Temperament::Jealous,     Temperament::Humble,      Temperament::Proud       },
            // Nobles (6)
            { Temperament::Jealous,     Temperament::Romantic,    Temperament::Independent, Temperament::Proud,       Temperament::Humble,      Temperament::Proud       },
            // Rulers (7)
            { Temperament::Proud,       Temperament::Independent, Temperament::Humble,      Temperament::Romantic,    Temperament::Jealous,     Temperament::Independent }
        };

        const auto scIdx = static_cast<std::size_t>(socialClass);
        const auto stIdx = static_cast<std::size_t>(skillType);
        if (scIdx < std::size(kTemperamentMatrix) && stIdx < std::size(kTemperamentMatrix[0])) {
            return kTemperamentMatrix[scIdx][stIdx];
        }
        return Temperament::Independent;
    }

    SkillType ComputeSkillTypeFromSkills(const std::array<float, 18>& skills) noexcept {
        int maxIndex = 0;
        float maxVal = skills[0];
        for (int i = 1; i < 18; ++i) {
            if (skills[static_cast<std::size_t>(i)] > maxVal) {
                maxIndex = i;
                maxVal = skills[static_cast<std::size_t>(i)];
            }
        }

        if (maxIndex == 0 || maxIndex == 1 || maxIndex == 3 || maxIndex == 5) {
            return SkillType::Warrior;
        }
        if (maxIndex == 2) {
            return SkillType::Ranger;
        }
        if (maxIndex == 7 || maxIndex == 8 || maxIndex == 9) {
            return SkillType::Rogue;
        }
        if (maxIndex >= 12 && maxIndex <= 16) {
            return SkillType::Mage;
        }
        if (maxIndex == 11) {
            return SkillType::Orator;
        }
        if (maxIndex == 4 || maxIndex == 10 || maxIndex == 17) {
            return SkillType::Craftsman;
        }
        if (maxIndex == 6) {
            // Light Armor tiebreaker: Archery vs Pickpocket/Lockpicking
            if (skills[2] > skills[7] && skills[2] > skills[8]) {
                return SkillType::Ranger;
            }
            return SkillType::Rogue;
        }
        return SkillType::Warrior;
    }

}  // namespace MARAS::Domain::Relationships
