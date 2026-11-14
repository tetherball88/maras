#pragma once

#include "maras/domain/lineage/ChildGrowthRules.h"
#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/StrongIds.h"
#include "maras/projection/PerspectiveView.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::Domain::Lineage {

    using Foundation::ActorId;
    using Foundation::ChildId;
    using Foundation::SkillType;

    struct ItemGrantSpec {
        std::string formKey{};
        std::int32_t count{ 1 };
    };

    struct SkillGrantSpec {
        SkillIndex skill{ SkillIndex::OneHanded };
        float bonus{ 0.0f };
    };

    struct TrainingDiscountInfo {
        std::int32_t discountPercent{ 0 };
        std::string reason{};
    };

    struct TrainingClassSpec {
        std::int32_t index{ 0 };
        std::string id{};
        std::string className{};
        std::string locationName{};
        std::string description{};
        float costMultiplier{ 1.0f };
        SkillType mappedSkillType{ SkillType::Warrior };
        bool requiresBeastblood{ false };
        std::array<float, 3> hmsWeights{ 0.50f, 0.25f, 0.25f };
        float moralityOverride{ -1.0f };
        float minConfidence{ 2.0f };

        std::string requirementKey{};
        std::string requirementLabel{};
        std::string requiredFactionKey{};
        std::string requiredQuestKey{};
        std::int32_t requiredQuestStage{ 0 };

        std::vector<std::string> memberFactionKeys{};
        std::vector<std::string> memberQuestKeys{};
        bool memberWerewolf{ false };
        std::vector<std::string> leaderFactionKeys{};
        std::int32_t leaderFactionRank{ -1 };
        std::vector<std::string> leaderQuestKeys{};
        std::vector<std::string> leaderNpcKeys{};

        std::vector<std::string> classKeys{};
        std::vector<std::string> combatStyleKeys{};
        std::vector<std::string> outfitKeys{};
        std::vector<SkillGrantSpec> skills{};
        std::vector<std::string> spellKeys{};
        std::vector<std::string> perkKeys{};
        std::vector<ItemGrantSpec> items{};

        std::string imagePath{};
    };

    struct DaedricBlessingSpec {
        std::string id{};
        std::string name{};
        std::string questKey{};
        float bonusHealth{ 0.0f };
        float bonusMagicka{ 0.0f };
        float bonusStamina{ 0.0f };
        std::vector<SkillGrantSpec> skills{};
        std::vector<std::string> spellKeys{};
        std::vector<std::string> perkKeys{};
    };

    class IParentAffiliationPort {
    public:
        virtual ~IParentAffiliationPort() = default;

        [[nodiscard]] virtual bool MatchesNpcKey(ActorId parentId, std::string_view npcKey) const = 0;
        [[nodiscard]] virtual std::int32_t GetFactionRank(ActorId parentId, std::string_view factionKey) const = 0;
        [[nodiscard]] virtual bool IsQuestCompleted(std::string_view questKey) const = 0;
        [[nodiscard]] virtual std::int32_t GetQuestStage(std::string_view questKey) const = 0;
        [[nodiscard]] virtual std::string GetParentDisplayName(ActorId parentId) const = 0;
    };

    class TrainingCatalog {
    public:
        void SetData(
            std::vector<TrainingClassSpec> classes,
            std::vector<DaedricBlessingSpec> daedricBlessings);

        void Clear() noexcept;

        [[nodiscard]] std::int32_t GetClassCount() const noexcept {
            return static_cast<std::int32_t>(classes_.size());
        }
        [[nodiscard]] bool IsValidClassIndex(std::int32_t classIndex) const noexcept {
            return classIndex >= 0 && classIndex < GetClassCount();
        }
        [[nodiscard]] const std::vector<TrainingClassSpec>& AllClasses() const noexcept { return classes_; }
        [[nodiscard]] const std::vector<DaedricBlessingSpec>& AllDaedricBlessings() const noexcept {
            return daedricBlessings_;
        }

        [[nodiscard]] const TrainingClassSpec* FindClass(std::int32_t classIndex) const noexcept;
        [[nodiscard]] std::int32_t FindClassByLocation(std::string_view locationName) const noexcept;
        [[nodiscard]] const DaedricBlessingSpec* FindDaedricBlessingById(std::string_view blessingId) const noexcept;

        [[nodiscard]] std::int32_t GetEffectiveTrainingCost(
            std::int32_t classIndex,
            std::int32_t baseCost) const noexcept;
        [[nodiscard]] std::int32_t GetDiscountedTrainingCost(
            std::int32_t classIndex,
            std::int32_t baseCost,
            std::int32_t discountPercent) const noexcept;

        [[nodiscard]] TrainingDiscountInfo EvaluateChildTrainingDiscount(
            ChildId childId,
            std::int32_t classIndex,
            const Projection::PerspectiveView& view,
            const IParentAffiliationPort& port) const;

        [[nodiscard]] bool EvaluatePlayerRequirements(
            std::int32_t classIndex,
            bool playerIsWerewolf,
            const IParentAffiliationPort& port,
            std::string* outReason = nullptr) const;

    private:
        std::vector<TrainingClassSpec> classes_{};
        std::vector<DaedricBlessingSpec> daedricBlessings_{};
    };

}  // namespace MARAS::Domain::Lineage
