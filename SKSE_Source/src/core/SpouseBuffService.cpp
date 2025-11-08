#include "core/SpouseBuffService.h"

#include <array>

#include "core/AffectionService.h"
#include "core/NPCRelationshipManager.h"
#include "core/SpouseHierarchyManager.h"
#include "papyrus/PapyrusInterface.h"
#include "utils/EnumUtils.h"

namespace MARAS {

    namespace {
        // Multiplier configuration
        constexpr float kSingleSpouseMultiplier = 2.0f;          // When only one spouse exists
        constexpr std::array<float, 3> kRankMultipliers{1.0f,    // Lead (rank 0)
                                                        0.5f,    // Second (rank 1)
                                                        0.25f};  // Third (rank 2)
        constexpr float kOtherSpouseMultiplier = 0.1f;           // Rank 3+

        // Number of social classes aggregated for permanent multipliers.
        // Keep in sync with NPCRelationshipManager::SocialClass enum.
        constexpr std::size_t kSocialClassCount = 8;

        inline float ComputeRankedSpouseMultiplier(int spouseRank, int spouseCount) {
            if (spouseCount == 1) {
                return kSingleSpouseMultiplier;
            }

            if (spouseRank >= 0 && spouseRank < static_cast<int>(kRankMultipliers.size())) {
                return kRankMultipliers[static_cast<std::size_t>(spouseRank)];
            }

            return kOtherSpouseMultiplier;
        }

        inline bool IndexInBounds(std::size_t index, std::size_t size) { return index < size; }
    }  // namespace

    float SpouseBuffService::GetSpouseMultiplier(const RE::Actor* spouse) {
        if (!spouse) return 0.0f;

        const RE::FormID fid = spouse->GetFormID();

        // Pull singletons once for clarity and potential micro-optimizations
        const auto& hierarchy = SpouseHierarchyManager::GetSingleton();
        const auto& relationships = NPCRelationshipManager::GetSingleton();
        const auto& affection = MARAS::AffectionService::GetSingleton();

        // Get hierarchy rank (0 = lead, 1 = second, 2 = third, 3+ = other)
        const int spouseRank = hierarchy.GetRank(fid);

        // Number of married spouses
        const int spouseCount = static_cast<int>(relationships.GetMarriedCount());

        // Affection multiplier: use native AffectionService permanent affection thresholds
        const float affectionBuffMult = affection.GetMultiplierForNPC(fid);

        return ComputeRankedSpouseMultiplier(spouseRank, spouseCount) * affectionBuffMult;
    }

    std::vector<float> SpouseBuffService::GetFollowersMultipliers(const std::vector<RE::Actor*>& followers) {
        // Six skill-type buckets (Warrior, Mage, Rogue, Craftsman, Ranger, Orator)
        std::vector<float> multipliers(static_cast<size_t>(MARAS::SkillType::Orator) + 1, 0.0f);

        const auto& relationships = NPCRelationshipManager::GetSingleton();
        const auto& affection = MARAS::AffectionService::GetSingleton();

        for (const auto* actor : followers) {
            if (!actor) continue;

            // Only count followers who are spouses
            const RE::FormID fid = actor->GetFormID();
            if (!relationships.IsMarried(fid)) continue;

            // Determine skill type for this NPC
            const auto skill = relationships.GetSkillType(fid);
            const std::size_t idx = static_cast<std::size_t>(skill);
            if (!IndexInBounds(idx, multipliers.size())) {
                MARAS_LOG_DEBUG("GetFollowersMultipliers: invalid skill index {} for {}", idx, fid);
                continue;
            }

            // Add affection multiplier from native AffectionService
            const float affectionMultFollower = affection.GetMultiplierForNPC(fid);

            multipliers[idx] += affectionMultFollower;
        }

        return multipliers;
    }

    std::vector<float> SpouseBuffService::GetPermanentMultipliers() {
        // Social-class buckets
        std::vector<float> multipliers(kSocialClassCount, 0.0f);

        // Retrieve all married spouse actor references via PapyrusInterface helper
        const auto spouses = MARAS::PapyrusInterface::GetNPCsByStatus(nullptr, "married");

        const auto& relationships = NPCRelationshipManager::GetSingleton();

        for (const auto* spouse : spouses) {
            if (!spouse) continue;

            // Use NPCRelationshipManager to obtain the social-class enum reliably
            const RE::FormID fid = spouse->GetFormID();
            const std::size_t socialIndex = static_cast<std::size_t>(relationships.GetSocialClass(fid));
            if (!IndexInBounds(socialIndex, multipliers.size())) {
                MARAS_LOG_DEBUG("GetPermanentMultipliers: spouse {} has invalid social index {}", fid, socialIndex);
                continue;
            }

            const float mult = GetSpouseMultiplier(spouse);
            multipliers[socialIndex] += mult;
        }

        return multipliers;
    }

}  // namespace MARAS
