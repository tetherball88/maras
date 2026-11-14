#include "maras/domain/household/SpouseBuffRules.h"

#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/StringUtils.h"

namespace MARAS::Domain::Household {

    namespace {
        [[nodiscard]] std::string SkillKeyForBuffs(SkillType st) {
            return Foundation::StringUtils::ToLowerStd(Foundation::SkillTypeToString(st));
        }

        [[nodiscard]] std::string SocialKeyForBuffs(SocialClass sc) {
            return Foundation::StringUtils::ToLowerStd(Foundation::SocialClassToString(sc));
        }
    }  // namespace

    float ComputeSpouseMultiplier(
        const State::WorldStateSnapshot& snap,
        ActorId spouseId) noexcept {
        if (!snap.relationships || !snap.hierarchy || !snap.affection || !snap.deceased ||
            spouseId.IsNull() || spouseId.IsPlayer()) {
            return 0.0f;
        }
        int livingMarriedCount = 0;
        for (const auto& [partnerId, status] : snap.relationships->GetActivePartners(ActorId::Player())) {
            if (status == Foundation::RelationshipStatus::Married &&
                !snap.deceased->IsDeceased(partnerId) &&
                (!snap.actors || snap.actors->IsRegisteredAdult(partnerId.AsRefId()))) {
                ++livingMarriedCount;
            }
        }
        if (livingMarriedCount <= 0) {
            return 0.0f;
        }
        const int rank = snap.hierarchy->GetRank(spouseId);
        const int permAff = snap.affection->GetPermanent(spouseId, 50);
        return State::ComputeHierarchyRankMultiplier(rank, livingMarriedCount) *
               State::ComputeAffectionMultiplier(permAff);
    }

    std::array<float, State::BuffMultipliersPlan::kSkillTypeCount> ComputeFollowerMultipliers(
        const State::WorldStateSnapshot& snap,
        std::span<const RefId> followerIds) noexcept {
        return State::ComputeBuffMultipliers(snap, followerIds).followerSkillTypeMultipliers;
    }

    std::array<float, State::BuffMultipliersPlan::kSocialClassCount> ComputePermanentMultipliers(
        const State::WorldStateSnapshot& snap) noexcept {
        return State::ComputeBuffMultipliers(snap, {}).permanentSocialClassMultipliers;
    }

    HouseholdBuffReport EvaluateHouseholdBuffs(
        const State::WorldStateSnapshot& snap,
        std::span<const RefId> activeTeammates,
        const Relationships::BonusCatalog& catalog) {
        HouseholdBuffReport report{};
        report.rawMultipliers = State::ComputeBuffMultipliers(snap, activeTeammates);

        for (std::size_t i = 0; i < State::BuffMultipliersPlan::kSocialClassCount; ++i) {
            const auto sc = static_cast<SocialClass>(i);
            const std::string key = SocialKeyForBuffs(sc);
            const float mult = report.rawMultipliers.permanentSocialClassMultipliers[i];
            const auto* be = catalog.GetBonusEntry(key, 0);

            EvaluatedBonusEntry entry{};
            entry.categoryKey = key;
            entry.bonusName = be && !be->name.empty() ? be->name : std::string(Foundation::SocialClassToString(sc));
            entry.description = be ? be->description : "";
            entry.unit = be ? be->unit : "";
            entry.bonusType = be && !be->type.empty() ? be->type : "spell";
            entry.baseValue = be ? be->value : 0.0f;
            entry.multiplier = mult;

            const auto targetVal = State::ComputeBonusTargetValue(entry.bonusType, entry.baseValue, mult);
            entry.effectiveValue = targetVal.rawScaledValue;
            entry.engineTargetValue = targetVal.engineTargetValue;
            entry.isActive = targetVal.isActive;
            if (entry.isActive) {
                ++report.activePermanentCount;
            }
            report.permanentEntries.push_back(std::move(entry));
        }

        for (std::size_t i = 0; i < State::BuffMultipliersPlan::kSkillTypeCount; ++i) {
            const auto st = static_cast<SkillType>(i);
            const std::string key = SkillKeyForBuffs(st);
            const float mult = report.rawMultipliers.followerSkillTypeMultipliers[i];
            const auto* be = catalog.GetBonusEntry(key, 0);

            EvaluatedBonusEntry entry{};
            entry.categoryKey = key;
            entry.bonusName = be && !be->name.empty() ? be->name : std::string(Foundation::SkillTypeToString(st));
            entry.description = be ? be->description : "";
            entry.unit = be ? be->unit : "";
            entry.bonusType = be && !be->type.empty() ? be->type : "spell";
            entry.baseValue = be ? be->value : 0.0f;
            entry.multiplier = mult;

            const auto targetVal = State::ComputeBonusTargetValue(entry.bonusType, entry.baseValue, mult);
            entry.effectiveValue = targetVal.rawScaledValue;
            entry.engineTargetValue = targetVal.engineTargetValue;
            entry.isActive = targetVal.isActive;
            if (entry.isActive) {
                ++report.activeFollowerCount;
            }
            report.followerEntries.push_back(std::move(entry));
        }

        return report;
    }

}  // namespace MARAS::Domain::Household
