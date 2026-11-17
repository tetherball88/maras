#include "maras/domain/household/SpouseAssetRules.h"

namespace MARAS::Domain::Household {

    ShareCellOutcome ApplyShareCellsWithPlayer(
        State::WorldState& ws,
        ActorId spouseId,
        const std::vector<CellId>& cellIds) {
        ShareCellOutcome out{};
        if (spouseId.IsNull() || cellIds.empty()) {
            return out;
        }

        for (CellId cid : cellIds) {
            if (cid.IsNull()) {
                continue;
            }
            State::SharedCellRecord rec{};
            if (const auto it = ws.household.Read().AllSharedCells().find(cid);
                it != ws.household.Read().AllSharedCells().end()) {
                rec = it->second;
            }
            const bool wasShared = rec.sharedWithPlayer;
            rec.sharedWithPlayer = true;
            rec.sharingSpouses.insert(spouseId);
            ws.household.Write().UpsertSharedCell(cid, std::move(rec));
            if (!wasShared) {
                out.newlySharedCells.push_back(cid);
            }
            out.shared = true;
        }
        return out;
    }

    StopSharingOutcome ApplyStopSharingForSpouse(
        State::WorldState& ws,
        ActorId spouseId) {
        StopSharingOutcome out{};
        if (spouseId.IsNull()) {
            return out;
        }

        std::vector<CellId> prevShared;
        for (const auto& [cid, rec] : ws.household.Read().AllSharedCells()) {
            if (rec.sharedWithPlayer && rec.sharingSpouses.contains(spouseId)) {
                prevShared.push_back(cid);
            }
        }

        out.stopped = ws.household.Write().StopSharingForSpouse(spouseId);
        if (out.stopped) {
            for (CellId cid : prevShared) {
                if (!ws.household.Read().IsCellSharedWithPlayer(cid)) {
                    out.restoredPrivateCells.push_back(cid);
                }
            }
        }
        return out;
    }

    bool IsCellSharedWithPlayer(
        const State::WorldStateSnapshot& snap,
        CellId cellId) noexcept {
        if (!snap.household || cellId.IsNull()) {
            return false;
        }
        return snap.household->IsCellSharedWithPlayer(cellId);
    }

    LoadedCellBedReconcilePlan PlanLoadedCellBedReconciliation(
        const State::WorldStateSnapshot& snap,
        CellId cellId,
        std::span<const RefId> loadedCellBedRefs) {
        LoadedCellBedReconcilePlan plan{};
        if (!snap.household || cellId.IsNull()) {
            return plan;
        }

        const auto& hh = *snap.household;
        if (const auto cellIt = hh.AllSharedCells().find(cellId);
            cellIt != hh.AllSharedCells().end() &&
            cellIt->second.sharedWithPlayer &&
            !cellIt->second.sharingSpouses.empty()) {
            plan.shouldEnsureCellPublic = true;
            plan.shouldRecordOriginalPublic = !cellIt->second.originalPublicRecorded;
            plan.sharingSpouses.assign(
                cellIt->second.sharingSpouses.begin(),
                cellIt->second.sharingSpouses.end());
            return plan;
        }

        if (loadedCellBedRefs.empty() || hh.AllOriginalBedOwners().empty()) {
            return plan;
        }

        const auto& savedOwners = hh.AllOriginalBedOwners();
        for (const RefId bedRef : loadedCellBedRefs) {
            if (bedRef.IsNull()) {
                continue;
            }
            if (const auto ownerIt = savedOwners.find(bedRef); ownerIt != savedOwners.end()) {
                plan.staleTrackedBedsToRestore.emplace_back(bedRef, ownerIt->second);
            }
        }
        return plan;
    }

}  // namespace MARAS::Domain::Household
