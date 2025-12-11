#pragma once

#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "RE/Skyrim.h"
#include "core/Serialization.h"
#include "utils/Common.h"
#include "utils/JsonOverrideLoader.h"

// Forward declare SKSE interface
namespace SKSE {
    class SerializationInterface;
}

namespace MARAS {

    // Enums for NPC attributes
    enum class SocialClass : uint8_t {
        Outcast = 0,
        Poverty = 1,
        Working = 2,
        Middle = 3,
        Wealthy = 4,
        Religious = 5,
        Nobles = 6,
        Rulers = 7,
        // Sentinel for range checking (not a valid value)
        _Count
    };

    enum class SkillType : uint8_t {
        Warrior = 0,
        Mage = 1,
        Rogue = 2,
        Craftsman = 3,
        Ranger = 4,
        Orator = 5,
        // Sentinel for range checking (not a valid value)
        _Count
    };

    enum class Temperament : uint8_t {
        Proud = 0,
        Humble = 1,
        Jealous = 2,
        Romantic = 3,
        Independent = 4,
        // Sentinel for range checking (not a valid value)
        _Count
    };

    enum class RelationshipStatus : uint8_t {
        Candidate = 0,
        Engaged = 1,
        Married = 2,
        Divorced = 3,
        Jilted = 4,
        Deceased = 5,
        // Unknown is used as a default/absent value when no stored data exists
        Unknown = 255
    };

    // Compile-time validation: ensure enums fit in uint8_t for serialization
    static_assert(static_cast<uint8_t>(SocialClass::_Count) <= 255,
                  "SocialClass enum exceeds uint8_t range");
    static_assert(static_cast<uint8_t>(SkillType::_Count) <= 255,
                  "SkillType enum exceeds uint8_t range");
    static_assert(static_cast<uint8_t>(Temperament::_Count) <= 255,
                  "Temperament enum exceeds uint8_t range");
    static_assert(static_cast<uint8_t>(RelationshipStatus::Deceased) < 255,
                  "RelationshipStatus valid values must be less than Unknown sentinel");

    // Lightweight data structure for NPC information
    struct NPCRelationshipData {
        RE::FormID formID;
        SocialClass socialClass;
        SkillType skillType;
        Temperament temperament;
        RelationshipStatus status;

        // Additional tracking data
        std::optional<RE::FormID> originalHome;
        std::optional<RE::FormID> currentHome;
        uint32_t engagementDate;  // Game days since start
        uint32_t marriageDate;    // Game days since start

        // Default constructor
        NPCRelationshipData()
            : formID(0),
              socialClass(SocialClass::Outcast),
              skillType(SkillType::Warrior),
              temperament(Temperament::Proud),
              status(RelationshipStatus::Candidate),
              engagementDate(0),
              marriageDate(0) {}

        // Constructor with required fields
        NPCRelationshipData(RE::FormID id, SocialClass sc, SkillType st, Temperament temp)
            : formID(id),
              socialClass(sc),
              skillType(st),
              temperament(temp),
              status(RelationshipStatus::Candidate),
              engagementDate(0),
              marriageDate(0) {}
    };

    // Main storage and management class
    class NPCRelationshipManager {
    private:
        // Master set - all registered NPCs
        std::unordered_set<RE::FormID> allRegistered;

        // Status buckets for fast filtering
        std::unordered_set<RE::FormID> candidates;
        std::unordered_set<RE::FormID> engaged;
        std::unordered_set<RE::FormID> married;
        std::unordered_set<RE::FormID> divorced;
        std::unordered_set<RE::FormID> jilted;
        std::unordered_set<RE::FormID> deceased;

        // Detailed data storage
        std::unordered_map<RE::FormID, NPCRelationshipData> npcData;

        // Override data storage
        Utils::OverrideMap npcOverrides;

        // Private helper methods
        void RemoveFromAllBuckets(RE::FormID npcFormID);
        void AddToBucket(RE::FormID npcFormID, RelationshipStatus status);
        void RemoveFromBucket(RE::FormID npcFormID, RelationshipStatus status);

        // Centralized status-change helper to reduce duplication in Promote*/Demote methods
        bool ChangeStatusCommon(RE::FormID npcFormID, RelationshipStatus status,
                                const std::function<void(RE::FormID)>& postAction = nullptr);

        // Faction management helper
        void UpdateTrackedFactionRank(RE::FormID npcFormID, RelationshipStatus status);

        // Event sending helper
        void SendStatusChangedEvent(RE::FormID npcFormID, RelationshipStatus status);

        // Low-level faction add helper (adds a FormID actor to a faction with rank)
        bool AddToFaction(RE::FormID npcFormID, RE::TESFaction* faction, std::int8_t rank);

        // Ensure NPC is registered prior to mutating operations
        bool EnsureRegistered(RE::FormID npcFormID);

        // Recalculate and update game globals for love interests and spouse counts
        void RecalculateAndUpdateGlobals();

        // Helper to look up override data by reference ID or base actor ID
        const Utils::NPCOverrideData* FindOverrideData(RE::FormID npcFormID) const;

    public:
        // Singleton access
        static NPCRelationshipManager& GetSingleton();

        // Registration and unregistration
        bool RegisterAsCandidate(RE::FormID npcFormID);  // New method with auto-determination
        bool UnregisterNPC(RE::FormID npcFormID);

        // Faction management methods
        bool AddToSocialClassFaction(RE::FormID npcFormID, std::int8_t rank);
        bool AddToSkillTypeFaction(RE::FormID npcFormID, std::int8_t rank);
        bool AddToTemperamentFaction(RE::FormID npcFormID, std::int8_t rank);

        // Manage faction membership based on relationship status
        void ManageFactions(RE::FormID npcFormID, RelationshipStatus status);

        // Status checks
        bool IsRegistered(RE::FormID npcFormID) const;
        bool IsCandidate(RE::FormID npcFormID) const;
        bool IsEngaged(RE::FormID npcFormID) const;
        bool IsMarried(RE::FormID npcFormID) const;
        bool IsDivorced(RE::FormID npcFormID) const;
        bool IsJilted(RE::FormID npcFormID) const;
        bool IsDeceased(RE::FormID npcFormID) const;

        // Status transitions
        bool PromoteToEngaged(RE::FormID npcFormID);
        bool PromoteToMarried(RE::FormID npcFormID);
        bool PromoteToDivorced(RE::FormID npcFormID);  // From married
        bool PromoteToJilted(RE::FormID npcFormID);    // From engaged
        bool PromoteToDeceased(RE::FormID npcFormID);  // From any status

        // Bulk retrievals
        std::vector<RE::FormID> GetAllRegisteredNPCs() const;
        std::vector<RE::FormID> GetAllCandidates() const;
        std::vector<RE::FormID> GetAllEngaged() const;
        std::vector<RE::FormID> GetAllMarried() const;
        std::vector<RE::FormID> GetAllDivorced() const;
        std::vector<RE::FormID> GetAllJilted() const;
        std::vector<RE::FormID> GetAllDeceased() const;

        // Data access
        const NPCRelationshipData* GetNPCData(RE::FormID npcFormID) const;
        RelationshipStatus GetRelationshipStatus(RE::FormID npcFormID) const;
        SocialClass GetSocialClass(RE::FormID npcFormID) const;
        SkillType GetSkillType(RE::FormID npcFormID) const;
        Temperament GetTemperament(RE::FormID npcFormID) const;

        // Home management
        bool SetOriginalHome(RE::FormID npcFormID, RE::FormID homeFormID);
        bool SetCurrentHome(RE::FormID npcFormID, RE::FormID homeFormID);
        std::optional<RE::FormID> GetOriginalHome(RE::FormID npcFormID) const;
        std::optional<RE::FormID> GetCurrentHome(RE::FormID npcFormID) const;

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
        size_t GetCandidateCount() const;
        size_t GetEngagedCount() const;
        size_t GetMarriedCount() const;
        size_t GetDivorcedCount() const;
        size_t GetJiltedCount() const;
        size_t GetDeceasedCount() const;

        // Save/Load support
        void Clear();

        // SKSE serialization support
        bool Save(SKSE::SerializationInterface* serialization) const;
        bool Load(SKSE::SerializationInterface* serialization);
        void Revert();

        // Debug/logging
        void LogStatistics() const;
        void LogNPCDetails(RE::FormID npcFormID) const;
    };

}  // namespace MARAS
