#pragma once

#include "PCH.h"
#include "core/Serialization.h"

namespace MARAS {

    class PlayerHouseService {
    public:
        struct House {
            RE::FormID id{0};
            RE::FormID marker{0};
            bool initialMoveDone{false};
            std::vector<RE::FormID> tenants;
        };

        struct Tenant {
            RE::FormID house{0};
            bool stayAtHome{true};
        };

        static PlayerHouseService& GetSingleton();

        // Register a player house location and store a custom placed marker for it
        [[nodiscard]] bool RegisterPlayerHouseCell(RE::FormID locationFormID, RE::FormID markerFormID);

        // House queries
        std::vector<RE::FormID> GetAllPlayerHouses() const;
        std::vector<std::string> GetAllPlayerHousesNames() const;
        int CountPlayerHouses() const noexcept;
        RE::FormID GetHouseMarkerFormID(RE::FormID playerHouseFormID) const noexcept;
        RE::TESObjectREFR* GetHouseMarkerRef(RE::FormID playerHouseFormID) const;
        static std::string GetHouseName(RE::FormID houseFormID);
        RE::FormID FindHouseByName(std::string_view name) const;

        // Tenant management
        [[nodiscard]] bool RegisterTenantInPlayerHouse(RE::FormID tenantFormID, RE::FormID playerHouseFormID, bool stayAtHome = true);
        [[nodiscard]] bool RemoveTenantFromPlayerHouse(RE::FormID tenantFormID);
        std::vector<RE::FormID> GetPlayerHouseTenants(RE::FormID playerHouseFormID) const;
        void SetTenantStayAtHome(RE::FormID tenantFormID, bool stayAtHome);
        [[nodiscard]] bool GetTenantStayAtHome(RE::FormID tenantFormID) const;
        RE::FormID GetTenantHouseFormID(RE::FormID tenantFormID) const noexcept;
        [[nodiscard]] bool CanRemoveTenant(RE::FormID tenantFormID) const;

        // Main unified method to relocate an entire player-related family unit (specific player spouse + children assigned to that spouse)
        // Accepts either a child NPC or adult spouse NPC. Strictly enforces player-family boundary and polygamy isolation.
        // Returns vector of all relocated member FormIDs (spouse and assigned children).
        std::vector<RE::FormID> RelocateFamilyUnit(RE::FormID npcFormID, RE::FormID playerHouseFormID, bool moveParents = true);

        // Per-house initial move state
        void SetHouseInitialMoveDone(RE::FormID houseFormID, bool done);
        bool IsHouseInitialMoveDone(RE::FormID houseFormID) const;

        // Serialization
        [[nodiscard]] bool Save(SKSE::SerializationInterface* serialization) const;
        [[nodiscard]] bool Load(SKSE::SerializationInterface* serialization, std::uint32_t version = Serialization::kDataVersion);
        void Revert();

        // Data dump
        [[nodiscard]] nlohmann::json DumpJson() const;

    private:
        PlayerHouseService();

        std::vector<House> houses_;
        std::unordered_map<RE::FormID, Tenant> tenants_;

        House* FindHouse(RE::FormID houseFormID);
        const House* FindHouse(RE::FormID houseFormID) const;

        bool UpdateSpouseHomeMarker(RE::FormID spouseFormID, RE::FormID playerHouseFormID);
        void SetHousedFaction(RE::FormID tenantFormID, bool add);
        void AddMerchantFactionsToTenant(RE::FormID tenantFormID);
        void RemoveMerchantFactionsFromTenant(RE::FormID tenantFormID);
        void ApplyStayAtHome(RE::FormID tenantFormID, bool stayAtHome);
    };

}  // namespace MARAS
