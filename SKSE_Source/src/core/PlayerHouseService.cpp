#include "core/PlayerHouseService.h"

#include "core/FactionReconciler.h"
#include "core/FormCache.h"
#include "core/GlobalVarReconciler.h"
#include "core/NPCIndex.h"
#include "core/NPCRelationshipManager.h"
#include "core/PackageOverrideService.h"
#include "core/PackageReconciler.h"
#include "core/PlayerChildrenService.h"
#include "maras/state/WorldStore.h"
#include "services/ActorService.h"
#include "utils/ActorUtils.h"
#include "utils/FormUtils.h"

namespace MARAS {

    PlayerHouseService& PlayerHouseService::GetSingleton() {
        static PlayerHouseService instance;
        return instance;
    }

    PlayerHouseService::PlayerHouseService() = default;

    void PlayerHouseService::SetHousedFaction(RE::FormID tenantFormID, bool /*add*/) {
        FactionReconciler::GetSingleton().ReconcileActor(tenantFormID);
    }

    void PlayerHouseService::AddMerchantFactionsToTenant(RE::FormID tenantFormID) {
        FactionReconciler::GetSingleton().ReconcileActor(tenantFormID);
    }

    void PlayerHouseService::RemoveMerchantFactionsFromTenant(RE::FormID tenantFormID) {
        FactionReconciler::GetSingleton().ReconcileActor(tenantFormID);
    }

    void PlayerHouseService::ApplyStayAtHome(RE::FormID tenantFormID, bool /*stayAtHome*/) {
        if (tenantFormID == 0 || IsSyntheticChildID(tenantFormID)) return;
        PackageReconciler::GetSingleton().ReconcileActor(tenantFormID);
    }

    bool PlayerHouseService::RegisterPlayerHouseCell(RE::FormID locationFormID, RE::FormID markerFormID) {
        if (locationFormID == 0) return false;

        std::vector<std::uint32_t> tenantsCopy;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& hh = ws.household.Write();
            const State::HouseId houseId{ locationFormID };
            if (auto* existing = hh.FindHouseMutable(houseId)) {
                if (markerFormID != 0) {
                    existing->markerId = State::RefId{ markerFormID };
                }
                tenantsCopy = existing->tenantOrder;
            } else {
                hh.RegisterHouse(houseId, State::RefId{ markerFormID }, false);
            }
        });

        if (markerFormID != 0) {
            MARAS_LOG_DEBUG("RegisterPlayerHouseCell: stored marker {:08X} for house {:08X}", markerFormID, locationFormID);

            auto& relMgr = NPCRelationshipManager::GetSingleton();
            for (const RE::FormID tenantID : tenantsCopy) {
                if (IsSyntheticChildID(tenantID) || relMgr.IsChildNPC(tenantID)) {
                    PlayerChildrenService::GetSingleton().AssignChildToHouse(tenantID, locationFormID, false);
                } else {
                    UpdateSpouseHomeMarker(tenantID, locationFormID);
                }
            }
        }

        // Reconcile the player houses count global
        GlobalVarReconciler::GetSingleton().ReconcileGlobals();

        return true;
    }

    std::vector<RE::FormID> PlayerHouseService::GetAllPlayerHouses() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& houses = snap->household->AllHouses();
        std::vector<RE::FormID> result;
        result.reserve(houses.size());
        for (const auto& h : houses) {
            result.push_back(h.houseId.raw());
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
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& h : snap->household->AllHouses()) {
            const RE::FormID hId = h.houseId.raw();
            if (GetHouseName(hId) == name) {
                return hId;
            }
        }
        return 0;
    }

    std::vector<std::string> PlayerHouseService::GetAllPlayerHousesNames() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& houses = snap->household->AllHouses();
        std::vector<std::string> names;
        names.reserve(houses.size());
        for (const auto& h : houses) {
            names.push_back(GetHouseName(h.houseId.raw()));
        }
        return names;
    }

    RE::FormID PlayerHouseService::GetHouseMarkerFormID(RE::FormID playerHouseFormID) const noexcept {
        if (playerHouseFormID == 0) return 0;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* h = snap->household->FindHouse(State::HouseId{ playerHouseFormID });
        return h ? h->markerId.raw() : 0;
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

        const bool isSynthetic = IsSyntheticChildID(tenantFormID);
        const bool isChild = isSynthetic || NPCRelationshipManager::GetSingleton().IsChildNPC(tenantFormID);
        if (isChild) {
            stayAtHome = true;
        }

        bool houseFound = false;
        bool alreadyRegistered = false;
        bool sameHouseOnly = false;
        bool stayChanged = false;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& hh = ws.household.Write();
            const State::HouseId dstHouseId{ playerHouseFormID };
            if (!hh.FindHouse(dstHouseId)) {
                return;
            }
            houseFound = true;

            const State::HouseId prevHouse = hh.GetTenantHouse(tenantFormID);
            alreadyRegistered = !prevHouse.IsNull();
            if (alreadyRegistered && prevHouse == dstHouseId) {
                sameHouseOnly = true;
                if (hh.GetTenantStayAtHome(tenantFormID) != stayAtHome) {
                    hh.SetTenantStayAtHome(tenantFormID, stayAtHome);
                    stayChanged = true;
                }
                return;
            }

            hh.AssignTenant(tenantFormID, dstHouseId, stayAtHome, isChild);
        });

        if (!houseFound) {
            MARAS_LOG_WARN("RegisterTenantInPlayerHouse: target house {:08X} not found", playerHouseFormID);
            return false;
        }

        if (sameHouseOnly) {
            if (stayChanged) {
                if (stayAtHome && !isChild) {
                    UpdateSpouseHomeMarker(tenantFormID, playerHouseFormID);
                }
                ApplyStayAtHome(tenantFormID, stayAtHome);
                MARAS_LOG_INFO("PlayerHouseService: Set tenant {:08X} stayAtHome = {}", tenantFormID, stayAtHome);
            }
            return true;
        }

        // Faction and marker management for spouses
        if (!isChild) {
            UpdateSpouseHomeMarker(tenantFormID, playerHouseFormID);
            FactionReconciler::GetSingleton().ReconcileActor(tenantFormID);
        }

        ApplyStayAtHome(tenantFormID, stayAtHome);

        MARAS_LOG_DEBUG("Registered tenant {:08X} in player house {:08X} (stayAtHome: {})", tenantFormID, playerHouseFormID, stayAtHome);
        return true;
    }

    void PlayerHouseService::SetTenantStayAtHome(RE::FormID tenantFormID, bool stayAtHome) {
        if (tenantFormID == 0) return;

        const bool isChild = IsSyntheticChildID(tenantFormID) || NPCRelationshipManager::GetSingleton().IsChildNPC(tenantFormID);
        bool notRegistered = false;
        bool changed = false;
        RE::FormID houseFormID = 0;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& hh = ws.household.Write();
            const State::HouseId curHouse = hh.GetTenantHouse(tenantFormID);
            if (curHouse.IsNull()) {
                notRegistered = true;
                return;
            }
            houseFormID = curHouse.raw();
            if (hh.GetTenantStayAtHome(tenantFormID) == stayAtHome) {
                return;
            }
            hh.SetTenantStayAtHome(tenantFormID, stayAtHome);
            changed = true;
        });

        if (notRegistered) {
            MARAS_LOG_WARN("SetTenantStayAtHome: actor {:08X} is not a registered tenant", tenantFormID);
            return;
        }
        if (!changed) {
            return;
        }

        if (stayAtHome && !isChild) {
            UpdateSpouseHomeMarker(tenantFormID, houseFormID);
        }
        ApplyStayAtHome(tenantFormID, stayAtHome);
        MARAS_LOG_INFO("PlayerHouseService: Set tenant {:08X} stayAtHome = {}", tenantFormID, stayAtHome);
    }

    bool PlayerHouseService::GetTenantStayAtHome(RE::FormID tenantFormID) const {
        if (tenantFormID == 0) return true;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->household->GetTenantStayAtHome(tenantFormID);
    }

    bool PlayerHouseService::RemoveTenantFromPlayerHouse(RE::FormID tenantFormID) {
        if (tenantFormID == 0) return false;

        const bool isChild = IsSyntheticChildID(tenantFormID) || NPCRelationshipManager::GetSingleton().IsChildNPC(tenantFormID);
        RE::FormID houseFormID = 0;
        bool foundInTenants = false;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& hh = ws.household.Write();
            const State::HouseId prevHouse = hh.GetTenantHouse(tenantFormID);
            if (prevHouse.IsNull()) {
                return;
            }
            houseFormID = prevHouse.raw();
            foundInTenants = true;
            (void)hh.EvictTenant(tenantFormID);
        });

        if (!foundInTenants) {
            return false;
        }

        // Remove factions / clear home assignment before reconciling
        if (!isChild) {
            NPCRelationshipManager::GetSingleton().ClearCurrentHome(tenantFormID);
            FactionReconciler::GetSingleton().ReconcileActor(tenantFormID);
        } else {
            PlayerChildrenService::GetSingleton().ClearChildHouseAssignment(tenantFormID);
        }

        // Reconcile package overrides after home assignment & marker are cleared
        ApplyStayAtHome(tenantFormID, false);

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

        if (!State::WorldStore::GetSingleton().Snapshot()->household->FindHouse(State::HouseId{ playerHouseFormID })) {
            MARAS_LOG_WARN("PlayerHouseService::RelocateFamilyUnit: Target house {:08X} not found", playerHouseFormID);
            return {};
        }

        auto& relMgr = NPCRelationshipManager::GetSingleton();

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

            bool playerIsGuardian = (childOpt.has_value() && childOpt->HasGuardian(kPlayerFormID)) ||
                                    relMgr.IsGuardian(npcFormID, kPlayerFormID);

            if (!playerIsGuardian) {
                MARAS_LOG_WARN("PlayerHouseService::RelocateFamilyUnit: Child {:08X} is not in Player's custodial household.", npcFormID);
                return {};
            }

            // Identify which specific Spouse shares custodial guardianship of this child with the Player
            targetSpouseID = childOpt.has_value() ? childOpt->GetOtherGuardian(kPlayerFormID)
                                                  : relMgr.GetCoGuardian(npcFormID, kPlayerFormID);

            if (Utils::MatchesActor(targetSpouseID, kPlayerFormID)) {
                targetSpouseID = 0;
            }

            familyChildren.insert(npcFormID);

            // Collect all household children sharing the exact same guardian set (Player + targetSpouseID, or sole Player when targetSpouseID == 0)
            for (const auto& [cID, cEntry] : relMgr.GetAllChildren()) {
                if (!isHouseholdChildEntry(cID, cEntry)) continue;
                if (cEntry.HasSameGuardians(kPlayerFormID, targetSpouseID)) {
                    familyChildren.insert(cID);
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

            // Collect children whose custodial guardians are (Player, targetSpouseID) in either slot order
            for (const auto& [cID, cEntry] : relMgr.GetAllChildren()) {
                if (!isHouseholdChildEntry(cID, cEntry)) continue;
                if (cEntry.HasSameGuardians(kPlayerFormID, targetSpouseID)) {
                    familyChildren.insert(cID);
                }
            }
        }

        std::vector<RE::FormID> relocatedMembers;
        std::unordered_set<RE::FormID> relocatedSet;

        // 1. Relocate the specific Spouse (if present and requested, or if the Spouse was the primary target)
        if (targetSpouseID != 0 && !Utils::MatchesActor(targetSpouseID, kPlayerFormID) && (moveParents || !isChild)) {
            if (relocatedSet.insert(targetSpouseID).second) {
                bool stayAtHome = (GetTenantHouseFormID(targetSpouseID) != 0) ? GetTenantStayAtHome(targetSpouseID) : !isChild;
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
        if (playerHouseFormID == 0) return {};
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* h = snap->household->FindHouse(State::HouseId{ playerHouseFormID });
        if (!h) return {};
        return std::vector<RE::FormID>(h->tenantOrder.begin(), h->tenantOrder.end());
    }

    RE::FormID PlayerHouseService::GetTenantHouseFormID(RE::FormID tenantFormID) const noexcept {
        if (tenantFormID == 0) return 0;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->household->GetTenantHouse(tenantFormID).raw();
    }

    int PlayerHouseService::CountPlayerHouses() const noexcept {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return static_cast<int>(snap->household->AllHouses().size());
    }

    void PlayerHouseService::SetHouseInitialMoveDone(RE::FormID houseFormID, bool done) {
        if (houseFormID == 0) return;
        bool updated = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* h = ws.household.Write().FindHouseMutable(State::HouseId{ houseFormID })) {
                h->initialMoveDone = done;
                updated = true;
            }
        });
        if (updated) {
            MARAS_LOG_INFO("SetHouseInitialMoveDone: house {:08X} initialMoveDone = {}", houseFormID, done);
        }
    }

    bool PlayerHouseService::IsHouseInitialMoveDone(RE::FormID houseFormID) const {
        if (houseFormID == 0) return false;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* h = snap->household->FindHouse(State::HouseId{ houseFormID });
        return h ? h->initialMoveDone : false;
    }

    void PlayerHouseService::FinalizeLoad() {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& hh = *snap->household;

        GlobalVarReconciler::GetSingleton().ReconcileGlobals();

        auto& relMgr = NPCRelationshipManager::GetSingleton();
        auto& idx = NPCIndex::GetSingleton();
        std::size_t syncedCount = 0;

        for (const auto& [tenantID, assignment] : hh.AllTenants()) {
            if (tenantID == 0 || tenantID == kPlayerFormID || IsSyntheticChildID(tenantID)) {
                continue;
            }
            if (relMgr.IsChildNPC(tenantID)) {
                continue;
            }

            auto* actor = RE::TESForm::LookupByID<RE::Actor>(tenantID);
            if (!actor) {
                actor = idx.GetActorFromFormId(tenantID);
            }
            if (actor && !actor->IsDead()) {
                if (!assignment.houseId.IsNull()) {
                    UpdateSpouseHomeMarker(tenantID, assignment.houseId.raw());
                }
                FactionReconciler::GetSingleton().ReconcileLoadedActor(actor);
                ++syncedCount;
            }
            ApplyStayAtHome(tenantID, assignment.stayAtHome);
        }

        MARAS_LOG_INFO("PlayerHouseService::FinalizeLoad - synced {} house(s) and {} loaded adult tenant(s)",
                       hh.AllHouses().size(), syncedCount);
    }

    void PlayerHouseService::Revert() {
        GlobalVarReconciler::GetSingleton().ReconcileGlobals();
        MARAS_LOG_INFO("Reverted player house data");
    }

    nlohmann::json PlayerHouseService::DumpJson() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& hh = *snap->household;

        nlohmann::json j = nlohmann::json::array();
        for (const auto& house : hh.AllHouses()) {
            const RE::FormID houseID = house.houseId.raw();
            const RE::FormID markerID = house.markerId.raw();

            nlohmann::json h;
            h["houseFormId"] = std::format("{:08X}", houseID);
            h["name"] = GetHouseName(houseID);
            h["markerFormId"] = markerID != 0 ? std::format("{:08X}", markerID) : "";
            h["initialMoveDone"] = house.initialMoveDone;

            nlohmann::json tenantsArray = nlohmann::json::array();
            for (const RE::FormID tID : house.tenantOrder) {
                nlohmann::json t;
                t["formId"] = std::format("{:08X}", tID);
                t["name"] = ActorSvc::GetName(tID);
                t["stayAtHome"] = hh.GetTenantStayAtHome(tID);
                tenantsArray.push_back(t);
            }
            h["tenants"] = tenantsArray;
            j.push_back(h);
        }
        return j;
    }

}  // namespace MARAS

