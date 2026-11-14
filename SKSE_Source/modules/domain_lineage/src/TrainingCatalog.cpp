#include "maras/domain/lineage/TrainingCatalog.h"

#include "maras/foundation/StringUtils.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace MARAS::Domain::Lineage {

    void TrainingCatalog::SetData(
        std::vector<TrainingClassSpec> classes,
        std::vector<DaedricBlessingSpec> daedricBlessings) {
        classes_ = std::move(classes);
        for (std::size_t i = 0; i < classes_.size(); ++i) {
            classes_[i].index = static_cast<std::int32_t>(i);
        }
        daedricBlessings_ = std::move(daedricBlessings);
    }

    void TrainingCatalog::Clear() noexcept {
        classes_.clear();
        daedricBlessings_.clear();
    }

    const TrainingClassSpec* TrainingCatalog::FindClass(std::int32_t classIndex) const noexcept {
        if (!IsValidClassIndex(classIndex)) {
            return nullptr;
        }
        return &classes_[static_cast<std::size_t>(classIndex)];
    }

    std::int32_t TrainingCatalog::FindClassByLocation(std::string_view locationName) const noexcept {
        if (locationName.empty()) {
            return -1;
        }
        for (const auto& spec : classes_) {
            if (Foundation::StringUtils::CaseInsensitiveEquals(spec.locationName, locationName)) {
                return spec.index;
            }
        }
        return -1;
    }

    const DaedricBlessingSpec* TrainingCatalog::FindDaedricBlessingById(std::string_view blessingId) const noexcept {
        if (blessingId.empty() || Foundation::StringUtils::CaseInsensitiveEquals(blessingId, "none")) {
            return nullptr;
        }
        for (const auto& b : daedricBlessings_) {
            if (Foundation::StringUtils::CaseInsensitiveEquals(b.id, blessingId) ||
                Foundation::StringUtils::CaseInsensitiveEquals(b.name, blessingId)) {
                return &b;
            }
        }
        return nullptr;
    }

    std::int32_t TrainingCatalog::GetEffectiveTrainingCost(
        std::int32_t classIndex,
        std::int32_t baseCost) const noexcept {
        if (baseCost <= 0) {
            return 0;
        }
        const auto* spec = FindClass(classIndex);
        const float mult = spec ? spec->costMultiplier : 1.0f;
        return std::max<std::int32_t>(
            0,
            static_cast<std::int32_t>(std::lround(static_cast<float>(baseCost) * mult)));
    }

    std::int32_t TrainingCatalog::GetDiscountedTrainingCost(
        std::int32_t classIndex,
        std::int32_t baseCost,
        std::int32_t discountPercent) const noexcept {
        const std::int32_t effectiveBase = GetEffectiveTrainingCost(classIndex, baseCost);
        if (effectiveBase <= 0 || discountPercent <= 0) {
            return effectiveBase;
        }
        const float factor = std::clamp(1.0f - (static_cast<float>(discountPercent) / 100.0f), 0.0f, 1.0f);
        return std::max<std::int32_t>(
            0,
            static_cast<std::int32_t>(std::lround(static_cast<float>(effectiveBase) * factor)));
    }

    TrainingDiscountInfo TrainingCatalog::EvaluateChildTrainingDiscount(
        ChildId childId,
        std::int32_t classIndex,
        const Projection::PerspectiveView& view,
        const IParentAffiliationPort& port) const {
        TrainingDiscountInfo result{};
        const auto* spec = FindClass(classIndex);
        if (!spec) {
            return result;
        }

        const std::vector<ActorId> parents = view.GetVisibleParentIdsForTrainingDiscount(childId);
        if (parents.empty()) {
            return result;
        }

        const auto isParentLeader = [&](ActorId parentId) -> bool {
            for (const auto& npcKey : spec->leaderNpcKeys) {
                if (port.MatchesNpcKey(parentId, npcKey)) {
                    return true;
                }
            }
            for (const auto& facKey : spec->leaderFactionKeys) {
                if (port.GetFactionRank(parentId, facKey) >= 0) {
                    return true;
                }
            }
            if (spec->leaderFactionRank >= 0) {
                for (const auto& facKey : spec->memberFactionKeys) {
                    if (port.GetFactionRank(parentId, facKey) >= spec->leaderFactionRank) {
                        return true;
                    }
                }
            }
            if (parentId.IsPlayer()) {
                for (const auto& qKey : spec->leaderQuestKeys) {
                    if (port.IsQuestCompleted(qKey)) {
                        return true;
                    }
                }
            }
            return false;
        };

        const auto isParentMember = [&](ActorId parentId) -> bool {
            for (const auto& facKey : spec->memberFactionKeys) {
                if (port.GetFactionRank(parentId, facKey) >= 0) {
                    return true;
                }
            }
            if (spec->memberWerewolf && view.IsActorKnownWerewolf(parentId)) {
                return true;
            }
            if (parentId.IsPlayer()) {
                for (const auto& qKey : spec->memberQuestKeys) {
                    const bool stageMet =
                        (spec->requiredQuestStage > 0 && spec->requiredQuestKey == qKey)
                            ? (port.GetQuestStage(qKey) >= spec->requiredQuestStage || port.IsQuestCompleted(qKey))
                            : port.IsQuestCompleted(qKey);
                    if (stageMet) {
                        return true;
                    }
                }
            }
            return false;
        };

        for (const ActorId parentId : parents) {
            if (isParentLeader(parentId)) {
                result.discountPercent = 25;
                result.reason = std::format("{} (Leader)", port.GetParentDisplayName(parentId));
                return result;
            }
        }

        for (const ActorId parentId : parents) {
            if (isParentMember(parentId)) {
                result.discountPercent = 10;
                result.reason = std::format("{} (Member)", port.GetParentDisplayName(parentId));
                return result;
            }
        }

        return result;
    }

    bool TrainingCatalog::EvaluatePlayerRequirements(
        std::int32_t classIndex,
        bool playerIsWerewolf,
        const IParentAffiliationPort& port,
        std::string* outReason) const {
        const auto* spec = FindClass(classIndex);
        if (!spec) {
            return false;
        }
        if (spec->requiresBeastblood && playerIsWerewolf) {
            return true;
        }

        const bool hasFactionReq = !spec->requiredFactionKey.empty();
        const bool hasQuestReq = !spec->requiredQuestKey.empty();
        if (!hasFactionReq && !hasQuestReq && !spec->requiresBeastblood) {
            return true;
        }

        bool met = false;
        if (hasFactionReq && port.GetFactionRank(ActorId::Player(), spec->requiredFactionKey) >= 0) {
            met = true;
        }
        if (!met && hasQuestReq) {
            if (spec->requiredQuestStage > 0) {
                met = (port.GetQuestStage(spec->requiredQuestKey) >= spec->requiredQuestStage) ||
                      port.IsQuestCompleted(spec->requiredQuestKey);
            } else {
                met = port.IsQuestCompleted(spec->requiredQuestKey);
            }
        }

        if (!met && outReason != nullptr) {
            *outReason = !spec->requirementLabel.empty() ? spec->requirementLabel : spec->requirementKey;
        }
        return met;
    }

}  // namespace MARAS::Domain::Lineage
