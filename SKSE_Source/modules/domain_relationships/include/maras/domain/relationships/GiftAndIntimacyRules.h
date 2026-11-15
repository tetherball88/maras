#pragma once

#include "maras/domain/relationships/InfidelityRules.h"
#include "maras/state/WorldState.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::Domain::Relationships {

    /**
     * Pure equivalent of `MARAS.IsNPCStatus(actor, "any")`:
     * returns true for living/tracked adults that either have a non-Unknown
     * relationship edge with the Player or are registered adults (which
     * resolve to effective Candidate status toward the Player).
     */
    [[nodiscard]] bool HasTrackedPlayerStatus(
        const State::WorldStateSnapshot& snap,
        ActorId actor) noexcept;

    struct GiftedItemEntry {
        std::string itemName{};
        std::int32_t count{ 0 };
        std::int32_t unitGoldValue{ 0 };

        bool operator==(const GiftedItemEntry&) const = default;
    };

    struct GiftSessionOutcome {
        bool valid{ false };
        std::int32_t totalGoldValue{ 0 };
        std::string formattedItems{};
        std::string skyrimNetMessage{};

        bool operator==(const GiftSessionOutcome&) const = default;
    };

    [[nodiscard]] GiftSessionOutcome EvaluateGiftSession(
        const State::WorldStateSnapshot& snap,
        ActorId receiver,
        std::string_view playerName,
        std::string_view receiverName,
        std::span<const GiftedItemEntry> items);

    struct IntimateThreadParticipantEvaluation {
        std::vector<ActorId> playerIntimacyPartners{};
        std::vector<ActorId> cheatersCaughtMidAct{};

        bool operator==(const IntimateThreadParticipantEvaluation&) const = default;
    };

    [[nodiscard]] IntimateThreadParticipantEvaluation EvaluateIntimateThreadParticipants(
        const State::WorldStateSnapshot& snap,
        std::span<const ActorId> participants,
        bool hasThreadCheatingOrOpenPotential);

    struct IntimateThreadCompletionOutcome {
        float baseAffection{ 8.0f };
        std::vector<ActorId> eligiblePartners{};

        bool operator==(const IntimateThreadCompletionOutcome&) const = default;
    };

    [[nodiscard]] IntimateThreadCompletionOutcome EvaluateIntimateThreadCompletion(
        const State::WorldStateSnapshot& snap,
        std::span<const ActorId> recordedPartners,
        bool hadSex);

}  // namespace MARAS::Domain::Relationships
