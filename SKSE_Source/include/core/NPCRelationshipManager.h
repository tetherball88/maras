#pragma once

#include "core/RelationshipTypes.h"
#include "core/Serialization.h"
#include "utils/Common.h"
#include "utils/JsonOverrideLoader.h"

// Forward declare SKSE interface
namespace SKSE {
    class SerializationInterface;
}

namespace MARAS {

    // Unique key identifying a bidirectional relationship pair between two actors
    struct RelationshipPairKey {
        RE::FormID actorA;
        RE::FormID actorB;

        RelationshipPairKey() : actorA(0), actorB(0) {}
        RelationshipPairKey(RE::FormID a, RE::FormID b) {
            if (a < b) {
                actorA = a;
                actorB = b;
            } else {
                actorA = b;
                actorB = a;
            }
        }

        bool operator==(const RelationshipPairKey& other) const {
            return actorA == other.actorA && actorB == other.actorB;
        }

        bool Contains(RE::FormID id) const {
            return actorA == id || actorB == id;
        }

        RE::FormID GetOther(RE::FormID id) const {
            if (actorA == id) return actorB;
            if (actorB == id) return actorA;
            return 0;
        }
    };

    struct RelationshipPairKeyHash {
        std::size_t operator()(const RelationshipPairKey& k) const {
            std::size_t h1 = std::hash<RE::FormID>{}(k.actorA);
            std::size_t h2 = std::hash<RE::FormID>{}(k.actorB);
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };

    // Structure tracking directional cheating incidents (who cheated on whom with whom)
    struct CheatingRecord {
        RE::FormID cheaterID{ 0 };        // Actor who cheated
        RE::FormID victimID{ 0 };         // Actor cheated on
        RE::FormID affairPartnerID{ 0 };  // Third party involved in cheating
        uint32_t count{ 1 };              // Number of occurrences
        int32_t lastTimestamp{ 0 };       // Game days passed of last incident
        bool isSecret{ false };           // Whether the cheating is secret or publicly known
        AffairType affairType{ AffairType::Casual };       // Type of affair (casual, dating, lover)
        AffairStatus affairStatus{ AffairStatus::Ongoing }; // Status of affair (ongoing, ended reasons, allowed)

        CheatingRecord() = default;
        CheatingRecord(RE::FormID cheater, RE::FormID victim, RE::FormID affairPartner, uint32_t c = 1, int32_t ts = 0, bool secret = false,
                       AffairType aType = AffairType::Casual, AffairStatus aStatus = AffairStatus::Ongoing)
            : cheaterID(cheater), victimID(victim), affairPartnerID(affairPartner), count(c), lastTimestamp(ts), isSecret(secret),
              affairType(aType), affairStatus(aStatus) {}
    };

    // Categorization of races into distinct phenotype tiers for paternity discovery
    enum class PhenotypeTier {
        PaleHuman,  // Nord, Imperial, Breton, Elder
        Redguard,   // Redguard (distinct dark-complexioned human)
        Mer,        // Altmer, Bosmer, Dunmer, Snow Elf, Falmer (pointed ears, elven bone structure)
        Orc,        // Orsimer / Orc (green-pigmented skin, prominent tusks, heavy brow)
        Argonian,   // Argonian / Saxhleel (reptilian scales, horns, tail)
        Khajiit,    // Khajiit (feline fur, whiskers, feline ears, tail)
        Unknown
    };


    // Intrigue state tracking secret paternity of children born from affairs or non-partner encounters
    struct PaternitySecret {
        bool isSecret{ false };                 // true = any secret active (isSecretForBioFather || isSecretForPartner)
        bool isSecretForBioFather{ false };     // true = biological father does not know he is the father
        bool isSecretForPartner{ false };       // true = partner does not know bio father != them
        bool discovered{ false };               // true = fully resolved (both know)
        bool discoveredByPartner{ false };      // true = partner found out (or was told)
        bool discoveredByBioFather{ false };    // true = bio father found out (or was told)
        bool hasPhenotypeMismatch{ false };     // true = distinct racial phenotypes between partner and bio father
        float lastCheckTimestamp{ 0.0f };       // game days when last probability check ran
        std::string bioFatherName;              // string name of biological father (if unknown or unindexed)
        RE::FormID bioFatherRaceID{ 0 };        // Race FormID of biological father (for phenotype check)

        PaternitySecret() = default;
        PaternitySecret(bool secret, bool disc = false, bool byPartner = false, bool mismatch = false, float lastCheck = 0.0f,
                        const std::string& name = "", RE::FormID raceID = 0,
                        bool secBio = false, bool secPartner = false, bool byBio = false)
            : isSecret(secret), isSecretForBioFather(secBio), isSecretForPartner(secPartner),
              discovered(disc), discoveredByPartner(byPartner), discoveredByBioFather(byBio),
              hasPhenotypeMismatch(mismatch), lastCheckTimestamp(lastCheck),
              bioFatherName(name), bioFatherRaceID(raceID) {}

        bool HasActiveSecret() const { return isSecret && (isSecretForBioFather || isSecretForPartner); }
    };

    enum class ChildStage : uint8_t {
        Unborn   = 0,  // Active gestation in mother's womb (ephemeral)
        Infant   = 1,  // Swaddled newborn item / carried baby (ephemeral)
        Child    = 2,  // Young child (unspawned record or physical Hearthfire child)
        Training = 3,  // Away from home undergoing training (ephemeral)
        Adult    = 4   // Adult follower (unsummoned record or physical adult follower)
    };

    // Structure tracking a child record in the child-centric registry
    struct ChildRegistryEntry {
        RE::FormID childID{ 0 };
        RE::FormID guardianAID{ 0 };   // Primary custodial guardian (0 = unknown/single)
        RE::FormID guardianBID{ 0 };   // Secondary custodial guardian (0 = unknown/single)
        std::string name;
        int32_t timestamp{ 0 };
        bool isAdopted{ true };
        RE::FormID bioMotherID{ 0 };   // Always female — NPC who carried/gave birth (0 for adopted)
        RE::FormID bioFatherID{ 0 };   // Always male — biological sire (0 for adopted)
        bool isBiologicalChild{ false };       // true = FMR-born, false = adopted/vanilla
        float conceptionTimestamp{ 0.0f };     // game days (from FMR)
        float birthTimestamp{ 0.0f };          // game days (from FMR)
        PaternitySecret paternitySecret;       // paternity secret state
        ChildStage stage{ ChildStage::Child }; // developmental lifecycle stage
        std::string trainingLocation;          // location name if away for training
        uint8_t gender{ 2 };                   // 0 = male (son), 1 = female (daughter), 2 = unknown/unborn

        ChildRegistryEntry() = default;
        ChildRegistryEntry(RE::FormID cID, RE::FormID gA, RE::FormID gB, const std::string& n, int32_t ts = 0, bool adopted = true,
                           RE::FormID bioM = 0, RE::FormID bioF = 0, bool bioChild = false, float concTs = 0.0f, float birthTs = 0.0f,
                           PaternitySecret pSec = {}, ChildStage st = ChildStage::Child, const std::string& trainLoc = "",
                           uint8_t gen = 2)
            : childID(cID), guardianAID(gA), guardianBID(gB), name(n), timestamp(ts), isAdopted(adopted),
              bioMotherID(bioM), bioFatherID(bioF), isBiologicalChild(bioChild),
              conceptionTimestamp(concTs), birthTimestamp(birthTs), paternitySecret(pSec),
              stage(st), trainingLocation(trainLoc), gender(gen) {}
    };

    // Structure tracking a relationship status change transition
    struct StatusChangeRecord {
        RelationshipStatus fromStatus{ RelationshipStatus::Unknown };
        RelationshipStatus toStatus{ RelationshipStatus::Unknown };
        float timestamp{ 0.0f };         // Game days passed with fractional time (e.g. 3.5 = Day 3, 12:00 PM; -1 for pre-existing)
        RE::FormID initiatorFormID{ 0 };  // Who triggered the change (e.g. player 0x14 or partner)

        StatusChangeRecord() = default;
        StatusChangeRecord(RelationshipStatus fromSt, RelationshipStatus toSt, float ts, RE::FormID initiator = 0)
            : fromStatus(fromSt), toStatus(toSt), timestamp(ts), initiatorFormID(initiator) {}
    };

    // Data structure for an NPC's profile/attributes
    struct NPCProfileData {
        RE::FormID formID;
        SocialClass socialClass;
        SkillType skillType;
        Temperament temperament;

        // Additional tracking data per NPC
        std::optional<RE::FormID> originalHome;
        std::optional<RE::FormID> currentHome;
        std::optional<RE::FormID> homeMarker;

        // Keywords added at runtime that must be re-applied on load
        std::unordered_set<RE::FormID> addedKeywords;

        NPCProfileData()
            : formID(0),
              socialClass(SocialClass::Outcast),
              skillType(SkillType::Warrior),
              temperament(Temperament::Proud) {}

        NPCProfileData(RE::FormID id, SocialClass sc, SkillType st, Temperament temp)
            : formID(id),
              socialClass(sc),
              skillType(st),
              temperament(temp) {}
    };

    // Lightweight data structure for a relationship pair between two actors
    struct NPCRelationshipData {
        RelationshipPairKey pairKey;
        RelationshipStatus status;
        RelationshipStatus originalStatus{ RelationshipStatus::Unknown };
        bool isOpen{ false };

        // Directional cheating records associated with this relationship pair
        std::vector<CheatingRecord> cheatingRecords;

        // Status change history for this relationship pair (Single Source of Truth)
        std::vector<StatusChangeRecord> statusHistory;

        // Default constructor
        NPCRelationshipData()
            : pairKey(0, 0),
              status(RelationshipStatus::Candidate),
              originalStatus(RelationshipStatus::Unknown),
              isOpen(false) {}

        // Constructor with required fields
        NPCRelationshipData(RE::FormID a, RE::FormID b, RelationshipStatus st = RelationshipStatus::Candidate)
            : pairKey(a, b),
              status(st),
              originalStatus(RelationshipStatus::Unknown),
              isOpen(false) {}
    };

    struct PendingRelationshipData {
        RE::FormID partnerBaseID{ 0 };     // Base ID (or Ref ID if resolved) of second NPC
        RE::FormID partnerRefID{ 0 };      // Ref ID of second NPC if already resolved (0 if unresolved)
        RelationshipStatus targetStatus{ RelationshipStatus::Unknown };
        RelationshipStatus originalStatus{ RelationshipStatus::Unknown };

        PendingRelationshipData() = default;
        PendingRelationshipData(RE::FormID pBase, RE::FormID pRef, RelationshipStatus targetSt, RelationshipStatus origSt)
            : partnerBaseID(pBase), partnerRefID(pRef), targetStatus(targetSt), originalStatus(origSt) {}
    };

    // Tracks a child-to-parent relationship pending resolution
    struct PendingChildData {
        RE::FormID childBaseID{ 0 };    // Base ID of the child actor
        RE::FormID childRefID{ 0 };     // Ref ID if already resolved, else 0
        RE::FormID parentABaseID{ 0 };  // Base ID of parent A
        RE::FormID parentARefID{ 0 };   // Ref ID of parent A, else 0
        RE::FormID parentBBaseID{ 0 };  // Base ID of parent B (0 if single-parent)
        RE::FormID parentBRefID{ 0 };   // Ref ID of parent B, else 0
        bool isAdopted{ true };         // true = adopted, false = biological

        PendingChildData() = default;
        PendingChildData(RE::FormID cBase, RE::FormID cRef,
                         RE::FormID pABase, RE::FormID pARef,
                         RE::FormID pBBase, RE::FormID pBRef,
                         bool adopted = true)
            : childBaseID(cBase), childRefID(cRef),
              parentABaseID(pABase), parentARefID(pARef),
              parentBBaseID(pBBase), parentBRefID(pBRef),
              isAdopted(adopted) {}
    };

    // Main storage and management class
    class NPCRelationshipManager {
    private:
        // Master set - all registered NPCs
        std::unordered_set<RE::FormID> allRegistered;

        // Base NPC pointer to FormID map for persistent reference resolution
        mutable std::unordered_map<RE::TESNPC*, RE::FormID> baseToRefIDMap;

        // Pending deferred relationships for unspawned NPCs (keyed by Base FormID)
        std::unordered_map<RE::FormID, std::vector<PendingRelationshipData>> pendingRelationshipsByBaseID;

        // Pending deferred children (keyed by child base FormID)
        std::unordered_map<RE::FormID, PendingChildData> pendingChildrenByBaseID;

        // Primary child-centric registry (keyed by child FormID)
        std::unordered_map<RE::FormID, ChildRegistryEntry> childrenByID;

        // Counter for generating unique synthetic child FormIDs for newborns
        uint32_t nextSyntheticChildID_{ 0xFF700001 };

        // NPC attribute profiles
        std::unordered_map<RE::FormID, NPCProfileData> npcProfiles;

        // Stored relationship pairs
        std::unordered_map<RelationshipPairKey, NPCRelationshipData, RelationshipPairKeyHash> relationshipPairs;

        // Per-actor adjacency index into relationshipPairs for O(degree(actor)) lookups
        std::unordered_map<RE::FormID, std::vector<RelationshipPairKey>> actorPairIndex_;

        // Override data storage
        Utils::OverrideMap npcOverrides;

        // Adjacency index & pair access helpers
        void IndexPair(const RelationshipPairKey& key);
        void UnindexPair(const RelationshipPairKey& key);
        NPCRelationshipData& GetOrCreatePairData(RE::FormID actorA, RE::FormID actorB);
        const std::vector<RelationshipPairKey>& GetActorPairKeys(RE::FormID actorID) const;
        size_t CountPartnersWithStatus(RE::FormID npcFormID, RelationshipStatus status) const;
        void RefreshPartnerRelationshipFactions(RE::FormID partnerID);

        // Private helper methods
        static RE::FormID DetermineDefaultInitiator(RE::FormID actorA, RE::FormID actorB);
        bool ChangeStatusCommon(RE::FormID npcFormID, RE::FormID targetFormID, RelationshipStatus status,
                                const std::function<void(RE::FormID, RE::FormID)>& postAction = nullptr,
                                bool sendEvent = true);

        // Helper for deduplicating and adding pending deferred relationship entries
        void AddPendingRelationship(RE::FormID baseID, const PendingRelationshipData& data);
        void AddPendingChild(RE::FormID childBaseID, const PendingChildData& data);
        void TryResolvePendingChildren(RE::FormID baseID, RE::FormID refID);

        // Helper methods for child custody evaluation on marriage and divorce
        void EvaluateChildCustodyOnMarriage(RE::FormID actorA, RE::FormID actorB = kPlayerFormID);
        void EvaluateChildCustodyOnDivorce(RE::FormID actorA, RE::FormID actorB = kPlayerFormID);

        // Faction management helper
        void UpdateTrackedFactionRank(RE::FormID npcFormID, RelationshipStatus status);

        // Event sending helper
        void SendStatusChangedEvent(RE::FormID npcFormID, RE::FormID targetFormID, RelationshipStatus status);

        // Low-level faction add helper (adds a FormID actor to a faction with rank)
        bool AddToFaction(RE::FormID npcFormID, RE::TESFaction* faction, std::int8_t rank);

        // Ensure NPC profile is registered prior to mutating operations
        bool EnsureRegistered(RE::FormID npcFormID);

        // Recalculate and update game globals for love interests and spouse counts
        void RecalculateAndUpdateGlobals();

        // Helper to look up override data by reference ID or base actor ID
        const Utils::NPCOverrideData* FindOverrideData(RE::FormID npcFormID) const;

        // Helper to set linked reference for home marker
        void SetLinkedRefForHomeMarker(RE::FormID npcFormID, RE::FormID markerFormID);

    public:
        // Singleton access
        static NPCRelationshipManager& GetSingleton();

        // Relationship logic helpers
        static bool IsMainTier(RelationshipStatus status);
        static bool IsActivePartnerStatus(RelationshipStatus status);
        static bool IsBreakupStatus(RelationshipStatus status);

        // Deferred relationship resolution
        void TryResolvePendingRelationships(RE::FormID baseID, RE::FormID refID);
        void TryResolvePendingRelationships(RE::Actor* actor);

        // Registration, unregistration, and death
        bool RegisterAsCandidate(RE::FormID npcFormID);
        bool UnregisterNPC(RE::FormID npcFormID);
        bool HandleNPCDeath(RE::FormID npcFormID);
        void EvaluateChildCustodyOnDeath(RE::FormID deceasedID);

        // Active relationship queries
        std::vector<std::pair<RE::FormID, RelationshipStatus>> GetActiveRelationships(RE::FormID actorID) const;

        // Original relationship tracking & initial scan
        void ScanExistingRelationships(bool isNewGame = false);
        bool ApplyImportedVanillaStatus(RE::FormID actorA, RE::FormID actorB, RelationshipStatus vanillaStatus, RelationshipStatus targetStatus);
        bool HasMarasMaritalHistory(RE::FormID npcFormID) const;
        RelationshipStatus GetOriginalRelationshipStatus(RE::FormID actorA, RE::FormID actorB) const;
        bool SetOriginalRelationshipStatus(RE::FormID actorA, RE::FormID actorB, RelationshipStatus status);

        // Faction management methods
        bool AddToSocialClassFaction(RE::FormID npcFormID, std::int8_t rank);
        bool AddToSkillTypeFaction(RE::FormID npcFormID, std::int8_t rank);
        bool AddToTemperamentFaction(RE::FormID npcFormID, std::int8_t rank);

        // Manage faction membership based on relationship status
        void ManageFactions(RE::FormID npcFormID, RE::FormID targetFormID, RelationshipStatus status);

        // Status checks
        bool IsRegistered(RE::FormID npcFormID) const;
        bool IsCandidate(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        bool IsEngaged(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        bool IsMarried(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        bool IsDivorced(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        bool IsMostRecentRelationshipDivorced(RE::FormID actorA, RE::FormID actorB) const;
        bool IsJilted(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        bool IsDating(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        bool IsLover(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        bool IsExDating(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        bool IsExLover(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] bool IsPlayerActiveMainPartner(RE::FormID npcFormID) const;

        // Status availability checks
        bool IsAvailableForDating(RE::FormID npcFormID) const;
        bool IsAvailableForDating(RE::FormID npcFormID, RE::FormID targetFormID) const;
        bool IsAvailableForEngaged(RE::FormID npcFormID) const;
        bool IsAvailableForEngaged(RE::FormID npcFormID, RE::FormID targetFormID) const;
        bool IsAvailableForMarried(RE::FormID npcFormID) const;
        bool IsAvailableForMarried(RE::FormID npcFormID, RE::FormID targetFormID) const;
        bool IsAvailableForLover(RE::FormID npcFormID) const;
        bool IsAvailableForLover(RE::FormID npcFormID, RE::FormID targetFormID) const;
        bool IsAvailableForDivorced(RE::FormID npcFormID) const;
        bool IsAvailableForDivorced(RE::FormID npcFormID, RE::FormID targetFormID) const;
        bool IsAvailableForJilted(RE::FormID npcFormID) const;
        bool IsAvailableForJilted(RE::FormID npcFormID, RE::FormID targetFormID) const;
        bool IsAvailableForExDating(RE::FormID npcFormID) const;
        bool IsAvailableForExDating(RE::FormID npcFormID, RE::FormID targetFormID) const;
        bool IsAvailableForExLover(RE::FormID npcFormID) const;
        bool IsAvailableForExLover(RE::FormID npcFormID, RE::FormID targetFormID) const;
        bool IsAvailableForBreakup(RE::FormID npcFormID) const;
        bool IsAvailableForBreakup(RE::FormID npcFormID, RE::FormID targetFormID) const;
        bool IsAvailableForBreakupDating(RE::FormID npcFormID) const;
        bool IsAvailableForBreakupDating(RE::FormID npcFormID, RE::FormID targetFormID) const;
        bool IsAvailableForBreakupLover(RE::FormID npcFormID) const;
        bool IsAvailableForBreakupLover(RE::FormID npcFormID, RE::FormID targetFormID) const;
        bool IsAvailableForOpenRelationship(RE::FormID npcFormID) const;
        bool IsAvailableForOpenRelationship(RE::FormID npcFormID, RE::FormID targetFormID) const;
        bool IsAvailableForCloseRelationship(RE::FormID npcFormID) const;
        bool IsAvailableForCloseRelationship(RE::FormID npcFormID, RE::FormID targetFormID) const;
        bool IsAvailableForStatus(RE::FormID npcFormID, RelationshipStatus status) const;
        bool IsAvailableForStatus(RE::FormID npcFormID, const std::string& statusName) const;
        bool IsAvailableForStatus(RE::FormID npcFormID, RE::FormID targetFormID, RelationshipStatus status) const;
        bool IsAvailableForStatus(RE::FormID npcFormID, RE::FormID targetFormID, const std::string& statusName) const;
        bool CanFormRelationship(RE::FormID actorA, RE::FormID actorB, RelationshipStatus status, std::string* outReason = nullptr) const;
        bool CanFormRelationship(RE::FormID actorA, RE::FormID actorB, const std::string& statusName, std::string* outReason = nullptr) const;
        bool AreFamilyMembers(RE::FormID actorA, RE::FormID actorB, std::string* outReason = nullptr) const;
        bool CanOpenRelationship(RE::FormID actorA, RE::FormID actorB) const;
        bool CanCloseRelationship(RE::FormID actorA, RE::FormID actorB) const;

        // Status transitions
        bool PromoteToEngaged(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID, bool sendEvent = true, bool breakLovers = true);
        bool PromoteToMarried(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID, bool sendEvent = true, bool breakLovers = true);
        bool PromoteToDivorced(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID, bool sendEvent = true);  // From married
        bool PromoteToJilted(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID, bool sendEvent = true);    // From engaged
        bool PromoteToDating(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID, bool sendEvent = true, bool breakLovers = true);
        bool PromoteToLover(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID, bool sendEvent = true);
        bool PromoteToExDating(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID, bool sendEvent = true);
        bool PromoteToExLover(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID, bool sendEvent = true);
        bool DemoteToCandidate(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID, bool sendEvent = true);
        bool Breakup(RE::FormID actorA, RE::FormID actorB, bool sendEvent = true);
        bool PromoteToStatusWith(RE::FormID npcFormID, RE::FormID targetFormID, RelationshipStatus status, bool sendEvent = true, bool breakLovers = true);
        int BreakAllLoverConnections(RE::FormID actorID, bool onlyCheating = false);

        // Bulk retrievals (defaulting to Player target)
        std::vector<RE::FormID> GetAllRegisteredNPCs() const;
        std::vector<RE::FormID> GetAllCandidates(RE::FormID targetFormID = kPlayerFormID) const;
        std::vector<RE::FormID> GetAllEngaged(RE::FormID targetFormID = kPlayerFormID) const;
        std::vector<RE::FormID> GetAllMarried(RE::FormID targetFormID = kPlayerFormID) const;
        std::vector<RE::FormID> GetAllDivorced(RE::FormID targetFormID = kPlayerFormID) const;
        std::vector<RE::FormID> GetAllJilted(RE::FormID targetFormID = kPlayerFormID) const;
        std::vector<RE::FormID> GetAllDating(RE::FormID targetFormID = kPlayerFormID) const;
        std::vector<RE::FormID> GetAllLovers(RE::FormID targetFormID = kPlayerFormID) const;
        std::vector<RE::FormID> GetAllExDating(RE::FormID targetFormID = kPlayerFormID) const;
        std::vector<RE::FormID> GetAllExLovers(RE::FormID targetFormID = kPlayerFormID) const;

        // Partner retrievals
        std::vector<RE::FormID> GetPartners(RE::FormID npcFormID, RelationshipStatus status) const;
        std::vector<RE::FormID> GetAllPartners(RE::FormID npcFormID) const;

        // Data access
        const NPCProfileData* GetNPCProfile(RE::FormID npcFormID) const;
        const NPCRelationshipData* GetNPCData(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        const std::unordered_map<RelationshipPairKey, NPCRelationshipData, RelationshipPairKeyHash>& GetAllRelationshipPairs() const { return relationshipPairs; }
        RelationshipStatus GetRelationshipStatus(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        SocialClass GetSocialClass(RE::FormID npcFormID) const;
        SkillType GetSkillType(RE::FormID npcFormID) const;
        Temperament GetTemperament(RE::FormID npcFormID) const;

        // Open relationship accessors (per-pair)
        bool IsNPCInOpenRelationship(RE::FormID actorID) const;
        bool GetPairIsOpen(RE::FormID actorA, RE::FormID actorB) const;
        bool SetPairIsOpen(RE::FormID actorA, RE::FormID actorB, bool isOpen);
        bool HasAnyOpenMainPair(RE::FormID actorID) const;

        // Cheating tracking accessors & mutators
        bool RecordCheating(RE::FormID cheaterID, RE::FormID victimID, RE::FormID affairPartnerID, bool isSecret = false,
                            AffairType affairType = AffairType::Casual, AffairStatus affairStatus = AffairStatus::Ongoing);
        bool HasCheatedOn(RE::FormID cheaterID, RE::FormID victimID) const;
        bool IsNPCCheating(RE::FormID actorID) const;
        bool IsRelationshipCheating(RE::FormID actorA, RE::FormID actorB) const;
        uint32_t GetCheatingCount(RE::FormID cheaterID, RE::FormID victimID) const;
        std::vector<RE::FormID> GetCheatedOnPartners(RE::FormID cheaterID) const;
        std::vector<RE::FormID> GetCheatersWhoCheatedOn(RE::FormID victimID) const;
        const std::vector<CheatingRecord>& GetCheatingRecords(RE::FormID actorA, RE::FormID actorB) const;
        std::vector<CheatingRecord> GetCheatingRecordsForActor(RE::FormID actorID) const;
        bool SetCheatingAffairStatus(RE::FormID cheaterID, RE::FormID victimID, RE::FormID affairPartnerID, AffairStatus status);
        AffairStatus GetCheatingAffairStatus(RE::FormID cheaterID, RE::FormID victimID, RE::FormID affairPartnerID) const;
        void MarkCheatingAllowedForPair(RE::FormID actorA, RE::FormID actorB);
        void MarkPlayerCheatingAllowedOnPolygamy();
        void EvaluateCheatingOnStatusChange(RE::FormID npcFormID, RE::FormID targetFormID, RelationshipStatus newStatus);
        bool SetCheatingDiscovered(RE::FormID cheaterID, RE::FormID affairPartnerID, AffairDiscovery discovery);
        bool SetCheatingDiscovered(RE::FormID cheaterID, RE::FormID affairPartnerID, AffairResolution resolution);
        bool SetCheatingDiscovered(RE::FormID cheaterID, RE::FormID affairPartnerID = 0, const std::string& statusOrResolution = "");
        void UpdateSpouseCheatingFaction(RE::FormID npcFormID);
        void UpdateSpouseOpenRelationshipFaction(RE::FormID npcFormID);
        void UpdateCanFormNonCheatingMainFaction(RE::FormID npcFormID);
        void UpdateAllSpousesCheatingAndOpenFactions();
        void UpdateAllCanFormNonCheatingMainFactions();

        // Children management accessors & mutators
        bool IsChildBlockedFromAdoption(RE::FormID childID, std::string* outReason = nullptr) const;
        bool RegisterChildActor(RE::FormID childID, RE::FormID guardianAID, RE::FormID guardianBID = 0, RE::FormID houseLocationID = 0,
                                RE::FormID bioMotherID = 0, RE::FormID bioFatherID = 0,
                                int32_t syntheticID = 0, uint8_t stage = 2,
                                std::optional<bool> explicitIsAdopted = std::nullopt,
                                bool sendEvent = true);
        void SendChildEvent(const std::string& eventType, RE::FormID childID, RE::FormID guardianAID, RE::FormID guardianBID = 0, RE::FormID otherID = 0, const std::string& reason = "");
        bool RemoveChild(RE::FormID childID);
        bool SetChildGuardian(RE::FormID childID, int slot /*0=A, 1=B*/, RE::FormID guardianID);
        std::optional<ChildRegistryEntry> GetChildEntry(RE::FormID childID) const;
        std::vector<RE::FormID> GetChildrenOfActor(RE::FormID parentID) const;
        std::vector<RE::FormID> GetChildren(RE::FormID guardianAID, RE::FormID guardianBID) const;
        const std::unordered_map<RE::FormID, ChildRegistryEntry>& GetAllChildren() const;
        bool IsChildNPC(RE::FormID formID) const;
        void UpdateChildBothParentsFaction(RE::FormID childID);
        void UpdateSpouseChildrenFaction(RE::FormID spouseID);
        void UpdateAllSpousesChildrenFaction();
        void SyncAllMarriedCouplesChildCustody();

        // Non-Player 0xFF Children Package & LinkedRef management
        RE::FormID ResolveNonPlayerChildTarget(const ChildRegistryEntry& entry) const;
        void SetChildSandboxAroundParentLinkedRef(RE::FormID childID, RE::FormID targetRefID);
        void RemoveChildSandboxAroundParentLinkedRef(RE::FormID childID);
        void UpdateNonPlayerChildPackageOverride(RE::FormID childID);
        void ReevaluateAllNonPlayerChildrenPackages();

        // Custodial Guardianship accessors
        RE::FormID GetGuardianA(RE::FormID childID) const;
        RE::FormID GetGuardianB(RE::FormID childID) const;
        std::pair<RE::FormID, RE::FormID> GetGuardians(RE::FormID childID) const;
        RE::FormID GetCoGuardian(RE::FormID childID, RE::FormID guardianID) const;
        bool IsGuardian(RE::FormID childID, RE::FormID actorID) const;

        // Biological and Partner lineage accessors
        RE::FormID GetBioMother(RE::FormID childID) const;
        RE::FormID GetBioFather(RE::FormID childID) const;
        RE::FormID GetMotherPartner(RE::FormID childID) const;
        bool SetBioMother(RE::FormID childID, RE::FormID motherID);
        bool SetBioFather(RE::FormID childID, RE::FormID fatherID);

        // Fertility and Pregnancy Tracking (Fertility Mode Reloaded integration)
        void SetNPCPregnant(RE::FormID motherID, bool pregnant, RE::FormID bioFatherID = 0, RE::FormID presumedFatherID = 0, float conceptionTime = 0.0f);
        bool IsNPCPregnant(RE::FormID motherID) const;
        RE::FormID RegisterUnbornChild(RE::FormID motherID, RE::FormID bioFatherID = 0, float conceptionTime = 0.0f, bool evaluateStepIn = true);
        RE::FormID PromoteUnbornToInfant(RE::FormID motherID, float birthTime = 0.0f, const std::string& name = "Newborn", RE::FormID bioFatherID = 0);
        bool RemoveUnbornChild(RE::FormID motherID, RE::FormID bioFatherID = 0);
        RE::FormID FindUnbornChild(RE::FormID motherID) const;
        std::vector<RE::FormID> GetUnbornChildren(RE::FormID motherID) const;
        RE::FormID RegisterFertilityChild(RE::FormID syntheticChildID, RE::FormID motherID, RE::FormID presumedFatherID, RE::FormID bioFatherID, float conceptionTime, float birthTime, const std::string& name);
        bool ResolvePaternitySecret(RE::FormID childID, bool discoveredByPartner);
        bool ResolvePaternitySecretForPartner(RE::FormID childID, bool discoveredByPartner, bool sendEvent = true);
        bool ResolvePaternitySecretForBioFather(RE::FormID childID, bool discoveredByBioFather, bool sendEvent = true);
        bool SetPaternitySecretEdges(RE::FormID childID, bool secretForBioFather, bool secretForPartner);
        bool SetChildPaternityLastCheck(RE::FormID childID, float currentDay);
        bool EvaluatePaternitySecret(RE::FormID childID);
        bool EvaluatePaternitySecretFallback(RE::FormID childID);
        bool ApplyBioFatherStepIn(RE::FormID motherID, RE::FormID bioFatherID);
        void EvaluateBioFatherStepIn(RE::FormID motherID, RE::FormID formerSpouseID = 0);
        bool IsSecretForBioFather(RE::FormID childID) const;
        bool IsSecretForPartner(RE::FormID childID) const;
        bool SetChildStage(RE::FormID childID, ChildStage stage);
        ChildStage GetChildStage(RE::FormID childID) const;
        bool SetChildTrainingLocation(RE::FormID childID, const std::string& location);
        std::string GetChildTrainingLocation(RE::FormID childID) const;
        bool SetChildGender(RE::FormID childID, uint8_t gender);
        uint8_t GetChildGender(RE::FormID childID) const;
        RE::FormID FindSyntheticChild(RE::FormID motherID, const std::string& name = "") const;
        bool UpdateChildName(RE::FormID childID, const std::string& newName);
        std::vector<RE::FormID> GetChildrenWithActivePaternitySecrets() const;
        PhenotypeTier GetActorPhenotypeTier(RE::Actor* actor) const;
        PhenotypeTier GetFormIDPhenotypeTier(RE::FormID formID) const;
        bool HasPhenotypeMismatch(RE::FormID father1ID, RE::FormID father2ID) const;
        std::string GetVisualDiscrepancyDescription(RE::FormID motherID, RE::FormID presumedFatherID, RE::FormID bioFatherID) const;
        RE::FormID GetPrimaryPartner(RE::FormID actorID) const;
        std::string GetChildName(RE::FormID childID) const;
        RE::FormID FindChildPaternitySecret(RE::FormID parent1ID, RE::FormID parent2ID = 0, const std::string& childName = "") const;
        std::vector<RE::FormID> GetChildrenWithActiveSecretsForActor(RE::FormID actorID) const;
        void CheckPaternitySecretsDailyTick(float currentDay);
        void CheckAutoMarriageForEngagedNPCs(float currentDay);
        float GetEngagementTimestamp(RE::FormID actorA, RE::FormID actorB) const;

        // Pregnancy Announcement Message Construction
        std::string BuildPregnancyAnnouncedMessage(RE::FormID motherID, RE::FormID partnerID = 0, RE::FormID fallbackBioID = 0, const std::string& fallbackBioName = "", int32_t childCount = 1) const;
        std::string BuildPregnancyAnnouncedJson(RE::FormID motherID, RE::FormID partnerID = 0, RE::FormID fallbackBioID = 0, const std::string& fallbackBioName = "", int32_t childCount = 1) const;

        // Child Birth Message Construction (grouped by biological father)
        std::string BuildChildBirthMessage(RE::FormID motherID, const std::vector<RE::FormID>& childIDs = {}) const;
        std::string BuildChildBirthJson(RE::FormID motherID, const std::vector<RE::FormID>& childIDs = {}) const;

        // Status change history management
        const std::vector<StatusChangeRecord>& GetStatusHistory(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        std::optional<StatusChangeRecord> GetStatusHistoryEntry(RE::FormID npcFormID, RE::FormID targetFormID, std::size_t index) const;
        size_t GetStatusHistoryCount(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        void ClearStatusHistory(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID);

        // Home marker management (replaces PO3's SetLinkedRef and StorageUtil)
        bool SetHomeMarker(RE::FormID npcFormID, RE::FormID markerFormID);
        std::optional<RE::FormID> GetHomeMarker(RE::FormID npcFormID) const;
        RE::TESObjectREFR* GetOrCreateHomeMarker(RE::Actor* npcActor);

        // Keyword persistence — track keywords added at runtime so they survive reloads
        bool AddTrackedKeyword(RE::FormID npcFormID, RE::FormID keywordFormID);
        bool RemoveTrackedKeyword(RE::FormID npcFormID, RE::FormID keywordFormID);

        // Type determination methods (with override support)
        SocialClass DetermineSocialClass(RE::FormID npcFormID);
        SkillType DetermineSkillType(RE::FormID npcFormID);
        Temperament DetermineTemperament(RE::FormID npcFormID);

        // Methods to programmatically change attributes (also update factions/derived types)
        bool SetSocialClass(RE::FormID npcFormID, std::int8_t socialClass);
        bool SetSkillType(RE::FormID npcFormID, std::int8_t skillType);
        bool SetTemperament(RE::FormID npcFormID, std::int8_t temperament);

        // Override management
        bool LoadOverridesFromFolder(const std::string& folderPath);
        bool HasSocialClassOverride(RE::FormID npcFormID) const;
        bool HasSkillTypeOverride(RE::FormID npcFormID) const;
        bool HasTemperamentOverride(RE::FormID npcFormID) const;
        std::optional<std::string> GetSocialClassOverride(RE::FormID npcFormID) const;
        std::optional<std::string> GetSkillTypeOverride(RE::FormID npcFormID) const;
        std::optional<std::string> GetTemperamentOverride(RE::FormID npcFormID) const;

        // Override statistics
        size_t GetOverrideCount() const;
        Utils::JsonOverrideLoader::LoadStatistics GetLastOverrideLoadStats() const;

        // Statistics
        size_t GetTotalRegisteredCount() const;
        size_t GetCandidateCount(RE::FormID targetFormID = kPlayerFormID) const;
        size_t GetEngagedCount(RE::FormID targetFormID = kPlayerFormID) const;
        size_t GetMarriedCount(RE::FormID targetFormID = kPlayerFormID) const;
        size_t GetDivorcedCount(RE::FormID targetFormID = kPlayerFormID) const;
        size_t GetJiltedCount(RE::FormID targetFormID = kPlayerFormID) const;
        size_t GetExDatingCount(RE::FormID targetFormID = kPlayerFormID) const;
        size_t GetExLoverCount(RE::FormID targetFormID = kPlayerFormID) const;

        // Save/Load support
        void Clear();

        // SKSE serialization support
        bool Save(SKSE::SerializationInterface* serialization) const;
        bool Load(SKSE::SerializationInterface* serialization, std::uint32_t version, std::uint32_t length = 0);
        void Revert();

        // Data dump
        [[nodiscard]] nlohmann::json DumpJson() const;

        // Debug/logging
        void LogStatistics() const;
        void LogNPCDetails(RE::FormID npcFormID) const;
    };

}  // namespace MARAS
