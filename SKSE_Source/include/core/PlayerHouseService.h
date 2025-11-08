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
        bool RegisterPlayerHouseCell(RE::FormID locationFormID, RE::FormID markerFormID);

        // Returns list of registered player house FormIDs
        std::vector<RE::FormID> GetAllPlayerHouses() const;

        // Tenant management
        bool RegisterTenantInPlayerHouse(RE::FormID tenantFormID, RE::FormID playerHouseFormID);
        bool RemoveTenantFromPlayerHouse(RE::FormID tenantFormID);
        std::vector<RE::FormID> GetPlayerHouseTenants(RE::FormID playerHouseFormID) const;

        // Serialization
        bool Save(SKSE::SerializationInterface* serialization) const;
        bool Load(SKSE::SerializationInterface* serialization);
        void Revert();

    private:
        PlayerHouseService();

        // list of house locations (FormIDs)
        std::vector<RE::FormID> houses_;

        // mapping house -> marker formid
        std::unordered_map<RE::FormID, RE::FormID> houseMarkers_;

        // mapping house -> tenants
        std::unordered_map<RE::FormID, std::vector<RE::FormID>> houseTenants_;

    public:
        // Return stored marker FormID for a house (0 if none)
        RE::FormID GetHouseMarkerFormID(RE::FormID playerHouseFormID) const;

        // Return list of house names
        std::vector<std::string> GetAllPlayerHousesNames() const;

        // Return the FormID of the house a tenant is registered in (0 if none)
        RE::FormID GetTenantHouseFormID(RE::FormID tenantFormID) const;

        // Return number of registered player houses
        int CountPlayerHouses() const;
    };

}  // namespace MARAS
