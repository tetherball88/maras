#pragma once

#include "PCH.h"

namespace MARAS {
    namespace Serialization {
        // Plugin unique ID for SKSE serialization (must be exactly 4 characters)
        constexpr std::uint32_t kMarasPluginID = 'MARS';

        // Current version of our serialization format
        // Version 1: Original format (released)
        // Version 2: Added homeMarker field to NPCRelationshipData (released)
        // Version 3: Added addedKeywords set to NPCRelationshipData (released)
        // Version 4: Unified format: NPC-NPC relationship pairs, status history, cheating records,
        //            pending deferred relationships & children, child-centric registry (lifecycle,
        //            fertility, paternity secrets, neutral guardians), and tenant stay-at-home preference.
        // Version 5: Added presumedFatherID to ChildRegistryEntry in NPCRelationshipData.
        constexpr std::uint32_t kDataVersion = 5;

        // Top-level SKSE co-save record types (11 records).
        // Note: Status history, pending deferred relationships, pending deferred children, and the
        // child-centric registry are serialized inline within kNPCRelationshipData ('NPCR').
        constexpr std::uint32_t kNPCRelationshipData = 'NPCR';
        // Spouse hierarchy data record
        constexpr std::uint32_t kSpouseHierarchyData = 'SPHR';
        // Affection system persistent data
        constexpr std::uint32_t kAffectionData = 'AFCT';
        // Player house persistent data
        constexpr std::uint32_t kPlayerHouseData = 'PHOU';
        // Spouse assets persistent data
        constexpr std::uint32_t kSpouseAssetsData = 'SPAS';
        // Player children runtime service data
        constexpr std::uint32_t kPlayerChildrenData = 'PLCH';
        // Child faction snapshots persistent data
        constexpr std::uint32_t kChildFactionSnapshotData = 'CFSN';
        // Matchmaking service persistent data
        constexpr std::uint32_t kMatchmakingData = 'MKMK';
        // Deceased registry persistent data
        constexpr std::uint32_t kDeceasedRegistryData = 'DCSD';
        // Banter coordinator persistent data
        constexpr std::uint32_t kBanterData = 'BNTR';
        // Plugin settings (persistable global settings such as log level)
        constexpr std::uint32_t kPluginSettingsData = 'CNFG';

        // Magic number to validate data integrity
        constexpr std::uint32_t kMagicNumber = 0xDEADBEEF;

        // SKSE Co-Save Serialization Callbacks
        void SaveCallback(SKSE::SerializationInterface* serialization);
        void LoadCallback(SKSE::SerializationInterface* serialization);
        void RevertCallback(SKSE::SerializationInterface* serialization);
    }
}
