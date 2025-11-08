#include "core/HomeCellService.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <sstream>

#include "core/FormCache.h"
#include "utils/FormUtils.h"

namespace MARAS {

    namespace {
        // Constants
        constexpr RE::FormID KEYWORD_DWELLING = 0x000130DC;
        constexpr RE::FormID KEYWORD_HOUSE = 0x0001CB85;
        constexpr RE::FormID LOCATION_CENTER_MARKER = 0x0001BDF1;  // Static: LocationCenterMarker
        constexpr std::uint32_t FURNITURE_CAN_SLEEP_MASK =
            static_cast<std::uint32_t>(RE::TESFurniture::ActiveMarker::kCanSleep);

        // Helper: Lookup a keyword by FormID
        RE::BGSKeyword* LookupKeyword(RE::FormID formID) { return RE::TESForm::LookupByID<RE::BGSKeyword>(formID); }

        // Helper: Check if location has dwelling/house keywords
        bool IsHomeLocation(RE::BGSLocation* location) {
            if (!location) return false;

            auto kwDwelling = LookupKeyword(KEYWORD_DWELLING);
            auto kwHouse = LookupKeyword(KEYWORD_HOUSE);

            if (kwDwelling && location->HasKeyword(kwDwelling)) return true;
            if (kwHouse && location->HasKeyword(kwHouse)) return true;

            return false;
        }

        // Helper: Check if door requires a key
        bool DoorRequiresKey(RE::TESObjectREFR& doorRef) {
            auto lock = doorRef.GetLock();
            if (!lock) {
                // MARAS_LOG_INFO("DoorRequiresKey: Door {:08X} has no lock", doorRef.GetFormID());
                return false;
            }

            if (!lock->key) {
                // MARAS_LOG_INFO("DoorRequiresKey: Door {:08X} has lock but no key", doorRef.GetFormID());
                return false;
            }

            auto lvl = lock->GetLockLevel(&doorRef);
            bool notRequiresKey = (lvl != RE::LOCK_LEVEL::kRequiresKey);
            // MARAS_LOG_INFO("DoorRequiresKey: Door {:08X} has key={:08X}, lockLevel={}, requiresKey={}",
            //                doorRef.GetFormID(), lock->key->GetFormID(), static_cast<int>(lvl),
            //                lvl == RE::LOCK_LEVEL::kRequiresKey);
            // True only when the lock level explicitly requires a key
            return notRequiresKey;
        }

        // Helper: Check if furniture is a bed
        bool IsBed(RE::TESFurniture* furniture) {
            if (!furniture) return false;
            auto flags = furniture->furnFlags.underlying();
            return (flags & FURNITURE_CAN_SLEEP_MASK) != 0;
        }

        // Helper: Get form name for logging (tries full name, then editor ID, then FormID)
        std::string GetFormName(const RE::TESForm* form) {
            if (!form) return "(null)";

            if (auto fullForm = form->As<RE::TESFullName>()) {
                const char* fullname = fullForm->GetFullName();
                if (fullname && fullname[0]) return fullname;
            }

            const char* edid = form->GetFormEditorID();
            if (edid) return edid;

            return std::to_string(form->GetFormID());
        }

        // Helper: Format FormID for logging
        std::string FormatFormID(RE::FormID formID) {
            std::ostringstream ss;
            ss << "0x" << std::hex << std::uppercase << formID << std::dec;
            return ss.str();
        }

        // Helper: Format list of FormIDs for logging
        std::string FormatFormIDList(const std::vector<RE::FormID>& formIDs) {
            std::ostringstream ss;
            for (size_t i = 0; i < formIDs.size(); ++i) {
                if (i) ss << ", ";
                ss << FormatFormID(formIDs[i]);
            }
            return ss.str();
        }

    }  // namespace

    HomeCellService& HomeCellService::GetSingleton() {
        static HomeCellService instance;
        return instance;
    }

    HomeCellService::HomeCellService() {
        homeCells_.clear();
        actorsWithHome_.clear();
        bedsWithOwners_.clear();
    }

    void HomeCellService::BuildIndex() {
        homeCells_.clear();
        actorsWithHome_.clear();
        bedsWithOwners_.clear();

        auto start = std::chrono::high_resolution_clock::now();

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            MARAS_LOG_WARN("HomeCellService: TESDataHandler not available");
            return;
        }

        size_t scanned = 0;

        // Iterate the interiorCells array directly, skipping null entries
        for (auto* cell : dataHandler->interiorCells) {
            if (!cell) continue;

            auto location = cell->GetLocation();
            if (!IsHomeLocation(location)) continue;

            // Scan cell contents (single pass) - fills out hdata and records beds/actors
            HomeCellData hdata;
            bool hasPersistentActor = ScanCell(cell, hdata);
            if (!hasPersistentActor) continue;

            // Save the cell data
            homeCells_[cell->GetFormID()] = std::move(hdata);
            ++scanned;
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        MARAS_LOG_INFO("HomeCellService: built index for {} cells in {} ms", scanned, dur);

        // LogIndex();
    }

    bool HomeCellService::ScanCell(RE::TESObjectCELL* cell, HomeCellData& outData) {
        if (!cell) return false;
        RE::FormID cellId = cell->GetFormID();

        bool hasPersistentActor = false;
        static RE::BGSLocationRefType* kLocationCenterLCRT =
            RE::TESForm::LookupByID<RE::BGSLocationRefType>(LOCATION_CENTER_MARKER);

        bool foundCenterMarker = false;

        // Single pass over references: collect doors, furniture ownership, persistent actors, and center marker
        cell->ForEachReference([&](RE::TESObjectREFR& ref) {
            auto base = ref.GetBaseObject();
            if (!base) return RE::BSContainer::ForEachResult::kContinue;

            // Process doors that require keys
            if (base->formType == RE::FormType::Door) {
                if (DoorRequiresKey(ref)) {
                    outData.doors.push_back(ref.GetFormID());
                }
            }

            // Process beds with owners
            if (base->formType == RE::FormType::Furniture) {
                if (auto furn = base->As<RE::TESFurniture>()) {
                    if (IsBed(furn)) {
                        ProcessBedOwnership(ref);
                    }
                }
            }

            // Process LocationCenterMarker: XMarker with ExtraLocationRefType (always takes priority)
            if (auto* lrt = ref.extraList.GetByType<RE::ExtraLocationRefType>()) {
                if (lrt->locRefType == kLocationCenterLCRT) {
                    outData.centerMarker = ref.GetFormID();
                    foundCenterMarker = true;
                }
            }

            // Record first object reference in the cell as fallback if no center marker set yet
            if (!foundCenterMarker && !outData.centerMarker) {
                outData.centerMarker = ref.GetFormID();
            }

            // Process persistent actor references
            if (ref.IsPersistent() && base->formType == RE::FormType::NPC) {
                hasPersistentActor = true;
                ProcessActorHome(ref.GetFormID(), cellId);
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        if (hasPersistentActor && !outData.centerMarker) {
            MARAS_LOG_WARN("HomeCellService: Cell {:08X} '{}' has persistent actors but NO center marker found", cellId,
                           GetFormName(cell));
        }

        return hasPersistentActor;
    }

    void HomeCellService::ProcessBedOwnership(RE::TESObjectREFR& bedRef) {
        RE::TESForm* ownerForm = bedRef.extraList.GetOwner();
        if (!ownerForm) return;

        RE::FormID ownerId = ownerForm->GetFormID();
        RE::FormID bedRefId = bedRef.GetFormID();
        bedsWithOwners_[ownerId].push_back(bedRefId);
    }

    void HomeCellService::ProcessActorHome(RE::FormID actorFormId, RE::FormID cellId) {
        // Insert only if not already present
        if (actorsWithHome_.find(actorFormId) == actorsWithHome_.end()) {
            ActorHomeData adata;
            adata.homeCell = cellId;
            actorsWithHome_.emplace(actorFormId, std::move(adata));
        }
    }

    RE::FormID HomeCellService::GetNpcHome(RE::Actor* npc) const {
        if (!npc) return 0;

        auto it = actorsWithHome_.find(npc->GetFormID());
        return (it != actorsWithHome_.end()) ? it->second.homeCell : 0;
    }

    RE::FormID HomeCellService::GetNpcOriginalHouseCenterMarker(RE::Actor* npc) const {
        if (!npc) return 0;

        // Get the NPC's home cell
        RE::FormID cellId = GetNpcHome(npc);
        if (cellId == 0) {
            MARAS_LOG_INFO("GetNpcOriginalHouseCenterMarker: NPC {:08X} has no home cell", npc->GetFormID());
            return 0;
        }

        // Lookup the cell data to get the center marker
        auto it = homeCells_.find(cellId);
        if (it == homeCells_.end()) {
            MARAS_LOG_INFO("GetNpcOriginalHouseCenterMarker: Cell {:08X} not found in index", cellId);
            return 0;
        }

        RE::FormID markerId = it->second.centerMarker;
        MARAS_LOG_INFO("GetNpcOriginalHouseCenterMarker: NPC {:08X} -> Cell {:08X} -> Marker {:08X}", npc->GetFormID(),
                       cellId, markerId);
        return markerId;
    }

    std::vector<RE::FormID> HomeCellService::GetNpcBeds(RE::Actor* npc) const {
        if (!npc) return {};

        // Get the NPC's base form (TESNpc) since bed ownership is stored by base form ID
        auto baseForm = npc->GetActorBase();
        if (!baseForm) return {};

        auto it = bedsWithOwners_.find(baseForm->GetFormID());
        return (it != bedsWithOwners_.end()) ? it->second : std::vector<RE::FormID>{};
    }

    std::vector<RE::FormID> HomeCellService::GetCellDoors(RE::FormID cellFormID) const {
        auto it = homeCells_.find(cellFormID);
        return (it != homeCells_.end()) ? it->second.doors : std::vector<RE::FormID>{};
    }

    void HomeCellService::Revert() {
        homeCells_.clear();
        actorsWithHome_.clear();
        bedsWithOwners_.clear();
        MARAS_LOG_INFO("HomeCellService: reverted index");
    }

    void HomeCellService::LogIndex() const {
        MARAS_LOG_INFO("HomeCellService: logging index ({} cells, {} actors, {} owners)", homeCells_.size(),
                       actorsWithHome_.size(), bedsWithOwners_.size());

        for (const auto& [cellId, cellData] : homeCells_) {
            auto cellForm = RE::TESForm::LookupByID(cellId);
            auto cell = cellForm ? cellForm->As<RE::TESObjectCELL>() : nullptr;

            std::string cellName = GetFormName(cell);
            std::string locStr = GetLocationInfo(cell);
            std::string doorsStr = FormatFormIDList(cellData.doors);
            std::string actorsStr = GetActorsInCell(cellId);
            std::string bedsStr = GetBedsInCell(cellId);
            std::string markerStr = cellData.centerMarker ? FormatFormID(cellData.centerMarker) : "(none)";

            MARAS_LOG_INFO("Cell {:08X} '{}' loc={} marker={} doors=[{}] actors=[{}] beds=[{}]", cellId, cellName,
                           locStr, markerStr, doorsStr, actorsStr, bedsStr);
        }

        // Log actors with their beds
        MARAS_LOG_INFO("HomeCellService: actor to beds mapping ({} actors with beds):", bedsWithOwners_.size());
        for (const auto& [actorId, bedList] : bedsWithOwners_) {
            std::string bedsStr = FormatFormIDList(bedList);
            MARAS_LOG_INFO("  Actor {:08X} has beds=[{}]", actorId, bedsStr);
        }
        if (bedsWithOwners_.empty()) {
            MARAS_LOG_INFO("  (no beds with owners found)");
        }
    }

    std::string HomeCellService::GetLocationInfo(const RE::TESObjectCELL* cell) const {
        if (!cell) return "(no cell)";

        auto loc = cell->GetLocation();
        if (!loc) return "(no location)";

        std::string lname = GetFormName(loc);
        const char* ledid = loc->GetFormEditorID();

        std::ostringstream ss;
        ss << "name='" << lname << "' edid='" << (ledid ? ledid : "(no edid)") << "'";
        return ss.str();
    }

    std::string HomeCellService::GetActorsInCell(RE::FormID cellId) const {
        std::vector<RE::FormID> actors;
        for (const auto& [actorId, homeData] : actorsWithHome_) {
            if (homeData.homeCell == cellId) {
                actors.push_back(actorId);
            }
        }
        return FormatFormIDList(actors);
    }

    std::string HomeCellService::GetBedsInCell(RE::FormID cellId) const {
        std::vector<RE::FormID> beds;
        for (const auto& [ownerId, bedList] : bedsWithOwners_) {
            for (auto bedRefId : bedList) {
                if (auto form = RE::TESForm::LookupByID(bedRefId)) {
                    if (auto ref = form->As<RE::TESObjectREFR>()) {
                        if (auto parentCell = ref->GetParentCell()) {
                            if (parentCell->GetFormID() == cellId) {
                                beds.push_back(bedRefId);
                            }
                        }
                    }
                }
            }
        }
        return FormatFormIDList(beds);
    }

}  // namespace MARAS
