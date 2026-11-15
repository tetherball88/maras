#include "maras/engine/BedOwnershipReconciler.h"

#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/FactionReconciler.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/HouseholdEngineOps.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/state/WorldStore.h"

#include <format>
#include <functional>

namespace MARAS {

    namespace {

        void ForEachNpcBed(RE::FormID npcFormId, const std::function<void(RE::TESObjectREFR*)>& fn) {
            if (!HasPhysicalActor(npcFormId)) {
                return;
            }
            const auto beds = SpatialWorldIndex::GetSingleton().GetNpcBeds(npcFormId);
            for (const RE::FormID bedId : beds) {
                if (auto* form = RE::TESForm::LookupByID(bedId)) {
                    if (auto* ref = form->As<RE::TESObjectREFR>()) {
                        fn(ref);
                    }
                }
            }
        }

        void RestoreCellOriginalPublicState(RE::FormID cellFormId, const State::SharedCellRecord& data) {
            if (!data.originalPublicRecorded || data.originalPublicState) {
                return;
            }
            if (auto* form = RE::TESForm::LookupByID(cellFormId)) {
                if (auto* cell = form->As<RE::TESObjectCELL>()) {
                    cell->SetPublic(false);
                    MARAS_LOG_INFO("BedOwnershipReconciler: restored cell {:08X} public=false", cellFormId);
                }
            }
        }

    }  // namespace

    BedOwnershipReconciler& BedOwnershipReconciler::GetSingleton() {
        static BedOwnershipReconciler instance;
        return instance;
    }

    RE::FormID BedOwnershipReconciler::GetNpcHomeCell(RE::FormID npcFormId) const {
        if (!HasPhysicalActor(npcFormId)) {
            return 0;
        }
        return SpatialWorldIndex::GetSingleton().GetNpcHome(npcFormId);
    }

    std::string BedOwnershipReconciler::GetCellName(RE::FormID cellFormId) {
        if (cellFormId == 0) {
            return {};
        }
        if (auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(cellFormId)) {
            if (cell->GetName()) {
                return cell->GetName();
            }
        }
        return {};
    }

    void BedOwnershipReconciler::ApplyCellSharedPublicState(RE::FormID cellFormId, State::SharedCellRecord& data) const {
        auto* form = RE::TESForm::LookupByID(cellFormId);
        if (!form) {
            MARAS_LOG_WARN("BedOwnershipReconciler: could not lookup cell {:08X} to apply public state", cellFormId);
            return;
        }
        auto* cell = form->As<RE::TESObjectCELL>();
        if (!cell) {
            MARAS_LOG_WARN("BedOwnershipReconciler: cell {:08X} form is not a TESObjectCELL", cellFormId);
            return;
        }

        const std::uint16_t flags = cell->cellFlags.underlying();
        const bool isPublic = (flags & static_cast<std::uint16_t>(RE::TESObjectCELL::Flag::kPublicArea)) != 0;
        if (!data.originalPublicRecorded) {
            data.originalPublicRecorded = true;
            data.originalPublicState = isPublic;
        }
        if (!isPublic) {
            cell->SetPublic(true);
            MARAS_LOG_INFO("BedOwnershipReconciler: set cell {:08X} public=true (was private)", cellFormId);
        }
    }

    void BedOwnershipReconciler::RestoreUnsharedCells(
        const std::vector<std::pair<RE::FormID, State::SharedCellRecord>>& cellsToRestore) const {
        for (const auto& [cellIdRaw, rec] : cellsToRestore) {
            RestoreCellOriginalPublicState(cellIdRaw, rec);
        }
    }

    void BedOwnershipReconciler::ApplyShareFurniture(RE::FormID npcFormId) {
        if (!HasPhysicalActor(npcFormId) || !ActorSvc::IsValid(npcFormId)) {
            return;
        }

        auto* playerFaction = FormCache::GetSingleton().GetPlayerFaction();
        if (!playerFaction) {
            MARAS_LOG_WARN("BedOwnershipReconciler::ApplyShareFurniture: player faction not available");
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& savedOwners = snap->household->AllOriginalBedOwners();
        std::vector<std::pair<State::RefId, State::FormRawId>> newBedOwners;

        ForEachNpcBed(npcFormId, [&](RE::TESObjectREFR* ref) {
            const State::RefId bedRefId{ ref->GetFormID() };
            if (!savedOwners.contains(bedRefId)) {
                auto* origOwner = ref->extraList.GetOwner();
                if (origOwner != playerFaction) {
                    newBedOwners.emplace_back(bedRefId, State::FormRawId{ origOwner ? origOwner->GetFormID() : 0 });
                }
            }
            ref->extraList.SetOwner(playerFaction);
        });

        if (!newBedOwners.empty()) {
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                auto& hh = ws.household.Write();
                for (const auto& [bedRefId, ownerRaw] : newBedOwners) {
                    if (!hh.AllOriginalBedOwners().contains(bedRefId)) {
                        hh.SetOriginalBedOwner(bedRefId, ownerRaw);
                    }
                }
            });
        }
    }

    void BedOwnershipReconciler::RestoreFurniture(RE::FormID npcFormId) {
        if (!HasPhysicalActor(npcFormId)) {
            return;
        }

        auto* actorBase = ActorSvc::GetActorBase(npcFormId);
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& savedOwners = snap->household->AllOriginalBedOwners();
        std::vector<State::RefId> bedsToErase;

        ForEachNpcBed(npcFormId, [&](RE::TESObjectREFR* ref) {
            const State::RefId bedRefId{ ref->GetFormID() };
            if (const auto it = savedOwners.find(bedRefId); it != savedOwners.end()) {
                const RE::FormID savedOwnerId = it->second.raw();
                bedsToErase.push_back(bedRefId);
                if (savedOwnerId == 0) {
                    ref->extraList.SetOwner(nullptr);
                    return;
                }
                if (auto* restoredOwner = RE::TESForm::LookupByID(savedOwnerId)) {
                    ref->extraList.SetOwner(restoredOwner);
                    return;
                }
            }
            if (actorBase) {
                ref->extraList.SetOwner(actorBase);
            }
        });

        if (!bedsToErase.empty()) {
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                auto& hh = ws.household.Write();
                for (const auto bedRefId : bedsToErase) {
                    hh.EraseOriginalBedOwner(bedRefId);
                }
            });
        }
    }

    bool BedOwnershipReconciler::ApplyShareHomeWithPlayer(RE::FormID npcFormId) {
        if (!HasPhysicalActor(npcFormId) || !ActorSvc::IsValid(npcFormId)) {
            return false;
        }

        const RE::FormID homeCell = GetNpcHomeCell(npcFormId);
        if (homeCell == 0) {
            MARAS_LOG_WARN("BedOwnershipReconciler::ApplyShareHomeWithPlayer: NPC {:08X} has no known home", npcFormId);
            if (DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
                Utils::AddToFaction(npcFormId, FormCache::GetSingleton().GetSpouseNoInitialHouseFaction(), 0);
            } else {
                AddCurrentEpochTask("BedOwnershipReconciler::AddNoInitialHouseFaction", [npcFormId]() {
                    Utils::AddToFaction(npcFormId, FormCache::GetSingleton().GetSpouseNoInitialHouseFaction(), 0);
                });
            }
            QueueHUDMessage(std::format("$TTM_MSG_SpouseNoHome{{{}}}", ActorSvc::GetName(npcFormId)));
            return false;
        }

        const State::CellId cellId{ homeCell };
        State::SharedCellRecord cellRecord{};
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            const auto& existingCells = snap->household->AllSharedCells();
            if (const auto it = existingCells.find(cellId); it != existingCells.end()) {
                cellRecord = it->second;
            }
        }

        ApplyCellSharedPublicState(homeCell, cellRecord);

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            State::SharedCellRecord updated{};
            const auto& existingCells = ws.household.Read().AllSharedCells();
            if (const auto it = existingCells.find(cellId); it != existingCells.end()) {
                updated = it->second;
            }
            if (!updated.originalPublicRecorded && cellRecord.originalPublicRecorded) {
                updated.originalPublicRecorded = true;
                updated.originalPublicState = cellRecord.originalPublicState;
            }
            updated.sharingSpouses.insert(State::ActorId{ npcFormId });
            updated.sharedWithPlayer = true;
            ws.household.Write().UpsertSharedCell(cellId, std::move(updated));
        });

        ApplyShareFurniture(npcFormId);
        FactionReconciler::GetSingleton().ReconcileActor(npcFormId);
        HouseholdEngineOps::GetSingleton().UnlockAndGrantSpouseHomeAccess(homeCell);

        std::string cellName = GetCellName(homeCell);
        if (cellName.empty()) {
            cellName = HouseholdEngineOps::GetHouseName(homeCell);
        }
        QueueHUDMessage(std::format("$TTM_MSG_SpouseSharedHome{{{}}}{{{}}}",
                                    ActorSvc::GetName(npcFormId),
                                    cellName));

        MARAS_LOG_INFO("BedOwnershipReconciler::ApplyShareHomeWithPlayer: NPC {:08X} shared home {:08X}",
                       npcFormId, homeCell);
        return true;
    }

    bool BedOwnershipReconciler::StopShareHomeWithPlayer(RE::FormID npcFormId, std::string_view reason) {
        if (!HasPhysicalActor(npcFormId)) {
            return false;
        }

        bool found = false;
        std::vector<std::pair<RE::FormID, State::SharedCellRecord>> cellsToRestore;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            const State::ActorId spouseId{ npcFormId };
            for (const auto& [cellId, rec] : ws.household.Read().AllSharedCells()) {
                if (rec.sharingSpouses.contains(spouseId) && rec.sharingSpouses.size() == 1) {
                    cellsToRestore.emplace_back(cellId.raw(), rec);
                }
            }
            found = ws.household.Write().StopSharingForSpouse(spouseId);
        });

        if (found) {
            RestoreFurniture(npcFormId);
            FactionReconciler::GetSingleton().ReconcileActor(npcFormId);

            const std::string spouseName = ActorSvc::GetName(npcFormId);
            if (reason == "divorce") {
                QueueHUDMessage(std::format("$TTM_MSG_SpouseSharedHomeStoppedDivorce{{{}}}", spouseName));
            } else if (reason == "deceased") {
                QueueHUDMessage(std::format("$TTM_MSG_SpouseSharedHomeStoppedDeceased{{{}}}", spouseName));
            } else if (reason == "affection") {
                QueueHUDMessage(std::format("$TTM_MSG_SpouseSharedHomeStoppedAffection{{{}}}", spouseName));
            } else {
                QueueHUDMessage(std::format("$TTM_MSG_SpouseSharedHomeStoppedOther{{{}}}", spouseName));
            }
        }
        RestoreUnsharedCells(cellsToRestore);

        MARAS_LOG_INFO("BedOwnershipReconciler::StopShareHomeWithPlayer: stopped sharing for NPC {:08X} (found: {}, reason: '{}')",
                       npcFormId, found, reason);
        return found;
    }

    void BedOwnershipReconciler::ReapplyAllSharedCellsOnLoad() {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& sharedCells = snap->household->AllSharedCells();

        std::vector<std::pair<State::CellId, State::SharedCellRecord>> publicFlagUpdates;
        std::size_t reappliedCount = 0;

        for (const auto& [cellId, data] : sharedCells) {
            if (!data.sharedWithPlayer || data.sharingSpouses.empty()) {
                continue;
            }
            State::SharedCellRecord updated = data;
            ApplyCellSharedPublicState(cellId.raw(), updated);
            if (updated.originalPublicRecorded != data.originalPublicRecorded ||
                updated.originalPublicState != data.originalPublicState) {
                publicFlagUpdates.emplace_back(cellId, updated);
            }
            for (const auto spouseId : data.sharingSpouses) {
                ApplyShareFurniture(spouseId.raw());
            }
            ++reappliedCount;
        }

        if (!publicFlagUpdates.empty()) {
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                auto& hh = ws.household.Write();
                for (const auto& [cellId, flagSnapshot] : publicFlagUpdates) {
                    if (const auto it = hh.AllSharedCells().find(cellId); it != hh.AllSharedCells().end()) {
                        State::SharedCellRecord merged = it->second;
                        if (!merged.originalPublicRecorded && flagSnapshot.originalPublicRecorded) {
                            merged.originalPublicRecorded = true;
                            merged.originalPublicState = flagSnapshot.originalPublicState;
                            hh.UpsertSharedCell(cellId, std::move(merged));
                        }
                    }
                }
            });
        }

        MARAS_LOG_INFO("BedOwnershipReconciler::ReapplyAllSharedCellsOnLoad: re-applied sharing for {} home cell(s)",
                       reappliedCount);
    }

}  // namespace MARAS
