#include "core/PlayerHouseService.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <string>

#include "core/FormCache.h"
#include "utils/ActorUtils.h"
#include "utils/FormUtils.h"

namespace MARAS {

    PlayerHouseService& PlayerHouseService::GetSingleton() {
        static PlayerHouseService instance;
        return instance;
    }

    namespace {
        constexpr RE::FormID kInvalidFormID = 0;
        constexpr std::int8_t kDefaultFactionRank = 0;
    }

    PlayerHouseService::PlayerHouseService() { Revert(); }

    RE::TESFaction* PlayerHouseService::GetHousedFaction() const {
        if (!housedFactionCache_) {
            housedFactionCache_ = FormCache::GetSingleton().GetSpouseHousedFaction();
        }
        return housedFactionCache_;
    }

    void PlayerHouseService::AddTenantFaction(RE::FormID tenantFormID) {
        auto faction = GetHousedFaction();
        if (!faction) return;
        if (auto actor = RE::TESForm::LookupByID<RE::Actor>(tenantFormID)) {
            actor->AddToFaction(faction, kDefaultFactionRank);
            MARAS_LOG_DEBUG("Added tenant {:08X} to housed faction {:08X}", tenantFormID, faction->GetFormID());
        } else {
            MARAS_LOG_WARN("AddTenantFaction: could not find Actor for tenant {:08X}", tenantFormID);
        }
    }

    void PlayerHouseService::RemoveTenantFaction(RE::FormID tenantFormID) {
        auto faction = GetHousedFaction();
        if (!faction) return;
        if (auto actor = RE::TESForm::LookupByID<RE::Actor>(tenantFormID)) {
            MARAS::Utils::RemoveFromFaction(actor, faction);
            MARAS_LOG_DEBUG("Removed tenant {:08X} from housed faction {:08X}", tenantFormID, faction->GetFormID());
        } else {
            MARAS_LOG_WARN("RemoveTenantFaction: could not find Actor for tenant {:08X}", tenantFormID);
        }
    }

    void PlayerHouseService::AddTenantToHouse(RE::FormID tenantFormID, RE::FormID houseFormID) {
        auto& tenants = houseTenants_[houseFormID];
        if (std::find(tenants.begin(), tenants.end(), tenantFormID) == tenants.end()) {
            tenants.push_back(tenantFormID);
        }
        tenantHouse_[tenantFormID] = houseFormID;
    }

    void PlayerHouseService::RemoveTenantFromHouse(RE::FormID tenantFormID) {
        auto it = tenantHouse_.find(tenantFormID);
        if (it == tenantHouse_.end()) return;
        auto houseFormID = it->second;
        auto tenantsIt = houseTenants_.find(houseFormID);
        if (tenantsIt != houseTenants_.end()) {
            auto& vec = tenantsIt->second;
            vec.erase(std::remove(vec.begin(), vec.end(), tenantFormID), vec.end());
            if (vec.empty()) {
                // optional: we could erase empty vector; keep for serialization ordering
            }
        }
        tenantHouse_.erase(it);
    }

    bool PlayerHouseService::RegisterPlayerHouseCell(RE::FormID locationFormID, RE::FormID markerFormID) {
        if (locationFormID == kInvalidFormID) return false;

        // add house if not present
        if (std::find(houses_.begin(), houses_.end(), locationFormID) == houses_.end()) {
            houses_.push_back(locationFormID);
        }

        if (markerFormID != 0) {
            houseMarkers_[locationFormID] = markerFormID;
            // Diagnostic logging to help trace why marker lookups may fail later.
            if (auto markerForm = RE::TESForm::LookupByID(markerFormID)) {
                const char* mname = markerForm->GetName();
                MARAS_LOG_DEBUG("RegisterPlayerHouseCell: stored marker {:08X} ('{}') for house {:08X}", markerFormID,
                                mname ? mname : "(null)", locationFormID);
            } else {
                MARAS_LOG_DEBUG("RegisterPlayerHouseCell: stored marker {:08X} (no form found) for house {:08X}",
                                markerFormID, locationFormID);
            }
        }

        // Update the player houses count global with diagnostic logging
        if (auto global = FormCache::GetSingleton().GetPlayerHousesCount()) {
            float oldVal = global->value;
            float newVal = static_cast<float>(houses_.size());
            if (oldVal != newVal) {
                MARAS_LOG_INFO("Player houses count global updated from {} to {}", oldVal, newVal);
            } else {
                MARAS_LOG_DEBUG("Player houses count global unchanged at {}", oldVal);
            }
            global->value = newVal;
        } else {
            MARAS_LOG_WARN("RegisterPlayerHouseCell: Player houses count global not found; could not update count ({})",
                           houses_.size());
        }

        return true;
    }

    std::vector<RE::FormID> PlayerHouseService::GetAllPlayerHouses() const { return houses_; }

    bool PlayerHouseService::RegisterTenantInPlayerHouse(RE::FormID tenantFormID, RE::FormID playerHouseFormID) {
        if (tenantFormID == kInvalidFormID || playerHouseFormID == kInvalidFormID) return false;

        auto existing = tenantHouse_.find(tenantFormID);
        if (existing != tenantHouse_.end()) {
            if (existing->second == playerHouseFormID) {
                // Already registered in target house
                return true;
            }
            // Move: remove from old house (retain faction semantics: remove then re-add)
            RemoveTenantFaction(tenantFormID);
            RemoveTenantFromHouse(tenantFormID);
        }

        AddTenantToHouse(tenantFormID, playerHouseFormID);
        AddTenantFaction(tenantFormID);
        MARAS_LOG_DEBUG("Registered tenant {:08X} in player house {:08X}", tenantFormID, playerHouseFormID);
        return true;
    }

    bool PlayerHouseService::RemoveTenantFromPlayerHouse(RE::FormID tenantFormID) {
        if (tenantFormID == kInvalidFormID) return false;
        auto existing = tenantHouse_.find(tenantFormID);
        if (existing == tenantHouse_.end()) return false;
        // Capture the house id for logging before we remove the mapping
        auto houseFormID = existing->second;

        // First remove the tenant from our house data structures, then update faction
        // This ordering avoids potential re-entrancy issues where faction operations
        // might trigger code that queries the player-house mappings while they
        // are still in an inconsistent state.
        RemoveTenantFromHouse(tenantFormID);
        RemoveTenantFaction(tenantFormID);
        MARAS_LOG_DEBUG("Removed tenant {:08X} from player house {:08X}", tenantFormID, houseFormID);
        return true;
    }

    std::vector<RE::FormID> PlayerHouseService::GetPlayerHouseTenants(RE::FormID playerHouseFormID) const {
        if (playerHouseFormID == 0) return {};
        auto it = houseTenants_.find(playerHouseFormID);
        if (it == houseTenants_.end()) return {};
        return it->second;
    }

    RE::FormID PlayerHouseService::GetHouseMarkerFormID(RE::FormID playerHouseFormID) const noexcept {
        if (playerHouseFormID == kInvalidFormID) return kInvalidFormID;
        auto it = houseMarkers_.find(playerHouseFormID);
        if (it == houseMarkers_.end()) {
            MARAS_LOG_DEBUG("GetHouseMarkerFormID: no marker registered for house {:08X}", playerHouseFormID);
            return kInvalidFormID;
        }
        auto marker = it->second;
        if (auto markerForm = RE::TESForm::LookupByID(marker)) {
            const char* mname = markerForm->GetName();
            MARAS_LOG_DEBUG("GetHouseMarkerFormID: found marker {:08X} ('{}') for house {:08X}", marker,
                            mname ? mname : "(null)", playerHouseFormID);
        } else {
            MARAS_LOG_DEBUG("GetHouseMarkerFormID: found marker {:08X} (no form) for house {:08X}", marker,
                            playerHouseFormID);
        }
        return marker;
    }

    std::vector<std::string> PlayerHouseService::GetAllPlayerHousesNames() const {
        std::vector<std::string> names;
        names.reserve(houses_.size());
        for (auto id : houses_) {
            if (auto form = RE::TESForm::LookupByID(id)) {
                const char* n = form->GetName();
                if (n)
                    names.emplace_back(n);
                else
                    names.emplace_back("");
            } else {
                names.emplace_back("");
            }
        }
        return names;
    }

    RE::FormID PlayerHouseService::GetTenantHouseFormID(RE::FormID tenantFormID) const noexcept {
        if (tenantFormID == kInvalidFormID) return kInvalidFormID;
        auto it = tenantHouse_.find(tenantFormID);
        if (it == tenantHouse_.end()) return kInvalidFormID;
        return it->second;
    }

    int PlayerHouseService::CountPlayerHouses() const noexcept { return static_cast<int>(houses_.size()); }

    bool PlayerHouseService::Save(SKSE::SerializationInterface* serialization) const {
        if (!serialization) return false;

        // Write number of houses
        std::uint32_t houseCount = static_cast<std::uint32_t>(houses_.size());
        if (!serialization->WriteRecordData(houseCount)) return false;

        for (auto house : houses_) {
            if (!serialization->WriteRecordData(house)) return false;
            // marker
            RE::FormID marker = 0;
            auto mit = houseMarkers_.find(house);
            if (mit != houseMarkers_.end()) marker = mit->second;
            if (!serialization->WriteRecordData(marker)) return false;

            // tenants
            auto tit = houseTenants_.find(house);
            std::uint32_t tenantCount = 0;
            if (tit != houseTenants_.end()) tenantCount = static_cast<std::uint32_t>(tit->second.size());
            if (!serialization->WriteRecordData(tenantCount)) return false;
            if (tit != houseTenants_.end()) {
                for (auto t : tit->second) {
                    if (!serialization->WriteRecordData(t)) return false;
                }
            }
        }

        MARAS_LOG_INFO("Saved {} player houses", houseCount);
        return true;
    }

    bool PlayerHouseService::Load(SKSE::SerializationInterface* serialization) {
        if (!serialization) return false;

        Revert();

        std::uint32_t houseCount = 0;
        if (!serialization->ReadRecordData(houseCount)) return false;

        houses_.reserve(houseCount);
        for (std::uint32_t i = 0; i < houseCount; ++i) {
            RE::FormID savedHouse = kInvalidFormID;
            if (!serialization->ReadRecordData(savedHouse)) return false;
            RE::FormID house = 0;
            if (savedHouse != 0 && !serialization->ResolveFormID(savedHouse, house)) {
                // Could not resolve house; need to consume marker and tenants entries but skip storing
                RE::FormID markerSkip = kInvalidFormID;
                if (!serialization->ReadRecordData(markerSkip)) return false;
                std::uint32_t tenantCountSkip = 0;
                if (!serialization->ReadRecordData(tenantCountSkip)) return false;
                for (std::uint32_t j = 0; j < tenantCountSkip; ++j) {
                    RE::FormID tidSkip = kInvalidFormID;
                    if (!serialization->ReadRecordData(tidSkip)) return false;
                }
                continue;  // move to next house
            }
            houses_.push_back(house);

            RE::FormID savedMarker = kInvalidFormID;
            if (!serialization->ReadRecordData(savedMarker)) return false;
            RE::FormID marker = 0;
            if (savedMarker != 0 && serialization->ResolveFormID(savedMarker, marker) && marker != kInvalidFormID) {
                houseMarkers_[house] = marker;
            }

            std::uint32_t tenantCount = 0;
            if (!serialization->ReadRecordData(tenantCount)) return false;
            if (tenantCount > 0) {
                auto& tenants = houseTenants_[house];
                tenants.reserve(tenantCount);
                for (std::uint32_t j = 0; j < tenantCount; ++j) {
                    RE::FormID savedTid = kInvalidFormID;
                    if (!serialization->ReadRecordData(savedTid)) return false;
                    RE::FormID resolvedTid = 0;
                    if (savedTid != 0 && serialization->ResolveFormID(savedTid, resolvedTid)) {
                        tenants.push_back(resolvedTid);
                        tenantHouse_[resolvedTid] = house;  // reconstruct reverse map
                    }
                }
            }
        }

        MARAS_LOG_INFO("Loaded {} player houses", houseCount);
        return true;
    }

    void PlayerHouseService::Revert() {
        houses_.clear();
        houseMarkers_.clear();
        houseTenants_.clear();
        tenantHouse_.clear();
        housedFactionCache_ = nullptr;
        MARAS_LOG_INFO("Reverted player house data");
    }

}  // namespace MARAS
