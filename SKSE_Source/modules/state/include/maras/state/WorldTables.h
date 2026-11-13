#pragma once

#include "maras/foundation/StrongIds.h"
#include "maras/state/DomainTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace MARAS::State {

    // =========================================================================
    // 1. ActorProfileTable ('NPCR' profiles + deferred base-ID relationship queue)
    // =========================================================================

    struct ActorProfileRecord {
        RefId refId{};
        BaseId baseId{};
        SocialClass socialClass{ SocialClass::Outcast };
        SkillType skillType{ SkillType::Warrior };
        Temperament temperament{ Temperament::Proud };

        // True for registered adult romance/matchmaking participants (including graduated Stage 4 Adult Children)
        bool isRegisteredAdult{ true };

        // Legacy pre-v4 housing metadata preserved for lossless 'NPCR' round-trip compatibility
        std::optional<FormRawId> legacyOriginalHome{};
        std::optional<FormRawId> legacyCurrentHome{};
        std::optional<RefId> legacyHomeMarker{};

        // Runtime-added keywords re-applied on load
        std::unordered_set<FormRawId> addedKeywords{};

        bool operator==(const ActorProfileRecord&) const = default;
    };

    struct PendingRelationshipEntry {
        BaseId partnerBaseId{};
        RefId partnerRefId{};
        RelationshipStatus targetStatus{ RelationshipStatus::Unknown };
        RelationshipStatus originalStatus{ RelationshipStatus::Unknown };

        bool operator==(const PendingRelationshipEntry&) const = default;
    };

    class ActorProfileTable {
    public:
        [[nodiscard]] const ActorProfileRecord* Find(RefId refId) const noexcept;
        [[nodiscard]] ActorProfileRecord* FindMutable(RefId refId) noexcept;
        ActorProfileRecord& Upsert(ActorProfileRecord record);
        bool Erase(RefId refId) noexcept;

        [[nodiscard]] bool IsRegisteredAdult(RefId refId) const noexcept;
        void SetRegisteredAdult(RefId refId, bool registered);

        [[nodiscard]] const std::unordered_map<RefId, ActorProfileRecord>& AllProfiles() const noexcept {
            return profiles_;
        }
        [[nodiscard]] std::size_t Size() const noexcept { return profiles_.size(); }

        void AddPendingRelationship(BaseId actorBaseId, const PendingRelationshipEntry& entry);
        bool ErasePendingRelationshipsForBase(BaseId actorBaseId) noexcept;
        [[nodiscard]] const std::unordered_map<BaseId, std::vector<PendingRelationshipEntry>>&
        AllPendingRelationships() const noexcept {
            return pendingByBase_;
        }

        void Clear() noexcept;
        bool operator==(const ActorProfileTable&) const = default;

    private:
        std::unordered_map<RefId, ActorProfileRecord> profiles_;
        std::unordered_map<BaseId, std::vector<PendingRelationshipEntry>> pendingByBase_;
    };

    // =========================================================================
    // 2. RelationshipEdgeTable ('NPCR' pairs + history + 'AGN1' partner intents)
    // =========================================================================

    struct StatusTransitionEntry {
        RelationshipStatus fromStatus{ RelationshipStatus::Unknown };
        RelationshipStatus toStatus{ RelationshipStatus::Unknown };
        float timestamp{ 0.0f };
        ActorId initiator{};

        bool operator==(const StatusTransitionEntry&) const = default;
    };

    struct RelationshipEdgeRecord {
        CanonicalPairKey pair{};
        RelationshipStatus status{ RelationshipStatus::Candidate };
        RelationshipStatus originalStatus{ RelationshipStatus::Unknown };
        bool isOpen{ false };
        std::vector<StatusTransitionEntry> statusHistory{};

        bool operator==(const RelationshipEdgeRecord&) const = default;
    };

    struct PendingPartnerIntentRecord {
        PartnerIntentType type{ PartnerIntentType::None };
        float createdGameDay{ -1.0f };
        ActorId relatedLover{};

        bool operator==(const PendingPartnerIntentRecord&) const = default;
    };

    class RelationshipEdgeTable {
    public:
        [[nodiscard]] const RelationshipEdgeRecord* FindEdge(ActorId a, ActorId b) const noexcept;
        [[nodiscard]] const RelationshipEdgeRecord* FindEdge(CanonicalPairKey key) const noexcept;
        RelationshipEdgeRecord& GetOrCreateEdge(ActorId a, ActorId b);
        void UpsertEdge(RelationshipEdgeRecord edge);
        bool EraseEdge(ActorId a, ActorId b) noexcept;
        bool EraseEdge(CanonicalPairKey key) noexcept;

        [[nodiscard]] RelationshipStatus GetStatus(ActorId a, ActorId b) const noexcept;
        [[nodiscard]] bool IsPairOpen(ActorId a, ActorId b) const noexcept;
        [[nodiscard]] bool HasAnyOpenMainPair(ActorId actor) const noexcept;
        [[nodiscard]] bool IsInAnyOpenRelationship(ActorId actor) const noexcept;

        [[nodiscard]] const std::vector<CanonicalPairKey>& GetAdjacentPairs(ActorId actor) const noexcept;
        [[nodiscard]] std::vector<ActorId> GetPartnersWithStatus(ActorId actor, RelationshipStatus status) const;
        [[nodiscard]] std::vector<std::pair<ActorId, RelationshipStatus>> GetActivePartners(ActorId actor) const;
        [[nodiscard]] ActorId GetPrimaryPartner(ActorId actor) const noexcept;

        // Pending partner agency intents ('AGN1')
        void SetPendingIntent(ActorId partner, PendingPartnerIntentRecord intent);
        [[nodiscard]] PendingPartnerIntentRecord GetPendingIntent(ActorId partner) const noexcept;
        bool ClearPendingIntent(ActorId partner) noexcept;
        void ClearIntentsInvolving(ActorId actor) noexcept;
        [[nodiscard]] const std::unordered_map<ActorId, PendingPartnerIntentRecord>&
        AllPendingIntents() const noexcept {
            return pendingPartnerIntents_;
        }

        [[nodiscard]] const std::unordered_map<CanonicalPairKey, RelationshipEdgeRecord, CanonicalPairKeyHash>&
        AllEdges() const noexcept {
            return edges_;
        }
        [[nodiscard]] std::size_t EdgeCount() const noexcept { return edges_.size(); }

        void Clear() noexcept;
        bool operator==(const RelationshipEdgeTable& other) const;

    private:
        void IndexKey(CanonicalPairKey key);
        void UnindexKey(CanonicalPairKey key) noexcept;

        std::unordered_map<CanonicalPairKey, RelationshipEdgeRecord, CanonicalPairKeyHash> edges_;
        std::unordered_map<ActorId, std::vector<CanonicalPairKey>> adjacency_;
        std::unordered_map<ActorId, PendingPartnerIntentRecord> pendingPartnerIntents_;
    };

    // =========================================================================
    // 3. AffairLedger ('NPCR' directional cheating records + 'AGN1' affairStage)
    //    Indexed by all 3 participant IDs + couple pair for O(1) lifecycle queries
    // =========================================================================

    struct AffairRecord {
        AffairId id{};
        ActorId cheater{};
        ActorId deceivedPartner{};
        ActorId paramour{};
        std::uint32_t incidentCount{ 1 };
        std::int32_t lastTimestamp{ 0 };
        bool isSecret{ false };
        AffairType affairType{ AffairType::Casual };
        AffairStatus affairStatus{ AffairStatus::Ongoing };
        AffairStage affairStage{ AffairStage::Physical };
        float stageStartDay{ 0.0f };

        [[nodiscard]] constexpr CanonicalPairKey CouplePair() const noexcept {
            return CanonicalPairKey{ cheater, deceivedPartner };
        }

        [[nodiscard]] constexpr bool CanObserverKnow(ActorId viewer) const noexcept {
            if (!isSecret || affairStatus == AffairStatus::Allowed || affairStatus == AffairStatus::EndedConfronted) {
                return true;
            }
            return !viewer.IsNull() && (viewer == cheater || viewer == paramour);
        }

        bool operator==(const AffairRecord&) const = default;
    };

    struct AffairIncidentOutcome {
        AffairId affairId{};
        bool escalatedTalkingToPhysical{ false };
    };

    class AffairLedger {
    public:
        AffairIncidentOutcome RecordIncident(
            ActorId cheater,
            ActorId deceivedPartner,
            ActorId paramour,
            bool isSecret,
            AffairType affairType,
            AffairStatus affairStatus,
            AffairStage affairStage,
            float currentGameDay);

        // Raw import used by CoSaveCodec to preserve exact serialized counts, timestamps, and ordering
        AffairId ImportSerializedRecord(
            CanonicalPairKey couplePair,
            ActorId cheater,
            ActorId deceivedPartner,
            ActorId paramour,
            std::uint32_t count,
            std::int32_t lastTimestamp,
            bool isSecret,
            AffairType affairType,
            AffairStatus affairStatus,
            AffairStage affairStage = AffairStage::Physical,
            float stageStartDay = 0.0f);

        bool UpdateStageMetadata(
            CanonicalPairKey couplePair,
            ActorId cheater,
            ActorId deceivedPartner,
            ActorId paramour,
            AffairStage stage,
            float stageStartDay) noexcept;

        [[nodiscard]] const AffairRecord* FindById(AffairId id) const noexcept;
        [[nodiscard]] const AffairRecord* FindTriple(ActorId cheater, ActorId deceivedPartner, ActorId paramour) const noexcept;

        [[nodiscard]] std::vector<AffairRecord> GetForCouplePair(CanonicalPairKey pair) const;
        [[nodiscard]] std::vector<AffairRecord> GetByCheater(ActorId cheater) const;
        [[nodiscard]] std::vector<AffairRecord> GetByDeceivedPartner(ActorId deceivedPartner) const;
        [[nodiscard]] std::vector<AffairRecord> GetByParamour(ActorId paramour) const;
        [[nodiscard]] std::vector<AffairRecord> GetAllInvolving(ActorId actor) const;

        [[nodiscard]] bool HasCheatedOn(ActorId cheater, ActorId deceivedPartner) const noexcept;
        [[nodiscard]] bool HasOngoingAffairForPair(CanonicalPairKey pair) const noexcept;

        bool SetStatus(ActorId cheater, ActorId deceivedPartner, ActorId paramour, AffairStatus nextStatus) noexcept;
        bool SetStage(ActorId cheater, ActorId deceivedPartner, ActorId paramour, AffairStage nextStage, float currentGameDay) noexcept;
        void MarkAllowedForPair(ActorId a, ActorId b) noexcept;
        std::size_t ExposeAndResolve(ActorId cheater, ActorId paramour, ActorId deceivedFilter, AffairResolution resolution) noexcept;

        // Lifecycle transitions: O(1) via 3-participant indices
        std::vector<ActorId> CloseOngoingForDeceasedActor(ActorId deceased) noexcept;
        std::vector<ActorId> CloseOngoingBetweenCheaterAndParamour(ActorId a, ActorId b) noexcept;
        std::vector<ActorId> CloseOngoingOnVictimSplit(ActorId a, ActorId b) noexcept;
        std::vector<ActorId> EraseAllInvolvingActor(ActorId unregistered) noexcept;
        void EraseForCouplePair(CanonicalPairKey pair) noexcept;

        [[nodiscard]] const std::unordered_map<AffairId, AffairRecord>& AllRecords() const noexcept {
            return records_;
        }
        [[nodiscard]] std::size_t Size() const noexcept { return records_.size(); }

        void Clear() noexcept;
        bool operator==(const AffairLedger& other) const;

    private:
        void IndexRecord(const AffairRecord& rec);
        void UnindexRecord(const AffairRecord& rec) noexcept;

        std::uint64_t nextAffairId_{ 1 };
        std::unordered_map<AffairId, AffairRecord> records_;
        std::unordered_map<CanonicalPairKey, std::vector<AffairId>, CanonicalPairKeyHash> byCouplePair_;
        std::unordered_map<ActorId, std::vector<AffairId>> byCheater_;
        std::unordered_map<ActorId, std::vector<AffairId>> byDeceivedPartner_;
        std::unordered_map<ActorId, std::vector<AffairId>> byParamour_;
    };

    // =========================================================================
    // 4. LineageTable ('NPCR' child registry + 'PRG1'/'CHD1'/'LYC1'/'CHD2' + pendingChildren)
    // =========================================================================

    // Strictly slot-neutral custodial guardian pair (SKSE_Source/AGENTS.md §10)
    struct GuardianSlots {
        ActorId slotA{};
        ActorId slotB{};

        [[nodiscard]] constexpr bool HasGuardian(ActorId actor) const noexcept {
            return !actor.IsNull() && (slotA == actor || slotB == actor);
        }

        [[nodiscard]] constexpr bool HasAnyGuardian() const noexcept {
            return !slotA.IsNull() || !slotB.IsNull();
        }

        [[nodiscard]] constexpr int Count() const noexcept {
            int count = 0;
            if (!slotA.IsNull()) {
                ++count;
            }
            if (!slotB.IsNull() && slotB != slotA) {
                ++count;
            }
            return count;
        }

        [[nodiscard]] constexpr bool IsSingleGuardian() const noexcept { return Count() == 1; }

        [[nodiscard]] constexpr ActorId SingleGuardian() const noexcept {
            if (!IsSingleGuardian()) {
                return ActorId{};
            }
            return !slotA.IsNull() ? slotA : slotB;
        }

        [[nodiscard]] constexpr bool IsSoleGuardian(ActorId actor) const noexcept {
            return IsSingleGuardian() && HasGuardian(actor);
        }

        [[nodiscard]] constexpr ActorId OtherGuardian(ActorId actor) const noexcept {
            if (actor.IsNull()) {
                return ActorId{};
            }
            if (slotA == actor) {
                return slotB;
            }
            if (slotB == actor) {
                return slotA;
            }
            return ActorId{};
        }

        [[nodiscard]] constexpr bool HasSameSet(ActorId gA, ActorId gB) const noexcept {
            if (gA == gB) {
                gB = ActorId{};
            }
            return (slotA == gA && slotB == gB) || (slotA == gB && slotB == gA);
        }

        [[nodiscard]] constexpr std::array<ActorId, 2> AsArray() const noexcept {
            return { slotA, slotB };
        }

        GuardianSetDiff Assign(ActorId newA, ActorId newB) noexcept;
        GuardianSetDiff Remove(ActorId actor) noexcept;

        // Order-independent equality for guardian set semantics; exact slot values preserved internally
        [[nodiscard]] constexpr bool operator==(const GuardianSlots& other) const noexcept {
            return HasSameSet(other.slotA, other.slotB);
        }
    };

    struct PaternitySecretState {
        bool isSecret{ false };
        bool isSecretForBioFather{ false };
        bool isSecretForPartner{ false };
        bool isPregnancySecretForPartner{ false };
        bool discovered{ false };
        bool discoveredByPartner{ false };
        bool discoveredByBioFather{ false };
        bool hasPhenotypeMismatch{ false };
        float lastCheckTimestamp{ 0.0f };
        std::string bioFatherName{};
        FormRawId bioFatherRaceId{};

        [[nodiscard]] constexpr bool HasActiveSecret() const noexcept {
            return isSecret && (isSecretForBioFather || isSecretForPartner || isPregnancySecretForPartner);
        }

        // Authoritative observer check for whether `viewer` knows the mother is pregnant (Unborn stage)
        [[nodiscard]] constexpr bool CanObserverKnowPregnancy(
            ChildStage stage,
            ActorId bioMother,
            ActorId bioFather,
            ActorId viewer) const noexcept {
            if (stage != ChildStage::Unborn || !isPregnancySecretForPartner) {
                return true;
            }
            if (!bioMother.IsNull() && viewer == bioMother) {
                return true;
            }
            if (!bioFather.IsNull() && !bioFather.IsUnknownFather() && viewer == bioFather) {
                return !isSecretForBioFather || discoveredByBioFather;
            }
            return false;
        }

        // Authoritative observer check for whether `viewer` knows the true `bioFather`
        [[nodiscard]] constexpr bool CanObserverKnowTrueBioFather(
            ChildId selfChildId,
            ActorId bioMother,
            ActorId bioFather,
            ActorId motherPartner,
            ActorId explicitPresumedFather,
            ActorId viewer) const noexcept {
            if (!HasActiveSecret() || discovered) {
                return true;
            }
            if (!bioMother.IsNull() && viewer == bioMother) {
                return true;
            }
            // A child never magically knows its own hidden paternity secret
            if (!selfChildId.IsNull() && viewer.raw() == selfChildId.raw()) {
                return false;
            }
            if (!bioFather.IsNull() && !bioFather.IsUnknownFather() && viewer == bioFather) {
                return !isSecretForBioFather || discoveredByBioFather;
            }
            const bool isBioSameAsPartner =
                (!motherPartner.IsNull() && !bioFather.IsNull() && bioFather == motherPartner);
            const bool hasPresumedPartner =
                (!motherPartner.IsNull() && !isBioSameAsPartner &&
                 (bioMother.IsNull() || motherPartner != bioMother) &&
                 (isSecretForPartner || !explicitPresumedFather.IsNull()));
            if (hasPresumedPartner) {
                return !isSecretForPartner || discoveredByPartner;
            }
            return !isSecretForBioFather || discoveredByBioFather;
        }

        bool operator==(const PaternitySecretState&) const = default;
    };

    struct ChildRecord {
        ChildId childId{};
        GuardianSlots guardians{};
        std::string name{};
        std::int32_t timestamp{ 0 };
        bool isAdopted{ true };
        ActorId bioMother{};
        ActorId bioFather{};
        ActorId presumedFather{};
        bool isBiologicalChild{ false };
        float conceptionTimestamp{ 0.0f };
        float birthTimestamp{ 0.0f };
        PaternitySecretState paternitySecret{};
        ChildStage stage{ ChildStage::Child };
        std::string trainingLocation{};
        std::uint8_t gender{ 2 };
        std::int32_t trainingClassIndex{ -1 };
        float trainingStartGameDay{ 0.0f };
        std::string savedBioTemplateName{};
        std::uint8_t savedRaceIndex{ 255 };

        // 'CHD1' visual genetics & RPG progression state
        FormRawId hairColorFormId{};
        FormRawId eyesPartFormId{};
        FormRawId hairPartFormId{};
        FormRawId facialHairPartFormId{};
        float heightScale{ 1.0f };
        bool rpgApplied{ false };

        // 'LYC1' beastblood inheritance & training state
        bool hasBeastblood{ false };
        bool beastbloodEvaluated{ false };

        // 'CHD2' lineage skill snapshots & scaling state
        bool bioLineageEvaluated{ false };
        bool guardianLineageEvaluated{ false };
        std::array<std::uint8_t, 18> bioSkillBonuses{};
        std::array<std::uint8_t, 18> guardianSkillBonuses{};
        std::string daedricBlessingId{};
        std::uint16_t lastAppliedLevel{ 0 };

        bool operator==(const ChildRecord&) const = default;
    };

    struct PendingChildEntry {
        BaseId childBaseId{};
        RefId childRefId{};
        BaseId parentABaseId{};
        RefId parentARefId{};
        BaseId parentBBaseId{};
        RefId parentBRefId{};
        bool isAdopted{ true };

        bool operator==(const PendingChildEntry&) const = default;
    };

    class LineageTable {
    public:
        SyntheticChildId AllocateSyntheticChildId();
        void ObserveLoadedSyntheticId(SyntheticChildId synId) noexcept;
        [[nodiscard]] std::uint32_t NextSyntheticChildRaw() const noexcept { return nextSyntheticChildRaw_; }

        [[nodiscard]] const ChildRecord* FindChild(ChildId id) const noexcept;
        [[nodiscard]] ChildRecord* FindChildMutable(ChildId id) noexcept;
        ChildRecord& UpsertChild(ChildRecord record);
        bool EraseChild(ChildId id) noexcept;

        [[nodiscard]] std::vector<ChildId> GetChildrenOfGuardian(ActorId guardian) const;
        [[nodiscard]] std::vector<ChildId> GetChildrenWithGuardianSet(ActorId gA, ActorId gB) const;
        [[nodiscard]] std::vector<ChildId> GetUnbornOfMother(ActorId mother) const;

        void AddPendingChild(BaseId childBaseId, const PendingChildEntry& entry);
        bool ErasePendingChild(BaseId childBaseId) noexcept;

        [[nodiscard]] const std::unordered_map<ChildId, ChildRecord>& AllChildren() const noexcept {
            return children_;
        }
        [[nodiscard]] std::unordered_map<ChildId, ChildRecord>& MutableChildren() noexcept {
            return children_;
        }
        [[nodiscard]] const std::unordered_map<BaseId, PendingChildEntry>& AllPendingChildren() const noexcept {
            return pendingChildrenByBase_;
        }
        [[nodiscard]] std::size_t Size() const noexcept { return children_.size(); }

        void Clear() noexcept;
        bool operator==(const LineageTable&) const = default;

    private:
        std::uint32_t nextSyntheticChildRaw_{ Foundation::kSyntheticChildMinRawId + 1u };
        std::unordered_map<ChildId, ChildRecord> children_;
        std::unordered_map<BaseId, PendingChildEntry> pendingChildrenByBase_;
    };

    // =========================================================================
    // 5. SpouseHierarchyTable ('SPHR')
    // =========================================================================

    class SpouseHierarchyTable {
    public:
        static constexpr std::size_t kSlotCount = 3;

        [[nodiscard]] int GetRank(ActorId spouse) const noexcept;
        [[nodiscard]] ActorId GetSpouseAtSlot(std::size_t slotIndex) const noexcept;
        [[nodiscard]] const std::array<ActorId, kSlotCount>& Slots() const noexcept { return slots_; }

        void SetRawSlot(std::size_t slotIndex, ActorId spouse) noexcept;
        bool SetRank(ActorId spouse, int rank, const std::vector<ActorId>& marriedToPlayerOrder);
        void OnSpouseAdded(ActorId spouse) noexcept;
        void OnSpouseRemoved(ActorId spouse, const std::vector<ActorId>& remainingMarriedOrder);
        void FillGaps(
            const std::vector<ActorId>& marriedToPlayerOrder,
            ActorId excludeActor = ActorId{},
            const std::function<bool(ActorId)>& isExcludedOrDeceased = nullptr);

        void Clear() noexcept { slots_.fill(ActorId{}); }
        bool operator==(const SpouseHierarchyTable&) const noexcept = default;

    private:
        std::array<ActorId, kSlotCount> slots_{};
    };

    // =========================================================================
    // 6. HouseholdTable ('PHOU' + 'SPAS' + 'PLCH' + 'CFSN' + 'ACTR')
    //    Single normalized owner of houses, tenancy, shared cells, child home state, & travel
    // =========================================================================

    struct PlayerHouseRecord {
        HouseId houseId{};
        RefId markerId{};
        bool initialMoveDone{ false };
        // Ordered tenant IDs (can hold adult RefId or ChildId raw values as persisted in 'PHOU')
        std::vector<std::uint32_t> tenantOrder{};

        bool operator==(const PlayerHouseRecord&) const = default;
    };

    struct TenantAssignment {
        HouseId houseId{};
        bool stayAtHome{ true };

        bool operator==(const TenantAssignment&) const = default;
    };

    struct SharedCellRecord {
        bool sharedWithPlayer{ false };
        bool originalPublicRecorded{ false };
        bool originalPublicState{ false };
        std::set<ActorId> sharingSpouses{};

        bool operator==(const SharedCellRecord&) const = default;
    };

    struct ChildHouseholdRecord {
        ChildId childId{};
        HouseId currentHouseId{};
        RefId homeMarkerId{};
        RefId petId{};
        RefId critterId{};
        float currentOrder{ 0.0f };
        float orderExpiryGameDay{ -1.0f };
        bool critterBanned{ false };
        bool petBanned{ false };
        bool isNewlyAdopted{ false };
        bool forcegreetPending{ false };
        float critterRetryTimestamp{ 0.0f };
        float petRetryTimestamp{ 0.0f };
        bool playedScene1{ false };
        bool playedScene2{ false };
        float lastInteractionTimestamp{ 0.0f };

        bool operator==(const ChildHouseholdRecord&) const = default;
    };

    struct FactionRankSnapshotEntry {
        FormRawId factionId{};
        std::int8_t rank{ 0 };

        bool operator==(const FactionRankSnapshotEntry&) const = default;
    };

    struct ChildFactionSnapshotRecord {
        FormRawId crimeFactionId{};
        std::vector<FactionRankSnapshotEntry> factions{};

        bool operator==(const ChildFactionSnapshotRecord&) const = default;
    };

    struct AdultChildTravelRecord {
        RefId targetMarkerId{};
        LocationId targetLocationId{};
        CellId targetCellId{};
        std::string destinationName{};
        float assignedGameDay{ -1.0f };
        float arrivedGameDay{ -1.0f };
        float stayDurationDays{ 3.0f };
        bool isFamilyVisit{ false };

        bool operator==(const AdultChildTravelRecord&) const = default;
    };

    class HouseholdTable {
    public:
        // Player Houses & Tenancy ('PHOU') — atomically synced with ChildHouseholdRecord::currentHouseId
        bool RegisterHouse(HouseId houseId, RefId markerId, bool initialMoveDone = false);
        [[nodiscard]] const PlayerHouseRecord* FindHouse(HouseId houseId) const noexcept;
        [[nodiscard]] PlayerHouseRecord* FindHouseMutable(HouseId houseId) noexcept;
        [[nodiscard]] const std::vector<PlayerHouseRecord>& AllHouses() const noexcept { return houses_; }

        bool AssignTenant(std::uint32_t tenantRawId, HouseId houseId, bool stayAtHome = true, bool isChildTenant = false);
        bool EvictTenant(std::uint32_t tenantRawId) noexcept;
        [[nodiscard]] HouseId GetTenantHouse(std::uint32_t tenantRawId) const noexcept;
        [[nodiscard]] bool GetTenantStayAtHome(std::uint32_t tenantRawId) const noexcept;
        void SetTenantStayAtHome(std::uint32_t tenantRawId, bool stayAtHome) noexcept;
        [[nodiscard]] const std::unordered_map<std::uint32_t, TenantAssignment>& AllTenants() const noexcept {
            return tenants_;
        }

        // Spouse Shared Cells & Original Bed Owners ('SPAS')
        SharedCellRecord& UpsertSharedCell(CellId cellId, SharedCellRecord record);
        bool StopSharingForSpouse(ActorId spouse) noexcept;
        [[nodiscard]] bool IsCellSharedWithPlayer(CellId cellId) const noexcept;
        [[nodiscard]] const std::map<CellId, SharedCellRecord>& AllSharedCells() const noexcept {
            return sharedCells_;
        }

        void SetOriginalBedOwner(RefId bedRef, FormRawId originalOwner) { originalBedOwners_[bedRef] = originalOwner; }
        bool EraseOriginalBedOwner(RefId bedRef) noexcept { return originalBedOwners_.erase(bedRef) > 0; }
        [[nodiscard]] const std::unordered_map<RefId, FormRawId>& AllOriginalBedOwners() const noexcept {
            return originalBedOwners_;
        }

        // Player Children Runtime State ('PLCH') & Faction Snapshots ('CFSN')
        [[nodiscard]] bool IsChildChestIntroduced() const noexcept { return childChestIntroduced_; }
        void SetChildChestIntroduced(bool introduced) noexcept { childChestIntroduced_ = introduced; }

        [[nodiscard]] const ChildHouseholdRecord* FindChildState(ChildId childId) const noexcept;
        [[nodiscard]] ChildHouseholdRecord* FindChildStateMutable(ChildId childId) noexcept;
        ChildHouseholdRecord& UpsertChildState(ChildHouseholdRecord record);
        bool EraseChildState(ChildId childId) noexcept;
        void ClearOnlyPlayerChildrenRecords() noexcept;
        [[nodiscard]] const std::unordered_map<ChildId, ChildHouseholdRecord>& AllChildStates() const noexcept {
            return childStates_;
        }

        void UpsertChildFactionSnapshot(ChildId childId, ChildFactionSnapshotRecord snapshot);
        bool EraseChildFactionSnapshot(ChildId childId) noexcept;
        void ClearChildFactionSnapshots() noexcept { childFactionSnapshots_.clear(); }
        [[nodiscard]] const std::unordered_map<ChildId, ChildFactionSnapshotRecord>&
        AllChildFactionSnapshots() const noexcept {
            return childFactionSnapshots_;
        }

        // Adult Child Travel ('ACTR')
        void UpsertAdultChildTravel(ChildId childId, AdultChildTravelRecord record);
        bool EraseAdultChildTravel(ChildId childId) noexcept;
        [[nodiscard]] const AdultChildTravelRecord* FindAdultChildTravel(ChildId childId) const noexcept;
        [[nodiscard]] const std::unordered_map<ChildId, AdultChildTravelRecord>&
        AllAdultChildTravel() const noexcept {
            return adultChildTravel_;
        }
        void ClearAdultChildTravel() noexcept { adultChildTravel_.clear(); }

        void Clear() noexcept;
        bool operator==(const HouseholdTable&) const = default;

    private:
        std::vector<PlayerHouseRecord> houses_;
        std::unordered_map<std::uint32_t, TenantAssignment> tenants_;
        std::map<CellId, SharedCellRecord> sharedCells_;
        std::unordered_map<RefId, FormRawId> originalBedOwners_;
        bool childChestIntroduced_{ false };
        std::unordered_map<ChildId, ChildHouseholdRecord> childStates_;
        std::unordered_map<ChildId, ChildFactionSnapshotRecord> childFactionSnapshots_;
        std::unordered_map<ChildId, AdultChildTravelRecord> adultChildTravel_;
    };

    // =========================================================================
    // 7. AffectionTable ('AFCT')
    // =========================================================================

    class AffectionTable {
    public:
        AffectionTable();

        static constexpr int ClampScore(int value) noexcept {
            return std::clamp(value, 0, 100);
        }
        [[nodiscard]] static std::string NormalizeTypeKey(std::string_view rawType);

        void SetPermanent(ActorId actor, int score);
        [[nodiscard]] int GetPermanent(ActorId actor, int defaultVal = 50) const noexcept;
        [[nodiscard]] bool HasPermanent(ActorId actor) const noexcept { return permanent_.contains(actor); }

        void SetLastInteractionDay(ActorId actor, float gameDay);
        [[nodiscard]] std::optional<float> GetLastInteractionDay(ActorId actor) const noexcept;

        void SetDailyAccumulation(ActorId actor, std::string_view type, float amount);
        [[nodiscard]] float GetDailyAccumulation(ActorId actor, std::string_view type) const;
        void ClearDailyAccumulation() noexcept { daily_.clear(); }

        void SetMinMaxForType(std::string_view type, int minVal, int maxVal);
        [[nodiscard]] std::optional<std::pair<int, int>> GetMinMaxForType(std::string_view type) const;
        void ResetDefaultMinMax();

        void EraseActor(ActorId actor) noexcept;
        void Clear();

        [[nodiscard]] const std::unordered_map<ActorId, int>& AllPermanent() const noexcept { return permanent_; }
        [[nodiscard]] const std::unordered_map<ActorId, float>& AllLastDays() const noexcept { return lastDay_; }
        [[nodiscard]] const std::unordered_map<ActorId, std::unordered_map<std::string, float>>&
        AllDaily() const noexcept {
            return daily_;
        }
        [[nodiscard]] const std::unordered_map<std::string, std::pair<int, int>>&
        AllMinMaxByType() const noexcept {
            return minMaxByType_;
        }

        bool operator==(const AffectionTable&) const = default;

    private:
        std::unordered_map<ActorId, int> permanent_;
        std::unordered_map<ActorId, float> lastDay_;
        std::unordered_map<ActorId, std::unordered_map<std::string, float>> daily_;
        std::unordered_map<std::string, std::pair<int, int>> minMaxByType_;
    };

    // =========================================================================
    // 8. DeceasedTable ('DCSD')
    //    Keys dynamic 0xFFxxxxxx spawned/cloned actors strictly by unique RefId
    // =========================================================================

    struct DeceasedPartnerEntry {
        BaseId partnerBaseId{};
        RefId partnerRefId{};
        std::string partnerName{};
        RelationshipStatus statusAtDeath{ RelationshipStatus::Unknown };
        bool isPlayer{ false };

        bool operator==(const DeceasedPartnerEntry&) const = default;
    };

    struct DeceasedChildMetadata {
        BaseId guardianABaseId{};
        RefId guardianARefId{};
        std::string guardianAName{};
        BaseId guardianBBaseId{};
        RefId guardianBRefId{};
        std::string guardianBName{};
        BaseId bioMotherBaseId{};
        BaseId bioFatherBaseId{};
        bool isAdopted{ true };
        HouseId houseId{};

        bool operator==(const DeceasedChildMetadata&) const = default;
    };

    struct DeceasedActorRecord {
        BaseId baseId{};
        RefId refId{};
        std::string name{};
        float deathTimestamp{ 0.0f };
        bool isChild{ false };
        bool wasKilledByPlayer{ false };
        BaseId killerBaseId{};
        RefId killerRefId{};
        std::string killerName{};

        // Adult attributes (populated when isChild == false)
        SocialClass socialClass{ SocialClass::Outcast };
        SkillType skillType{ SkillType::Warrior };
        Temperament temperament{ Temperament::Proud };
        std::vector<DeceasedPartnerEntry> partnersAtDeath{};

        // Present for deceased young children and deceased graduated Stage 4 Adult Children (v7+)
        std::optional<DeceasedChildMetadata> childInfo{};

        bool operator==(const DeceasedActorRecord&) const = default;
    };

    class DeceasedTable {
    public:
        // Canonical primary key computation: dynamic 0xFFxxxxxx actors NEVER index by shared template BaseId
        [[nodiscard]] static constexpr std::uint32_t ComputePrimaryKey(BaseId baseId, RefId refId) noexcept {
            if (refId.IsDynamicRuntimeRef()) {
                return refId.raw();
            }
            if (!baseId.IsNull()) {
                return baseId.raw();
            }
            return refId.raw();
        }

        bool UpsertRecord(DeceasedActorRecord record);
        [[nodiscard]] const DeceasedActorRecord* FindByActor(ActorId actor, BaseId optionalKnownBase = BaseId{}) const noexcept;
        [[nodiscard]] bool IsDeceased(ActorId actor, BaseId optionalKnownBase = BaseId{}) const noexcept {
            return FindByActor(actor, optionalKnownBase) != nullptr;
        }
        [[nodiscard]] bool IsWidowOrWidower(ActorId survivingActor) const noexcept;

        [[nodiscard]] const std::unordered_map<std::uint32_t, DeceasedActorRecord>& AllRecords() const noexcept {
            return recordsByKey_;
        }
        [[nodiscard]] std::size_t Size() const noexcept { return recordsByKey_.size(); }

        void Clear() noexcept;
        bool operator==(const DeceasedTable&) const = default;

    private:
        std::unordered_map<std::uint32_t, DeceasedActorRecord> recordsByKey_;
        std::unordered_map<RefId, std::uint32_t> refToKey_;
    };

    // =========================================================================
    // 9. CooldownTable ('MKMK' + 'BNTR')
    // =========================================================================

    struct CooldownTable {
        // Matchmaking & Partner Agency ('MKMK')
        float lastMatchmakingDay{ -1.0f };
        float lastPlayerPartnerCheatingDay{ -1.0f };
        std::unordered_map<ActorId, float> npcLastMatchmakingChangeDay{};
        std::unordered_map<LocationId, float> lastHomeArrivalEventDayByLoc{};

        // Banter Coordinator ('BNTR')
        float savedBanterCooldownHours{ 24.0f };
        float lastBanterSceneDay{ -1.0f };
        std::unordered_map<CanonicalPairKey, float, CanonicalPairKeyHash> banterCoupleCooldowns{};

        void ClearMatchmaking() noexcept {
            lastMatchmakingDay = -1.0f;
            lastPlayerPartnerCheatingDay = -1.0f;
            npcLastMatchmakingChangeDay.clear();
            lastHomeArrivalEventDayByLoc.clear();
        }

        void ClearBanter(float defaultCooldownHours = 24.0f) noexcept {
            savedBanterCooldownHours = std::max(0.0f, defaultCooldownHours);
            lastBanterSceneDay = -1.0f;
            banterCoupleCooldowns.clear();
        }

        void EraseActor(ActorId actor) noexcept;

        bool operator==(const CooldownTable&) const = default;
    };

    // =========================================================================
    // 10. SettingsData ('CNFG' subVersion 4 + INI runtime defaults)
    // =========================================================================

    struct SettingsData {
        std::uint32_t toggleUIKey{ 0x30u };
        bool toggleUIModCtrl{ false };
        bool toggleUIModShift{ false };
        bool toggleUIModAlt{ true };

        bool enableConfirmationModal{ true };
        std::int32_t actionsCooldown{ 10 };
        std::int32_t actionsDeclineCooldown{ 60 };

        bool enableConfirmPlayerNpc{ true };
        bool enableConfirmNpcNpc{ true };

        bool confirmStatusChange_Dating{ true };
        bool confirmStatusChange_Engagement{ true };
        bool confirmStatusChange_Lover{ true };
        bool confirmStatusChange_Marry{ true };
        bool confirmBreakup_Dating{ true };
        bool confirmBreakup_Engagement{ true };
        bool confirmBreakup_Lover{ true };
        bool confirmDivorce{ true };
        bool confirmDivorceDuringLowAffectionQuest{ false };
        bool confirmOpenRelationship{ true };
        bool confirmCloseRelationship{ true };

        bool skipWedding{ false };
        bool alwaysSuccessMarriage{ false };
        std::int32_t logDestination{ 0 };
        std::int32_t defaultLogLevel{ 1 };
        std::int32_t logLevel{ 1 };
        float affectionDecayMultiplier{ 1.0f };
        std::int32_t lonelinessThresholdDays{ 2 };
        float skyrimnetActionRadius{ 3000.0f };
        bool skyrimnetUseLLMMemories{ true };
        bool skyrimnetUseDecisions{ true };
        bool disablePolygamyQuest{ false };
        bool migratedFromStorageUtil{ false };
        bool initialRelationshipScanDone{ false };
        bool simulateNoSkyrimNet{ false };

        ChildCustodyGenderPolicy childCustodyGenderPolicy{ ChildCustodyGenderPolicy::StayWithMother };
        ChildCustodyPlayerPolicy childCustodyPlayerPolicy{ ChildCustodyPlayerPolicy::UseGenderCustody };
        bool childCustodyPrioritizeBioParent{ true };

        bool matchmakingEnabled{ true };
        std::int32_t matchmakingIntervalDays{ 5 };
        std::int32_t matchmakingMaxSuggestions{ 5 };
        std::int32_t matchmakingNpcCooldownDays{ 14 };
        bool matchmakingAllowBreakups{ true };
        bool matchmakingAllowOpenClose{ true };
        bool matchmakingAllowCheating{ true };
        MatchmakingOrientation matchmakingOrientation{ MatchmakingOrientation::Heterosexual };
        bool matchmakingUseLLM{ true };
        bool matchmakingEvaluationStep{ true };
        bool matchmakingGenerateMemories{ true };
        bool matchmakingConfirmResults{ false };

        bool playerPartnerCheatingAgencyEnabled{ true };
        bool playerPartnerOpenRelAgencyEnabled{ true };
        float playerPartnerCheatingChanceMultiplier{ 1.0f };
        std::int32_t playerPartnerCheatingMaxRollCap{ 65 };
        std::int32_t playerPartnerCheatingMaxSecretLovers{ 1 };
        bool playerPartnerCheatingAllowWhenHappy{ false };
        bool playerPartnerCheatingEnableNeglect{ true };
        bool playerPartnerCheatingEnableRevenge{ true };
        bool playerPartnerCheatingEnableHierarchy{ true };
        bool playerPartnerCheatingEnableChildrenAnchor{ true };
        bool playerPartnerCheatingUseLLM{ true };
        bool playerPartnerCheatingConfirmResults{ false };
        float playerPartnerAgencyOpenLoverChanceMultiplier{ 1.0f };
        float playerPartnerAgencyBreakupChanceMultiplier{ 1.0f };
        bool playerPartnerAgencyEnableForceGreets{ true };
        bool playerPartnerAgencyEnableHomeArrivalEvents{ true };

        float multLonelinessDecayStart{ 1.0f };
        float multPartnerAgencyScanInterval{ 1.5f };
        float multRecentAttentionWindow{ 1.0f };
        float multVeryRecentVisitWindow{ 0.5f };
        float multNeglectGrowthStart{ 1.0f };
        float multContentNeglectStart{ 2.0f };
        float multProlongedAbsencePenalty{ 2.0f };
        float multAffairTalkingEscalation{ 2.0f };
        float multMinSideFlingAgeForBreakup{ 1.5f };
        float multPartnerIntentTTL{ 2.0f };
        float multHomeArrivalCooldown{ 1.0f };
        float multAffairFatigueStart{ 5.0f };
        float multRomanticAttendedBreakup{ 1.5f };
        float multSuggestOpenNeglectMin{ 2.0f };

        std::int32_t diceRollChanceNewDating{ 22 };
        std::int32_t diceRollChanceNewEngaged{ 17 };
        std::int32_t diceRollChanceNewLoverAllowed{ 17 };
        std::int32_t diceRollChanceNewLoverCheating{ 6 };
        std::int32_t diceRollChanceAdvance{ 22 };
        std::int32_t diceRollChanceBreakup{ 11 };
        std::int32_t diceRollChanceOpenClose{ 5 };

        std::int32_t paternityBaseChance{ 2 };
        float paternityDailyGrowth{ 2.0f };
        std::int32_t paternityMaxChance{ 60 };
        std::int32_t paternityPhenotypeBaseChance{ 15 };
        float paternityPhenotypeDailyGrowth{ 4.0f };
        std::int32_t paternityPhenotypeMaxChance{ 80 };
        std::int32_t paternityPhenotypeBirthChance{ 100 };
        std::int32_t paternityNonSkyrimNetRevealChance{ 50 };
        std::int32_t paternityBreakupChance{ 50 };
        std::int32_t paternityBioStepUpChance{ 30 };
        bool paternityForceDiceRolls{ false };

        bool revealSecretsInUI{ false };
        bool uiFullScreen{ false };
        float uiScale{ 1.0f };
        bool autoPauseOnOpen{ false };

        bool enableScenes{ true };
        float scenesCooldownHours{ 24.0f };
        bool enableScenes_Lover{ true };
        bool enableScenes_Dating{ true };
        bool enableScenes_Engaged{ true };
        bool enableScenes_Married{ true };
        bool enableScenes_OpenRelationship{ true };
        bool enableScenes_Cheating{ true };
        bool enableScenes_CheatingDiscovered{ true };
        bool enableScenes_CheatingCaught{ true };

        bool enableScenes_LocTypeTemple{ true };
        bool enableScenes_LocTypeStore{ true };
        bool enableScenes_LocTypeStewardsDwelling{ true };
        bool enableScenes_LocTypeInn{ true };
        bool enableScenes_LocTypeHouse{ true };
        bool enableScenes_LocTypeBarracks{ true };
        bool enableScenes_LocTypeGuild{ true };
        bool enableScenes_LocTypePlayerHouse{ true };
        bool enableScenes_BYOH_LocTypeHomestead{ true };
        bool enableScenes_LocTypeTown{ true };
        bool enableScenes_LocTypeSettlement{ true };
        bool enableScenes_LocTypeOrcStronghold{ true };
        bool enableScenes_LocTypeCity{ true };
        bool enableScenes_LocTypeLumberMill{ true };
        bool enableScenes_LocTypeFarm{ true };

        bool bGlobalBanCritter{ false };
        bool bGlobalBanPet{ false };

        std::int32_t childTrainingDurationDays{ 14 };
        std::int32_t childTrainingCost{ 2000 };
        bool childTrainingRequireFaction{ true };

        // Optional runtime mirror of enablePolygamy TESGlobal (default true per Settings::GetEnablePolygamy)
        bool enablePlayerPolygamy{ true };

        [[nodiscard]] constexpr float GetBaseLonelinessDays() const noexcept {
            return static_cast<float>(std::clamp(lonelinessThresholdDays, 1, 14));
        }
        [[nodiscard]] constexpr float GetLonelinessDecayStartDays() const noexcept {
            return GetBaseLonelinessDays() * multLonelinessDecayStart;
        }
        [[nodiscard]] constexpr float GetPartnerAgencyScanIntervalDays() const noexcept {
            return GetBaseLonelinessDays() * multPartnerAgencyScanInterval;
        }
        [[nodiscard]] constexpr float GetAffairTalkingEscalationDays() const noexcept {
            return GetBaseLonelinessDays() * multAffairTalkingEscalation;
        }
        [[nodiscard]] constexpr float GetPartnerIntentTTLDays() const noexcept {
            return GetBaseLonelinessDays() * multPartnerIntentTTL;
        }
        [[nodiscard]] constexpr float GetHomeArrivalCooldownDays() const noexcept {
            return GetBaseLonelinessDays() * multHomeArrivalCooldown;
        }

        bool operator==(const SettingsData&) const = default;
    };

    // Visits every co-save persisted field in exact 'CNFG' subVersion 4 binary stream order
    template <typename Visitor>
    void ForEachPersistedSettingField(Visitor&& visit) {
        // Required initial v4 header block (10 fields)
        visit("skipWedding", &SettingsData::skipWedding, true);
        visit("alwaysSuccessMarriage", &SettingsData::alwaysSuccessMarriage, true);
        visit("logDestination", &SettingsData::logDestination, true);
        visit("logLevel", &SettingsData::logLevel, true);
        visit("affectionDecayMultiplier", &SettingsData::affectionDecayMultiplier, true);
        visit("confirmStatusChange_Marry", &SettingsData::confirmStatusChange_Marry, true);
        visit("confirmBreakup_Engagement", &SettingsData::confirmBreakup_Engagement, true);
        visit("confirmDivorce", &SettingsData::confirmDivorce, true);
        visit("confirmDivorceDuringLowAffectionQuest", &SettingsData::confirmDivorceDuringLowAffectionQuest, true);
        visit("migratedFromStorageUtil", &SettingsData::migratedFromStorageUtil, true);

        // Optional tail fields (in exact binary stream order)
        visit("initialRelationshipScanDone", &SettingsData::initialRelationshipScanDone, false);
        visit("childCustodyGenderPolicy", &SettingsData::childCustodyGenderPolicy, false);
        visit("childCustodyPlayerPolicy", &SettingsData::childCustodyPlayerPolicy, false);
        visit("childCustodyPrioritizeBioParent", &SettingsData::childCustodyPrioritizeBioParent, false);
        visit("bGlobalBanCritter", &SettingsData::bGlobalBanCritter, false);
        visit("bGlobalBanPet", &SettingsData::bGlobalBanPet, false);
        visit("enableConfirmPlayerNpc", &SettingsData::enableConfirmPlayerNpc, false);
        visit("enableConfirmNpcNpc", &SettingsData::enableConfirmNpcNpc, false);
        visit("confirmStatusChange_Dating", &SettingsData::confirmStatusChange_Dating, false);
        visit("confirmStatusChange_Engagement", &SettingsData::confirmStatusChange_Engagement, false);
        visit("confirmStatusChange_Lover", &SettingsData::confirmStatusChange_Lover, false);
        visit("confirmBreakup_Dating", &SettingsData::confirmBreakup_Dating, false);
        visit("confirmBreakup_Lover", &SettingsData::confirmBreakup_Lover, false);
        visit("confirmOpenRelationship", &SettingsData::confirmOpenRelationship, false);
        visit("confirmCloseRelationship", &SettingsData::confirmCloseRelationship, false);
        visit("skyrimnetActionRadius", &SettingsData::skyrimnetActionRadius, false);
        visit("matchmakingEnabled", &SettingsData::matchmakingEnabled, false);
        visit("matchmakingIntervalDays", &SettingsData::matchmakingIntervalDays, false);
        visit("matchmakingMaxSuggestions", &SettingsData::matchmakingMaxSuggestions, false);
        visit("matchmakingAllowBreakups", &SettingsData::matchmakingAllowBreakups, false);
        visit("matchmakingAllowOpenClose", &SettingsData::matchmakingAllowOpenClose, false);
        visit("matchmakingOrientation", &SettingsData::matchmakingOrientation, false);
        visit("matchmakingAllowCheating", &SettingsData::matchmakingAllowCheating, false);
        visit("matchmakingNpcCooldownDays", &SettingsData::matchmakingNpcCooldownDays, false);
        visit("matchmakingUseLLM", &SettingsData::matchmakingUseLLM, false);
        visit("diceRollChanceNewDating", &SettingsData::diceRollChanceNewDating, false);
        visit("diceRollChanceNewEngaged", &SettingsData::diceRollChanceNewEngaged, false);
        visit("diceRollChanceNewLoverAllowed", &SettingsData::diceRollChanceNewLoverAllowed, false);
        visit("diceRollChanceNewLoverCheating", &SettingsData::diceRollChanceNewLoverCheating, false);
        visit("diceRollChanceAdvance", &SettingsData::diceRollChanceAdvance, false);
        visit("diceRollChanceBreakup", &SettingsData::diceRollChanceBreakup, false);
        visit("diceRollChanceOpenClose", &SettingsData::diceRollChanceOpenClose, false);
        visit("simulateNoSkyrimNet", &SettingsData::simulateNoSkyrimNet, false);
        visit("matchmakingEvaluationStep", &SettingsData::matchmakingEvaluationStep, false);
        visit("matchmakingGenerateMemories", &SettingsData::matchmakingGenerateMemories, false);
        visit("paternityBaseChance", &SettingsData::paternityBaseChance, false);
        visit("paternityDailyGrowth", &SettingsData::paternityDailyGrowth, false);
        visit("paternityMaxChance", &SettingsData::paternityMaxChance, false);
        visit("paternityPhenotypeBaseChance", &SettingsData::paternityPhenotypeBaseChance, false);
        visit("paternityPhenotypeDailyGrowth", &SettingsData::paternityPhenotypeDailyGrowth, false);
        visit("paternityPhenotypeMaxChance", &SettingsData::paternityPhenotypeMaxChance, false);
        visit("paternityPhenotypeBirthChance", &SettingsData::paternityPhenotypeBirthChance, false);
        visit("paternityNonSkyrimNetRevealChance", &SettingsData::paternityNonSkyrimNetRevealChance, false);
        visit("paternityBreakupChance", &SettingsData::paternityBreakupChance, false);
        visit("paternityBioStepUpChance", &SettingsData::paternityBioStepUpChance, false);
        visit("paternityForceDiceRolls", &SettingsData::paternityForceDiceRolls, false);
        visit("enableScenes", &SettingsData::enableScenes, false);
        visit("enableScenes_Lover", &SettingsData::enableScenes_Lover, false);
        visit("enableScenes_Dating", &SettingsData::enableScenes_Dating, false);
        visit("enableScenes_Engaged", &SettingsData::enableScenes_Engaged, false);
        visit("enableScenes_Married", &SettingsData::enableScenes_Married, false);
        visit("enableScenes_OpenRelationship", &SettingsData::enableScenes_OpenRelationship, false);
        visit("enableScenes_Cheating", &SettingsData::enableScenes_Cheating, false);
        visit("enableScenes_CheatingDiscovered", &SettingsData::enableScenes_CheatingDiscovered, false);
        visit("enableScenes_CheatingCaught", &SettingsData::enableScenes_CheatingCaught, false);
        visit("enableScenes_LocTypeTemple", &SettingsData::enableScenes_LocTypeTemple, false);
        visit("enableScenes_LocTypeStore", &SettingsData::enableScenes_LocTypeStore, false);
        visit("enableScenes_LocTypeStewardsDwelling", &SettingsData::enableScenes_LocTypeStewardsDwelling, false);
        visit("enableScenes_LocTypeInn", &SettingsData::enableScenes_LocTypeInn, false);
        visit("enableScenes_LocTypeHouse", &SettingsData::enableScenes_LocTypeHouse, false);
        visit("enableScenes_LocTypeBarracks", &SettingsData::enableScenes_LocTypeBarracks, false);
        visit("enableScenes_LocTypeFarm", &SettingsData::enableScenes_LocTypeFarm, false);
        visit("enableScenes_LocTypeGuild", &SettingsData::enableScenes_LocTypeGuild, false);
        visit("enableScenes_LocTypePlayerHouse", &SettingsData::enableScenes_LocTypePlayerHouse, false);
        visit("enableScenes_BYOH_LocTypeHomestead", &SettingsData::enableScenes_BYOH_LocTypeHomestead, false);
        visit("enableScenes_LocTypeTown", &SettingsData::enableScenes_LocTypeTown, false);
        visit("enableScenes_LocTypeSettlement", &SettingsData::enableScenes_LocTypeSettlement, false);
        visit("enableScenes_LocTypeOrcStronghold", &SettingsData::enableScenes_LocTypeOrcStronghold, false);
        visit("enableScenes_LocTypeCity", &SettingsData::enableScenes_LocTypeCity, false);
        visit("enableScenes_LocTypeLumberMill", &SettingsData::enableScenes_LocTypeLumberMill, false);
        visit("scenesCooldownHours", &SettingsData::scenesCooldownHours, false);
        visit("disablePolygamyQuest", &SettingsData::disablePolygamyQuest, false);
        visit("matchmakingConfirmResults", &SettingsData::matchmakingConfirmResults, false);
        visit("revealSecretsInUI", &SettingsData::revealSecretsInUI, false);
        visit("uiFullScreen", &SettingsData::uiFullScreen, false);
        visit("uiScale", &SettingsData::uiScale, false);
        visit("autoPauseOnOpen", &SettingsData::autoPauseOnOpen, false);
        visit("playerPartnerCheatingAgencyEnabled", &SettingsData::playerPartnerCheatingAgencyEnabled, false);
        visit("playerPartnerCheatingChanceMultiplier", &SettingsData::playerPartnerCheatingChanceMultiplier, false);
        visit("playerPartnerCheatingMaxRollCap", &SettingsData::playerPartnerCheatingMaxRollCap, false);
        visit("playerPartnerCheatingMaxSecretLovers", &SettingsData::playerPartnerCheatingMaxSecretLovers, false);
        visit("playerPartnerCheatingAllowWhenHappy", &SettingsData::playerPartnerCheatingAllowWhenHappy, false);
        visit("playerPartnerCheatingEnableNeglect", &SettingsData::playerPartnerCheatingEnableNeglect, false);
        visit("playerPartnerCheatingEnableRevenge", &SettingsData::playerPartnerCheatingEnableRevenge, false);
        visit("playerPartnerCheatingEnableHierarchy", &SettingsData::playerPartnerCheatingEnableHierarchy, false);
        visit("playerPartnerCheatingEnableChildrenAnchor", &SettingsData::playerPartnerCheatingEnableChildrenAnchor, false);
        visit("playerPartnerCheatingUseLLM", &SettingsData::playerPartnerCheatingUseLLM, false);
        visit("playerPartnerCheatingConfirmResults", &SettingsData::playerPartnerCheatingConfirmResults, false);
        visit("lonelinessThresholdDays", &SettingsData::lonelinessThresholdDays, false);
        visit("playerPartnerAgencyOpenLoverChanceMultiplier", &SettingsData::playerPartnerAgencyOpenLoverChanceMultiplier, false);
        visit("playerPartnerAgencyBreakupChanceMultiplier", &SettingsData::playerPartnerAgencyBreakupChanceMultiplier, false);
        visit("playerPartnerAgencyEnableForceGreets", &SettingsData::playerPartnerAgencyEnableForceGreets, false);
        visit("playerPartnerAgencyEnableHomeArrivalEvents", &SettingsData::playerPartnerAgencyEnableHomeArrivalEvents, false);
        visit("playerPartnerOpenRelAgencyEnabled", &SettingsData::playerPartnerOpenRelAgencyEnabled, false);
        visit("multLonelinessDecayStart", &SettingsData::multLonelinessDecayStart, false);
        visit("multPartnerAgencyScanInterval", &SettingsData::multPartnerAgencyScanInterval, false);
        visit("multRecentAttentionWindow", &SettingsData::multRecentAttentionWindow, false);
        visit("multVeryRecentVisitWindow", &SettingsData::multVeryRecentVisitWindow, false);
        visit("multNeglectGrowthStart", &SettingsData::multNeglectGrowthStart, false);
        visit("multContentNeglectStart", &SettingsData::multContentNeglectStart, false);
        visit("multProlongedAbsencePenalty", &SettingsData::multProlongedAbsencePenalty, false);
        visit("multAffairTalkingEscalation", &SettingsData::multAffairTalkingEscalation, false);
        visit("multMinSideFlingAgeForBreakup", &SettingsData::multMinSideFlingAgeForBreakup, false);
        visit("multPartnerIntentTTL", &SettingsData::multPartnerIntentTTL, false);
        visit("multHomeArrivalCooldown", &SettingsData::multHomeArrivalCooldown, false);
        visit("multAffairFatigueStart", &SettingsData::multAffairFatigueStart, false);
        visit("multRomanticAttendedBreakup", &SettingsData::multRomanticAttendedBreakup, false);
        visit("multSuggestOpenNeglectMin", &SettingsData::multSuggestOpenNeglectMin, false);
        visit("skyrimnetUseLLMMemories", &SettingsData::skyrimnetUseLLMMemories, false);
        visit("skyrimnetUseDecisions", &SettingsData::skyrimnetUseDecisions, false);
        visit("childTrainingDurationDays", &SettingsData::childTrainingDurationDays, false);
        visit("childTrainingCost", &SettingsData::childTrainingCost, false);
        visit("childTrainingRequireFaction", &SettingsData::childTrainingRequireFaction, false);
    }

}  // namespace MARAS::State
