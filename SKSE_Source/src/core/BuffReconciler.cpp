#include "core/BuffReconciler.h"

#include "RE/B/BGSAbilityPerkEntry.h"
#include "RE/B/BGSEntryPointFunctionDataOneValue.h"
#include "RE/B/BGSEntryPointPerkEntry.h"
#include "core/BonusesService.h"
#include "core/DeferredActorResolver.h"
#include "core/PollingService.h"
#include "core/SessionEpoch.h"
#include "maras/state/EngineSyncPlan.h"
#include "maras/state/WorldStore.h"
#include "utils/Common.h"
#include "utils/FormUtils.h"
#include "utils/StringUtils.h"

#include <array>
#include <cmath>
#include <unordered_map>

namespace MARAS {

    namespace {
        constexpr float kMagnitudeEpsilon = 1e-5f;

        constexpr std::array<const char*, State::BuffMultipliersPlan::kSocialClassCount> kSocialClassKeys{{
            "outcast",
            "poverty",
            "working",
            "middle",
            "wealthy",
            "religious",
            "nobles",
            "rulers"
        }};

        constexpr std::array<const char*, State::BuffMultipliersPlan::kSkillTypeCount> kSkillTypeKeys{{
            "warrior",
            "mage",
            "rogue",
            "craftsman",
            "ranger",
            "orator"
        }};
    }  // namespace

    BuffReconciler& BuffReconciler::GetSingleton() {
        static BuffReconciler instance;
        return instance;
    }

    void BuffReconciler::InvalidateCache() {
        cacheValid_ = false;
        perkGroups_.clear();
    }

    void BuffReconciler::Revert() {
        pendingReconcile_.store(false, std::memory_order_release);
        InvalidateCache();
    }

    void BuffReconciler::RebuildCacheIfNeeded() {
        if (cacheValid_) {
            return;
        }

        perkGroups_.clear();
        const auto& bonuses = BonusesService::GetSingleton();
        std::unordered_map<RE::BGSPerk*, std::size_t> perkToGroupIdx;

        auto appendCategory = [&](bool isPermanent, std::size_t catIndex, const char* categoryKey) {
            const int count = bonuses.GetBonusCount(categoryKey);
            for (int i = 0; i < count; ++i) {
                const auto* be = bonuses.GetBonusEntry(categoryKey, i);
                if (!be || be->perk.empty()) {
                    continue;
                }
                const auto resolvedIdOpt = Utils::ParseAndResolveFormKey(be->perk);
                if (!resolvedIdOpt.has_value() || *resolvedIdOpt == 0) {
                    MARAS_LOG_WARN("[BuffReconciler] Could not resolve bonus perk form key '{}' (category '{}', idx {})",
                                   be->perk, categoryKey, i);
                    continue;
                }
                auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(*resolvedIdOpt);
                if (!perk) {
                    MARAS_LOG_WARN("[BuffReconciler] FormID {:08X} ('{}') is not a BGSPerk",
                                   *resolvedIdOpt, be->perk);
                    continue;
                }

                std::size_t groupIdx = 0;
                if (auto it = perkToGroupIdx.find(perk); it != perkToGroupIdx.end()) {
                    groupIdx = it->second;
                } else {
                    groupIdx = perkGroups_.size();
                    perkToGroupIdx.emplace(perk, groupIdx);
                    PerkGroup group{};
                    group.perk = perk;
                    perkGroups_.push_back(std::move(group));
                }

                CachedEntryRef ref{};
                ref.isPermanent = isPermanent;
                ref.categoryIndex = catIndex;
                ref.effectIndex = be->effectIndex;
                ref.type = StringUtils::ToLowerStd(be->type);
                ref.baseValue = be->value;
                perkGroups_[groupIdx].entries.push_back(std::move(ref));
            }
        };

        for (std::size_t i = 0; i < kSocialClassKeys.size(); ++i) {
            appendCategory(true, i, kSocialClassKeys[i]);
        }
        for (std::size_t i = 0; i < kSkillTypeKeys.size(); ++i) {
            appendCategory(false, i, kSkillTypeKeys[i]);
        }

        cacheValid_ = true;
        MARAS_LOG_DEBUG("[BuffReconciler] Cached {} grouped bonus perk(s) from bonuses.json",
                        perkGroups_.size());
    }

    void BuffReconciler::ReconcilePlayerBuffs() {
        if (!DeferredActorResolver::GetSingleton().IsOnMainThread()) {
            if (!pendingReconcile_.exchange(true, std::memory_order_acq_rel)) {
                if (!AddEpochTask(GetSessionEpoch(), "BuffReconciler::ReconcilePlayerBuffs", []() {
                        BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
                    })) {
                    pendingReconcile_.store(false, std::memory_order_release);
                }
            }
            return;
        }

        pendingReconcile_.store(false, std::memory_order_release);

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        RebuildCacheIfNeeded();
        if (perkGroups_.empty()) {
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto teammatesRaw = PollingService::GetSingleton().GetCurrentTeammates();
        std::vector<Foundation::RefId> teammateIds;
        teammateIds.reserve(teammatesRaw.size());
        for (const RE::FormID id : teammatesRaw) {
            teammateIds.push_back(Foundation::RefId{ id });
        }

        const auto multPlan = State::ComputeBuffMultipliers(*snap, teammateIds);

        struct EvaluatedEntry {
            const CachedEntryRef* entry{ nullptr };
            State::BonusTargetValue target{};
        };
        std::vector<EvaluatedEntry> evaluated;

        std::size_t updatedPerkCount = 0;
        std::size_t removedPerkCount = 0;

        for (const auto& group : perkGroups_) {
            RE::BGSPerk* perk = group.perk;
            if (!perk || group.entries.empty()) {
                continue;
            }

            evaluated.clear();
            evaluated.reserve(group.entries.size());

            bool anyEntryActive = false;
            for (const auto& entry : group.entries) {
                const float mult = entry.isPermanent
                    ? multPlan.permanentSocialClassMultipliers[entry.categoryIndex]
                    : multPlan.followerSkillTypeMultipliers[entry.categoryIndex];
                const auto target = State::ComputeBonusTargetValue(entry.type, entry.baseValue, mult);
                if (target.isActive) {
                    anyEntryActive = true;
                }
                evaluated.push_back({ &entry, target });
            }

            const bool playerHasPerk = player->HasPerk(perk);

            if (!anyEntryActive) {
                if (playerHasPerk) {
                    player->RemovePerk(perk);
                    ++removedPerkCount;
                    MARAS_LOG_DEBUG("[BuffReconciler] Removed inactive bonus perk {:08X}", perk->GetFormID());
                }
                continue;
            }

            bool anyFormValueChanged = false;
            for (const auto& item : evaluated) {
                const auto& entry = *item.entry;
                const float desired = item.target.engineTargetValue;

                if (entry.effectIndex < 0 ||
                    static_cast<std::uint32_t>(entry.effectIndex) >= perk->perkEntries.size()) {
                    MARAS_LOG_WARN("[BuffReconciler] effectIndex {} out of bounds for perk {:08X} (entries={})",
                                   entry.effectIndex, perk->GetFormID(), perk->perkEntries.size());
                    continue;
                }

                auto* perkEntry = perk->perkEntries[static_cast<std::uint32_t>(entry.effectIndex)];
                if (!perkEntry) {
                    continue;
                }

                if (entry.type == "spell") {
                    if (perkEntry->GetType() != RE::PERK_ENTRY_TYPE::kAbility) {
                        MARAS_LOG_WARN("[BuffReconciler] Perk {:08X} entry {} expected kAbility for 'spell' bonus",
                                       perk->GetFormID(), entry.effectIndex);
                        continue;
                    }
                    auto* abilityEntry = static_cast<RE::BGSAbilityPerkEntry*>(perkEntry);
                    auto* spell = abilityEntry->ability;
                    if (!spell || spell->effects.empty() || !spell->effects[0]) {
                        MARAS_LOG_WARN("[BuffReconciler] Perk {:08X} entry {} has missing ability spell/effect[0]",
                                       perk->GetFormID(), entry.effectIndex);
                        continue;
                    }
                    const float currentMag = spell->effects[0]->GetMagnitude();
                    if (std::abs(currentMag - desired) > kMagnitudeEpsilon) {
                        spell->effects[0]->SetMagnitude(desired);
                        anyFormValueChanged = true;
                        MARAS_LOG_TRACE("[BuffReconciler] Perk {:08X} spell {:08X} magnitude {} -> {}",
                                        perk->GetFormID(), spell->GetFormID(), currentMag, desired);
                    }
                } else if (entry.type == "multiply" || entry.type == "add") {
                    if (perkEntry->GetType() != RE::PERK_ENTRY_TYPE::kEntryPoint) {
                        MARAS_LOG_WARN("[BuffReconciler] Perk {:08X} entry {} expected kEntryPoint for '{}' bonus",
                                       perk->GetFormID(), entry.effectIndex, entry.type);
                        continue;
                    }
                    auto* epEntry = static_cast<RE::BGSEntryPointPerkEntry*>(perkEntry);
                    auto* fnData = epEntry->functionData;
                    if (!fnData ||
                        fnData->GetType() != RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kOneValue) {
                        MARAS_LOG_WARN("[BuffReconciler] Perk {:08X} entry {} functionData is not kOneValue",
                                       perk->GetFormID(), entry.effectIndex);
                        continue;
                    }
                    auto* oneVal = static_cast<RE::BGSEntryPointFunctionDataOneValue*>(fnData);
                    if (std::abs(oneVal->data - desired) > kMagnitudeEpsilon) {
                        MARAS_LOG_TRACE("[BuffReconciler] Perk {:08X} entry {} value {} -> {}",
                                        perk->GetFormID(), entry.effectIndex, oneVal->data, desired);
                        oneVal->data = desired;
                        anyFormValueChanged = true;
                    }
                }
            }

            if (anyFormValueChanged) {
                if (playerHasPerk) {
                    player->RemovePerk(perk);
                }
                player->AddPerk(perk);
                ++updatedPerkCount;
                MARAS_LOG_DEBUG("[BuffReconciler] Applied updated perk {:08X}", perk->GetFormID());
            } else if (!playerHasPerk) {
                player->AddPerk(perk);
                ++updatedPerkCount;
                MARAS_LOG_DEBUG("[BuffReconciler] Added active perk {:08X}", perk->GetFormID());
            }
        }

        if (updatedPerkCount > 0 || removedPerkCount > 0) {
            MARAS_LOG_DEBUG("[BuffReconciler] ReconcilePlayerBuffs complete (updated={}, removed={})",
                            updatedPerkCount, removedPerkCount);
        }
    }

}  // namespace MARAS
