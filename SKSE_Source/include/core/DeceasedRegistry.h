#pragma once

#include "core/NPCRelationshipManager.h"

namespace MARAS {

    struct DeceasedPartnerSnapshot {
        RE::FormID partnerBaseID{ 0 };
        RE::FormID partnerRefID{ 0 };
        std::string partnerName;
        RelationshipStatus statusAtDeath{ RelationshipStatus::Unknown };
        bool isPlayer{ false };

        DeceasedPartnerSnapshot() = default;
        DeceasedPartnerSnapshot(RE::FormID baseID, RE::FormID refID, const std::string& name,
                                RelationshipStatus st, bool player = false)
            : partnerBaseID(baseID), partnerRefID(refID), partnerName(name),
              statusAtDeath(st), isPlayer(player) {}
    };

    struct DeceasedChildInfo {
        RE::FormID guardianABaseID{ 0 };
        RE::FormID guardianARefID{ 0 };
        std::string guardianAName;
        RE::FormID guardianBBaseID{ 0 };
        RE::FormID guardianBRefID{ 0 };
        std::string guardianBName;
        RE::FormID bioMotherBaseID{ 0 };
        RE::FormID bioFatherBaseID{ 0 };
        bool isAdopted{ true };
        RE::FormID houseFormID{ 0 };

        DeceasedChildInfo() = default;
    };

    struct DeceasedNPCRecord {
        RE::FormID baseFormID{ 0 };
        RE::FormID refFormID{ 0 };
        std::string name;
        float deathTimestamp{ 0.0f }; // in-game day (Calendar::GetDaysPassed)
        bool isChild{ false };
        bool wasKilledByPlayer{ false };
        RE::FormID killerBaseID{ 0 };
        RE::FormID killerRefID{ 0 };
        std::string killerName;

        // Adult data (if isChild == false)
        SocialClass socialClass{ SocialClass::Outcast };
        SkillType skillType{ SkillType::Warrior };
        Temperament temperament{ Temperament::Proud };
        std::vector<DeceasedPartnerSnapshot> partnersAtDeath;

        // Child lineage data (present for young children and graduated Stage 4 adult children)
        std::optional<DeceasedChildInfo> childInfo;

        DeceasedNPCRecord() = default;
    };

    /**
     * DeceasedRegistry - Single Source of Truth for deceased MARAS NPCs.
     *
     * Unlike unregistration (which is an administrative removal of a living NPC),
     * death archives a permanent historical snapshot of an NPC and their active
     * relationships at the moment of death, indexed permanently by Base FormID
     * (or unique 0xFF Reference FormID for dynamically spawned runtime actors).
     */
    class DeceasedRegistry {
    public:
        static DeceasedRegistry& GetSingleton();

        // Death registration
        bool RecordDeceasedAdult(RE::Actor* victim, bool killedByPlayer, RE::Actor* killer = nullptr,
                                 const ChildRegistryEntry* adultChildEntry = nullptr);
        bool RecordDeceasedChild(RE::Actor* child, const ChildRegistryEntry& entry, bool killedByPlayer, RE::Actor* killer = nullptr);

        // Queries
        bool IsDeceased(RE::FormID formID) const;
        bool IsWidowOrWidower(RE::FormID actorFormID) const;
        bool WasMarriedTo(RE::FormID deceasedFormID, RE::FormID partnerFormID) const;
        bool WasEngagedTo(RE::FormID deceasedFormID, RE::FormID partnerFormID) const;
        bool WasExOf(RE::FormID deceasedFormID, RE::FormID partnerFormID) const;
        std::vector<DeceasedNPCRecord> GetDeceasedSpousesOf(RE::FormID actorFormID) const;
        std::vector<DeceasedNPCRecord> GetDeceasedPartnersOf(RE::FormID actorFormID) const;
        std::vector<DeceasedNPCRecord> GetDeceasedChildrenOf(RE::FormID parentFormID) const;
        bool HasDeceasedChildren(RE::FormID parentFormID) const;
        std::optional<DeceasedNPCRecord> FindRecord(RE::FormID formID) const;
        std::unordered_map<RE::FormID, DeceasedNPCRecord> GetAllRecords() const;
        std::size_t GetDeceasedCount() const;

        // Runtime revert
        void Revert();

        // Data dump
        [[nodiscard]] nlohmann::json DumpJson() const;

    private:
        DeceasedRegistry() = default;
        ~DeceasedRegistry() = default;
        DeceasedRegistry(const DeceasedRegistry&) = delete;
        DeceasedRegistry(DeceasedRegistry&&) = delete;
        DeceasedRegistry& operator=(const DeceasedRegistry&) = delete;
        DeceasedRegistry& operator=(DeceasedRegistry&&) = delete;

        void ResolveFormIDs(RE::FormID inputFormID, RE::FormID& outBaseID, RE::FormID& outRefID) const;
    };

}  // namespace MARAS
