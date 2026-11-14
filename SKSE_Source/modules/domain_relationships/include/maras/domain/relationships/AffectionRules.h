#pragma once

#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MARAS::Domain::Relationships {

    using Foundation::ActorId;

    struct DailyAffectionChange {
        ActorId actorId{};
        int oldScore{ 0 };
        int newScore{ 0 };
        int unclampedScore{ 0 };
        std::string_view oldThreshold{};
        std::string_view newThreshold{};
        bool marriedSpouseChanged{ false };
    };

    [[nodiscard]] std::string NormalizeAffectionType(std::string_view rawType);

    [[nodiscard]] std::string_view GetAffectionThreshold(int affectionValue) noexcept;

    [[nodiscard]] int CalculateTotalDailyDelta(
        const State::AffectionTable& table,
        const std::unordered_map<std::string, float>& dailyByType);

    [[nodiscard]] float CalculateLonelinessDecay(
        const State::WorldStateSnapshot& snap,
        ActorId npcId,
        bool isFollowing,
        std::size_t activePartnerCount);

    [[nodiscard]] std::vector<ActorId> CollectUniquePlayerActivePartners(
        const State::WorldStateSnapshot& snap);

    [[nodiscard]] std::vector<DailyAffectionChange> ApplyDailyAffections(
        State::WorldState& ws,
        const State::IActorMetadataProvider& meta);

}  // namespace MARAS::Domain::Relationships
