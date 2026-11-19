#pragma once

#include "maras/engine/EngineLogging.h"
#include "maras/state/DomainTypes.h"

#include <cstdint>
#include <string>

namespace MARAS::Utils {

    // ── Linked Reference & Package Override Utilities ──────────────────────────────

    // Sets or updates a linked reference on an object/actor with the given keyword.
    // If a_evaluatePackage is true and the source is an Actor, calls actor->EvaluatePackage().
    bool SetLinkedRef(
        RE::FormID a_sourceID,
        RE::FormID a_targetID,
        RE::BGSKeyword* a_keyword,
        bool a_evaluatePackage = true);

    // Removes a linked reference by keyword from an object/actor.
    // If a_evaluatePackage is true and the source is an Actor, calls actor->EvaluatePackage().
    bool RemoveLinkedRef(
        RE::FormID a_sourceID,
        RE::BGSKeyword* a_keyword,
        bool a_evaluatePackage = true);

    // Gets the linked reference FormID for the specified keyword, or 0 if not found.
    [[nodiscard]] RE::FormID GetLinkedRef(RE::FormID a_sourceID, RE::BGSKeyword* a_keyword);

    // High-level helper:
    // 1. Sets/updates the linked ref with the specified keyword on the actor.
    // 2. Registers the actor with PackageReconciler (specific packageID, or default base sandbox if 0).
    // 3. Evaluates the package on the actor.
    bool AssignPackageWithLinkedRef(
        RE::FormID a_actorID,
        RE::FormID a_targetRefID,
        RE::BGSKeyword* a_keyword,
        RE::FormID a_packageID = 0);

    // ── Faction & Crime Faction Utilities ──────────────────────────────────────────

    bool RemoveFromFaction(RE::Actor* a_actor, RE::TESFaction* a_faction);
    bool RemoveFromFaction(RE::FormID a_formID, RE::TESFaction* a_faction);

    bool AddToFaction(RE::Actor* a_actor, RE::TESFaction* a_faction, std::int8_t a_rank);
    bool AddToFaction(RE::FormID a_formID, RE::TESFaction* a_faction, std::int8_t a_rank);

    bool SetCrimeFaction(RE::Actor* a_actor, RE::TESFaction* a_crimeFaction);
    bool SetCrimeFaction(RE::FormID a_formID, RE::TESFaction* a_crimeFaction);

    [[nodiscard]] bool IsInFactionByEditorID(RE::Actor* actor, const char* editorID);

    // ── Actor Classification & Identity Helpers ────────────────────────────────────

    [[nodiscard]] bool IsMannequin(RE::TESNPC* a_base);
    [[nodiscard]] bool IsMannequin(RE::Actor* a_actor);

    [[nodiscard]] bool MatchesActor(RE::FormID idInEntry, RE::FormID targetID);
    [[nodiscard]] bool MatchesActorOrEmpty(RE::FormID slotA, RE::FormID slotB);
    [[nodiscard]] bool SameGuardianSet(RE::FormID a1, RE::FormID b1, RE::FormID a2, RE::FormID b2);
    [[nodiscard]] State::GuardianSetDiff DiffGuardianSets(
        RE::FormID oldA,
        RE::FormID oldB,
        RE::FormID newA,
        RE::FormID newB);

    [[nodiscard]] bool IsPlayerFollower(RE::Actor* actor);

    [[nodiscard]] bool AreVanillaEngineFamily(
        RE::FormID actorA,
        RE::FormID actorB,
        std::string* outReason = nullptr);

}  // namespace MARAS::Utils

namespace MARAS {
    using Utils::IsPlayerFollower;
    using Utils::MatchesActor;
}  // namespace MARAS
