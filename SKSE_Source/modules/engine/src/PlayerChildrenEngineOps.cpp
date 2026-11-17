#include "maras/engine/PlayerChildrenEngineOps.h"

#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/DialogueEventSink.h"
#include "maras/engine/FactionReconciler.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/HouseholdEngineOps.h"
#include "maras/engine/PackageReconciler.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/state/WorldStore.h"

#include <RE/E/ExtraFactionChanges.h>
#include <optional>
#include <unordered_map>
#include <vector>

namespace MARAS {

    namespace {

        [[nodiscard]] bool IsPlayerHouseholdFaction(RE::TESFaction* faction) {
            if (!faction) {
                return false;
            }
            auto& fc = FormCache::GetSingleton();
            return faction == fc.GetPlayerChildFaction() ||
                   faction == fc.GetBYOHRelationshipAdoptionChildOwnedFaction() ||
                   faction == fc.GetBYOHRelationshipAdoptionFaction() ||
                   faction == fc.GetPlayerFaction() ||
                   faction == fc.GetBYOHRelationshipAdoptableFaction() ||
                   faction == fc.GetPlayerChildBothParentsFaction();
        }

    }  // namespace

    PlayerChildrenEngineOps& PlayerChildrenEngineOps::GetSingleton() {
        static PlayerChildrenEngineOps instance;
        return instance;
    }

    bool PlayerChildrenEngineOps::SetChildHomeMarkerLinkedRef(RE::FormID childId, RE::FormID markerFormId) const {
        if (!HasPhysicalActor(childId) || markerFormId == 0) {
            return false;
        }
        auto* keyword = FormCache::GetSingleton().GetHomeSandboxKeyword();
        return Utils::SetLinkedRef(childId, markerFormId, keyword);
    }

    RE::TESObjectREFR* PlayerChildrenEngineOps::GetOrCreateChildHomeMarker(RE::Actor* childActor) {
        if (!childActor) {
            return nullptr;
        }

        const RE::FormID childId = childActor->GetFormID();
        if (!HasPhysicalActor(childId)) {
            return nullptr;
        }

        RE::FormID existingMarkerId = 0;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            if (const auto* st = snap->household->FindChildState(State::ChildId{ childId })) {
                existingMarkerId = st->homeMarkerId.raw();
            }
        }

        if (existingMarkerId != 0) {
            if (auto* existingMarker = RE::TESForm::LookupByID<RE::TESObjectREFR>(existingMarkerId)) {
                (void)SetChildHomeMarkerLinkedRef(childId, existingMarkerId);
                return existingMarker;
            }
        }

        auto* homeMarkerStatic = FormCache::GetSingleton().GetHomeSandboxMarkerStatic();
        if (!homeMarkerStatic) {
            MARAS_LOG_WARN("PlayerChildrenEngineOps::GetOrCreateChildHomeMarker: homeMarkerStatic form not found");
            return nullptr;
        }

        auto markerRef = childActor->PlaceObjectAtMe(homeMarkerStatic, true);
        if (!markerRef) {
            MARAS_LOG_WARN("PlayerChildrenEngineOps::GetOrCreateChildHomeMarker: PlaceObjectAtMe failed for child {:08X}",
                           childId);
            return nullptr;
        }

        const RE::FormID markerId = markerRef->GetFormID();
        State::WorldStore::GetSingleton().Modify([childId, markerId](State::WorldState& ws) {
            auto& hh = ws.household.Write();
            const State::ChildId cid{ childId };
            State::ChildHouseholdRecord rec =
                hh.FindChildState(cid) ? *hh.FindChildState(cid) : State::ChildHouseholdRecord{};
            rec.childId = cid;
            rec.homeMarkerId = State::RefId{ markerId };
            hh.UpsertChildState(std::move(rec));
        });

        (void)SetChildHomeMarkerLinkedRef(childId, markerId);
        MARAS_LOG_INFO("PlayerChildrenEngineOps::GetOrCreateChildHomeMarker: placed home marker {:08X} for child {:08X}",
                       markerId, childId);
        return markerRef.get();
    }

    bool PlayerChildrenEngineOps::MoveChildAndMarkerToHouse(RE::FormID childId, RE::FormID houseId, bool moveChild) const {
        if (!HasPhysicalActor(childId) || houseId == 0) {
            return false;
        }
        auto* childActor = RE::TESForm::LookupByID<RE::Actor>(childId);
        if (!childActor) {
            return false;
        }
        auto* houseMarker = HouseholdEngineOps::GetSingleton().GetHouseMarkerRef(houseId);
        if (!houseMarker) {
            return false;
        }
        auto* childMarker = GetSingleton().GetOrCreateChildHomeMarker(childActor);
        if (!childMarker) {
            return false;
        }
        childMarker->MoveTo(houseMarker);
        MARAS_LOG_DEBUG("PlayerChildrenEngineOps::MoveChildAndMarkerToHouse: moved child {:08X} marker {:08X} to house {:08X} marker {:08X}",
                        childId, childMarker->GetFormID(), houseId, houseMarker->GetFormID());
        if (moveChild) {
            childActor->MoveTo(childMarker);
            childActor->EvaluatePackage();
            MARAS_LOG_DEBUG("PlayerChildrenEngineOps::MoveChildAndMarkerToHouse: moved child {:08X} to marker {:08X}",
                            childId, childMarker->GetFormID());
        }
        return true;
    }

    void PlayerChildrenEngineOps::SnapshotChildFactions(RE::FormID childId) {
        if (!HasPhysicalActor(childId)) {
            return;
        }
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            if (snap->household->AllChildFactionSnapshots().contains(State::ChildId{ childId })) {
                MARAS_LOG_DEBUG("PlayerChildrenEngineOps::SnapshotChildFactions: child {:08X} already has snapshot, skipping",
                                childId);
                return;
            }
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(childId);
        if (!actor) {
            MARAS_LOG_WARN("PlayerChildrenEngineOps::SnapshotChildFactions: child {:08X} not found as Actor", childId);
            return;
        }

        State::ChildFactionSnapshotRecord snapshot{};
        if (auto* cf = actor->GetCrimeFaction()) {
            snapshot.crimeFactionId = State::FormRawId{ cf->GetFormID() };
        } else if (auto* base = actor->GetActorBase(); base && base->crimeFaction) {
            snapshot.crimeFactionId = State::FormRawId{ base->crimeFaction->GetFormID() };
        }

        std::unordered_map<RE::TESFaction*, std::int8_t> effectiveFactions;
        if (auto* base = actor->GetActorBase()) {
            for (const auto& fData : base->factions) {
                if (fData.faction && !IsPlayerHouseholdFaction(fData.faction)) {
                    effectiveFactions[fData.faction] = fData.rank;
                }
            }
        }

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

        for (const auto& [faction, rank] : effectiveFactions) {
            if (faction && rank >= 0) {
                snapshot.factions.push_back(State::FactionRankSnapshotEntry{
                    State::FormRawId{ faction->GetFormID() }, rank });
            }
        }

        const std::size_t factionCount = snapshot.factions.size();
        const RE::FormID crimeId = snapshot.crimeFactionId.raw();

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& hh = ws.household.Write();
            const State::ChildId cid{ childId };
            if (!hh.AllChildFactionSnapshots().contains(cid)) {
                hh.UpsertChildFactionSnapshot(cid, std::move(snapshot));
            }
        });

        MARAS_LOG_INFO("PlayerChildrenEngineOps::SnapshotChildFactions: recorded snapshot for child {:08X} ({} factions, crime: {:08X})",
                       childId, factionCount, crimeId);
    }

    void PlayerChildrenEngineOps::ClearChildExternalFactions(RE::FormID childId) {
        if (!HasPhysicalActor(childId)) {
            return;
        }
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(childId);
        if (!actor) {
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& snaps = snap->household->AllChildFactionSnapshots();
        const auto it = snaps.find(State::ChildId{ childId });
        if (it != snaps.end()) {
            for (const auto& entry : it->second.factions) {
                if (auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(entry.factionId.raw())) {
                    Utils::RemoveFromFaction(actor, faction);
                }
            }
            MARAS_LOG_INFO("PlayerChildrenEngineOps::ClearChildExternalFactions: cleared {} external factions from child {:08X}",
                           it->second.factions.size(), childId);
        }
    }

    void PlayerChildrenEngineOps::RestoreChildFactions(
        RE::FormID childId,
        const State::ChildFactionSnapshotRecord* fallbackSnapshot) {
        if (!HasPhysicalActor(childId)) {
            return;
        }
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(childId);
        if (!actor) {
            return;
        }

        std::optional<State::ChildFactionSnapshotRecord> foundSnap;
        bool wasInStore = false;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            const auto& snaps = snap->household->AllChildFactionSnapshots();
            if (const auto it = snaps.find(State::ChildId{ childId }); it != snaps.end()) {
                foundSnap = it->second;
                wasInStore = true;
            } else if (fallbackSnapshot != nullptr) {
                foundSnap = *fallbackSnapshot;
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
            MARAS_LOG_INFO("PlayerChildrenEngineOps::RestoreChildFactions: restored {} factions and crime {:08X} for child {:08X}",
                           foundSnap->factions.size(), foundSnap->crimeFactionId.raw(), childId);
            if (wasInStore) {
                State::WorldStore::GetSingleton().Modify([childId](State::WorldState& ws) {
                    ws.household.Write().EraseChildFactionSnapshot(State::ChildId{ childId });
                });
            }
        } else {
            MARAS_LOG_DEBUG("PlayerChildrenEngineOps::RestoreChildFactions: no faction snapshot found for child {:08X}",
                            childId);
        }

        actor->EvaluatePackage();
    }

    void PlayerChildrenEngineOps::ApplyOrderAndForcegreetOnActor(
        RE::FormID childId,
        float currentOrder,
        bool forcegreetPending) const {
        if (!HasPhysicalActor(childId)) {
            return;
        }
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(childId);
        if (!actor) {
            return;
        }

        if (auto* avOwner = actor->AsActorValueOwner()) {
            avOwner->SetActorValue(RE::ActorValue::kVariable06, currentOrder);
            avOwner->SetActorValue(RE::ActorValue::kVariable07, forcegreetPending ? 1.0f : 0.0f);
        }
        actor->EvaluatePackage();
        MARAS_LOG_DEBUG("PlayerChildrenEngineOps::ApplyOrderAndForcegreetOnActor: updated child {:08X} (order={}, forcegreet={})",
                        childId, currentOrder, forcegreetPending);
    }

    void PlayerChildrenEngineOps::ApplyOrderOnActorFromWorld(RE::FormID childId) const {
        if (!HasPhysicalActor(childId)) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childId });
        if (!st) {
            return;
        }
        ApplyOrderAndForcegreetOnActor(childId, st->currentOrder, st->forcegreetPending);
    }

    void PlayerChildrenEngineOps::ResetForceGreetingOnActor(RE::FormID childId) const {
        if (!HasPhysicalActor(childId)) {
            return;
        }
        if (!DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            AddCurrentEpochTask("PlayerChildrenEngineOps::ResetForceGreetingOnActor", [childId]() {
                std::lock_guard lock(GetPapyrusMutex());
                PlayerChildrenEngineOps::GetSingleton().ResetForceGreetingOnActor(childId);
            });
            return;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(childId);
        if (!actor || actor->IsDeleted()) {
            return;
        }
        if (auto* avOwner = actor->AsActorValueOwner()) {
            avOwner->SetActorValue(RE::ActorValue::kVariable07, 0.0f);
        }

        auto* ui = RE::UI::GetSingleton();
        const bool dialogueOpen = ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
        if (dialogueOpen) {
            DialogueEventSink::GetSingleton()->DeferPackageEvalUntilDialogueClosed(childId);
            MARAS_LOG_INFO(
                "PlayerChildrenEngineOps::ResetForceGreetingOnActor: reset Variable07 for child {:08X} ({}); deferring EvaluatePackage until Dialogue Menu closes",
                childId,
                actor->GetName());
        } else {
            actor->EvaluatePackage();
            MARAS_LOG_INFO(
                "PlayerChildrenEngineOps::ResetForceGreetingOnActor: reset Variable07 and evaluated package for child {:08X} ({})",
                childId,
                actor->GetName());
        }
    }

    void PlayerChildrenEngineOps::ReassertChildPackagesOnLoad() {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& childStates = snap->household->AllChildStates();
        std::vector<RE::FormID> toReconcile;
        toReconcile.reserve(childStates.size() * 3);

        for (const auto& [childKey, rec] : childStates) {
            const RE::FormID childId = childKey.raw();
            if (!HasPhysicalActor(childId)) {
                continue;
            }

            ApplyOrderAndForcegreetOnActor(childId, rec.currentOrder, rec.forcegreetPending);
            toReconcile.push_back(childId);

            if (!rec.petId.IsNull()) {
                toReconcile.push_back(rec.petId.raw());
            }
            if (!rec.critterId.IsNull()) {
                toReconcile.push_back(rec.critterId.raw());
            }
        }

        if (!toReconcile.empty()) {
            FactionReconciler::GetSingleton().ReconcileActors(toReconcile);
        }
        PackageReconciler::GetSingleton().ReconcileWorld();
        MARAS_LOG_INFO("PlayerChildrenEngineOps::ReassertChildPackagesOnLoad: reasserted state for {} children",
                       childStates.size());
    }

    void PlayerChildrenEngineOps::ClearAdoptableFactionOnRemove(RE::FormID childId) const {
        if (!HasPhysicalActor(childId) || (childId >> 24) != 0xFFu) {
            return;
        }

        auto clearAdoptable = [childId]() {
            if (auto* adoptableFaction = FormCache::GetSingleton().GetBYOHRelationshipAdoptableFaction()) {
                FactionReconciler::InternalFactionScope scope;
                Utils::RemoveFromFaction(childId, adoptableFaction);
            }
        };

        if (DeferredActorResolver::GetSingleton().IsOnMainThread()) {
            clearAdoptable();
        } else {
            AddCurrentEpochTask("PlayerChildrenEngineOps::ClearAdoptableOnRemove", [clearAdoptable]() {
                std::lock_guard lock(GetPapyrusMutex());
                clearAdoptable();
            });
        }
    }

    void PlayerChildrenEngineOps::SendChildChestEvent(RE::FormID childOrHouseId, bool introduced) const {
        EmitModCallbackEvent("maras_child_chest_event", introduced ? "introduced" : "reset",
                             introduced ? 1.0f : 0.0f, childOrHouseId);
    }

}  // namespace MARAS
