#pragma once

#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/IRng.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldState.h"
#include "maras/state/WorldTables.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace MARAS::Domain::Agency {

    using Foundation::ActorId;
    using Foundation::RelationshipStatus;

    enum class BanterSceneKind : std::int32_t {
        None = 0,
        Lover = 1,
        Dating = 2,
        Engaged = 3,
        Married = 4,
        OpenRelationship = 5,
        CheatingSecret = 6,
        CheatingDiscovered = 7,
        CheatingCaught = 8,
    };

    struct CheatingSceneEnvironment {
        bool hasSexFramework{ false };
        bool isInterior{ false };
        bool hasBedsNearby{ false };
        bool allowCaught{ true };
    };

    struct BanterSceneSetupInput {
        ActorId npc1{};
        ActorId npc2{};
        std::string_view forcedScenario{};
        CheatingSceneEnvironment cheatingEnv{};
    };

    struct BanterSceneSetupPlan {
        bool valid{ false };
        BanterSceneKind sceneKind{ BanterSceneKind::None };
        std::int32_t sceneIdx{ -1 };
        ActorId npc1{};
        ActorId npc2{};
        ActorId npc1MainPartner{};
        ActorId npc2MainPartner{};
        bool isForced{ false };
        bool isCheating{ false };
        bool shouldSetupSexScene{ false };
        bool shouldExposeNpc1Affair{ false };
        bool needsPartnerMove{ false };
    };

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

    /**
     * Selects a cheating banter type (0=Secret, 1=Discovered, 2=Caught, or -1=None)
     * honoring MCM toggles, player-victim protection, and environment fallbacks.
     */
    [[nodiscard]] std::int32_t SelectCheatingSceneKindWithFallbacks(
        const State::WorldStateSnapshot& world,
        ActorId npc1,
        ActorId npc2,
        const CheatingSceneEnvironment& env,
        Foundation::IRng& rng);

    /**
     * Selects a scene variant index (0..3/4/5) for a cheating banter scene type.
     */
    [[nodiscard]] std::int32_t SelectCheatingSceneVariantIndex(
        const State::WorldStateSnapshot& world,
        ActorId npc1,
        ActorId npc2,
        std::int32_t cheatingType,
        std::int32_t initialSceneIdx,
        Foundation::IRng& rng);

    /**
     * Plans a banter scene setup (scene kind, variant index, swapped actor order,
     * primary partners, affair exposure, and partner relocation flags).
     */
    [[nodiscard]] BanterSceneSetupPlan PlanBanterSceneSetup(
        const State::WorldStateSnapshot& world,
        const BanterSceneSetupInput& input,
        Foundation::IRng& rng);

}  // namespace MARAS::Domain::Agency
