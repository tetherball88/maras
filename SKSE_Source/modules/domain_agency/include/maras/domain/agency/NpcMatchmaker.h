#pragma once

#include "maras/domain/agency/MatchmakingRules.h"
#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/IRng.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace MARAS::Domain::Agency {

    enum class MatchmakingActionKind : std::uint8_t {
        New = 0,
        Advance = 1,
        Breakup = 2,
        OpenClose = 3
    };

    struct MatchmakingActionSlot {
        MatchmakingActionKind action{ MatchmakingActionKind::New };
        RelationshipStatus targetStatus{ RelationshipStatus::Dating };
        bool isCheating{ false };
        bool openTarget{ false };
    };

    struct MatchmakingProposal {
        MatchmakingActionSlot slot{};
        ActorId actorA{};
        ActorId actorB{};
        RelationshipStatus fromStatus{ RelationshipStatus::Candidate };
    };

    [[nodiscard]] std::vector<MatchmakingActionSlot> RollActionSlots(
        const State::SettingsData& settings,
        int maxSlots,
        Foundation::IRng& rng);

    [[nodiscard]] std::optional<MatchmakingProposal> FindEligiblePairForSlot(
        const State::WorldStateSnapshot& snap,
        const std::vector<NpcMatchmakingProfile>& npcs,
        const MatchmakingActionSlot& slot,
        float currentGameDay,
        Foundation::IRng& rng);

    [[nodiscard]] std::vector<MatchmakingProposal> GenerateDiceRollProposals(
        const State::WorldStateSnapshot& snap,
        const std::vector<NpcMatchmakingProfile>& npcs,
        float currentGameDay,
        Foundation::IRng& rng);

}  // namespace MARAS::Domain::Agency
