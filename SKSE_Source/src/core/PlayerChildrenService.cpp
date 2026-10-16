#include "core/PlayerChildrenService.h"

#include <RE/E/ExtraFactionChanges.h>

#include "core/FormCache.h"
#include "core/NPCIndex.h"
#include "core/NPCRelationshipManager.h"
#include "core/PackageOverrideService.h"
#include "core/PlayerHouseService.h"
#include "services/ActorService.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/Settings.h"

namespace MARAS {

    namespace {
        bool IsPlayerHouseholdFaction(RE::TESFaction* a_faction) {
            if (!a_faction) return false;
            auto& fc = FormCache::GetSingleton();
            return a_faction == fc.GetPlayerChildFaction() ||
                   a_faction == fc.GetBYOHRelationshipAdoptionChildOwnedFaction() ||
                   a_faction == fc.GetBYOHRelationshipAdoptionFaction() ||
                   a_faction == fc.GetPlayerFaction() ||
                   a_faction == fc.GetBYOHRelationshipAdoptableFaction() ||
                   a_faction == fc.GetPlayerChildBothParentsFaction();
        }
    }  // namespace

    PlayerChildrenService& PlayerChildrenService::GetSingleton() {
        static PlayerChildrenService instance;
        return instance;
    }

    void PlayerChildrenService::SnapshotChildFactions(RE::FormID childID) {
        if (childID == 0 || IsSyntheticChildID(childID)) return;
        if (factionSnapshots_.find(childID) != factionSnapshots_.end()) {
            MARAS_LOG_DEBUG("SnapshotChildFactions: Child {:08X} already has a faction snapshot, skipping", childID);
            return;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(childID);
        if (!actor) {
            MARAS_LOG_WARN("SnapshotChildFactions: Child {:08X} not found as Actor", childID);
            return;
        }

        ChildFactionSnapshot snapshot;

        // Crime Faction
        if (auto* cf = actor->GetCrimeFaction()) {
            snapshot.crimeFactionID = cf->GetFormID();
        } else if (auto* base = actor->GetActorBase(); base && base->crimeFaction) {
            snapshot.crimeFactionID = base->crimeFaction->GetFormID();
        }

        // Collect all distinct factions and effective ranks
        std::unordered_map<RE::TESFaction*, std::int8_t> effectiveFactions;

        // 1. Base NPC factions
        if (auto* base = actor->GetActorBase()) {
            for (const auto& fData : base->factions) {
                if (fData.faction && !IsPlayerHouseholdFaction(fData.faction)) {
                    effectiveFactions[fData.faction] = fData.rank;
                }
            }
        }

        // 2. Extra faction changes on actor reference
        if (auto* extra = actor->extraList.GetByType<RE::ExtraFactionChanges>()) {
            for (const auto& fData : extra->factionChanges) {
                if (fData.faction && !IsPlayerHouseholdFaction(fData.faction)) {
                    if (fData.rank < 0) {
                        effectiveFactions.erase(fData.faction);
                    } else {
                        effectiveFactions[fData.faction] = fData.rank;
                    }
                }
            }
        }

        // Store active factions (rank >= 0)
        for (const auto& [faction, rank] : effectiveFactions) {
            if (faction && rank >= 0) {
                snapshot.factions.push_back({ faction->GetFormID(), rank });
            }
        }

        factionSnapshots_[childID] = snapshot;
        MARAS_LOG_INFO("SnapshotChildFactions: Recorded snapshot for child {:08X} ({} factions, crime: {:08X})",
                       childID, snapshot.factions.size(), snapshot.crimeFactionID);
    }

    void PlayerChildrenService::ClearChildExternalFactions(RE::FormID childID) {
        if (childID == 0 || IsSyntheticChildID(childID)) return;
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(childID);
        if (!actor) return;

        auto it = factionSnapshots_.find(childID);
        if (it != factionSnapshots_.end()) {
            for (const auto& entry : it->second.factions) {
                if (auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(entry.factionID)) {
                    Utils::RemoveFromFaction(actor, faction);
                }
            }
            MARAS_LOG_INFO("ClearChildExternalFactions: Cleared {} external factions from child {:08X}",
                           it->second.factions.size(), childID);
        }
    }

    void PlayerChildrenService::RestoreChildFactions(RE::FormID childID) {
        if (childID == 0 || IsSyntheticChildID(childID)) return;
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(childID);
        if (!actor) return;

        auto it = factionSnapshots_.find(childID);
        if (it != factionSnapshots_.end()) {
            for (const auto& entry : it->second.factions) {
                if (auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(entry.factionID)) {
                    Utils::AddToFaction(actor, faction, entry.rank);
                }
            }
            if (it->second.crimeFactionID != 0) {
                if (auto* crimeFaction = RE::TESForm::LookupByID<RE::TESFaction>(it->second.crimeFactionID)) {
                    Utils::SetCrimeFaction(actor, crimeFaction);
                }
            }
            MARAS_LOG_INFO("RestoreChildFactions: Restored {} snapshotted factions and crime faction {:08X} for child {:08X}",
                           it->second.factions.size(), it->second.crimeFactionID, childID);
            factionSnapshots_.erase(it);
        } else {
            MARAS_LOG_DEBUG("RestoreChildFactions: No faction snapshot found for child {:08X}", childID);
        }
        actor->EvaluatePackage();
    }

    bool PlayerChildrenService::HasFactionSnapshot(RE::FormID childID) const {
        return factionSnapshots_.find(childID) != factionSnapshots_.end();
    }

    void PlayerChildrenService::OnChildAddedToHousehold(RE::FormID childID, RE::FormID initialHouseFormID) {
        if (childID == 0) return;

        auto& state = children_[childID];
        state.childID = childID;
        state.isNewlyAdopted = true;

        if (!IsSyntheticChildID(childID)) {
            // 1. Snapshot original factions before modifying anything (if not already snapshotted)
            SnapshotChildFactions(childID);

            // 2. Clear external non-household factions so child strictly follows player family routines
            ClearChildExternalFactions(childID);

            // 3. Ensure household child factions
            auto& formCache = FormCache::GetSingleton();
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(childID)) {
                // Reinstate crime faction from snapshot if available
                auto itSnap = factionSnapshots_.find(childID);
                if (itSnap != factionSnapshots_.end() && itSnap->second.crimeFactionID != 0) {
                    if (auto* crimeFaction = RE::TESForm::LookupByID<RE::TESFaction>(itSnap->second.crimeFactionID)) {
                        Utils::SetCrimeFaction(actor, crimeFaction);
                    }
                }

                if (auto* playerChildFaction = formCache.GetPlayerChildFaction()) {
                    if (!actor->IsInFaction(playerChildFaction)) {
                        Utils::AddToFaction(actor, playerChildFaction, 0);
                        MARAS_LOG_INFO("OnChildAddedToHousehold: Added PlayerChild faction to child {:08X}", childID);
                    }
                }
                if (auto* byohChildOwnedFaction = formCache.GetBYOHRelationshipAdoptionChildOwnedFaction()) {
                    if (!actor->IsInFaction(byohChildOwnedFaction)) {
                        Utils::AddToFaction(actor, byohChildOwnedFaction, 0);
                        MARAS_LOG_INFO("OnChildAddedToHousehold: Added BYOHRelationshipAdoptionChildOwnedFaction to child {:08X}", childID);
                    }
                }
                if (auto* byohAdoptionFaction = formCache.GetBYOHRelationshipAdoptionFaction()) {
                    if (!actor->IsInFaction(byohAdoptionFaction)) {
                        Utils::AddToFaction(actor, byohAdoptionFaction, 0);
                        MARAS_LOG_INFO("OnChildAddedToHousehold: Added BYOHRelationshipAdoptionFaction to child {:08X}", childID);
                    }
                }
                if (auto* playerFaction = formCache.GetPlayerFaction()) {
                    if (!actor->IsInFaction(playerFaction)) {
                        Utils::AddToFaction(actor, playerFaction, 0);
                        MARAS_LOG_INFO("OnChildAddedToHousehold: Added PlayerFaction to child {:08X}", childID);
                    }
                }
                if (auto* adoptableFaction = formCache.GetBYOHRelationshipAdoptableFaction()) {
                    Utils::AddToFaction(actor, adoptableFaction, 25);
                    MARAS_LOG_INFO("OnChildAddedToHousehold: Updated BYOHRelationshipAdoptableFaction rank to 25 for child {:08X}", childID);
                }
            }

            // Update TTM_PlayerChildBothParents faction status
            NPCRelationshipManager::GetSingleton().UpdateChildBothParentsFaction(childID);
        }

        if (initialHouseFormID != 0) {
            PlayerHouseService::GetSingleton().RelocateFamilyUnit(childID, initialHouseFormID, true);
        } else {
            ApplyPackageForCurrentState(childID);
        }

        MARAS_LOG_INFO("OnChildAddedToHousehold: Registered child {:08X} in household (house: {:08X})", childID, initialHouseFormID);
    }

    void PlayerChildrenService::OnChildRemovedFromHousehold(RE::FormID childID) {
        auto it = children_.find(childID);
        if (it == children_.end()) return;

        RemovePet(childID);
        RemoveCritter(childID);

        if (!PlayerHouseService::GetSingleton().RemoveTenantFromPlayerHouse(childID)) {
            MARAS_LOG_DEBUG("OnChildRemovedFromHousehold: child {:08X} was not a registered house tenant", childID);
        }

        if (!IsSyntheticChildID(childID)) {
            auto& formCache = FormCache::GetSingleton();
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(childID)) {
                if (auto* playerChildFaction = formCache.GetPlayerChildFaction()) {
                    Utils::RemoveFromFaction(actor, playerChildFaction);
                }
                if (auto* byohChildOwnedFaction = formCache.GetBYOHRelationshipAdoptionChildOwnedFaction()) {
                    Utils::RemoveFromFaction(actor, byohChildOwnedFaction);
                }
                if (auto* byohAdoptionFaction = formCache.GetBYOHRelationshipAdoptionFaction()) {
                    Utils::RemoveFromFaction(actor, byohAdoptionFaction);
                }
                if (auto* adoptableFaction = formCache.GetBYOHRelationshipAdoptableFaction()) {
                    Utils::RemoveFromFaction(actor, adoptableFaction);
                }
                if (auto* playerFaction = formCache.GetPlayerFaction()) {
                    Utils::RemoveFromFaction(actor, playerFaction);
                }
                if (auto* bothParentsFaction = formCache.GetPlayerChildBothParentsFaction()) {
                    Utils::RemoveFromFaction(actor, bothParentsFaction);
                }
            }

            // Restore original snapshotted factions
            RestoreChildFactions(childID);
        }

        children_.erase(it);
        MARAS_LOG_INFO("OnChildRemovedFromHousehold: Removed child {:08X} from household and restored factions", childID);
    }

    bool PlayerChildrenService::AssignChildToHouse(RE::FormID childID, RE::FormID playerHouseFormID, bool moveChild) {
        if (childID == 0 || playerHouseFormID == 0) return false;

        if (!PlayerHouseService::GetSingleton().RegisterTenantInPlayerHouse(childID, playerHouseFormID)) {
            MARAS_LOG_WARN("AssignChildToHouse: Failed to register child {:08X} as tenant in house {:08X}", childID, playerHouseFormID);
            return false;
        }

        auto& state = children_[childID];
        state.childID = childID;
        state.currentHouseFormID = playerHouseFormID;

        if (!IsSyntheticChildID(childID)) {
            if (auto* childActor = RE::TESForm::LookupByID<RE::Actor>(childID)) {
                if (auto* childMarker = GetOrCreateChildHomeMarker(childActor)) {
                    if (auto* houseMarker = PlayerHouseService::GetSingleton().GetHouseMarkerRef(playerHouseFormID)) {
                        childMarker->MoveTo(houseMarker);
                        MARAS_LOG_DEBUG("AssignChildToHouse: Moved child {:08X} home marker {:08X} to house marker {:08X}",
                                        childID, childMarker->GetFormID(), houseMarker->GetFormID());
                        if (moveChild) {
                            childActor->MoveTo(childMarker);
                            childActor->EvaluatePackage();
                            MARAS_LOG_DEBUG("AssignChildToHouse: Moved child {:08X} to home marker {:08X}",
                                            childID, childMarker->GetFormID());
                        }
                    }
                }
            }

            ApplyPackageForCurrentState(childID);
        }

        MARAS_LOG_INFO("AssignChildToHouse: Child {:08X} assigned to house {:08X} (moveChild: {})", childID, playerHouseFormID, moveChild);
        return true;
    }

    RE::FormID PlayerChildrenService::GetChildHouseFormID(RE::FormID childID) const {
        auto it = children_.find(childID);
        if (it != children_.end() && it->second.currentHouseFormID != 0) {
            return it->second.currentHouseFormID;
        }
        return PlayerHouseService::GetSingleton().GetTenantHouseFormID(childID);
    }

    bool PlayerChildrenService::SetChildHomeMarker(RE::FormID childID, RE::FormID markerFormID) {
        if (childID == 0) return false;

        auto& state = children_[childID];
        state.childID = childID;
        state.homeMarkerFormID = markerFormID;

        if (markerFormID != 0 && !IsSyntheticChildID(childID)) {
            auto* keyword = FormCache::GetSingleton().GetHomeSandboxKeyword();
            Utils::SetLinkedRef(childID, markerFormID, keyword);
        }

        MARAS_LOG_INFO("SetChildHomeMarker: Child {:08X} home marker set to {:08X}", childID, markerFormID);
        return true;
    }

    RE::FormID PlayerChildrenService::GetChildHomeMarker(RE::FormID childID) const {
        auto it = children_.find(childID);
        if (it != children_.end()) {
            return it->second.homeMarkerFormID;
        }
        return 0;
    }

    RE::TESObjectREFR* PlayerChildrenService::GetOrCreateChildHomeMarker(RE::Actor* childActor) {
        if (!childActor) return nullptr;

        RE::FormID childID = childActor->GetFormID();
        auto it = children_.find(childID);
        if (it != children_.end() && it->second.homeMarkerFormID != 0) {
            if (auto* existingMarker = RE::TESForm::LookupByID<RE::TESObjectREFR>(it->second.homeMarkerFormID)) {
                return existingMarker;
            }
        }

        auto* homeMarkerStatic = FormCache::GetSingleton().GetHomeSandboxMarkerStatic();
        if (!homeMarkerStatic) {
            MARAS_LOG_WARN("GetOrCreateChildHomeMarker: homeMarkerStatic form not found");
            return nullptr;
        }

        auto markerRef = childActor->PlaceObjectAtMe(homeMarkerStatic, true);
        if (!markerRef) {
            MARAS_LOG_WARN("GetOrCreateChildHomeMarker: PlaceObjectAtMe failed for child {:08X}", childID);
            return nullptr;
        }

        RE::FormID markerID = markerRef->GetFormID();
        SetChildHomeMarker(childID, markerID);

        MARAS_LOG_INFO("GetOrCreateChildHomeMarker: Placed new home marker {:08X} for child {:08X}", markerID, childID);
        return markerRef.get();
    }


    void PlayerChildrenService::ApplyPackageForCurrentState(RE::FormID childID) {
        if (childID == 0 || IsSyntheticChildID(childID)) return;
        auto it = children_.find(childID);
        if (it == children_.end()) return;
        auto& state = it->second;

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(childID);
        if (!actor) return;

        // Sync Variable06 (Order) and Variable07 (Forcegreet) ActorValues
        if (auto* avOwner = actor->AsActorValueOwner()) {
            avOwner->SetActorValue(RE::ActorValue::kVariable06, state.currentOrder);
            avOwner->SetActorValue(RE::ActorValue::kVariable07, state.forcegreetPending ? 1.0f : 0.0f);
        }

        actor->EvaluatePackage();
        MARAS_LOG_DEBUG("ApplyPackageForCurrentState: Updated child {:08X} (order: {}, forcegreet: {})", childID, state.currentOrder, state.forcegreetPending);
    }

    void PlayerChildrenService::ResetForceGreeting(RE::FormID childID) {
        if (childID == 0) return;
        auto it = children_.find(childID);
        if (it == children_.end()) return;

        it->second.forcegreetPending = false;
        if (!IsSyntheticChildID(childID)) {
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(childID)) {
                if (auto* avOwner = actor->AsActorValueOwner()) {
                    avOwner->SetActorValue(RE::ActorValue::kVariable07, 0.0f);
                }
                actor->EvaluatePackage();
            }
        }
        MARAS_LOG_INFO("ResetForceGreeting: Reset forcegreet for child {:08X}", childID);
    }

    bool PlayerChildrenService::IssueOrder(RE::FormID childID, float orderValue, float durationGameHours) {
        auto it = children_.find(childID);
        if (it == children_.end()) return false;

        auto& state = it->second;

        // Same-order-lock (ported from vanilla IssueOrderWithDuration): void gift-triggered play
        // orders (dummy/doll) if another child in this household is already running that exact order.
        if (std::abs(orderValue - 1.1f) < 0.01f || std::abs(orderValue - 1.2f) < 0.01f) {
            for (const auto& [otherChildID, otherState] : children_) {
                if (otherChildID == childID) continue;
                if (otherState.currentHouseFormID != state.currentHouseFormID) continue;
                if (std::abs(otherState.currentOrder - orderValue) < 0.01f) {
                    MARAS_LOG_DEBUG("IssueOrder: Voided order {} for child {:08X}, already active on child {:08X} in the same household",
                                    orderValue, childID, otherChildID);
                    return false;
                }
            }
        }

        state.currentOrder = orderValue;

        auto* calendar = RE::Calendar::GetSingleton();
        float nowDay = calendar ? calendar->GetDaysPassed() : 0.0f;
        state.orderExpiryGameDay = (durationGameHours > 0.0f) ? (nowDay + (durationGameHours / 24.0f)) : -1.0f;

        ApplyPackageForCurrentState(childID);
        MARAS_LOG_INFO("IssueOrder: Issued order {} to child {:08X} for {} game hours", orderValue, childID, durationGameHours);
        return true;
    }

    void PlayerChildrenService::Tick(float currentGameDay) {
        for (auto& [childID, state] : children_) {
            if (state.orderExpiryGameDay >= 0.0f && currentGameDay >= state.orderExpiryGameDay) {
                MARAS_LOG_INFO("Tick: Order {} expired for child {:08X} (game day {})", state.currentOrder, childID, currentGameDay);
                state.currentOrder = 0.0f;
                state.orderExpiryGameDay = -1.0f;
                ApplyPackageForCurrentState(childID);
            }
        }
    }

    void PlayerChildrenService::RollForcegreetForHousehold() {
        if (children_.empty()) return;

        // Track houses that already have an active forcegreet slot (ONE shared per-household slot)
        std::unordered_set<RE::FormID> housesWithActiveForcegreet;
        std::unordered_map<RE::FormID, std::vector<RE::FormID>> houseChildrenMap;

        for (const auto& [childID, state] : children_) {
            if (IsSyntheticChildID(childID)) continue;
            RE::FormID houseID = state.currentHouseFormID != 0 ? state.currentHouseFormID : GetChildHouseFormID(childID);
            if (houseID == 0) continue;

            if (state.forcegreetPending) {
                housesWithActiveForcegreet.insert(houseID);
            } else {
                houseChildrenMap[houseID].push_back(childID);
            }
        }

        for (const auto& [houseID, list] : houseChildrenMap) {
            if (list.empty() || housesWithActiveForcegreet.find(houseID) != housesWithActiveForcegreet.end()) {
                continue;
            }
            RE::FormID chosenChildID = PickWeightedChild(list);
            if (chosenChildID == 0) continue;

            auto& state = children_[chosenChildID];
            state.forcegreetPending = true;
            ApplyPackageForCurrentState(chosenChildID);

            MARAS_LOG_INFO("RollForcegreetForHousehold: Picked child {:08X} for forcegreet in house {:08X}", chosenChildID, houseID);
        }
    }

    namespace {
        void SetPetOrCritterFactionsAndPackage(RE::FormID companionID, RE::FormID childID) {
            if (companionID == 0) return;
            if (auto* faction = FormCache::GetSingleton().GetPlayerChildPetFaction()) {
                Utils::AddToFaction(companionID, faction, 0);
            }
            if (auto* playerFaction = FormCache::GetSingleton().GetPlayerFaction()) {
                Utils::AddToFaction(companionID, playerFaction, 0);
            }
            auto* keyword = FormCache::GetSingleton().GetHomeSandboxKeyword();
            RE::FormID pkgID = 0;
            if (auto* petPkg = FormCache::GetSingleton().GetPetCritterHomeSandboxPackage()) {
                pkgID = petPkg->GetFormID();
            }
            Utils::AssignPackageWithLinkedRef(companionID, childID, keyword, pkgID);
        }

        void ClearPetOrCritterFactions(RE::FormID companionID) {
            if (companionID == 0) return;
            if (auto* faction = FormCache::GetSingleton().GetPlayerChildPetFaction()) {
                Utils::RemoveFromFaction(companionID, faction);
            }
            if (auto* playerFaction = FormCache::GetSingleton().GetPlayerFaction()) {
                Utils::RemoveFromFaction(companionID, playerFaction);
            }
        }

        bool IsCompanionRetryCooldownElapsed(float storedTs) {
            if (storedTs <= 0.0f) return true;
            float currentDays = 0.0f;
            if (auto* calendar = RE::Calendar::GetSingleton()) {
                currentDays = calendar->GetDaysPassed();
            }
            return (currentDays - storedTs) >= 15.0f;
        }
    }  // namespace

    bool PlayerChildrenService::AssignPet(RE::FormID childID, RE::FormID petActorOrBaseID) {
        auto it = children_.find(childID);
        if (it == children_.end()) return false;

        RE::FormID oldPetID = it->second.petFormID;
        if (oldPetID != 0 && oldPetID != petActorOrBaseID) {
            ClearPetOrCritterFactions(oldPetID);
        }

        it->second.petFormID = petActorOrBaseID;

        if (oldPetID != 0 && oldPetID != petActorOrBaseID) {
            if (GetChildForPetOrCritter(oldPetID) == 0) {
                PackageOverrideService::GetSingleton().UnregisterActor(oldPetID);
            }
        }

        SetPetOrCritterFactionsAndPackage(petActorOrBaseID, childID);

        MARAS_LOG_INFO("AssignPet: Assigned pet {:08X} to child {:08X}", petActorOrBaseID, childID);
        return true;
    }

    RE::FormID PlayerChildrenService::GetPet(RE::FormID childID) const {
        auto it = children_.find(childID);
        return (it != children_.end()) ? it->second.petFormID : 0;
    }

    bool PlayerChildrenService::RemovePet(RE::FormID childID) {
        if (childID == 0) return false;
        auto it = children_.find(childID);
        if (it == children_.end()) return false;

        RE::FormID oldPetID = it->second.petFormID;
        it->second.petFormID = 0;

        if (oldPetID != 0) {
            ClearPetOrCritterFactions(oldPetID);
            if (GetChildForPetOrCritter(oldPetID) == 0) {
                PackageOverrideService::GetSingleton().UnregisterActor(oldPetID);
            }
        }

        MARAS_LOG_INFO("RemovePet: Removed pet from child {:08X}", childID);
        return true;
    }

    RE::FormID PlayerChildrenService::GetChildForPet(RE::FormID petID) const {
        if (petID == 0) return 0;
        for (const auto& [childID, state] : children_) {
            if (state.petFormID == petID) return childID;
        }
        return 0;
    }


    void PlayerChildrenService::SetChildPetBanned(RE::FormID childID, bool banned) {
        if (childID == 0) return;
        auto it = children_.find(childID);
        if (it != children_.end()) {
            it->second.petBanned = banned;
            MARAS_LOG_INFO("SetChildPetBanned: Set child {:08X} petBanned = {}", childID, banned);
        }
    }

    bool PlayerChildrenService::IsChildPetBanned(RE::FormID childID) const {
        auto it = children_.find(childID);
        return (it != children_.end()) ? it->second.petBanned : false;
    }

    bool PlayerChildrenService::IsPetBanned(RE::FormID childID) const {
        if (Settings::GetSingleton().IsGlobalPetBanned()) {
            return true;
        }
        if (IsChildPetBanned(childID)) {
            return true;
        }
        if (!IsPetRetryAvailable(childID)) {
            return true;
        }
        return false;
    }

    void PlayerChildrenService::SetPetRetryTimestamp(RE::FormID childID, float timestamp) {
        if (childID == 0) return;
        auto it = children_.find(childID);
        if (it != children_.end()) {
            it->second.petRetryTimestamp = timestamp;
            MARAS_LOG_INFO("SetPetRetryTimestamp: Set child {:08X} petRetryTimestamp = {:.2f}", childID, timestamp);
        }
    }

    void PlayerChildrenService::RecordPetRejected(RE::FormID childID) {
        if (childID == 0) return;
        float currentDays = 0.0f;
        if (auto calendar = RE::Calendar::GetSingleton()) {
            currentDays = calendar->GetDaysPassed();
        }
        SetPetRetryTimestamp(childID, currentDays);
    }

    float PlayerChildrenService::GetPetRetryTimestamp(RE::FormID childID) const {
        auto it = children_.find(childID);
        return (it != children_.end()) ? it->second.petRetryTimestamp : 0.0f;
    }

    bool PlayerChildrenService::IsPetRetryAvailable(RE::FormID childID) const {
        auto it = children_.find(childID);
        if (it == children_.end()) return true;
        return IsCompanionRetryCooldownElapsed(it->second.petRetryTimestamp);
    }

    bool PlayerChildrenService::AssignCritter(RE::FormID childID, RE::FormID critterFormID) {
        if (childID == 0) return false;
        auto it = children_.find(childID);
        if (it == children_.end()) return false;

        RE::FormID oldCritterID = it->second.critterFormID;
        if (oldCritterID != 0 && oldCritterID != critterFormID) {
            ClearPetOrCritterFactions(oldCritterID);
        }

        it->second.critterFormID = critterFormID;

        if (oldCritterID != 0 && oldCritterID != critterFormID) {
            if (GetChildForPetOrCritter(oldCritterID) == 0) {
                PackageOverrideService::GetSingleton().UnregisterActor(oldCritterID);
            }
        }

        SetPetOrCritterFactionsAndPackage(critterFormID, childID);

        MARAS_LOG_INFO("AssignCritter: Assigned critter {:08X} to child {:08X}", critterFormID, childID);
        return true;
    }

    RE::FormID PlayerChildrenService::GetCritter(RE::FormID childID) const {
        auto it = children_.find(childID);
        return (it != children_.end()) ? it->second.critterFormID : 0;
    }

    bool PlayerChildrenService::RemoveCritter(RE::FormID childID) {
        if (childID == 0) return false;
        auto it = children_.find(childID);
        if (it == children_.end()) return false;

        RE::FormID oldCritterID = it->second.critterFormID;
        it->second.critterFormID = 0;

        if (oldCritterID != 0) {
            ClearPetOrCritterFactions(oldCritterID);
            if (GetChildForPetOrCritter(oldCritterID) == 0) {
                PackageOverrideService::GetSingleton().UnregisterActor(oldCritterID);
            }
        }

        MARAS_LOG_INFO("RemoveCritter: Removed critter from child {:08X}", childID);
        return true;
    }

    RE::FormID PlayerChildrenService::GetChildForCritter(RE::FormID critterID) const {
        if (critterID == 0) return 0;
        for (const auto& [childID, state] : children_) {
            if (state.critterFormID == critterID) return childID;
        }
        return 0;
    }

    RE::FormID PlayerChildrenService::GetChildForPetOrCritter(RE::FormID petOrCritterID) const {
        if (petOrCritterID == 0) return 0;
        for (const auto& [childID, state] : children_) {
            if (state.petFormID == petOrCritterID || state.critterFormID == petOrCritterID) return childID;
        }
        return 0;
    }

    std::vector<RE::FormID> PlayerChildrenService::GetAllPetAndCritterIDs() const {
        std::vector<RE::FormID> result;
        for (const auto& [childID, state] : children_) {
            if (state.petFormID != 0) {
                result.push_back(state.petFormID);
            }
            if (state.critterFormID != 0) {
                result.push_back(state.critterFormID);
            }
        }
        return result;
    }

    void PlayerChildrenService::SetChildCritterBanned(RE::FormID childID, bool banned) {
        if (childID == 0) return;
        auto it = children_.find(childID);
        if (it != children_.end()) {
            it->second.critterBanned = banned;
            MARAS_LOG_INFO("SetChildCritterBanned: Set child {:08X} critterBanned = {}", childID, banned);
        }
    }

    bool PlayerChildrenService::IsChildCritterBanned(RE::FormID childID) const {
        auto it = children_.find(childID);
        return (it != children_.end()) ? it->second.critterBanned : false;
    }

    bool PlayerChildrenService::IsCritterBanned(RE::FormID childID) const {
        if (Settings::GetSingleton().IsGlobalCritterBanned()) {
            return true;
        }
        if (IsChildCritterBanned(childID)) {
            return true;
        }
        if (!IsCritterRetryAvailable(childID)) {
            return true;
        }
        return false;
    }

    void PlayerChildrenService::SetIsNewlyAdopted(RE::FormID childID, bool isNewlyAdopted, bool clearSameHouse) {
        if (childID == 0) return;
        auto it = children_.find(childID);
        if (it != children_.end()) {
            it->second.isNewlyAdopted = isNewlyAdopted;
            MARAS_LOG_INFO("SetIsNewlyAdopted: Set child {:08X} isNewlyAdopted = {}", childID, isNewlyAdopted);

            if (clearSameHouse) {
                RE::FormID houseID = it->second.currentHouseFormID != 0 ? it->second.currentHouseFormID : GetChildHouseFormID(childID);
                if (houseID != 0) {
                    for (auto& [otherID, state] : children_) {
                        if (otherID == childID) continue;
                        if (state.currentHouseFormID == houseID || (state.currentHouseFormID == 0 && GetChildHouseFormID(otherID) == houseID)) {
                            state.isNewlyAdopted = false;
                            MARAS_LOG_INFO("SetIsNewlyAdopted: Cleared isNewlyAdopted for child {:08X} in house {:08X}", otherID, houseID);
                        }
                    }
                }
            }
        }
    }

    bool PlayerChildrenService::IsNewlyAdopted(RE::FormID childID) const {
        auto it = children_.find(childID);
        return (it != children_.end()) ? it->second.isNewlyAdopted : false;
    }

    void PlayerChildrenService::SetChildChestIntroduced(bool introduced) {
        childChestIntroduced_ = introduced;
        MARAS_LOG_INFO("SetChildChestIntroduced: Set global childChestIntroduced = {}", introduced);
    }

    bool PlayerChildrenService::IsChildChestIntroduced() const {
        return childChestIntroduced_;
    }

    void PlayerChildrenService::SetCritterRetryTimestamp(RE::FormID childID, float timestamp) {
        if (childID == 0) return;
        auto it = children_.find(childID);
        if (it != children_.end()) {
            it->second.critterRetryTimestamp = timestamp;
            MARAS_LOG_INFO("SetCritterRetryTimestamp: Set child {:08X} critterRetryTimestamp = {:.2f}", childID, timestamp);
        }
    }

    void PlayerChildrenService::RecordCritterRejected(RE::FormID childID) {
        if (childID == 0) return;
        float currentDays = 0.0f;
        if (auto calendar = RE::Calendar::GetSingleton()) {
            currentDays = calendar->GetDaysPassed();
        }
        SetCritterRetryTimestamp(childID, currentDays);
    }

    float PlayerChildrenService::GetCritterRetryTimestamp(RE::FormID childID) const {
        auto it = children_.find(childID);
        return (it != children_.end()) ? it->second.critterRetryTimestamp : 0.0f;
    }

    bool PlayerChildrenService::IsCritterRetryAvailable(RE::FormID childID) const {
        auto it = children_.find(childID);
        if (it == children_.end()) return true;
        return IsCompanionRetryCooldownElapsed(it->second.critterRetryTimestamp);
    }

    std::optional<ChildHouseholdState> PlayerChildrenService::GetState(RE::FormID childID) const {
        auto it = children_.find(childID);
        if (it != children_.end()) return it->second;
        return std::nullopt;
    }

    std::vector<RE::FormID> PlayerChildrenService::GetAllPlayerChildren(bool onlyRealActors) const {
        std::vector<RE::FormID> list;
        list.reserve(children_.size());
        for (const auto& [cID, state] : children_) {
            if (!onlyRealActors || !IsSyntheticChildID(cID)) {
                list.push_back(cID);
            }
        }
        return list;
    }

    std::vector<RE::FormID> PlayerChildrenService::GetChildrenForHouse(RE::FormID playerHouseFormID, bool onlyRealActors) const {
        std::vector<RE::FormID> list;
        MARAS_LOG_DEBUG("GetChildrenForHouse: Player house form ID: {:08X} (onlyRealActors: {})", playerHouseFormID, onlyRealActors);
        if (playerHouseFormID == 0) return list;
        for (const auto& [cID, state] : children_) {
            MARAS_LOG_DEBUG("GetChildrenForHouse: Child {:08X} has house {:08X}", cID, state.currentHouseFormID);
            if (state.currentHouseFormID == playerHouseFormID) {
                if (!onlyRealActors || !IsSyntheticChildID(cID)) {
                    list.push_back(cID);
                }
            }
        }
        return list;
    }

    void PlayerChildrenService::SetChildPlayedScene(RE::FormID childID, int32_t sceneID, bool played) {
        if (childID == 0) return;
        auto it = children_.find(childID);
        if (it != children_.end()) {
            if (sceneID == 1) it->second.playedScene1 = played;
            else if (sceneID == 2) it->second.playedScene2 = played;
            MARAS_LOG_INFO("SetChildPlayedScene: child {:08X} scene {} = {}", childID, sceneID, played);
        }
    }

    bool PlayerChildrenService::HasChildPlayedScene(RE::FormID childID, int32_t sceneID) const {
        auto it = children_.find(childID);
        if (it == children_.end()) return false;
        if (sceneID == 1) return it->second.playedScene1;
        if (sceneID == 2) return it->second.playedScene2;
        return false;
    }

    void PlayerChildrenService::RecordChildInteraction(RE::FormID childID) {
        if (childID == 0) return;
        auto it = children_.find(childID);
        if (it != children_.end()) {
            float currentDays = 0.0f;
            if (auto calendar = RE::Calendar::GetSingleton()) {
                currentDays = calendar->GetDaysPassed();
            }
            it->second.lastInteractionTimestamp = currentDays;
            MARAS_LOG_INFO("RecordChildInteraction: child {:08X} timestamp = {:.2f}", childID, currentDays);
        }
    }

    float PlayerChildrenService::GetLastInteractionTimestamp(RE::FormID childID) const {
        auto it = children_.find(childID);
        return (it != children_.end()) ? it->second.lastInteractionTimestamp : 0.0f;
    }

    RE::FormID PlayerChildrenService::PickWeightedChild(const std::vector<RE::FormID>& candidateIDs) const {
        std::vector<RE::FormID> validCandidates;
        validCandidates.reserve(candidateIDs.size());
        for (auto cID : candidateIDs) {
            if (!IsSyntheticChildID(cID)) {
                validCandidates.push_back(cID);
            }
        }

        if (validCandidates.empty()) return 0;
        if (validCandidates.size() == 1) return validCandidates[0];

        float currentDays = 0.0f;
        if (auto calendar = RE::Calendar::GetSingleton()) {
            currentDays = calendar->GetDaysPassed();
        }

        std::vector<float> weights;
        weights.reserve(validCandidates.size());

        for (auto cID : validCandidates) {
            float lastTs = GetLastInteractionTimestamp(cID);
            // Weight is days elapsed since last interaction (min 0.1, default 30 days if never interacted)
            float daysElapsed = (lastTs > 0.0f) ? (currentDays - lastTs) : 30.0f;
            weights.push_back(std::max(0.1f, daysElapsed));
        }

        thread_local std::mt19937 gen(std::random_device{}());
        std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
        size_t chosenIndex = dist(gen);

        return validCandidates[chosenIndex];
    }

    RE::FormID PlayerChildrenService::GetNewlyAdoptedChild(RE::FormID playerHouseFormID) const {
        std::vector<RE::FormID> candidates;
        if (playerHouseFormID == 0) return 0;
        for (const auto& [cID, state] : children_) {
            if (state.currentHouseFormID == playerHouseFormID && state.isNewlyAdopted && !IsSyntheticChildID(cID)) {
                candidates.push_back(cID);
            }
        }
        return PickWeightedChild(candidates);
    }

    RE::FormID PlayerChildrenService::GetChildWithoutPet(RE::FormID playerHouseFormID) const {
        std::vector<RE::FormID> candidates;
        if (playerHouseFormID == 0) return 0;
        for (const auto& [cID, state] : children_) {
            if (state.currentHouseFormID == playerHouseFormID && state.petFormID == 0 && !IsPetBanned(cID) && !IsSyntheticChildID(cID)) {
                candidates.push_back(cID);
            }
        }
        return PickWeightedChild(candidates);
    }

    RE::FormID PlayerChildrenService::GetChildWithoutCritter(RE::FormID playerHouseFormID) const {
        std::vector<RE::FormID> candidates;
        if (playerHouseFormID == 0) return 0;
        for (const auto& [cID, state] : children_) {
            if (state.currentHouseFormID == playerHouseFormID && state.critterFormID == 0 && !IsCritterBanned(cID) && !IsSyntheticChildID(cID)) {
                candidates.push_back(cID);
            }
        }
        return PickWeightedChild(candidates);
    }

    std::vector<RE::FormID> PlayerChildrenService::GetChildrenWithoutScene(RE::FormID playerHouseFormID, int32_t sceneID) const {
        std::vector<RE::FormID> candidates;
        if (playerHouseFormID == 0) return candidates;
        for (const auto& [cID, state] : children_) {
            if (state.currentHouseFormID == playerHouseFormID && !HasChildPlayedScene(cID, sceneID) && !IsSyntheticChildID(cID)) {
                candidates.push_back(cID);
            }
        }
        if (candidates.size() < 2) {
            return {}; // Scene requires exactly 2 children; return empty if fewer than 2 eligible
        }
        if (candidates.size() == 2) {
            return candidates;
        }

        thread_local std::mt19937 gen(std::random_device{}());
        std::shuffle(candidates.begin(), candidates.end(), gen);
        candidates.resize(2);
        return candidates;
    }

    RE::FormID PlayerChildrenService::GetWeightedRandomChild(RE::FormID playerHouseFormID) const {
        return PickWeightedChild(GetChildrenForHouse(playerHouseFormID, true));
    }

    bool PlayerChildrenService::Save(SKSE::SerializationInterface* serialization) const {
        if (!serialization) return false;

        // Save global state
        if (!serialization->WriteRecordData(childChestIntroduced_)) return false;

        std::uint32_t count = static_cast<std::uint32_t>(children_.size());
        if (!serialization->WriteRecordData(count)) return false;

        for (const auto& [cID, state] : children_) {
            if (!serialization->WriteRecordData(state.childID) ||
                !serialization->WriteRecordData(state.currentHouseFormID) ||
                !serialization->WriteRecordData(state.homeMarkerFormID) ||
                !serialization->WriteRecordData(state.petFormID) ||
                !serialization->WriteRecordData(state.critterFormID) ||
                !serialization->WriteRecordData(state.currentOrder) ||
                !serialization->WriteRecordData(state.orderExpiryGameDay) ||
                !serialization->WriteRecordData(state.forcegreetPending) ||
                !serialization->WriteRecordData(state.critterBanned) ||
                !serialization->WriteRecordData(state.petBanned) ||
                !serialization->WriteRecordData(state.isNewlyAdopted) ||
                !serialization->WriteRecordData(state.critterRetryTimestamp) ||
                !serialization->WriteRecordData(state.petRetryTimestamp) ||
                !serialization->WriteRecordData(state.playedScene1) ||
                !serialization->WriteRecordData(state.playedScene2) ||
                !serialization->WriteRecordData(state.lastInteractionTimestamp)) {
                MARAS_LOG_ERROR("Failed to write PlayerChildrenService state for child {:08X}", cID);
                return false;
            }
        }

        MARAS_LOG_INFO("Saved PlayerChildrenService state for {} children (chestIntroduced: {})", count, childChestIntroduced_);
        return true;
    }

    bool PlayerChildrenService::Load(SKSE::SerializationInterface* serialization) {
        if (!serialization) return false;

        Revert();

        // Load global state
        if (!serialization->ReadRecordData(childChestIntroduced_)) return false;

        constexpr std::uint32_t kMaxChildren = 10000;
        std::uint32_t count = 0;
        if (!serialization->ReadRecordData(count)) return false;
        if (count > kMaxChildren) {
            MARAS_LOG_ERROR("PlayerChildrenService::Load - corrupt count {}", count);
            return false;
        }

        auto& relMgr = NPCRelationshipManager::GetSingleton();

        for (std::uint32_t i = 0; i < count; ++i) {
            ChildHouseholdState state;
            RE::FormID oldCID = 0, oldHouse = 0, oldHomeMarker = 0, oldPet = 0, oldCritter = 0;

            if (!serialization->ReadRecordData(oldCID) ||
                !serialization->ReadRecordData(oldHouse) ||
                !serialization->ReadRecordData(oldHomeMarker) ||
                !serialization->ReadRecordData(oldPet) ||
                !serialization->ReadRecordData(oldCritter) ||
                !serialization->ReadRecordData(state.currentOrder) ||
                !serialization->ReadRecordData(state.orderExpiryGameDay) ||
                !serialization->ReadRecordData(state.forcegreetPending) ||
                !serialization->ReadRecordData(state.critterBanned) ||
                !serialization->ReadRecordData(state.petBanned) ||
                !serialization->ReadRecordData(state.isNewlyAdopted) ||
                !serialization->ReadRecordData(state.critterRetryTimestamp) ||
                !serialization->ReadRecordData(state.petRetryTimestamp) ||
                !serialization->ReadRecordData(state.playedScene1) ||
                !serialization->ReadRecordData(state.playedScene2) ||
                !serialization->ReadRecordData(state.lastInteractionTimestamp)) {
                return false;
            }

            bool validCID = false;
            if (IsSyntheticChildID(oldCID)) {
                state.childID = oldCID;
                validCID = relMgr.GetChildEntry(oldCID).has_value();
            } else if (oldCID == kPlayerFormID) {
                state.childID = kPlayerFormID;
                validCID = true;
            } else {
                validCID = serialization->ResolveFormID(oldCID, state.childID);
            }

            if (validCID && state.childID != 0) {
                if (oldHouse != 0) serialization->ResolveFormID(oldHouse, state.currentHouseFormID);
                if (oldHomeMarker != 0) serialization->ResolveFormID(oldHomeMarker, state.homeMarkerFormID);
                if (oldPet != 0) serialization->ResolveFormID(oldPet, state.petFormID);
                if (oldCritter != 0) serialization->ResolveFormID(oldCritter, state.critterFormID);
                children_[state.childID] = state;
            }
        }

        MARAS_LOG_INFO("Loaded PlayerChildrenService state for {} children (chestIntroduced: {})", children_.size(), childChestIntroduced_);
        return true;
    }

    void PlayerChildrenService::Revert() {
        children_.clear();
        childChestIntroduced_ = false;
        RevertFactionSnapshots();
        MARAS_LOG_INFO("Reverted PlayerChildrenService data");
    }

    bool PlayerChildrenService::SaveFactionSnapshots(SKSE::SerializationInterface* serialization) const {
        if (!serialization) return false;

        std::uint32_t count = static_cast<std::uint32_t>(factionSnapshots_.size());
        if (!serialization->WriteRecordData(count)) return false;

        for (const auto& [childID, snapshot] : factionSnapshots_) {
            if (!serialization->WriteRecordData(childID) ||
                !serialization->WriteRecordData(snapshot.crimeFactionID)) {
                return false;
            }
            std::uint32_t fCount = static_cast<std::uint32_t>(snapshot.factions.size());
            if (!serialization->WriteRecordData(fCount)) return false;
            for (const auto& fEntry : snapshot.factions) {
                if (!serialization->WriteRecordData(fEntry.factionID) ||
                    !serialization->WriteRecordData(fEntry.rank)) {
                    return false;
                }
            }
        }
        MARAS_LOG_INFO("Saved {} child faction snapshots", count);
        return true;
    }

    bool PlayerChildrenService::LoadFactionSnapshots(SKSE::SerializationInterface* serialization) {
        if (!serialization) return false;

        RevertFactionSnapshots();

        constexpr std::uint32_t kMaxSnapshots = 10000;
        constexpr std::uint32_t kMaxFactionsPerSnapshot = 1000;

        std::uint32_t count = 0;
        if (!serialization->ReadRecordData(count)) return false;
        if (count > kMaxSnapshots) {
            MARAS_LOG_ERROR("PlayerChildrenService::LoadFactionSnapshots - corrupt count {}", count);
            return false;
        }

        for (std::uint32_t i = 0; i < count; ++i) {
            RE::FormID oldChildID = 0, oldCrimeFactionID = 0;
            if (!serialization->ReadRecordData(oldChildID) ||
                !serialization->ReadRecordData(oldCrimeFactionID)) {
                return false;
            }

            RE::FormID newChildID = 0;
            bool validChild = false;
            if (!IsSyntheticChildID(oldChildID)) {
                validChild = (oldChildID == kPlayerFormID) ? (newChildID = kPlayerFormID, true)
                                                           : serialization->ResolveFormID(oldChildID, newChildID);
            }

            RE::FormID newCrimeFactionID = 0;
            if (oldCrimeFactionID != 0) {
                serialization->ResolveFormID(oldCrimeFactionID, newCrimeFactionID);
            }

            std::uint32_t fCount = 0;
            if (!serialization->ReadRecordData(fCount)) return false;
            if (fCount > kMaxFactionsPerSnapshot) {
                MARAS_LOG_ERROR("PlayerChildrenService::LoadFactionSnapshots - corrupt fCount {}", fCount);
                return false;
            }

            std::vector<FactionRankEntry> loadedFactions;
            loadedFactions.reserve(fCount);

            for (std::uint32_t j = 0; j < fCount; ++j) {
                RE::FormID oldFactionID = 0;
                std::int8_t rank = 0;
                if (!serialization->ReadRecordData(oldFactionID) ||
                    !serialization->ReadRecordData(rank)) {
                    return false;
                }
                RE::FormID newFactionID = 0;
                if (serialization->ResolveFormID(oldFactionID, newFactionID)) {
                    loadedFactions.push_back({ newFactionID, rank });
                }
            }

            if (validChild && newChildID != 0) {
                factionSnapshots_[newChildID] = ChildFactionSnapshot{ newCrimeFactionID, std::move(loadedFactions) };
            }
        }

        MARAS_LOG_INFO("Loaded {} child faction snapshots", factionSnapshots_.size());
        return true;
    }

    void PlayerChildrenService::RevertFactionSnapshots() {
        factionSnapshots_.clear();
        MARAS_LOG_INFO("Reverted child faction snapshots");
    }

    void PlayerChildrenService::ReassertAllOnLoad() {
        auto* keyword = FormCache::GetSingleton().GetHomeSandboxKeyword();
        for (const auto& [childID, state] : children_) {
            if (IsSyntheticChildID(childID)) continue;

            ApplyPackageForCurrentState(childID);

            if (state.homeMarkerFormID != 0) {
                Utils::SetLinkedRef(childID, state.homeMarkerFormID, keyword);
            }

            if (state.petFormID != 0) {
                SetPetOrCritterFactionsAndPackage(state.petFormID, childID);
            }

            if (state.critterFormID != 0) {
                SetPetOrCritterFactionsAndPackage(state.critterFormID, childID);
            }
        }
        MARAS_LOG_INFO("ReassertAllOnLoad: Reasserted package/LinkedRef state for {} children", children_.size());
    }

    nlohmann::json PlayerChildrenService::DumpJson() const {
        nlohmann::json j;
        j["childChestIntroduced"] = childChestIntroduced_;
        j["factionSnapshotsCount"] = factionSnapshots_.size();
        nlohmann::json list = nlohmann::json::array();
        for (const auto& [cID, state] : children_) {
            nlohmann::json c;
            c["childId"] = std::format("{:08X}", cID);
            c["name"] = ActorSvc::GetName(cID);
            c["currentHouseFormId"] = state.currentHouseFormID != 0 ? std::format("{:08X}", state.currentHouseFormID) : "";
            c["homeMarkerFormId"] = state.homeMarkerFormID != 0 ? std::format("{:08X}", state.homeMarkerFormID) : "";
            c["petFormId"] = state.petFormID != 0 ? std::format("{:08X}", state.petFormID) : "";
            c["petName"] = state.petFormID != 0 ? ActorSvc::GetName(state.petFormID) : "";
            c["critterFormId"] = state.critterFormID != 0 ? std::format("{:08X}", state.critterFormID) : "";
            c["critterName"] = state.critterFormID != 0 ? ActorSvc::GetName(state.critterFormID) : "";
            c["currentOrder"] = state.currentOrder;
            c["orderExpiryGameDay"] = state.orderExpiryGameDay;
            c["critterBanned"] = state.critterBanned;
            c["petBanned"] = state.petBanned;
            c["isNewlyAdopted"] = state.isNewlyAdopted;
            c["forcegreetPending"] = state.forcegreetPending;
            c["critterRetryTimestamp"] = state.critterRetryTimestamp;
            c["petRetryTimestamp"] = state.petRetryTimestamp;
            c["playedScene1"] = state.playedScene1;
            c["playedScene2"] = state.playedScene2;
            c["lastInteractionTimestamp"] = state.lastInteractionTimestamp;

            auto snapIt = factionSnapshots_.find(cID);
            if (snapIt != factionSnapshots_.end()) {
                c["hasFactionSnapshot"] = true;
                c["snapshotFactionCount"] = snapIt->second.factions.size();
                c["snapshotCrimeFaction"] = snapIt->second.crimeFactionID != 0 ? std::format("{:08X}", snapIt->second.crimeFactionID) : "";
            } else {
                c["hasFactionSnapshot"] = false;
            }

            list.push_back(c);
        }
        j["children"] = list;
        return j;
    }

}  // namespace MARAS
