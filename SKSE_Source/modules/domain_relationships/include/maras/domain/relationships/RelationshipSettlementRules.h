#pragma once

namespace MARAS::Domain::Relationships {

    struct SpouseIncomeShareResult {
        bool awarded{ false };
        int goldAmount{ 0 };
        float updatedLastSharedDay{ -1.0f };
    };

    struct DivorceSettlementResult {
        bool onCooldown{ false };
        int playerWealthShare{ 0 };
        int assetsShare{ 0 };
        int totalFee{ 0 };
        bool canPayDirectly{ false };
    };

    /// Computes initial permanent affection floor when an actor first enters Candidate status.
    [[nodiscard]] int ComputeInitialCandidateAffection(int playerRelationshipRank) noexcept;

    /// Computes daily spouse income share (100 gold/day, clamped [0, 500]).
    [[nodiscard]] SpouseIncomeShareResult ComputeSpouseIncomeShare(
        float currentGameDay,
        float lastSharedDay) noexcept;

    /// Computes divorce settlement fee (100 base + 30% gold capped at 1000 + assets share) with a 7-day cooldown.
    [[nodiscard]] DivorceSettlementResult ComputeDivorceSettlement(
        float currentGameDay,
        float lastPaidDay,
        int playerGold,
        int housesOwned,
        int horsesOwned,
        int questsCompleted,
        int dungeonsCleared) noexcept;

}  // namespace MARAS::Domain::Relationships
