#include "core/AdultChildProgression.h"

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
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <unordered_map>

#include "core/FormCache.h"
#include "core/NPCIndex.h"
#include "core/NPCRelationshipManager.h"
#include "core/NPCTypeDeterminer.h"
#include "core/PlayerHouseService.h"
#include "maras/projection/PerspectiveView.h"
#include "services/ActorService.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"
#include "utils/Settings.h"
#include "utils/StringUtils.h"

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

        template <typename T>
        T* ResolveFormFromKeyString(std::string_view rawKey, std::string_view context) {
            std::string_view trimmed = StringUtils::TrimView(rawKey);
            if (trimmed.empty()) {
                return nullptr;
            }
            if (trimmed.starts_with("__formData|")) {
                auto parsed = Utils::ParseFormKey(trimmed);
                if (!parsed.isValid) {
                    MARAS_LOG_WARN("[ChildTraining] Malformed form key '{}' for {}", trimmed, context);
                    return nullptr;
                }
                return Utils::LookupForm<T>(parsed.localFormID, parsed.pluginName, context);
            }
            if (auto pipePos = trimmed.find('|'); pipePos != std::string_view::npos) {
                std::string_view plugin = StringUtils::TrimView(trimmed.substr(0, pipePos));
                std::string_view hexStr = StringUtils::TrimView(trimmed.substr(pipePos + 1));
                auto formIDOpt = Utils::HexStringToFormID(hexStr);
                if (!plugin.empty() && formIDOpt.has_value()) {
                    return Utils::LookupForm<T>(*formIDOpt, plugin, context);
                }
                MARAS_LOG_WARN("[ChildTraining] Invalid plugin|hex form key '{}' for {}", trimmed, context);
                return nullptr;
            }
            if (auto formIDOpt = Utils::HexStringToFormID(trimmed); formIDOpt.has_value()) {
                auto* form = RE::TESForm::LookupByID<T>(*formIDOpt);
                if (!form) {
                    MARAS_LOG_WARN("[ChildTraining] Failed to lookup form 0x{:08X} ('{}') for {}", *formIDOpt, trimmed, context);
                }
                return form;
            }
            MARAS_LOG_WARN("[ChildTraining] Unrecognized form key format '{}' for {}", trimmed, context);
            return nullptr;
        }

        std::optional<RE::ActorValue> ParseSkillActorValue(std::string_view rawSkillName) {
            std::string clean;
            clean.reserve(rawSkillName.size());
            for (char c : rawSkillName) {
                if (c != '_' && c != '-' && c != ' ') {
                    clean.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                }
            }
            if (clean == "onehanded") return RE::ActorValue::kOneHanded;
            if (clean == "twohanded") return RE::ActorValue::kTwoHanded;
            if (clean == "archery" || clean == "marksman") return RE::ActorValue::kArchery;
            if (clean == "block") return RE::ActorValue::kBlock;
            if (clean == "smithing") return RE::ActorValue::kSmithing;
            if (clean == "heavyarmor") return RE::ActorValue::kHeavyArmor;
            if (clean == "lightarmor") return RE::ActorValue::kLightArmor;
            if (clean == "pickpocket") return RE::ActorValue::kPickpocket;
            if (clean == "lockpicking") return RE::ActorValue::kLockpicking;
            if (clean == "sneak") return RE::ActorValue::kSneak;
            if (clean == "alchemy") return RE::ActorValue::kAlchemy;
            if (clean == "speech" || clean == "speechcraft") return RE::ActorValue::kSpeech;
            if (clean == "alteration") return RE::ActorValue::kAlteration;
            if (clean == "conjuration") return RE::ActorValue::kConjuration;
            if (clean == "destruction") return RE::ActorValue::kDestruction;
            if (clean == "illusion") return RE::ActorValue::kIllusion;
            if (clean == "restoration") return RE::ActorValue::kRestoration;
            if (clean == "enchanting") return RE::ActorValue::kEnchanting;
            return std::nullopt;
        }

        std::vector<std::string> ParseStringOrArrayKeys(const nlohmann::json& j, std::string_view fieldName) {
            std::vector<std::string> result;
            if (!j.is_object() || !j.contains(fieldName)) {
                return result;
            }
            const auto& val = j[fieldName];
            if (val.is_string()) {
                std::string s = StringUtils::TrimStd(val.get<std::string>());
                if (!s.empty()) {
                    result.push_back(std::move(s));
                }
            } else if (val.is_array()) {
                for (const auto& elem : val) {
                    if (elem.is_string()) {
                        std::string s = StringUtils::TrimStd(elem.get<std::string>());
                        if (!s.empty()) {
                            result.push_back(std::move(s));
                        }
                    }
                }
            }
            return result;
        }

        std::vector<AdultChildProgression::SkillGrantSpec> ParseSkillsMapJson(const nlohmann::json& skillsJson) {
            std::vector<AdultChildProgression::SkillGrantSpec> out;
            if (!skillsJson.is_object()) {
                return out;
            }
            for (const auto& [skillKey, bonusVal] : skillsJson.items()) {
                if (!bonusVal.is_number()) continue;
                auto avOpt = ParseSkillActorValue(skillKey);
                if (!avOpt.has_value()) {
                    MARAS_LOG_WARN("[ChildTraining] Ignoring unknown skill key '{}' in JSON config", skillKey);
                    continue;
                }
                const float bonus = bonusVal.get<float>();
                if (std::abs(bonus) > 0.001f) {
                    out.push_back({*avOpt, bonus});
                }
            }
            return out;
        }

        std::vector<AdultChildProgression::ItemGrantSpec> ParseItemsArrayJson(const nlohmann::json& itemsJson) {
            std::vector<AdultChildProgression::ItemGrantSpec> out;
            if (!itemsJson.is_array()) {
                return out;
            }
            for (const auto& elem : itemsJson) {
                if (elem.is_string()) {
                    std::string fKey = StringUtils::TrimStd(elem.get<std::string>());
                    if (!fKey.empty()) {
                        out.push_back({std::move(fKey), 1});
                    }
                } else if (elem.is_object()) {
                    std::string fKey;
                    if (elem.contains("form") && elem["form"].is_string()) {
                        fKey = StringUtils::TrimStd(elem["form"].get<std::string>());
                    } else if (elem.contains("formKey") && elem["formKey"].is_string()) {
                        fKey = StringUtils::TrimStd(elem["formKey"].get<std::string>());
                    }
                    std::int32_t count = 1;
                    if (elem.contains("count") && elem["count"].is_number_integer()) {
                        count = std::max<std::int32_t>(1, elem["count"].get<std::int32_t>());
                    }
                    if (!fKey.empty()) {
                        out.push_back({std::move(fKey), count});
                    }
                }
            }
            return out;
        }

        std::array<RE::ActorValue, 4> GetSignatureSkillsForSkillType(SkillType skillType) {
            switch (skillType) {
                case SkillType::Warrior:
                    return {RE::ActorValue::kOneHanded, RE::ActorValue::kTwoHanded,
                            RE::ActorValue::kHeavyArmor, RE::ActorValue::kBlock};
                case SkillType::Mage:
                    return {RE::ActorValue::kDestruction, RE::ActorValue::kRestoration,
                            RE::ActorValue::kAlteration, RE::ActorValue::kConjuration};
                case SkillType::Rogue:
                    return {RE::ActorValue::kSneak, RE::ActorValue::kOneHanded,
                            RE::ActorValue::kLightArmor, RE::ActorValue::kPickpocket};
                case SkillType::Craftsman:
                    return {RE::ActorValue::kSmithing, RE::ActorValue::kEnchanting,
                            RE::ActorValue::kAlchemy, RE::ActorValue::kHeavyArmor};
                case SkillType::Ranger:
                    return {RE::ActorValue::kArchery, RE::ActorValue::kLightArmor,
                            RE::ActorValue::kSneak, RE::ActorValue::kAlchemy};
                case SkillType::Orator:
                    return {RE::ActorValue::kSpeech, RE::ActorValue::kIllusion,
                            RE::ActorValue::kRestoration, RE::ActorValue::kOneHanded};
                default:
                    return {RE::ActorValue::kOneHanded, RE::ActorValue::kBlock,
                            RE::ActorValue::kHeavyArmor, RE::ActorValue::kLightArmor};
            }
        }

        std::array<float, 3> GetHMSWeightsForSkillType(SkillType skillType) {
            switch (skillType) {
                case SkillType::Warrior:
                    return {0.65f, 0.05f, 0.30f};
                case SkillType::Mage:
                    return {0.25f, 0.65f, 0.10f};
                case SkillType::Rogue:
                    return {0.30f, 0.10f, 0.60f};
                case SkillType::Craftsman:
                    return {0.50f, 0.20f, 0.30f};
                case SkillType::Ranger:
                    return {0.35f, 0.10f, 0.55f};
                case SkillType::Orator:
                    return {0.40f, 0.40f, 0.20f};
                default:
                    return {0.50f, 0.20f, 0.30f};
            }
        }

        int GetSocialClassRankOrder(SocialClass sc) {
            switch (sc) {
                case SocialClass::Outcast:   return 0;
                case SocialClass::Poverty:   return 1;
                case SocialClass::Working:   return 2;
                case SocialClass::Middle:    return 3;
                case SocialClass::Religious: return 4;
                case SocialClass::Wealthy:   return 5;
                case SocialClass::Nobles:    return 6;
                case SocialClass::Rulers:    return 7;
                default:                     return 1;
            }
        }

        SkillType ResolveActorEffectiveSkillType(const NPCRelationshipManager& mgr, RE::FormID actorID) {
            if (actorID == 0 || actorID == kUnknownFatherFormID) {
                return SkillType::Warrior;
            }
            if (Utils::MatchesActor(actorID, kPlayerFormID)) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                auto* avOwner = player ? player->AsActorValueOwner() : nullptr;
                if (!avOwner) {
                    return SkillType::Warrior;
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

                SkillType bestType = SkillType::Warrior;
                float bestScore = warriorScore;
                auto checkCandidate = [&](SkillType candidate, float score) {
                    if (score > bestScore) {
                        bestScore = score;
                        bestType = candidate;
                    }
                };
                checkCandidate(SkillType::Mage, mageScore);
                checkCandidate(SkillType::Rogue, rogueScore);
                checkCandidate(SkillType::Ranger, rangerScore);
                checkCandidate(SkillType::Craftsman, craftScore);
                checkCandidate(SkillType::Orator, oratorScore);
                return bestType;
            }
            return mgr.GetSkillType(actorID);
        }

        bool ContainsInsensitive(std::string_view haystack, std::string_view needle) {
            if (needle.empty() || haystack.size() < needle.size()) {
                return false;
            }
            auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                                  [](char a, char b) {
                                      return std::tolower(static_cast<unsigned char>(a)) ==
                                             std::tolower(static_cast<unsigned char>(b));
                                  });
            return it != haystack.end();
        }

        bool IsHeadPartValidForChild(RE::BGSHeadPart* part, RE::TESRace* childRace, bool isFemale,
                                     RE::BGSHeadPart::HeadPartType expectedType, bool requireVanillaMaster) {
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
                for (std::string_view blocked : {"Vampire", "Blind", "Demon", "Afflicted", "Dead", "Falmer", "Zombie"}) {
                    if (ContainsInsensitive(edid, blocked)) {
                        return false;
                    }
                }
            } else if (expectedType == RE::BGSHeadPart::HeadPartType::kHair) {
                for (std::string_view blocked : {"Bald", "Shaved", "Afflicted"}) {
                    if (ContainsInsensitive(edid, blocked)) {
                        return false;
                    }
                }
            }
            return true;
        }

        std::string_view ExtractEyeColorKeyword(std::string_view edid) {
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

        RE::Actor* ResolveActorPtr(RE::FormID formID) {
            if (formID == 0 || formID == kUnknownFatherFormID) {
                return nullptr;
            }
            if (Utils::MatchesActor(formID, kPlayerFormID)) {
                return RE::PlayerCharacter::GetSingleton();
            }
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(formID)) {
                return actor;
            }
            return NPCIndex::GetSingleton().GetActorFromFormId(formID);
        }

        RE::TESNPC* GetActorBaseNPC(RE::FormID formID) {
            if (formID == 0 || formID == kUnknownFatherFormID) {
                return nullptr;
            }
            if (Utils::MatchesActor(formID, kPlayerFormID)) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                return player ? player->GetActorBase() : nullptr;
            }
            return ActorSvc::GetActorBase(formID);
        }

        std::int32_t GetActorFactionRank(RE::FormID actorID, RE::TESFaction* faction) {
            if (actorID == 0 || actorID == kUnknownFatherFormID || !faction) {
                return -1;
            }
            auto* actor = ResolveActorPtr(actorID);
            RE::TESObjectREFR* ref = actor ? static_cast<RE::TESObjectREFR*>(actor) : ActorSvc::GetReference(actorID);
            if (ref) {
                if (auto* xFac = ref->extraList.GetByType<RE::ExtraFactionChanges>()) {
                    for (const auto& f : xFac->factionChanges) {
                        if (f.faction == faction) {
                            return static_cast<std::int32_t>(f.rank);
                        }
                    }
                }
            }
            if (auto* base = GetActorBaseNPC(actorID)) {
                for (const auto& f : base->factions) {
                    if (f.faction == faction) {
                        return static_cast<std::int32_t>(f.rank);
                    }
                }
            }
            if (actor && actor->IsInFaction(faction)) {
                return 0;
            }
            return -1;
        }

        bool DoesParentMatchLeaderNpc(RE::FormID parentID, RE::TESForm* leaderForm) {
            if (parentID == 0 || parentID == kUnknownFatherFormID || !leaderForm) {
                return false;
            }
            if (Utils::MatchesActor(parentID, leaderForm->GetFormID())) {
                return true;
            }
            auto* parentBase = GetActorBaseNPC(parentID);
            if (parentBase && parentBase->GetFormID() == leaderForm->GetFormID()) {
                return true;
            }
            if (auto* leaderActor = leaderForm->As<RE::Actor>()) {
                if (auto* leaderBase = leaderActor->GetActorBase()) {
                    if (Utils::MatchesActor(parentID, leaderBase->GetFormID()) ||
                        (parentBase && parentBase->GetFormID() == leaderBase->GetFormID())) {
                        return true;
                    }
                }
            }
            return false;
        }

        std::string GetParentDisplayName(RE::FormID parentID) {
            if (Utils::MatchesActor(parentID, kPlayerFormID)) {
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    if (const char* pName = player->GetName(); pName && pName[0] != '\0') {
                        return pName;
                    }
                }
                return "Player";
            }
            std::string name = ActorSvc::GetName(parentID);
            return name.empty() ? "Parent" : name;
        }

        std::uint16_t ResolveActorEffectiveLevel(RE::FormID actorID) {
            if (actorID == 0 || actorID == kUnknownFatherFormID) {
                return 1;
            }
            if (Utils::MatchesActor(actorID, kPlayerFormID)) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                return player ? std::max<std::uint16_t>(1, player->GetLevel()) : 1;
            }
            if (auto* actor = ResolveActorPtr(actorID)) {
                return std::max<std::uint16_t>(1, actor->GetLevel());
            }
            if (auto* base = GetActorBaseNPC(actorID)) {
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

        std::uint16_t GetEffectiveChildLevel() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            const int playerLevel = player ? static_cast<int>(player->GetLevel()) : 10;
            return static_cast<std::uint16_t>(std::clamp(playerLevel, 10, 50));
        }

        std::uint8_t ComputeBioParentSkillBonus(std::uint16_t parentLevel) {
            // Scales linearly from +2 (at Lv 1) up to +8 (at Lv 50+)
            const float t = std::clamp((static_cast<float>(parentLevel) - 1.0f) / 49.0f, 0.0f, 1.0f);
            return static_cast<std::uint8_t>(std::lround(2.0f + 6.0f * t));
        }

        std::uint8_t ComputeGuardianSkillBonus(std::uint16_t guardianLevel) {
            // Scales linearly from +2 (at Lv 1) up to +7 (at Lv 50+)
            const float t = std::clamp((static_cast<float>(guardianLevel) - 1.0f) / 49.0f, 0.0f, 1.0f);
            return static_cast<std::uint8_t>(std::lround(2.0f + 5.0f * t));
        }

        std::array<RE::ActorValue, 4> ResolveParentSignatureSkills(const NPCRelationshipManager& mgr,
                                                                   RE::FormID parentID) {
            if (Utils::MatchesActor(parentID, kPlayerFormID)) {
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    if (auto* avOwner = player->AsActorValueOwner()) {
                        std::array<std::pair<float, RE::ActorValue>, 18> scored{};
                        float minSkill = 1000.0f;
                        float maxSkill = 0.0f;
                        for (std::size_t i = 0; i < kAllSkills.size(); ++i) {
                            const float val = avOwner->GetBaseActorValue(kAllSkills[i]);
                            scored[i] = {val, kAllSkills[i]};
                            minSkill = std::min(minSkill, val);
                            maxSkill = std::max(maxSkill, val);
                        }
                        if (maxSkill > minSkill + 0.5f) {
                            std::stable_sort(scored.begin(), scored.end(),
                                             [](const auto& a, const auto& b) { return a.first > b.first; });
                            return {scored[0].second, scored[1].second, scored[2].second, scored[3].second};
                        }
                    }
                }
            }
            return GetSignatureSkillsForSkillType(ResolveActorEffectiveSkillType(mgr, parentID));
        }

        float NormalizeClassSkillWeight(float rawBonus) {
            if (rawBonus <= 0.0f) {
                return 0.0f;
            }
            return std::clamp(rawBonus, 1.0f, 5.0f);
        }

    }  // namespace

    void AdultChildProgression::ParseTrainingLocationObject(const nlohmann::json& obj, bool /*isExtensionFile*/) {
        if (!obj.is_object()) {
            return;
        }

        std::string id;
        if (obj.contains("id") && obj["id"].is_string()) {
            id = StringUtils::TrimStd(obj["id"].get<std::string>());
        }
        std::string name;
        if (obj.contains("name") && obj["name"].is_string()) {
            name = StringUtils::TrimStd(obj["name"].get<std::string>());
        } else if (obj.contains("className") && obj["className"].is_string()) {
            name = StringUtils::TrimStd(obj["className"].get<std::string>());
        }
        if (id.empty()) {
            MARAS_LOG_ERROR("[ChildTraining] Rejecting training location config: missing required 'id'");
            return;
        }

        // Check if this entry updates an existing training path by id or explicit index
        TrainingClassRuntimeSpec* target = nullptr;
        for (auto& existing : catalog_) {
            if (StringUtils::CaseInsensitiveEquals(existing.id, id)) {
                target = &existing;
                break;
            }
        }
        if (!target && obj.contains("index") && obj["index"].is_number_integer()) {
            const int idx = obj["index"].get<int>();
            if (idx >= 0 && static_cast<std::size_t>(idx) < catalog_.size()) {
                target = &catalog_[static_cast<std::size_t>(idx)];
            }
        }

        if (!target) {
            const auto classKeys = obj.contains("npcClass") ? ParseStringOrArrayKeys(obj, "npcClass")
                                                            : ParseStringOrArrayKeys(obj, "class");
            const auto combatStyleKeys = ParseStringOrArrayKeys(obj, "combatStyle");
            const auto outfitKeys = ParseStringOrArrayKeys(obj, "outfit");
            const bool hasLocation = (obj.contains("location") && obj["location"].is_string() && !StringUtils::TrimStd(obj["location"].get<std::string>()).empty()) ||
                                     (obj.contains("locationName") && obj["locationName"].is_string() && !StringUtils::TrimStd(obj["locationName"].get<std::string>()).empty());

            if (name.empty() || !hasLocation || classKeys.empty() || combatStyleKeys.empty() || outfitKeys.empty()) {
                MARAS_LOG_ERROR(
                    "[ChildTraining] Rejecting training location '{}': missing critical config (name='{}', hasLocation={}, npcClass={}, combatStyle={}, outfit={})",
                    id, name, hasLocation, !classKeys.empty(), !combatStyleKeys.empty(), !outfitKeys.empty());
                return;
            }

            TrainingClassRuntimeSpec newSpec;
            newSpec.index = static_cast<std::int32_t>(catalog_.size());
            newSpec.id = id;
            newSpec.className = name;
            newSpec.imagePath = std::format("Data/SKSE/Plugins/MARAS/childTraining/images/{}.webp", newSpec.id);
            catalog_.push_back(std::move(newSpec));
            target = &catalog_.back();
        }

        if (!id.empty()) {
            target->id = id;
        }
        if (!name.empty()) {
            target->className = name;
        }
        if (obj.contains("location") && obj["location"].is_string()) {
            target->locationName = StringUtils::TrimStd(obj["location"].get<std::string>());
        } else if (obj.contains("locationName") && obj["locationName"].is_string()) {
            target->locationName = StringUtils::TrimStd(obj["locationName"].get<std::string>());
        }
        if (obj.contains("image") && obj["image"].is_string()) {
            target->imagePath = StringUtils::TrimStd(obj["image"].get<std::string>());
            target->cachedImageSrc.clear();
        }
        if (obj.contains("description") && obj["description"].is_string()) {
            target->description = StringUtils::TrimStd(obj["description"].get<std::string>());
        }
        if (obj.contains("costMultiplier") && obj["costMultiplier"].is_number()) {
            target->costMultiplier = std::max(0.0f, obj["costMultiplier"].get<float>());
        } else if (obj.contains("multiplier") && obj["multiplier"].is_number()) {
            target->costMultiplier = std::max(0.0f, obj["multiplier"].get<float>());
        }
        if (obj.contains("skillType") && obj["skillType"].is_string()) {
            target->mappedSkillType = Utils::StringToSkillType(obj["skillType"].get<std::string>());
        }
        if (obj.contains("requiresBeastblood") && obj["requiresBeastblood"].is_boolean()) {
            target->requiresBeastblood = obj["requiresBeastblood"].get<bool>();
        }

        if (obj.contains("hmsWeights")) {
            const auto& hms = obj["hmsWeights"];
            float h = target->hmsWeights[0];
            float m = target->hmsWeights[1];
            float s = target->hmsWeights[2];
            if (hms.is_object()) {
                if (hms.contains("health") && hms["health"].is_number()) h = std::max(0.0f, hms["health"].get<float>());
                if (hms.contains("magicka") && hms["magicka"].is_number()) m = std::max(0.0f, hms["magicka"].get<float>());
                if (hms.contains("stamina") && hms["stamina"].is_number()) s = std::max(0.0f, hms["stamina"].get<float>());
            } else if (hms.is_array() && hms.size() >= 3 && hms[0].is_number() && hms[1].is_number() && hms[2].is_number()) {
                h = std::max(0.0f, hms[0].get<float>());
                m = std::max(0.0f, hms[1].get<float>());
                s = std::max(0.0f, hms[2].get<float>());
            }
            const float sum = h + m + s;
            if (sum > 0.001f) {
                target->hmsWeights = {h / sum, m / sum, s / sum};
            }
        }

        if (obj.contains("ai") && obj["ai"].is_object()) {
            const auto& ai = obj["ai"];
            if (ai.contains("morality") && ai["morality"].is_number()) {
                target->moralityOverride = ai["morality"].get<float>();
            }
            if (ai.contains("minConfidence") && ai["minConfidence"].is_number()) {
                target->minConfidence = std::clamp(ai["minConfidence"].get<float>(), 1.0f, 4.0f);
            }
        }

        if (obj.contains("requirements") && obj["requirements"].is_object()) {
            const auto& req = obj["requirements"];
            if (req.contains("key") && req["key"].is_string()) {
                target->requirementKey = StringUtils::TrimStd(req["key"].get<std::string>());
            }
            if (req.contains("label") && req["label"].is_string()) {
                target->requirementLabel = StringUtils::TrimStd(req["label"].get<std::string>());
            }
            if (req.contains("faction") && req["faction"].is_string()) {
                target->requiredFactionKey = StringUtils::TrimStd(req["faction"].get<std::string>());
            }
            if (req.contains("quest") && req["quest"].is_string()) {
                target->requiredQuestKey = StringUtils::TrimStd(req["quest"].get<std::string>());
            }
            if (req.contains("questStage") && req["questStage"].is_number_integer()) {
                target->requiredQuestStage = req["questStage"].get<std::int32_t>();
            } else if (req.contains("minQuestStage") && req["minQuestStage"].is_number_integer()) {
                target->requiredQuestStage = req["minQuestStage"].get<std::int32_t>();
            }
        }

        if (obj.contains("affiliation") && obj["affiliation"].is_object()) {
            const auto& aff = obj["affiliation"];
            if (aff.contains("memberFactions")) {
                target->memberFactionKeys = ParseStringOrArrayKeys(aff, "memberFactions");
            }
            if (aff.contains("memberQuests")) {
                target->memberQuestKeys = ParseStringOrArrayKeys(aff, "memberQuests");
            }
            if (aff.contains("memberWerewolf") && aff["memberWerewolf"].is_boolean()) {
                target->memberWerewolf = aff["memberWerewolf"].get<bool>();
            }
            if (aff.contains("leaderFactions")) {
                target->leaderFactionKeys = ParseStringOrArrayKeys(aff, "leaderFactions");
            }
            if (aff.contains("leaderFactionRank") && aff["leaderFactionRank"].is_number_integer()) {
                target->leaderFactionRank = aff["leaderFactionRank"].get<std::int32_t>();
            }
            if (aff.contains("leaderQuests")) {
                target->leaderQuestKeys = ParseStringOrArrayKeys(aff, "leaderQuests");
            }
            if (aff.contains("leaderNpcs")) {
                target->leaderNpcKeys = ParseStringOrArrayKeys(aff, "leaderNpcs");
            }
        }

        if (obj.contains("npcClass")) {
            target->classKeys = ParseStringOrArrayKeys(obj, "npcClass");
            if (target->cachedRuntimeClass) {
                target->cachedRuntimeClass->fullName = RE::BSFixedString();
                RE::free(target->cachedRuntimeClass);
                target->cachedRuntimeClass = nullptr;
            }
        } else if (obj.contains("class")) {
            target->classKeys = ParseStringOrArrayKeys(obj, "class");
            if (target->cachedRuntimeClass) {
                target->cachedRuntimeClass->fullName = RE::BSFixedString();
                RE::free(target->cachedRuntimeClass);
                target->cachedRuntimeClass = nullptr;
            }
        }
        if (obj.contains("combatStyle")) {
            target->combatStyleKeys = ParseStringOrArrayKeys(obj, "combatStyle");
            target->cachedCombatStyle = nullptr;
        }
        if (obj.contains("outfit")) {
            target->outfitKeys = ParseStringOrArrayKeys(obj, "outfit");
            target->cachedOutfit = nullptr;
        }
        if (obj.contains("skills") && obj["skills"].is_object()) {
            target->skills = ParseSkillsMapJson(obj["skills"]);
        }
        if (obj.contains("spells") && obj["spells"].is_array()) {
            target->spellKeys = ParseStringOrArrayKeys(obj, "spells");
        }
        if (obj.contains("perks") && obj["perks"].is_array()) {
            target->perkKeys = ParseStringOrArrayKeys(obj, "perks");
        }
        if (obj.contains("items") && obj["items"].is_array()) {
            target->items = ParseItemsArrayJson(obj["items"]);
        }
    }

    void AdultChildProgression::ParseDaedricBlessingObject(const nlohmann::json& obj) {
        if (!obj.is_object()) {
            return;
        }
        std::string id;
        if (obj.contains("id") && obj["id"].is_string()) {
            id = StringUtils::TrimStd(obj["id"].get<std::string>());
        }
        std::string name;
        if (obj.contains("name") && obj["name"].is_string()) {
            name = StringUtils::TrimStd(obj["name"].get<std::string>());
        }
        if (id.empty() && !name.empty()) {
            id = StringUtils::ToLowerStd(name);
        }
        if (id.empty()) {
            return;
        }

        DaedricBlessingRuntimeSpec* target = nullptr;
        for (auto& existing : daedricBlessings_) {
            if (StringUtils::CaseInsensitiveEquals(existing.id, id)) {
                target = &existing;
                break;
            }
        }
        if (!target) {
            DaedricBlessingRuntimeSpec newBlessing;
            newBlessing.id = id;
            newBlessing.name = name.empty() ? id : name;
            daedricBlessings_.push_back(std::move(newBlessing));
            target = &daedricBlessings_.back();
        }

        if (!name.empty()) {
            target->name = name;
        }
        if (obj.contains("quest") && obj["quest"].is_string()) {
            target->questKey = StringUtils::TrimStd(obj["quest"].get<std::string>());
        }
        if (obj.contains("hmsBonus") && obj["hmsBonus"].is_object()) {
            const auto& hms = obj["hmsBonus"];
            if (hms.contains("health") && hms["health"].is_number()) target->bonusHealth = hms["health"].get<float>();
            if (hms.contains("magicka") && hms["magicka"].is_number()) target->bonusMagicka = hms["magicka"].get<float>();
            if (hms.contains("stamina") && hms["stamina"].is_number()) target->bonusStamina = hms["stamina"].get<float>();
        }
        if (obj.contains("bonusHealth") && obj["bonusHealth"].is_number()) {
            target->bonusHealth = obj["bonusHealth"].get<float>();
        }
        if (obj.contains("bonusMagicka") && obj["bonusMagicka"].is_number()) {
            target->bonusMagicka = obj["bonusMagicka"].get<float>();
        }
        if (obj.contains("bonusStamina") && obj["bonusStamina"].is_number()) {
            target->bonusStamina = obj["bonusStamina"].get<float>();
        }
        if (obj.contains("skills") && obj["skills"].is_object()) {
            target->skills = ParseSkillsMapJson(obj["skills"]);
        }
        if (obj.contains("spells") && obj["spells"].is_array()) {
            target->spellKeys = ParseStringOrArrayKeys(obj, "spells");
        }
        if (obj.contains("perks") && obj["perks"].is_array()) {
            target->perkKeys = ParseStringOrArrayKeys(obj, "perks");
        }
    }

    namespace {
        nlohmann::json YamlNodeToJson(const YAML::Node& node) {
            if (!node.IsDefined() || node.IsNull()) {
                return nullptr;
            }
            if (node.IsMap()) {
                nlohmann::json obj = nlohmann::json::object();
                for (const auto& kv : node) {
                    obj[kv.first.as<std::string>()] = YamlNodeToJson(kv.second);
                }
                return obj;
            }
            if (node.IsSequence()) {
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& elem : node) {
                    arr.push_back(YamlNodeToJson(elem));
                }
                return arr;
            }
            if (node.IsScalar()) {
                const std::string& raw = node.Scalar();
                if (raw == "true" || raw == "True" || raw == "TRUE") {
                    return true;
                }
                if (raw == "false" || raw == "False" || raw == "FALSE") {
                    return false;
                }
                std::int64_t iVal = 0;
                if (YAML::convert<std::int64_t>::decode(node, iVal)) {
                    return iVal;
                }
                double dVal = 0.0;
                if (YAML::convert<double>::decode(node, dVal)) {
                    return dVal;
                }
                return raw;
            }
            return nullptr;
        }
    }  // namespace

    bool AdultChildProgression::LoadConfigFile(const std::filesystem::path& filePath, bool isExtensionFile) {
        try {
            YAML::Node yamlRoot = YAML::LoadFile(filePath.string());
            nlohmann::json root = YamlNodeToJson(yamlRoot);

            if (root.is_array()) {
                for (const auto& item : root) {
                    ParseTrainingLocationObject(item, isExtensionFile);
                }
                return true;
            }

            if (!root.is_object()) {
                return false;
            }

            if (root.contains("trainingLocations") && root["trainingLocations"].is_array()) {
                for (const auto& item : root["trainingLocations"]) {
                    ParseTrainingLocationObject(item, isExtensionFile);
                }
            } else if (root.contains("id") || root.contains("name") || root.contains("className")) {
                ParseTrainingLocationObject(root, isExtensionFile);
            }

            if (root.contains("daedricBlessings") && root["daedricBlessings"].is_array()) {
                for (const auto& item : root["daedricBlessings"]) {
                    ParseDaedricBlessingObject(item);
                }
            }

            return true;
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("[ChildTraining] Exception loading '{}': {}", filePath.string(), e.what());
            return false;
        }
    }

    bool AdultChildProgression::LoadConfig(
        const std::filesystem::path& configPath,
        const std::filesystem::path& extensionFolder)
    {
        ClearCachedCatalogForms();
        FlushRetiredForms();
        catalog_.clear();
        daedricBlessings_.clear();

        bool loadedAny = false;
        std::error_code ec;
        if (!configPath.empty() && std::filesystem::exists(configPath, ec) && std::filesystem::is_regular_file(configPath, ec)) {
            if (LoadConfigFile(configPath, /*isExtensionFile=*/false)) {
                MARAS_LOG_INFO("[ChildTraining] Loaded base training config from '{}' ({} paths, {} Daedric blessings)",
                               configPath.string(), catalog_.size(), daedricBlessings_.size());
                loadedAny = true;
            }
        } else if (!configPath.empty()) {
            MARAS_LOG_WARN("[ChildTraining] Base training config file not found at '{}'", configPath.string());
        }

        if (!extensionFolder.empty() && std::filesystem::exists(extensionFolder, ec) && std::filesystem::is_directory(extensionFolder, ec)) {
            std::vector<std::filesystem::path> extFiles;
            for (const auto& entry : std::filesystem::directory_iterator(extensionFolder, ec)) {
                if (entry.is_regular_file() &&
                    StringUtils::CaseInsensitiveEquals(entry.path().extension().string(), ".yaml")) {
                    extFiles.push_back(entry.path());
                }
            }
            std::sort(extFiles.begin(), extFiles.end());
            std::size_t extLoaded = 0;
            for (const auto& f : extFiles) {
                if (LoadConfigFile(f, /*isExtensionFile=*/true)) {
                    MARAS_LOG_INFO("[ChildTraining] Loaded extension config '{}'", f.filename().string());
                    ++extLoaded;
                    loadedAny = true;
                }
            }
            if (extLoaded > 0) {
                MARAS_LOG_INFO("[ChildTraining] Applied {} extension file(s) from '{}' (total paths: {}, Daedric blessings: {})",
                               extLoaded, extensionFolder.string(), catalog_.size(), daedricBlessings_.size());
            }
        }

        // Ensure sequential index parity across catalog_
        for (std::size_t i = 0; i < catalog_.size(); ++i) {
            catalog_[i].index = static_cast<std::int32_t>(i);
        }

        return loadedAny;
    }

    std::int32_t AdultChildProgression::GetTrainingClassCount() noexcept {
        return static_cast<std::int32_t>(GetSingleton().catalog_.size());
    }

    bool AdultChildProgression::IsValidTrainingClass(std::int32_t classIndex) noexcept {
        return classIndex >= 0 && classIndex < GetTrainingClassCount();
    }

    std::int32_t AdultChildProgression::FindTrainingClassByLocation(std::string_view locationName) noexcept {
        if (locationName.empty()) {
            return -1;
        }
        const auto& cat = GetSingleton().catalog_;
        for (const auto& spec : cat) {
            if (StringUtils::CaseInsensitiveEquals(spec.locationName, locationName)) {
                return spec.index;
            }
        }
        return -1;
    }

    std::string_view AdultChildProgression::GetTrainingClassName(std::int32_t classIndex) noexcept {
        if (!IsValidTrainingClass(classIndex)) {
            return "";
        }
        return GetSingleton().catalog_[static_cast<std::size_t>(classIndex)].className;
    }

    std::string_view AdultChildProgression::GetTrainingLocationName(std::int32_t classIndex) noexcept {
        if (!IsValidTrainingClass(classIndex)) {
            return "";
        }
        return GetSingleton().catalog_[static_cast<std::size_t>(classIndex)].locationName;
    }

    std::string_view AdultChildProgression::GetTrainingDescription(std::int32_t classIndex) noexcept {
        if (!IsValidTrainingClass(classIndex)) {
            return "";
        }
        return GetSingleton().catalog_[static_cast<std::size_t>(classIndex)].description;
    }

    float AdultChildProgression::GetTrainingClassCostMultiplier(std::int32_t classIndex) noexcept {
        if (!IsValidTrainingClass(classIndex)) {
            return 1.0f;
        }
        return GetSingleton().catalog_[static_cast<std::size_t>(classIndex)].costMultiplier;
    }

    std::int32_t AdultChildProgression::GetEffectiveTrainingCost(std::int32_t classIndex, std::int32_t baseCost) noexcept {
        if (baseCost <= 0) {
            return 0;
        }
        const float mult = GetTrainingClassCostMultiplier(classIndex);
        return std::max(0, static_cast<std::int32_t>(std::lround(static_cast<float>(baseCost) * mult)));
    }

    std::int32_t AdultChildProgression::GetDiscountedTrainingCost(std::int32_t classIndex, std::int32_t baseCost,
                                                                  std::int32_t discountPercent) noexcept {
        const std::int32_t effectiveBase = GetEffectiveTrainingCost(classIndex, baseCost);
        if (effectiveBase <= 0 || discountPercent <= 0) {
            return effectiveBase;
        }
        const float factor = std::clamp(1.0f - (static_cast<float>(discountPercent) / 100.0f), 0.0f, 1.0f);
        return std::max(0, static_cast<std::int32_t>(std::lround(static_cast<float>(effectiveBase) * factor)));
    }

    AdultChildProgression::TrainingDiscountInfo AdultChildProgression::EvaluateChildTrainingDiscount(
        State::ChildId childId, std::int32_t classIndex, const Projection::PerspectiveView& view) const {
        TrainingDiscountInfo result{};
        if (!IsValidTrainingClass(classIndex)) {
            return result;
        }
        const auto& spec = catalog_[static_cast<std::size_t>(classIndex)];

        const std::vector<State::ActorId> parents = view.GetVisibleParentIdsForTrainingDiscount(childId);
        if (parents.empty()) {
            return result;
        }

        auto isParentLeader = [&](RE::FormID parentID) -> bool {
            for (const auto& npcKey : spec.leaderNpcKeys) {
                if (auto* leaderForm = ResolveFormFromKeyString<RE::TESForm>(npcKey, "training leaderNpc")) {
                    if (DoesParentMatchLeaderNpc(parentID, leaderForm)) {
                        return true;
                    }
                }
            }
            for (const auto& facKey : spec.leaderFactionKeys) {
                if (auto* fac = ResolveFormFromKeyString<RE::TESFaction>(facKey, "training leaderFaction")) {
                    if (GetActorFactionRank(parentID, fac) >= 0) {
                        return true;
                    }
                }
            }
            if (spec.leaderFactionRank >= 0) {
                for (const auto& facKey : spec.memberFactionKeys) {
                    if (auto* fac = ResolveFormFromKeyString<RE::TESFaction>(facKey, "training memberFaction rank")) {
                        if (GetActorFactionRank(parentID, fac) >= spec.leaderFactionRank) {
                            return true;
                        }
                    }
                }
            }
            if (Utils::MatchesActor(parentID, kPlayerFormID)) {
                for (const auto& qKey : spec.leaderQuestKeys) {
                    if (auto* quest = ResolveFormFromKeyString<RE::TESQuest>(qKey, "training leaderQuest")) {
                        if (quest->IsCompleted()) {
                            return true;
                        }
                    }
                }
            }
            return false;
        };

        auto isParentMember = [&](State::ActorId parentActorId) -> bool {
            const RE::FormID parentID = parentActorId.raw();
            for (const auto& facKey : spec.memberFactionKeys) {
                if (auto* fac = ResolveFormFromKeyString<RE::TESFaction>(facKey, "training memberFaction")) {
                    if (GetActorFactionRank(parentID, fac) >= 0) {
                        return true;
                    }
                }
            }
            if (spec.memberWerewolf && view.IsActorKnownWerewolf(parentActorId)) {
                return true;
            }
            if (Utils::MatchesActor(parentID, kPlayerFormID)) {
                for (const auto& qKey : spec.memberQuestKeys) {
                    if (auto* quest = ResolveFormFromKeyString<RE::TESQuest>(qKey, "training memberQuest")) {
                        const bool stageMet = (spec.requiredQuestStage > 0 && spec.requiredQuestKey == qKey)
                                                  ? (quest->GetCurrentStageID() >= static_cast<std::uint16_t>(spec.requiredQuestStage) || quest->IsCompleted())
                                                  : quest->IsCompleted();
                        if (stageMet) {
                            return true;
                        }
                    }
                }
            }
            return false;
        };

        // Check for 25% Leader discount first across all parents
        for (const State::ActorId parentActorId : parents) {
            if (isParentLeader(parentActorId.raw())) {
                result.discountPercent = 25;
                result.reason = std::format("{} (Leader)", GetParentDisplayName(parentActorId.raw()));
                return result;
            }
        }

        // Otherwise check for 10% Member discount across all parents
        for (const State::ActorId parentActorId : parents) {
            if (isParentMember(parentActorId)) {
                result.discountPercent = 10;
                result.reason = std::format("{} (Member)", GetParentDisplayName(parentActorId.raw()));
                return result;
            }
        }

        return result;
    }

    nlohmann::json AdultChildProgression::BuildChildTrainingDiscountsJson(
        State::ChildId childId, const Projection::PerspectiveView& view) const {
        nlohmann::json discounts = nlohmann::json::object();
        for (const auto& spec : catalog_) {
            const auto info = EvaluateChildTrainingDiscount(childId, spec.index, view);
            if (info.discountPercent > 0) {
                discounts[std::to_string(spec.index)] = {
                    {"percent", info.discountPercent},
                    {"reason", info.reason}
                };
            }
        }
        return discounts;
    }

    SkillType AdultChildProgression::MapTrainingClassToSkillType(std::int32_t classIndex, SkillType fallback) noexcept {
        if (!IsValidTrainingClass(classIndex)) {
            return fallback;
        }
        return GetSingleton().catalog_[static_cast<std::size_t>(classIndex)].mappedSkillType;
    }

    bool AdultChildProgression::DoesTrainingRequireBeastblood(std::int32_t classIndex) noexcept {
        if (!IsValidTrainingClass(classIndex)) {
            return false;
        }
        return GetSingleton().catalog_[static_cast<std::size_t>(classIndex)].requiresBeastblood;
    }

    bool AdultChildProgression::EvaluateTrainingRequirements(const TrainingClassRuntimeSpec& spec, std::string* outReason) const {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return true;
        }

        if (spec.requiresBeastblood && NPCRelationshipManager::IsActorWerewolf(kPlayerFormID)) {
            return true;
        }

        const bool hasFactionReq = !spec.requiredFactionKey.empty();
        const bool hasQuestReq = !spec.requiredQuestKey.empty();

        bool factionMet = false;
        if (hasFactionReq) {
            auto* faction = ResolveFormFromKeyString<RE::TESFaction>(spec.requiredFactionKey, "training requiredFaction");
            factionMet = (!faction || GetActorFactionRank(kPlayerFormID, faction) >= 0);
        }

        bool questMet = false;
        if (hasQuestReq) {
            auto* quest = ResolveFormFromKeyString<RE::TESQuest>(spec.requiredQuestKey, "training requiredQuest");
            if (!quest) {
                questMet = true;
            } else {
                questMet = (spec.requiredQuestStage > 0)
                               ? (quest->GetCurrentStageID() >= static_cast<std::uint16_t>(spec.requiredQuestStage) || quest->IsCompleted())
                               : quest->IsCompleted();
            }
        }

        if (hasFactionReq && hasQuestReq) {
            if (factionMet || questMet) {
                return true;
            }
            if (outReason) {
                const std::string label = spec.requirementLabel.empty() ? spec.locationName : spec.requirementLabel;
                *outReason = std::format("Requires Player membership or patronage: {}", label);
            }
            return false;
        }

        if (hasFactionReq && !factionMet) {
            if (outReason) {
                const std::string label = spec.requirementLabel.empty() ? spec.locationName : spec.requirementLabel;
                *outReason = std::format("Requires Player membership in {}", label);
            }
            return false;
        }

        if (hasQuestReq && !questMet) {
            if (outReason) {
                const std::string label = spec.requirementLabel.empty() ? spec.locationName : spec.requirementLabel;
                *outReason = std::format("Requires Player patronage/completion: {}", label);
            }
            return false;
        }

        return true;
    }

    bool AdultChildProgression::IsPlayerEligibleForTrainingClass(std::int32_t classIndex, std::string* outReason) {
        if (!IsValidTrainingClass(classIndex)) {
            if (outReason) *outReason = "Invalid training class index";
            return false;
        }
        if (!Settings::GetSingleton().Snapshot()->childTrainingRequireFaction) {
            return true;
        }
        return EvaluateTrainingRequirements(catalog_[static_cast<std::size_t>(classIndex)], outReason);
    }

    nlohmann::json AdultChildProgression::BuildPlayerTrainingEligibilityJson() {
        nlohmann::json j = nlohmann::json::object();
        for (const auto& spec : catalog_) {
            if (!spec.requirementKey.empty()) {
                j[spec.requirementKey] = EvaluateTrainingRequirements(spec, nullptr);
            }
        }

        std::int32_t playerGold = 0;
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            if (auto* goldBase = FormCache::GetSingleton().GetGoldBaseObject()) {
                auto inv = player->GetInventory();
                if (auto it = inv.find(goldBase); it != inv.end()) {
                    playerGold = it->second.first;
                }
            }
        }
        j["playerGold"] = playerGold;
        return j;
    }

    namespace {
        std::string Base64EncodeBytes(const std::vector<std::uint8_t>& data) {
            static constexpr char kBase64Chars[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string encoded;
            encoded.reserve(((data.size() + 2) / 3) * 4);
            std::size_t i = 0;
            while (i + 2 < data.size()) {
                const std::uint32_t triple = (static_cast<std::uint32_t>(data[i]) << 16) |
                                             (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                                             static_cast<std::uint32_t>(data[i + 2]);
                encoded.push_back(kBase64Chars[(triple >> 18) & 0x3F]);
                encoded.push_back(kBase64Chars[(triple >> 12) & 0x3F]);
                encoded.push_back(kBase64Chars[(triple >> 6) & 0x3F]);
                encoded.push_back(kBase64Chars[triple & 0x3F]);
                i += 3;
            }
            if (i < data.size()) {
                const std::uint32_t b0 = data[i];
                const std::uint32_t b1 = (i + 1 < data.size()) ? data[i + 1] : 0;
                const std::uint32_t triple = (b0 << 16) | (b1 << 8);
                encoded.push_back(kBase64Chars[(triple >> 18) & 0x3F]);
                encoded.push_back(kBase64Chars[(triple >> 12) & 0x3F]);
                encoded.push_back((i + 1 < data.size()) ? kBase64Chars[(triple >> 6) & 0x3F] : '=');
                encoded.push_back('=');
            }
            return encoded;
        }

        std::string ResolveTrainingImageSrc(AdultChildProgression::TrainingClassRuntimeSpec& spec) {
            if (!spec.cachedImageSrc.empty()) {
                return spec.cachedImageSrc;
            }
            if (spec.imagePath.empty()) {
                return "";
            }

            std::error_code ec;
            std::filesystem::path resolvedPath(spec.imagePath);
            if (!std::filesystem::exists(resolvedPath, ec) || !std::filesystem::is_regular_file(resolvedPath, ec)) {
                // If user dropped a different common image extension (.webp/.png/.jpg) with the same stem, find it
                const auto parent = resolvedPath.parent_path();
                const auto stem = resolvedPath.stem().string();
                bool foundAlt = false;
                for (std::string_view ext : {".webp", ".png", ".jpg", ".jpeg"}) {
                    auto candidate = parent / (stem + std::string(ext));
                    if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec)) {
                        resolvedPath = std::move(candidate);
                        foundAlt = true;
                        break;
                    }
                }
                if (!foundAlt) {
                    return spec.imagePath;
                }
            }

            std::ifstream ifs(resolvedPath, std::ios::binary | std::ios::ate);
            if (!ifs.is_open()) {
                return spec.imagePath;
            }
            const auto fileSize = ifs.tellg();
            if (fileSize <= 0 || fileSize > 4 * 1024 * 1024) {
                return spec.imagePath;
            }
            ifs.seekg(0, std::ios::beg);
            std::vector<std::uint8_t> buffer(static_cast<std::size_t>(fileSize));
            if (!ifs.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
                return spec.imagePath;
            }

            const std::string extLower = StringUtils::ToLowerStd(resolvedPath.extension().string());
            std::string_view mime = "image/webp";
            if (extLower == ".png") {
                mime = "image/png";
            } else if (extLower == ".jpg" || extLower == ".jpeg") {
                mime = "image/jpeg";
            } else if (extLower == ".svg") {
                mime = "image/svg+xml";
            }

            spec.cachedImageSrc = std::format("data:{};base64,{}", mime, Base64EncodeBytes(buffer));
            return spec.cachedImageSrc;
        }
    }  // namespace

    nlohmann::json AdultChildProgression::BuildTrainingCatalogJson(std::int32_t baseCost) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& spec : catalog_) {
            nlohmann::json item;
            item["index"] = spec.index;
            item["id"] = spec.id;
            item["className"] = spec.className;
            item["location"] = spec.locationName;
            item["description"] = spec.description;
            item["image"] = ResolveTrainingImageSrc(spec);
            item["costMultiplier"] = std::round(static_cast<double>(spec.costMultiplier) * 100.0) / 100.0;
            item["effectiveCost"] = GetEffectiveTrainingCost(spec.index, baseCost);
            item["requiresBeastblood"] = spec.requiresBeastblood;
            item["requirementMet"] = EvaluateTrainingRequirements(spec, nullptr);
            if (!spec.requirementKey.empty()) {
                item["factionKey"] = spec.requirementKey;
            }
            if (!spec.requirementLabel.empty()) {
                item["factionLabel"] = spec.requirementLabel;
            }
            arr.push_back(std::move(item));
        }
        return arr;
    }

    RE::TESClass* AdultChildProgression::GetTrainingClass(std::int32_t classIndex) {
        if (!IsValidTrainingClass(classIndex)) {
            MARAS_LOG_ERROR("[ChildTraining] GetTrainingClass called with invalid classIndex {}", classIndex);
            return nullptr;
        }
        auto& spec = catalog_[static_cast<std::size_t>(classIndex)];
        if (spec.cachedRuntimeClass) {
            return spec.cachedRuntimeClass;
        }

        RE::TESClass* baseClass = nullptr;
        for (const auto& key : spec.classKeys) {
            if (auto* c = ResolveFormFromKeyString<RE::TESClass>(key, "adult child npcClass")) {
                baseClass = c;
                break;
            }
        }
        if (!baseClass) {
            MARAS_LOG_ERROR("[ChildTraining] Failed to resolve npcClass for training '{}' (idx={})", spec.id, classIndex);
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
            !spec.className.empty() ? spec.className.c_str() : (origClassName ? origClassName : ""));
        runtimeClass->data.attributeWeights.health =
            static_cast<std::uint8_t>(std::clamp(std::lround(spec.hmsWeights[0] * 10.0f), 0l, 100l));
        runtimeClass->data.attributeWeights.magicka =
            static_cast<std::uint8_t>(std::clamp(std::lround(spec.hmsWeights[1] * 10.0f), 0l, 100l));
        runtimeClass->data.attributeWeights.stamina =
            static_cast<std::uint8_t>(std::clamp(std::lround(spec.hmsWeights[2] * 10.0f), 0l, 100l));

        if (!spec.skills.empty()) {
            auto* weightsPtr = &runtimeClass->data.skillWeights.oneHanded;
            std::memset(weightsPtr, 0, sizeof(RE::CLASS_DATA::SkillWeights));
            for (const auto& sg : spec.skills) {
                const int sIdx = static_cast<int>(sg.skill) - static_cast<int>(RE::ActorValue::kOneHanded);
                if (sIdx >= 0 && sIdx < 18 && sg.bonus > 0.0f) {
                    const float normWeight = NormalizeClassSkillWeight(sg.bonus);
                    weightsPtr[sIdx] = static_cast<std::uint8_t>(std::clamp(std::lround(normWeight), 1l, 10l));
                }
            }
        }

        spec.cachedRuntimeClass = runtimeClass;
        return spec.cachedRuntimeClass;
    }

    RE::TESCombatStyle* AdultChildProgression::GetCombatStyle(std::int32_t classIndex) {
        if (!IsValidTrainingClass(classIndex)) {
            MARAS_LOG_ERROR("[ChildTraining] GetCombatStyle called with invalid classIndex {}", classIndex);
            return nullptr;
        }
        auto& spec = catalog_[static_cast<std::size_t>(classIndex)];
        if (spec.cachedCombatStyle) {
            return spec.cachedCombatStyle;
        }
        for (const auto& key : spec.combatStyleKeys) {
            if (auto* cs = ResolveFormFromKeyString<RE::TESCombatStyle>(key, "adult child combatStyle")) {
                spec.cachedCombatStyle = cs;
                break;
            }
        }
        if (!spec.cachedCombatStyle) {
            MARAS_LOG_ERROR("[ChildTraining] Failed to resolve combatStyle for training '{}' (idx={})", spec.id, classIndex);
        }
        return spec.cachedCombatStyle;
    }

    RE::BGSOutfit* AdultChildProgression::GetClassOutfit(std::int32_t classIndex) {
        if (!IsValidTrainingClass(classIndex)) {
            MARAS_LOG_ERROR("[ChildTraining] GetClassOutfit called with invalid classIndex {}", classIndex);
            return nullptr;
        }
        auto& spec = catalog_[static_cast<std::size_t>(classIndex)];
        if (spec.cachedOutfit) {
            return spec.cachedOutfit;
        }
        for (const auto& key : spec.outfitKeys) {
            if (auto* outfit = ResolveFormFromKeyString<RE::BGSOutfit>(key, "adult child outfit")) {
                spec.cachedOutfit = outfit;
                break;
            }
        }
        if (!spec.cachedOutfit) {
            MARAS_LOG_ERROR("[ChildTraining] Failed to resolve outfit for training '{}' (idx={})", spec.id, classIndex);
        }
        return spec.cachedOutfit;
    }

    std::vector<RE::SpellItem*> AdultChildProgression::GetClassSpells(std::int32_t classIndex) {
        std::vector<RE::SpellItem*> out;
        if (!IsValidTrainingClass(classIndex)) {
            return out;
        }
        const auto& spec = catalog_[static_cast<std::size_t>(classIndex)];
        for (const auto& key : spec.spellKeys) {
            if (auto* spell = ResolveFormFromKeyString<RE::SpellItem>(key, "adult child spell")) {
                out.push_back(spell);
            }
        }
        return out;
    }

    std::vector<RE::BGSPerk*> AdultChildProgression::GetClassPerks(std::int32_t classIndex) {
        std::vector<RE::BGSPerk*> out;
        if (!IsValidTrainingClass(classIndex)) {
            return out;
        }
        const auto& spec = catalog_[static_cast<std::size_t>(classIndex)];
        for (const auto& key : spec.perkKeys) {
            if (auto* perk = ResolveFormFromKeyString<RE::BGSPerk>(key, "adult child perk")) {
                out.push_back(perk);
            }
        }
        return out;
    }

    std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> AdultChildProgression::GetClassEquipment(
        std::int32_t classIndex) {
        std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> out;
        if (!IsValidTrainingClass(classIndex)) {
            return out;
        }
        const auto& spec = catalog_[static_cast<std::size_t>(classIndex)];
        for (const auto& itemSpec : spec.items) {
            if (itemSpec.count <= 0) continue;
            if (auto* obj = ResolveFormFromKeyString<RE::TESBoundObject>(itemSpec.formKey, "adult child item")) {
                out.emplace_back(obj, itemSpec.count);
            }
        }
        return out;
    }

    RE::TESNPC* AdultChildProgression::GetOriginalAdultBase(RE::FormID childActorID) const {
        if (auto it = originalAdultBases_.find(childActorID); it != originalAdultBases_.end()) {
            return it->second;
        }
        return nullptr;
    }

    void AdultChildProgression::ReleaseRuntimeBaseForActor(RE::FormID childActorID) {
        if (childActorID == 0) {
            return;
        }
        auto origIt = originalAdultBases_.find(childActorID);
        auto cloneIt = clonedAdultBases_.find(childActorID);
        if (cloneIt != clonedAdultBases_.end() && cloneIt->second) {
            RE::TESNPC* cloneBase = cloneIt->second;
            if (origIt != originalAdultBases_.end() && origIt->second) {
                if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(childActorID)) {
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

    void AdultChildProgression::FlushRetiredForms() {
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

    void AdultChildProgression::RevertRuntimeBases() {
        for (const auto& [actorID, origBase] : originalAdultBases_) {
            if (!origBase) continue;
            auto cloneIt = clonedAdultBases_.find(actorID);
            RE::TESNPC* cloneBase = (cloneIt != clonedAdultBases_.end()) ? cloneIt->second : nullptr;
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID)) {
                if (!cloneBase || actor->data.objectReference == cloneBase) {
                    actor->data.objectReference = origBase;
                }
            }
        }
        for (auto& [actorID, cloneBase] : clonedAdultBases_) {
            if (cloneBase) {
                retiredClonedBases_.push_back(cloneBase);
            }
        }
        originalAdultBases_.clear();
        clonedAdultBases_.clear();
    }

    void AdultChildProgression::ClearCachedCatalogForms() {
        for (auto& spec : catalog_) {
            if (spec.cachedRuntimeClass) {
                retiredClasses_.push_back(spec.cachedRuntimeClass);
                spec.cachedRuntimeClass = nullptr;
            }
            spec.cachedCombatStyle = nullptr;
            spec.cachedOutfit = nullptr;
        }
    }

    void AdultChildProgression::Revert() {
        RevertRuntimeBases();
        ClearCachedCatalogForms();
    }

    const AdultChildProgression::DaedricBlessingRuntimeSpec* AdultChildProgression::FindDaedricBlessingById(
        std::string_view blessingId) const noexcept {
        if (blessingId.empty() || StringUtils::CaseInsensitiveEquals(blessingId, "none")) {
            return nullptr;
        }
        for (const auto& b : daedricBlessings_) {
            if (StringUtils::CaseInsensitiveEquals(b.id, blessingId) ||
                StringUtils::CaseInsensitiveEquals(b.name, blessingId)) {
                return &b;
            }
        }
        return nullptr;
    }

    void AdultChildProgression::EvaluateChildBioLineageAtBirth(NPCRelationshipManager& mgr, ChildRegistryEntry& entry) {
        if (entry.bioLineageEvaluated) {
            return;
        }
        entry.bioSkillBonuses.fill(0);

        std::int32_t motherBonus = 0;
        std::int32_t fatherBonus = 0;

        for (RE::FormID bioParentID : {entry.bioMotherID, entry.bioFatherID}) {
            if (bioParentID == 0 || bioParentID == kUnknownFatherFormID) {
                continue;
            }
            const std::uint16_t parentLevel = ResolveActorEffectiveLevel(bioParentID);
            const std::int32_t bonusPts = ComputeBioParentSkillBonus(parentLevel);
            if (bioParentID == entry.bioMotherID) {
                motherBonus = bonusPts;
            } else {
                fatherBonus = bonusPts;
            }
            for (RE::ActorValue sigAV : ResolveParentSignatureSkills(mgr, bioParentID)) {
                const int sIdx = static_cast<int>(sigAV) - static_cast<int>(RE::ActorValue::kOneHanded);
                if (sIdx >= 0 && sIdx < 18) {
                    const int nextVal = std::clamp(static_cast<int>(entry.bioSkillBonuses[sIdx]) + bonusPts, 0, 32);
                    entry.bioSkillBonuses[sIdx] = static_cast<std::uint8_t>(nextVal);
                }
            }
        }

        entry.bioLineageEvaluated = true;
        MARAS_LOG_INFO(
            "[ChildTraining] Evaluated bio-parent skill snapshot for child {:08X} ('{}'): bioMother={:08X} (+{} pts/skill), bioFather={:08X} (+{} pts/skill)",
            entry.childID, entry.name, entry.bioMotherID, motherBonus, entry.bioFatherID, fatherBonus);
    }

    void AdultChildProgression::EvaluateChildGuardianLineageAtGraduation(NPCRelationshipManager& mgr,
                                                                         ChildRegistryEntry& entry) {
        if (!entry.bioLineageEvaluated) {
            EvaluateChildBioLineageAtBirth(mgr, entry);
        }

        if (!entry.guardianLineageEvaluated) {
            entry.guardianSkillBonuses.fill(0);
            for (RE::FormID gID : entry.GetGuardians()) {
                if (gID == 0) {
                    continue;
                }
                const std::uint16_t guardLevel = ResolveActorEffectiveLevel(gID);
                const std::int32_t bonusPts = ComputeGuardianSkillBonus(guardLevel);
                for (RE::ActorValue sigAV : ResolveParentSignatureSkills(mgr, gID)) {
                    const int sIdx = static_cast<int>(sigAV) - static_cast<int>(RE::ActorValue::kOneHanded);
                    if (sIdx >= 0 && sIdx < 18) {
                        const int nextVal = std::clamp(static_cast<int>(entry.guardianSkillBonuses[sIdx]) + bonusPts, 0, 30);
                        entry.guardianSkillBonuses[sIdx] = static_cast<std::uint8_t>(nextVal);
                    }
                }
            }
            entry.guardianLineageEvaluated = true;
            MARAS_LOG_INFO(
                "[ChildTraining] Evaluated guardian mentorship skill snapshot for child {:08X} ('{}'): guardianA={:08X}, guardianB={:08X}",
                entry.childID, entry.name, entry.guardianAID, entry.guardianBID);
        }

        if (entry.daedricBlessingId.empty()) {
            const bool playerIsBioParent =
                Utils::MatchesActor(entry.bioMotherID, kPlayerFormID) ||
                Utils::MatchesActor(entry.bioFatherID, kPlayerFormID);
            const bool playerIsGuardian = entry.HasGuardian(kPlayerFormID);
            if ((playerIsBioParent || playerIsGuardian) && !daedricBlessings_.empty()) {
                std::vector<const DaedricBlessingRuntimeSpec*> activeBlessings;
                for (const auto& b : daedricBlessings_) {
                    if (auto* q = ResolveFormFromKeyString<RE::TESQuest>(b.questKey, "Daedric blessing quest")) {
                        if (q->IsCompleted()) {
                            activeBlessings.push_back(&b);
                        }
                    }
                }
                if (!activeBlessings.empty()) {
                    const std::uint32_t seed = entry.childID ^ (entry.bioMotherID << 1) ^ (entry.bioFatherID << 2) ^
                                               static_cast<std::uint32_t>(std::hash<std::string>{}(entry.name));
                    std::mt19937 blessingRng(seed ^ 0x9E3779B9u);
                    std::uniform_int_distribution<std::size_t> dist(0, activeBlessings.size() - 1);
                    const DaedricBlessingRuntimeSpec* chosen = activeBlessings[dist(blessingRng)];
                    entry.daedricBlessingId = chosen->id;
                } else {
                    entry.daedricBlessingId = "none";
                }
            } else {
                entry.daedricBlessingId = "none";
            }
        }
    }

    RE::TESNPC* AdultChildProgression::EnsurePerActorRuntimeBase(NPCRelationshipManager& mgr, RE::Actor* actor,
                                                                 ChildRegistryEntry& entry) {
        if (!actor || entry.stage != ChildStage::Adult || IsSyntheticChildID(entry.childID)) {
            return actor ? actor->GetActorBase() : nullptr;
        }

        const RE::FormID childID = entry.childID;
        RE::TESNPC* origNpc = nullptr;
        if (auto origIt = originalAdultBases_.find(childID); origIt != originalAdultBases_.end() && origIt->second) {
            origNpc = origIt->second;
        } else {
            origNpc = actor->GetActorBase();
            if (origNpc) {
                originalAdultBases_[childID] = origNpc;
            }
        }
        if (!origNpc) {
            return nullptr;
        }

        RE::TESNPC* cloneNpc = nullptr;
        if (auto cloneIt = clonedAdultBases_.find(childID); cloneIt != clonedAdultBases_.end() && cloneIt->second) {
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
                !entry.name.empty() ? entry.name.c_str() : (origNpcName ? origNpcName : ""));

            clonedAdultBases_[childID] = cloneNpc;
        }

        if (!catalog_.empty()) {
            EvaluateChildGuardianLineageAtGraduation(mgr, entry);

            std::int32_t trainIdx = entry.trainingClassIndex;
            if (!IsValidTrainingClass(trainIdx) && !entry.trainingLocation.empty()) {
                trainIdx = FindTrainingClassByLocation(entry.trainingLocation);
            }
            if (!IsValidTrainingClass(trainIdx)) {
                trainIdx = (catalog_.size() > 1) ? 1 : 0;
            }
            const auto& classSpec = catalog_[static_cast<std::size_t>(trainIdx)];

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
            const float levelFactor = std::clamp((static_cast<float>(effectiveLevel) - 10.0f) / 40.0f, 0.0f, 1.0f);
            cloneNpc->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kPCLevelMult);
            cloneNpc->actorData.level = effectiveLevel;

            // Deterministically populate cloneNpc->playerSkills & cloneNpc->aiData without touching origNpc or calling AddChange
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
                    skillMap[av] += static_cast<float>(entry.bioSkillBonuses[sIdx]);
                    skillMap[av] += static_cast<float>(entry.guardianSkillBonuses[sIdx]);
                }
            }
            const float pointsPerWeight = 5.0f + 11.0f * levelFactor;  // +5/weight at Lv 10 -> +16/weight at Lv 50
            for (const auto& sg : classSpec.skills) {
                const float normWeight = NormalizeClassSkillWeight(sg.bonus);
                skillMap[sg.skill] += normWeight * pointsPerWeight;
            }

            float daedricBonusHealth = 0.0f;
            float daedricBonusMagicka = 0.0f;
            float daedricBonusStamina = 0.0f;
            if (const auto* chosenBlessing = FindDaedricBlessingById(entry.daedricBlessingId)) {
                for (const auto& sb : chosenBlessing->skills) {
                    skillMap[sb.skill] += sb.bonus;
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

            const auto& classHMS = classSpec.hmsWeights;
            std::array<float, 3> bioHMS = {0.50f, 0.25f, 0.25f};
            int bioCount = 0;
            std::array<float, 3> bioSum = {0.0f, 0.0f, 0.0f};
            for (RE::FormID bioParentID : {entry.bioMotherID, entry.bioFatherID}) {
                if (bioParentID == 0 || bioParentID == kUnknownFatherFormID) continue;
                const auto w = GetHMSWeightsForSkillType(ResolveActorEffectiveSkillType(mgr, bioParentID));
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
            const float totalBonusPool = 80.0f + 320.0f * levelFactor;  // +80 at Lv 10 -> +400 at Lv 50
            const float beastbloodHealthBonus = entry.hasBeastblood ? 25.0f : 0.0f;
            const float beastbloodStaminaBonus = entry.hasBeastblood ? 50.0f : 0.0f;

            const float finalHealth = std::round(100.0f + totalBonusPool * hWeight + daedricBonusHealth + beastbloodHealthBonus);
            const float finalMagicka = std::round(100.0f + totalBonusPool * mWeight + daedricBonusMagicka);
            const float finalStamina = std::round(100.0f + totalBonusPool * sWeight + daedricBonusStamina + beastbloodStaminaBonus);

            cloneNpc->playerSkills.health = static_cast<std::uint16_t>(std::clamp(finalHealth, 50.0f, 2000.0f));
            cloneNpc->playerSkills.magicka = static_cast<std::uint16_t>(std::clamp(finalMagicka, 50.0f, 2000.0f));
            cloneNpc->playerSkills.stamina = static_cast<std::uint16_t>(std::clamp(finalStamina, 50.0f, 2000.0f));

            float confidence = 2.0f;
            float morality = 3.0f;
            int guardAICount = 0;
            float guardConfSum = 0.0f;
            float guardMoralSum = 0.0f;
            for (RE::FormID gID : entry.GetGuardians()) {
                if (gID == 0 || Utils::MatchesActor(gID, kPlayerFormID)) continue;
                if (auto* gActor = ResolveActorPtr(gID)) {
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
            if (classSpec.moralityOverride >= 0.0f) {
                morality = classSpec.moralityOverride;
            }
            confidence = std::max(confidence, classSpec.minConfidence);

            cloneNpc->SetAggressionLevel(RE::ACTOR_AGGRESSION::kAggressive);
            cloneNpc->SetConfidenceLevel(static_cast<RE::ACTOR_CONFIDENCE>(static_cast<int>( std::lround(confidence))));
            cloneNpc->SetAssistanceLevel(RE::ACTOR_ASSISTANCE::kHelpsFriends);
            const int moralInt = std::clamp(static_cast<int>(std::lround(morality)), 0, 3);
            cloneNpc->aiData.morality1 = (moralInt & 1) != 0;
            cloneNpc->aiData.morality2 = (moralInt & 2) != 0;
        }

        if (entry.hairColorFormID != 0 && cloneNpc->headRelatedData) {
            if (auto* hc = RE::TESForm::LookupByID<RE::BGSColorForm>(entry.hairColorFormID)) {
                cloneNpc->headRelatedData->hairColor = hc;
            }
        }

        actor->data.objectReference = cloneNpc;
        return cloneNpc;
    }

    void AdultChildProgression::InitializeAndApplyChildVisualsAndRPG(NPCRelationshipManager& mgr, RE::FormID childID) {
        if (childID == 0 || IsSyntheticChildID(childID)) {
            return;
        }
        auto entryOpt = mgr.GetChildEntry(childID);
        if (!entryOpt.has_value()) {
            MARAS_LOG_WARN("[ChildTraining] InitializeAndApplyChildVisualsAndRPG: child {:08X} not found in childrenByID", childID);
            return;
        }
        auto& entry = *entryOpt;
        auto* actor = ResolveActorPtr(childID);
        if (!actor) {
            MARAS_LOG_WARN("[ChildTraining] InitializeAndApplyChildVisualsAndRPG: failed to resolve Actor* for child {:08X} ('{}')",
                           childID, entry.name);
            return;
        }
        auto* childNpc = actor->GetActorBase();
        if (!childNpc) {
            MARAS_LOG_WARN("[ChildTraining] InitializeAndApplyChildVisualsAndRPG: Actor {:08X} ('{}') has null ActorBase",
                           childID, entry.name);
            return;
        }

        const std::uint32_t seed = childID ^ (entry.bioMotherID << 1) ^ (entry.bioFatherID << 2) ^
                                   static_cast<std::uint32_t>(std::hash<std::string>{}(entry.name));
        std::mt19937 rng(seed);

        auto* motherNpc = GetActorBaseNPC(entry.bioMotherID);
        auto* fatherNpc = GetActorBaseNPC(entry.bioFatherID);

        // -----------------------------------------------------------------
        // 1. PILLAR 1: BIOLOGICAL PARENTS — VISUAL GENETICS
        // -----------------------------------------------------------------
        if (entry.hairColorFormID == 0) {
            std::vector<RE::FormID> parentHairColors;
            if (motherNpc && motherNpc->headRelatedData && motherNpc->headRelatedData->hairColor) {
                parentHairColors.push_back(motherNpc->headRelatedData->hairColor->GetFormID());
            }
            if (fatherNpc && fatherNpc->headRelatedData && fatherNpc->headRelatedData->hairColor) {
                parentHairColors.push_back(fatherNpc->headRelatedData->hairColor->GetFormID());
            }
            if (!parentHairColors.empty()) {
                std::uniform_int_distribution<std::size_t> dist(0, parentHairColors.size() - 1);
                entry.hairColorFormID = parentHairColors[dist(rng)];
            } else if (childNpc->headRelatedData && childNpc->headRelatedData->hairColor) {
                entry.hairColorFormID = childNpc->headRelatedData->hairColor->GetFormID();
            }
        }

        if (entry.stage == ChildStage::Adult) {
            auto* childRace = actor->GetRace();
            const bool isFemale = (childNpc->GetSex() == RE::SEX::kFemale);

            // Height / Scale inheritance from bio parents (+/- 0.03f genetic variance, clamped 0.94..1.06)
            if (!entry.rpgApplied && entry.heightScale == 1.0f) {
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
                entry.heightScale = std::clamp(baseHeight + jitterDist(rng), 0.94f, 1.06f);
            }

            // Gather valid vanilla headparts for child's race & sex
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

            // Resolve Eye Color HeadPart from bioMother / bioFather
            if (entry.eyesPartFormID == 0 && childRace) {
                std::array<RE::TESNPC*, 2> donors = {motherNpc, fatherNpc};
                if (std::uniform_int_distribution<int>(0, 1)(rng) == 1) {
                    std::swap(donors[0], donors[1]);
                }
                for (auto* donor : donors) {
                    if (!donor) continue;
                    auto* donorEyes = donor->GetCurrentHeadPartByType(RE::BGSHeadPart::HeadPartType::kEyes);
                    if (!donorEyes) continue;
                    if (IsHeadPartValidForChild(donorEyes, childRace, isFemale, RE::BGSHeadPart::HeadPartType::kEyes, false)) {
                        entry.eyesPartFormID = donorEyes->GetFormID();
                        break;
                    }
                    std::string_view colorToken = ExtractEyeColorKeyword(donorEyes->formEditorID.c_str());
                    if (!colorToken.empty()) {
                        for (auto* candidate : validEyes) {
                            if (ContainsInsensitive(candidate->formEditorID.c_str(), colorToken)) {
                                entry.eyesPartFormID = candidate->GetFormID();
                                break;
                            }
                        }
                        if (entry.eyesPartFormID != 0) break;
                    }
                }
                if (entry.eyesPartFormID == 0 && !validEyes.empty()) {
                    std::uniform_int_distribution<std::size_t> dist(0, validEyes.size() - 1);
                    entry.eyesPartFormID = validEyes[dist(rng)]->GetFormID();
                }
            }

            // Resolve Hairstyle HeadPart (35% same-sex parent inheritance if valid, 65% varied playable hair)
            if (entry.hairPartFormID == 0 && childRace) {
                auto* sameSexParent = isFemale ? motherNpc : fatherNpc;
                if (sameSexParent && std::uniform_int_distribution<int>(1, 100)(rng) <= 35) {
                    auto* parentHair = sameSexParent->GetCurrentHeadPartByType(RE::BGSHeadPart::HeadPartType::kHair);
                    if (IsHeadPartValidForChild(parentHair, childRace, isFemale, RE::BGSHeadPart::HeadPartType::kHair, false)) {
                        entry.hairPartFormID = parentHair->GetFormID();
                    }
                }
                if (entry.hairPartFormID == 0 && !validHairs.empty()) {
                    std::uniform_int_distribution<std::size_t> dist(0, validHairs.size() - 1);
                    entry.hairPartFormID = validHairs[dist(rng)]->GetFormID();
                }
            }

            // Resolve Facial Hair HeadPart for male adult children whose template has a facial hair node
            if (!isFemale && entry.facialHairPartFormID == 0 && childRace &&
                childNpc->GetCurrentHeadPartByType(RE::BGSHeadPart::HeadPartType::kFacialHair) != nullptr) {
                if (fatherNpc && std::uniform_int_distribution<int>(1, 100)(rng) <= 45) {
                    auto* fatherBeard = fatherNpc->GetCurrentHeadPartByType(RE::BGSHeadPart::HeadPartType::kFacialHair);
                    if (IsHeadPartValidForChild(fatherBeard, childRace, false,
                                                RE::BGSHeadPart::HeadPartType::kFacialHair, false)) {
                        entry.facialHairPartFormID = fatherBeard->GetFormID();
                    }
                }
                if (entry.facialHairPartFormID == 0 && !validBeards.empty()) {
                    std::uniform_int_distribution<std::size_t> dist(0, validBeards.size() - 1);
                    entry.facialHairPartFormID = validBeards[dist(rng)]->GetFormID();
                }
            }

            if (!entry.beastbloodEvaluated) {
                NPCRelationshipManager::EvaluateChildBeastbloodInheritance(entry);
            }

            // Attach an isolated per-actor runtime TESNPC clone before any RPG / inventory / 3D operations run
            childNpc = EnsurePerActorRuntimeBase(mgr, actor, entry);
        }

        // -----------------------------------------------------------------
        // 2. PILLARS 1, 2 & 3 + DAEDRIC LINEAGE: ADULT CHILD RPG PROGRESSION
        // -----------------------------------------------------------------
        if (entry.stage == ChildStage::Adult && !entry.rpgApplied && catalog_.empty()) {
            MARAS_LOG_WARN("[ChildTraining] Cannot apply adult child RPG progression for {:08X} ('{}'): training catalog is empty!",
                           childID, entry.name);
        }
        if (entry.stage == ChildStage::Adult && !entry.rpgApplied && !catalog_.empty()) {
            // Pillar 2 (Guardians — strictly slot-neutral via entry.GetGuardians()):
            // Inherit SocialClass from the higher-standing custodial guardian.
            SocialClass inheritedSocialClass = SocialClass::Poverty;
            bool foundGuardian = false;
            SkillType guardianFallbackSkillType = SkillType::Warrior;

            for (RE::FormID gID : entry.GetGuardians()) {
                if (gID == 0) continue;
                SocialClass gSocialClass = SocialClass::Middle;
                if (Utils::MatchesActor(gID, kPlayerFormID)) {
                    gSocialClass = (PlayerHouseService::GetSingleton().CountPlayerHouses() > 0)
                                       ? SocialClass::Wealthy
                                       : SocialClass::Middle;
                } else {
                    gSocialClass = mgr.GetSocialClass(gID);
                }

                if (!foundGuardian ||
                    GetSocialClassRankOrder(gSocialClass) > GetSocialClassRankOrder(inheritedSocialClass)) {
                    inheritedSocialClass = gSocialClass;
                }
                if (!foundGuardian) {
                    guardianFallbackSkillType = ResolveActorEffectiveSkillType(mgr, gID);
                }
                foundGuardian = true;
            }

            std::int32_t trainIdx = entry.trainingClassIndex;
            if (!IsValidTrainingClass(trainIdx) && !entry.trainingLocation.empty()) {
                trainIdx = FindTrainingClassByLocation(entry.trainingLocation);
            }
            if (!IsValidTrainingClass(trainIdx)) {
                trainIdx = (catalog_.size() > 1) ? 1 : 0;  // Default Warrior if untrained
            }

            const auto& classSpec = catalog_[static_cast<std::size_t>(trainIdx)];
            const SkillType childSkillType = IsValidTrainingClass(entry.trainingClassIndex)
                                                 ? classSpec.mappedSkillType
                                                 : guardianFallbackSkillType;
            const Temperament childTemperament = NPCTypeDeterminer::ComputeTemperament(inheritedSocialClass, childSkillType);

            // Store adult child archetype profile and sync M.A.R.A.S engine mirror factions
            mgr.UpsertNPCProfile(NPCProfileData(childID, inheritedSocialClass, childSkillType, childTemperament));
            mgr.AddToSocialClassFaction(childID, static_cast<std::int8_t>(inheritedSocialClass));
            mgr.AddToSkillTypeFaction(childID, static_cast<std::int8_t>(childSkillType));
            mgr.AddToTemperamentFaction(childID, static_cast<std::int8_t>(childTemperament));

            std::string inheritedDaedricPatron;
            const float finalHealth = childNpc ? static_cast<float>(childNpc->playerSkills.health) : 100.0f;
            const float finalMagicka = childNpc ? static_cast<float>(childNpc->playerSkills.magicka) : 100.0f;
            const float finalStamina = childNpc ? static_cast<float>(childNpc->playerSkills.stamina) : 100.0f;
            const float finalConfidence = childNpc ? static_cast<float>(std::to_underlying(childNpc->GetConfidenceLevel())) : 2.0f;
            const float finalMorality = childNpc ? static_cast<float>((childNpc->aiData.morality1 ? 1 : 0) | (childNpc->aiData.morality2 ? 2 : 0)) : 3.0f;
            std::size_t daedricSpellsAdded = 0;

            if (const auto* chosenBlessing = FindDaedricBlessingById(entry.daedricBlessingId)) {
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
                entry.lastAppliedLevel = childNpc->actorData.level;
            }

            // Pillar 3: Class Spells from childTraining.yaml
            std::size_t classSpellsAdded = 0;
            for (auto* spell : GetClassSpells(trainIdx)) {
                if (spell && !actor->HasSpell(spell)) {
                    actor->AddSpell(spell);
                    ++classSpellsAdded;
                }
            }

            // Pillar 1: Hereditary Beastblood (Lycanthropy)
            if (entry.hasBeastblood) {
                auto& fc = FormCache::GetSingleton();
                if (auto* wwImmunity = fc.GetWerewolfImmunitySpell(); wwImmunity && !actor->HasSpell(wwImmunity)) {
                    actor->AddSpell(wwImmunity);
                }
                if (auto* wwChange = fc.GetWerewolfChangeSpell(); wwChange && !actor->HasSpell(wwChange)) {
                    actor->AddSpell(wwChange);
                }
                if (auto* playerWWFac = fc.GetPlayerWerewolfFaction()) {
                    Utils::AddToFaction(actor, playerWWFac, 0);
                }
                if (StringUtils::CaseInsensitiveEquals(classSpec.id, "pack_berserker")) {
                    if (auto* circleFac = fc.GetCompanionsCircleFaction()) {
                        Utils::AddToFaction(actor, circleFac, 0);
                    }
                } else if (StringUtils::CaseInsensitiveEquals(classSpec.id, "pack_hunter")) {
                    if (auto* frostmoonFac = fc.GetFrostmoonWerewolvesFaction()) {
                        Utils::AddToFaction(actor, frostmoonFac, 0);
                    }
                }
            }

            // Pillar 3: Unconditionally replace template outfit & weapons on the Actor instance (NEVER mutate shared ActorBase)
            actor->InitInventoryIfRequired();

            auto* origNpc = GetOriginalAdultBase(childID);
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
            } else if (!classSpec.outfitKeys.empty()) {
                MARAS_LOG_WARN("[ChildTraining] Failed to resolve class outfit for adult child {:08X} ('{}', class='{}')",
                               childID, entry.name, classSpec.className);
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

            // Pillar 2: Starting Pocket Gold scaled by inherited SocialClass
            std::int32_t goldAmount = 0;
            if (auto* goldObj = FormCache::GetSingleton().GetGoldBaseObject()) {
                goldAmount = 50;
                switch (inheritedSocialClass) {
                    case SocialClass::Outcast:
                    case SocialClass::Poverty:
                        goldAmount = 25;
                        break;
                    case SocialClass::Working:
                        goldAmount = 50;
                        break;
                    case SocialClass::Middle:
                    case SocialClass::Religious:
                        goldAmount = 100;
                        break;
                    case SocialClass::Wealthy:
                        goldAmount = 250;
                        break;
                    case SocialClass::Nobles:
                    case SocialClass::Rulers:
                        goldAmount = 500;
                        break;
                    default:
                        goldAmount = 50;
                        break;
                }
                actor->AddObjectToContainer(goldObj, nullptr, goldAmount, nullptr);
            }

            entry.rpgApplied = true;
            MARAS_LOG_INFO(
                "[ChildTraining] Applied 3-pillar RPG progression to adult child {:08X} ('{}', lv={}): class='{}' (idx={}, loc='{}'), "
                "social={}, skill={}, temp={}, HMS={:.0f}/{:.0f}/{:.0f}, AI(conf={:.0f}, moral={:.0f}), "
                "outfit={:08X}, items={}, spells={} (+{} Daedric), gold={}, daedricPatron='{}', hasBeastblood={}, "
                "visuals(height={:.2f}, hairColor={:08X}, eyes={:08X}, hair={:08X}, beard={:08X})",
                childID, entry.name, childNpc ? childNpc->actorData.level : GetEffectiveChildLevel(),
                GetTrainingClassName(trainIdx), trainIdx, GetTrainingLocationName(trainIdx),
                Utils::SocialClassToString(inheritedSocialClass), Utils::SkillTypeToString(childSkillType),
                Utils::TemperamentToString(childTemperament), finalHealth, finalMagicka, finalStamina,
                finalConfidence, finalMorality, classOutfit ? classOutfit->GetFormID() : 0, itemsGranted,
                classSpellsAdded, daedricSpellsAdded, goldAmount,
                inheritedDaedricPatron.empty() ? "None" : inheritedDaedricPatron, entry.hasBeastblood,
                entry.heightScale, entry.hairColorFormID, entry.eyesPartFormID, entry.hairPartFormID,
                entry.facialHairPartFormID);
        }

        // Apply per-actor 3D visuals and runtime ExtraCombatStyle
        ApplyChildVisualsToActor(actor, entry);
        mgr.UpsertChildEntry(entry);
    }

    void AdultChildProgression::ApplyChildVisualsToActor(RE::Actor* actor, ChildRegistryEntry& entry) {
        if (!actor || IsSyntheticChildID(entry.childID)) {
            return;
        }
        auto* npc = (entry.stage == ChildStage::Adult)
                        ? EnsurePerActorRuntimeBase(NPCRelationshipManager::GetSingleton(), actor, entry)
                        : actor->GetActorBase();
        if (!npc) {
            return;
        }

        if (entry.stage == ChildStage::Adult) {
            if (entry.heightScale > 0.5f && entry.heightScale < 1.5f &&
                std::abs(actor->GetScale() - entry.heightScale) > 0.005f) {
                actor->SetScale(entry.heightScale);
            }

            std::int32_t trainIdx = entry.trainingClassIndex;
            if (!IsValidTrainingClass(trainIdx) && !entry.trainingLocation.empty()) {
                trainIdx = FindTrainingClassByLocation(entry.trainingLocation);
            }
            if (!IsValidTrainingClass(trainIdx)) {
                trainIdx = (catalog_.size() > 1) ? 1 : 0;
            }
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

            // Sync scaled HMS and skills when child's effective level changes
            const std::uint16_t effectiveLevel = npc->actorData.level;
            if (auto* avOwner = actor->AsActorValueOwner()) {
                if (entry.lastAppliedLevel != effectiveLevel) {
                    const std::uint16_t prevLevel = entry.lastAppliedLevel;
                    avOwner->SetBaseActorValue(RE::ActorValue::kHealth, static_cast<float>(npc->playerSkills.health));
                    avOwner->SetBaseActorValue(RE::ActorValue::kMagicka, static_cast<float>(npc->playerSkills.magicka));
                    avOwner->SetBaseActorValue(RE::ActorValue::kStamina, static_cast<float>(npc->playerSkills.stamina));
                    for (RE::ActorValue av : kAllSkills) {
                        const int sIdx = static_cast<int>(av) - static_cast<int>(RE::ActorValue::kOneHanded);
                        if (sIdx >= 0 && sIdx < RE::TESNPC::Skills::kTotal) {
                            avOwner->SetBaseActorValue(av, static_cast<float>(npc->playerSkills.values[sIdx]));
                        }
                    }
                    entry.lastAppliedLevel = effectiveLevel;
                    if (prevLevel != 0) {
                        MARAS_LOG_INFO(
                            "[ChildTraining] Scaled adult child {:08X} ('{}') level {} -> {}: HMS={}/{}/{}",
                            entry.childID, entry.name, prevLevel, effectiveLevel,
                            npc->playerSkills.health, npc->playerSkills.magicka, npc->playerSkills.stamina);
                    }
                }
            }

            // Ensure any delayed 3D-load template outfit/weapons from origNpc are stripped and classOutfit is worn
            auto* origNpc = GetOriginalAdultBase(entry.childID);
            auto* classOutfit = GetClassOutfit(trainIdx);
            if (origNpc && origNpc->defaultOutfit && origNpc->defaultOutfit != classOutfit) {
                actor->RemoveOutfitItems(origNpc->defaultOutfit);
            }

            const auto classEquip = GetClassEquipment(trainIdx);
            auto isClassEquipmentItem = [&](RE::TESBoundObject* candidate) {
                for (const auto& [eqObj, eqCount] : classEquip) {
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
                        if (item && (item->IsWeapon() || item->IsArmor() || item->IsAmmo())) {
                            equipMgr->EquipObject(actor, item);
                        }
                    }
                }
            }

            auto* xCSCheck = actor->extraList.GetByType<RE::ExtraCombatStyle>();
            MARAS_LOG_DEBUG(
                "[ChildTraining] ApplyChildVisualsToActor for {:08X} ('{}', race='{}', lv={}, 3DLoaded={}, faceNode={}): "
                "origBase={:08X}(ptr={:p}, class='{}', cs={:08X}, outfit={:08X}) -> "
                "actorBase={:08X}(ptr={:p}, class='{}', cs={:08X}, outfit={:08X}, extraCS={:08X})",
                entry.childID, entry.name,
                npc->race ? (npc->race->GetFormEditorID() ? npc->race->GetFormEditorID() : "") : "None",
                npc->actorData.level, actor->Is3DLoaded(), actor->GetFaceNodeSkinned() != nullptr,
                origNpc ? origNpc->GetFormID() : 0, static_cast<const void*>(origNpc),
                (origNpc && origNpc->npcClass) ? origNpc->npcClass->GetName() : "None",
                (origNpc && origNpc->combatStyle) ? origNpc->combatStyle->GetFormID() : 0,
                (origNpc && origNpc->defaultOutfit) ? origNpc->defaultOutfit->GetFormID() : 0,
                npc->GetFormID(), static_cast<const void*>(npc),
                npc->npcClass ? npc->npcClass->GetName() : "None",
                npc->combatStyle ? npc->combatStyle->GetFormID() : 0,
                npc->defaultOutfit ? npc->defaultOutfit->GetFormID() : 0,
                (xCSCheck && xCSCheck->combatStyle) ? xCSCheck->combatStyle->GetFormID() : 0);
            for (const auto& [boundObj, invEntry] : actor->GetInventory()) {
                if (!boundObj || invEntry.first <= 0) continue;
                if (boundObj->IsArmor() || boundObj->IsWeapon()) {
                    MARAS_LOG_TRACE("[ChildTraining] ApplyChildVisualsToActor inv on {:08X}: {:08X} ('{}') count={} worn={}",
                                    entry.childID, boundObj->GetFormID(), boundObj->GetName(), invEntry.first,
                                    invEntry.second ? invEntry.second->IsWorn() : false);
                }
            }

            if (auto* faceNode = actor->GetFaceNodeSkinned()) {
                auto swapPartIfLoaded = [&](RE::BGSHeadPart::HeadPartType type, RE::FormID targetPartID) {
                    if (targetPartID == 0) return;
                    auto* newPart = RE::TESForm::LookupByID<RE::BGSHeadPart>(targetPartID);
                    if (!newPart || newPart->formEditorID.empty()) return;
                    if (faceNode->GetObjectByName(newPart->formEditorID) != nullptr) {
                        return;  // Already active on this 3D instance
                    }
                    auto* oldPart = npc->GetCurrentHeadPartByType(type);
                    if (oldPart && oldPart != newPart && !oldPart->formEditorID.empty() &&
                        faceNode->GetObjectByName(oldPart->formEditorID) != nullptr) {
                        actor->ChangeHeadPart(oldPart, newPart);
                    }
                };

                swapPartIfLoaded(RE::BGSHeadPart::HeadPartType::kEyes, entry.eyesPartFormID);
                swapPartIfLoaded(RE::BGSHeadPart::HeadPartType::kHair, entry.hairPartFormID);
                swapPartIfLoaded(RE::BGSHeadPart::HeadPartType::kFacialHair, entry.facialHairPartFormID);
            }

            ApplyAdultChildPerks(actor, entry);
        }

        if (entry.hairColorFormID != 0) {
            if (auto* hairColor = RE::TESForm::LookupByID<RE::BGSColorForm>(entry.hairColorFormID)) {
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

    namespace {
        bool EvaluatePerkConditionComparison(float lhs, RE::CONDITION_ITEM_DATA::OpCode op, float rhs) noexcept {
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

        // Checks whether the adult child currently meets the character-level and skill-level
        // requirements encoded on `perk` (`perk->data.level` and `perk->perkConditions`).
        // Ignores `HasPerk` tree-link conditions so YAML presets do not fail if an intermediate node is omitted.
        bool DoesActorMeetPerkRequirements(RE::Actor* actor, const RE::TESNPC* npc, const RE::BGSPerk* perk) {
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

        // Returns true if `lower` links to `higher` via `BGSPerk::nextPerk` (multi-rank perk chain).
        bool IsEarlierRankInPerkChain(const RE::BGSPerk* lower, const RE::BGSPerk* higher) noexcept {
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

    void AdultChildProgression::ApplyAdultChildPerks(RE::Actor* actor, const ChildRegistryEntry& entry) {
        if (!actor || entry.stage != ChildStage::Adult || IsSyntheticChildID(entry.childID)) {
            return;
        }
        auto* npc = actor->GetActorBase();
        auto* origNpc = GetOriginalAdultBase(entry.childID);
        if (!npc || npc == origNpc) {
            return;
        }

        std::vector<RE::BGSPerk*> qualifiedPerks;
        qualifiedPerks.reserve(24);

        auto addUniqueQualifiedPerk = [&](RE::BGSPerk* perk) {
            if (!perk) return;
            if (std::find(qualifiedPerks.begin(), qualifiedPerks.end(), perk) == qualifiedPerks.end()) {
                qualifiedPerks.push_back(perk);
            }
        };

        // 1. Training Class Perks from childTraining.yaml — gated by current skill & level requirements
        std::int32_t trainIdx = entry.trainingClassIndex;
        if (!IsValidTrainingClass(trainIdx) && !entry.trainingLocation.empty()) {
            trainIdx = FindTrainingClassByLocation(entry.trainingLocation);
        }
        if (!IsValidTrainingClass(trainIdx)) {
            trainIdx = (catalog_.size() > 1) ? 1 : 0;
        }
        const auto allClassPerks = GetClassPerks(trainIdx);
        for (auto* perk : allClassPerks) {
            if (DoesActorMeetPerkRequirements(actor, npc, perk)) {
                addUniqueQualifiedPerk(perk);
            }
        }
        const std::size_t qualifiedClassPerkCount = qualifiedPerks.size();

        // 2. Daedric Champion Lineage Blessing Perks from childTraining.yaml
        if (const auto* chosenBlessing = FindDaedricBlessingById(entry.daedricBlessingId)) {
            for (const auto& pkKey : chosenBlessing->perkKeys) {
                if (auto* perk = ResolveFormFromKeyString<RE::BGSPerk>(pkKey, "Daedric blessing perk")) {
                    addUniqueQualifiedPerk(perk);
                }
            }
        }
        const std::size_t blessingPerkCount = qualifiedPerks.size() - qualifiedClassPerkCount;

        // 3. Deduplicate multi-rank perk chains (`BGSPerk::nextPerk`) so only the highest qualified rank is active
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

        // 4. Upgrade any lower-rank perks in-place on `npc->perks` and collect brand-new perks
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
                entry.childID, entry.name, npc->actorData.level, missingPerks.size(), upgradedCount,
                qualifiedClassPerkCount, allClassPerks.size(), blessingPerkCount, activeTargetPerks.size());
        }
    }

    void AdultChildProgression::ApplyChildVisualsOn3DLoad(NPCRelationshipManager& mgr, RE::Actor* actor) {
        if (!actor) return;
        const RE::FormID formID = actor->GetFormID();
        auto entryOpt = mgr.GetChildEntry(formID);
        if (!entryOpt.has_value()) return;
        if (entryOpt->stage != ChildStage::Child && entryOpt->stage != ChildStage::Adult) return;

        if (entryOpt->hairColorFormID == 0 || (entryOpt->stage == ChildStage::Adult && !entryOpt->rpgApplied)) {
            InitializeAndApplyChildVisualsAndRPG(mgr, formID);
        } else {
            const std::uint16_t prevLevel = entryOpt->lastAppliedLevel;
            ApplyChildVisualsToActor(actor, *entryOpt);
            if (entryOpt->lastAppliedLevel != prevLevel) {
                mgr.UpsertChildEntry(*entryOpt);
            }
        }
    }

    void AdultChildProgression::ReassertAllChildVisualsOnLoad(NPCRelationshipManager& mgr) {
        std::vector<RE::FormID> childIDs;
        const auto allChildren = mgr.GetAllChildren();
        childIDs.reserve(allChildren.size());
        for (const auto& [cID, entry] : allChildren) {
            if (!IsSyntheticChildID(cID) && (entry.stage == ChildStage::Child || entry.stage == ChildStage::Adult)) {
                childIDs.push_back(cID);
            }
        }
        for (RE::FormID cID : childIDs) {
            if (auto* actor = ResolveActorPtr(cID)) {
                ApplyChildVisualsOn3DLoad(mgr, actor);
            }
        }
    }

}  // namespace MARAS
