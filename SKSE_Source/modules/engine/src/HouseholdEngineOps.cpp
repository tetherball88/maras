#include "maras/engine/HouseholdEngineOps.h"

#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/EngineEventHooks.h"
#include "maras/engine/FactionReconciler.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/GlobalVarReconciler.h"
#include "maras/engine/PackageReconciler.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/state/WorldStore.h"

#include <cstddef>

namespace MARAS {

    HouseholdEngineOps& HouseholdEngineOps::GetSingleton() {
        static HouseholdEngineOps instance;
        return instance;
    }

    std::string HouseholdEngineOps::GetHouseName(RE::FormID houseFormId) {
        if (houseFormId == 0) {
            return {};
        }
        if (auto* form = RE::TESForm::LookupByID(houseFormId)) {
            const char* name = form->GetName();
            if (name && name[0] != '\0') {
                return name;
            }
        }
        return {};
    }

    RE::FormID HouseholdEngineOps::FindHouseByName(std::string_view name) const {
        if (name.empty()) {
            return 0;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& h : snap->household->AllHouses()) {
            const RE::FormID hId = h.houseId.raw();
            if (GetHouseName(hId) == name) {
                return hId;
            }
        }
        return 0;
    }

    RE::FormID HouseholdEngineOps::GetHouseMarkerFormID(RE::FormID houseFormId) const noexcept {
        if (houseFormId == 0) {
            return 0;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* h = snap->household->FindHouse(State::HouseId{ houseFormId });
        return h ? h->markerId.raw() : 0;
    }

    RE::TESObjectREFR* HouseholdEngineOps::GetHouseMarkerRef(RE::FormID houseFormId) const {
        const RE::FormID markerId = GetHouseMarkerFormID(houseFormId);
        return markerId != 0 ? RE::TESForm::LookupByID<RE::TESObjectREFR>(markerId) : nullptr;
    }

    void HouseholdEngineOps::SetLinkedRefForHomeMarker(RE::FormID npcFormId, RE::FormID markerFormId) const {
        if (!HasPhysicalActor(npcFormId) || markerFormId == 0) {
            return;
        }
        auto* homeSandboxKeyword = FormCache::GetSingleton().GetHomeSandboxKeyword();
        if (!Utils::SetLinkedRef(npcFormId, markerFormId, homeSandboxKeyword)) {
            MARAS_LOG_WARN("HouseholdEngineOps::SetLinkedRefForHomeMarker: failed for NPC {:08X} (marker={:08X}, keywordPresent={})",
                           npcFormId, markerFormId, homeSandboxKeyword != nullptr);
        }
    }

    void HouseholdEngineOps::ClearHomeMarkerLinkedRef(RE::FormID npcFormId) const {
        if (!HasPhysicalActor(npcFormId)) {
            return;
        }
        if (auto* homeSandboxKeyword = FormCache::GetSingleton().GetHomeSandboxKeyword()) {
            Utils::RemoveLinkedRef(npcFormId, homeSandboxKeyword);
        }
    }

    RE::TESObjectREFR* HouseholdEngineOps::GetOrCreateHomeMarker(RE::Actor* npcActor) {
        if (!npcActor) {
            return nullptr;
        }

        const RE::FormID npcFormId = npcActor->GetFormID();
        if (!HasPhysicalActor(npcFormId)) {
            return nullptr;
        }

        RE::FormID existingMarkerId = 0;
        bool hasProfile = false;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            if (const auto* prof = snap->actors->Find(State::RefId{ npcFormId })) {
                hasProfile = true;
                if (prof->legacyHomeMarker.has_value()) {
                    existingMarkerId = prof->legacyHomeMarker->raw();
                }
            }
        }

        if (existingMarkerId != 0) {
            if (auto* existingMarker = RE::TESForm::LookupByID<RE::TESObjectREFR>(existingMarkerId)) {
                SetLinkedRefForHomeMarker(npcFormId, existingMarkerId);
                return existingMarker;
            }
        }

        auto* homeMarkerStatic = FormCache::GetSingleton().GetHomeSandboxMarkerStatic();
        if (!homeMarkerStatic) {
            MARAS_LOG_WARN("HouseholdEngineOps::GetOrCreateHomeMarker: homeMarkerStatic form not found");
            return nullptr;
        }

        auto markerRef = npcActor->PlaceObjectAtMe(homeMarkerStatic, true);
        if (!markerRef) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                markerRef = player->PlaceObjectAtMe(homeMarkerStatic, true);
            }
        }
        if (!markerRef) {
            MARAS_LOG_WARN("HouseholdEngineOps::GetOrCreateHomeMarker: PlaceObjectAtMe failed for NPC {:08X}",
                           npcFormId);
            return nullptr;
        }

        const RE::FormID markerId = markerRef->GetFormID();
        if (!hasProfile) {
            if (auto* hooks = Engine::GetEngineEventHooks()) {
                hooks->EnsureActorRegistered(npcFormId);
            }
        }

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& actors = ws.actors.Write();
            if (auto* prof = actors.FindMutable(State::RefId{ npcFormId })) {
                prof->legacyHomeMarker = State::RefId{ markerId };
            } else {
                MARAS_LOG_WARN(
                    "HouseholdEngineOps::GetOrCreateHomeMarker: actor {:08X} lacks ActorProfileRecord; skipping partial upsert",
                    npcFormId);
            }
        });

        SetLinkedRefForHomeMarker(npcFormId, markerId);
        MARAS_LOG_INFO("HouseholdEngineOps::GetOrCreateHomeMarker: placed new home marker {:08X} for NPC {:08X}",
                       markerId, npcFormId);
        return markerRef.get();
    }

    bool HouseholdEngineOps::UpdateSpouseHomeMarker(RE::FormID spouseFormId, RE::FormID houseFormId) {
        if (!HasPhysicalActor(spouseFormId) || houseFormId == 0) {
            return false;
        }

        auto* spouseActor = RE::TESForm::LookupByID<RE::Actor>(spouseFormId);
        if (!spouseActor) {
            MARAS_LOG_WARN("HouseholdEngineOps::UpdateSpouseHomeMarker: actor {:08X} not found", spouseFormId);
            return false;
        }

        auto* houseMarker = GetHouseMarkerRef(houseFormId);
        if (!houseMarker) {
            MARAS_LOG_WARN(
                "HouseholdEngineOps::UpdateSpouseHomeMarker: no house marker for house {:08X}; spouse {:08X} deferred",
                houseFormId, spouseFormId);
            return false;
        }

        auto* spouseMarker = GetOrCreateHomeMarker(spouseActor);
        if (!spouseMarker) {
            MARAS_LOG_WARN("HouseholdEngineOps::UpdateSpouseHomeMarker: failed to get or create marker for spouse {:08X}",
                           spouseFormId);
            return false;
        }

        spouseMarker->MoveTo(houseMarker);
        MARAS_LOG_INFO(
            "HouseholdEngineOps::UpdateSpouseHomeMarker: moved spouse {:08X} marker {:08X} to house marker {:08X} (house {:08X})",
            spouseFormId, spouseMarker->GetFormID(), houseMarker->GetFormID(), houseFormId);
        return true;
    }

    bool HouseholdEngineOps::MoveActorToMarker(
        RE::Actor* actor,
        RE::TESObjectREFR* targetMarker,
        bool evaluatePackage) const {
        if (!actor || !targetMarker) {
            return false;
        }
        actor->MoveTo(targetMarker);
        if (evaluatePackage) {
            RefreshActorAfterMove(actor);
        }
        return true;
    }

    bool HouseholdEngineOps::MoveActorToHouse(
        RE::FormID actorId,
        RE::FormID houseId,
        bool evaluatePackage) {
        if (!HasPhysicalActor(actorId) || houseId == 0) {
            return false;
        }
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorId);
        if (!actor) {
            return false;
        }
        if (!UpdateSpouseHomeMarker(actorId, houseId)) {
            auto* houseMarker = GetHouseMarkerRef(houseId);
            return MoveActorToMarker(actor, houseMarker, evaluatePackage);
        }
        auto* spouseMarker = GetOrCreateHomeMarker(actor);
        auto* dest = spouseMarker ? spouseMarker : GetHouseMarkerRef(houseId);
        return MoveActorToMarker(actor, dest, evaluatePackage);
    }

    void HouseholdEngineOps::RefreshActorAfterMove(RE::Actor* actor) const {
        if (!actor || actor->IsDeleted()) {
            return;
        }
        actor->EvaluatePackage(false, true);
    }

    void HouseholdEngineOps::ReconcileTenantsOnLoad() {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& hh = *snap->household;

        GlobalVarReconciler::GetSingleton().ReconcileGlobals();

        auto& idx = SpatialWorldIndex::GetSingleton();
        std::size_t syncedCount = 0;

        for (const auto& [tenantId, assignment] : hh.AllTenants()) {
            if (tenantId == 0 || tenantId == kPlayerFormID || IsSyntheticChildID(tenantId)) {
                continue;
            }
            if (snap->lineage->FindChild(State::ChildId{ tenantId }) != nullptr) {
                continue;
            }

            auto* actor = RE::TESForm::LookupByID<RE::Actor>(tenantId);
            if (!actor) {
                actor = idx.GetActorFromFormId(tenantId);
            }
            if (actor && !actor->IsDead()) {
                if (!assignment.houseId.IsNull()) {
                    UpdateSpouseHomeMarker(tenantId, assignment.houseId.raw());
                }
                FactionReconciler::GetSingleton().ReconcileLoadedActor(actor);
                ++syncedCount;
            }
            PackageReconciler::GetSingleton().ReconcileActor(tenantId);
        }

        MARAS_LOG_INFO("HouseholdEngineOps::ReconcileTenantsOnLoad: synced {} house(s) and {} loaded adult tenant(s)",
                       hh.AllHouses().size(), syncedCount);
    }

    void HouseholdEngineOps::UnlockAndGrantSpouseHomeAccess(RE::FormID homeCellFormId) {
        if (homeCellFormId == 0) {
            return;
        }
        if (!DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            AddCurrentEpochTask("HouseholdEngineOps::UnlockAndGrantSpouseHomeAccess", [homeCellFormId]() {
                HouseholdEngineOps::GetSingleton().UnlockAndGrantSpouseHomeAccess(homeCellFormId);
            });
            return;
        }

        const auto doorIds = SpatialWorldIndex::GetSingleton().GetCellDoors(homeCellFormId);
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto invCounts = player ? player->GetInventoryCounts() : RE::TESObjectREFR::InventoryCountMap{};

        const auto processDoorLock = [&](RE::TESObjectREFR* ref) {
            if (!ref || ref->IsDeleted()) {
                return;
            }
            auto* lockData = ref->GetLock();
            if (!lockData) {
                return;
            }
            if (RE::TESKey* key = lockData->key) {
                if (player) {
                    const auto it = invCounts.find(key);
                    if (it == invCounts.end() || it->second <= 0) {
                        player->AddObjectToContainer(key, nullptr, 1, nullptr);
                        invCounts[key] = 1;
                    }
                }
            } else if (lockData->IsLocked() &&
                       lockData->GetLockLevel(ref) != RE::LOCK_LEVEL::kRequiresKey) {
                lockData->SetLocked(false);
                ref->AddLockChange();
            }
        };

        for (const RE::FormID doorId : doorIds) {
            if (doorId == 0) {
                continue;
            }
            auto* doorRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(doorId);
            if (!doorRef || doorRef->IsDeleted()) {
                continue;
            }
            processDoorLock(doorRef);
            if (auto* teleport = doorRef->extraList.GetByType<RE::ExtraTeleport>();
                teleport && teleport->teleportData) {
                if (auto linked = teleport->teleportData->linkedDoor.get()) {
                    processDoorLock(linked.get());
                }
            }
        }
    }

}  // namespace MARAS
