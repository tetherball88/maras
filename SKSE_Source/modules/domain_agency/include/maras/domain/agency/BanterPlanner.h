#pragma once

#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/IRng.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldState.h"
#include "maras/state/WorldTables.h"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace MARAS::Domain::Agency {

    using Foundation::ActorId;
    using Foundation::RelationshipStatus;

    struct BanterCandidate {
        ActorId actorA{};
        ActorId actorB{};
        int tier{ 1 };  // 1 = both loaded nearby, 2 = one nearby + absent living partner
        float lastPlayedDay{ -1.0f };
        bool isOpen{ false };
        bool isCheating{ false };
        bool isDiscovered{ false };
        bool isCaught{ false };
    };

    [[nodiscard]] bool IsBanterQualifyingStatus(RelationshipStatus status) noexcept;

    [[nodiscard]] bool IsLocationKeywordAllowedForScenes(
        const State::SettingsData& settings,
        std::string_view keywordEditorId) noexcept;

    [[nodiscard]] bool AreAllLocationKeywordsAllowedForScenes(
        const State::SettingsData& settings,
        std::span<const std::string_view> keywordEditorIds) noexcept;

    [[nodiscard]] bool IsCoupleOnBanterCooldown(
        const State::WorldStateSnapshot& world,
        ActorId actorA,
        ActorId actorB,
        float currentGameDay) noexcept;

    [[nodiscard]] std::vector<BanterCandidate> FindBanterCoupleCandidates(
        const State::WorldStateSnapshot& world,
        const State::SpatialActorSnapshot& spatial,
        float scanRadius,
        float currentGameDay);

    [[nodiscard]] std::optional<BanterCandidate> SelectPrioritizedCouple(
        const std::vector<BanterCandidate>& candidates,
        Foundation::IRng& rng);

}  // namespace MARAS::Domain::Agency
