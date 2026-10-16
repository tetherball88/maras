#pragma once

#include <REL/Relocation.h>
#include <RE/A/AIProcess.h>
#include <RE/B/BSExtraData.h>
#include <RE/E/ExtraFactionChanges.h>
#include <RE/E/ExtraLinkedRef.h>
#include <RE/P/PackageLocation.h>
#include <RE/T/TESPackage.h>

#include "core/FactionService.h"
#include "core/FormCache.h"
#include "core/NPCIndex.h"
#include "core/PackageOverrideService.h"
#include "services/ActorService.h"
#include "utils/Common.h"
#include "utils/StringUtils.h"

namespace MARAS::Utils {
    // ── Linked Reference & Package Override Utilities ──────────────────────────────

    // Sets or updates a linked reference on an object/actor with the given keyword.
    // If a_evaluatePackage is true and the source is an Actor, calls actor->EvaluatePackage().
    inline bool SetLinkedRef(RE::FormID a_sourceID, RE::FormID a_targetID, RE::BGSKeyword* a_keyword, bool a_evaluatePackage = true) {
        if (!HasPhysicalActor(a_sourceID) || !HasPhysicalActor(a_targetID) || !a_keyword) {
            return false;
        }

        auto* srcRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_sourceID);
        auto* tgtRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_targetID);
        if (!srcRef || !tgtRef) {
            return false;
        }

        auto* extraLinkedRef = srcRef->extraList.GetByType<RE::ExtraLinkedRef>();
        if (!extraLinkedRef) {
            extraLinkedRef = RE::BSExtraData::Create<RE::ExtraLinkedRef>();
            srcRef->extraList.Add(extraLinkedRef);
        }

        for (auto& entry : extraLinkedRef->linkedRefs) {
            if (entry.keyword == a_keyword) {
                entry.refr = tgtRef;
                if (a_evaluatePackage) {
                    if (auto* actor = srcRef->As<RE::Actor>()) {
                        actor->EvaluatePackage();
                    }
                }
                return true;
            }
        }

        RE::ExtraLinkedRef::LinkedRef linkedRef;
        linkedRef.keyword = a_keyword;
        linkedRef.refr = tgtRef;
        extraLinkedRef->linkedRefs.push_back(linkedRef);

        if (a_evaluatePackage) {
            if (auto* actor = srcRef->As<RE::Actor>()) {
                actor->EvaluatePackage();
            }
        }
        return true;
    }

    // Removes a linked reference by keyword from an object/actor.
    // If a_evaluatePackage is true and the source is an Actor, calls actor->EvaluatePackage().
    inline bool RemoveLinkedRef(RE::FormID a_sourceID, RE::BGSKeyword* a_keyword, bool a_evaluatePackage = true) {
        if (!HasPhysicalActor(a_sourceID) || !a_keyword) {
            return false;
        }

        auto* srcRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_sourceID);
        if (!srcRef) {
            return false;
        }

        auto* extraLinkedRef = srcRef->extraList.GetByType<RE::ExtraLinkedRef>();
        if (!extraLinkedRef) {
            return false;
        }

        bool removed = false;
        for (auto it = extraLinkedRef->linkedRefs.begin(); it != extraLinkedRef->linkedRefs.end();) {
            if (it->keyword == a_keyword) {
                it = extraLinkedRef->linkedRefs.erase(it);
                removed = true;
            } else {
                ++it;
            }
        }

        if (removed && a_evaluatePackage) {
            if (auto* actor = srcRef->As<RE::Actor>()) {
                actor->EvaluatePackage();
            }
        }
        return removed;
    }

    // Gets the linked reference FormID for the specified keyword, or 0 if not found.
    inline RE::FormID GetLinkedRef(RE::FormID a_sourceID, RE::BGSKeyword* a_keyword) {
        if (!HasPhysicalActor(a_sourceID) || !a_keyword) {
            return 0;
        }

        auto* srcRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_sourceID);
        if (!srcRef) {
            return 0;
        }

        auto* extraLinkedRef = srcRef->extraList.GetByType<RE::ExtraLinkedRef>();
        if (!extraLinkedRef) {
            return 0;
        }

        for (const auto& entry : extraLinkedRef->linkedRefs) {
            if (entry.keyword == a_keyword && entry.refr) {
                return entry.refr->GetFormID();
            }
        }
        return 0;
    }

    // High-level all-in-one helper:
    // 1. Sets/updates the linked ref with the specified keyword on the actor.
    // 2. Registers the actor with the given package override (or default base sandbox if packageID is 0).
    // 3. Injects and evaluates the package on the actor.
    inline bool AssignPackageWithLinkedRef(RE::FormID a_actorID, RE::FormID a_targetRefID, RE::BGSKeyword* a_keyword, RE::FormID a_packageID = 0) {
        if (!HasPhysicalActor(a_actorID) || !HasPhysicalActor(a_targetRefID) || !a_keyword) {
            return false;
        }

        if (!SetLinkedRef(a_actorID, a_targetRefID, a_keyword, false)) {
            return false;
        }

        if (a_packageID != 0) {
            PackageOverrideService::GetSingleton().RegisterActorWithPackage(a_actorID, a_packageID);
        } else {
            PackageOverrideService::GetSingleton().RegisterActor(a_actorID);
        }

        if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_actorID)) {
            actor->EvaluatePackage();
        }

        return true;
    }

    // Try to call the engine's RemoveFromFaction via a relocation. If the relocation
    // isn't present for the current address library/version, fall back to setting
    // the faction rank to -1 using AddToFaction (safe behavior used elsewhere).
    inline bool RemoveFromFaction(RE::Actor* a_actor, RE::TESFaction* a_faction) {
        if (!a_actor || !a_faction) {
            return false;
        }

        FactionService::InternalFactionScope scope;

        using func_t = void (*)(RE::Actor*, RE::TESFaction*);
        REL::Relocation<func_t> func{RELOCATION_ID(36680, 37688)};

        if (func.address() != 0) {
            // relocation resolved — call the real engine function
            func(a_actor, a_faction);
            return true;
        }

        // Fallback: clear the actor's rank in the faction by assigning -1.
        a_actor->AddToFaction(a_faction, static_cast<std::int8_t>(-1));
        return true;
    }

    inline bool RemoveFromFaction(RE::FormID a_formID, RE::TESFaction* a_faction) {
        if (!a_faction || !HasPhysicalActor(a_formID)) {
            return false;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_formID);
        if (!actor) {
            return false;
        }

        return RemoveFromFaction(actor, a_faction);
    }

    // Add actor to faction helper. Provide overloads for both actor pointer and FormID lookup.
    inline bool AddToFaction(RE::Actor* a_actor, RE::TESFaction* a_faction, std::int8_t a_rank) {
        if (!a_actor || !a_faction) {
            return false;
        }

        FactionService::InternalFactionScope scope;

        // Direct engine call to set faction rank for actor
        a_actor->AddToFaction(a_faction, a_rank);
        return true;
    }

    inline bool AddToFaction(RE::FormID a_formID, RE::TESFaction* a_faction, std::int8_t a_rank) {
        if (!a_faction || !HasPhysicalActor(a_formID)) {
            return false;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_formID);
        if (!actor) {
            return false;
        }

        return AddToFaction(actor, a_faction, a_rank);
    }

    inline bool SetCrimeFaction(RE::Actor* a_actor, RE::TESFaction* a_crimeFaction) {
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

    inline bool SetCrimeFaction(RE::FormID a_formID, RE::TESFaction* a_crimeFaction) {
        if (!HasPhysicalActor(a_formID)) return false;
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_formID);
        return SetCrimeFaction(actor, a_crimeFaction);
    }

    inline bool IsInFactionByEditorID(RE::Actor* actor, const char* editorID) {
        if (!actor || !editorID || editorID[0] == '\0') return false;
        auto* faction = RE::TESForm::LookupByEditorID<RE::TESFaction>(editorID);
        return faction && actor->IsInFaction(faction);
    }

    namespace detail {
        inline bool IsMannequinRace(const RE::TESRace* a_race) {
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
                if (StringUtils::Contains(raceEdid, "manakin") || StringUtils::Contains(raceEdid, "mannequin")) {
                    return true;
                }
            }

            return false;
        }
    }  // namespace detail

    inline bool IsMannequin(RE::TESNPC* a_base) {
        if (!a_base) return false;

        constexpr RE::FormID kPlayerHouseMannequinBaseID = 0x00089A85;
        if (a_base->GetFormID() == kPlayerHouseMannequinBaseID) {
            return true;
        }

        if (detail::IsMannequinRace(a_base->race)) {
            return true;
        }

        if (const char* baseEdid = a_base->GetFormEditorID(); baseEdid && baseEdid[0] != '\0') {
            if (StringUtils::Contains(baseEdid, "manakin") || StringUtils::Contains(baseEdid, "mannequin")) {
                return true;
            }
        }

        if (const char* baseName = a_base->GetName(); baseName && baseName[0] != '\0') {
            if (StringUtils::Contains(baseName, "mannequin") || StringUtils::Contains(baseName, "manakin")) {
                return true;
            }
        }

        return false;
    }

    inline bool IsMannequin(RE::Actor* a_actor) {
        if (!a_actor) return false;

        auto* base = a_actor->GetActorBase();
        if (base && IsMannequin(base)) {
            return true;
        }

        const auto* race = a_actor->GetRace();
        if (race && (!base || race != base->race) && detail::IsMannequinRace(race)) {
            return true;
        }

        if (const char* refEdid = a_actor->GetFormEditorID(); refEdid && refEdid[0] != '\0') {
            if (StringUtils::Contains(refEdid, "manakin") || StringUtils::Contains(refEdid, "mannequin")) {
                return true;
            }
        }

        return false;
    }

    // Scans high-actor process list and player character for valid, non-dead, non-combat, non-mannequin, humanoid actors within radius
    inline std::vector<RE::Actor*> GetNearbyActors(
        RE::Actor* center,
        float radius = 2000.0f)
    {
        std::vector<RE::Actor*> result;

        if (!center || radius <= 0.0f) {
            return result;
        }

        if (!center->GetActorRuntimeData().currentProcess) {
            return result;
        }

        const auto centerPos = center->GetPosition();
        const float radiusSq = radius * radius;

        auto PassesFilters = [&](RE::Actor* target) -> bool {
            if (!target || target == center) return false;
            if (!target->GetActorRuntimeData().currentProcess) return false;
            if (target->IsDead()) return false;
            if (target->GetCurrentScene()) return false;
            if (target->IsInCombat()) return false;
            if (target->IsChild()) return false;
            if (centerPos.GetSquaredDistance(target->GetPosition()) >= radiusSq) return false;
            if (IsMannequin(target)) return false;

            const auto* race = target->GetRace();
            if (!race) return false;
            {
                static RE::BGSKeyword* s_kwActorTypeNPC =
                    RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeNPC");
                if (s_kwActorTypeNPC && !race->HasKeyword(s_kwActorTypeNPC)) return false;
            }

            return true;
        };

        // Check player
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            if (PassesFilters(player)) {
                result.push_back(player);
            }
        }

        const auto processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) {
            return result;
        }

        for (auto& handle : processLists->highActorHandles) {
            if (!handle) continue;
            auto targetPtr = handle.get();
            if (!targetPtr) continue;
            RE::Actor* target = targetPtr.get();

            if (!PassesFilters(target)) continue;

            result.push_back(target);
        }

        return result;
    }

    inline bool MatchesActor(RE::FormID idInEntry, RE::FormID targetID) {
        if (idInEntry == 0 || targetID == 0) return false;
        if (idInEntry == targetID) return true;
        auto& idx = NPCIndex::GetSingleton();
        RE::FormID tRef = idx.GetRefID(targetID);
        if (tRef != 0 && idInEntry == tRef) return true;
        RE::FormID tBase = idx.GetBaseID(targetID);
        if (tBase != 0 && idInEntry == tBase) return true;
        auto* tActorBase = ActorSvc::GetActorBase(targetID);
        if (tActorBase && idInEntry == tActorBase->GetFormID()) return true;

        RE::FormID eRef = idx.GetRefID(idInEntry);
        if (eRef != 0 && (eRef == targetID || eRef == tRef || eRef == tBase)) return true;
        RE::FormID eBase = idx.GetBaseID(idInEntry);
        if (eBase != 0 && (eBase == targetID || eBase == tRef || eBase == tBase)) return true;

        return false;
    }

    namespace detail {
        inline bool IsPlayerReference(RE::TESForm* form, RE::PlayerCharacter* player) {
            if (!form) return false;
            if (form == player) return true;
            auto formID = form->GetFormID();
            return formID == kPlayerRefID || formID == kPlayerBaseID;
        }

        inline bool IsPlayerReference(RE::ObjectRefHandle handle, RE::PlayerCharacter* player) {
            if (!handle) return false;
            auto ref = handle.get();
            return ref && ref.get() == player;
        }

        inline bool IsPlayerReference(RE::RefHandle handle, RE::PlayerCharacter* player) {
            if (!handle) return false;
            auto ref = RE::TESObjectREFR::LookupByHandle(handle);
            return ref && ref.get() == player;
        }

        inline bool CheckPackageTargetsPlayer(RE::TESPackage* pkg, RE::Actor* actor, RE::PlayerCharacter* player,
                                               RE::AIProcess* ai) {
            if (pkg) {
                // 1. Check PackageTarget safely according to targType
                if (pkg->packTarg) {
                    // targType 0 = Specific Reference (ObjectRefHandle)
                    if (pkg->packTarg->targType == 0) {
                        if (IsPlayerReference(pkg->packTarg->target.handle, player)) {
                            MARAS_LOG_TRACE("Actor {:08X} package target is player", actor->GetFormID());
                            return true;
                        }
                    }
                    // targType 1 = Specific Object / Form (TESForm*)
                    else if (pkg->packTarg->targType == 1 && pkg->packTarg->target.object) {
                        if (IsPlayerReference(pkg->packTarg->target.object, player)) {
                            MARAS_LOG_TRACE("Actor {:08X} package target is player", actor->GetFormID());
                            return true;
                        }
                    }
                }

                // 2. Check PackageLocation safely (kNearReference uses refHandle)
                if (pkg->packLoc) {
                    if (pkg->packLoc->locType == RE::PackageLocation::Type::kNearReference) {
                        if (IsPlayerReference(pkg->packLoc->data.refHandle, player)) {
                            MARAS_LOG_TRACE("Actor {:08X} package location is player", actor->GetFormID());
                            return true;
                        }
                    }
                }
            }

            // 3. Check AIProcess runtime package and follow/escort state
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
    }  // namespace detail

    // Check if an actor is currently recognized as a player follower/teammate
    inline bool IsPlayerFollower(RE::Actor* actor) {
        if (!actor) {
            return false;
        }

        if (!actor->Is3DLoaded()) {
            return false;
        }

        if (actor->IsDeleted()) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;

        auto* currentFollowerFaction = FormCache::GetSingleton().GetCurrentFollowerFaction();

        bool isFollower = false;

        // Check CurrentFollowerFaction
        if (currentFollowerFaction && actor->IsInFaction(currentFollowerFaction)) {
            isFollower = true;
        }

        // Check if actor is targeting the player via package or AI state
        if (!isFollower) {
            auto* pkg = actor->GetCurrentPackage();
            auto& runtimeData = actor->GetActorRuntimeData();
            RE::AIProcess* ai = runtimeData.currentProcess;
            if (detail::CheckPackageTargetsPlayer(pkg, actor, player, ai)) {
                isFollower = true;
            }
        }

        return isFollower;
    }

}

namespace MARAS {
    using Utils::MatchesActor;
    using Utils::IsPlayerFollower;
}

