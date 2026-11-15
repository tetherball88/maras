#pragma once

#include "RE/Skyrim.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::Projection {
    class PerspectiveView;
}

namespace MARAS::App {

    /**
     * Application-layer household, tenant housing, shared spouse asset, and adult-child
     * independent travel coordinator.
     *
     * Consolidates legacy PlayerHouseService, SpouseAssetsService, and AdultChildTravelService
     * orchestration on top of State::WorldStore, Domain::Household rules, and Layer 3 engine
     * reconcilers (HouseholdEngineOps, BedOwnershipReconciler, SpatialWorldIndex,
     * FactionReconciler, PackageReconciler, GlobalVarReconciler).
     */
    class HouseholdUseCases {
    public:
        static HouseholdUseCases& GetSingleton();

        // ---------------------------------------------------------------------
        // Player Houses & Tenants (PlayerHouseService)
        // ---------------------------------------------------------------------

        [[nodiscard]] bool RegisterPlayerHouseCell(RE::FormID locationFormID, RE::FormID markerFormID);

        [[nodiscard]] std::vector<RE::FormID> GetAllPlayerHouses() const;
        [[nodiscard]] std::vector<std::string> GetAllPlayerHousesNames() const;
        [[nodiscard]] int CountPlayerHouses() const noexcept;

        [[nodiscard]] RE::FormID GetHouseMarkerFormID(RE::FormID playerHouseFormID) const noexcept;
        [[nodiscard]] RE::TESObjectREFR* GetHouseMarkerRef(RE::FormID playerHouseFormID) const;
        [[nodiscard]] static std::string GetHouseName(RE::FormID houseFormID);
        [[nodiscard]] RE::FormID FindHouseByName(std::string_view name) const;

        [[nodiscard]] bool RegisterTenantInPlayerHouse(
            RE::FormID tenantFormID,
            RE::FormID playerHouseFormID,
            bool stayAtHome = true);
        [[nodiscard]] bool RemoveTenantFromPlayerHouse(RE::FormID tenantFormID);

        [[nodiscard]] std::vector<RE::FormID> GetPlayerHouseTenants(RE::FormID playerHouseFormID) const;
        void SetTenantStayAtHome(RE::FormID tenantFormID, bool stayAtHome);
        [[nodiscard]] bool GetTenantStayAtHome(RE::FormID tenantFormID) const;
        [[nodiscard]] RE::FormID GetTenantHouseFormID(RE::FormID tenantFormID) const noexcept;
        [[nodiscard]] bool CanRemoveTenant(RE::FormID tenantFormID) const;

        /**
         * Relocates a spouse and/or player-custodied dependent children to target house.
         * Returns vector of all relocated member FormIDs (adults first, then children).
         */
        std::vector<RE::FormID> RelocateFamilyUnit(
            RE::FormID npcFormID,
            RE::FormID playerHouseFormID,
            bool moveParents = true);

        void SetHouseInitialMoveDone(RE::FormID houseFormID, bool done);
        [[nodiscard]] bool IsHouseInitialMoveDone(RE::FormID houseFormID) const;

        // ---------------------------------------------------------------------
        // Shared Spouse Assets (SpouseAssetsService)
        // ---------------------------------------------------------------------

        bool ShareHouseWithPlayer(RE::FormID npcFormID);
        bool StopShareHouseWithPlayer(RE::FormID npcFormID, std::string_view reason = "");
        [[nodiscard]] bool IsHouseSharedWithPlayer(RE::FormID cellFormID) const;

        // ---------------------------------------------------------------------
        // Adult Child Independent Travel (AdultChildTravelService)
        // ---------------------------------------------------------------------

        void BuildDestinationIndex();
        bool AssignOrUpdateDestination(RE::FormID childID, bool forceRotate = false);
        void UnregisterAdultChild(RE::FormID childID);
        void TickAdultChildTravel(float currentGameDay);
        void ReassertAllTravelOnLoad();

        [[nodiscard]] RE::FormID GetCurrentTargetMarkerID(RE::FormID childID) const;
        [[nodiscard]] std::string GetCurrentDestinationName(RE::FormID childID) const;

        // ---------------------------------------------------------------------
        // Lifecycle & Serialization Hooks
        // ---------------------------------------------------------------------

        void FinalizeLoad();
        void FinalizeCoSaveLoad(bool loadedAny);
        void PollTick(float currentGameDay);
        void ProcessDailyTick(float currentGameDay);
        void ProcessDayChange(float currentGameDay);
        void Revert();

        // ---------------------------------------------------------------------
        // Read-Only Diagnostic Dumps (CQS)
        // ---------------------------------------------------------------------

        [[nodiscard]] nlohmann::json DumpHousesJson() const;
        [[nodiscard]] nlohmann::json DumpSharedAssetsJson() const;
        [[nodiscard]] nlohmann::json DumpTravelJson(const Projection::PerspectiveView& view) const;
        [[nodiscard]] nlohmann::json DumpTravelJson() const;

    private:
        HouseholdUseCases() = default;
        ~HouseholdUseCases() = default;
        HouseholdUseCases(const HouseholdUseCases&) = delete;
        HouseholdUseCases& operator=(const HouseholdUseCases&) = delete;

        mutable std::shared_mutex travelMutex_{};
    };

}  // namespace MARAS::App
