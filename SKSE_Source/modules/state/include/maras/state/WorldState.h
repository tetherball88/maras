#pragma once

#include "maras/foundation/IRng.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/DomainTypes.h"
#include "maras/state/PerTableSnapshot.h"
#include "maras/state/WorldTables.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MARAS::State {

    /**
     * Pure read-only actor metadata port injected by the engine adapter (in MARAS.dll)
     * or by deterministic mocks in `maras_tests`.
     */
    class IActorMetadataProvider {
    public:
        virtual ~IActorMetadataProvider() = default;

        [[nodiscard]] virtual ActorSex GetSex(ActorId actor) const noexcept {
            (void)actor;
            return ActorSex::Unknown;
        }

        [[nodiscard]] virtual bool IsAliveAndValid(ActorId actor) const noexcept {
            return !actor.IsNull() && !actor.IsUnknownFather() && !actor.IsSyntheticChild();
        }

        [[nodiscard]] virtual bool IsDynamicActorPresent(RefId refId) const noexcept {
            return !refId.IsNull();
        }

        [[nodiscard]] virtual std::string GetActorName(ActorId actor) const {
            if (actor.IsPlayer()) {
                return "Player";
            }
            return {};
        }

        [[nodiscard]] virtual FormRawId GetActorRaceId(ActorId actor) const noexcept {
            (void)actor;
            return FormRawId{};
        }

        [[nodiscard]] virtual BaseId GetActorBaseId(ActorId actor) const noexcept {
            if (actor.IsPlayer()) {
                return BaseId{ Foundation::kPlayerBaseRawId };
            }
            return BaseId{};
        }
    };

    class DefaultActorMetadataProvider final : public IActorMetadataProvider {};

    /**
     * Deltas produced by a canonical WorldState mutation so downstream engine adapters
     * (factions, AI packages, UI dashboard push notifications) can reconcile strictly
     * what changed without mutating domain state inside queries.
     */
    struct RestoredSharedCellDelta {
        CellId cellId{};
        SharedCellRecord previousRecord{};

        bool operator==(const RestoredSharedCellDelta&) const = default;
    };

    struct HierarchyShiftDelta {
        ActorId spouse{};
        int oldRank{ 4 };
        int newRank{ 4 };

        [[nodiscard]] bool IsPromotion() const noexcept { return newRank < oldRank; }
        [[nodiscard]] int RankDiff() const noexcept { return oldRank - newRank; }

        bool operator==(const HierarchyShiftDelta&) const = default;
    };

    struct WorldMutationResult {
        bool changed{ false };
        bool hierarchyChanged{ false };
        std::vector<ActorId> dirtyActors{};
        std::vector<ChildId> dirtyChildren{};
        std::vector<ActorId> stoppedSharingSpouses{};
        std::vector<RestoredSharedCellDelta> restoredSharedCells{};
        std::vector<RefId> retiredSceneMarkers{};
        std::vector<HierarchyShiftDelta> hierarchyShifts{};

        void MarkActorDirty(ActorId actor);
        void MarkChildDirty(ChildId child);
        void Merge(const WorldMutationResult& other);
    };

    /**
     * Immutable, lock-free point-in-time snapshot of all 10 normalized M.A.R.A.S tables.
     * Obtained in O(1) via `WorldState::Snapshot()` with zero copying.
     */
    struct WorldStateSnapshot {
        std::shared_ptr<const ActorProfileTable> actors;
        std::shared_ptr<const RelationshipEdgeTable> relationships;
        std::shared_ptr<const AffairLedger> affairs;
        std::shared_ptr<const LineageTable> lineage;
        std::shared_ptr<const SpouseHierarchyTable> hierarchy;
        std::shared_ptr<const HouseholdTable> household;
        std::shared_ptr<const AffectionTable> affection;
        std::shared_ptr<const DeceasedTable> deceased;
        std::shared_ptr<const CooldownTable> cooldowns;
        std::shared_ptr<const SettingsData> settings;
    };

    /**
     * WorldState — Single authoritative, engine-header-free owner of all M.A.R.A.S persistent domain state.
     *
     * Consolidates all 12 legacy mutable singletons into 10 normalized `CowTable<T>` tables.
     * Calling `Revert()` (`*this = WorldState{};`) guarantees zero cross-save state bleed by construction.
     */
    class WorldState {
    public:
        CowTable<ActorProfileTable> actors{};
        CowTable<RelationshipEdgeTable> relationships{};
        CowTable<AffairLedger> affairs{};
        CowTable<LineageTable> lineage{};
        CowTable<SpouseHierarchyTable> hierarchy{};
        CowTable<HouseholdTable> household{};
        CowTable<AffectionTable> affection{};
        CowTable<DeceasedTable> deceased{};
        CowTable<CooldownTable> cooldowns{};
        CowTable<SettingsData> settings{};

        // Complete structural reset on SKSE Revert / New Game
        void Revert() { *this = WorldState{}; }

        // O(1) lock-free immutable snapshot export
        [[nodiscard]] WorldStateSnapshot Snapshot() const noexcept;

        // =====================================================================
        // Read-Only Domain Queries (CQS-compliant: zero state mutation)
        // =====================================================================

        [[nodiscard]] bool IsMarried(ActorId a, ActorId b) const noexcept;
        [[nodiscard]] bool IsDivorced(ActorId a, ActorId b) const noexcept;
        [[nodiscard]] bool IsPlayerActiveMainPartner(ActorId npc) const noexcept;
        [[nodiscard]] std::vector<ActorId> GetPlayerSpousesInMarriageOrder() const;
        [[nodiscard]] ActorId GetHighestRankingSurvivingPlayerSpouse(ActorId excludeSpouse = ActorId{}) const;
        [[nodiscard]] ActorId GetMotherPartner(ChildId childId) const noexcept;

        [[nodiscard]] bool IsActorCheating(ActorId actor) const;
        [[nodiscard]] bool IsRelationshipCheating(ActorId actorA, ActorId actorB) const;

        [[nodiscard]] ActorId ResolveSingleCustodian(
            ChildId childId,
            const ChildRecord& child,
            ActorId parentA,
            ActorId parentB,
            const IActorMetadataProvider& meta,
            Foundation::IRng& rng) const;

        // =====================================================================
        // Canonical Cross-Table Lifecycle Mutations
        // =====================================================================

        WorldMutationResult TransitionRelationship(
            ActorId actorA,
            ActorId actorB,
            RelationshipStatus targetStatus,
            float currentGameDay,
            ActorId initiator = ActorId{},
            const IActorMetadataProvider& meta = DefaultActorMetadataProvider{},
            Foundation::IRng* optionalRng = nullptr);

        WorldMutationResult PromoteToMarried(
            ActorId actorA,
            ActorId actorB,
            float currentGameDay,
            ActorId initiator = ActorId{},
            const IActorMetadataProvider& meta = DefaultActorMetadataProvider{});

        WorldMutationResult PromoteToDivorced(
            ActorId actorA,
            ActorId actorB,
            float currentGameDay,
            ActorId initiator = ActorId{},
            const IActorMetadataProvider& meta = DefaultActorMetadataProvider{},
            Foundation::IRng* optionalRng = nullptr);

        WorldMutationResult BreakupPair(
            ActorId actorA,
            ActorId actorB,
            RelationshipStatus breakupStatus,
            float currentGameDay,
            ActorId initiator = ActorId{},
            const IActorMetadataProvider& meta = DefaultActorMetadataProvider{},
            Foundation::IRng* optionalRng = nullptr);

        WorldMutationResult SetPairOpen(
            ActorId actorA,
            ActorId actorB,
            bool isOpen,
            float currentGameDay = 0.0f);

        bool RecordCheatingIncident(
            ActorId cheater,
            ActorId deceivedPartner,
            ActorId paramour,
            bool isSecret,
            AffairType affairType,
            AffairStatus affairStatus,
            AffairStage affairStage,
            float currentGameDay,
            WorldMutationResult* outMutations = nullptr);

        // Custody & Household Synchronization
        WorldMutationResult SyncChildHouseholdState(
            ChildId childId,
            HouseId explicitHouseId = HouseId{});

        WorldMutationResult ApplyBirthCustodyRules(
            ChildId childId,
            ActorId motherId,
            ActorId presumedFatherId,
            ActorId bioFatherId,
            const IActorMetadataProvider& meta,
            Foundation::IRng& rng);

        WorldMutationResult EvaluateCustodyOnMarriage(
            ActorId actorA,
            ActorId actorB,
            const IActorMetadataProvider& meta);

        WorldMutationResult EvaluateCustodyOnDivorce(
            ActorId actorA,
            ActorId actorB,
            const IActorMetadataProvider& meta,
            Foundation::IRng& rng);

        WorldMutationResult EvaluateCustodyOnDeath(
            ActorId deceasedActor,
            const IActorMetadataProvider& meta);

        WorldMutationResult SyncAllMarriedCouplesChildCustody(
            const IActorMetadataProvider& meta,
            Foundation::IRng& rng);

        // Death & Unregistration
        WorldMutationResult RecordActorDeath(
            ActorId deceasedActor,
            float deathGameDay,
            bool killedByPlayer = false,
            ActorId killerActor = ActorId{},
            const IActorMetadataProvider& meta = DefaultActorMetadataProvider{});

        WorldMutationResult UnregisterActor(ActorId actor);
        WorldMutationResult StopSharingForSpouseWithDeltas(ActorId spouse);

        bool operator==(const WorldState&) const = default;
    };

}  // namespace MARAS::State
