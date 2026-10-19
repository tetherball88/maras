#include "papyrus/PapyrusInterface.h"

#include "core/HomeCellService.h"
#include "core/PlayerChildrenService.h"
#include "core/PlayerHouseService.h"
#include "core/SpouseAssetsService.h"
#include "utils/Common.h"

namespace MARAS::PapyrusInterface {

    // ========================================
    // Player house bindings
    // ========================================

    bool RegisterPlayerHouseCell(RE::StaticFunctionTag*, RE::BGSLocation* loc, RE::TESObjectREFR* homeMarker) {
        if (!loc) {
            MARAS_LOG_WARN("RegisterPlayerHouseCell: null location provided");
            return false;
        }

        RE::FormID markerId = homeMarker ? homeMarker->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        return MARAS::PlayerHouseService::GetSingleton().RegisterPlayerHouseCell(loc->GetFormID(), markerId);
    }

    std::vector<RE::BGSLocation*> GetAllPlayerHouses(RE::StaticFunctionTag*) {
        std::vector<RE::BGSLocation*> result;
        std::lock_guard lock(GetPapyrusMutex());
        auto ids = MARAS::PlayerHouseService::GetSingleton().GetAllPlayerHouses();
        result.reserve(ids.size());
        for (auto id : ids) {
            if (auto form = RE::TESForm::LookupByID(id)) {
                if (auto loc = form->As<RE::BGSLocation>()) result.push_back(loc);
            }
        }
        return result;
    }

    std::vector<std::string> GetAllPlayerHousesNames(RE::StaticFunctionTag*) {
        std::lock_guard lock(GetPapyrusMutex());
        return MARAS::PlayerHouseService::GetSingleton().GetAllPlayerHousesNames();
    }

    bool RegisterTenantInPlayerHouse(RE::StaticFunctionTag*, RE::Actor* spouse, RE::BGSLocation* playerHouse, bool stayAtHome) {
        if (!spouse || !playerHouse) {
            MARAS_LOG_WARN("RegisterTenantInPlayerHouse: null argument(s)");
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        auto& houseSvc = MARAS::PlayerHouseService::GetSingleton();
        auto res = houseSvc.RelocateFamilyUnit(spouse->GetFormID(),
                                               playerHouse->GetFormID(),
                                               true);
        if (!res.empty()) {
            houseSvc.SetTenantStayAtHome(spouse->GetFormID(), stayAtHome);
            return true;
        }
        return false;
    }

    bool RemoveTenantFromPlayerHouse(RE::StaticFunctionTag*, RE::Actor* spouse) {
        if (!spouse) {
            MARAS_LOG_WARN("RemoveTenantFromPlayerHouse: null spouse");
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        auto& houseSvc = MARAS::PlayerHouseService::GetSingleton();
        if (!houseSvc.CanRemoveTenant(spouse->GetFormID())) {
            MARAS_LOG_WARN("RemoveTenantFromPlayerHouse: blocked removal of spouse {:08X} because they have children with player",
                           spouse->GetFormID());
            return false;
        }
        return houseSvc.RemoveTenantFromPlayerHouse(spouse->GetFormID());
    }

    void SetTenantStayAtHome(RE::StaticFunctionTag*, RE::Actor* spouse, bool stayAtHome) {
        if (!spouse) return;
        std::lock_guard lock(GetPapyrusMutex());
        MARAS::PlayerHouseService::GetSingleton().SetTenantStayAtHome(spouse->GetFormID(), stayAtHome);
    }

    bool GetTenantStayAtHome(RE::StaticFunctionTag*, RE::Actor* spouse) {
        if (!spouse) return true;
        std::lock_guard lock(GetPapyrusMutex());
        return MARAS::PlayerHouseService::GetSingleton().GetTenantStayAtHome(spouse->GetFormID());
    }

    std::vector<RE::Actor*> GetPlayerHouseTenants(RE::StaticFunctionTag*, RE::BGSLocation* playerHouse) {
        if (!playerHouse) return {};
        std::vector<RE::Actor*> result;
        std::lock_guard lock(GetPapyrusMutex());
        auto ids = MARAS::PlayerHouseService::GetSingleton().GetPlayerHouseTenants(playerHouse->GetFormID());
        result.reserve(ids.size());
        for (auto id : ids) {
            if (auto form = RE::TESForm::LookupByID(id)) {
                if (auto actor = form->As<RE::Actor>()) result.push_back(actor);
            }
        }
        return result;
    }

    RE::TESObjectREFR* GetHouseMarker(RE::StaticFunctionTag*, RE::BGSLocation* playerHouse) {
        if (!playerHouse) return nullptr;
        std::lock_guard lock(GetPapyrusMutex());
        return MARAS::PlayerHouseService::GetSingleton().GetHouseMarkerRef(playerHouse->GetFormID());
    }

    RE::BGSLocation* GetTenantHouse(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) return nullptr;
        std::lock_guard lock(GetPapyrusMutex());
        RE::FormID houseId = MARAS::PlayerHouseService::GetSingleton().GetTenantHouseFormID(npc->GetFormID());
        if (houseId == 0) return nullptr;
        if (auto form = RE::TESForm::LookupByID(houseId)) {
            if (auto loc = form->As<RE::BGSLocation>()) return loc;
        }
        return nullptr;
    }

    int CountPlayerHouses(RE::StaticFunctionTag*) {
        std::lock_guard lock(GetPapyrusMutex());
        return MARAS::PlayerHouseService::GetSingleton().CountPlayerHouses();
    }

    void SetHouseInitialMoveDone(RE::StaticFunctionTag*, RE::BGSLocation* houseLoc, bool done) {
        if (!houseLoc) return;
        std::lock_guard lock(GetPapyrusMutex());
        PlayerHouseService::GetSingleton().SetHouseInitialMoveDone(houseLoc->GetFormID(), done);
    }

    bool IsHouseInitialMoveDone(RE::StaticFunctionTag*, RE::BGSLocation* houseLoc) {
        if (!houseLoc) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerHouseService::GetSingleton().IsHouseInitialMoveDone(houseLoc->GetFormID());
    }

    // ========================================
    // Spouse assets & home cell bindings
    // ========================================

    std::vector<RE::TESObjectREFR*> GetCellDoors(RE::StaticFunctionTag*, RE::TESObjectCELL* currentCell) {
        if (!currentCell) return {};
        std::vector<RE::TESObjectREFR*> result;
        std::lock_guard lock(GetPapyrusMutex());
        auto doorIDs = MARAS::HomeCellService::GetSingleton().GetCellDoors(currentCell->GetFormID());
        result.reserve(doorIDs.size());
        for (auto id : doorIDs) {
            if (auto form = RE::TESForm::LookupByID(id)) {
                if (auto ref = form->As<RE::TESObjectREFR>()) result.push_back(ref);
            }
        }
        return result;
    }

    RE::TESObjectCELL* GetNpcOriginalHouse(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) return nullptr;
        std::lock_guard lock(GetPapyrusMutex());
        RE::FormID cellId = MARAS::HomeCellService::GetSingleton().GetNpcHome(npc);
        if (cellId == 0) return nullptr;
        if (auto form = RE::TESForm::LookupByID(cellId)) {
            return form->As<RE::TESObjectCELL>();
        }
        return nullptr;
    }

    RE::TESObjectREFR* GetNpcOriginalHouseCenterMarker(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("GetNpcOriginalHouseCenterMarker: null NPC provided");
            return nullptr;
        }

        std::lock_guard lock(GetPapyrusMutex());
        RE::FormID markerId = MARAS::HomeCellService::GetSingleton().GetNpcOriginalHouseCenterMarker(npc);
        MARAS_LOG_INFO("GetNpcOriginalHouseCenterMarker (Papyrus): NPC {:08X} -> Marker {:08X}", npc->GetFormID(),
                       markerId);

        if (markerId == 0) {
            MARAS_LOG_INFO("GetNpcOriginalHouseCenterMarker: No marker found for NPC {:08X}", npc->GetFormID());
            return nullptr;
        }

        auto form = RE::TESForm::LookupByID(markerId);
        if (!form) {
            MARAS_LOG_WARN("GetNpcOriginalHouseCenterMarker: Failed to lookup form {:08X}", markerId);
            return nullptr;
        }

        auto ref = form->As<RE::TESObjectREFR>();
        if (!ref) {
            MARAS_LOG_WARN("GetNpcOriginalHouseCenterMarker: Form {:08X} is not a TESObjectREFR", markerId);
            return nullptr;
        }

        MARAS_LOG_INFO("GetNpcOriginalHouseCenterMarker: Returning reference {:08X}", ref->GetFormID());
        return ref;
    }

    std::vector<RE::TESObjectREFR*> GetNpcBeds(RE::StaticFunctionTag*, RE::Actor* npc) {
        std::vector<RE::TESObjectREFR*> result;
        if (!npc) return result;

        std::lock_guard lock(GetPapyrusMutex());
        auto bedIDs = MARAS::HomeCellService::GetSingleton().GetNpcBeds(npc);
        result.reserve(bedIDs.size());
        for (auto id : bedIDs) {
            if (auto form = RE::TESForm::LookupByID(id)) {
                if (auto ref = form->As<RE::TESObjectREFR>()) result.push_back(ref);
            }
        }
        return result;
    }

    bool ShareHouseWithPlayer(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("ShareHouseWithPlayer: null argument(s)");
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        return MARAS::SpouseAssetsService::GetSingleton().ShareHouseWithPlayer(npc->GetFormID());
    }

    bool StopShareHouseWithPlayer(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("StopShareHouseWithPlayer: null npc");
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        return MARAS::SpouseAssetsService::GetSingleton().StopShareHouseWithPlayer(npc->GetFormID());
    }

    bool IsHouseSharedWithPlayer(RE::StaticFunctionTag*, RE::TESObjectCELL* currentCell) {
        if (!currentCell) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return MARAS::SpouseAssetsService::GetSingleton().IsHouseSharedWithPlayer(currentCell->GetFormID());
    }

    bool HasSpouseSharedHouseWithPlayer(RE::StaticFunctionTag*, RE::Actor* spouse) {
        if (!spouse) {
            MARAS_LOG_WARN("HasSpouseSharedHouseWithPlayer: null spouse provided");
            return false;
        }

        std::lock_guard lock(GetPapyrusMutex());
        RE::FormID houseId = MARAS::HomeCellService::GetSingleton().GetNpcHome(spouse);
        if (houseId == 0) return false;
        return MARAS::SpouseAssetsService::GetSingleton().IsHouseSharedWithPlayer(houseId);
    }

    // ========================================
    // Home marker management
    // ========================================

    RE::TESObjectREFR* GetTrackedNpcHomeMarker(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) return nullptr;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().GetOrCreateHomeMarker(npc);
    }

    bool SetTrackedNpcHomeMarker(RE::StaticFunctionTag*, RE::Actor* npc, RE::TESObjectREFR* marker) {
        if (!npc) {
            MARAS_LOG_WARN("SetTrackedNpcHomeMarker: null NPC provided");
            return false;
        }

        std::lock_guard lock(GetPapyrusMutex());
        RE::FormID markerID = marker ? marker->GetFormID() : 0;
        return NPCRelationshipManager::GetSingleton().SetHomeMarker(npc->GetFormID(), markerID);
    }

    RE::TESObjectREFR* GetChildHomeMarker(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return nullptr;
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerChildrenService::GetSingleton().GetOrCreateChildHomeMarker(child);
    }

}  // namespace MARAS::PapyrusInterface

