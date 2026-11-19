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
        ActorId actor,
        const State::IActorMetadataProvider* meta = nullptr,
        bool isEngineFamilyWithPlayer = false) noexcept;

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
        std::span<const GiftedItemEntry> items,
        const State::IActorMetadataProvider* meta = nullptr,
        bool isReceiverEngineFamilyWithPlayer = false);

    struct IntimateThreadParticipantEvaluation {
        std::vector<ActorId> playerIntimacyPartners{};
        std::vector<ActorId> cheatersCaughtMidAct{};

        bool operator==(const IntimateThreadParticipantEvaluation&) const = default;
    };

    [[nodiscard]] IntimateThreadParticipantEvaluation EvaluateIntimateThreadParticipants(
        const State::WorldStateSnapshot& snap,
        std::span<const ActorId> participants,
        bool hasThreadCheatingOrOpenPotential,
        std::span<const ActorId> engineFamilyWithPlayer = {},
        const State::IActorMetadataProvider* meta = nullptr);

    struct IntimateThreadCompletionOutcome {
        float baseAffection{ 8.0f };
        std::vector<ActorId> eligiblePartners{};

        bool operator==(const IntimateThreadCompletionOutcome&) const = default;
    };

    [[nodiscard]] IntimateThreadCompletionOutcome EvaluateIntimateThreadCompletion(
        const State::WorldStateSnapshot& snap,
        std::span<const ActorId> recordedPartners,
        bool hadSex,
        std::span<const ActorId> engineFamilyWithPlayer = {},
        const State::IActorMetadataProvider* meta = nullptr);

}  // namespace MARAS::Domain::Relationships
