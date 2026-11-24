#pragma once

#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/IRng.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldState.h"
#include "maras/state/WorldTables.h"

#include <optional>
#include <string>
#include <vector>

namespace MARAS::Domain::Agency {

    using Foundation::ActorId;
    using Foundation::ActorSex;
    using Foundation::AffairStage;
    using Foundation::LocationId;
    using Foundation::PartnerIntentType;
    using Foundation::RelationshipStatus;
    using Foundation::Temperament;

    enum class PartnerAgencyActionType : std::uint8_t {
        None = 0,
        ExpiredIntentConfessEscalatePhysical,
        ExpiredIntentForceSuggestOpenAffair,
        ExpiredIntentSuggestClosePenalty,
        EscalateTalkingToPhysical,
        BreakupClosedAffairLover,
        ConfessAffairIntent,
        BreakupOpenLover,
        SuggestOpenIntent,
        SuggestCloseIntent,
        SeekOpenLover,
        SeekSecretAffair
    };

    struct PartnerAgencyInput {
        ActorId partnerId{};
        std::string partnerName{};
        ActorSex partnerSex{ ActorSex::Unknown };
        bool isFollowerOrNearPlayer{ false };
        bool isPregnantWithPlayerChild{ false };
    };

    struct PartnerAgencyActionPlan {
        PartnerAgencyActionType type{ PartnerAgencyActionType::None };
        ActorId partnerId{};
        ActorId relatedLoverId{};
        float rolledChance{ 0.0f };
        int permanentAffectionDelta{ 0 };
        bool triggerConfessIntent{ false };
        bool forceCheatingFromExpiredSuggestOpen{ false };
        bool escalateTalkingToPhysical{ false };
    };

    enum class HomeArrivalKind : std::uint8_t {
        None = 0,
        PendingIntentForcegreet = 1,
        OpenDateInHouse = 2,
        OpenDateLetter = 3,
        CheatingTalkingLetter = 4,
        CheatingPhysicalLetter = 5,
        CheatingTalkingInHouse = 6,
        CheatingPhysicalInHouse = 7
    };

    struct HomeArrivalPartnerCandidate {
        ActorId partnerId{};
        LocationId nativeHomeLocationId{};
        bool residesAtPlayerLocation{ false };
        bool isPartnerInCurrentCell{ false };
        bool isBusyInScene{ false };
        std::vector<ActorId> ineligibleSideLovers{};
    };

    struct HomeArrivalPlan {
        HomeArrivalKind kind{ HomeArrivalKind::None };
        ActorId partnerId{};
        ActorId relatedLoverId{};
        PartnerIntentType intentType{ PartnerIntentType::None };
        Temperament partnerTemperament{ Temperament::Proud };
    };

    [[nodiscard]] bool IsPartnerAgencyCheatingAllowedByImmunities(
        const State::WorldStateSnapshot& snap,
        const PartnerAgencyInput& input,
        float currentGameDay,
        bool forceBypassFromExpiredSuggestOpen = false) noexcept;

    [[nodiscard]] float ComputePartnerCheatingChance(
        const State::WorldStateSnapshot& snap,
        const PartnerAgencyInput& input,
        float currentGameDay,
        int secretLoversCount,
        int sharedChildrenCount,
        bool playerWasCheatingRecently,
        int partnerHierarchyRank) noexcept;

    [[nodiscard]] std::vector<PartnerAgencyActionPlan> EvaluatePartnerAgencyActions(
        const State::WorldStateSnapshot& snap,
        const std::vector<PartnerAgencyInput>& partners,
        float currentGameDay,
        Foundation::IRng& rng);

    [[nodiscard]] std::optional<HomeArrivalPlan> EvaluatePartnerHomeArrival(
        const State::WorldStateSnapshot& snap,
        const State::SpatialActorSnapshot& spatial,
        LocationId playerLocationId,
        bool playerHomeHasTavernMarker,
        int currentHourOfDay,
        float currentGameDay,
        Foundation::IRng& rng,
        const std::vector<HomeArrivalPartnerCandidate>& partnerCandidates = {});

}  // namespace MARAS::Domain::Agency
