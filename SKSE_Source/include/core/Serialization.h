#pragma once

#include <cstdint>

namespace MARAS {
    namespace Serialization {
        // Plugin unique ID for SKSE serialization (must be exactly 4 characters)
        constexpr std::uint32_t kMarasPluginID = 'MARS';

        // Current version of our serialization format
        constexpr std::uint32_t kDataVersion = 1;

        // Record types for different data chunks
        constexpr std::uint32_t kNPCRelationshipData = 'NPCR';
        // Spouse hierarchy data record
        constexpr std::uint32_t kSpouseHierarchyData = 'SPHR';
        // Affection system persistent data
        constexpr std::uint32_t kAffectionData = 'AFCT';
        // Player house persistent data
        constexpr std::uint32_t kPlayerHouseData = 'PHOU';
        // Spouse assets persistent data
        constexpr std::uint32_t kSpouseAssetsData = 'SPAS';

        // Plugin settings (persistable global settings such as log level)
        constexpr std::uint32_t kPluginSettingsData = 'CNFG';

        // Magic number to validate data integrity
        constexpr std::uint32_t kMagicNumber = 0xDEADBEEF;
    }
}
