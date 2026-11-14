#pragma once

#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/IRng.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"
#include "maras/state/WorldTables.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::Domain::Lineage {

    using Foundation::ActorId;
    using Foundation::ChildId;
    using Foundation::FormRawId;
    using Foundation::PhenotypeTier;

    struct UnbornChildRegistrationInput {
        ActorId bioMother{};
        ActorId bioFather{};
        std::string bioFatherName{};
        FormRawId bioFatherRaceId{};
        std::string_view motherRaceEditorId{};
        std::string_view presumedFatherRaceEditorId{};
        std::string_view bioFatherRaceEditorId{};
        bool motherIsWerewolf{ false };
        bool bioFatherIsWerewolf{ false };
        float conceptionTimestamp{ 0.0f };
    };

    struct UnbornChildRegistrationOutcome {
        ChildId childId{};
        ActorId presumedFather{};
        bool hasSecret{ false };
        bool hasPhenotypeMismatch{ false };
        std::vector<ActorId> dirtyActors{};
    };

    struct PromoteUnbornOutcome {
        ChildId childId{};
        bool promoted{ false };
        bool evaluateAtBirth{ false };
        ActorId bioFather{};
        ActorId presumedFather{};
        bool isSecretForPartner{ false };
        bool isSecretForBioFather{ false };
        std::vector<ActorId> dirtyActors{};
        std::vector<ChildId> dirtyChildren{};
    };

    struct PaternityRevealOutcome {
        bool updated{ false };
        std::string modEventOutcome{};
        std::string memoryOutcome{};
        bool learnedPartner{ false };
        bool learnedBioFather{ false };
        bool revealedPregnancyOnly{ false };
        bool wasPregnancySecretForPartner{ false };
        std::vector<ChildId> affectedChildren{};
    };

    struct PaternityFallbackOutcome {
        bool evaluated{ false };
        bool revealed{ false };
        bool brokeUp{ false };
        std::string modEventOutcome{};
        ActorId motherId{};
        ActorId partnerId{};
        ActorId bioFatherId{};
        PaternityRevealOutcome revealOutcome{};
        State::WorldMutationResult relationshipMutation{};
    };

    [[nodiscard]] PhenotypeTier ClassifyPhenotypeTierFromEditorID(std::string_view raceEditorId) noexcept;

    [[nodiscard]] bool HasPhenotypeMismatch(
        PhenotypeTier fatherA,
        PhenotypeTier fatherB) noexcept;

    [[nodiscard]] bool HasPhenotypeMismatch(
        std::string_view raceEditorA,
        std::string_view raceEditorB) noexcept;

    [[nodiscard]] std::string GetVisualDiscrepancyDescription(
        PhenotypeTier bioTier,
        PhenotypeTier presumedTier,
        std::string_view subjectName);

    [[nodiscard]] std::optional<State::PaternitySecretState> FindChildPaternitySecret(
        const State::WorldStateSnapshot& snap,
        ChildId childId) noexcept;

    UnbornChildRegistrationOutcome ApplyRegisterUnbornChild(
        State::WorldState& ws,
        const UnbornChildRegistrationInput& input,
        const State::IActorMetadataProvider& meta);

    PromoteUnbornOutcome ApplyPromoteUnbornToInfant(
        State::WorldState& ws,
        ActorId motherId,
        float birthTime,
        std::string_view name,
        ActorId bioFatherId,
        bool motherIsWerewolf,
        bool fatherIsWerewolf,
        const State::IActorMetadataProvider& meta,
        Foundation::IRng& rng);

    std::vector<ChildId> ApplyRemoveUnbornChild(
        State::WorldState& ws,
        ActorId motherId,
        ActorId bioFatherId,
        std::vector<ActorId>* outAffectedGuardians = nullptr);

    PaternityRevealOutcome ApplyResolvePaternitySecret(
        State::WorldState& ws,
        ChildId childId,
        std::string_view trigger);

    PaternityRevealOutcome ApplyResolvePaternitySecretForPartner(
        State::WorldState& ws,
        ChildId childId,
        std::string_view trigger);

    PaternityRevealOutcome ApplyRevealPregnancyToPartner(
        State::WorldState& ws,
        ChildId childId,
        std::string_view trigger);

    PaternityRevealOutcome ApplyResolvePaternitySecretForBioFather(
        State::WorldState& ws,
        ChildId childId,
        std::string_view trigger);

    PaternityRevealOutcome ApplySetPaternitySecretEdges(
        State::WorldState& ws,
        ChildId childId,
        std::optional<bool> secretForBioFather,
        std::optional<bool> secretForPartner,
        std::optional<bool> pregnancySecretForPartner,
        bool bypassCooldown = false);

    [[nodiscard]] std::vector<ActorId> CollectBioFatherStepInCandidates(
        const State::WorldStateSnapshot& snap,
        ActorId motherId,
        const State::IActorMetadataProvider& meta);

    [[nodiscard]] bool EvaluatePaternitySecretDailyRoll(
        const State::ChildRecord& child,
        const State::SettingsData& settings,
        float currentGameDay,
        Foundation::IRng& rng) noexcept;

    PaternityFallbackOutcome ApplyPaternitySecretFallback(
        State::WorldState& ws,
        ChildId childId,
        float currentGameDay,
        const State::IActorMetadataProvider& meta,
        Foundation::IRng& rng);

}  // namespace MARAS::Domain::Lineage
