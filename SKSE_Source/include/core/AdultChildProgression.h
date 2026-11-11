#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/RelationshipTypes.h"
#include "utils/Common.h"

namespace MARAS {

    struct ChildRegistryEntry;
    class NPCRelationshipManager;

    /**
     * AdultChildProgression
     * ---------------------
     * Owns the data-driven adolescent training catalog (loaded from
     * Data/SKSE/Plugins/MARAS/childTraining.yaml and extensible via
     * Data/SKSE/Plugins/MARAS/childTraining/*.yaml), tiered tuition multipliers,
     * guild/Daedric eligibility checks, passive Daedric Champion lineage blessings,
     * and the 3-pillar visual genetics + RPG progression applied to Stage 2/4 children.
     */
    class AdultChildProgression {
    public:
        struct ItemGrantSpec {
            std::string formKey;
            std::int32_t count{1};
        };

        struct SkillGrantSpec {
            RE::ActorValue skill{RE::ActorValue::kNone};
            float bonus{0.0f};
        };

        struct TrainingDiscountInfo {
            std::int32_t discountPercent{0};
            std::string reason;
        };

        struct TrainingClassRuntimeSpec {
            std::int32_t index{0};
            std::string id;
            std::string className;
            std::string locationName;
            std::string description;
            float costMultiplier{1.0f};
            SkillType mappedSkillType{SkillType::Warrior};
            bool requiresBeastblood{false};
            std::array<float, 3> hmsWeights{0.50f, 0.25f, 0.25f};
            float moralityOverride{-1.0f};
            float minConfidence{2.0f};

            // Optional Player eligibility requirements
            std::string requirementKey;
            std::string requirementLabel;
            std::string requiredFactionKey;
            std::string requiredQuestKey;
            std::int32_t requiredQuestStage{0};

            // Optional Parent affiliation discount criteria (10% Member / 25% Leader)
            std::vector<std::string> memberFactionKeys;
            std::vector<std::string> memberQuestKeys;
            bool memberWerewolf{false};
            std::vector<std::string> leaderFactionKeys;
            std::int32_t leaderFactionRank{-1};
            std::vector<std::string> leaderQuestKeys;
            std::vector<std::string> leaderNpcKeys;

            // Form keys (__formData|Plugin|0xHexID); supports fallback chains
            std::vector<std::string> classKeys;
            std::vector<std::string> combatStyleKeys;
            std::vector<std::string> outfitKeys;
            std::vector<SkillGrantSpec> skills;
            std::vector<std::string> spellKeys;
            std::vector<std::string> perkKeys;
            std::vector<ItemGrantSpec> items;

            // Optional image path (e.g., Data/SKSE/Plugins/MARAS/childTraining/images/<id>.webp)
            std::string imagePath;

            // Lazy-resolved runtime pointers & cached Prisma UI image data URI
            RE::TESClass* cachedRuntimeClass{nullptr};
            RE::TESCombatStyle* cachedCombatStyle{nullptr};
            RE::BGSOutfit* cachedOutfit{nullptr};
            std::string cachedImageSrc;
        };

        struct DaedricBlessingRuntimeSpec {
            std::string id;
            std::string name;
            std::string questKey;
            float bonusHealth{0.0f};
            float bonusMagicka{0.0f};
            float bonusStamina{0.0f};
            std::vector<SkillGrantSpec> skills;
            std::vector<std::string> spellKeys;
            std::vector<std::string> perkKeys;
        };

        static AdultChildProgression& GetSingleton() {
            static AdultChildProgression instance;
            return instance;
        }

        // Load childTraining.yaml and optional childTraining/*.yaml extensions
        bool LoadConfig(
            const std::filesystem::path& configPath = "Data/SKSE/Plugins/MARAS/childTraining.yaml",
            const std::filesystem::path& extensionFolder = "Data/SKSE/Plugins/MARAS/childTraining");

        // Catalog metadata queries
        static std::int32_t GetTrainingClassCount() noexcept;
        static bool IsValidTrainingClass(std::int32_t classIndex) noexcept;
        static std::int32_t FindTrainingClassByLocation(std::string_view locationName) noexcept;
        static std::string_view GetTrainingClassName(std::int32_t classIndex) noexcept;
        static std::string_view GetTrainingLocationName(std::int32_t classIndex) noexcept;
        static std::string_view GetTrainingDescription(std::int32_t classIndex) noexcept;
        static float GetTrainingClassCostMultiplier(std::int32_t classIndex) noexcept;
        static std::int32_t GetEffectiveTrainingCost(std::int32_t classIndex, std::int32_t baseCost) noexcept;
        static std::int32_t GetDiscountedTrainingCost(std::int32_t classIndex, std::int32_t baseCost,
                                                      std::int32_t discountPercent) noexcept;
        static SkillType MapTrainingClassToSkillType(std::int32_t classIndex, SkillType fallback) noexcept;
        static bool DoesTrainingRequireBeastblood(std::int32_t classIndex) noexcept;

        // Parent affiliation discounts (10% Member / 25% Leader)
        TrainingDiscountInfo EvaluateChildTrainingDiscount(const ChildRegistryEntry& entry,
                                                           std::int32_t classIndex) const;
        nlohmann::json BuildChildTrainingDiscountsJson(const ChildRegistryEntry& entry) const;

        // Player guild / Daedric covenant eligibility & Prisma UI metadata
        bool IsPlayerEligibleForTrainingClass(std::int32_t classIndex, std::string* outReason = nullptr);
        nlohmann::json BuildPlayerTrainingEligibilityJson();
        nlohmann::json BuildTrainingCatalogJson(std::int32_t baseCost);

        // Lazy-loaded form getters for adult child classes
        RE::TESClass* GetTrainingClass(std::int32_t classIndex);
        RE::TESCombatStyle* GetCombatStyle(std::int32_t classIndex);
        RE::BGSOutfit* GetClassOutfit(std::int32_t classIndex);
        std::vector<RE::SpellItem*> GetClassSpells(std::int32_t classIndex);
        std::vector<RE::BGSPerk*> GetClassPerks(std::int32_t classIndex);
        std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> GetClassEquipment(std::int32_t classIndex);

        // Per-actor runtime TESNPC clone management (never mutates shared FMR/BF ActorBase)
        RE::TESNPC* EnsurePerActorRuntimeBase(NPCRelationshipManager& mgr, RE::Actor* actor,
                                              ChildRegistryEntry& entry);
        RE::TESNPC* GetOriginalAdultBase(RE::FormID childActorID) const;
        void ReleaseRuntimeBaseForActor(RE::FormID childActorID);
        void FlushRetiredForms();
        void RevertRuntimeBases();
        void Revert();

        // Lineage skill snapshot evaluation (Stage 1 Birth & Stage 4 Graduation)
        void EvaluateChildBioLineageAtBirth(NPCRelationshipManager& mgr, ChildRegistryEntry& entry);
        void EvaluateChildGuardianLineageAtGraduation(NPCRelationshipManager& mgr, ChildRegistryEntry& entry);
        const DaedricBlessingRuntimeSpec* FindDaedricBlessingById(std::string_view blessingId) const noexcept;

        // 3-Pillar Visual Genetics & RPG Progression + Daedric Lineage Inheritance
        void InitializeAndApplyChildVisualsAndRPG(NPCRelationshipManager& mgr, RE::FormID childID);
        void ApplyChildVisualsToActor(RE::Actor* actor, ChildRegistryEntry& entry);
        void ApplyChildVisualsOn3DLoad(NPCRelationshipManager& mgr, RE::Actor* actor);
        void ReassertAllChildVisualsOnLoad(NPCRelationshipManager& mgr);

    private:
        AdultChildProgression() = default;
        ~AdultChildProgression() = default;

        AdultChildProgression(const AdultChildProgression&) = delete;
        AdultChildProgression& operator=(const AdultChildProgression&) = delete;

        bool LoadConfigFile(const std::filesystem::path& filePath, bool isExtensionFile);
        void ParseTrainingLocationObject(const nlohmann::json& obj, bool isExtensionFile);
        void ParseDaedricBlessingObject(const nlohmann::json& obj);
        void ClearCachedCatalogForms();
        bool EvaluateTrainingRequirements(const TrainingClassRuntimeSpec& spec, std::string* outReason = nullptr) const;
        void ApplyAdultChildPerks(RE::Actor* actor, const ChildRegistryEntry& entry);

        std::vector<TrainingClassRuntimeSpec> catalog_;
        std::vector<DaedricBlessingRuntimeSpec> daedricBlessings_;
        std::unordered_map<RE::FormID, RE::TESNPC*> originalAdultBases_;
        std::unordered_map<RE::FormID, RE::TESNPC*> clonedAdultBases_;
        std::vector<RE::TESNPC*> retiredClonedBases_;
        std::vector<RE::TESClass*> retiredClasses_;
    };

}  // namespace MARAS
