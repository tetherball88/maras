#include "core/PlayerHouseService.h"

#include <spdlog/spdlog.h>

#include <string>

#include "utils/ActorUtils.h"
#include "utils/FormUtils.h"

namespace MARAS {

    PlayerHouseService& PlayerHouseService::GetSingleton() {
        static PlayerHouseService instance;
        return instance;
    }

    PlayerHouseService::PlayerHouseService() {
        houses_.clear();
        houseMarkers_.clear();
        houseTenants_.clear();
    }

    bool PlayerHouseService::RegisterPlayerHouseCell(RE::FormID locationFormID, RE::FormID markerFormID) {
        if (locationFormID == 0) return false;

        // add house if not present
        if (std::find(houses_.begin(), houses_.end(), locationFormID) == houses_.end()) {
            houses_.push_back(locationFormID);
        }

        if (markerFormID != 0) {
            houseMarkers_[locationFormID] = markerFormID;
        }

        return true;
    }

    std::vector<RE::FormID> PlayerHouseService::GetAllPlayerHouses() const { return houses_; }

    bool PlayerHouseService::RegisterTenantInPlayerHouse(RE::FormID tenantFormID, RE::FormID playerHouseFormID) {
        if (tenantFormID == 0 || playerHouseFormID == 0) return false;

        // If tenant is already registered in another house, remove them from that house first.
        for (auto& kv : houseTenants_) {
            auto& vec = kv.second;
            auto it = std::find(vec.begin(), vec.end(), tenantFormID);
            if (it != vec.end()) {
                // If already in the requested house, nothing to do
                if (kv.first == playerHouseFormID) {
                    return true;
                }
                // Remove from old house
                // Also remove the 'spouse housed' faction for the tenant since they're being moved
                auto housedFaction = MARAS::Utils::LookupForm<RE::TESFaction>(0x6c, "TT_MARAS.esp");
                if (housedFaction) {
                    auto actor = RE::TESForm::LookupByID<RE::Actor>(tenantFormID);
                    if (actor) {
                        MARAS::Utils::RemoveFromFaction(actor, housedFaction);
                        MARAS_LOG_DEBUG("Moved tenant {:08X}: removed housed faction {:08X} from old house",
                                        tenantFormID, housedFaction->GetFormID());
                    }
                }
                vec.erase(it);
                break;
            }
        }

        auto& tenants = houseTenants_[playerHouseFormID];
        if (std::find(tenants.begin(), tenants.end(), tenantFormID) == tenants.end()) {
            tenants.push_back(tenantFormID);
            // Also add the tenant actor to the 'spouse housed' faction (0x6c in TT_MARAS.esp)
            auto housedFaction = MARAS::Utils::LookupForm<RE::TESFaction>(0x6c, "TT_MARAS.esp");
            if (housedFaction) {
                auto actor = RE::TESForm::LookupByID<RE::Actor>(tenantFormID);
                if (actor) {
                    // Use rank 0 for membership (matches Papyrus AddToFaction default behavior)
                    actor->AddToFaction(housedFaction, static_cast<std::int8_t>(0));
                    MARAS_LOG_DEBUG("Added tenant {:08X} to housed faction {:08X}", tenantFormID,
                                    housedFaction->GetFormID());
                } else {
                    MARAS_LOG_WARN("RegisterTenantInPlayerHouse: could not find Actor for tenant {:08X}", tenantFormID);
                }
            } else {
                MARAS_LOG_WARN(
                    "RegisterTenantInPlayerHouse: could not find SpouseHousedFaction (0x6c) in TT_MARAS.esp");
            }
        }

        return true;
    }

    bool PlayerHouseService::RemoveTenantFromPlayerHouse(RE::FormID tenantFormID) {
        if (tenantFormID == 0) return false;

        for (auto& kv : houseTenants_) {
            auto& vec = kv.second;
            auto it = std::find(vec.begin(), vec.end(), tenantFormID);
            if (it != vec.end()) {
                // Before erasing, attempt to remove the tenant from the 'spouse housed' faction
                auto housedFaction = MARAS::Utils::LookupForm<RE::TESFaction>(0x6c, "TT_MARAS.esp");
                if (housedFaction) {
                    auto actor = RE::TESForm::LookupByID<RE::Actor>(tenantFormID);
                    if (actor) {
                        // Use helper to remove from faction (engine call if available, fallback to rank -1)
                        MARAS::Utils::RemoveFromFaction(actor, housedFaction);
                        MARAS_LOG_DEBUG("Removed tenant {:08X} from housed faction {:08X}", tenantFormID,
                                        housedFaction->GetFormID());
                    } else {
                        MARAS_LOG_WARN("RemoveTenantFromPlayerHouse: could not find Actor for tenant {:08X}",
                                       tenantFormID);
                    }
                } else {
                    MARAS_LOG_WARN(
                        "RemoveTenantFromPlayerHouse: could not find SpouseHousedFaction (0x6c) in TT_MARAS.esp");
                }

                vec.erase(it);
                return true;
            }
        }
        return false;
    }

    std::vector<RE::FormID> PlayerHouseService::GetPlayerHouseTenants(RE::FormID playerHouseFormID) const {
        if (playerHouseFormID == 0) return {};
        auto it = houseTenants_.find(playerHouseFormID);
        if (it == houseTenants_.end()) return {};
        return it->second;
    }

    RE::FormID PlayerHouseService::GetHouseMarkerFormID(RE::FormID playerHouseFormID) const {
        if (playerHouseFormID == 0) return 0;
        auto it = houseMarkers_.find(playerHouseFormID);
        if (it == houseMarkers_.end()) return 0;
        return it->second;
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

    RE::FormID PlayerHouseService::GetTenantHouseFormID(RE::FormID tenantFormID) const {
        if (tenantFormID == 0) return 0;
        for (const auto& kv : houseTenants_) {
            const auto& vec = kv.second;
            if (std::find(vec.begin(), vec.end(), tenantFormID) != vec.end()) {
                return kv.first;
            }
        }
        return 0;
    }

    int PlayerHouseService::CountPlayerHouses() const { return static_cast<int>(houses_.size()); }

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

        SKSE::log::info("Saved {} player houses", houseCount);
        return true;
    }

    bool PlayerHouseService::Load(SKSE::SerializationInterface* serialization) {
        if (!serialization) return false;

        houses_.clear();
        houseMarkers_.clear();
        houseTenants_.clear();

        std::uint32_t houseCount = 0;
        if (!serialization->ReadRecordData(houseCount)) return false;

        for (std::uint32_t i = 0; i < houseCount; ++i) {
            RE::FormID house = 0;
            if (!serialization->ReadRecordData(house)) return false;
            houses_.push_back(house);

            RE::FormID marker = 0;
            if (!serialization->ReadRecordData(marker)) return false;
            if (marker != 0) houseMarkers_[house] = marker;

            std::uint32_t tenantCount = 0;
            if (!serialization->ReadRecordData(tenantCount)) return false;
            if (tenantCount > 0) {
                std::vector<RE::FormID> tenants;
                tenants.reserve(tenantCount);
                for (std::uint32_t j = 0; j < tenantCount; ++j) {
                    RE::FormID tid = 0;
                    if (!serialization->ReadRecordData(tid)) return false;
                    tenants.push_back(tid);
                }
                houseTenants_[house] = std::move(tenants);
            }
        }

        SKSE::log::info("Loaded {} player houses", houseCount);
        return true;
    }

    void PlayerHouseService::Revert() {
        houses_.clear();
        houseMarkers_.clear();
        houseTenants_.clear();
        SKSE::log::info("Reverted player house data");
    }

}  // namespace MARAS
