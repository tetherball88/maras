#pragma once

#include "maras/engine/EngineLogging.h"
#include "maras/state/WorldTables.h"

#include <cstdint>

namespace MARAS {

    // Engine operations for child actor placement, linked-ref targeting, FMR race/beastblood inspection, and cleanup.
    class ChildSpawnService {
    public:
        static ChildSpawnService& GetSingleton();

        // Resolves the live guardian / bio-parent / Honorhall marker target for unhoused non-player children.
        [[nodiscard]] RE::FormID ResolveNonPlayerChildTarget(const State::ChildRecord& child) const;
        [[nodiscard]] RE::FormID ResolveNonPlayerChildTargetById(RE::FormID childId) const;

        // Manages the 0x377 child-sandbox-around-parent linked ref.
        void SetChildSandboxAroundParentLinkedRef(RE::FormID childId, RE::FormID targetRefId) const;
        void RemoveChildSandboxAroundParentLinkedRef(RE::FormID childId) const;

        // Moves an actor reference to a marker reference and refreshes AI package evaluation (main thread only).
        bool PlaceChildAtMarker(RE::Actor* childActor, RE::TESObjectREFR* targetMarker) const;
        bool PlaceChildAtMarker(RE::FormID childId, RE::FormID targetMarkerId) const;

        // Disables and flags a physical child or companion critter reference for deletion.
        bool DisableAndDeleteActor(RE::FormID actorId) const;

        // Removes BYOHRelationshipAdoptableFaction from a physical child actor.
        void ClearAdoptableFaction(RE::FormID childId) const;

        // Resolves Fertility Mode Reloaded 0..9 race index for an actor (255 = unknown/non-standard).
        [[nodiscard]] static std::uint8_t ResolveActorFMRRaceIndex(RE::FormID actorId);

        // Checks whether an actor (or saved child record) carries lycanthropy / beastblood.
        [[nodiscard]] static bool IsActorWerewolf(RE::FormID actorId);

        // Evaluates deterministic beastblood inheritance on a child record; returns true if record changed.
        static bool EvaluateChildBeastbloodInheritance(State::ChildRecord& record);

    private:
        ChildSpawnService() = default;
        ~ChildSpawnService() = default;
        ChildSpawnService(const ChildSpawnService&) = delete;
        ChildSpawnService& operator=(const ChildSpawnService&) = delete;
    };

}  // namespace MARAS
