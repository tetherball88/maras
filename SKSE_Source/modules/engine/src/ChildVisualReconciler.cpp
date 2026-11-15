#include "maras/engine/ChildVisualReconciler.h"

#include "maras/domain/lineage/ChildGrowthRules.h"
#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/ChildSpawnService.h"
#include "maras/engine/FactionReconciler.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/FormUtils.h"
#include "maras/engine/NPCTypeDeterminer.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/engine/TrainingCatalogLoader.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/StringUtils.h"
#include "maras/state/WorldStore.h"

#include <RE/B/BGSPerk.h>
#include <RE/B/BGSPerkEntry.h>
#include <RE/E/ExtraCombatStyle.h>
#include <RE/E/ExtraOutfitItem.h>
#include <RE/T/TESClass.h>
#include <RE/T/TESCondition.h>
#include <RE/T/TESGlobal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace MARAS {

    namespace {

        constexpr std::array<RE::ActorValue, 18> kAllSkills = {
            RE::ActorValue::kOneHanded,   RE::ActorValue::kTwoHanded,   RE::ActorValue::kArchery,
            RE::ActorValue::kBlock,       RE::ActorValue::kSmithing,    RE::ActorValue::kHeavyArmor,
            RE::ActorValue::kLightArmor,  RE::ActorValue::kPickpocket,  RE::ActorValue::kLockpicking,
            RE::ActorValue::kSneak,       RE::ActorValue::kAlchemy,     RE::ActorValue::kSpeech,
            RE::ActorValue::kAlteration,  RE::ActorValue::kConjuration, RE::ActorValue::kDestruction,
            RE::ActorValue::kIllusion,    RE::ActorValue::kRestoration, RE::ActorValue::kEnchanting
        };

        [[nodiscard]] constexpr RE::ActorValue SkillIndexToActorValue(Domain::Lineage::SkillIndex idx) noexcept {
            return static_cast<RE::ActorValue>(
                static_cast<int>(RE::ActorValue::kOneHanded) + static_cast<int>(idx));
        }

        template <typename T>
        [[nodiscard]] T* ResolveFormFromKeyString(std::string_view rawKey, std::string_view context) {
            const std::string_view trimmed = Foundation::StringUtils::TrimView(rawKey);
            if (trimmed.empty()) {
                return nullptr;
            }
            if (trimmed.starts_with("__formData|")) {
                const auto parsed = Utils::ParseFormKey(trimmed);
                if (!parsed.isValid) {
                    MARAS_LOG_WARN("[ChildTraining] Malformed form key '{}' for {}", trimmed, context);
                    return nullptr;
                }
                return Utils::LookupForm<T>(parsed.localFormID, parsed.pluginName, context);
            }
            if (const auto pipePos = trimmed.find('|'); pipePos != std::string_view::npos) {
                const std::string_view plugin = Foundation::StringUtils::TrimView(trimmed.substr(0, pipePos));
                const std::string_view hexStr = Foundation::StringUtils::TrimView(trimmed.substr(pipePos + 1));
                const auto formIdOpt = Utils::HexStringToFormID(hexStr);
                if (!plugin.empty() && formIdOpt.has_value()) {
                    return Utils::LookupForm<T>(*formIdOpt, plugin, context);
                }
                MARAS_LOG_WARN("[ChildTraining] Invalid plugin|hex form key '{}' for {}", trimmed, context);
                return nullptr;
            }
            if (const auto formIdOpt = Utils::HexStringToFormID(trimmed); formIdOpt.has_value()) {
                auto* form = RE::TESForm::LookupByID<T>(*formIdOpt);
                if (!form) {
                    MARAS_LOG_WARN("[ChildTraining] Failed to lookup form 0x{:08X} ('{}') for {}",
                                   *formIdOpt, trimmed, context);
                }
                return form;
            }
            MARAS_LOG_WARN("[ChildTraining] Unrecognized form key format '{}' for {}", trimmed, context);
            return nullptr;
        }

        [[nodiscard]] RE::Actor* ResolveActorPtr(RE::FormID formId) {
            if (formId == 0 || formId == kUnknownFatherFormID || IsSyntheticChildID(formId)) {
                return nullptr;
            }
            if (Utils::MatchesActor(formId, kPlayerFormID)) {
                return RE::PlayerCharacter::GetSingleton();
            }
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(formId)) {
                return actor;
            }
            return SpatialWorldIndex::GetSingleton().GetActorFromFormId(formId);
        }

        [[nodiscard]] RE::TESNPC* GetActorBaseNPC(RE::FormID formId) {
            if (formId == 0 || formId == kUnknownFatherFormID || IsSyntheticChildID(formId)) {
                return nullptr;
            }
            if (Utils::MatchesActor(formId, kPlayerFormID)) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                return player ? player->GetActorBase() : nullptr;
            }
            return ActorSvc::GetActorBase(formId);
        }

        [[nodiscard]] bool ContainsInsensitive(std::string_view haystack, std::string_view needle) {
            if (needle.empty() || haystack.size() < needle.size()) {
                return false;
            }
            const auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                                        [](char a, char b) {
                                            return std::tolower(static_cast<unsigned char>(a)) ==
                                                   std::tolower(static_cast<unsigned char>(b));
                                        });
            return it != haystack.end();
        }

        [[nodiscard]] bool IsHeadPartValidForChild(
            RE::BGSHeadPart* part,
            RE::TESRace* childRace,
            bool isFemale,
            RE::BGSHeadPart::HeadPartType expectedType,
            bool requireVanillaMaster) {
            if (!part || part->IsDeleted() || !childRace) {
                return false;
            }
            if (part->type.get() != expectedType) {
                return false;
            }
            if (!part->flags.all(RE::BGSHeadPart::Flag::kPlayable) ||
                part->flags.any(RE::BGSHeadPart::Flag::kIsExtraPart)) {
                return false;
            }
            const bool genderMatch = isFemale ? part->flags.all(RE::BGSHeadPart::Flag::kFemale)
                                              : part->flags.all(RE::BGSHeadPart::Flag::kMale);
            if (!genderMatch) {
                return false;
            }
            if (!part->validRaces || !part->validRaces->HasForm(childRace)) {
                return false;
            }
            if (part->formEditorID.empty()) {
                return false;
            }
            if (requireVanillaMaster && ((part->GetFormID() >> 24) > 0x04)) {
                return false;
            }
            const std::string_view edid = part->formEditorID.c_str();
            if (expectedType == RE::BGSHeadPart::HeadPartType::kEyes) {
                for (std::string_view blocked : { "Vampire", "Blind", "Demon", "Afflicted", "Dead", "Falmer", "Zombie" }) {
                    if (ContainsInsensitive(edid, blocked)) {
                        return false;
                    }
                }
            } else if (expectedType == RE::BGSHeadPart::HeadPartType::kHair) {
                for (std::string_view blocked : { "Bald", "Shaved", "Afflicted" }) {
                    if (ContainsInsensitive(edid, blocked)) {
                        return false;
                    }
                }
            }
            return true;
        }

        [[nodiscard]] std::string_view ExtractEyeColorKeyword(std::string_view edid) {
            static constexpr std::array<std::string_view, 14> kEyeColors = {
                "Hazel", "Green", "Blue", "Brown", "Yellow", "Amber", "Grey",
                "Gray",  "Red",   "Aqua", "Violet", "Orange", "Dark", "Light"
            };
            for (std::string_view token : kEyeColors) {
                if (ContainsInsensitive(edid, token)) {
                    return token;
                }
            }
            return {};
        }

        [[nodiscard]] Foundation::SocialClass GetSocialClassFromWorld(
            const State::WorldStateSnapshot& worldSnap,
            RE::FormID actorId) {
            if (const auto* prof = worldSnap.actors->Find(State::RefId{ actorId })) {
                return prof->socialClass;
            }
            return NPCTypeDeterminer::DetermineSocialClass(actorId, {});
        }

        [[nodiscard]] Foundation::SkillType ResolveActorEffectiveSkillType(
            const State::WorldStateSnapshot& worldSnap,
            RE::FormID actorId) {
            if (actorId == 0 || actorId == kUnknownFatherFormID) {
                return Foundation::SkillType::Warrior;
            }
            if (Utils::MatchesActor(actorId, kPlayerFormID)) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                auto* avOwner = player ? player->AsActorValueOwner() : nullptr;
                if (!avOwner) {
                    return Foundation::SkillType::Warrior;
                }
                const float warriorScore = avOwner->GetBaseActorValue(RE::ActorValue::kOneHanded) +
                                           avOwner->GetBaseActorValue(RE::ActorValue::kTwoHanded) +
                                           avOwner->GetBaseActorValue(RE::ActorValue::kHeavyArmor) +
                                           avOwner->GetBaseActorValue(RE::ActorValue::kBlock);
                const float mageScore = avOwner->GetBaseActorValue(RE::ActorValue::kDestruction) +
                                        avOwner->GetBaseActorValue(RE::ActorValue::kConjuration) +
                                        avOwner->GetBaseActorValue(RE::ActorValue::kRestoration) +
                                        avOwner->GetBaseActorValue(RE::ActorValue::kAlteration);
                const float rogueScore = avOwner->GetBaseActorValue(RE::ActorValue::kSneak) +
                                         avOwner->GetBaseActorValue(RE::ActorValue::kPickpocket) +
                                         avOwner->GetBaseActorValue(RE::ActorValue::kLockpicking) +
                                         avOwner->GetBaseActorValue(RE::ActorValue::kLightArmor);
                const float rangerScore = avOwner->GetBaseActorValue(RE::ActorValue::kArchery) * 2.0f +
                                          avOwner->GetBaseActorValue(RE::ActorValue::kLightArmor) +
                                          avOwner->GetBaseActorValue(RE::ActorValue::kSneak);
                const float craftScore = avOwner->GetBaseActorValue(RE::ActorValue::kSmithing) +
                                         avOwner->GetBaseActorValue(RE::ActorValue::kEnchanting) +
                                         avOwner->GetBaseActorValue(RE::ActorValue::kAlchemy) * 1.5f;
                const float oratorScore = avOwner->GetBaseActorValue(RE::ActorValue::kSpeech) * 2.0f +
                                          avOwner->GetBaseActorValue(RE::ActorValue::kIllusion) * 1.5f;

                Foundation::SkillType bestType = Foundation::SkillType::Warrior;
                float bestScore = warriorScore;
                auto checkCandidate = [&](Foundation::SkillType candidate, float score) {
                    if (score > bestScore) {
                        bestScore = score;
                        bestType = candidate;
                    }
                };
                checkCandidate(Foundation::SkillType::Mage, mageScore);
                checkCandidate(Foundation::SkillType::Rogue, rogueScore);
                checkCandidate(Foundation::SkillType::Ranger, rangerScore);
                checkCandidate(Foundation::SkillType::Craftsman, craftScore);
                checkCandidate(Foundation::SkillType::Orator, oratorScore);
                return bestType;
            }
            if (const auto* prof = worldSnap.actors->Find(State::RefId{ actorId })) {
                return prof->skillType;
            }
            return NPCTypeDeterminer::DetermineSkillType(actorId, {});
        }

        [[nodiscard]] std::uint16_t ResolveActorEffectiveLevel(RE::FormID actorId) {
            if (actorId == 0 || actorId == kUnknownFatherFormID) {
                return 1;
            }
            if (Utils::MatchesActor(actorId, kPlayerFormID)) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                return player ? std::max<std::uint16_t>(1, player->GetLevel()) : 1;
            }
            if (auto* actor = ResolveActorPtr(actorId)) {
                return std::max<std::uint16_t>(1, actor->GetLevel());
            }
            if (auto* base = GetActorBaseNPC(actorId)) {
                if (base->HasPCLevelMult()) {
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    const float pLevel = static_cast<float>(player ? std::max<std::uint16_t>(1, player->GetLevel()) : 1);
                    float computed = std::round(pLevel * (static_cast<float>(base->actorData.level) / 1000.0f));
                    if (base->actorData.calcLevelMin > 0) {
                        computed = std::max(computed, static_cast<float>(base->actorData.calcLevelMin));
                    }
                    if (base->actorData.calcLevelMax > 0) {
                        computed = std::min(computed, static_cast<float>(base->actorData.calcLevelMax));
                    }
                    return static_cast<std::uint16_t>(std::clamp(computed, 1.0f, 100.0f));
                }
                return std::max<std::uint16_t>(1, base->actorData.level);
            }
            return 1;
        }

        [[nodiscard]] std::uint16_t GetEffectiveChildLevel() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            const std::uint16_t playerLevel = player ? player->GetLevel() : 10;
            return Domain::Lineage::ComputeEffectiveChildLevel(playerLevel);
        }

        [[nodiscard]] std::array<Domain::Lineage::SkillIndex, 4> ResolveParentSignatureSkills(
            const State::WorldStateSnapshot& worldSnap,
            RE::FormID parentId) {
            if (Utils::MatchesActor(parentId, kPlayerFormID)) {
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    if (auto* avOwner = player->AsActorValueOwner()) {
                        std::array<float, 18> skills{};
                        for (std::size_t i = 0; i < kAllSkills.size(); ++i) {
                            skills[i] = avOwner->GetBaseActorValue(kAllSkills[i]);
                        }
                        return Domain::Lineage::ResolveSignatureSkillsFromArray(
                            skills, ResolveActorEffectiveSkillType(worldSnap, parentId));
                    }
                }
            }
            return Domain::Lineage::GetSignatureSkillsForSkillType(
                ResolveActorEffectiveSkillType(worldSnap, parentId));
        }

        [[nodiscard]] std::optional<Domain::Lineage::ParentLineageInput> BuildParentLineageInput(
            const State::WorldStateSnapshot& worldSnap,
            RE::FormID parentId) {
            if (parentId == 0 || parentId == kUnknownFatherFormID) {
                return std::nullopt;
            }
            Domain::Lineage::ParentLineageInput out{};
            out.actorId = Foundation::ActorId{ parentId };
            out.level = ResolveActorEffectiveLevel(parentId);
            out.signatureSkills = ResolveParentSignatureSkills(worldSnap, parentId);
            return out;
        }

        [[nodiscard]] std::int32_t ResolveTrainingClassIndexForRecord(
            const State::ChildRecord& record,
            const Domain::Lineage::TrainingCatalog& catalog) noexcept {
            std::int32_t trainIdx = record.trainingClassIndex;
            if (!catalog.IsValidClassIndex(trainIdx) && !record.trainingLocation.empty()) {
                trainIdx = catalog.FindClassByLocation(record.trainingLocation);
            }
            if (!catalog.IsValidClassIndex(trainIdx)) {
                trainIdx = (catalog.GetClassCount() > 1) ? 1 : 0;
            }
            return catalog.IsValidClassIndex(trainIdx) ? trainIdx : -1;
        }

        [[nodiscard]] bool EvaluatePerkConditionComparison(
            float lhs,
            RE::CONDITION_ITEM_DATA::OpCode op,
            float rhs) noexcept {
            switch (op) {
                case RE::CONDITION_ITEM_DATA::OpCode::kEqualTo:
                    return std::abs(lhs - rhs) < 0.001f;
                case RE::CONDITION_ITEM_DATA::OpCode::kNotEqualTo:
                    return std::abs(lhs - rhs) >= 0.001f;
                case RE::CONDITION_ITEM_DATA::OpCode::kGreaterThan:
                    return lhs > (rhs - 0.001f);
                case RE::CONDITION_ITEM_DATA::OpCode::kGreaterThanOrEqualTo:
                    return lhs >= (rhs - 0.001f);
                case RE::CONDITION_ITEM_DATA::OpCode::kLessThan:
                    return lhs < (rhs + 0.001f);
                case RE::CONDITION_ITEM_DATA::OpCode::kLessThanOrEqualTo:
                    return lhs <= (rhs + 0.001f);
                default:
                    return lhs >= (rhs - 0.001f);
            }
        }

        [[nodiscard]] bool DoesActorMeetPerkRequirements(
            RE::Actor* actor,
            const RE::TESNPC* npc,
            const RE::BGSPerk* perk) {
            if (!actor || !npc || !perk) {
                return false;
            }
            const std::uint16_t actorLevel = npc->actorData.level;
            if (perk->data.level > 0 && actorLevel < static_cast<std::uint16_t>(perk->data.level)) {
                return false;
            }

            auto getActorSkillOrStat = [&](RE::ActorValue av) -> float {
                float val = 0.0f;
                const int sIdx = static_cast<int>(av) - static_cast<int>(RE::ActorValue::kOneHanded);
                if (sIdx >= 0 && sIdx < RE::TESNPC::Skills::kTotal) {
                    val = static_cast<float>(npc->playerSkills.values[sIdx]);
                }
                if (auto* avOwner = actor->AsActorValueOwner()) {
                    val = std::max(val, avOwner->GetBaseActorValue(av));
                    val = std::max(val, avOwner->GetActorValue(av));
                }
                return val;
            };

            bool inOrGroup = false;
            bool orGroupAccum = false;
            for (const auto* item = perk->perkConditions.head; item != nullptr; item = item->next) {
                if (item->data.object != RE::CONDITIONITEMOBJECT::kSelf) {
                    continue;
                }
                const auto fn = item->data.functionData.function.get();
                bool isRelevant = false;
                bool itemResult = true;

                const float targetVal = (item->data.flags.global && item->data.comparisonValue.g)
                                            ? item->data.comparisonValue.g->value
                                            : item->data.comparisonValue.f;

                if (fn == RE::FUNCTION_DATA::FunctionID::kGetBaseActorValue ||
                    fn == RE::FUNCTION_DATA::FunctionID::kGetActorValue ||
                    fn == RE::FUNCTION_DATA::FunctionID::kGetPermanentActorValue) {
                    const auto rawAV = static_cast<std::int32_t>(
                        reinterpret_cast<std::uintptr_t>(item->data.functionData.params[0]));
                    if (rawAV >= 0 && rawAV < static_cast<std::int32_t>(RE::ActorValue::kTotal)) {
                        isRelevant = true;
                        const float actualVal = getActorSkillOrStat(static_cast<RE::ActorValue>(rawAV));
                        itemResult = EvaluatePerkConditionComparison(actualVal, item->data.flags.opCode, targetVal);
                    }
                } else if (fn == RE::FUNCTION_DATA::FunctionID::kGetLevel) {
                    isRelevant = true;
                    itemResult = EvaluatePerkConditionComparison(
                        static_cast<float>(actorLevel), item->data.flags.opCode, targetVal);
                }

                if (!isRelevant) {
                    continue;
                }

                if (item->data.flags.isOR) {
                    inOrGroup = true;
                    orGroupAccum = orGroupAccum || itemResult;
                } else {
                    const bool groupResult = inOrGroup ? (orGroupAccum || itemResult) : itemResult;
                    inOrGroup = false;
                    orGroupAccum = false;
                    if (!groupResult) {
                        return false;
                    }
                }
            }
            if (inOrGroup && !orGroupAccum) {
                return false;
            }
            return true;
        }

        [[nodiscard]] bool IsEarlierRankInPerkChain(const RE::BGSPerk* lower, const RE::BGSPerk* higher) noexcept {
            if (!lower || !higher || lower == higher) {
                return false;
            }
            int hopGuard = 0;
            for (const RE::BGSPerk* cur = lower->nextPerk; cur != nullptr && hopGuard < 16; cur = cur->nextPerk, ++hopGuard) {
                if (cur == higher) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    ChildVisualReconciler& ChildVisualReconciler::GetSingleton() {
        static ChildVisualReconciler instance;
        return instance;
    }

    void ChildVisualReconciler::EnsureClassCacheSize(std::size_t count) {
        if (classCaches_.size() < count) {
            classCaches_.resize(count);
        }
    }

    RE::TESClass* ChildVisualReconciler::GetTrainingClass(std::int32_t classIndex) {
        const auto& catalog = Engine::TrainingCatalogLoader::GetSingleton().GetCatalog();
        const auto* spec = catalog.FindClass(classIndex);
        if (!spec) {
            MARAS_LOG_ERROR("[ChildTraining] GetTrainingClass called with invalid classIndex {}", classIndex);
            return nullptr;
        }
        EnsureClassCacheSize(static_cast<std::size_t>(catalog.GetClassCount()));
        auto& cache = classCaches_[static_cast<std::size_t>(classIndex)];
        if (cache.runtimeClass) {
            return cache.runtimeClass;
        }

        RE::TESClass* baseClass = nullptr;
        for (const auto& key : spec->classKeys) {
            if (auto* c = ResolveFormFromKeyString<RE::TESClass>(key, "adult child npcClass")) {
                baseClass = c;
                break;
            }
        }
        if (!baseClass) {
            MARAS_LOG_ERROR("[ChildTraining] Failed to resolve npcClass for training '{}' (idx={})",
                            spec->id, classIndex);
            return nullptr;
        }

        auto* runtimeClass = RE::calloc<RE::TESClass>(1);
        if (!runtimeClass) {
            return baseClass;
        }
        std::memcpy(runtimeClass, baseClass, sizeof(RE::TESClass));
        const char* origClassName = baseClass->GetName();
        std::memset(&runtimeClass->fullName, 0, sizeof(RE::BSFixedString));
        runtimeClass->fullName = RE::BSFixedString(
            !spec->className.empty() ? spec->className.c_str() : (origClassName ? origClassName : ""));
        runtimeClass->data.attributeWeights.health =
            static_cast<std::uint8_t>(std::clamp(std::lround(spec->hmsWeights[0] * 10.0f), 0l, 100l));
        runtimeClass->data.attributeWeights.magicka =
            static_cast<std::uint8_t>(std::clamp(std::lround(spec->hmsWeights[1] * 10.0f), 0l, 100l));
        runtimeClass->data.attributeWeights.stamina =
            static_cast<std::uint8_t>(std::clamp(std::lround(spec->hmsWeights[2] * 10.0f), 0l, 100l));

        if (!spec->skills.empty()) {
            auto* weightsPtr = &runtimeClass->data.skillWeights.oneHanded;
            std::memset(weightsPtr, 0, sizeof(RE::CLASS_DATA::SkillWeights));
            for (const auto& sg : spec->skills) {
                const int sIdx = static_cast<int>(sg.skill);
                if (sIdx >= 0 && sIdx < 18 && sg.bonus > 0.0f) {
                    const float normWeight = Domain::Lineage::NormalizeClassSkillWeight(sg.bonus);
                    weightsPtr[sIdx] = static_cast<std::uint8_t>(std::clamp(std::lround(normWeight), 1l, 10l));
                }
            }
        }

        cache.runtimeClass = runtimeClass;
        return cache.runtimeClass;
    }

    RE::TESCombatStyle* ChildVisualReconciler::GetCombatStyle(std::int32_t classIndex) {
        const auto& catalog = Engine::TrainingCatalogLoader::GetSingleton().GetCatalog();
        const auto* spec = catalog.FindClass(classIndex);
        if (!spec) {
            MARAS_LOG_ERROR("[ChildTraining] GetCombatStyle called with invalid classIndex {}", classIndex);
            return nullptr;
        }
        EnsureClassCacheSize(static_cast<std::size_t>(catalog.GetClassCount()));
        auto& cache = classCaches_[static_cast<std::size_t>(classIndex)];
        if (cache.combatStyle) {
            return cache.combatStyle;
        }
        for (const auto& key : spec->combatStyleKeys) {
            if (auto* cs = ResolveFormFromKeyString<RE::TESCombatStyle>(key, "adult child combatStyle")) {
                cache.combatStyle = cs;
                break;
            }
        }
        if (!cache.combatStyle) {
            MARAS_LOG_ERROR("[ChildTraining] Failed to resolve combatStyle for training '{}' (idx={})",
                            spec->id, classIndex);
        }
        return cache.combatStyle;
    }

    RE::BGSOutfit* ChildVisualReconciler::GetClassOutfit(std::int32_t classIndex) {
        const auto& catalog = Engine::TrainingCatalogLoader::GetSingleton().GetCatalog();
        const auto* spec = catalog.FindClass(classIndex);
        if (!spec) {
            MARAS_LOG_ERROR("[ChildTraining] GetClassOutfit called with invalid classIndex {}", classIndex);
            return nullptr;
        }
        EnsureClassCacheSize(static_cast<std::size_t>(catalog.GetClassCount()));
        auto& cache = classCaches_[static_cast<std::size_t>(classIndex)];
        if (cache.outfit) {
            return cache.outfit;
        }
        for (const auto& key : spec->outfitKeys) {
            if (auto* outfit = ResolveFormFromKeyString<RE::BGSOutfit>(key, "adult child outfit")) {
                cache.outfit = outfit;
                break;
            }
        }
        if (!cache.outfit) {
            MARAS_LOG_ERROR("[ChildTraining] Failed to resolve outfit for training '{}' (idx={})",
                            spec->id, classIndex);
        }
        return cache.outfit;
    }

    std::vector<RE::SpellItem*> ChildVisualReconciler::GetClassSpells(std::int32_t classIndex) {
        std::vector<RE::SpellItem*> out;
        const auto* spec = Engine::TrainingCatalogLoader::GetSingleton().GetCatalog().FindClass(classIndex);
        if (!spec) {
            return out;
        }
        for (const auto& key : spec->spellKeys) {
            if (auto* spell = ResolveFormFromKeyString<RE::SpellItem>(key, "adult child spell")) {
                out.push_back(spell);
            }
        }
        return out;
    }

    std::vector<RE::BGSPerk*> ChildVisualReconciler::GetClassPerks(std::int32_t classIndex) {
        std::vector<RE::BGSPerk*> out;
        const auto* spec = Engine::TrainingCatalogLoader::GetSingleton().GetCatalog().FindClass(classIndex);
        if (!spec) {
            return out;
        }
        for (const auto& key : spec->perkKeys) {
            if (auto* perk = ResolveFormFromKeyString<RE::BGSPerk>(key, "adult child perk")) {
                out.push_back(perk);
            }
        }
        return out;
    }

    std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> ChildVisualReconciler::GetClassEquipment(
        std::int32_t classIndex) {
        std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> out;
        const auto* spec = Engine::TrainingCatalogLoader::GetSingleton().GetCatalog().FindClass(classIndex);
        if (!spec) {
            return out;
        }
        for (const auto& itemSpec : spec->items) {
            if (itemSpec.count <= 0) continue;
            if (auto* obj = ResolveFormFromKeyString<RE::TESBoundObject>(itemSpec.formKey, "adult child item")) {
                out.emplace_back(obj, itemSpec.count);
            }
        }
        return out;
    }

    RE::TESNPC* ChildVisualReconciler::GetOriginalAdultBase(RE::FormID childActorId) const {
        if (const auto it = originalAdultBases_.find(childActorId); it != originalAdultBases_.end()) {
            return it->second;
        }
        return nullptr;
    }

    void ChildVisualReconciler::ReleaseRuntimeBaseForActor(RE::FormID childActorId) {
        if (childActorId == 0) {
            return;
        }
        const auto origIt = originalAdultBases_.find(childActorId);
        const auto cloneIt = clonedAdultBases_.find(childActorId);
        if (cloneIt != clonedAdultBases_.end() && cloneIt->second) {
            RE::TESNPC* cloneBase = cloneIt->second;
            if (origIt != originalAdultBases_.end() && origIt->second) {
                if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(childActorId); actor && !actor->IsDeleted()) {
                    if (actor->data.objectReference == cloneBase) {
                        actor->data.objectReference = origIt->second;
                    }
                }
            }
            retiredClonedBases_.push_back(cloneBase);
        }
        if (origIt != originalAdultBases_.end()) {
            originalAdultBases_.erase(origIt);
        }
        if (cloneIt != clonedAdultBases_.end()) {
            clonedAdultBases_.erase(cloneIt);
        }
    }

    void ChildVisualReconciler::FlushRetiredForms() {
        for (auto* cloneBase : retiredClonedBases_) {
            if (!cloneBase) continue;
            if (cloneBase->perks) {
                RE::free(cloneBase->perks);
                cloneBase->perks = nullptr;
                cloneBase->perkCount = 0;
            }
            if (cloneBase->keywords) {
                RE::free(cloneBase->keywords);
                cloneBase->keywords = nullptr;
                cloneBase->numKeywords = 0;
            }
            if (cloneBase->headRelatedData) {
                RE::free(cloneBase->headRelatedData);
                cloneBase->headRelatedData = nullptr;
            }
            cloneBase->fullName = RE::BSFixedString();
            RE::free(cloneBase);
        }
        retiredClonedBases_.clear();

        for (auto* runtimeClass : retiredClasses_) {
            if (!runtimeClass) continue;
            runtimeClass->fullName = RE::BSFixedString();
            RE::free(runtimeClass);
        }
        retiredClasses_.clear();
    }

    void ChildVisualReconciler::RevertRuntimeBases() {
        for (const auto& [actorId, origBase] : originalAdultBases_) {
            if (!origBase) continue;
            const auto cloneIt = clonedAdultBases_.find(actorId);
            RE::TESNPC* cloneBase = (cloneIt != clonedAdultBases_.end()) ? cloneIt->second : nullptr;
            if (!cloneBase) continue;
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorId); actor && !actor->IsDeleted()) {
                if (actor->data.objectReference == cloneBase) {
                    actor->data.objectReference = origBase;
                }
            }
        }
        for (const auto& [actorId, cloneBase] : clonedAdultBases_) {
            (void)actorId;
            if (cloneBase) {
                retiredClonedBases_.push_back(cloneBase);
            }
        }
        originalAdultBases_.clear();
        clonedAdultBases_.clear();
    }

    void ChildVisualReconciler::ClearCachedCatalogForms() {
        for (auto& cache : classCaches_) {
            if (cache.runtimeClass) {
                retiredClasses_.push_back(cache.runtimeClass);
                cache.runtimeClass = nullptr;
            }
            cache.combatStyle = nullptr;
            cache.outfit = nullptr;
        }
        classCaches_.clear();
    }

    void ChildVisualReconciler::Revert() {
        RevertRuntimeBases();
        ClearCachedCatalogForms();
    }

    bool ChildVisualReconciler::EvaluateChildBioLineageAtBirth(State::ChildRecord& record) {
        if (record.bioLineageEvaluated) {
            return false;
        }
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto bioM = BuildParentLineageInput(*worldSnap, record.bioMother.raw());
        const auto bioF = BuildParentLineageInput(*worldSnap, record.bioFather.raw());
        Domain::Lineage::EvaluateChildBioLineageAtBirth(
            record,
            bioM ? &(*bioM) : nullptr,
            bioF ? &(*bioF) : nullptr);

        MARAS_LOG_INFO(
            "[ChildTraining] Evaluated bio-parent skill snapshot for child {:08X} ('{}'): bioMother={:08X}, bioFather={:08X}",
            record.childId.raw(), record.name, record.bioMother.raw(), record.bioFather.raw());
        return record.bioLineageEvaluated;
    }

    bool ChildVisualReconciler::EvaluateChildGuardianLineageAtGraduation(State::ChildRecord& record) {
        const bool prevBio = record.bioLineageEvaluated;
        const bool prevGuard = record.guardianLineageEvaluated;
        const std::string prevBlessing = record.daedricBlessingId;

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto bioM = BuildParentLineageInput(*worldSnap, record.bioMother.raw());
        const auto bioF = BuildParentLineageInput(*worldSnap, record.bioFather.raw());

        std::vector<Domain::Lineage::ParentLineageInput> guardians;
        guardians.reserve(2);
        for (const auto gId : { record.guardians.slotA, record.guardians.slotB }) {
            if (auto gIn = BuildParentLineageInput(*worldSnap, gId.raw())) {
                guardians.push_back(*gIn);
            }
        }

        std::vector<std::string> completedDaedricBlessingIds;
        for (const auto& b : Engine::TrainingCatalogLoader::GetSingleton().GetDaedricBlessings()) {
            if (auto* q = ResolveFormFromKeyString<RE::TESQuest>(b.questKey, "Daedric blessing quest")) {
                if (q->IsCompleted()) {
                    completedDaedricBlessingIds.push_back(b.id);
                }
            }
        }

        Domain::Lineage::EvaluateChildGuardianLineageAtGraduation(
            record,
            bioM ? &(*bioM) : nullptr,
            bioF ? &(*bioF) : nullptr,
            guardians,
            completedDaedricBlessingIds);

        if (!prevGuard && record.guardianLineageEvaluated) {
            MARAS_LOG_INFO(
                "[ChildTraining] Evaluated guardian mentorship skill snapshot for child {:08X} ('{}'): guardianA={:08X}, guardianB={:08X}",
                record.childId.raw(), record.name, record.guardians.slotA.raw(), record.guardians.slotB.raw());
        }
        return (record.bioLineageEvaluated != prevBio) ||
               (record.guardianLineageEvaluated != prevGuard) ||
               (record.daedricBlessingId != prevBlessing);
    }

    RE::TESNPC* ChildVisualReconciler::EnsurePerActorRuntimeBase(RE::Actor* actor, State::ChildRecord& record) {
        if (!actor || record.stage != State::ChildStage::Adult || record.childId.IsSynthetic()) {
            return actor ? actor->GetActorBase() : nullptr;
        }

        const RE::FormID childId = record.childId.raw();
        RE::TESNPC* origNpc = nullptr;
        if (const auto origIt = originalAdultBases_.find(childId); origIt != originalAdultBases_.end() && origIt->second) {
            origNpc = origIt->second;
        } else {
            origNpc = actor->GetActorBase();
            if (origNpc) {
                originalAdultBases_[childId] = origNpc;
            }
        }
        if (!origNpc) {
            return nullptr;
        }

        RE::TESNPC* cloneNpc = nullptr;
        if (const auto cloneIt = clonedAdultBases_.find(childId); cloneIt != clonedAdultBases_.end() && cloneIt->second) {
            cloneNpc = cloneIt->second;
        } else {
            cloneNpc = RE::calloc<RE::TESNPC>(1);
            if (!cloneNpc) {
                return origNpc;
            }
            std::memcpy(cloneNpc, origNpc, sizeof(RE::TESNPC));

            if (origNpc->perks && origNpc->perkCount > 0) {
                auto* newPerks = RE::calloc<RE::PerkRankData>(origNpc->perkCount);
                if (newPerks) {
                    std::memcpy(newPerks, origNpc->perks, sizeof(RE::PerkRankData) * origNpc->perkCount);
                }
                cloneNpc->perks = newPerks;
                cloneNpc->perkCount = newPerks ? origNpc->perkCount : 0;
            } else {
                cloneNpc->perks = nullptr;
                cloneNpc->perkCount = 0;
            }

            if (origNpc->keywords && origNpc->numKeywords > 0) {
                auto* newKeywords = RE::calloc<RE::BGSKeyword*>(origNpc->numKeywords);
                if (newKeywords) {
                    std::memcpy(newKeywords, origNpc->keywords, sizeof(RE::BGSKeyword*) * origNpc->numKeywords);
                }
                cloneNpc->keywords = newKeywords;
                cloneNpc->numKeywords = newKeywords ? origNpc->numKeywords : 0;
            } else {
                cloneNpc->keywords = nullptr;
                cloneNpc->numKeywords = 0;
            }

            auto* newHrd = RE::calloc<RE::TESNPC::HeadRelatedData>(1);
            if (newHrd && origNpc->headRelatedData) {
                *newHrd = *origNpc->headRelatedData;
            }
            cloneNpc->headRelatedData = newHrd;

            // Prevent template container weapons (CNTO) and template spells (SPLO) from bleeding onto this actor
            cloneNpc->containerObjects = nullptr;
            cloneNpc->numContainerObjects = 0;
            cloneNpc->actorEffects = nullptr;
            cloneNpc->sleepOutfit = nullptr;
            cloneNpc->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kAutoCalcStats);
            cloneNpc->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kPCLevelMult);

            const char* origNpcName = origNpc->GetName();
            std::memset(&cloneNpc->fullName, 0, sizeof(RE::BSFixedString));
            cloneNpc->fullName = RE::BSFixedString(
                !record.name.empty() ? record.name.c_str() : (origNpcName ? origNpcName : ""));

            clonedAdultBases_[childId] = cloneNpc;
        }

        const auto& catalog = Engine::TrainingCatalogLoader::GetSingleton().GetCatalog();
        if (catalog.GetClassCount() > 0) {
            EvaluateChildGuardianLineageAtGraduation(record);

            const std::int32_t trainIdx = ResolveTrainingClassIndexForRecord(record, catalog);
            const auto* classSpec = catalog.FindClass(trainIdx);
            if (classSpec) {
                if (auto* tClass = GetTrainingClass(trainIdx)) {
                    cloneNpc->npcClass = tClass;
                }
                if (auto* cs = GetCombatStyle(trainIdx)) {
                    cloneNpc->combatStyle = cs;
                }
                if (auto* outfit = GetClassOutfit(trainIdx)) {
                    cloneNpc->defaultOutfit = outfit;
                }
                cloneNpc->sleepOutfit = nullptr;

                const std::uint16_t effectiveLevel = GetEffectiveChildLevel();
                const float levelFactor =
                    std::clamp((static_cast<float>(effectiveLevel) - 10.0f) / 40.0f, 0.0f, 1.0f);
                cloneNpc->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kPCLevelMult);
                cloneNpc->actorData.level = effectiveLevel;

                std::unordered_map<RE::ActorValue, float> skillMap;
                for (RE::ActorValue av : kAllSkills) {
                    skillMap[av] = 15.0f;
                }
                if (auto* race = origNpc->race) {
                    for (const auto& boost : race->data.skillBoosts) {
                        const auto av = boost.skill.get();
                        if (skillMap.contains(av)) {
                            skillMap[av] += static_cast<float>(boost.bonus);
                        }
                    }
                }
                for (RE::ActorValue av : kAllSkills) {
                    const int sIdx = static_cast<int>(av) - static_cast<int>(RE::ActorValue::kOneHanded);
                    if (sIdx >= 0 && sIdx < 18) {
                        skillMap[av] += static_cast<float>(record.bioSkillBonuses[sIdx]);
                        skillMap[av] += static_cast<float>(record.guardianSkillBonuses[sIdx]);
                    }
                }
                const float pointsPerWeight = 5.0f + 11.0f * levelFactor;
                for (const auto& sg : classSpec->skills) {
                    const float normWeight = Domain::Lineage::NormalizeClassSkillWeight(sg.bonus);
                    skillMap[SkillIndexToActorValue(sg.skill)] += normWeight * pointsPerWeight;
                }

                float daedricBonusHealth = 0.0f;
                float daedricBonusMagicka = 0.0f;
                float daedricBonusStamina = 0.0f;
                if (const auto* chosenBlessing = catalog.FindDaedricBlessingById(record.daedricBlessingId)) {
                    for (const auto& sb : chosenBlessing->skills) {
                        skillMap[SkillIndexToActorValue(sb.skill)] += sb.bonus;
                    }
                    daedricBonusHealth += chosenBlessing->bonusHealth;
                    daedricBonusMagicka += chosenBlessing->bonusMagicka;
                    daedricBonusStamina += chosenBlessing->bonusStamina;
                }

                for (RE::ActorValue av : kAllSkills) {
                    const int sIdx = static_cast<int>(av) - static_cast<int>(RE::ActorValue::kOneHanded);
                    if (sIdx >= 0 && sIdx < RE::TESNPC::Skills::kTotal) {
                        const float clamped = std::clamp(skillMap[av], 15.0f, 100.0f);
                        cloneNpc->playerSkills.values[sIdx] = static_cast<std::uint8_t>(std::lround(clamped));
                        cloneNpc->playerSkills.offsets[sIdx] = 0;
                    }
                }

                const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
                const auto& classHMS = classSpec->hmsWeights;
                std::array<float, 3> bioHMS = { 0.50f, 0.25f, 0.25f };
                int bioCount = 0;
                std::array<float, 3> bioSum = { 0.0f, 0.0f, 0.0f };
                for (const RE::FormID bioParentId : { record.bioMother.raw(), record.bioFather.raw() }) {
                    if (bioParentId == 0 || bioParentId == kUnknownFatherFormID) continue;
                    const auto w = Domain::Lineage::GetHMSWeightsForSkillType(
                        ResolveActorEffectiveSkillType(*worldSnap, bioParentId));
                    bioSum[0] += w[0];
                    bioSum[1] += w[1];
                    bioSum[2] += w[2];
                    ++bioCount;
                }
                if (bioCount > 0) {
                    bioHMS[0] = bioSum[0] / static_cast<float>(bioCount);
                    bioHMS[1] = bioSum[1] / static_cast<float>(bioCount);
                    bioHMS[2] = bioSum[2] / static_cast<float>(bioCount);
                }
                const float hWeight = 0.85f * classHMS[0] + 0.15f * bioHMS[0];
                const float mWeight = 0.85f * classHMS[1] + 0.15f * bioHMS[1];
                const float sWeight = 0.85f * classHMS[2] + 0.15f * bioHMS[2];
                const float totalBonusPool = 80.0f + 320.0f * levelFactor;
                const float beastbloodHealthBonus = record.hasBeastblood ? 25.0f : 0.0f;
                const float beastbloodStaminaBonus = record.hasBeastblood ? 50.0f : 0.0f;

                const float finalHealth =
                    std::round(100.0f + totalBonusPool * hWeight + daedricBonusHealth + beastbloodHealthBonus);
                const float finalMagicka = std::round(100.0f + totalBonusPool * mWeight + daedricBonusMagicka);
                const float finalStamina =
                    std::round(100.0f + totalBonusPool * sWeight + daedricBonusStamina + beastbloodStaminaBonus);

                cloneNpc->playerSkills.health = static_cast<std::uint16_t>(std::clamp(finalHealth, 50.0f, 2000.0f));
                cloneNpc->playerSkills.magicka = static_cast<std::uint16_t>(std::clamp(finalMagicka, 50.0f, 2000.0f));
                cloneNpc->playerSkills.stamina = static_cast<std::uint16_t>(std::clamp(finalStamina, 50.0f, 2000.0f));

                float confidence = 2.0f;
                float morality = 3.0f;
                int guardAICount = 0;
                float guardConfSum = 0.0f;
                float guardMoralSum = 0.0f;
                for (const RE::FormID gId : { record.guardians.slotA.raw(), record.guardians.slotB.raw() }) {
                    if (gId == 0 || Utils::MatchesActor(gId, kPlayerFormID)) continue;
                    if (auto* gActor = ResolveActorPtr(gId)) {
                        if (auto* gAV = gActor->AsActorValueOwner()) {
                            guardConfSum += gAV->GetBaseActorValue(RE::ActorValue::kConfidence);
                            guardMoralSum += gAV->GetBaseActorValue(RE::ActorValue::kMorality);
                            ++guardAICount;
                        }
                    }
                }
                if (guardAICount > 0) {
                    confidence = std::clamp(std::round(guardConfSum / static_cast<float>(guardAICount)), 1.0f, 4.0f);
                    morality = std::clamp(std::round(guardMoralSum / static_cast<float>(guardAICount)), 0.0f, 3.0f);
                }
                if (classSpec->moralityOverride >= 0.0f) {
                    morality = classSpec->moralityOverride;
                }
                confidence = std::max(confidence, classSpec->minConfidence);

                cloneNpc->SetAggressionLevel(RE::ACTOR_AGGRESSION::kAggressive);
                cloneNpc->SetConfidenceLevel(static_cast<RE::ACTOR_CONFIDENCE>(static_cast<int>(std::lround(confidence))));
                cloneNpc->SetAssistanceLevel(RE::ACTOR_ASSISTANCE::kHelpsFriends);
                const int moralInt = std::clamp(static_cast<int>(std::lround(morality)), 0, 3);
                cloneNpc->aiData.morality1 = (moralInt & 1) != 0;
                cloneNpc->aiData.morality2 = (moralInt & 2) != 0;
            }
        }

        if (!record.hairColorFormId.IsNull() && cloneNpc->headRelatedData) {
            if (auto* hc = RE::TESForm::LookupByID<RE::BGSColorForm>(record.hairColorFormId.raw())) {
                cloneNpc->headRelatedData->hairColor = hc;
            }
        }

        actor->data.objectReference = cloneNpc;
        return cloneNpc;
    }

    void ChildVisualReconciler::InitializeAndApplyChildVisualsAndRPG(RE::FormID childId) {
        if (!HasPhysicalActor(childId)) {
            return;
        }
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto* childPtr = worldSnap->lineage->FindChild(State::ChildId{ childId });
        if (!childPtr) {
            MARAS_LOG_WARN("[ChildTraining] InitializeAndApplyChildVisualsAndRPG: child {:08X} not found in lineage",
                           childId);
            return;
        }
        State::ChildRecord record = *childPtr;

        auto* actor = ResolveActorPtr(childId);
        if (!actor) {
            MARAS_LOG_WARN(
                "[ChildTraining] InitializeAndApplyChildVisualsAndRPG: failed to resolve Actor* for child {:08X} ('{}')",
                childId, record.name);
            return;
        }
        auto* childNpc = actor->GetActorBase();
        if (!childNpc) {
            MARAS_LOG_WARN(
                "[ChildTraining] InitializeAndApplyChildVisualsAndRPG: Actor {:08X} ('{}') has null ActorBase",
                childId, record.name);
            return;
        }

        const std::uint32_t seed = childId ^ (record.bioMother.raw() << 1) ^ (record.bioFather.raw() << 2) ^
                                   static_cast<std::uint32_t>(std::hash<std::string>{}(record.name));
        std::mt19937 rng(seed);

        auto* motherNpc = GetActorBaseNPC(record.bioMother.raw());
        auto* fatherNpc = GetActorBaseNPC(record.bioFather.raw());

        // 1. PILLAR 1: BIOLOGICAL PARENTS — VISUAL GENETICS
        if (record.hairColorFormId.IsNull()) {
            std::vector<RE::FormID> parentHairColors;
            if (motherNpc && motherNpc->headRelatedData && motherNpc->headRelatedData->hairColor) {
                parentHairColors.push_back(motherNpc->headRelatedData->hairColor->GetFormID());
            }
            if (fatherNpc && fatherNpc->headRelatedData && fatherNpc->headRelatedData->hairColor) {
                parentHairColors.push_back(fatherNpc->headRelatedData->hairColor->GetFormID());
            }
            if (!parentHairColors.empty()) {
                std::uniform_int_distribution<std::size_t> dist(0, parentHairColors.size() - 1);
                record.hairColorFormId = State::FormRawId{ parentHairColors[dist(rng)] };
            } else if (childNpc->headRelatedData && childNpc->headRelatedData->hairColor) {
                record.hairColorFormId = State::FormRawId{ childNpc->headRelatedData->hairColor->GetFormID() };
            }
        }

        if (record.stage == State::ChildStage::Adult) {
            auto* childRace = actor->GetRace();
            const bool isFemale = (childNpc->GetSex() == RE::SEX::kFemale);

            if (!record.rpgApplied && record.heightScale == 1.0f) {
                float sumHeight = 0.0f;
                int heightCount = 0;
                if (motherNpc && motherNpc->height > 0.5f && motherNpc->height < 1.5f) {
                    sumHeight += motherNpc->height;
                    ++heightCount;
                }
                if (fatherNpc && fatherNpc->height > 0.5f && fatherNpc->height < 1.5f) {
                    sumHeight += fatherNpc->height;
                    ++heightCount;
                }
                const float baseHeight = (heightCount > 0) ? (sumHeight / static_cast<float>(heightCount)) : 1.0f;
                std::uniform_real_distribution<float> jitterDist(-0.03f, 0.03f);
                record.heightScale = std::clamp(baseHeight + jitterDist(rng), 0.94f, 1.06f);
            }

            std::vector<RE::BGSHeadPart*> validEyes;
            std::vector<RE::BGSHeadPart*> validHairs;
            std::vector<RE::BGSHeadPart*> validBeards;

            if (auto* dataHandler = RE::TESDataHandler::GetSingleton(); dataHandler && childRace) {
                for (auto* part : dataHandler->GetFormArray<RE::BGSHeadPart>()) {
                    if (IsHeadPartValidForChild(part, childRace, isFemale, RE::BGSHeadPart::HeadPartType::kEyes, true)) {
                        validEyes.push_back(part);
                    } else if (IsHeadPartValidForChild(part, childRace, isFemale, RE::BGSHeadPart::HeadPartType::kHair, true)) {
                        validHairs.push_back(part);
                    } else if (!isFemale && IsHeadPartValidForChild(part, childRace, false,
                                                                    RE::BGSHeadPart::HeadPartType::kFacialHair, true)) {
                        validBeards.push_back(part);
                    }
                }
            }

            if (record.eyesPartFormId.IsNull() && childRace) {
                std::array<RE::TESNPC*, 2> donors = { motherNpc, fatherNpc };
                if (std::uniform_int_distribution<int>(0, 1)(rng) == 1) {
                    std::swap(donors[0], donors[1]);
                }
                for (auto* donor : donors) {
                    if (!donor) continue;
                    auto* donorEyes = donor->GetCurrentHeadPartByType(RE::BGSHeadPart::HeadPartType::kEyes);
                    if (!donorEyes) continue;
                    if (IsHeadPartValidForChild(donorEyes, childRace, isFemale, RE::BGSHeadPart::HeadPartType::kEyes, false)) {
                        record.eyesPartFormId = State::FormRawId{ donorEyes->GetFormID() };
                        break;
                    }
                    const std::string_view colorToken = ExtractEyeColorKeyword(donorEyes->formEditorID.c_str());
                    if (!colorToken.empty()) {
                        for (auto* candidate : validEyes) {
                            if (ContainsInsensitive(candidate->formEditorID.c_str(), colorToken)) {
                                record.eyesPartFormId = State::FormRawId{ candidate->GetFormID() };
                                break;
                            }
                        }
                        if (!record.eyesPartFormId.IsNull()) break;
                    }
                }
                if (record.eyesPartFormId.IsNull() && !validEyes.empty()) {
                    std::uniform_int_distribution<std::size_t> dist(0, validEyes.size() - 1);
                    record.eyesPartFormId = State::FormRawId{ validEyes[dist(rng)]->GetFormID() };
                }
            }

            if (record.hairPartFormId.IsNull() && childRace) {
                auto* sameSexParent = isFemale ? motherNpc : fatherNpc;
                if (sameSexParent && std::uniform_int_distribution<int>(1, 100)(rng) <= 35) {
                    auto* parentHair = sameSexParent->GetCurrentHeadPartByType(RE::BGSHeadPart::HeadPartType::kHair);
                    if (IsHeadPartValidForChild(parentHair, childRace, isFemale, RE::BGSHeadPart::HeadPartType::kHair, false)) {
                        record.hairPartFormId = State::FormRawId{ parentHair->GetFormID() };
                    }
                }
                if (record.hairPartFormId.IsNull() && !validHairs.empty()) {
                    std::uniform_int_distribution<std::size_t> dist(0, validHairs.size() - 1);
                    record.hairPartFormId = State::FormRawId{ validHairs[dist(rng)]->GetFormID() };
                }
            }

            if (!isFemale && record.facialHairPartFormId.IsNull() && childRace &&
                childNpc->GetCurrentHeadPartByType(RE::BGSHeadPart::HeadPartType::kFacialHair) != nullptr) {
                if (fatherNpc && std::uniform_int_distribution<int>(1, 100)(rng) <= 45) {
                    auto* fatherBeard = fatherNpc->GetCurrentHeadPartByType(RE::BGSHeadPart::HeadPartType::kFacialHair);
                    if (IsHeadPartValidForChild(fatherBeard, childRace, false,
                                                RE::BGSHeadPart::HeadPartType::kFacialHair, false)) {
                        record.facialHairPartFormId = State::FormRawId{ fatherBeard->GetFormID() };
                    }
                }
                if (record.facialHairPartFormId.IsNull() && !validBeards.empty()) {
                    std::uniform_int_distribution<std::size_t> dist(0, validBeards.size() - 1);
                    record.facialHairPartFormId = State::FormRawId{ validBeards[dist(rng)]->GetFormID() };
                }
            }

            if (!record.beastbloodEvaluated) {
                ChildSpawnService::EvaluateChildBeastbloodInheritance(record);
            }

            childNpc = EnsurePerActorRuntimeBase(actor, record);
        }

        // 2. PILLARS 1, 2 & 3 + DAEDRIC LINEAGE: ADULT CHILD RPG PROGRESSION
        std::optional<State::ActorProfileRecord> newAdultProfile;
        const auto& catalog = Engine::TrainingCatalogLoader::GetSingleton().GetCatalog();
        if (record.stage == State::ChildStage::Adult && !record.rpgApplied && catalog.GetClassCount() == 0) {
            MARAS_LOG_WARN("[ChildTraining] Cannot apply adult child RPG progression for {:08X} ('{}'): training catalog is empty!",
                           childId, record.name);
        }
        if (record.stage == State::ChildStage::Adult && !record.rpgApplied && catalog.GetClassCount() > 0) {
            Foundation::SocialClass inheritedSocialClass = Foundation::SocialClass::Poverty;
            bool foundGuardian = false;
            Foundation::SkillType guardianFallbackSkillType = Foundation::SkillType::Warrior;

            for (const RE::FormID gId : { record.guardians.slotA.raw(), record.guardians.slotB.raw() }) {
                if (gId == 0) continue;
                Foundation::SocialClass gSocialClass = Foundation::SocialClass::Middle;
                if (Utils::MatchesActor(gId, kPlayerFormID)) {
                    gSocialClass = (!worldSnap->household->AllHouses().empty())
                                       ? Foundation::SocialClass::Wealthy
                                       : Foundation::SocialClass::Middle;
                } else {
                    gSocialClass = GetSocialClassFromWorld(*worldSnap, gId);
                }

                if (!foundGuardian ||
                    Domain::Lineage::GetSocialClassRankOrder(gSocialClass) >
                        Domain::Lineage::GetSocialClassRankOrder(inheritedSocialClass)) {
                    inheritedSocialClass = gSocialClass;
                }
                if (!foundGuardian) {
                    guardianFallbackSkillType = ResolveActorEffectiveSkillType(*worldSnap, gId);
                }
                foundGuardian = true;
            }

            const std::int32_t trainIdx = ResolveTrainingClassIndexForRecord(record, catalog);
            const auto* classSpec = catalog.FindClass(trainIdx);
            const Foundation::SkillType childSkillType =
                (catalog.IsValidClassIndex(record.trainingClassIndex) && classSpec)
                    ? classSpec->mappedSkillType
                    : guardianFallbackSkillType;
            const Foundation::Temperament childTemperament =
                NPCTypeDeterminer::ComputeTemperament(inheritedSocialClass, childSkillType);

            State::ActorProfileRecord profileRec{};
            if (const auto* existingProf = worldSnap->actors->Find(State::RefId{ childId })) {
                profileRec = *existingProf;
            }
            profileRec.refId = State::RefId{ childId };
            profileRec.baseId = State::BaseId{ childNpc ? childNpc->GetFormID() : 0 };
            profileRec.socialClass = inheritedSocialClass;
            profileRec.skillType = childSkillType;
            profileRec.temperament = childTemperament;
            profileRec.isRegisteredAdult = true;
            newAdultProfile = std::move(profileRec);

            std::string inheritedDaedricPatron;
            const float finalHealth = childNpc ? static_cast<float>(childNpc->playerSkills.health) : 100.0f;
            const float finalMagicka = childNpc ? static_cast<float>(childNpc->playerSkills.magicka) : 100.0f;
            const float finalStamina = childNpc ? static_cast<float>(childNpc->playerSkills.stamina) : 100.0f;
            const float finalConfidence =
                childNpc ? static_cast<float>(std::to_underlying(childNpc->GetConfidenceLevel())) : 2.0f;
            const float finalMorality =
                childNpc ? static_cast<float>((childNpc->aiData.morality1 ? 1 : 0) | (childNpc->aiData.morality2 ? 2 : 0))
                         : 3.0f;
            std::size_t daedricSpellsAdded = 0;

            if (const auto* chosenBlessing = catalog.FindDaedricBlessingById(record.daedricBlessingId)) {
                for (const auto& spKey : chosenBlessing->spellKeys) {
                    if (auto* spell = ResolveFormFromKeyString<RE::SpellItem>(spKey, "Daedric blessing spell")) {
                        if (!actor->HasSpell(spell)) {
                            actor->AddSpell(spell);
                            ++daedricSpellsAdded;
                        }
                    }
                }
                inheritedDaedricPatron = chosenBlessing->name;
            }

            if (auto* avOwner = actor->AsActorValueOwner(); avOwner && childNpc) {
                avOwner->SetBaseActorValue(RE::ActorValue::kHealth, finalHealth);
                avOwner->SetBaseActorValue(RE::ActorValue::kMagicka, finalMagicka);
                avOwner->SetBaseActorValue(RE::ActorValue::kStamina, finalStamina);
                for (RE::ActorValue av : kAllSkills) {
                    const int sIdx = static_cast<int>(av) - static_cast<int>(RE::ActorValue::kOneHanded);
                    if (sIdx >= 0 && sIdx < RE::TESNPC::Skills::kTotal) {
                        avOwner->SetBaseActorValue(av, static_cast<float>(childNpc->playerSkills.values[sIdx]));
                    }
                }
                record.lastAppliedLevel = childNpc->actorData.level;
            }

            std::size_t classSpellsAdded = 0;
            for (auto* spell : GetClassSpells(trainIdx)) {
                if (spell && !actor->HasSpell(spell)) {
                    actor->AddSpell(spell);
                    ++classSpellsAdded;
                }
            }

            if (record.hasBeastblood) {
                auto& fc = FormCache::GetSingleton();
                if (auto* wwImmunity = fc.GetWerewolfImmunitySpell(); wwImmunity && !actor->HasSpell(wwImmunity)) {
                    actor->AddSpell(wwImmunity);
                }
                if (auto* wwChange = fc.GetWerewolfChangeSpell(); wwChange && !actor->HasSpell(wwChange)) {
                    actor->AddSpell(wwChange);
                }
            }

            actor->InitInventoryIfRequired();

            auto* origNpc = GetOriginalAdultBase(childId);
            if (origNpc && origNpc->defaultOutfit) {
                actor->RemoveOutfitItems(origNpc->defaultOutfit);
            }

            std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> templateGearToStrip;
            for (const auto& [boundObj, invEntry] : actor->GetInventory()) {
                if (!boundObj || invEntry.first <= 0) continue;
                if (boundObj->IsWeapon() || boundObj->IsArmor() || boundObj->IsAmmo()) {
                    templateGearToStrip.emplace_back(boundObj, invEntry.first);
                }
            }
            for (const auto& [boundObj, count] : templateGearToStrip) {
                actor->RemoveItem(boundObj, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
            }

            auto* classOutfit = GetClassOutfit(trainIdx);
            if (classOutfit) {
                if (!actor->IsDisabled()) {
                    actor->AddWornOutfit(classOutfit, true);
                }
            } else if (classSpec && !classSpec->outfitKeys.empty()) {
                MARAS_LOG_WARN("[ChildTraining] Failed to resolve class outfit for adult child {:08X} ('{}', class='{}')",
                               childId, record.name, classSpec->className);
            }

            auto* equipMgr = RE::ActorEquipManager::GetSingleton();
            std::size_t itemsGranted = 0;
            for (const auto& [item, count] : GetClassEquipment(trainIdx)) {
                if (!item || count <= 0) continue;
                actor->AddObjectToContainer(item, nullptr, count, nullptr);
                if (equipMgr && (item->IsWeapon() || item->IsArmor() || item->IsAmmo())) {
                    equipMgr->EquipObject(actor, item);
                }
                ++itemsGranted;
            }

            std::int32_t goldAmount = 0;
            if (auto* goldObj = FormCache::GetSingleton().GetGoldBaseObject()) {
                switch (inheritedSocialClass) {
                    case Foundation::SocialClass::Outcast:
                    case Foundation::SocialClass::Poverty:
                        goldAmount = 25;
                        break;
                    case Foundation::SocialClass::Working:
                        goldAmount = 50;
                        break;
                    case Foundation::SocialClass::Middle:
                    case Foundation::SocialClass::Religious:
                        goldAmount = 100;
                        break;
                    case Foundation::SocialClass::Wealthy:
                        goldAmount = 250;
                        break;
                    case Foundation::SocialClass::Nobles:
                    case Foundation::SocialClass::Rulers:
                        goldAmount = 500;
                        break;
                    default:
                        goldAmount = 50;
                        break;
                }
                actor->AddObjectToContainer(goldObj, nullptr, goldAmount, nullptr);
            }

            record.rpgApplied = true;
            MARAS_LOG_INFO(
                "[ChildTraining] Applied 3-pillar RPG progression to adult child {:08X} ('{}', lv={}): class='{}' (idx={}, loc='{}'), "
                "social={}, skill={}, temp={}, HMS={:.0f}/{:.0f}/{:.0f}, AI(conf={:.0f}, moral={:.0f}), "
                "outfit={:08X}, items={}, spells={} (+{} Daedric), gold={}, daedricPatron='{}', hasBeastblood={}, "
                "visuals(height={:.2f}, hairColor={:08X}, eyes={:08X}, hair={:08X}, beard={:08X})",
                childId, record.name, childNpc ? childNpc->actorData.level : GetEffectiveChildLevel(),
                classSpec ? classSpec->className : "", trainIdx, classSpec ? classSpec->locationName : "",
                Foundation::SocialClassToString(inheritedSocialClass),
                Foundation::SkillTypeToString(childSkillType),
                Foundation::TemperamentToString(childTemperament),
                finalHealth, finalMagicka, finalStamina,
                finalConfidence, finalMorality, classOutfit ? classOutfit->GetFormID() : 0, itemsGranted,
                classSpellsAdded, daedricSpellsAdded, goldAmount,
                inheritedDaedricPatron.empty() ? "None" : inheritedDaedricPatron, record.hasBeastblood,
                record.heightScale, record.hairColorFormId.raw(), record.eyesPartFormId.raw(),
                record.hairPartFormId.raw(), record.facialHairPartFormId.raw());
        }

        ApplyChildVisualsToActor(actor, record);

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            ws.lineage.Write().UpsertChild(record);
            if (newAdultProfile.has_value()) {
                ws.actors.Write().Upsert(*newAdultProfile);
            }
        });

        FactionReconciler::GetSingleton().ReconcileLoadedActor(actor);
    }

    void ChildVisualReconciler::ApplyChildVisualsToActor(RE::Actor* actor, State::ChildRecord& record) {
        if (!actor || record.childId.IsSynthetic()) {
            return;
        }
        auto* npc = (record.stage == State::ChildStage::Adult)
                        ? EnsurePerActorRuntimeBase(actor, record)
                        : actor->GetActorBase();
        if (!npc) {
            return;
        }

        if (record.stage == State::ChildStage::Adult) {
            if (record.heightScale > 0.5f && record.heightScale < 1.5f &&
                std::abs(actor->GetScale() - record.heightScale) > 0.005f) {
                actor->SetScale(record.heightScale);
            }

            const auto& catalog = Engine::TrainingCatalogLoader::GetSingleton().GetCatalog();
            const std::int32_t trainIdx = ResolveTrainingClassIndexForRecord(record, catalog);

            if (auto* cs = GetCombatStyle(trainIdx)) {
                auto* xCombatStyle = actor->extraList.GetByType<RE::ExtraCombatStyle>();
                if (!xCombatStyle) {
                    xCombatStyle = RE::BSExtraData::Create<RE::ExtraCombatStyle>();
                    if (xCombatStyle) {
                        xCombatStyle->combatStyle = cs;
                        actor->extraList.Add(xCombatStyle);
                    }
                } else {
                    xCombatStyle->combatStyle = cs;
                }
            }

            const std::uint16_t effectiveLevel = npc->actorData.level;
            if (auto* avOwner = actor->AsActorValueOwner()) {
                if (record.lastAppliedLevel != effectiveLevel) {
                    const std::uint16_t prevLevel = record.lastAppliedLevel;
                    avOwner->SetBaseActorValue(RE::ActorValue::kHealth, static_cast<float>(npc->playerSkills.health));
                    avOwner->SetBaseActorValue(RE::ActorValue::kMagicka, static_cast<float>(npc->playerSkills.magicka));
                    avOwner->SetBaseActorValue(RE::ActorValue::kStamina, static_cast<float>(npc->playerSkills.stamina));
                    for (RE::ActorValue av : kAllSkills) {
                        const int sIdx = static_cast<int>(av) - static_cast<int>(RE::ActorValue::kOneHanded);
                        if (sIdx >= 0 && sIdx < RE::TESNPC::Skills::kTotal) {
                            avOwner->SetBaseActorValue(av, static_cast<float>(npc->playerSkills.values[sIdx]));
                        }
                    }
                    record.lastAppliedLevel = effectiveLevel;
                    if (prevLevel != 0) {
                        MARAS_LOG_INFO(
                            "[ChildTraining] Scaled adult child {:08X} ('{}') level {} -> {}: HMS={}/{}/{}",
                            record.childId.raw(), record.name, prevLevel, effectiveLevel,
                            npc->playerSkills.health, npc->playerSkills.magicka, npc->playerSkills.stamina);
                    }
                }
            }

            auto* origNpc = GetOriginalAdultBase(record.childId.raw());
            auto* classOutfit = GetClassOutfit(trainIdx);
            if (origNpc && origNpc->defaultOutfit && origNpc->defaultOutfit != classOutfit) {
                actor->RemoveOutfitItems(origNpc->defaultOutfit);
            }

            const auto classEquip = GetClassEquipment(trainIdx);
            auto isClassEquipmentItem = [&](RE::TESBoundObject* candidate) {
                for (const auto& [eqObj, eqCount] : classEquip) {
                    (void)eqCount;
                    if (eqObj == candidate) return true;
                }
                return false;
            };
            auto isOrigTemplateItem = [&](RE::TESBoundObject* candidate) {
                if (!origNpc || !origNpc->containerObjects || origNpc->numContainerObjects == 0) return false;
                for (std::uint32_t i = 0; i < origNpc->numContainerObjects; ++i) {
                    if (origNpc->containerObjects[i] && origNpc->containerObjects[i]->obj == candidate) {
                        return true;
                    }
                }
                return false;
            };

            bool hasClassOutfitItems = false;
            std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> foreignGearToRemove;
            for (const auto& [boundObj, invEntry] : actor->GetInventory()) {
                if (!boundObj || invEntry.first <= 0 || !invEntry.second) continue;
                bool hasForeignOutfitTag = false;
                if (invEntry.second->extraLists) {
                    for (auto* xList : *invEntry.second->extraLists) {
                        if (!xList) continue;
                        if (auto* xOutfit = xList->GetByType<RE::ExtraOutfitItem>()) {
                            if (classOutfit && xOutfit->id == classOutfit->GetFormID()) {
                                hasClassOutfitItems = true;
                            } else if (xOutfit->id != 0) {
                                hasForeignOutfitTag = true;
                            }
                        }
                    }
                }
                if (hasForeignOutfitTag || (isOrigTemplateItem(boundObj) && !isClassEquipmentItem(boundObj))) {
                    foreignGearToRemove.emplace_back(boundObj, invEntry.first);
                }
            }
            for (const auto& [boundObj, count] : foreignGearToRemove) {
                actor->RemoveItem(boundObj, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
            }
            if (classOutfit && !hasClassOutfitItems && !actor->IsDisabled()) {
                actor->AddWornOutfit(classOutfit, true);
                if (auto* equipMgr = RE::ActorEquipManager::GetSingleton()) {
                    for (const auto& [item, count] : classEquip) {
                        (void)count;
                        if (item && (item->IsWeapon() || item->IsArmor() || item->IsAmmo())) {
                            equipMgr->EquipObject(actor, item);
                        }
                    }
                }
            }

            if (auto* faceNode = actor->GetFaceNodeSkinned()) {
                auto swapPartIfLoaded = [&](RE::BGSHeadPart::HeadPartType type, RE::FormID targetPartId) {
                    if (targetPartId == 0) return;
                    auto* newPart = RE::TESForm::LookupByID<RE::BGSHeadPart>(targetPartId);
                    if (!newPart || newPart->formEditorID.empty()) return;
                    if (faceNode->GetObjectByName(newPart->formEditorID) != nullptr) {
                        return;
                    }
                    auto* oldPart = npc->GetCurrentHeadPartByType(type);
                    if (oldPart && oldPart != newPart && !oldPart->formEditorID.empty() &&
                        faceNode->GetObjectByName(oldPart->formEditorID) != nullptr) {
                        actor->ChangeHeadPart(oldPart, newPart);
                    }
                };

                swapPartIfLoaded(RE::BGSHeadPart::HeadPartType::kEyes, record.eyesPartFormId.raw());
                swapPartIfLoaded(RE::BGSHeadPart::HeadPartType::kHair, record.hairPartFormId.raw());
                swapPartIfLoaded(RE::BGSHeadPart::HeadPartType::kFacialHair, record.facialHairPartFormId.raw());
            }

            ApplyAdultChildPerks(actor, record);
        }

        if (!record.hairColorFormId.IsNull()) {
            if (auto* hairColor = RE::TESForm::LookupByID<RE::BGSColorForm>(record.hairColorFormId.raw())) {
                if (auto* model = actor->Get3D(false)) {
                    RE::NiColor color;
                    color.red = static_cast<float>(hairColor->color.red) / 128.0f;
                    color.green = static_cast<float>(hairColor->color.green) / 128.0f;
                    color.blue = static_cast<float>(hairColor->color.blue) / 128.0f;
                    model->UpdateHairColor(color);
                }
            }
        }
    }

    void ChildVisualReconciler::ApplyAdultChildPerks(RE::Actor* actor, const State::ChildRecord& record) {
        if (!actor || record.stage != State::ChildStage::Adult || record.childId.IsSynthetic()) {
            return;
        }
        auto* npc = actor->GetActorBase();
        auto* origNpc = GetOriginalAdultBase(record.childId.raw());
        if (!npc || npc == origNpc) {
            return;
        }

        const auto& catalog = Engine::TrainingCatalogLoader::GetSingleton().GetCatalog();
        const std::int32_t trainIdx = ResolveTrainingClassIndexForRecord(record, catalog);

        std::vector<RE::BGSPerk*> qualifiedPerks;
        qualifiedPerks.reserve(24);

        auto addUniqueQualifiedPerk = [&](RE::BGSPerk* perk) {
            if (!perk) return;
            if (std::find(qualifiedPerks.begin(), qualifiedPerks.end(), perk) == qualifiedPerks.end()) {
                qualifiedPerks.push_back(perk);
            }
        };

        const auto allClassPerks = GetClassPerks(trainIdx);
        for (auto* perk : allClassPerks) {
            if (DoesActorMeetPerkRequirements(actor, npc, perk)) {
                addUniqueQualifiedPerk(perk);
            }
        }
        const std::size_t qualifiedClassPerkCount = qualifiedPerks.size();

        if (const auto* chosenBlessing = catalog.FindDaedricBlessingById(record.daedricBlessingId)) {
            for (const auto& pkKey : chosenBlessing->perkKeys) {
                if (auto* perk = ResolveFormFromKeyString<RE::BGSPerk>(pkKey, "Daedric blessing perk")) {
                    addUniqueQualifiedPerk(perk);
                }
            }
        }
        const std::size_t blessingPerkCount = qualifiedPerks.size() - qualifiedClassPerkCount;

        std::vector<RE::BGSPerk*> activeTargetPerks;
        activeTargetPerks.reserve(qualifiedPerks.size());
        for (auto* candidate : qualifiedPerks) {
            bool supersededByHigherRank = false;
            for (auto* other : qualifiedPerks) {
                if (IsEarlierRankInPerkChain(candidate, other)) {
                    supersededByHigherRank = true;
                    break;
                }
            }
            if (!supersededByHigherRank) {
                activeTargetPerks.push_back(candidate);
            }
        }

        std::vector<RE::BGSPerk*> missingPerks;
        missingPerks.reserve(activeTargetPerks.size());
        std::size_t upgradedCount = 0;

        for (auto* targetPerk : activeTargetPerks) {
            if (!targetPerk || npc->GetPerkIndex(targetPerk).has_value()) {
                continue;
            }

            bool handledInChain = false;
            if (npc->perks && npc->perkCount > 0) {
                for (std::uint32_t i = 0; i < npc->perkCount; ++i) {
                    auto* existingPerk = npc->perks[i].perk;
                    if (!existingPerk) continue;
                    if (IsEarlierRankInPerkChain(existingPerk, targetPerk)) {
                        for (auto* perkEntry : existingPerk->perkEntries) {
                            if (perkEntry) {
                                perkEntry->RemovePerkEntry(actor);
                            }
                        }
                        npc->perks[i].perk = targetPerk;
                        npc->perks[i].currentRank = 1;
                        ++upgradedCount;
                        handledInChain = true;
                        break;
                    }
                    if (IsEarlierRankInPerkChain(targetPerk, existingPerk)) {
                        handledInChain = true;
                        break;
                    }
                }
            }

            if (!handledInChain) {
                missingPerks.push_back(targetPerk);
            }
        }

        if (!missingPerks.empty() || upgradedCount > 0) {
            if (!missingPerks.empty()) {
                npc->AddPerks(missingPerks, 1);
            }
            actor->ApplyPerksFromBase();
            MARAS_LOG_INFO(
                "[ChildTraining] Updated perks for adult child {:08X} ('{}', lv={}): +{} new, {} rank-upgraded "
                "(qualified {}/{} class perks + {} Daedric, {} active after rank dedup)",
                record.childId.raw(), record.name, npc->actorData.level, missingPerks.size(), upgradedCount,
                qualifiedClassPerkCount, allClassPerks.size(), blessingPerkCount, activeTargetPerks.size());
        }
    }

    void ChildVisualReconciler::ApplyChildVisualsOn3DLoad(RE::Actor* actor) {
        if (!actor) {
            return;
        }
        const RE::FormID formId = actor->GetFormID();
        std::optional<State::ChildRecord> recOpt;
        {
            const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
            if (const auto* ptr = worldSnap->lineage->FindChild(State::ChildId{ formId })) {
                recOpt = *ptr;
            }
        }
        if (!recOpt.has_value()) {
            return;
        }
        if (recOpt->stage != State::ChildStage::Child && recOpt->stage != State::ChildStage::Adult) {
            return;
        }

        if (recOpt->hairColorFormId.IsNull() ||
            (recOpt->stage == State::ChildStage::Adult && !recOpt->rpgApplied)) {
            InitializeAndApplyChildVisualsAndRPG(formId);
        } else {
            const std::uint16_t prevLevel = recOpt->lastAppliedLevel;
            ApplyChildVisualsToActor(actor, *recOpt);
            if (recOpt->lastAppliedLevel != prevLevel) {
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    if (auto* mutableRec = ws.lineage.Write().FindChildMutable(State::ChildId{ formId })) {
                        mutableRec->lastAppliedLevel = recOpt->lastAppliedLevel;
                    }
                });
            }
        }
    }

    void ChildVisualReconciler::ReassertAllChildVisualsOnLoad() {
        std::vector<RE::FormID> childIds;
        {
            const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
            const auto& allChildren = worldSnap->lineage->AllChildren();
            childIds.reserve(allChildren.size());
            for (const auto& [cid, rec] : allChildren) {
                if (!cid.IsSynthetic() &&
                    (rec.stage == State::ChildStage::Child || rec.stage == State::ChildStage::Adult)) {
                    childIds.push_back(cid.raw());
                }
            }
        }
        for (const RE::FormID cId : childIds) {
            if (auto* actor = ResolveActorPtr(cId)) {
                ApplyChildVisualsOn3DLoad(actor);
            }
        }
    }

}  // namespace MARAS
