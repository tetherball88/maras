#include "maras/engine/ActorEngineUtils.h"

#include "maras/engine/ActorService.h"
#include "maras/engine/FactionReconciler.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/PackageReconciler.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/foundation/StringUtils.h"
#include "maras/state/SpatialActorSnapshot.h"

#include <RE/A/AIProcess.h>
#include <RE/B/BSExtraData.h>
#include <RE/E/ExtraFactionChanges.h>
#include <RE/E/ExtraLinkedRef.h>
#include <RE/P/PackageLocation.h>
#include <RE/T/TESPackage.h>
#include <REL/Relocation.h>

namespace MARAS::Utils {

    namespace {

        bool IsMannequinRace(const RE::TESRace* a_race) {
            if (!a_race) return false;

            constexpr RE::FormID kManakinRaceFormID = 0x0010760A;
            if (a_race->GetFormID() == kManakinRaceFormID) {
                return true;
            }

            static RE::TESRace* s_manakinRace = RE::TESForm::LookupByEditorID<RE::TESRace>("ManakinRace");
            if (s_manakinRace && a_race == s_manakinRace) {
                return true;
            }

            if (const char* raceEdid = a_race->GetFormEditorID(); raceEdid && raceEdid[0] != '\0') {
                if (Foundation::StringUtils::Contains(raceEdid, "manakin") ||
                    Foundation::StringUtils::Contains(raceEdid, "mannequin")) {
                    return true;
                }
            }

            return false;
        }

        bool IsPlayerReference(RE::TESForm* form, RE::PlayerCharacter* player) {
            if (!form) return false;
            if (form == player) return true;
            const auto formID = form->GetFormID();
            return formID == kPlayerRefID || formID == kPlayerBaseID;
        }

        bool IsPlayerReference(RE::ObjectRefHandle handle, RE::PlayerCharacter* player) {
            if (!handle) return false;
            const auto ref = handle.get();
            return ref && ref.get() == player;
        }

        bool IsPlayerReference(RE::RefHandle handle, RE::PlayerCharacter* player) {
            if (!handle) return false;
            const auto ref = RE::TESObjectREFR::LookupByHandle(handle);
            return ref && ref.get() == player;
        }

        bool CheckPackageTargetsPlayer(
            RE::TESPackage* pkg,
            RE::Actor* actor,
            RE::PlayerCharacter* player,
            RE::AIProcess* ai) {
            if (pkg) {
                if (pkg->packTarg) {
                    if (pkg->packTarg->targType == 0) {
                        if (IsPlayerReference(pkg->packTarg->target.handle, player)) {
                            MARAS_LOG_TRACE("Actor {:08X} package target is player", actor->GetFormID());
                            return true;
                        }
                    } else if (pkg->packTarg->targType == 1 && pkg->packTarg->target.object) {
                        if (IsPlayerReference(pkg->packTarg->target.object, player)) {
                            MARAS_LOG_TRACE("Actor {:08X} package target is player", actor->GetFormID());
                            return true;
                        }
                    }
                }

                if (pkg->packLoc && pkg->packLoc->locType == RE::PackageLocation::Type::kNearReference) {
                    if (IsPlayerReference(pkg->packLoc->data.refHandle, player)) {
                        MARAS_LOG_TRACE("Actor {:08X} package location is player", actor->GetFormID());
                        return true;
                    }
                }
            }

            if (ai) {
                if (IsPlayerReference(ai->currentPackage.target, player)) {
                    return true;
                }
                if (ai->escortingPlayer) {
                    return true;
                }
                if (IsPlayerReference(ai->followTarget, player)) {
                    return true;
                }
            }

            return false;
        }

    }  // namespace

    bool SetLinkedRef(
        RE::FormID a_sourceID,
        RE::FormID a_targetID,
        RE::BGSKeyword* a_keyword,
        bool a_evaluatePackage) {
        if (!HasPhysicalActor(a_sourceID) || !HasPhysicalActor(a_targetID) || !a_keyword) {
            return false;
        }

        auto* srcRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_sourceID);
        auto* tgtRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_targetID);
        if (!srcRef || srcRef->IsDeleted() || !tgtRef || tgtRef->IsDeleted()) {
            return false;
        }

        auto* existing = srcRef->extraList.GetLinkedRef(a_keyword);
        if (existing != tgtRef) {
            srcRef->extraList.SetLinkedRef(tgtRef, a_keyword);
        }

        if (a_evaluatePackage) {
            if (auto* actor = srcRef->As<RE::Actor>()) {
                actor->EvaluatePackage();
            }
        }
        return true;
    }

    bool RemoveLinkedRef(
        RE::FormID a_sourceID,
        RE::BGSKeyword* a_keyword,
        bool a_evaluatePackage) {
        if (!HasPhysicalActor(a_sourceID) || !a_keyword) {
            return false;
        }

        auto* srcRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_sourceID);
        if (!srcRef || srcRef->IsDeleted()) {
            return false;
        }

        if (!srcRef->extraList.GetLinkedRef(a_keyword)) {
            return false;
        }

        srcRef->extraList.SetLinkedRef(nullptr, a_keyword);

        if (a_evaluatePackage) {
            if (auto* actor = srcRef->As<RE::Actor>()) {
                actor->EvaluatePackage();
            }
        }
        return true;
    }

    RE::FormID GetLinkedRef(RE::FormID a_sourceID, RE::BGSKeyword* a_keyword) {
        if (!HasPhysicalActor(a_sourceID) || !a_keyword) {
            return 0;
        }

        auto* srcRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_sourceID);
        if (!srcRef || srcRef->IsDeleted()) {
            return 0;
        }

        auto* linked = srcRef->extraList.GetLinkedRef(a_keyword);
        return linked ? linked->GetFormID() : 0;
    }

    bool AssignPackageWithLinkedRef(
        RE::FormID a_actorID,
        RE::FormID a_targetRefID,
        RE::BGSKeyword* a_keyword,
        RE::FormID a_packageID) {
        if (!HasPhysicalActor(a_actorID) || !HasPhysicalActor(a_targetRefID) || !a_keyword) {
            return false;
        }

        if (!SetLinkedRef(a_actorID, a_targetRefID, a_keyword, false)) {
            return false;
        }

        if (a_packageID != 0) {
            PackageReconciler::GetSingleton().RegisterActorWithPackage(a_actorID, a_packageID);
        } else {
            PackageReconciler::GetSingleton().RegisterActor(a_actorID);
        }

        if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_actorID); actor && !actor->IsDeleted()) {
            actor->EvaluatePackage();
        }

        return true;
    }

    bool RemoveFromFaction(RE::Actor* a_actor, RE::TESFaction* a_faction) {
        if (!a_actor || !a_faction) {
            return false;
        }

        FactionReconciler::InternalFactionScope scope;

        using func_t = void (*)(RE::Actor*, RE::TESFaction*);
        REL::Relocation<func_t> func{ RELOCATION_ID(36680, 37688) };

        if (func.address() != 0) {
            func(a_actor, a_faction);
            return true;
        }

        a_actor->AddToFaction(a_faction, static_cast<std::int8_t>(-1));
        return true;
    }

    bool RemoveFromFaction(RE::FormID a_formID, RE::TESFaction* a_faction) {
        if (!a_faction || !HasPhysicalActor(a_formID)) {
            return false;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_formID);
        if (!actor || actor->IsDeleted()) {
            return false;
        }

        return RemoveFromFaction(actor, a_faction);
    }

    bool AddToFaction(RE::Actor* a_actor, RE::TESFaction* a_faction, std::int8_t a_rank) {
        if (!a_actor || !a_faction) {
            return false;
        }

        FactionReconciler::InternalFactionScope scope;
        a_actor->AddToFaction(a_faction, a_rank);
        return true;
    }

    bool AddToFaction(RE::FormID a_formID, RE::TESFaction* a_faction, std::int8_t a_rank) {
        if (!a_faction || !HasPhysicalActor(a_formID)) {
            return false;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_formID);
        if (!actor || actor->IsDeleted()) {
            return false;
        }

        return AddToFaction(actor, a_faction, a_rank);
    }

    bool SetCrimeFaction(RE::Actor* a_actor, RE::TESFaction* a_crimeFaction) {
        if (!a_actor) return false;
        auto* xFac = a_actor->extraList.GetByType<RE::ExtraFactionChanges>();
        if (!xFac) {
            xFac = RE::BSExtraData::Create<RE::ExtraFactionChanges>();
            a_actor->extraList.Add(xFac);
        }
        if (!xFac) {
            return false;
        }
        xFac->crimeFaction = a_crimeFaction;
        xFac->removeCrimeFaction = (a_crimeFaction == nullptr);
        return true;
    }

    bool SetCrimeFaction(RE::FormID a_formID, RE::TESFaction* a_crimeFaction) {
        if (!HasPhysicalActor(a_formID)) return false;
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_formID);
        if (!actor || actor->IsDeleted()) return false;
        return SetCrimeFaction(actor, a_crimeFaction);
    }

    bool IsInFactionByEditorID(RE::Actor* actor, const char* editorID) {
        if (!actor || !editorID || editorID[0] == '\0') return false;
        auto* faction = RE::TESForm::LookupByEditorID<RE::TESFaction>(editorID);
        return faction && actor->IsInFaction(faction);
    }

    bool IsMannequin(RE::TESNPC* a_base) {
        if (!a_base) return false;

        constexpr RE::FormID kPlayerHouseMannequinBaseID = 0x00089A85;
        if (a_base->GetFormID() == kPlayerHouseMannequinBaseID) {
            return true;
        }

        if (IsMannequinRace(a_base->race)) {
            return true;
        }

        if (const char* baseEdid = a_base->GetFormEditorID(); baseEdid && baseEdid[0] != '\0') {
            if (Foundation::StringUtils::Contains(baseEdid, "manakin") ||
                Foundation::StringUtils::Contains(baseEdid, "mannequin")) {
                return true;
            }
        }

        if (const char* baseName = a_base->GetName(); baseName && baseName[0] != '\0') {
            if (Foundation::StringUtils::Contains(baseName, "mannequin") ||
                Foundation::StringUtils::Contains(baseName, "manakin")) {
                return true;
            }
        }

        return false;
    }

    bool IsMannequin(RE::Actor* a_actor) {
        if (!a_actor) return false;

        auto* base = a_actor->GetActorBase();
        if (base && IsMannequin(base)) {
            return true;
        }

        const auto* race = a_actor->GetRace();
        if (race && (!base || race != base->race) && IsMannequinRace(race)) {
            return true;
        }

        if (const char* refEdid = a_actor->GetFormEditorID(); refEdid && refEdid[0] != '\0') {
            if (Foundation::StringUtils::Contains(refEdid, "manakin") ||
                Foundation::StringUtils::Contains(refEdid, "mannequin")) {
                return true;
            }
        }

        return false;
    }

    bool MatchesActor(RE::FormID idInEntry, RE::FormID targetID) {
        if (idInEntry == 0 || targetID == 0) return false;
        if (idInEntry == targetID) return true;

        const auto& idx = SpatialWorldIndex::GetSingleton();
        const RE::FormID tRef = idx.GetRefID(targetID);
        if (tRef != 0 && idInEntry == tRef) return true;
        const RE::FormID tBase = idx.GetBaseID(targetID);
        if (tBase != 0 && idInEntry == tBase) return true;
        if (auto* tActorBase = ActorSvc::GetActorBase(targetID);
            tActorBase && idInEntry == tActorBase->GetFormID()) {
            return true;
        }

        const RE::FormID eRef = idx.GetRefID(idInEntry);
        if (eRef != 0 && (eRef == targetID || eRef == tRef || eRef == tBase)) return true;
        const RE::FormID eBase = idx.GetBaseID(idInEntry);
        if (eBase != 0 && (eBase == targetID || eBase == tRef || eBase == tBase)) return true;

        if (const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot()) {
            const auto* tSample = spatialSnap->Find(State::RefId{ targetID });
            const auto* eSample = spatialSnap->Find(State::RefId{ idInEntry });
            if (tSample && !tSample->baseId.IsNull() &&
                (idInEntry == tSample->baseId.raw() ||
                 (eBase != 0 && eBase == tSample->baseId.raw()))) {
                return true;
            }
            if (eSample && !eSample->baseId.IsNull() &&
                (targetID == eSample->baseId.raw() ||
                 (tBase != 0 && tBase == eSample->baseId.raw()))) {
                return true;
            }
        }

        return false;
    }

    bool MatchesActorOrEmpty(RE::FormID slotA, RE::FormID slotB) {
        if (slotA == 0 && slotB == 0) return true;
        if (slotA == 0 || slotB == 0) return false;
        return MatchesActor(slotA, slotB);
    }

    bool SameGuardianSet(RE::FormID a1, RE::FormID b1, RE::FormID a2, RE::FormID b2) {
        if (a1 != 0 && b1 != 0 && MatchesActor(a1, b1)) b1 = 0;
        if (a2 != 0 && b2 != 0 && MatchesActor(a2, b2)) b2 = 0;
        return (MatchesActorOrEmpty(a1, a2) && MatchesActorOrEmpty(b1, b2)) ||
               (MatchesActorOrEmpty(a1, b2) && MatchesActorOrEmpty(b1, a2));
    }

    State::GuardianSetDiff DiffGuardianSets(
        RE::FormID oldA,
        RE::FormID oldB,
        RE::FormID newA,
        RE::FormID newB) {
        if (oldA != 0 && oldB != 0 && MatchesActor(oldA, oldB)) oldB = 0;
        if (newA != 0 && newB != 0 && MatchesActor(newA, newB)) newB = 0;

        State::GuardianSetDiff diff{};
        const auto inNew = [newA, newB](RE::FormID id) {
            return id != 0 && (MatchesActor(newA, id) || MatchesActor(newB, id));
        };
        const auto inOld = [oldA, oldB](RE::FormID id) {
            return id != 0 && (MatchesActor(oldA, id) || MatchesActor(oldB, id));
        };

        if (oldA != 0 && !inNew(oldA)) {
            diff.removedA = State::ActorId{ oldA };
        }
        if (oldB != 0 && !inNew(oldB)) {
            if (diff.removedA.IsNull()) {
                diff.removedA = State::ActorId{ oldB };
            } else {
                diff.removedB = State::ActorId{ oldB };
            }
        }

        if (newA != 0 && !inOld(newA)) {
            diff.addedA = State::ActorId{ newA };
        }
        if (newB != 0 && !inOld(newB)) {
            if (diff.addedA.IsNull()) {
                diff.addedA = State::ActorId{ newB };
            } else {
                diff.addedB = State::ActorId{ newB };
            }
        }

        diff.changed = !SameGuardianSet(oldA, oldB, newA, newB);
        return diff;
    }

    bool IsPlayerFollower(RE::Actor* actor) {
        if (!actor || !actor->Is3DLoaded() || actor->IsDeleted()) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;

        auto* currentFollowerFaction = FormCache::GetSingleton().GetCurrentFollowerFaction();
        if (currentFollowerFaction && actor->IsInFaction(currentFollowerFaction)) {
            return true;
        }

        auto* pkg = actor->GetCurrentPackage();
        auto& runtimeData = actor->GetActorRuntimeData();
        RE::AIProcess* ai = runtimeData.currentProcess;
        return CheckPackageTargetsPlayer(pkg, actor, player, ai);
    }

}  // namespace MARAS::Utils
