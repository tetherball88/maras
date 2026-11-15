#include "maras/domain/relationships/GiftAndIntimacyRules.h"

#include <algorithm>
#include <format>
#include <limits>

namespace MARAS::Domain::Relationships {

    bool HasTrackedPlayerStatus(
        const State::WorldStateSnapshot& snap,
        ActorId actor) noexcept {
        if (actor.IsNull() || actor.IsPlayer() || actor.IsUnknownFather() || actor.IsSyntheticChild()) {
            return false;
        }
        if (snap.relationships &&
            snap.relationships->GetStatus(actor, ActorId::Player()) != RelationshipStatus::Unknown) {
            return true;
        }
        return snap.actors && snap.actors->IsRegisteredAdult(Foundation::RefId{ actor.raw() });
    }

    GiftSessionOutcome EvaluateGiftSession(
        const State::WorldStateSnapshot& snap,
        ActorId receiver,
        std::string_view playerName,
        std::string_view receiverName,
        std::span<const GiftedItemEntry> items) {
        GiftSessionOutcome outcome{};
        if (!HasTrackedPlayerStatus(snap, receiver) || items.empty()) {
            return outcome;
        }

        std::int64_t totalGold = 0;
        for (const auto& item : items) {
            if (item.count <= 0 || item.itemName.empty()) {
                continue;
            }
            if (!outcome.formattedItems.empty()) {
                outcome.formattedItems.push_back(',');
            }
            outcome.formattedItems.append(item.itemName);
            if (item.count > 1) {
                outcome.formattedItems.append(std::format("({})", item.count));
            }
            const std::int64_t unitGold = std::max<std::int32_t>(0, item.unitGoldValue);
            totalGold += static_cast<std::int64_t>(item.count) * unitGold;
        }

        if (outcome.formattedItems.empty()) {
            return outcome;
        }

        constexpr auto kMaxGold = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max());
        outcome.totalGoldValue = static_cast<std::int32_t>(std::clamp<std::int64_t>(totalGold, 0, kMaxGold));
        outcome.valid = true;
        outcome.skyrimNetMessage = std::format(
            "{} gifted {} to {} of total value {}.",
            playerName,
            outcome.formattedItems,
            receiverName,
            outcome.totalGoldValue);
        return outcome;
    }

    IntimateThreadParticipantEvaluation EvaluateIntimateThreadParticipants(
        const State::WorldStateSnapshot& snap,
        std::span<const ActorId> participants,
        bool hasThreadCheatingOrOpenPotential) {
        IntimateThreadParticipantEvaluation eval{};
        bool hasPlayer = false;
        std::vector<ActorId> uniqueParticipants;
        uniqueParticipants.reserve(participants.size());

        for (const ActorId actor : participants) {
            if (actor.IsNull()) {
                continue;
            }
            if (actor.IsPlayer()) {
                hasPlayer = true;
            }
            if (std::find(uniqueParticipants.begin(), uniqueParticipants.end(), actor) ==
                uniqueParticipants.end()) {
                uniqueParticipants.push_back(actor);
            }
        }

        for (const ActorId actor : uniqueParticipants) {
            if (actor.IsPlayer()) {
                continue;
            }
            if (hasPlayer && HasTrackedPlayerStatus(snap, actor)) {
                eval.playerIntimacyPartners.push_back(actor);
            }
            if (hasThreadCheatingOrOpenPotential &&
                snap.relationships &&
                Foundation::IsMainTierStatus(snap.relationships->GetStatus(actor, ActorId::Player())) &&
                IsActorCheating(snap, actor) &&
                snap.affairs &&
                snap.affairs->HasCheatedOn(actor, ActorId::Player())) {
                eval.cheatersCaughtMidAct.push_back(actor);
            }
        }

        return eval;
    }

    IntimateThreadCompletionOutcome EvaluateIntimateThreadCompletion(
        const State::WorldStateSnapshot& snap,
        std::span<const ActorId> recordedPartners,
        bool hadSex) {
        IntimateThreadCompletionOutcome outcome{};
        outcome.baseAffection = hadSex ? 15.0f : 8.0f;

        for (const ActorId partner : recordedPartners) {
            if (!HasTrackedPlayerStatus(snap, partner)) {
                continue;
            }
            if (std::find(outcome.eligiblePartners.begin(), outcome.eligiblePartners.end(), partner) ==
                outcome.eligiblePartners.end()) {
                outcome.eligiblePartners.push_back(partner);
            }
        }

        return outcome;
    }

}  // namespace MARAS::Domain::Relationships
