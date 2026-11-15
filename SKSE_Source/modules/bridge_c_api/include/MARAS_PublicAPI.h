#pragma once

// MARAS Public API
// ================
// Exported C functions for use by companion DLLs (e.g. MARASUnleashed.dll).
// All functions are resolved at runtime via GetProcAddress — no link-time
// dependency on MARAS.dll is required or expected.
//
// Call order: these functions are safe to call from kDataLoaded onwards,
// once MARAS has registered its own kDataLoaded listener and all services
// are initialised.

#include <cstdint>

// Re-export the Temperament enum values so callers don't need to include
// DomainEnums.h.  Values 0..4 are compile-time verified against
// MARAS::Foundation::Temperament in MARAS_PublicAPI.cpp; Unknown (255) is a
// caller-side sentinel and is never returned by MARAS_GetTemperament.
enum class MARAS_Temperament : std::uint8_t {
    Proud = 0,
    Humble = 1,
    Jealous = 2,
    Romantic = 3,
    Independent = 4,
    Unknown = 255
};

extern "C" {

// ============================================================
// Affection Queries
// ============================================================

// Returns permanent affection [0-100] for the given NPC, or 50 if not found.
__declspec(dllexport) int MARAS_GetPermanentAffection(std::uint32_t npcFormID);

// Returns days since the NPC last received an affection interaction.
// Returns -1.0f if the NPC has never received one.
__declspec(dllexport) float MARAS_GetDaysSinceLastAffection(std::uint32_t npcFormID);

// ============================================================
// Relationship & History Queries (Dashboard Perspective-Masked)
// ============================================================

// Fills buf with up to maxCount married spouse FormIDs.
// Returns the actual total count of married spouses (may exceed maxCount).
__declspec(dllexport) std::uint32_t MARAS_GetAllMarried(std::uint32_t* buf, std::uint32_t maxCount);

// Fills buf with up to maxCount engaged spouse FormIDs.
// Returns the actual total count of engaged spouses (may exceed maxCount).
__declspec(dllexport) std::uint32_t MARAS_GetAllEngaged(std::uint32_t* buf, std::uint32_t maxCount);

// Returns the Temperament [Proud(0)..Independent(4)] for the given NPC.
// Reads WorldStore::Snapshot() ActorProfileTable, falling back to DeceasedTable
// if the NPC is deceased, or MARAS_Temperament::Proud (0) if neither holds them.
__declspec(dllexport) MARAS_Temperament MARAS_GetTemperament(std::uint32_t npcFormID);

// Returns the count of status history entries for the given pair (targetFormID = 0x14 for player).
// Filtered through PerspectiveView::ForDashboard so undiscovered secret affairs are masked
// when Intrigue Mode (!revealSecretsInUI) is active.
__declspec(dllexport) std::uint32_t MARAS_GetStatusHistoryCount(std::uint32_t npcFormID, std::uint32_t targetFormID);

// Fills out parameters for the status history entry at index for the given pair.
// Filtered through PerspectiveView::ForDashboard alongside MARAS_GetStatusHistoryCount.
// outTimestamp receives fractional game days passed (or -1.0f for pre-existing relationships).
// Returns true if index is valid, false otherwise.
__declspec(dllexport) bool MARAS_GetStatusHistoryEntry(std::uint32_t npcFormID,
                                                       std::uint32_t targetFormID,
                                                       std::uint32_t index,
                                                       std::uint8_t* outFromStatus,
                                                       std::uint8_t* outToStatus,
                                                       float* outTimestamp,
                                                       std::uint32_t* outInitiatorFormID);

// ============================================================
// Household Queries
// ============================================================

// Returns the FormID of the player house the given tenant is assigned to, or 0.
__declspec(dllexport) std::uint32_t MARAS_GetTenantHouseFormID(std::uint32_t tenantFormID);

// Fills buf with up to maxCount tenant FormIDs for the given player house.
// Returns the actual total count of tenants (may exceed maxCount).
__declspec(dllexport) std::uint32_t MARAS_GetPlayerHouseTenants(std::uint32_t houseFormID,
                                                                 std::uint32_t* buf,
                                                                 std::uint32_t maxCount);

// ============================================================
// Spatial Teammate Queries
// ============================================================

// Fills buf with up to maxCount FormIDs of the player's current teammates from
// SpatialSnapshotStore::Snapshot() (refreshed by PollingService).
// Returns the actual total count of teammates (may exceed maxCount).
__declspec(dllexport) std::uint32_t MARAS_GetCurrentTeammates(std::uint32_t* buf, std::uint32_t maxCount);

}  // extern "C"
