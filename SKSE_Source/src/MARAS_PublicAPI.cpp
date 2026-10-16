#include "MARAS_PublicAPI.h"

#include "core/AffectionService.h"
#include "core/NPCRelationshipManager.h"
#include "core/PlayerHouseService.h"
#include "core/PollingService.h"
#include "utils/Common.h"

namespace {
    static_assert(static_cast<std::uint8_t>(MARAS_Temperament::Proud) == static_cast<std::uint8_t>(MARAS::Temperament::Proud),
                  "MARAS_Temperament::Proud must match MARAS::Temperament::Proud");
    static_assert(static_cast<std::uint8_t>(MARAS_Temperament::Humble) == static_cast<std::uint8_t>(MARAS::Temperament::Humble),
                  "MARAS_Temperament::Humble must match MARAS::Temperament::Humble");
    static_assert(static_cast<std::uint8_t>(MARAS_Temperament::Jealous) == static_cast<std::uint8_t>(MARAS::Temperament::Jealous),
                  "MARAS_Temperament::Jealous must match MARAS::Temperament::Jealous");
    static_assert(static_cast<std::uint8_t>(MARAS_Temperament::Romantic) == static_cast<std::uint8_t>(MARAS::Temperament::Romantic),
                  "MARAS_Temperament::Romantic must match MARAS::Temperament::Romantic");
    static_assert(static_cast<std::uint8_t>(MARAS_Temperament::Independent) == static_cast<std::uint8_t>(MARAS::Temperament::Independent),
                  "MARAS_Temperament::Independent must match MARAS::Temperament::Independent");

    template <typename Range>
    std::uint32_t CopyRangeToBuffer(const Range& src, std::uint32_t* buf, std::uint32_t maxCount) {
        const auto total = static_cast<std::uint32_t>(src.size());
        if (buf && maxCount > 0) {
            std::uint32_t i = 0;
            for (const auto id : src) {
                if (i >= maxCount) {
                    break;
                }
                buf[i++] = id;
            }
        }
        return total;
    }
}  // namespace

// ============================================================
// AffectionService
// ============================================================

extern "C" __declspec(dllexport) int MARAS_GetPermanentAffection(std::uint32_t npcFormID) {
    return MARAS::AffectionService::GetSingleton().GetPermanentAffection(npcFormID);
}

extern "C" __declspec(dllexport) float MARAS_GetDaysSinceLastAffection(std::uint32_t npcFormID) {
    return MARAS::AffectionService::GetSingleton().GetDaysSinceLastAffection(npcFormID);
}

// ============================================================
// NPCRelationshipManager
// ============================================================

extern "C" __declspec(dllexport) std::uint32_t MARAS_GetAllMarried(std::uint32_t* buf, std::uint32_t maxCount) {
    return CopyRangeToBuffer(MARAS::NPCRelationshipManager::GetSingleton().GetAllMarried(), buf, maxCount);
}

extern "C" __declspec(dllexport) std::uint32_t MARAS_GetAllEngaged(std::uint32_t* buf, std::uint32_t maxCount) {
    return CopyRangeToBuffer(MARAS::NPCRelationshipManager::GetSingleton().GetAllEngaged(), buf, maxCount);
}

extern "C" __declspec(dllexport) MARAS_Temperament MARAS_GetTemperament(std::uint32_t npcFormID) {
    auto t = MARAS::NPCRelationshipManager::GetSingleton().GetTemperament(npcFormID);
    return static_cast<MARAS_Temperament>(static_cast<std::uint8_t>(t));
}

extern "C" __declspec(dllexport) std::uint32_t MARAS_GetStatusHistoryCount(std::uint32_t npcFormID, std::uint32_t targetFormID) {
    return static_cast<std::uint32_t>(MARAS::NPCRelationshipManager::GetSingleton().GetStatusHistoryCount(npcFormID, targetFormID));
}

extern "C" __declspec(dllexport) bool MARAS_GetStatusHistoryEntry(std::uint32_t npcFormID,
                                                                   std::uint32_t targetFormID,
                                                                   std::uint32_t index,
                                                                   std::uint8_t* outFromStatus,
                                                                   std::uint8_t* outToStatus,
                                                                   float* outTimestamp,
                                                                   std::uint32_t* outInitiatorFormID) {
    auto entryOpt = MARAS::NPCRelationshipManager::GetSingleton().GetStatusHistoryEntry(npcFormID, targetFormID, index);
    if (!entryOpt.has_value()) {
        return false;
    }
    const auto& entry = *entryOpt;
    if (outFromStatus) *outFromStatus = static_cast<std::uint8_t>(entry.fromStatus);
    if (outToStatus) *outToStatus = static_cast<std::uint8_t>(entry.toStatus);
    if (outTimestamp) *outTimestamp = entry.timestamp;
    if (outInitiatorFormID) *outInitiatorFormID = entry.initiatorFormID;
    return true;
}

// ============================================================
// PlayerHouseService
// ============================================================

extern "C" __declspec(dllexport) std::uint32_t MARAS_GetTenantHouseFormID(std::uint32_t tenantFormID) {
    return MARAS::PlayerHouseService::GetSingleton().GetTenantHouseFormID(tenantFormID);
}

extern "C" __declspec(dllexport) std::uint32_t MARAS_GetPlayerHouseTenants(std::uint32_t houseFormID,
                                                                             std::uint32_t* buf,
                                                                             std::uint32_t maxCount) {
    return CopyRangeToBuffer(MARAS::PlayerHouseService::GetSingleton().GetPlayerHouseTenants(houseFormID), buf, maxCount);
}

// ============================================================
// PollingService
// ============================================================

extern "C" __declspec(dllexport) std::uint32_t MARAS_GetCurrentTeammates(std::uint32_t* buf, std::uint32_t maxCount) {
    return CopyRangeToBuffer(MARAS::PollingService::GetSingleton().GetCurrentTeammates(), buf, maxCount);
}
