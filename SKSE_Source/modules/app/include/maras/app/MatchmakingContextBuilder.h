#pragma once

#include "RE/Skyrim.h"
#include "maras/app/ModalTypes.h"
#include "maras/domain/agency/MatchmakingRules.h"
#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/IRng.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace MARAS::App {

    struct ActorLocationContext {
        std::unordered_set<RE::FormID> activeCells{};
        RE::FormID activeCellID{ 0 };
        std::string activeCellName{};
        RE::FormID subLocationID{ 0 };
        std::string subLocationName{};
        RE::FormID settlementLocationID{ 0 };
        std::string settlementName{};
        RE::FormID holdLocationID{ 0 };
        std::string holdName{};
        RE::FormID playerHouseID{ 0 };
        RE::FormID crimeFactionID{ 0 };
        std::string locationName{};
    };

    struct TavernRendezvousTarget {
        RE::FormID innLocationID{ 0 };
        RE::FormID centerMarkerID{ 0 };
        std::string displayName{};
        bool found{ false };
    };

    struct ScoredAffairCandidate {
        RE::FormID formID{ 0 };
        std::uint64_t uuid{ 0 };
        std::string name{};
        std::string race{};
        std::string sex{};
        std::string socialClass{};
        std::string temperament{};
        std::string eligibility{};
        std::string locationName{};
        std::string connectionTag{};
        int tier{ 5 };
        int score{ 0 };
        int jitteredScore{ 0 };
        std::string reasonsSummary{};
    };

    struct PartnerAgencyContextInput {
        RE::FormID partnerID{ 0 };
        std::string partnerName{};
        Foundation::RelationshipStatus statusWithPlayer{ Foundation::RelationshipStatus::Married };
        int affection{ 50 };
        float daysSinceInteraction{ 0.0f };
        int hierarchyRank{ -1 };
        std::size_t sharedChildrenCount{ 0 };
        bool isOpenRelationshipLover{ false };
        float finalChance{ 0.0f };
        std::string driversSummary{};
        ActorLocationContext locCtx{};
        std::vector<ScoredAffairCandidate> shortlist{};
        std::string evalMode{ "candidate_select" };
        RE::FormID sideLoverID{ 0 };
        std::string sideLoverName{};
        Foundation::AffairStage sideAffairStage{ Foundation::AffairStage::Physical };
        float sideLoverAgeDays{ 0.0f };
        bool canConfess{ false };
    };

    // -------------------------------------------------------------------------
    // Shared Actor / Location / Cheating Helpers
    // -------------------------------------------------------------------------

    [[nodiscard]] std::string ResolveActorNameForAgency(RE::FormID id);
    [[nodiscard]] Foundation::ActorSex ResolveActorSexForAgency(RE::FormID id) noexcept;
    [[nodiscard]] bool IsNpcEligibleWorldAdult(RE::FormID id, const State::WorldStateSnapshot& snap);
    [[nodiscard]] bool IsPairCheatingTransition(const State::WorldStateSnapshot& snap,
                                                RE::FormID actorA,
                                                RE::FormID actorB,
                                                std::string_view targetStatus);

    [[nodiscard]] bool IsSettlementLevelKeyword(RE::BGSLocation* loc);
    [[nodiscard]] bool IsLocationOrParentMatch(RE::BGSLocation* loc, RE::FormID targetLocID);
    [[nodiscard]] ActorLocationContext ResolveActorLocationContext(RE::FormID actorFormID,
                                                                   const State::WorldStateSnapshot& snap);
    [[nodiscard]] TavernRendezvousTarget FindNearbyTavernForRendezvous(RE::BGSLocation* homeLoc,
                                                                       const ActorLocationContext& loverLocCtx);

    // -------------------------------------------------------------------------
    // World Adult Roster & Paramour Shortlisting
    // -------------------------------------------------------------------------

    [[nodiscard]] std::vector<Domain::Agency::NpcMatchmakingProfile> CollectWorldAdultMatchmakingProfiles(
        const State::WorldStateSnapshot& snap,
        float currentGameDay,
        bool requireEligibleForScheduled);

    [[nodiscard]] std::vector<ScoredAffairCandidate> CollectShortlistedParamours(
        const State::WorldStateSnapshot& snap,
        Foundation::ActorId partnerId,
        bool isOpenRelationshipLover,
        const ActorLocationContext& partnerLocCtx,
        float currentGameDay,
        Foundation::IRng& rng);

    // -------------------------------------------------------------------------
    // LLM Prompt Context JSON Builders
    // -------------------------------------------------------------------------

    [[nodiscard]] std::string BuildExistingPairsString(const State::WorldStateSnapshot& snap);
    [[nodiscard]] std::string BuildActorDetailedRelationshipsString(const State::WorldStateSnapshot& snap,
                                                                    RE::FormID actorFormID);

    [[nodiscard]] nlohmann::json BuildScheduledMatchmakingContextJson(
        const State::WorldStateSnapshot& snap,
        float currentGameDay,
        std::optional<std::size_t> maxSuggestionsOverride = std::nullopt);

    [[nodiscard]] nlohmann::json BuildManualMatchmakingContextJson(
        const State::WorldStateSnapshot& snap,
        RE::FormID targetNpcFormID,
        float currentGameDay);

    [[nodiscard]] nlohmann::json BuildVerifyContextJson(
        const MatchmakingResult& cand,
        const State::WorldStateSnapshot& snap,
        float currentGameDay);

    [[nodiscard]] nlohmann::json BuildPartnerAgencyContextJson(
        const State::WorldStateSnapshot& snap,
        const PartnerAgencyContextInput& input,
        float currentGameDay);

}  // namespace MARAS::App
