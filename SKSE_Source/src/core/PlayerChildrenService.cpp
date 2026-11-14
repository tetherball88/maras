#include "core/PlayerChildrenService.h"

#include <RE/E/ExtraFactionChanges.h>

#include <unordered_set>

#include "core/AffectionService.h"
#include "core/FormCache.h"
#include "core/NPCIndex.h"
#include "core/NPCRelationshipManager.h"
#include "core/PackageOverrideService.h"
#include "core/PlayerHouseService.h"
#include "core/WorldStateLegacyAdapters.h"
#include "maras/state/WorldStore.h"
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
        if (HasFactionSnapshot(childID)) {
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

        auto record = LegacyAdapters::ToChildFactionSnapshotRecord(snapshot);
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& hh = ws.household.Write();
            const State::ChildId cid{ childID };
            if (!hh.AllChildFactionSnapshots().contains(cid)) {
                hh.UpsertChildFactionSnapshot(cid, std::move(record));
            }
        });
        MARAS_LOG_INFO("SnapshotChildFactions: Recorded snapshot for child {:08X} ({} factions, crime: {:08X})",
                       childID, snapshot.factions.size(), snapshot.crimeFactionID);
    }

    void PlayerChildrenService::ClearChildExternalFactions(RE::FormID childID) {
        if (childID == 0 || IsSyntheticChildID(childID)) return;
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(childID);
        if (!actor) return;

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& snaps = snap->household->AllChildFactionSnapshots();
        auto it = snaps.find(State::ChildId{ childID });
        if (it != snaps.end()) {
            for (const auto& entry : it->second.factions) {
                if (auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(entry.factionId.raw())) {
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

        std::optional<State::ChildFactionSnapshotRecord> foundSnap;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            const auto& snaps = snap->household->AllChildFactionSnapshots();
            if (auto it = snaps.find(State::ChildId{ childID }); it != snaps.end()) {
                foundSnap = it->second;
            }
        }

        if (foundSnap.has_value()) {
            for (const auto& entry : foundSnap->factions) {
                if (auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(entry.factionId.raw())) {
                    Utils::AddToFaction(actor, faction, entry.rank);
                }
            }
            if (!foundSnap->crimeFactionId.IsNull()) {
                if (auto* crimeFaction = RE::TESForm::LookupByID<RE::TESFaction>(foundSnap->crimeFactionId.raw())) {
                    Utils::SetCrimeFaction(actor, crimeFaction);
                }
            }
            MARAS_LOG_INFO("RestoreChildFactions: Restored {} snapshotted factions and crime faction {:08X} for child {:08X}",
                           foundSnap->factions.size(), foundSnap->crimeFactionId.raw(), childID);
            State::WorldStore::GetSingleton().Modify([childID](State::WorldState& ws) {
                ws.household.Write().EraseChildFactionSnapshot(State::ChildId{ childID });
            });
        } else {
            MARAS_LOG_DEBUG("RestoreChildFactions: No faction snapshot found for child {:08X}", childID);
        }
        actor->EvaluatePackage();
    }

    bool PlayerChildrenService::HasFactionSnapshot(RE::FormID childID) const {
        if (childID == 0) return false;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->household->AllChildFactionSnapshots().contains(State::ChildId{ childID });
    }

    void PlayerChildrenService::OnChildAddedToHousehold(RE::FormID childID, RE::FormID initialHouseFormID) {
        if (childID == 0) return;

        State::WorldStore::GetSingleton().Modify([childID](State::WorldState& ws) {
            auto& hh = ws.household.Write();
            const State::ChildId cid{ childID };
            State::ChildHouseholdRecord rec = hh.FindChildState(cid) ? *hh.FindChildState(cid) : State::ChildHouseholdRecord{};
            rec.childId = cid;
            rec.isNewlyAdopted = true;
            hh.UpsertChildState(std::move(rec));
        });

        if (!IsSyntheticChildID(childID)) {
            // 1. Snapshot original factions before modifying anything (if not already snapshotted)
            SnapshotChildFactions(childID);

            // 2. Clear external non-household factions so child strictly follows player family routines
            ClearChildExternalFactions(childID);

            // 3. Ensure household child factions
            auto& formCache = FormCache::GetSingleton();
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(childID)) {
                // Reinstate crime faction from snapshot if available
                RE::FormID crimeFactionId = 0;
                {
                    const auto snap = State::WorldStore::GetSingleton().Snapshot();
                    const auto& snaps = snap->household->AllChildFactionSnapshots();
                    if (auto itSnap = snaps.find(State::ChildId{ childID }); itSnap != snaps.end()) {
                        crimeFactionId = itSnap->second.crimeFactionId.raw();
                    }
                }
                if (crimeFactionId != 0) {
                    if (auto* crimeFaction = RE::TESForm::LookupByID<RE::TESFaction>(crimeFactionId)) {
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
            NPCRelationshipManager::GetSingleton().UpdateNonPlayerChildPackageOverride(childID);
            ApplyPackageForCurrentState(childID);
        }

        MARAS_LOG_INFO("OnChildAddedToHousehold: Registered child {:08X} in household (house: {:08X})", childID, initialHouseFormID);
    }

    void PlayerChildrenService::OnChildRemovedFromHousehold(RE::FormID childID) {
        if (childID == 0 || !State::WorldStore::GetSingleton().Snapshot()->household->FindChildState(State::ChildId{ childID })) {
            return;
        }

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

        State::WorldStore::GetSingleton().Modify([childID](State::WorldState& ws) {
            ws.household.Write().EraseChildState(State::ChildId{ childID });
        });
        MARAS_LOG_INFO("OnChildRemovedFromHousehold: Removed child {:08X} from household and restored factions", childID);
    }

    bool PlayerChildrenService::AssignChildToHouse(RE::FormID childID, RE::FormID playerHouseFormID, bool moveChild) {
        if (childID == 0 || playerHouseFormID == 0) return false;

        if (!PlayerHouseService::GetSingleton().RegisterTenantInPlayerHouse(childID, playerHouseFormID)) {
            MARAS_LOG_WARN("AssignChildToHouse: Failed to register child {:08X} as tenant in house {:08X}", childID, playerHouseFormID);
            return false;
        }

        State::WorldStore::GetSingleton().Modify([childID, playerHouseFormID](State::WorldState& ws) {
            auto& hh = ws.household.Write();
            const State::ChildId cid{ childID };
            State::ChildHouseholdRecord rec = hh.FindChildState(cid) ? *hh.FindChildState(cid) : State::ChildHouseholdRecord{};
            rec.childId = cid;
            rec.currentHouseId = State::HouseId{ playerHouseFormID };
            hh.UpsertChildState(std::move(rec));
        });

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

            NPCRelationshipManager::GetSingleton().UpdateNonPlayerChildPackageOverride(childID);
            ApplyPackageForCurrentState(childID);
        }

        MARAS_LOG_INFO("AssignChildToHouse: Child {:08X} assigned to house {:08X} (moveChild: {})", childID, playerHouseFormID, moveChild);
        return true;
    }

    RE::FormID PlayerChildrenService::GetChildHouseFormID(RE::FormID childID) const {
        if (childID == 0) return 0;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* st = snap->household->FindChildState(State::ChildId{ childID })) {
            if (!st->currentHouseId.IsNull()) {
                return st->currentHouseId.raw();
            }
        }
        return snap->household->GetTenantHouse(childID).raw();
    }

    void PlayerChildrenService::ClearChildHouseAssignment(RE::FormID childID) {
        if (childID == 0) return;
        RE::FormID prevHouse = 0;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID });
                st && !st->currentHouseId.IsNull()) {
                prevHouse = st->currentHouseId.raw();
                st->currentHouseId = State::HouseId{};
            }
        });
        if (prevHouse != 0) {
            MARAS_LOG_DEBUG("ClearChildHouseAssignment: Cleared house {:08X} from child {:08X}", prevHouse, childID);
        }
    }

    bool PlayerChildrenService::SetChildHomeMarker(RE::FormID childID, RE::FormID markerFormID) {
        if (childID == 0) return false;

        State::WorldStore::GetSingleton().Modify([childID, markerFormID](State::WorldState& ws) {
            auto& hh = ws.household.Write();
            const State::ChildId cid{ childID };
            State::ChildHouseholdRecord rec = hh.FindChildState(cid) ? *hh.FindChildState(cid) : State::ChildHouseholdRecord{};
            rec.childId = cid;
            rec.homeMarkerId = State::RefId{ markerFormID };
            hh.UpsertChildState(std::move(rec));
        });

        if (markerFormID != 0 && !IsSyntheticChildID(childID)) {
            auto* keyword = FormCache::GetSingleton().GetHomeSandboxKeyword();
            Utils::SetLinkedRef(childID, markerFormID, keyword);
        }

        MARAS_LOG_INFO("SetChildHomeMarker: Child {:08X} home marker set to {:08X}", childID, markerFormID);
        return true;
    }

    RE::FormID PlayerChildrenService::GetChildHomeMarker(RE::FormID childID) const {
        if (childID == 0) return 0;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        return st ? st->homeMarkerId.raw() : 0;
    }

    RE::TESObjectREFR* PlayerChildrenService::GetOrCreateChildHomeMarker(RE::Actor* childActor) {
        if (!childActor) return nullptr;

        const RE::FormID childID = childActor->GetFormID();
        const RE::FormID existingMarkerID = GetChildHomeMarker(childID);
        if (existingMarkerID != 0) {
            if (auto* existingMarker = RE::TESForm::LookupByID<RE::TESObjectREFR>(existingMarkerID)) {
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
        float currentOrder = 0.0f;
        bool forcegreetPending = false;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            const auto* st = snap->household->FindChildState(State::ChildId{ childID });
            if (!st) return;
            currentOrder = st->currentOrder;
            forcegreetPending = st->forcegreetPending;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(childID);
        if (!actor) return;

        // Sync Variable06 (Order) and Variable07 (Forcegreet) ActorValues
        if (auto* avOwner = actor->AsActorValueOwner()) {
            avOwner->SetActorValue(RE::ActorValue::kVariable06, currentOrder);
            avOwner->SetActorValue(RE::ActorValue::kVariable07, forcegreetPending ? 1.0f : 0.0f);
        }

        actor->EvaluatePackage();
        MARAS_LOG_DEBUG("ApplyPackageForCurrentState: Updated child {:08X} (order: {}, forcegreet: {})", childID, currentOrder, forcegreetPending);
    }

    void PlayerChildrenService::ResetForceGreeting(RE::FormID childID) {
        if (childID == 0) return;
        bool found = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                st->forcegreetPending = false;
                found = true;
            }
        });
        if (!found) return;

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
        if (childID == 0) return false;
        auto* calendar = RE::Calendar::GetSingleton();
        const float nowDay = calendar ? calendar->GetDaysPassed() : 0.0f;

        bool found = false;
        RE::FormID conflictChildID = 0;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& hh = ws.household.Write();
            const State::ChildId cid{ childID };
            auto* st = hh.FindChildStateMutable(cid);
            if (!st) return;
            found = true;

            // Same-order-lock (ported from vanilla IssueOrderWithDuration): void gift-triggered play
            // orders (dummy/doll) if another child in this household is already running that exact order.
            if (std::abs(orderValue - 1.1f) < 0.01f || std::abs(orderValue - 1.2f) < 0.01f) {
                for (const auto& [otherChildId, otherState] : hh.AllChildStates()) {
                    if (otherChildId == cid) continue;
                    if (otherState.currentHouseId != st->currentHouseId) continue;
                    if (std::abs(otherState.currentOrder - orderValue) < 0.01f) {
                        conflictChildID = otherChildId.raw();
                        return;
                    }
                }
            }

            st->currentOrder = orderValue;
            st->orderExpiryGameDay = (durationGameHours > 0.0f) ? (nowDay + (durationGameHours / 24.0f)) : -1.0f;
        });

        if (!found) return false;
        if (conflictChildID != 0) {
            MARAS_LOG_DEBUG("IssueOrder: Voided order {} for child {:08X}, already active on child {:08X} in the same household",
                            orderValue, childID, conflictChildID);
            return false;
        }

        ApplyPackageForCurrentState(childID);
        MARAS_LOG_INFO("IssueOrder: Issued order {} to child {:08X} for {} game hours", orderValue, childID, durationGameHours);
        return true;
    }

    void PlayerChildrenService::Tick(float currentGameDay) {
        struct ExpiredOrder {
            RE::FormID childID{ 0 };
            float orderValue{ 0.0f };
        };
        std::vector<ExpiredOrder> expired;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& hh = ws.household.Write();
            std::vector<State::ChildId> expiredIds;
            for (const auto& [cid, st] : hh.AllChildStates()) {
                if (st.orderExpiryGameDay >= 0.0f && currentGameDay >= st.orderExpiryGameDay) {
                    expiredIds.push_back(cid);
                }
            }
            for (const auto cid : expiredIds) {
                if (auto* st = hh.FindChildStateMutable(cid)) {
                    expired.push_back(ExpiredOrder{ cid.raw(), st->currentOrder });
                    st->currentOrder = 0.0f;
                    st->orderExpiryGameDay = -1.0f;
                }
            }
        });

        for (const auto& item : expired) {
            MARAS_LOG_INFO("Tick: Order {} expired for child {:08X} (game day {})", item.orderValue, item.childID, currentGameDay);
            ApplyPackageForCurrentState(item.childID);
        }
    }

    void PlayerChildrenService::RollForcegreetForHousehold() {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& allChildren = snap->household->AllChildStates();
        if (allChildren.empty()) return;

        // Track houses that already have an active forcegreet slot (ONE shared per-household slot)
        std::unordered_set<RE::FormID> housesWithActiveForcegreet;
        std::unordered_map<RE::FormID, std::vector<RE::FormID>> houseChildrenMap;

        for (const auto& [cid, state] : allChildren) {
            const RE::FormID childID = cid.raw();
            if (IsSyntheticChildID(childID)) continue;
            const RE::FormID houseID = !state.currentHouseId.IsNull()
                                           ? state.currentHouseId.raw()
                                           : snap->household->GetTenantHouse(childID).raw();
            if (houseID == 0) continue;

            if (state.forcegreetPending) {
                housesWithActiveForcegreet.insert(houseID);
            } else {
                houseChildrenMap[houseID].push_back(childID);
            }
        }

        std::vector<std::pair<RE::FormID, RE::FormID>> chosenByHouse;
        for (const auto& [houseID, list] : houseChildrenMap) {
            if (list.empty() || housesWithActiveForcegreet.find(houseID) != housesWithActiveForcegreet.end()) {
                continue;
            }
            RE::FormID chosenChildID = PickWeightedChild(list);
            if (chosenChildID != 0) {
                chosenByHouse.emplace_back(houseID, chosenChildID);
            }
        }

        if (chosenByHouse.empty()) return;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& hh = ws.household.Write();
            for (const auto& [houseID, chosenChildID] : chosenByHouse) {
                (void)houseID;
                if (auto* st = hh.FindChildStateMutable(State::ChildId{ chosenChildID })) {
                    st->forcegreetPending = true;
                }
            }
        });

        for (const auto& [houseID, chosenChildID] : chosenByHouse) {
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
        if (childID == 0) return false;
        bool found = false;
        RE::FormID oldPetID = 0;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                found = true;
                oldPetID = st->petId.raw();
                st->petId = State::RefId{ petActorOrBaseID };
            }
        });

        if (!found) return false;

        if (oldPetID != 0 && oldPetID != petActorOrBaseID) {
            ClearPetOrCritterFactions(oldPetID);
            if (GetChildForPetOrCritter(oldPetID) == 0) {
                PackageOverrideService::GetSingleton().UnregisterActor(oldPetID);
            }
        }

        SetPetOrCritterFactionsAndPackage(petActorOrBaseID, childID);

        MARAS_LOG_INFO("AssignPet: Assigned pet {:08X} to child {:08X}", petActorOrBaseID, childID);
        return true;
    }

    RE::FormID PlayerChildrenService::GetPet(RE::FormID childID) const {
        if (childID == 0) return 0;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        return st ? st->petId.raw() : 0;
    }

    bool PlayerChildrenService::RemovePet(RE::FormID childID) {
        if (childID == 0) return false;
        bool found = false;
        RE::FormID oldPetID = 0;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                found = true;
                oldPetID = st->petId.raw();
                st->petId = State::RefId{};
            }
        });

        if (!found) return false;

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
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [cid, state] : snap->household->AllChildStates()) {
            if (state.petId.raw() == petID) return cid.raw();
        }
        return 0;
    }


    void PlayerChildrenService::SetChildPetBanned(RE::FormID childID, bool banned) {
        if (childID == 0) return;
        bool updated = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                st->petBanned = banned;
                updated = true;
            }
        });
        if (updated) {
            MARAS_LOG_INFO("SetChildPetBanned: Set child {:08X} petBanned = {}", childID, banned);
        }
    }

    bool PlayerChildrenService::IsChildPetBanned(RE::FormID childID) const {
        if (childID == 0) return false;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        return st ? st->petBanned : false;
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
        bool updated = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                st->petRetryTimestamp = timestamp;
                updated = true;
            }
        });
        if (updated) {
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
        if (childID == 0) return 0.0f;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        return st ? st->petRetryTimestamp : 0.0f;
    }

    bool PlayerChildrenService::IsPetRetryAvailable(RE::FormID childID) const {
        if (childID == 0) return true;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        if (!st) return true;
        return IsCompanionRetryCooldownElapsed(st->petRetryTimestamp);
    }

    bool PlayerChildrenService::AssignCritter(RE::FormID childID, RE::FormID critterFormID) {
        if (childID == 0) return false;
        bool found = false;
        RE::FormID oldCritterID = 0;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                found = true;
                oldCritterID = st->critterId.raw();
                st->critterId = State::RefId{ critterFormID };
            }
        });

        if (!found) return false;

        if (oldCritterID != 0 && oldCritterID != critterFormID) {
            ClearPetOrCritterFactions(oldCritterID);
            if (GetChildForPetOrCritter(oldCritterID) == 0) {
                PackageOverrideService::GetSingleton().UnregisterActor(oldCritterID);
            }
        }

        SetPetOrCritterFactionsAndPackage(critterFormID, childID);

        MARAS_LOG_INFO("AssignCritter: Assigned critter {:08X} to child {:08X}", critterFormID, childID);
        return true;
    }

    RE::FormID PlayerChildrenService::GetCritter(RE::FormID childID) const {
        if (childID == 0) return 0;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        return st ? st->critterId.raw() : 0;
    }

    bool PlayerChildrenService::RemoveCritter(RE::FormID childID) {
        if (childID == 0) return false;
        bool found = false;
        RE::FormID oldCritterID = 0;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                found = true;
                oldCritterID = st->critterId.raw();
                st->critterId = State::RefId{};
            }
        });

        if (!found) return false;

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
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [cid, state] : snap->household->AllChildStates()) {
            if (state.critterId.raw() == critterID) return cid.raw();
        }
        return 0;
    }

    RE::FormID PlayerChildrenService::GetChildForPetOrCritter(RE::FormID petOrCritterID) const {
        if (petOrCritterID == 0) return 0;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [cid, state] : snap->household->AllChildStates()) {
            if (state.petId.raw() == petOrCritterID || state.critterId.raw() == petOrCritterID) {
                return cid.raw();
            }
        }
        return 0;
    }

    std::vector<RE::FormID> PlayerChildrenService::GetAllPetAndCritterIDs() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::vector<RE::FormID> result;
        for (const auto& [cid, state] : snap->household->AllChildStates()) {
            (void)cid;
            if (!state.petId.IsNull()) {
                result.push_back(state.petId.raw());
            }
            if (!state.critterId.IsNull()) {
                result.push_back(state.critterId.raw());
            }
        }
        return result;
    }

    void PlayerChildrenService::SetChildCritterBanned(RE::FormID childID, bool banned) {
        if (childID == 0) return;
        bool updated = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                st->critterBanned = banned;
                updated = true;
            }
        });
        if (updated) {
            MARAS_LOG_INFO("SetChildCritterBanned: Set child {:08X} critterBanned = {}", childID, banned);
        }
    }

    bool PlayerChildrenService::IsChildCritterBanned(RE::FormID childID) const {
        if (childID == 0) return false;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        return st ? st->critterBanned : false;
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
        bool updated = false;
        RE::FormID sameHouseID = 0;
        std::vector<RE::FormID> clearedChildIDs;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& hh = ws.household.Write();
            const State::ChildId cid{ childID };
            auto* st = hh.FindChildStateMutable(cid);
            if (!st) return;

            st->isNewlyAdopted = isNewlyAdopted;
            updated = true;

            if (clearSameHouse) {
                const State::HouseId houseId = !st->currentHouseId.IsNull()
                                                   ? st->currentHouseId
                                                   : hh.GetTenantHouse(childID);
                if (!houseId.IsNull()) {
                    sameHouseID = houseId.raw();
                    std::vector<State::ChildId> toClear;
                    for (const auto& [otherCid, otherState] : hh.AllChildStates()) {
                        if (otherCid == cid) continue;
                        if (otherState.currentHouseId == houseId ||
                            (otherState.currentHouseId.IsNull() && hh.GetTenantHouse(otherCid.raw()) == houseId)) {
                            toClear.push_back(otherCid);
                        }
                    }
                    for (const auto otherCid : toClear) {
                        if (auto* otherMut = hh.FindChildStateMutable(otherCid)) {
                            otherMut->isNewlyAdopted = false;
                            clearedChildIDs.push_back(otherCid.raw());
                        }
                    }
                }
            }
        });

        if (updated) {
            MARAS_LOG_INFO("SetIsNewlyAdopted: Set child {:08X} isNewlyAdopted = {}", childID, isNewlyAdopted);
            for (const RE::FormID otherID : clearedChildIDs) {
                MARAS_LOG_INFO("SetIsNewlyAdopted: Cleared isNewlyAdopted for child {:08X} in house {:08X}", otherID, sameHouseID);
            }
        }
    }

    bool PlayerChildrenService::IsNewlyAdopted(RE::FormID childID) const {
        if (childID == 0) return false;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        return st ? st->isNewlyAdopted : false;
    }

    void PlayerChildrenService::SetChildChestIntroduced(bool introduced) {
        State::WorldStore::GetSingleton().Modify([introduced](State::WorldState& ws) {
            ws.household.Write().SetChildChestIntroduced(introduced);
        });
        MARAS_LOG_INFO("SetChildChestIntroduced: Set global childChestIntroduced = {}", introduced);
    }

    bool PlayerChildrenService::IsChildChestIntroduced() const {
        return State::WorldStore::GetSingleton().Snapshot()->household->IsChildChestIntroduced();
    }

    void PlayerChildrenService::SetCritterRetryTimestamp(RE::FormID childID, float timestamp) {
        if (childID == 0) return;
        bool updated = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                st->critterRetryTimestamp = timestamp;
                updated = true;
            }
        });
        if (updated) {
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
        if (childID == 0) return 0.0f;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        return st ? st->critterRetryTimestamp : 0.0f;
    }

    bool PlayerChildrenService::IsCritterRetryAvailable(RE::FormID childID) const {
        if (childID == 0) return true;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        if (!st) return true;
        return IsCompanionRetryCooldownElapsed(st->critterRetryTimestamp);
    }

    std::optional<ChildHouseholdState> PlayerChildrenService::GetState(RE::FormID childID) const {
        if (childID == 0) return std::nullopt;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        if (!st) return std::nullopt;
        return LegacyAdapters::ToLegacyChildHouseholdState(*st);
    }

    std::vector<RE::FormID> PlayerChildrenService::GetAllPlayerChildren(bool onlyRealActors) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& allChildren = snap->household->AllChildStates();
        std::vector<RE::FormID> list;
        list.reserve(allChildren.size());
        for (const auto& [cid, state] : allChildren) {
            (void)state;
            const RE::FormID cID = cid.raw();
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
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [cid, state] : snap->household->AllChildStates()) {
            const RE::FormID cID = cid.raw();
            MARAS_LOG_DEBUG("GetChildrenForHouse: Child {:08X} has house {:08X}", cID, state.currentHouseId.raw());
            if (state.currentHouseId.raw() == playerHouseFormID) {
                if (!onlyRealActors || !IsSyntheticChildID(cID)) {
                    list.push_back(cID);
                }
            }
        }
        return list;
    }

    void PlayerChildrenService::SetChildPlayedScene(RE::FormID childID, int32_t sceneID, bool played) {
        if (childID == 0) return;
        bool updated = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                if (sceneID == 1) st->playedScene1 = played;
                else if (sceneID == 2) st->playedScene2 = played;
                updated = true;
            }
        });
        if (updated) {
            MARAS_LOG_INFO("SetChildPlayedScene: child {:08X} scene {} = {}", childID, sceneID, played);
        }
    }

    bool PlayerChildrenService::HasChildPlayedScene(RE::FormID childID, int32_t sceneID) const {
        if (childID == 0) return false;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        if (!st) return false;
        if (sceneID == 1) return st->playedScene1;
        if (sceneID == 2) return st->playedScene2;
        return false;
    }

    void PlayerChildrenService::RecordChildInteraction(RE::FormID childID) {
        if (childID == 0) return;
        float currentDays = 0.0f;
        if (auto calendar = RE::Calendar::GetSingleton()) {
            currentDays = calendar->GetDaysPassed();
        }

        bool found = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                st->lastInteractionTimestamp = currentDays;
                found = true;
            }
        });

        if (!found) return;

        MARAS_LOG_INFO("RecordChildInteraction: child {:08X} timestamp = {:.2f}", childID, currentDays);

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* playerCell = player ? player->GetParentCell() : nullptr;
        if (playerCell) {
            auto& relMgr = NPCRelationshipManager::GetSingleton();
            auto entryOpt = relMgr.GetChildEntry(childID);
            if (entryOpt.has_value()) {
                const auto& entry = *entryOpt;
                std::unordered_set<RE::FormID> coParentIDs;
                auto addCandidate = [&](RE::FormID id) {
                    if (id != 0 && id != kPlayerFormID) {
                        coParentIDs.insert(id);
                    }
                };
                addCandidate(entry.guardianAID);
                addCandidate(entry.guardianBID);
                addCandidate(entry.bioMotherID);
                if (entry.paternitySecret.isSecretForPartner) {
                    addCandidate(entry.presumedFatherID != 0 ? entry.presumedFatherID : relMgr.GetMotherPartner(childID));
                }
                if (!entry.paternitySecret.isSecretForBioFather) {
                    addCandidate(entry.bioFatherID);
                }

                for (RE::FormID partnerID : coParentIDs) {
                    auto status = relMgr.GetRelationshipStatus(partnerID, kPlayerFormID);
                    if (!NPCRelationshipManager::IsActivePartnerStatus(status)) {
                        continue;
                    }
                    auto* partnerActor = RE::TESForm::LookupByID<RE::Actor>(partnerID);
                    if (!partnerActor || partnerActor->GetParentCell() != playerCell) {
                        continue;
                    }

                    float affection = 2.0f;
                    auto temperament = relMgr.GetTemperament(partnerID);
                    if (temperament == Temperament::Romantic || temperament == Temperament::Humble) {
                        affection = 3.0f;
                    } else if (temperament == Temperament::Independent) {
                        affection = 1.0f;
                    }

                    AffectionService::GetSingleton().AddAffection(partnerID, affection, "family");
                }
            }
        }
    }

    float PlayerChildrenService::GetLastInteractionTimestamp(RE::FormID childID) const {
        if (childID == 0) return 0.0f;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        return st ? st->lastInteractionTimestamp : 0.0f;
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
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [cid, state] : snap->household->AllChildStates()) {
            const RE::FormID cID = cid.raw();
            if (state.currentHouseId.raw() == playerHouseFormID && state.isNewlyAdopted && !IsSyntheticChildID(cID)) {
                candidates.push_back(cID);
            }
        }
        return PickWeightedChild(candidates);
    }

    RE::FormID PlayerChildrenService::GetChildWithoutPet(RE::FormID playerHouseFormID) const {
        std::vector<RE::FormID> candidates;
        if (playerHouseFormID == 0) return 0;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [cid, state] : snap->household->AllChildStates()) {
            const RE::FormID cID = cid.raw();
            if (state.currentHouseId.raw() == playerHouseFormID && state.petId.IsNull() && !IsPetBanned(cID) && !IsSyntheticChildID(cID)) {
                candidates.push_back(cID);
            }
        }
        return PickWeightedChild(candidates);
    }

    RE::FormID PlayerChildrenService::GetChildWithoutCritter(RE::FormID playerHouseFormID) const {
        std::vector<RE::FormID> candidates;
        if (playerHouseFormID == 0) return 0;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [cid, state] : snap->household->AllChildStates()) {
            const RE::FormID cID = cid.raw();
            if (state.currentHouseId.raw() == playerHouseFormID && state.critterId.IsNull() && !IsCritterBanned(cID) && !IsSyntheticChildID(cID)) {
                candidates.push_back(cID);
            }
        }
        return PickWeightedChild(candidates);
    }

    std::vector<RE::FormID> PlayerChildrenService::GetChildrenWithoutScene(RE::FormID playerHouseFormID, int32_t sceneID) const {
        std::vector<RE::FormID> candidates;
        if (playerHouseFormID == 0) return candidates;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [cid, state] : snap->household->AllChildStates()) {
            const RE::FormID cID = cid.raw();
            const bool played = (sceneID == 1) ? state.playedScene1 : ((sceneID == 2) ? state.playedScene2 : false);
            if (state.currentHouseId.raw() == playerHouseFormID && !played && !IsSyntheticChildID(cID)) {
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

    void PlayerChildrenService::Revert() {
        MARAS_LOG_INFO("Reverted PlayerChildrenService data");
    }

    void PlayerChildrenService::ReassertAllOnLoad() {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& childStates = snap->household->AllChildStates();
        auto* keyword = FormCache::GetSingleton().GetHomeSandboxKeyword();
        for (const auto& [childKey, rec] : childStates) {
            const RE::FormID childID = childKey.raw();
            if (IsSyntheticChildID(childID)) continue;

            ApplyPackageForCurrentState(childID);

            if (rec.homeMarkerId.raw() != 0) {
                Utils::SetLinkedRef(childID, rec.homeMarkerId.raw(), keyword);
            }

            if (rec.petId.raw() != 0) {
                SetPetOrCritterFactionsAndPackage(rec.petId.raw(), childID);
            }

            if (rec.critterId.raw() != 0) {
                SetPetOrCritterFactionsAndPackage(rec.critterId.raw(), childID);
            }
        }
        MARAS_LOG_INFO("ReassertAllOnLoad: Reasserted package/LinkedRef state for {} children", childStates.size());
    }

    nlohmann::json PlayerChildrenService::DumpJson() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& childStates = snap->household->AllChildStates();
        const auto& factionSnapshots = snap->household->AllChildFactionSnapshots();

        nlohmann::json j;
        j["childChestIntroduced"] = snap->household->IsChildChestIntroduced();
        j["factionSnapshotsCount"] = factionSnapshots.size();
        nlohmann::json list = nlohmann::json::array();
        for (const auto& [cKey, rec] : childStates) {
            const RE::FormID cID = cKey.raw();
            const ChildHouseholdState state = LegacyAdapters::ToLegacyChildHouseholdState(rec);
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

            auto snapIt = factionSnapshots.find(cKey);
            if (snapIt != factionSnapshots.end()) {
                const auto legacySnap = LegacyAdapters::ToLegacyChildFactionSnapshot(snapIt->second);
                c["hasFactionSnapshot"] = true;
                c["snapshotFactionCount"] = legacySnap.factions.size();
                c["snapshotCrimeFaction"] = legacySnap.crimeFactionID != 0 ? std::format("{:08X}", legacySnap.crimeFactionID) : "";
            } else {
                c["hasFactionSnapshot"] = false;
            }

            list.push_back(c);
        }
        j["children"] = list;
        return j;
    }

}  // namespace MARAS
