#include "maras/bridge_ui/HouseholdBuffsJsonBuilder.h"

#include "maras/domain/household/SpouseBuffRules.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/BonusesConfigLoader.h"
#include "maras/engine/LocalizationService.h"
#include "maras/engine/PollingService.h"
#include "maras/foundation/DomainEnums.h"
#include "maras/state/EngineSyncPlan.h"
#include "maras/state/WorldStore.h"

#include <algorithm>
#include <format>
#include <unordered_set>
#include <vector>

namespace MARAS::Bridge::Ui {

    namespace {

        struct SpouseBuffDetails {
            Foundation::ActorId actorId{};
            State::SocialClass socialClass{ State::SocialClass::Outcast };
            State::SkillType skillType{ State::SkillType::Warrior };
            int rank{ -1 };
            float rankMultiplier{ 0.0f };
            int permanentAffection{ 0 };
            float affectionMultiplier{ 0.0f };
            bool isFollower{ false };
        };

        [[nodiscard]] nlohmann::json MakeContributorJson(
            const SpouseBuffDetails& spouse,
            bool includeRankContribution) {
            nlohmann::json j = nlohmann::json::object();
            j["formId"] = std::format("{:08X}", spouse.actorId.raw());
            j["name"] = ActorSvc::GetName(spouse.actorId.raw());
            if (includeRankContribution && spouse.rank >= 0) {
                j["rank"] = spouse.rank;
            }
            if (includeRankContribution) {
                j["rankMultiplier"] = spouse.rankMultiplier;
            }
            j["affection"] = spouse.permanentAffection;
            j["affectionMultiplier"] = spouse.affectionMultiplier;
            j["multiplier"] = includeRankContribution
                                  ? (spouse.affectionMultiplier * spouse.rankMultiplier)
                                  : spouse.affectionMultiplier;
            j["isFollower"] = spouse.isFollower;
            return j;
        }

        [[nodiscard]] nlohmann::json BuildBuffEntryJson(
            const Domain::Household::EvaluatedBonusEntry& eval,
            std::string_view categoryType,
            const nlohmann::json& contributors,
            const nlohmann::json* potentialSpouses) {
            const float uiEffectiveValue = (eval.bonusType == "multiply")
                                               ? (eval.baseValue * eval.multiplier)
                                               : eval.effectiveValue;

            const auto& locSvc = Engine::LocalizationService::GetSingleton();
            nlohmann::json entry = nlohmann::json::object();
            entry["id"] = eval.categoryKey;
            entry["category"] = eval.categoryKey;
            entry["categoryType"] = categoryType;
            entry["name"] = locSvc.ResolveBonusName(eval.categoryKey, 0, eval.bonusName);
            entry["description"] = locSvc.ResolveBonusDescription(eval.categoryKey, 0, eval.description);
            entry["unit"] = eval.unit;
            entry["type"] = eval.bonusType;
            entry["baseValue"] = eval.baseValue;
            entry["multiplier"] = eval.multiplier;
            entry["effectiveValue"] = uiEffectiveValue;
            entry["isActive"] = eval.isActive;
            entry["contributors"] = contributors;
            if (potentialSpouses != nullptr) {
                entry["potentialSpouses"] = *potentialSpouses;
            }
            return entry;
        }

    }  // namespace

    nlohmann::json BuildHouseholdBuffsJson() {
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto& catalog = Engine::BonusesConfigLoader::GetSingleton().GetCatalog();
        const auto rawTeammates = PollingService::GetSingleton().GetCurrentTeammates();

        std::vector<Foundation::RefId> teammateRefs;
        std::unordered_set<RE::FormID> teammateSet;
        teammateRefs.reserve(rawTeammates.size());
        teammateSet.reserve(rawTeammates.size());
        for (const RE::FormID id : rawTeammates) {
            if (id != 0) {
                teammateRefs.push_back(Foundation::RefId{ id });
                teammateSet.insert(id);
            }
        }

        const auto report = Domain::Household::EvaluateHouseholdBuffs(*worldSnap, teammateRefs, catalog);

        std::vector<SpouseBuffDetails> livingSpouses;
        if (worldSnap->actors && worldSnap->relationships && worldSnap->affection &&
            worldSnap->hierarchy && worldSnap->deceased) {
            std::vector<Foundation::ActorId> marriedIds;
            for (const auto& [partnerId, status] :
                 worldSnap->relationships->GetActivePartners(Foundation::ActorId::Player())) {
                if (status != Foundation::RelationshipStatus::Married ||
                    partnerId.IsNull() ||
                    partnerId.IsPlayer()) {
                    continue;
                }
                const auto* prof = worldSnap->actors->Find(partnerId.AsRefId());
                if (!prof || !prof->isRegisteredAdult) {
                    continue;
                }
                if (worldSnap->deceased->IsDeceased(partnerId, prof->baseId)) {
                    continue;
                }
                marriedIds.push_back(partnerId);
            }

            std::sort(marriedIds.begin(), marriedIds.end(), [](Foundation::ActorId a, Foundation::ActorId b) {
                return a.raw() < b.raw();
            });

            const int livingCount = static_cast<int>(marriedIds.size());
            livingSpouses.reserve(marriedIds.size());
            for (const Foundation::ActorId spouseId : marriedIds) {
                const auto* prof = worldSnap->actors->Find(spouseId.AsRefId());
                if (!prof) {
                    continue;
                }
                const int rank = worldSnap->hierarchy->GetRank(spouseId);
                const float rankMult = State::ComputeHierarchyRankMultiplier(rank, livingCount);
                const int permAff = worldSnap->affection->GetPermanent(spouseId, 0);
                const float affMult = State::ComputeAffectionMultiplier(permAff);

                livingSpouses.push_back(SpouseBuffDetails{
                    .actorId = spouseId,
                    .socialClass = prof->socialClass,
                    .skillType = prof->skillType,
                    .rank = rank,
                    .rankMultiplier = rankMult,
                    .permanentAffection = permAff,
                    .affectionMultiplier = affMult,
                    .isFollower = teammateSet.contains(spouseId.raw())
                });
            }
        }

        nlohmann::json permanentBuffs = nlohmann::json::array();
        for (std::size_t i = 0; i < report.permanentEntries.size(); ++i) {
            const auto targetSocialClass = static_cast<State::SocialClass>(i);
            nlohmann::json contributors = nlohmann::json::array();
            for (const auto& spouse : livingSpouses) {
                if (spouse.socialClass == targetSocialClass) {
                    contributors.push_back(MakeContributorJson(spouse, /*includeRankContribution=*/true));
                }
            }
            permanentBuffs.push_back(BuildBuffEntryJson(
                report.permanentEntries[i],
                "permanent",
                contributors,
                nullptr));
        }

        nlohmann::json followerBuffs = nlohmann::json::array();
        for (std::size_t i = 0; i < report.followerEntries.size(); ++i) {
            const auto targetSkillType = static_cast<State::SkillType>(i);
            nlohmann::json contributors = nlohmann::json::array();
            nlohmann::json potentialSpouses = nlohmann::json::array();
            for (const auto& spouse : livingSpouses) {
                if (spouse.skillType != targetSkillType) {
                    continue;
                }
                if (spouse.isFollower) {
                    contributors.push_back(MakeContributorJson(spouse, /*includeRankContribution=*/false));
                } else {
                    potentialSpouses.push_back(MakeContributorJson(spouse, /*includeRankContribution=*/false));
                }
            }
            followerBuffs.push_back(BuildBuffEntryJson(
                report.followerEntries[i],
                "follower",
                contributors,
                &potentialSpouses));
        }

        nlohmann::json result = nlohmann::json::object();
        result["permanentBuffs"] = std::move(permanentBuffs);
        result["followerBuffs"] = std::move(followerBuffs);
        result["activePermanentCount"] = report.activePermanentCount;
        result["activeFollowerCount"] = report.activeFollowerCount;
        return result;
    }

}  // namespace MARAS::Bridge::Ui
