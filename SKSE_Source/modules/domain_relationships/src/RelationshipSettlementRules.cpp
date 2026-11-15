#include "maras/domain/relationships/RelationshipSettlementRules.h"

#include <algorithm>

namespace MARAS::Domain::Relationships {

    int ComputeInitialCandidateAffection(int playerRelationshipRank) noexcept {
        return std::clamp(50 + playerRelationshipRank * 10, 0, 100);
    }

    SpouseIncomeShareResult ComputeSpouseIncomeShare(
        float currentGameDay,
        float lastSharedDay) noexcept {
        const float diff = (lastSharedDay < 0.0f) ? 1.0f : (currentGameDay - lastSharedDay);
        if (diff < 1.0f) {
            return SpouseIncomeShareResult{
                .awarded = false,
                .goldAmount = 0,
                .updatedLastSharedDay = lastSharedDay,
            };
        }

        const int diffDays = static_cast<int>(diff);
        const int gold = std::clamp(diffDays * 100, 0, 500);
        return SpouseIncomeShareResult{
            .awarded = true,
            .goldAmount = gold,
            .updatedLastSharedDay = currentGameDay,
        };
    }

    DivorceSettlementResult ComputeDivorceSettlement(
        float currentGameDay,
        float lastPaidDay,
        int playerGold,
        int housesOwned,
        int horsesOwned,
        int questsCompleted,
        int dungeonsCleared) noexcept {
        if (lastPaidDay >= 0.0f && (currentGameDay - lastPaidDay) < 7.0f) {
            return DivorceSettlementResult{ .onCooldown = true };
        }

        const int clampedGold = std::max(0, playerGold);
        const int playerWealthShare = std::clamp(
            static_cast<int>(static_cast<double>(clampedGold) * 0.3),
            0,
            1000);
        const int housesComp = std::max(0, housesOwned) * 700;
        const int horsesComp = std::max(0, horsesOwned) * 500;
        const int questsComp = std::clamp(std::max(0, questsCompleted) * 100, 0, 1000);
        const int dungeonsComp = std::clamp(std::max(0, dungeonsCleared) * 100, 0, 1000);
        const int assetsShare = housesComp + horsesComp + questsComp + dungeonsComp;
        const int totalFee = 100 + playerWealthShare + assetsShare;

        return DivorceSettlementResult{
            .onCooldown = false,
            .playerWealthShare = playerWealthShare,
            .assetsShare = assetsShare,
            .totalFee = totalFee,
            .canPayDirectly = (clampedGold >= totalFee),
        };
    }

}  // namespace MARAS::Domain::Relationships
