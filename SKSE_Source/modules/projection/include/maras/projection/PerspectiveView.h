#pragma once

#include "maras/foundation/StrongIds.h"
#include "maras/state/DomainTypes.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldState.h"
#include "maras/state/WorldTables.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MARAS::Projection {

    /**
     * Optional pure adapter port used by PerspectiveView when an unloaded adult actor's
     * werewolf status cannot be derived from the 2 Hz SpatialActorSnapshot or LineageTable.
     */
    class IWerewolfStateProvider {
    public:
        virtual ~IWerewolfStateProvider() = default;

        [[nodiscard]] virtual std::optional<bool> IsAdultWerewolf(State::ActorId actor) const noexcept {
            (void)actor;
            return std::nullopt;
        }
    };

    enum class ViewerMode : std::uint8_t {
        ActorSelf = 0,
        DialogueTargetPublic = 1,
        DashboardIntrigue = 2,
        Omniscient = 3
    };

    struct PerceivedPaternitySecret {
        bool isSecret{ false };
        bool isSecretForPartner{ false };
        bool isSecretForBioFather{ false };
        bool isPregnancySecretForPartner{ false };
        bool discovered{ false };
        bool discoveredByPartner{ false };
        bool discoveredByBioFather{ false };
        bool hasPhenotypeMismatch{ false };
        State::ActorId motherPartner{};
        State::ActorId presumedFather{};
        bool presumedFatherIsGuardian{ false };
        State::ActorId trueBioFather{};
        std::string trueBioFatherFallbackName{};
        State::FormRawId trueBioFatherRaceId{};

        bool operator==(const PerceivedPaternitySecret&) const = default;
    };

    struct PerceivedChildLineage {
        State::ChildId childId{};
        std::string name{};
        std::int32_t timestamp{ 0 };
        State::ChildStage stage{ State::ChildStage::Child };
        std::uint8_t gender{ 2 };
        bool isAdopted{ true };
        bool isBiologicalChild{ false };
        float conceptionTimestamp{ 0.0f };
        float birthTimestamp{ 0.0f };

        State::GuardianSlots guardians{};
        State::ActorId bioMother{};
        State::ActorId perceivedBioFather{};
        std::string perceivedBioFatherFallbackName{};
        State::ActorId publicBioFather{};
        State::ActorId motherPartner{};
        State::ActorId presumedFather{};

        std::vector<State::ActorId> biologicalParents{};
        std::vector<State::ActorId> stepParents{};

        bool hasUndiscoveredPaternitySecret{ false };
        bool hideSecretFromViewer{ false };
        bool hasBeastblood{ false };

        std::string trainingLocation{};
        std::int32_t trainingClassIndex{ -1 };
        float trainingStartGameDay{ 0.0f };
        std::string savedBioTemplateName{};

        std::optional<PerceivedPaternitySecret> paternitySecret{};

        [[nodiscard]] bool IsActorPerceivedBioParent(State::ActorId actor) const noexcept;
        [[nodiscard]] bool IsActorPerceivedStepParent(State::ActorId actor) const noexcept;
        [[nodiscard]] State::ActorId PerceivedOtherParent(State::ActorId parent) const noexcept;

        bool operator==(const PerceivedChildLineage&) const = default;
    };

    struct PerceivedPregnancyGroup {
        State::ActorId perceivedFather{};
        std::string fatherFallbackName{};
        std::uint32_t fetusCount{ 0 };
        float earliestConceptionDay{ 0.0f };
        bool isMotherPartner{ false };
        bool isSecret{ false };
        bool isSecretForPartner{ false };
        bool isSecretForBioFather{ false };
        bool isPregnancySecretForPartner{ false };
        std::vector<State::ChildId> fetusIds{};

        bool operator==(const PerceivedPregnancyGroup&) const = default;
    };

    struct PerceivedSelfPregnancy {
        State::ActorId mother{};
        State::ActorId motherPartner{};
        std::uint32_t totalFetusCount{ 0 };
        bool hasPaternitySecret{ false };
        bool anySecret{ false };
        bool anySecretForPartner{ false };
        bool anySecretForBioFather{ false };
        bool anyPregnancySecretForPartner{ false };
        std::vector<PerceivedPregnancyGroup> groups{};

        bool operator==(const PerceivedSelfPregnancy&) const = default;
    };

    enum class ExpectingParentRole : std::uint8_t {
        PartnerBioFather = 0,
        PresumedFatherUnaware = 1,
        PartnerKnowsTruth = 2,
        BioFatherKnows = 3
    };

    struct PerceivedExpectingPregnancy {
        State::ActorId mother{};
        std::uint32_t fetusCount{ 0 };
        float earliestConceptionDay{ 0.0f };
        ExpectingParentRole role{ ExpectingParentRole::PartnerBioFather };
        State::ActorId perceivedBioFather{};
        std::string perceivedBioFatherFallbackName{};
        State::ActorId motherPartner{};
        bool isSecretForPartner{ false };
        bool isPregnancySecretForPartner{ false };
        bool isSecretForBioFather{ false };

        bool operator==(const PerceivedExpectingPregnancy&) const = default;
    };

    enum class SiblingRelationKind : std::uint8_t {
        Full = 0,
        Half = 1,
        AdoptiveOrStep = 2
    };

    struct PerceivedSiblingEntry {
        State::ChildId childId{};
        std::string name{};
        std::int32_t timestamp{ 0 };
        State::ChildStage stage{ State::ChildStage::Child };
        std::uint8_t gender{ 2 };
        SiblingRelationKind relation{ SiblingRelationKind::Half };
        bool isAdopted{ true };

        bool operator==(const PerceivedSiblingEntry&) const = default;
    };

    struct PerceivedAffair {
        State::AffairId id{};
        State::ActorId cheater{};
        State::ActorId deceivedPartner{};
        State::ActorId paramour{};
        std::uint32_t incidentCount{ 1 };
        std::int32_t lastTimestamp{ 0 };
        bool isSecret{ false };
        State::AffairType affairType{ State::AffairType::Casual };
        State::AffairStatus affairStatus{ State::AffairStatus::Ongoing };
        State::AffairStage effectiveAffairStage{ State::AffairStage::Physical };
        float stageStartDay{ 0.0f };

        bool operator==(const PerceivedAffair&) const = default;
    };

    struct PerceivedRelationshipEdge {
        State::CanonicalPairKey pair{};
        State::RelationshipStatus status{ State::RelationshipStatus::Candidate };
        State::RelationshipStatus originalStatus{ State::RelationshipStatus::Unknown };
        bool isOpen{ false };
        bool isSecretAffair{ false };
        bool isCheatingPair{ false };
        std::vector<State::StatusTransitionEntry> statusHistory{};

        [[nodiscard]] constexpr State::ActorId Other(State::ActorId actor) const noexcept {
            return pair.Other(actor);
        }

        bool operator==(const PerceivedRelationshipEdge&) const = default;
    };

    /**
     * Single canonical read-only projection lens enforcing secret-masking and perspective
     * semantics across all C++ readers (SkyrimNet, UI dashboard, dumps, Papyrus modals, progression).
     *
     * Raw LineageTable and AffairLedger remain strictly encapsulated inside PerspectiveView
     * so readers cannot accidentally bypass perspective masking.
     */
    class PerspectiveView {
    public:
        [[nodiscard]] static PerspectiveView ForActor(
            std::shared_ptr<const State::WorldStateSnapshot> world,
            std::shared_ptr<const State::SpatialActorSnapshot> spatial,
            State::ActorId viewer,
            const IWerewolfStateProvider* werewolfProvider = nullptr);

        [[nodiscard]] static PerspectiveView ForDialogueTarget(
            std::shared_ptr<const State::WorldStateSnapshot> world,
            std::shared_ptr<const State::SpatialActorSnapshot> spatial,
            const IWerewolfStateProvider* werewolfProvider = nullptr);

        [[nodiscard]] static PerspectiveView ForDashboard(
            std::shared_ptr<const State::WorldStateSnapshot> world,
            std::shared_ptr<const State::SpatialActorSnapshot> spatial,
            const IWerewolfStateProvider* werewolfProvider = nullptr);

        [[nodiscard]] static PerspectiveView ForDashboard(
            std::shared_ptr<const State::WorldStateSnapshot> world,
            std::shared_ptr<const State::SpatialActorSnapshot> spatial,
            bool revealSecretsOverride,
            const IWerewolfStateProvider* werewolfProvider = nullptr);

        [[nodiscard]] ViewerMode Mode() const noexcept { return mode_; }
        [[nodiscard]] State::ActorId Viewer() const noexcept { return viewer_; }
        [[nodiscard]] bool IsPublicOnly() const noexcept { return mode_ == ViewerMode::DialogueTargetPublic; }
        [[nodiscard]] bool IsOmniscient() const noexcept { return mode_ == ViewerMode::Omniscient; }

        // Safe read-only accessors for non-secret domain tables and spatial state
        [[nodiscard]] const State::SpatialActorSnapshot& Spatial() const noexcept { return *spatial_; }
        [[nodiscard]] const State::ActorProfileTable& Actors() const noexcept { return *world_->actors; }
        [[nodiscard]] const State::SpouseHierarchyTable& Hierarchy() const noexcept { return *world_->hierarchy; }
        [[nodiscard]] const State::HouseholdTable& Household() const noexcept { return *world_->household; }
        [[nodiscard]] const State::AffectionTable& Affection() const noexcept { return *world_->affection; }
        [[nodiscard]] const State::DeceasedTable& Deceased() const noexcept { return *world_->deceased; }
        [[nodiscard]] const State::CooldownTable& Cooldowns() const noexcept { return *world_->cooldowns; }
        [[nodiscard]] const State::SettingsData& Settings() const noexcept { return *world_->settings; }

        [[nodiscard]] bool IsActorKnownWerewolf(State::ActorId actor) const noexcept;
        [[nodiscard]] bool IsActorAlive(State::ActorId actor) const noexcept;

        // Child, lineage & pregnancy projections
        [[nodiscard]] bool IsChildVisible(State::ChildId childId) const noexcept;
        [[nodiscard]] bool CanViewerKnowPregnancy(State::ChildId childId) const noexcept;
        [[nodiscard]] bool CanViewerKnowTrueBioFather(State::ChildId childId) const noexcept;

        [[nodiscard]] std::optional<PerceivedChildLineage> GetPerceivedChild(State::ChildId childId) const;
        [[nodiscard]] std::vector<PerceivedChildLineage> GetVisibleChildren() const;
        [[nodiscard]] std::vector<PerceivedChildLineage> GetCustodialChildrenOf(State::ActorId guardian) const;
        [[nodiscard]] std::vector<PerceivedChildLineage> GetNonCustodialChildrenOf(State::ActorId parent) const;
        [[nodiscard]] std::vector<PerceivedSiblingEntry> GetPerceivedSiblingsOf(
            State::ChildId childId,
            bool forcePublicOnly = false) const;

        [[nodiscard]] std::optional<PerceivedSelfPregnancy> GetSelfPregnancy(State::ActorId mother) const;
        [[nodiscard]] std::vector<PerceivedExpectingPregnancy> GetExpectingPregnanciesFor(
            State::ActorId prospectiveParent) const;

        [[nodiscard]] std::vector<State::ActorId> GetVisibleParentIdsForTrainingDiscount(
            State::ChildId childId) const;
        [[nodiscard]] std::vector<State::ActorId> GetVisitableFamilyParentIds(
            State::ChildId childId) const;

        // Relationship & affair projections
        [[nodiscard]] bool CanViewerKnowAffair(const State::AffairRecord& record) const noexcept;
        [[nodiscard]] std::optional<PerceivedRelationshipEdge> GetPerceivedPair(
            State::ActorId a,
            State::ActorId b) const;
        [[nodiscard]] std::vector<PerceivedRelationshipEdge> GetPerceivedRelationshipsOf(
            State::ActorId subject) const;
        [[nodiscard]] std::vector<PerceivedRelationshipEdge> GetVisibleRelationshipEdges() const;

        [[nodiscard]] std::vector<PerceivedAffair> GetKnownAffairsInvolving(State::ActorId actor) const;
        [[nodiscard]] std::vector<PerceivedAffair> GetAllVisibleAffairs() const;
        [[nodiscard]] State::PendingPartnerIntentRecord GetPerceivedPendingIntent(
            State::ActorId partner) const noexcept;

    private:
        PerspectiveView(
            std::shared_ptr<const State::WorldStateSnapshot> world,
            std::shared_ptr<const State::SpatialActorSnapshot> spatial,
            ViewerMode mode,
            State::ActorId viewer,
            const IWerewolfStateProvider* werewolfProvider);

        struct ResolvedPaternityContext {
            State::ActorId cleanMotherPartner{};
            State::ActorId effectivePresumedFather{};
            bool isBioSameAsPresumed{ false };
            bool hasPresumedFather{ false };
            bool secretFromPartnerActive{ false };
            bool secretFromBioFatherActive{ false };
            bool pregnancySecretActive{ false };
            bool rawHasUndiscoveredSecret{ false };
            bool canViewerKnow{ false };
            bool hideSecretFromViewer{ false };
        };

        [[nodiscard]] State::ActorId ResolveMotherPartner(const State::ChildRecord& child) const noexcept;
        [[nodiscard]] ResolvedPaternityContext AnalyzePaternity(const State::ChildRecord& child) const noexcept;
        [[nodiscard]] bool CanViewerKnowTrueBioFatherImpl(
            const State::ChildRecord& child,
            State::ActorId effectivePresumedFather,
            bool isBioSameAsPresumed) const noexcept;
        [[nodiscard]] bool CanViewerKnowPregnancyImpl(const State::ChildRecord& child) const noexcept;
        [[nodiscard]] PerceivedChildLineage ProjectChildRecord(const State::ChildRecord& child) const;

        [[nodiscard]] bool IsSecretAffairPair(State::ActorId a, State::ActorId b) const;
        [[nodiscard]] bool HasVisibleOngoingAffairBetween(State::ActorId a, State::ActorId b) const;
        [[nodiscard]] bool IsVisiblePairCheating(const PerceivedRelationshipEdge& perceived) const;

        std::shared_ptr<const State::WorldStateSnapshot> world_;
        std::shared_ptr<const State::SpatialActorSnapshot> spatial_;
        ViewerMode mode_{ ViewerMode::ActorSelf };
        State::ActorId viewer_{};
        const IWerewolfStateProvider* werewolfProvider_{ nullptr };
    };

}  // namespace MARAS::Projection

namespace maras {
    namespace projection = ::MARAS::Projection;
}
