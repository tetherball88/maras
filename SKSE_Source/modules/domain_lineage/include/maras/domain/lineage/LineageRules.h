#pragma once

#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/IRng.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"
#include "maras/state/WorldTables.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::Domain::Lineage {

    using Foundation::ActorId;
    using Foundation::BaseId;
    using Foundation::ChildId;
    using Foundation::HouseId;
    using Foundation::RefId;
    using Foundation::Temperament;

    inline constexpr std::uint32_t kAventusAretinoRefRawId = 0x0001B121u;
    inline constexpr std::uint32_t kAventusAretinoBaseRawId = 0x00014132u;
    inline constexpr float kCompanionRetryCooldownDays = 15.0f;

    struct ChildRegistrationInput {
        ChildId childId{};
        ActorId guardianA{};
        ActorId guardianB{};
        std::string name{};
        std::int32_t timestamp{ 0 };
        bool isAdopted{ true };
        ActorId bioMother{};
        ActorId bioFather{};
        bool motherIsWerewolf{ false };
        bool fatherIsWerewolf{ false };
    };

    struct ChildGuardianMutationOutcome {
        bool changed{ false };
        State::GuardianSetDiff guardianDiff{};
        std::vector<ActorId> affectedActors{};
        std::optional<State::ChildFactionSnapshotRecord> removedFactionSnapshot{};
    };

    struct ChildOrderOutcome {
        bool applied{ false };
        bool previousOrderClearedSameType{ false };
        bool expiredPrevious{ false };
        std::optional<float> orderToApply{};
        std::vector<ChildId> updatedChildren{};
    };

    struct SetBioFatherInput {
        ChildId childId{};
        ActorId fatherId{};
        bool motherIsWerewolf{ false };
        bool fatherIsWerewolf{ false };
        std::string fatherName{};
        State::FormRawId fatherRaceId{};
        std::string presumedFatherRaceEditorId{};
        std::string bioFatherRaceEditorId{};
        bool initializePartnerSecretIfDifferent{ false };
    };

    struct SetPresumedFatherInput {
        ChildId childId{};
        ActorId fatherId{};
        float currentGameDay{ 0.0f };
        std::string bioFatherName{};
        State::FormRawId bioFatherRaceId{};
        std::string presumedFatherRaceEditorId{};
        std::string bioFatherRaceEditorId{};
    };

    [[nodiscard]] bool IsChildBlockedFromAdoption(
        RefId childRefId,
        BaseId childBaseId,
        bool db01ActiveUncompleted,
        std::string* outReason = nullptr);

    void EvaluateChildBeastbloodInheritance(
        State::ChildRecord& record,
        bool motherIsWerewolf,
        bool fatherIsWerewolf,
        bool forceReevaluate = false) noexcept;

    [[nodiscard]] bool IsCompanionRetryCooldownElapsed(
        float storedTimestamp,
        float currentGameDays,
        float cooldownDays = kCompanionRetryCooldownDays) noexcept;

    [[nodiscard]] bool IsPetBannedForChild(
        const State::WorldStateSnapshot& snap,
        ChildId childId,
        float currentGameDays) noexcept;

    [[nodiscard]] bool IsCritterBannedForChild(
        const State::WorldStateSnapshot& snap,
        ChildId childId,
        float currentGameDays) noexcept;

    [[nodiscard]] float CalculateChildInteractionFamilyAffection(Temperament temperament) noexcept;

    ChildGuardianMutationOutcome ApplyRegisterChild(
        State::WorldState& ws,
        const ChildRegistrationInput& input,
        const State::IActorMetadataProvider& meta = State::DefaultActorMetadataProvider{});

    ChildGuardianMutationOutcome ApplyRemoveChild(
        State::WorldState& ws,
        ChildId childId);

    ChildGuardianMutationOutcome ApplySetChildGuardian(
        State::WorldState& ws,
        ChildId childId,
        ActorId guardianId,
        bool isAdd,
        const State::IActorMetadataProvider& meta = State::DefaultActorMetadataProvider{});

    ChildGuardianMutationOutcome ApplySetChildGuardiansPair(
        State::WorldState& ws,
        ChildId childId,
        ActorId guardianA,
        ActorId guardianB,
        const State::IActorMetadataProvider& meta = State::DefaultActorMetadataProvider{});

    bool ApplySetBioMother(
        State::WorldState& ws,
        ChildId childId,
        ActorId motherId,
        bool motherIsWerewolf,
        bool fatherIsWerewolf) noexcept;

    bool ApplySetBioFather(
        State::WorldState& ws,
        const SetBioFatherInput& input);

    bool ApplySetPresumedFather(
        State::WorldState& ws,
        const SetPresumedFatherInput& input);

    ChildOrderOutcome ApplyOrderIssue(
        State::WorldState& ws,
        ChildId childId,
        float orderValue,
        float durationGameDays,
        float currentGameDays);

    std::vector<ChildId> ApplyExpiredOrders(
        State::WorldState& ws,
        float currentGameDays);

    [[nodiscard]] std::vector<ChildId> SelectForcegreetChildrenByHouse(
        const State::WorldStateSnapshot& snap,
        HouseId houseId,
        bool onlyPhysicalActors = true) noexcept;

    [[nodiscard]] ChildId PickWeightedChild(
        const State::WorldStateSnapshot& snap,
        const std::vector<ChildId>& candidates,
        float currentGameDays,
        Foundation::IRng& rng) noexcept;

}  // namespace MARAS::Domain::Lineage
