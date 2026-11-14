#include "core/SpouseAssetsService.h"

#include "core/FormCache.h"
#include "core/HomeCellService.h"
#include "maras/state/WorldStore.h"
#include "services/ActorService.h"
#include "utils/Common.h"

namespace MARAS {

    namespace {
        // Iterate all bed references registered for an NPC (by FormID) and apply a function
        inline void ForEachNpcBed(RE::FormID npcFormID, const std::function<void(RE::TESObjectREFR*)>& fn) {
            if (npcFormID == 0 || IsSyntheticChildID(npcFormID)) return;
            auto beds = MARAS::HomeCellService::GetSingleton().GetNpcBeds(npcFormID);
            for (auto bedId : beds) {
                if (auto form = RE::TESForm::LookupByID(bedId)) {
                    if (auto ref = form->As<RE::TESObjectREFR>()) {
                        fn(ref);
                    }
                }
            }
        }

        // Captures original cell public flag if not yet recorded and ensures cell is set public
        void ApplyCellSharedPublicState(RE::FormID cellFormID, State::SharedCellRecord& data) {
            auto* form = RE::TESForm::LookupByID(cellFormID);
            if (!form) {
                MARAS_LOG_WARN("SpouseAssetsService: could not lookup cell {:08X} to apply public state", cellFormID);
                return;
            }
            auto* cell = form->As<RE::TESObjectCELL>();
            if (!cell) {
                MARAS_LOG_WARN("SpouseAssetsService: cell {:08X} form is not a TESObjectCELL", cellFormID);
                return;
            }

            std::uint16_t flags = cell->cellFlags.underlying();
            bool isPublic = (flags & static_cast<std::uint16_t>(RE::TESObjectCELL::Flag::kPublicArea)) != 0;
            if (!data.originalPublicRecorded) {
                data.originalPublicRecorded = true;
                data.originalPublicState = isPublic;
            }
            if (!isPublic) {
                cell->SetPublic(true);
                MARAS_LOG_INFO("SpouseAssetsService: set cell {:08X} public=true (was private)", cellFormID);
            }
        }

        // Restores original cell public flag if it was originally private
        void RestoreCellOriginalPublicState(RE::FormID cellFormID, const State::SharedCellRecord& data) {
            if (!data.originalPublicRecorded || data.originalPublicState) {
                return;
            }
            if (auto* form = RE::TESForm::LookupByID(cellFormID)) {
                if (auto* cell = form->As<RE::TESObjectCELL>()) {
                    cell->SetPublic(false);
                    MARAS_LOG_INFO("SpouseAssetsService: restored cell {:08X} public=false", cellFormID);
                }
            }
        }
    }  // namespace

    SpouseAssetsService& SpouseAssetsService::GetSingleton() {
        static SpouseAssetsService instance;
        return instance;
    }

    SpouseAssetsService::SpouseAssetsService() = default;

    // Note: door queries are provided by HomeCellService; this service doesn't store door lists.

    bool SpouseAssetsService::ShareHouseWithPlayer(RE::FormID npcFormID) {
        if (npcFormID == 0 || IsSyntheticChildID(npcFormID) || !ActorSvc::IsValid(npcFormID)) return false;

        // Lookup npc home from HomeCellService by FormID (supports unloaded persistent spouses)
        const RE::FormID homeCell = MARAS::HomeCellService::GetSingleton().GetNpcHome(npcFormID);
        if (homeCell == 0) {
            MARAS_LOG_WARN("ShareHouseWithPlayer: NPC {:08X} has no known home", npcFormID);
            return false;
        }

        const State::CellId cellId{ homeCell };
        State::SharedCellRecord cellRecord{};
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            const auto& existingCells = snap->household->AllSharedCells();
            if (auto it = existingCells.find(cellId); it != existingCells.end()) {
                cellRecord = it->second;
            }
        }

        ApplyCellSharedPublicState(homeCell, cellRecord);

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            State::SharedCellRecord updated{};
            const auto& existingCells = ws.household.Read().AllSharedCells();
            if (auto it = existingCells.find(cellId); it != existingCells.end()) {
                updated = it->second;
            }
            if (!updated.originalPublicRecorded && cellRecord.originalPublicRecorded) {
                updated.originalPublicRecorded = true;
                updated.originalPublicState = cellRecord.originalPublicState;
            }
            updated.sharingSpouses.insert(State::ActorId{ npcFormID });
            updated.sharedWithPlayer = true;
            ws.household.Write().UpsertSharedCell(cellId, std::move(updated));
        });

        // Transfer owned beds to player faction using HomeCellService data
        ShareFurniture(npcFormID);

        MARAS_LOG_INFO("ShareHouseWithPlayer: NPC {:08X} shared home {:08X}", npcFormID, homeCell);
        return true;
    }

    bool SpouseAssetsService::StopShareHouseWithPlayer(RE::FormID npcFormID) {
        if (npcFormID == 0 || IsSyntheticChildID(npcFormID)) return false;

        bool found = false;
        std::vector<std::pair<RE::FormID, State::SharedCellRecord>> cellsToRestore;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            const State::ActorId spouseId{ npcFormID };
            for (const auto& [cellId, rec] : ws.household.Read().AllSharedCells()) {
                if (rec.sharingSpouses.contains(spouseId) && rec.sharingSpouses.size() == 1) {
                    cellsToRestore.emplace_back(cellId.raw(), rec);
                }
            }
            found = ws.household.Write().StopSharingForSpouse(spouseId);
        });

        if (found) {
            StopShareFurniture(npcFormID);
        }
        for (const auto& [cellIdRaw, rec] : cellsToRestore) {
            RestoreCellOriginalPublicState(cellIdRaw, rec);
        }

        MARAS_LOG_INFO("StopShareHouseWithPlayer: stopped sharing for NPC {:08X} (found: {})", npcFormID, found);
        return found;
    }

    bool SpouseAssetsService::IsHouseSharedWithPlayer(RE::FormID cellFormID) const {
        if (cellFormID == 0) return false;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->household->IsCellSharedWithPlayer(State::CellId{ cellFormID });
    }

    void SpouseAssetsService::FinalizeLoad() {
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
                ShareFurniture(spouseId.raw());
            }
            ++reappliedCount;
        }

        if (!publicFlagUpdates.empty()) {
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                auto& hh = ws.household.Write();
                for (const auto& [cellId, flagSnapshot] : publicFlagUpdates) {
                    if (auto it = hh.AllSharedCells().find(cellId); it != hh.AllSharedCells().end()) {
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

        MARAS_LOG_INFO("SpouseAssetsService::FinalizeLoad - re-applied sharing for {} shared home cell(s)",
                       reappliedCount);
    }

    void SpouseAssetsService::ShareFurniture(RE::FormID npcFormID) {
        if (npcFormID == 0 || IsSyntheticChildID(npcFormID) || !ActorSvc::IsValid(npcFormID)) return;

        auto playerFaction = MARAS::FormCache::GetSingleton().GetPlayerFaction();
        if (!playerFaction) {
            MARAS_LOG_WARN("ShareFurniture: player faction not available");
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& savedOwners = snap->household->AllOriginalBedOwners();
        std::vector<std::pair<State::RefId, State::FormRawId>> newBedOwners;

        // Record original bed owner (including 0 when unowned) before transferring to player faction
        ForEachNpcBed(npcFormID, [&](RE::TESObjectREFR* ref) {
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

    void SpouseAssetsService::StopShareFurniture(RE::FormID npcFormID) {
        if (npcFormID == 0 || IsSyntheticChildID(npcFormID)) return;

        auto actorBase = ActorSvc::GetActorBase(npcFormID);
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& savedOwners = snap->household->AllOriginalBedOwners();
        std::vector<State::RefId> bedsToErase;

        ForEachNpcBed(npcFormID, [&](RE::TESObjectREFR* ref) {
            const State::RefId bedRefId{ ref->GetFormID() };
            if (const auto it = savedOwners.find(bedRefId); it != savedOwners.end()) {
                const RE::FormID savedOwnerID = it->second.raw();
                bedsToErase.push_back(bedRefId);
                if (savedOwnerID == 0) {
                    ref->extraList.SetOwner(nullptr);
                    return;
                }
                if (auto* restoredOwner = RE::TESForm::LookupByID(savedOwnerID)) {
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

    void SpouseAssetsService::Revert() {
        MARAS_LOG_INFO("SpouseAssetsService: reverted data");
    }

    nlohmann::json SpouseAssetsService::DumpJson() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        nlohmann::json j = nlohmann::json::array();
        for (const auto& [cellId, data] : snap->household->AllSharedCells()) {
            const RE::FormID cellRaw = cellId.raw();
            nlohmann::json h;
            h["cellFormId"] = std::format("{:08X}", cellRaw);
            std::string cellName = "";
            if (auto cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(cellRaw)) {
                if (cell->GetName()) cellName = cell->GetName();
            }
            h["cellName"] = cellName;
            h["sharedWithPlayer"] = data.sharedWithPlayer;
            h["originalPublicRecorded"] = data.originalPublicRecorded;
            h["originalPublicState"] = data.originalPublicState;

            nlohmann::json spousesArray = nlohmann::json::array();
            for (const auto spouseId : data.sharingSpouses) {
                const RE::FormID spouseRaw = spouseId.raw();
                nlohmann::json s;
                s["formId"] = std::format("{:08X}", spouseRaw);
                s["name"] = ActorSvc::GetName(spouseRaw);
                spousesArray.push_back(s);
            }
            h["sharingSpouses"] = spousesArray;
            j.push_back(h);
        }
        return j;
    }

}  // namespace MARAS
