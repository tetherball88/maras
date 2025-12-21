#pragma once

#include <unordered_map>
#include <vector>

#include "PCH.h"
#include "core/Serialization.h"

namespace MARAS {

    class PlayerHouseService {
    public:
        static PlayerHouseService& GetSingleton();

        // Register a player house location and store a custom placed marker for it
        [[nodiscard]] bool RegisterPlayerHouseCell(RE::FormID locationFormID, RE::FormID markerFormID);

        // Returns list of registered player house FormIDs
        std::vector<RE::FormID> GetAllPlayerHouses() const;

        // Tenant management
        [[nodiscard]] bool RegisterTenantInPlayerHouse(RE::FormID tenantFormID, RE::FormID playerHouseFormID);
        [[nodiscard]] bool RemoveTenantFromPlayerHouse(RE::FormID tenantFormID);
        std::vector<RE::FormID> GetPlayerHouseTenants(RE::FormID playerHouseFormID) const;

        // Serialization
        [[nodiscard]] bool Save(SKSE::SerializationInterface* serialization) const;
        [[nodiscard]] bool Load(SKSE::SerializationInterface* serialization);
        void Revert();

    private:
        PlayerHouseService();

        // list of house locations (FormIDs)
        std::vector<RE::FormID> houses_;

        // mapping house -> marker formid
        std::unordered_map<RE::FormID, RE::FormID> houseMarkers_;

        // mapping house -> tenants
        std::unordered_map<RE::FormID, std::vector<RE::FormID>> houseTenants_;

        // reverse mapping tenant -> house (for O(1) lookup)
        std::unordered_map<RE::FormID, RE::FormID> tenantHouse_;

        // Cached housed faction pointer (mutable so we can lazy fetch in const getters if needed)
        mutable RE::TESFaction* housedFactionCache_{nullptr};

        // Internal helpers (not exposed publicly)
        void AddTenantToHouse(RE::FormID tenantFormID, RE::FormID houseFormID);
        void RemoveTenantFromHouse(RE::FormID tenantFormID);
        void AddTenantFaction(RE::FormID tenantFormID);
        void RemoveTenantFaction(RE::FormID tenantFormID);
        void AddMerchantFactionsToTenant(RE::FormID tenantFormID);
        void RemoveMerchantFactionsFromTenant(RE::FormID tenantFormID);
        RE::TESFaction* GetHousedFaction() const;

    public:
        // Return stored marker FormID for a house (0 if none)
        RE::FormID GetHouseMarkerFormID(RE::FormID playerHouseFormID) const noexcept;

        // Return list of house names
        std::vector<std::string> GetAllPlayerHousesNames() const;

        // Return the FormID of the house a tenant is registered in (0 if none)
        RE::FormID GetTenantHouseFormID(RE::FormID tenantFormID) const noexcept;

        // Return number of registered player houses
        int CountPlayerHouses() const noexcept;
    };

}  // namespace MARAS
