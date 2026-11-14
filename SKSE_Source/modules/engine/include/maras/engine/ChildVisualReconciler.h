#pragma once

#include "maras/engine/EngineLogging.h"
#include "maras/state/WorldTables.h"

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace RE {
    class BGSOutfit;
    class BGSPerk;
    class SpellItem;
    class TESBoundObject;
    class TESClass;
    class TESCombatStyle;
    class TESNPC;
}  // namespace RE

namespace MARAS {

    // Manages runtime adult-child TESNPC clones, compiled TESClass/TESCombatStyle/BGSOutfit resolution,
    // 3D genetic headpart/hairColor/scale application, and 3-pillar RPG progression & perk synchronization.
    class ChildVisualReconciler {
    public:
        static ChildVisualReconciler& GetSingleton();

        // Runtime form resolution for configured training classes (indexed into TrainingCatalogLoader).
        [[nodiscard]] RE::TESClass* GetTrainingClass(std::int32_t classIndex);
        [[nodiscard]] RE::TESCombatStyle* GetCombatStyle(std::int32_t classIndex);
        [[nodiscard]] RE::BGSOutfit* GetClassOutfit(std::int32_t classIndex);
        [[nodiscard]] std::vector<RE::SpellItem*> GetClassSpells(std::int32_t classIndex);
        [[nodiscard]] std::vector<RE::BGSPerk*> GetClassPerks(std::int32_t classIndex);
        [[nodiscard]] std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> GetClassEquipment(
            std::int32_t classIndex);

        // Evaluates birth / graduation skill snapshots & Daedric blessing on a mutable ChildRecord.
        bool EvaluateChildBioLineageAtBirth(State::ChildRecord& record);
        bool EvaluateChildGuardianLineageAtGraduation(State::ChildRecord& record);

        // Ensures an isolated per-actor cloned TESNPC base for a Stage 4 Adult child and attaches it.
        RE::TESNPC* EnsurePerActorRuntimeBase(RE::Actor* actor, State::ChildRecord& record);

        // Full birth genetics + Stage 4 RPG progression + visual & perk application for a loaded child actor.
        void InitializeAndApplyChildVisualsAndRPG(RE::FormID childId);

        // Applies persisted visual/scale/combatStyle/outfit/perks onto a loaded child actor reference.
        void ApplyChildVisualsToActor(RE::Actor* actor, State::ChildRecord& record);
        void ApplyAdultChildPerks(RE::Actor* actor, const State::ChildRecord& record);
        void ApplyChildVisualsOn3DLoad(RE::Actor* actor);
        void ReassertAllChildVisualsOnLoad();

        // Per-actor runtime TESNPC clone lifetime management.
        [[nodiscard]] RE::TESNPC* GetOriginalAdultBase(RE::FormID childActorId) const;
        void ReleaseRuntimeBaseForActor(RE::FormID childActorId);
        void FlushRetiredForms();
        void RevertRuntimeBases();
        void ClearCachedCatalogForms();
        void Revert();

    private:
        struct CachedClassRuntimeForms {
            RE::TESClass* runtimeClass{ nullptr };
            RE::TESCombatStyle* combatStyle{ nullptr };
            RE::BGSOutfit* outfit{ nullptr };
        };

        ChildVisualReconciler() = default;
        ~ChildVisualReconciler() = default;
        ChildVisualReconciler(const ChildVisualReconciler&) = delete;
        ChildVisualReconciler& operator=(const ChildVisualReconciler&) = delete;

        void EnsureClassCacheSize(std::size_t count);

        std::vector<CachedClassRuntimeForms> classCaches_{};
        std::unordered_map<RE::FormID, RE::TESNPC*> originalAdultBases_{};
        std::unordered_map<RE::FormID, RE::TESNPC*> clonedAdultBases_{};
        std::vector<RE::TESNPC*> retiredClonedBases_{};
        std::vector<RE::TESClass*> retiredClasses_{};
    };

}  // namespace MARAS
