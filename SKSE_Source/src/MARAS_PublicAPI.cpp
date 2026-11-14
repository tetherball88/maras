#include "MARAS_PublicAPI.h"

#include "core/NPCRelationshipManager.h"
#include "core/PerspectiveEngineAdapters.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"
#include "utils/Common.h"

#include <algorithm>
#include <vector>

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

    template <typename StrongIdRange>
    std::uint32_t CopyStrongIdsToBuffer(const StrongIdRange& src, std::uint32_t* buf, std::uint32_t maxCount) {
        const auto total = static_cast<std::uint32_t>(src.size());
        if (buf && maxCount > 0) {
            std::uint32_t i = 0;
            for (const auto& id : src) {
                if (i >= maxCount) {
                    break;
                }
                buf[i++] = id.raw();
            }
        }
        return total;
    }
}  // namespace

// ============================================================
// Affection Queries
// ============================================================

extern "C" __declspec(dllexport) int MARAS_GetPermanentAffection(std::uint32_t npcFormID) {
    const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
    if (!worldSnap || !worldSnap->affection || npcFormID == 0) {
        return 50;
    }
    return worldSnap->affection->GetPermanent(MARAS::State::ActorId{ npcFormID }, 50);
}

extern "C" __declspec(dllexport) float MARAS_GetDaysSinceLastAffection(std::uint32_t npcFormID) {
    const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
    if (!worldSnap || !worldSnap->affection || npcFormID == 0) {
        return -1.0f;
    }
    const auto lastDayOpt = worldSnap->affection->GetLastInteractionDay(MARAS::State::ActorId{ npcFormID });
    if (!lastDayOpt.has_value() || *lastDayOpt < 0.0f) {
        return -1.0f;
    }

    const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
    float currentGameDay = (spatialSnap && spatialSnap->CurrentGameDay() >= 0.0f) ? spatialSnap->CurrentGameDay() : -1.0f;
    if (currentGameDay < 0.0f) {
        if (auto* calendar = RE::Calendar::GetSingleton()) {
            currentGameDay = calendar->GetDaysPassed();
        } else {
            return -1.0f;
        }
    }
    return std::max(0.0f, currentGameDay - *lastDayOpt);
}

// ============================================================
// Relationship & History Queries (Dashboard Perspective-Masked)
// ============================================================

extern "C" __declspec(dllexport) std::uint32_t MARAS_GetAllMarried(std::uint32_t* buf, std::uint32_t maxCount) {
    const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
    if (!worldSnap || !worldSnap->relationships) {
        return 0;
    }
    const auto partners = worldSnap->relationships->GetPartnersWithStatus(
        MARAS::State::ActorId::Player(), MARAS::State::RelationshipStatus::Married);
    return CopyStrongIdsToBuffer(partners, buf, maxCount);
}

extern "C" __declspec(dllexport) std::uint32_t MARAS_GetAllEngaged(std::uint32_t* buf, std::uint32_t maxCount) {
    const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
    if (!worldSnap || !worldSnap->relationships) {
        return 0;
    }
    const auto partners = worldSnap->relationships->GetPartnersWithStatus(
        MARAS::State::ActorId::Player(), MARAS::State::RelationshipStatus::Engaged);
    return CopyStrongIdsToBuffer(partners, buf, maxCount);
}

extern "C" __declspec(dllexport) MARAS_Temperament MARAS_GetTemperament(std::uint32_t npcFormID) {
    const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
    if (!worldSnap || npcFormID == 0) {
        return MARAS_Temperament::Proud;
    }
    if (worldSnap->actors) {
        if (const auto* profile = worldSnap->actors->Find(MARAS::State::RefId{ npcFormID })) {
            return static_cast<MARAS_Temperament>(static_cast<std::uint8_t>(profile->temperament));
        }
    }
    if (worldSnap->deceased) {
        if (const auto* deceased = worldSnap->deceased->FindByActor(MARAS::State::ActorId{ npcFormID })) {
            return static_cast<MARAS_Temperament>(static_cast<std::uint8_t>(deceased->temperament));
        }
    }
    return MARAS_Temperament::Proud;
}

extern "C" __declspec(dllexport) std::uint32_t MARAS_GetStatusHistoryCount(std::uint32_t npcFormID, std::uint32_t targetFormID) {
    if (npcFormID == 0 || targetFormID == 0) {
        return 0;
    }
    const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
    const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
    if (!worldSnap || !spatialSnap) {
        return 0;
    }
    const auto view = MARAS::Projection::PerspectiveView::ForDashboard(
        worldSnap, spatialSnap, &MARAS::GetEngineWerewolfStateProvider());
    const auto edgeOpt = view.GetPerceivedPair(MARAS::State::ActorId{ npcFormID }, MARAS::State::ActorId{ targetFormID });
    if (!edgeOpt.has_value()) {
        return 0;
    }
    return static_cast<std::uint32_t>(edgeOpt->statusHistory.size());
}

extern "C" __declspec(dllexport) bool MARAS_GetStatusHistoryEntry(std::uint32_t npcFormID,
                                                                   std::uint32_t targetFormID,
                                                                   std::uint32_t index,
                                                                   std::uint8_t* outFromStatus,
                                                                   std::uint8_t* outToStatus,
                                                                   float* outTimestamp,
                                                                   std::uint32_t* outInitiatorFormID) {
    if (npcFormID == 0 || targetFormID == 0) {
        return false;
    }
    const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
    const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
    if (!worldSnap || !spatialSnap) {
        return false;
    }
    const auto view = MARAS::Projection::PerspectiveView::ForDashboard(
        worldSnap, spatialSnap, &MARAS::GetEngineWerewolfStateProvider());
    const auto edgeOpt = view.GetPerceivedPair(MARAS::State::ActorId{ npcFormID }, MARAS::State::ActorId{ targetFormID });
    if (!edgeOpt.has_value() || index >= edgeOpt->statusHistory.size()) {
        return false;
    }
    const auto& entry = edgeOpt->statusHistory[index];
    if (outFromStatus) *outFromStatus = static_cast<std::uint8_t>(entry.fromStatus);
    if (outToStatus) *outToStatus = static_cast<std::uint8_t>(entry.toStatus);
    if (outTimestamp) *outTimestamp = entry.timestamp;
    if (outInitiatorFormID) *outInitiatorFormID = entry.initiator.raw();
    return true;
}

// ============================================================
// Household Queries
// ============================================================

extern "C" __declspec(dllexport) std::uint32_t MARAS_GetTenantHouseFormID(std::uint32_t tenantFormID) {
    if (tenantFormID == 0) {
        return 0;
    }
    const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
    if (!worldSnap || !worldSnap->household) {
        return 0;
    }
    return worldSnap->household->GetTenantHouse(tenantFormID).raw();
}

extern "C" __declspec(dllexport) std::uint32_t MARAS_GetPlayerHouseTenants(std::uint32_t houseFormID,
                                                                             std::uint32_t* buf,
                                                                             std::uint32_t maxCount) {
    if (houseFormID == 0) {
        return 0;
    }
    const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
    if (!worldSnap || !worldSnap->household) {
        return 0;
    }
    const auto* house = worldSnap->household->FindHouse(MARAS::State::HouseId{ houseFormID });
    if (!house) {
        return 0;
    }
    return CopyRangeToBuffer(house->tenantOrder, buf, maxCount);
}

// ============================================================
// Spatial Teammate Queries
// ============================================================

extern "C" __declspec(dllexport) std::uint32_t MARAS_GetCurrentTeammates(std::uint32_t* buf, std::uint32_t maxCount) {
    const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
    const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
    if (!spatialSnap) {
        return 0;
    }
    const auto teammates = spatialSnap->GetPlayerTeammates(worldSnap ? worldSnap->actors.get() : nullptr);
    return CopyStrongIdsToBuffer(teammates, buf, maxCount);
}
