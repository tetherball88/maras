#pragma once

#include "maras/engine/EngineLogging.h"

#include <optional>
#include <string>
#include <string_view>

namespace MARAS {

    // Skyrim engine operations for player houses, spouse home markers, linked refs, actor relocation, and load reconciliation.
    class HouseholdEngineOps {
    public:
        static HouseholdEngineOps& GetSingleton();

        // House lookup helpers.
        [[nodiscard]] static std::string GetHouseName(RE::FormID houseFormId);
        [[nodiscard]] RE::FormID FindHouseByName(std::string_view name) const;
        [[nodiscard]] RE::FormID GetHouseMarkerFormID(RE::FormID houseFormId) const noexcept;
        [[nodiscard]] RE::TESObjectREFR* GetHouseMarkerRef(RE::FormID houseFormId) const;

        // Adult spouse home marker & 0x6D linked ref helpers.
        void SetLinkedRefForHomeMarker(RE::FormID npcFormId, RE::FormID markerFormId) const;
        void ClearHomeMarkerLinkedRef(RE::FormID npcFormId) const;
        RE::TESObjectREFR* GetOrCreateHomeMarker(RE::Actor* npcActor);
        bool UpdateSpouseHomeMarker(RE::FormID spouseFormId, RE::FormID houseFormId);

        // Moves an actor reference to a marker or assigned house marker and refreshes AI package evaluation.
        bool MoveActorToMarker(RE::Actor* actor, RE::TESObjectREFR* targetMarker, bool evaluatePackage = true) const;
        bool MoveActorToHouse(RE::FormID actorId, RE::FormID houseId, bool evaluatePackage = true);
        void RefreshActorAfterMove(RE::Actor* actor) const;

        // Post-load engine sync for globals and all non-child house tenants.
        void ReconcileTenantsOnLoad();

        // Unlocks pickable spouse home doors and grants missing house keys to the Player.
        void UnlockAndGrantSpouseHomeAccess(RE::FormID homeCellFormId);

    private:
        HouseholdEngineOps() = default;
        ~HouseholdEngineOps() = default;
        HouseholdEngineOps(const HouseholdEngineOps&) = delete;
        HouseholdEngineOps& operator=(const HouseholdEngineOps&) = delete;
    };

}  // namespace MARAS
