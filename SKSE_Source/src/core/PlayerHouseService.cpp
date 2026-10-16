#include "core/PlayerHouseService.h"

#include "core/FormCache.h"
#include "core/NPCIndex.h"
#include "core/NPCRelationshipManager.h"
#include "core/PackageOverrideService.h"
#include "core/PlayerChildrenService.h"
#include "services/ActorService.h"
#include "utils/ActorUtils.h"
#include "utils/FormUtils.h"

namespace MARAS {

    PlayerHouseService& PlayerHouseService::GetSingleton() {
        static PlayerHouseService instance;
        return instance;
    }

    namespace {
        constexpr std::int8_t kDefaultFactionRank = 0;
    }

    PlayerHouseService::PlayerHouseService() { Revert(); }

    PlayerHouseService::House* PlayerHouseService::FindHouse(RE::FormID houseFormID) {
        if (houseFormID == 0) return nullptr;
        auto it = std::find_if(houses_.begin(), houses_.end(), [houseFormID](const House& h) {
            return h.id == houseFormID;
        });
        return (it != houses_.end()) ? &(*it) : nullptr;
    }

    const PlayerHouseService::House* PlayerHouseService::FindHouse(RE::FormID houseFormID) const {
        if (houseFormID == 0) return nullptr;
        auto it = std::find_if(houses_.begin(), houses_.end(), [houseFormID](const House& h) {
            return h.id == houseFormID;
        });
        return (it != houses_.end()) ? &(*it) : nullptr;
    }

    void PlayerHouseService::SetHousedFaction(RE::FormID tenantFormID, bool add) {
        auto* faction = FormCache::GetSingleton().GetSpouseHousedFaction();
        if (!faction) return;

        if (auto* actor = NPCIndex::GetSingleton().GetActorFromFormId(tenantFormID)) {
            if (add) {
                MARAS::Utils::AddToFaction(actor, faction, kDefaultFactionRank);
                MARAS_LOG_DEBUG("Added tenant {:08X} to housed faction {:08X}", tenantFormID, faction->GetFormID());
            } else {
                MARAS::Utils::RemoveFromFaction(actor, faction);
                MARAS_LOG_DEBUG("Removed tenant {:08X} from housed faction {:08X}", tenantFormID, faction->GetFormID());
            }
        } else {
            MARAS_LOG_WARN("SetHousedFaction: could not find Actor for tenant {:08X}", tenantFormID);
        }
    }

    void PlayerHouseService::AddMerchantFactionsToTenant(RE::FormID tenantFormID) {
        auto* actor = NPCIndex::GetSingleton().GetActorFromFormId(tenantFormID);
        if (!actor) {
            MARAS_LOG_WARN("AddMerchantFactionsToTenant: could not find Actor for tenant {:08X}", tenantFormID);
            return;
        }

        auto& formCache = FormCache::GetSingleton();

        struct MerchantMapping {
            RE::TESFaction* skyrimFaction;
            RE::TESFaction* housedFaction;
            const char* name;
        };

        const std::array<MerchantMapping, 8> merchantMappings{{
            {formCache.GetApothecaryFaction(), formCache.GetApothecaryMerchantHousedFaction(), "apothecary"},
            {formCache.GetBlacksmithFaction(), formCache.GetBlacksmithMerchantHousedFaction(), "blacksmith"},
            {formCache.GetFletcherFaction(), formCache.GetFletcherMerchantHousedFaction(), "fletcher"},
            {formCache.GetHunterFaction(), formCache.GetHunterMerchantHousedFaction(), "hunter"},
            {formCache.GetInnKeeperFaction(), formCache.GetInnkeeperMerchantHousedFaction(), "innkeeper"},
            {formCache.GetJewelerFaction(), formCache.GetJewelerMerchantHousedFaction(), "jeweler"},
            {formCache.GetSpellFaction(), formCache.GetSpellsMerchantHousedFaction(), "spells"},
            {formCache.GetTailorFaction(), formCache.GetTailorMerchantHousedFaction(), "tailor"}
        }};

        auto addIfMissing = [actor](RE::TESFaction* f, const char* name) {
            if (f && !actor->IsInFaction(f)) {
                MARAS::Utils::AddToFaction(actor, f, kDefaultFactionRank);
                MARAS_LOG_DEBUG("Added {} faction to tenant {:08X}", name, actor->GetFormID());
                return true;
            }
            return false;
        };

        bool foundSpecific = false;
        for (const auto& m : merchantMappings) {
            if (m.skyrimFaction && actor->IsInFaction(m.skyrimFaction)) {
                if (addIfMissing(m.housedFaction, m.name)) {
                    foundSpecific = true;
                }
            }
        }

        if (!foundSpecific) {
            addIfMissing(formCache.GetMerchantFaction(), "default merchant");
            addIfMissing(formCache.GetMiscFaction(), "misc merchant");
            addIfMissing(formCache.GetMiscMerchantHousedFaction(), "misc housed merchant");
        }
    }

    void PlayerHouseService::RemoveMerchantFactionsFromTenant(RE::FormID tenantFormID) {
        auto* actor = NPCIndex::GetSingleton().GetActorFromFormId(tenantFormID);
        if (!actor) return;

        auto& formCache = FormCache::GetSingleton();

        const std::array<RE::TESFaction*, 11> factionsToRemove{{
            formCache.GetApothecaryMerchantHousedFaction(),
            formCache.GetBlacksmithMerchantHousedFaction(),
            formCache.GetFletcherMerchantHousedFaction(),
            formCache.GetHunterMerchantHousedFaction(),
            formCache.GetInnkeeperMerchantHousedFaction(),
            formCache.GetJewelerMerchantHousedFaction(),
            formCache.GetSpellsMerchantHousedFaction(),
            formCache.GetTailorMerchantHousedFaction(),
            formCache.GetMerchantFaction(),
            formCache.GetMiscFaction(),
            formCache.GetMiscMerchantHousedFaction()
        }};

        for (auto* f : factionsToRemove) {
            if (f && actor->IsInFaction(f)) {
                MARAS::Utils::RemoveFromFaction(actor, f);
            }
        }
        MARAS_LOG_DEBUG("Removed merchant factions from tenant {:08X}", tenantFormID);
    }

    void PlayerHouseService::ApplyStayAtHome(RE::FormID tenantFormID, bool stayAtHome) {
        if (tenantFormID == 0 || IsSyntheticChildID(tenantFormID)) return;
        if (stayAtHome) {
            PackageOverrideService::GetSingleton().RegisterActor(tenantFormID);
        } else {
            PackageOverrideService::GetSingleton().UnregisterActor(tenantFormID);
        }
    }

    bool PlayerHouseService::RegisterPlayerHouseCell(RE::FormID locationFormID, RE::FormID markerFormID) {
        if (locationFormID == 0) return false;

        House* house = FindHouse(locationFormID);
        if (!house) {
            houses_.push_back(House{locationFormID, markerFormID, false, {}});
            house = &houses_.back();
        } else if (markerFormID != 0) {
            house->marker = markerFormID;
        }

        if (markerFormID != 0) {
            MARAS_LOG_DEBUG("RegisterPlayerHouseCell: stored marker {:08X} for house {:08X}", markerFormID, locationFormID);

            // Copy tenant list to avoid iterator invalidation if child assignment moves co-parents (Bug B1 fix)
            auto tenantsCopy = house->tenants;
            auto& relMgr = NPCRelationshipManager::GetSingleton();
            for (auto tenantID : tenantsCopy) {
                if (relMgr.IsChildNPC(tenantID)) {
                    PlayerChildrenService::GetSingleton().AssignChildToHouse(tenantID, locationFormID, false);
                } else {
                    UpdateSpouseHomeMarker(tenantID, locationFormID);
                }
            }
        }

        // Update the player houses count global
        if (auto* global = FormCache::GetSingleton().GetPlayerHousesCount()) {
            global->value = static_cast<float>(houses_.size());
            MARAS_LOG_DEBUG("Player houses count global set to {}", global->value);
        }

        return true;
    }

    std::vector<RE::FormID> PlayerHouseService::GetAllPlayerHouses() const {
        std::vector<RE::FormID> result;
        result.reserve(houses_.size());
        for (const auto& h : houses_) {
            result.push_back(h.id);
        }
        return result;
    }

    std::string PlayerHouseService::GetHouseName(RE::FormID houseFormID) {
        if (houseFormID == 0) return {};
        if (auto* form = RE::TESForm::LookupByID(houseFormID)) {
            const char* n = form->GetName();
            if (n && n[0] != '\0') {
                return n;
            }
        }
        return {};
    }

    RE::FormID PlayerHouseService::FindHouseByName(std::string_view name) const {
        if (name.empty()) return 0;
        for (const auto& h : houses_) {
            if (GetHouseName(h.id) == name) {
                return h.id;
            }
        }
        return 0;
    }

    std::vector<std::string> PlayerHouseService::GetAllPlayerHousesNames() const {
        std::vector<std::string> names;
        names.reserve(houses_.size());
        for (const auto& h : houses_) {
            names.push_back(GetHouseName(h.id));
        }
        return names;
    }

    RE::FormID PlayerHouseService::GetHouseMarkerFormID(RE::FormID playerHouseFormID) const noexcept {
        const auto* h = FindHouse(playerHouseFormID);
        return h ? h->marker : 0;
    }

    RE::TESObjectREFR* PlayerHouseService::GetHouseMarkerRef(RE::FormID playerHouseFormID) const {
        RE::FormID markerID = GetHouseMarkerFormID(playerHouseFormID);
        return markerID != 0 ? RE::TESForm::LookupByID<RE::TESObjectREFR>(markerID) : nullptr;
    }

    bool PlayerHouseService::UpdateSpouseHomeMarker(RE::FormID spouseFormID, RE::FormID playerHouseFormID) {
        if (spouseFormID == 0 || playerHouseFormID == 0) {
            return false;
        }

        auto* spouseActor = RE::TESForm::LookupByID<RE::Actor>(spouseFormID);
        if (!spouseActor) {
            MARAS_LOG_WARN("UpdateSpouseHomeMarker: actor {:08X} not found", spouseFormID);
            return false;
        }

        auto* houseMarker = GetHouseMarkerRef(playerHouseFormID);
        if (!houseMarker) {
            MARAS_LOG_WARN("UpdateSpouseHomeMarker: no house marker found for house {:08X}; spouse {:08X} marker deferred",
                           playerHouseFormID, spouseFormID);
            return false;
        }

        auto& relMgr = NPCRelationshipManager::GetSingleton();
        auto* spouseMarker = relMgr.GetOrCreateHomeMarker(spouseActor);
        if (!spouseMarker) {
            MARAS_LOG_WARN("UpdateSpouseHomeMarker: failed to get or create home marker for spouse {:08X}", spouseFormID);
            return false;
        }

        spouseMarker->MoveTo(houseMarker);
        MARAS_LOG_INFO("UpdateSpouseHomeMarker: Moved spouse {:08X} home marker {:08X} to house marker {:08X} (house: {:08X})",
                       spouseFormID, spouseMarker->GetFormID(), houseMarker->GetFormID(), playerHouseFormID);

        return true;
    }

    bool PlayerHouseService::RegisterTenantInPlayerHouse(RE::FormID tenantFormID, RE::FormID playerHouseFormID, bool stayAtHome) {
        if (tenantFormID == 0 || tenantFormID == kPlayerFormID || playerHouseFormID == 0) {
            return false;
        }

        House* targetHouse = FindHouse(playerHouseFormID);
        if (!targetHouse) {
            MARAS_LOG_WARN("RegisterTenantInPlayerHouse: target house {:08X} not found", playerHouseFormID);
            return false;
        }

        const bool isSynthetic = IsSyntheticChildID(tenantFormID);
        const bool isChild = isSynthetic || NPCRelationshipManager::GetSingleton().IsChildNPC(tenantFormID);
        if (isChild) {
            stayAtHome = true;
        }

        auto it = tenants_.find(tenantFormID);
        const bool alreadyRegistered = (it != tenants_.end());
        const RE::FormID oldHouseID = alreadyRegistered ? it->second.house : 0;

        if (alreadyRegistered && oldHouseID == playerHouseFormID) {
            if (it->second.stayAtHome != stayAtHome) {
                SetTenantStayAtHome(tenantFormID, stayAtHome);
            }
            return true;
        }

        // Moving from another house: detach from old house vector
        if (alreadyRegistered && oldHouseID != 0) {
            if (auto* oldHouse = FindHouse(oldHouseID)) {
                auto& vec = oldHouse->tenants;
                vec.erase(std::remove(vec.begin(), vec.end(), tenantFormID), vec.end());
            }
        }

        // Add to target house vector
        if (std::find(targetHouse->tenants.begin(), targetHouse->tenants.end(), tenantFormID) == targetHouse->tenants.end()) {
            targetHouse->tenants.push_back(tenantFormID);
        }

        // Record or update tenant entry
        if (alreadyRegistered) {
            it->second = {playerHouseFormID, stayAtHome};
        } else {
            tenants_.emplace(tenantFormID, Tenant{playerHouseFormID, stayAtHome});
        }

        // Faction and marker management for spouses
        if (!isChild) {
            if (!alreadyRegistered) {
                SetHousedFaction(tenantFormID, true);
                AddMerchantFactionsToTenant(tenantFormID);
            }
            UpdateSpouseHomeMarker(tenantFormID, playerHouseFormID);
        }

        ApplyStayAtHome(tenantFormID, stayAtHome);

        MARAS_LOG_DEBUG("Registered tenant {:08X} in player house {:08X} (stayAtHome: {})", tenantFormID, playerHouseFormID, stayAtHome);
        return true;
    }

    void PlayerHouseService::SetTenantStayAtHome(RE::FormID tenantFormID, bool stayAtHome) {
        if (tenantFormID == 0) return;
        auto it = tenants_.find(tenantFormID);
        if (it == tenants_.end()) {
            MARAS_LOG_WARN("SetTenantStayAtHome: actor {:08X} is not a registered tenant", tenantFormID);
            return;
        }

        if (it->second.stayAtHome == stayAtHome) return;

        it->second.stayAtHome = stayAtHome;
        ApplyStayAtHome(tenantFormID, stayAtHome);

        if (stayAtHome && !IsSyntheticChildID(tenantFormID) && !NPCRelationshipManager::GetSingleton().IsChildNPC(tenantFormID)) {
            UpdateSpouseHomeMarker(tenantFormID, it->second.house);
        }
        MARAS_LOG_INFO("PlayerHouseService: Set tenant {:08X} stayAtHome = {}", tenantFormID, stayAtHome);
    }

    bool PlayerHouseService::GetTenantStayAtHome(RE::FormID tenantFormID) const {
        auto it = tenants_.find(tenantFormID);
        return (it != tenants_.end()) ? it->second.stayAtHome : true;
    }

    bool PlayerHouseService::RemoveTenantFromPlayerHouse(RE::FormID tenantFormID) {
        if (tenantFormID == 0) return false;

        auto it = tenants_.find(tenantFormID);
        if (it == tenants_.end()) return false;

        RE::FormID houseFormID = it->second.house;
        bool isChild = IsSyntheticChildID(tenantFormID) || NPCRelationshipManager::GetSingleton().IsChildNPC(tenantFormID);

        // Remove from house tenants vector
        if (auto* house = FindHouse(houseFormID)) {
            auto& vec = house->tenants;
            vec.erase(std::remove(vec.begin(), vec.end(), tenantFormID), vec.end());
        }

        // Erase tenant mapping
        tenants_.erase(it);

        // Unregister package overrides
        ApplyStayAtHome(tenantFormID, false);

        // Remove factions if not a child
        if (!isChild) {
            SetHousedFaction(tenantFormID, false);
            RemoveMerchantFactionsFromTenant(tenantFormID);
        }

        MARAS_LOG_DEBUG("Removed tenant {:08X} from player house {:08X}", tenantFormID, houseFormID);
        return true;
    }

    bool PlayerHouseService::CanRemoveTenant(RE::FormID tenantFormID) const {
        if (tenantFormID == 0) return false;
        auto& relMgr = NPCRelationshipManager::GetSingleton();
        if (relMgr.GetChildren(kPlayerFormID, tenantFormID).size() > 0) {
            return false;
        }
        if (IsSyntheticChildID(tenantFormID) || relMgr.IsChildNPC(tenantFormID)) {
            auto childOpt = relMgr.GetChildEntry(tenantFormID);
            if (childOpt.has_value()) {
                RE::FormID spID = relMgr.GetCoGuardian(tenantFormID, kPlayerFormID);
                if (spID != 0 && relMgr.GetRelationshipStatus(kPlayerFormID, spID) == RelationshipStatus::Married) {
                    return false;
                }
            }
        }
        return true;
    }

    std::vector<RE::FormID> PlayerHouseService::RelocateFamilyUnit(RE::FormID npcFormID, RE::FormID playerHouseFormID, bool moveParents) {
        // Player houses can only house player-related families, and Player itself is never moved as a tenant.
        if (npcFormID == 0 || npcFormID == kPlayerFormID || playerHouseFormID == 0) {
            return {};
        }

        House* targetHouse = FindHouse(playerHouseFormID);
        if (!targetHouse) {
            MARAS_LOG_WARN("PlayerHouseService::RelocateFamilyUnit: Target house {:08X} not found", playerHouseFormID);
            return {};
        }

        auto& relMgr = NPCRelationshipManager::GetSingleton();
        auto& idx = NPCIndex::GetSingleton();

        auto matches = [&idx](RE::FormID stored, RE::FormID target) -> bool {
            if (stored == 0 || target == 0) return false;
            if (stored == target) return true;
            RE::FormID tRef = idx.GetRefID(target);
            if (tRef != 0 && stored == tRef) return true;
            RE::FormID tBase = idx.GetBaseID(target);
            if (tBase != 0 && stored == tBase) return true;
            return false;
        };

        auto isHouseholdChildEntry = [](RE::FormID cID, const ChildRegistryEntry& cEntry) -> bool {
            if (cID == 0) return false;
            return cEntry.stage != ChildStage::Adult;
        };

        auto childOpt = relMgr.GetChildEntry(npcFormID);
        bool isChild = childOpt.has_value() || IsSyntheticChildID(npcFormID) || relMgr.IsChildNPC(npcFormID);

        RE::FormID targetSpouseID = 0;
        std::unordered_set<RE::FormID> familyChildren;

        if (isChild) {
            // ==============================================================
            // CASE A: npcFormID is a Child
            // Verify child is in the Player's custodial guardianship (never conflate with bio parents)
            // ==============================================================
            if (childOpt.has_value() && !isHouseholdChildEntry(npcFormID, *childOpt)) {
                return {};
            }

            RE::FormID gA = childOpt.has_value() ? childOpt->guardianAID : 0;
            RE::FormID gB = childOpt.has_value() ? childOpt->guardianBID : 0;

            bool playerIsGuardian = matches(gA, kPlayerFormID) || matches(gB, kPlayerFormID) ||
                                    relMgr.IsGuardian(npcFormID, kPlayerFormID);

            if (!playerIsGuardian) {
                MARAS_LOG_WARN("PlayerHouseService::RelocateFamilyUnit: Child {:08X} is not in Player's custodial household.", npcFormID);
                return {};
            }

            // Identify which specific Spouse shares custodial guardianship of this child with the Player
            if (matches(gA, kPlayerFormID)) {
                targetSpouseID = gB;
            } else if (matches(gB, kPlayerFormID)) {
                targetSpouseID = gA;
            } else {
                targetSpouseID = relMgr.GetCoGuardian(npcFormID, kPlayerFormID);
            }

            if (matches(targetSpouseID, kPlayerFormID)) {
                targetSpouseID = 0;
            }

            familyChildren.insert(npcFormID);

            if (targetSpouseID != 0) {
                // Collect children whose custodial guardians are (Player, targetSpouseID)
                for (const auto& [cID, cEntry] : relMgr.GetAllChildren()) {
                    if (!isHouseholdChildEntry(cID, cEntry)) continue;
                    bool hasPlayer = matches(cEntry.guardianAID, kPlayerFormID) ||
                                     matches(cEntry.guardianBID, kPlayerFormID);
                    bool hasThisSpouse = matches(cEntry.guardianAID, targetSpouseID) ||
                                         matches(cEntry.guardianBID, targetSpouseID);
                    if (hasPlayer && hasThisSpouse) {
                        familyChildren.insert(cID);
                    }
                }
            } else {
                // Single-parent child of the Player (no spouse co-guardian)
                for (const auto& [cID, cEntry] : relMgr.GetAllChildren()) {
                    if (!isHouseholdChildEntry(cID, cEntry)) continue;
                    bool hasPlayer = matches(cEntry.guardianAID, kPlayerFormID) ||
                                     matches(cEntry.guardianBID, kPlayerFormID);
                    if (hasPlayer && relMgr.GetCoGuardian(cID, kPlayerFormID) == 0) {
                        familyChildren.insert(cID);
                    }
                }
            }
        } else {
            // ==============================================================
            // CASE B: npcFormID is an Adult NPC
            // Verify adult is a Player spouse / partner or shares custodial guardianship with Player
            // ==============================================================
            auto relStatus = relMgr.GetRelationshipStatus(kPlayerFormID, npcFormID);
            bool isPlayerPartner = (relStatus == RelationshipStatus::Married ||
                                    relStatus == RelationshipStatus::Engaged ||
                                    relStatus == RelationshipStatus::Dating ||
                                    relStatus == RelationshipStatus::Lover);
            bool sharesChildWithPlayer = !relMgr.GetChildren(kPlayerFormID, npcFormID).empty();

            if (!isPlayerPartner && !sharesChildWithPlayer) {
                MARAS_LOG_WARN("PlayerHouseService::RelocateFamilyUnit: Adult {:08X} is not a Player spouse/partner. Player houses only accept player-related families.", npcFormID);
                return {};
            }

            targetSpouseID = npcFormID;

            // Collect children whose custodial guardians are (Player, targetSpouseID)!
            for (const auto& [cID, cEntry] : relMgr.GetAllChildren()) {
                if (!isHouseholdChildEntry(cID, cEntry)) continue;
                bool hasPlayer = matches(cEntry.guardianAID, kPlayerFormID) ||
                                 matches(cEntry.guardianBID, kPlayerFormID);
                bool hasThisSpouse = matches(cEntry.guardianAID, targetSpouseID) ||
                                     matches(cEntry.guardianBID, targetSpouseID);
                if (hasPlayer && hasThisSpouse) {
                    familyChildren.insert(cID);
                }
            }
        }

        std::vector<RE::FormID> relocatedMembers;
        std::unordered_set<RE::FormID> relocatedSet;

        // 1. Relocate the specific Spouse (if present and requested, or if the Spouse was the primary target)
        if (targetSpouseID != 0 && !matches(targetSpouseID, kPlayerFormID) && (moveParents || !isChild)) {
            if (relocatedSet.insert(targetSpouseID).second) {
                bool stayAtHome = GetTenantStayAtHome(targetSpouseID);
                (void)RegisterTenantInPlayerHouse(targetSpouseID, playerHouseFormID, stayAtHome);
                relocatedMembers.push_back(targetSpouseID);
                MARAS_LOG_INFO("PlayerHouseService::RelocateFamilyUnit: Relocated player spouse {:08X} to house {:08X} (stayAtHome: {})",
                               targetSpouseID, playerHouseFormID, stayAtHome);
            }
        }

        // 2. Relocate ONLY the children assigned to this specific Spouse
        auto& childSvc = PlayerChildrenService::GetSingleton();
        for (auto cID : familyChildren) {
            if (relocatedSet.insert(cID).second) {
                childSvc.AssignChildToHouse(cID, playerHouseFormID, false);
                relocatedMembers.push_back(cID);
                MARAS_LOG_INFO("PlayerHouseService::RelocateFamilyUnit: Relocated child {:08X} (belonging to spouse {:08X}) to house {:08X}",
                               cID, targetSpouseID, playerHouseFormID);
            }
        }

        MARAS_LOG_INFO("PlayerHouseService::RelocateFamilyUnit: Completed relocation of {} total family members for spouse {:08X} to house {:08X}",
                       relocatedMembers.size(), targetSpouseID, playerHouseFormID);
        return relocatedMembers;
    }

    std::vector<RE::FormID> PlayerHouseService::GetPlayerHouseTenants(RE::FormID playerHouseFormID) const {
        const auto* h = FindHouse(playerHouseFormID);
        return h ? h->tenants : std::vector<RE::FormID>{};
    }

    RE::FormID PlayerHouseService::GetTenantHouseFormID(RE::FormID tenantFormID) const noexcept {
        if (tenantFormID == 0) return 0;
        auto it = tenants_.find(tenantFormID);
        return (it != tenants_.end()) ? it->second.house : 0;
    }

    int PlayerHouseService::CountPlayerHouses() const noexcept {
        return static_cast<int>(houses_.size());
    }

    void PlayerHouseService::SetHouseInitialMoveDone(RE::FormID houseFormID, bool done) {
        if (auto* h = FindHouse(houseFormID)) {
            h->initialMoveDone = done;
            MARAS_LOG_INFO("SetHouseInitialMoveDone: house {:08X} initialMoveDone = {}", houseFormID, done);
        }
    }

    bool PlayerHouseService::IsHouseInitialMoveDone(RE::FormID houseFormID) const {
        const auto* h = FindHouse(houseFormID);
        return h ? h->initialMoveDone : false;
    }

    bool PlayerHouseService::Save(SKSE::SerializationInterface* serialization) const {
        if (!serialization) return false;

        std::uint32_t houseCount = static_cast<std::uint32_t>(houses_.size());
        if (!serialization->WriteRecordData(houseCount)) return false;

        for (const auto& house : houses_) {
            if (!serialization->WriteRecordData(house.id)) return false;
            if (!serialization->WriteRecordData(house.marker)) return false;
            if (!serialization->WriteRecordData(house.initialMoveDone)) return false;

            std::uint32_t tenantCount = static_cast<std::uint32_t>(house.tenants.size());
            if (!serialization->WriteRecordData(tenantCount)) return false;

            for (auto tenantID : house.tenants) {
                if (!serialization->WriteRecordData(tenantID)) return false;
                bool stayAtHome = GetTenantStayAtHome(tenantID);
                if (!serialization->WriteRecordData(stayAtHome)) return false;
            }
        }

        MARAS_LOG_INFO("Saved {} player houses", houseCount);
        return true;
    }

    bool PlayerHouseService::Load(SKSE::SerializationInterface* serialization, std::uint32_t version) {
        if (!serialization) return false;

        Revert();

        constexpr std::uint32_t kMaxHouses = 1000;
        constexpr std::uint32_t kMaxTenants = 10000;

        std::uint32_t houseCount = 0;
        if (!serialization->ReadRecordData(houseCount)) return false;
        if (houseCount > kMaxHouses) {
            MARAS_LOG_ERROR("PlayerHouseService::Load - corrupt houseCount {}", houseCount);
            return false;
        }

        auto& relMgr = NPCRelationshipManager::GetSingleton();
        auto& idx = NPCIndex::GetSingleton();

        houses_.reserve(houseCount);
        for (std::uint32_t i = 0; i < houseCount; ++i) {
            RE::FormID savedHouse = 0;
            if (!serialization->ReadRecordData(savedHouse)) return false;

            RE::FormID savedMarker = 0;
            if (!serialization->ReadRecordData(savedMarker)) return false;

            bool moveDone = false;
            if (version >= 4) {
                if (!serialization->ReadRecordData(moveDone)) return false;
            }

            std::uint32_t tenantCount = 0;
            if (!serialization->ReadRecordData(tenantCount)) return false;
            if (tenantCount > kMaxTenants) {
                MARAS_LOG_ERROR("PlayerHouseService::Load - corrupt tenantCount {}", tenantCount);
                return false;
            }

            struct LoadedTenant {
                RE::FormID savedID{0};
                bool stayAtHome{true};
            };
            std::vector<LoadedTenant> loadedTenants;
            loadedTenants.reserve(tenantCount);

            for (std::uint32_t j = 0; j < tenantCount; ++j) {
                RE::FormID savedTid = 0;
                if (!serialization->ReadRecordData(savedTid)) return false;
                bool stayAtHome = true;
                if (version >= 4) {
                    if (!serialization->ReadRecordData(stayAtHome)) return false;
                }
                loadedTenants.push_back({savedTid, stayAtHome});
            }

            if (savedHouse == 0) continue;
            RE::FormID houseID = 0;
            if (!serialization->ResolveFormID(savedHouse, houseID) || houseID == 0) {
                continue;
            }

            RE::FormID markerID = 0;
            if (savedMarker != 0) {
                serialization->ResolveFormID(savedMarker, markerID);
            }

            House house{houseID, markerID, moveDone, {}};
            house.tenants.reserve(tenantCount);

            for (const auto& lt : loadedTenants) {
                if (lt.savedID == 0) continue;
                if (IsSyntheticChildID(lt.savedID)) {
                    if (relMgr.GetChildEntry(lt.savedID).has_value()) {
                        house.tenants.push_back(lt.savedID);
                        tenants_[lt.savedID] = Tenant{houseID, lt.stayAtHome};
                    }
                    continue;
                }
                RE::FormID resolvedTid = 0;
                if (serialization->ResolveFormID(lt.savedID, resolvedTid) && resolvedTid != 0) {
                    auto* actor = idx.GetActorFromFormId(resolvedTid);
                    if (!actor || actor->IsDead()) {
                        MARAS_LOG_INFO("PlayerHouseService::Load - skipping dead/invalid tenant {:08X}", resolvedTid);
                        continue;
                    }
                    house.tenants.push_back(resolvedTid);
                    tenants_[resolvedTid] = Tenant{houseID, lt.stayAtHome};

                    if (!relMgr.IsChildNPC(resolvedTid)) {
                        SetHousedFaction(resolvedTid, true);
                        AddMerchantFactionsToTenant(resolvedTid);
                    }
                }
            }

            houses_.push_back(std::move(house));
        }

        if (auto* global = FormCache::GetSingleton().GetPlayerHousesCount()) {
            global->value = static_cast<float>(houses_.size());
        }

        MARAS_LOG_INFO("Loaded {} player houses (from {} saved entries)", houses_.size(), houseCount);

        PackageOverrideService::GetSingleton().RebuildFromTenants();
        return true;
    }

    void PlayerHouseService::Revert() {
        houses_.clear();
        tenants_.clear();
        if (auto* global = FormCache::GetSingleton().GetPlayerHousesCount()) {
            global->value = 0.0f;
        }
        MARAS_LOG_INFO("Reverted player house data");
    }

    nlohmann::json PlayerHouseService::DumpJson() const {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& house : houses_) {
            nlohmann::json h;
            h["houseFormId"] = std::format("{:08X}", house.id);
            h["name"] = GetHouseName(house.id);
            h["markerFormId"] = house.marker != 0 ? std::format("{:08X}", house.marker) : "";
            h["initialMoveDone"] = house.initialMoveDone;

            nlohmann::json tenantsArray = nlohmann::json::array();
            for (auto tID : house.tenants) {
                nlohmann::json t;
                t["formId"] = std::format("{:08X}", tID);
                t["name"] = ActorSvc::GetName(tID);
                t["stayAtHome"] = GetTenantStayAtHome(tID);
                tenantsArray.push_back(t);
            }
            h["tenants"] = tenantsArray;
            j.push_back(h);
        }
        return j;
    }

}  // namespace MARAS
