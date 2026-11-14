#include "maras/bridge_papyrus/PapyrusInterface.h"

#include "maras/app/HouseholdUseCases.h"
#include "maras/app/RelationshipUseCases.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/PlayerChildrenEngineOps.h"
#include "maras/engine/SpatialWorldIndex.h"

namespace MARAS::PapyrusInterface {

    namespace {
        template <class FormT>
        [[nodiscard]] std::vector<FormT*> LookupFormsById(const std::vector<RE::FormID>& formIds) {
            std::vector<FormT*> result;
            result.reserve(formIds.size());
            for (const RE::FormID id : formIds) {
                if (id == 0) {
                    continue;
                }
                if (auto* form = RE::TESForm::LookupByID<FormT>(id)) {
                    result.push_back(form);
                }
            }
            return result;
        }

        [[nodiscard]] std::vector<RE::Actor*> ResolveActorsById(const std::vector<RE::FormID>& actorIds) {
            std::vector<RE::Actor*> result;
            result.reserve(actorIds.size());
            const auto& index = SpatialWorldIndex::GetSingleton();
            for (const RE::FormID id : actorIds) {
                if (id == 0) {
                    continue;
                }
                if (auto* actor = index.GetActorFromFormId(id)) {
                    result.push_back(actor);
                }
            }
            return result;
        }
    }  // namespace

    // ========================================
    // Player House & Tenant Management
    // ========================================

    bool RegisterPlayerHouseCell(RE::StaticFunctionTag*, RE::BGSLocation* loc, RE::TESObjectREFR* homeMarker) {
        if (!loc || !homeMarker) {
            MARAS_LOG_WARN("RegisterPlayerHouseCell: null location or marker provided");
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::HouseholdUseCases::GetSingleton().RegisterPlayerHouseCell(loc->GetFormID(), homeMarker->GetFormID());
    }

    std::vector<RE::BGSLocation*> GetAllPlayerHouses(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return LookupFormsById<RE::BGSLocation>(App::HouseholdUseCases::GetSingleton().GetAllPlayerHouses());
    }

    std::vector<std::string> GetAllPlayerHousesNames(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::HouseholdUseCases::GetSingleton().GetAllPlayerHousesNames();
    }

    bool RegisterTenantInPlayerHouse(RE::StaticFunctionTag*,
                                     RE::Actor* spouse,
                                     RE::BGSLocation* playerHouse,
                                     bool stayAtHome) {
        if (!spouse || !playerHouse) {
            MARAS_LOG_WARN("RegisterTenantInPlayerHouse: null spouse or house provided");
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::HouseholdUseCases::GetSingleton().RegisterTenantInPlayerHouse(
            spouse->GetFormID(), playerHouse->GetFormID(), stayAtHome);
    }

    bool RemoveTenantFromPlayerHouse(RE::StaticFunctionTag*, RE::Actor* spouse) {
        if (!spouse) {
            MARAS_LOG_WARN("RemoveTenantFromPlayerHouse: null spouse provided");
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::HouseholdUseCases::GetSingleton().RemoveTenantFromPlayerHouse(spouse->GetFormID());
    }

    void SetTenantStayAtHome(RE::StaticFunctionTag*, RE::Actor* spouse, bool stayAtHome) {
        if (!spouse) {
            MARAS_LOG_WARN("SetTenantStayAtHome: null spouse provided");
            return;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::HouseholdUseCases::GetSingleton().SetTenantStayAtHome(spouse->GetFormID(), stayAtHome);
    }

    bool GetTenantStayAtHome(RE::StaticFunctionTag*, RE::Actor* spouse) {
        if (!spouse) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::HouseholdUseCases::GetSingleton().GetTenantStayAtHome(spouse->GetFormID());
    }

    std::vector<RE::Actor*> GetPlayerHouseTenants(RE::StaticFunctionTag*, RE::BGSLocation* playerHouse) {
        if (!playerHouse) {
            return {};
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorsById(
            App::HouseholdUseCases::GetSingleton().GetPlayerHouseTenants(playerHouse->GetFormID()));
    }

    RE::TESObjectREFR* GetHouseMarker(RE::StaticFunctionTag*, RE::BGSLocation* playerHouse) {
        if (!playerHouse) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::HouseholdUseCases::GetSingleton().GetHouseMarkerRef(playerHouse->GetFormID());
    }

    RE::BGSLocation* GetTenantHouse(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID houseID = App::HouseholdUseCases::GetSingleton().GetTenantHouseFormID(npc->GetFormID());
        return houseID != 0 ? RE::TESForm::LookupByID<RE::BGSLocation>(houseID) : nullptr;
    }

    int32_t CountPlayerHouses(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(App::HouseholdUseCases::GetSingleton().CountPlayerHouses());
    }

    std::vector<RE::TESObjectREFR*> GetCellDoors(RE::StaticFunctionTag*, RE::TESObjectCELL* currentCell) {
        if (!currentCell) {
            return {};
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return LookupFormsById<RE::TESObjectREFR>(
            SpatialWorldIndex::GetSingleton().GetCellDoors(currentCell->GetFormID()));
    }

    RE::TESObjectCELL* GetNpcOriginalHouse(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID cellID = SpatialWorldIndex::GetSingleton().GetNpcHome(npc);
        return cellID != 0 ? RE::TESForm::LookupByID<RE::TESObjectCELL>(cellID) : nullptr;
    }

    RE::TESObjectREFR* GetNpcOriginalHouseCenterMarker(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID markerID = SpatialWorldIndex::GetSingleton().GetNpcOriginalHouseCenterMarker(npc);
        return markerID != 0 ? RE::TESForm::LookupByID<RE::TESObjectREFR>(markerID) : nullptr;
    }

    std::vector<RE::TESObjectREFR*> GetNpcBeds(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            return {};
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return LookupFormsById<RE::TESObjectREFR>(SpatialWorldIndex::GetSingleton().GetNpcBeds(npc));
    }

    // ========================================
    // Shared Spouse Houses & Home Markers
    // ========================================

    bool ShareHouseWithPlayer(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("ShareHouseWithPlayer: null npc");
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::HouseholdUseCases::GetSingleton().ShareHouseWithPlayer(npc->GetFormID());
    }

    bool StopShareHouseWithPlayer(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("StopShareHouseWithPlayer: null npc");
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::HouseholdUseCases::GetSingleton().StopShareHouseWithPlayer(npc->GetFormID());
    }

    bool IsHouseSharedWithPlayer(RE::StaticFunctionTag*, RE::TESObjectCELL* currentCell) {
        if (!currentCell) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::HouseholdUseCases::GetSingleton().IsHouseSharedWithPlayer(currentCell->GetFormID());
    }

    bool HasSpouseSharedHouseWithPlayer(RE::StaticFunctionTag*, RE::Actor* spouse) {
        if (!spouse) {
            MARAS_LOG_WARN("HasSpouseSharedHouseWithPlayer: null spouse provided");
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID houseID = SpatialWorldIndex::GetSingleton().GetNpcHome(spouse);
        if (houseID == 0) {
            return false;
        }
        return App::HouseholdUseCases::GetSingleton().IsHouseSharedWithPlayer(houseID);
    }

    RE::TESObjectREFR* GetTrackedNpcHomeMarker(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::RelationshipUseCases::GetSingleton().GetOrCreateHomeMarker(npc);
    }

    bool SetTrackedNpcHomeMarker(RE::StaticFunctionTag*, RE::Actor* npc, RE::TESObjectREFR* marker) {
        if (!npc) {
            MARAS_LOG_WARN("SetTrackedNpcHomeMarker: null NPC provided");
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID markerID = marker ? marker->GetFormID() : 0;
        return App::RelationshipUseCases::GetSingleton().SetHomeMarker(npc->GetFormID(), markerID);
    }

    RE::TESObjectREFR* GetChildHomeMarker(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return PlayerChildrenEngineOps::GetSingleton().GetOrCreateChildHomeMarker(child);
    }

}  // namespace MARAS::PapyrusInterface
