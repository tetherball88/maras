#include <spdlog/spdlog.h>

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

#include "core/NPCRelationshipManager.h"
#include "core/SpouseBuffService.h"
#include "core/SpouseHierarchyManager.h"
#include "papyrus/PapyrusInterface.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"

namespace MARAS {

    float SpouseBuffService::GetSpouseMultiplier(RE::Actor* spouse) {
        if (!spouse) return 0.0f;

        // Get hierarchy rank (0 = lead, 1 = second, 2 = third, 3+ = other)
        int spouseRank = SpouseHierarchyManager::GetSingleton().GetRank(spouse->GetFormID());

        // Number of married spouses
        int spouseCount = static_cast<int>(NPCRelationshipManager::GetSingleton().GetMarriedCount());

        // Affection multiplier is currently provided by the Papyrus TTM_ServiceAffection; until
        // that is ported to native code we conservatively use 1.0 here. TODO: wire native affection.
        // TODO: use affection when implemented
        float affectionBuffMult = 1.0f;

        if (spouseCount == 1) {
            return 2.0f * affectionBuffMult;
        }

        if (spouseRank == 0) {
            return 1.0f * affectionBuffMult;
        } else if (spouseRank == 1) {
            return 0.5f * affectionBuffMult;
        } else if (spouseRank == 2) {
            return 0.25f * affectionBuffMult;
        } else {
            return 0.1f * affectionBuffMult;
        }
    }

    std::vector<float> SpouseBuffService::GetFollowersMultipliers(const std::vector<RE::Actor*>& followers) {
        // Six skill-type buckets (Warrior, Mage, Rogue, Craftsman, Ranger, Orator)
        std::vector<float> multipliers(static_cast<size_t>(MARAS::SkillType::Orator) + 1, 0.0f);

        for (auto actor : followers) {
            if (!actor) continue;

            // Only count followers who are spouses
            RE::FormID fid = actor->GetFormID();
            if (!NPCRelationshipManager::GetSingleton().IsMarried(fid)) continue;

            // Determine skill type for this NPC
            auto skill = NPCRelationshipManager::GetSingleton().GetSkillType(fid);
            int skillIndex = static_cast<int>(skill);
            if (skillIndex < 0 || skillIndex >= static_cast<int>(multipliers.size())) {
                MARAS_LOG_DEBUG("GetFollowersMultipliers: invalid skill index {} for {}", skillIndex, fid);
                continue;
            }

            // TODO add affection multiplier when implemented
            float mult = 1.0f;
            multipliers[static_cast<size_t>(skillIndex)] += mult;
        }

        return multipliers;
    }

    std::vector<float> SpouseBuffService::GetPermanentMultipliers() {
        // Social-class buckets (8 classes)
        std::vector<float> multipliers(8, 0.0f);

        // Retrieve all married spouse actor references via PapyrusInterface helper
        auto spouses = MARAS::PapyrusInterface::GetNPCsByStatus(nullptr, "married");

        for (auto spouse : spouses) {
            if (!spouse) continue;

            // Use NPCRelationshipManager to obtain the social-class enum instead of
            int socialIndex =
                static_cast<int>(NPCRelationshipManager::GetSingleton().GetSocialClass(spouse->GetFormID()));
            if (socialIndex < 0 || socialIndex >= static_cast<int>(multipliers.size())) {
                MARAS_LOG_DEBUG("GetPermanentMultipliers: spouse {} has invalid social index {}", spouse->GetFormID(),
                                socialIndex);
                continue;
            }

            float mult = GetSpouseMultiplier(spouse);
            multipliers[static_cast<size_t>(socialIndex)] += mult;
        }

        return multipliers;
    }

}  // namespace MARAS
