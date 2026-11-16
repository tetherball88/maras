#include "maras/engine/TrainingCatalogLoader.h"

#include "maras/engine/EngineLogging.h"
#include "maras/engine/FormUtils.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/StringUtils.h"

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace MARAS::Engine {

    namespace {

        constexpr const char* kDefaultConfigPath = "Data/SKSE/Plugins/MARAS/childTraining.yaml";
        constexpr const char* kFallbackConfigPath = "SKSE/Plugins/MARAS/childTraining.yaml";
        constexpr const char* kDefaultExtFolder = "Data/SKSE/Plugins/MARAS/childTraining";
        constexpr const char* kFallbackExtFolder = "SKSE/Plugins/MARAS/childTraining";

        [[nodiscard]] bool IsPlayerFormID(RE::FormID id) noexcept {
            return id == MARAS::kPlayerRefID || id == MARAS::kPlayerBaseID;
        }

        [[nodiscard]] bool MatchesActorID(RE::FormID a, RE::FormID b) noexcept {
            return a == b || (IsPlayerFormID(a) && IsPlayerFormID(b));
        }

        [[nodiscard]] bool IsValidActorLookupID(RE::FormID id) noexcept {
            return id != 0 && id != MARAS::kUnknownFatherFormID && !MARAS::IsSyntheticChildID(id);
        }

        [[nodiscard]] bool LooksLikeHexString(std::string_view token) noexcept {
            if (token.starts_with("0x") || token.starts_with("0X")) {
                token.remove_prefix(2);
            }
            if (token.empty()) {
                return false;
            }
            return std::all_of(token.begin(), token.end(), [](unsigned char ch) {
                return std::isxdigit(ch) != 0;
            });
        }

        template <typename T>
        [[nodiscard]] T* ResolveFormFromKeyString(std::string_view rawKey, std::string_view context) {
            const std::string_view trimmed = MARAS::Foundation::StringUtils::TrimView(rawKey);
            if (trimmed.empty()) {
                return nullptr;
            }

            if (trimmed.starts_with("__formData|")) {
                const auto parsed = MARAS::Utils::ParseFormKey(trimmed);
                if (!parsed.isValid) {
                    MARAS_LOG_WARN("[TrainingCatalogLoader] Malformed form key '{}' for {}", trimmed, context);
                    return nullptr;
                }
                return MARAS::Utils::LookupForm<T>(parsed.localFormID, parsed.pluginName, context);
            }

            if (const auto pipePos = trimmed.find('|'); pipePos != std::string_view::npos) {
                const std::string_view plugin = MARAS::Foundation::StringUtils::TrimView(trimmed.substr(0, pipePos));
                const std::string_view hexStr = MARAS::Foundation::StringUtils::TrimView(trimmed.substr(pipePos + 1));
                const auto formIDOpt = LooksLikeHexString(hexStr) ? MARAS::Utils::HexStringToFormID(hexStr)
                                                                    : std::optional<RE::FormID>{};
                if (!plugin.empty() && formIDOpt.has_value()) {
                    return MARAS::Utils::LookupForm<T>(*formIDOpt, plugin, context);
                }
                MARAS_LOG_WARN("[TrainingCatalogLoader] Invalid plugin|hex key '{}' for {}", trimmed, context);
                return nullptr;
            }

            if (LooksLikeHexString(trimmed)) {
                if (const auto formIDOpt = MARAS::Utils::HexStringToFormID(trimmed)) {
                    auto* form = RE::TESForm::LookupByID<T>(*formIDOpt);
                    if (!form) {
                        MARAS_LOG_WARN("[TrainingCatalogLoader] Failed lookup {:08X} ('{}') for {}",
                                       *formIDOpt, trimmed, context);
                    }
                    return form;
                }
            }

            MARAS_LOG_WARN("[TrainingCatalogLoader] Unrecognized form key '{}' for {}", trimmed, context);
            return nullptr;
        }

        [[nodiscard]] RE::Actor* ResolveActorPtr(RE::FormID formID) {
            if (!IsValidActorLookupID(formID)) {
                return nullptr;
            }
            if (IsPlayerFormID(formID)) {
                return RE::PlayerCharacter::GetSingleton();
            }
            return RE::TESForm::LookupByID<RE::Actor>(formID);
        }

        [[nodiscard]] RE::TESNPC* ResolveActorBaseNPC(RE::FormID formID) {
            if (!IsValidActorLookupID(formID)) {
                return nullptr;
            }
            if (IsPlayerFormID(formID)) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                return player ? player->GetActorBase() : nullptr;
            }
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(formID)) {
                return actor->GetActorBase();
            }
            if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(formID)) {
                if (auto* baseObj = ref->GetBaseObject()) {
                    return baseObj->As<RE::TESNPC>();
                }
            }
            return RE::TESForm::LookupByID<RE::TESNPC>(formID);
        }

        [[nodiscard]] std::string ReadTrimmedString(const nlohmann::json& obj, const char* key) {
            if (!obj.is_object() || !obj.contains(key) || !obj[key].is_string()) {
                return {};
            }
            return MARAS::Foundation::StringUtils::TrimStd(obj[key].get<std::string>());
        }

        [[nodiscard]] std::vector<std::string> ParseStringOrArrayKeys(const nlohmann::json& j,
                                                                      std::string_view fieldName) {
            std::vector<std::string> result;
            const std::string key(fieldName);
            if (!j.is_object() || !j.contains(key)) {
                return result;
            }
            const auto& val = j[key];
            if (val.is_string()) {
                std::string s = MARAS::Foundation::StringUtils::TrimStd(val.get<std::string>());
                if (!s.empty()) {
                    result.push_back(std::move(s));
                }
            } else if (val.is_array()) {
                result.reserve(val.size());
                for (const auto& elem : val) {
                    if (!elem.is_string()) {
                        continue;
                    }
                    std::string s = MARAS::Foundation::StringUtils::TrimStd(elem.get<std::string>());
                    if (!s.empty()) {
                        result.push_back(std::move(s));
                    }
                }
            }
            return result;
        }

        [[nodiscard]] std::vector<MARAS::Domain::Lineage::SkillGrantSpec> ParseSkillsMapJson(
            const nlohmann::json& skillsJson) {
            std::vector<MARAS::Domain::Lineage::SkillGrantSpec> out;
            if (!skillsJson.is_object()) {
                return out;
            }
            for (const auto& [skillKey, bonusVal] : skillsJson.items()) {
                if (!bonusVal.is_number()) {
                    continue;
                }
                const auto skillOpt = MARAS::Domain::Lineage::ParseSkillIndex(skillKey);
                if (!skillOpt.has_value()) {
                    MARAS_LOG_WARN("[TrainingCatalogLoader] Ignoring unknown skill key '{}'", skillKey);
                    continue;
                }
                const float bonus = bonusVal.get<float>();
                if (std::abs(bonus) > 0.001f) {
                    out.push_back({ *skillOpt, bonus });
                }
            }
            return out;
        }

        [[nodiscard]] std::vector<MARAS::Domain::Lineage::ItemGrantSpec> ParseItemsArrayJson(
            const nlohmann::json& itemsJson) {
            std::vector<MARAS::Domain::Lineage::ItemGrantSpec> out;
            if (!itemsJson.is_array()) {
                return out;
            }
            for (const auto& elem : itemsJson) {
                if (elem.is_string()) {
                    std::string formKey = MARAS::Foundation::StringUtils::TrimStd(elem.get<std::string>());
                    if (!formKey.empty()) {
                        out.push_back({ std::move(formKey), 1 });
                    }
                } else if (elem.is_object()) {
                    std::string formKey = ReadTrimmedString(elem, "form");
                    if (formKey.empty()) {
                        formKey = ReadTrimmedString(elem, "formKey");
                    }
                    std::int32_t count = 1;
                    if (elem.contains("count") && elem["count"].is_number_integer()) {
                        count = std::max<std::int32_t>(1, elem["count"].get<std::int32_t>());
                    }
                    if (!formKey.empty()) {
                        out.push_back({ std::move(formKey), count });
                    }
                }
            }
            return out;
        }

        void ApplyHmsWeights(const nlohmann::json& obj, MARAS::Domain::Lineage::TrainingClassSpec& target) {
            if (!obj.contains("hmsWeights")) {
                return;
            }
            const auto& hms = obj["hmsWeights"];
            float h = target.hmsWeights[0];
            float m = target.hmsWeights[1];
            float s = target.hmsWeights[2];

            if (hms.is_object()) {
                if (hms.contains("health") && hms["health"].is_number()) h = std::max(0.0f, hms["health"].get<float>());
                if (hms.contains("magicka") && hms["magicka"].is_number()) m = std::max(0.0f, hms["magicka"].get<float>());
                if (hms.contains("stamina") && hms["stamina"].is_number()) s = std::max(0.0f, hms["stamina"].get<float>());
            } else if (hms.is_array() && hms.size() >= 3 && hms[0].is_number() && hms[1].is_number() &&
                       hms[2].is_number()) {
                h = std::max(0.0f, hms[0].get<float>());
                m = std::max(0.0f, hms[1].get<float>());
                s = std::max(0.0f, hms[2].get<float>());
            }

            const float sum = h + m + s;
            if (sum > 0.001f) {
                target.hmsWeights = { h / sum, m / sum, s / sum };
            }
        }

        void ParseTrainingLocationObject(const nlohmann::json& obj,
                                         std::vector<MARAS::Domain::Lineage::TrainingClassSpec>& classes) {
            if (!obj.is_object()) {
                return;
            }

            const std::string id = ReadTrimmedString(obj, "id");
            std::string name = ReadTrimmedString(obj, "name");
            if (name.empty()) {
                name = ReadTrimmedString(obj, "className");
            }
            if (id.empty()) {
                MARAS_LOG_ERROR("[TrainingCatalogLoader] Rejecting training location config: missing required 'id'");
                return;
            }

            MARAS::Domain::Lineage::TrainingClassSpec* target = nullptr;
            for (auto& existing : classes) {
                if (MARAS::Foundation::StringUtils::CaseInsensitiveEquals(existing.id, id)) {
                    target = &existing;
                    break;
                }
            }
            if (!target && obj.contains("index") && obj["index"].is_number_integer()) {
                const int idx = obj["index"].get<int>();
                if (idx >= 0 && static_cast<std::size_t>(idx) < classes.size()) {
                    target = &classes[static_cast<std::size_t>(idx)];
                }
            }

            if (!target) {
                const auto classKeys = obj.contains("npcClass") ? ParseStringOrArrayKeys(obj, "npcClass")
                                                                : ParseStringOrArrayKeys(obj, "class");
                const auto combatStyleKeys = ParseStringOrArrayKeys(obj, "combatStyle");
                const auto outfitKeys = ParseStringOrArrayKeys(obj, "outfit");
                std::string location = ReadTrimmedString(obj, "location");
                if (location.empty()) {
                    location = ReadTrimmedString(obj, "locationName");
                }

                if (name.empty() || location.empty() || classKeys.empty() || combatStyleKeys.empty() ||
                    outfitKeys.empty()) {
                    MARAS_LOG_ERROR(
                        "[TrainingCatalogLoader] Rejecting training location '{}': missing critical fields "
                        "(name='{}', hasLocation={}, npcClass={}, combatStyle={}, outfit={})",
                        id, name, !location.empty(), !classKeys.empty(), !combatStyleKeys.empty(),
                        !outfitKeys.empty());
                    return;
                }

                MARAS::Domain::Lineage::TrainingClassSpec spec;
                spec.index = static_cast<std::int32_t>(classes.size());
                spec.id = id;
                spec.className = name;
                spec.imagePath = std::format("Data/SKSE/Plugins/MARAS/childTraining/images/{}.webp", spec.id);
                classes.push_back(std::move(spec));
                target = &classes.back();
            }

            target->id = id;
            if (!name.empty()) {
                target->className = name;
            }
            if (std::string loc = ReadTrimmedString(obj, "location"); !loc.empty()) {
                target->locationName = std::move(loc);
            } else if (std::string locAlt = ReadTrimmedString(obj, "locationName"); !locAlt.empty()) {
                target->locationName = std::move(locAlt);
            }
            if (obj.contains("image") && obj["image"].is_string()) {
                target->imagePath = MARAS::Foundation::StringUtils::TrimStd(obj["image"].get<std::string>());
            }
            if (obj.contains("description") && obj["description"].is_string()) {
                target->description = MARAS::Foundation::StringUtils::TrimStd(obj["description"].get<std::string>());
            }
            if (obj.contains("skyrimnetContext") && obj["skyrimnetContext"].is_string()) {
                target->trainingContext = MARAS::Foundation::StringUtils::TrimStd(obj["skyrimnetContext"].get<std::string>());
            } else if (obj.contains("skyrimNetContext") && obj["skyrimNetContext"].is_string()) {
                target->trainingContext = MARAS::Foundation::StringUtils::TrimStd(obj["skyrimNetContext"].get<std::string>());
            } else if (obj.contains("trainingContext") && obj["trainingContext"].is_string()) {
                target->trainingContext = MARAS::Foundation::StringUtils::TrimStd(obj["trainingContext"].get<std::string>());
            }
            if (obj.contains("costMultiplier") && obj["costMultiplier"].is_number()) {
                target->costMultiplier = std::max(0.0f, obj["costMultiplier"].get<float>());
            } else if (obj.contains("multiplier") && obj["multiplier"].is_number()) {
                target->costMultiplier = std::max(0.0f, obj["multiplier"].get<float>());
            }
            if (obj.contains("skillType") && obj["skillType"].is_string()) {
                target->mappedSkillType = MARAS::Foundation::StringToSkillType(obj["skillType"].get<std::string>());
            }
            if (obj.contains("requiresBeastblood") && obj["requiresBeastblood"].is_boolean()) {
                target->requiresBeastblood = obj["requiresBeastblood"].get<bool>();
            }

            ApplyHmsWeights(obj, *target);

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
                    target->requirementKey = ReadTrimmedString(req, "key");
                }
                if (req.contains("label") && req["label"].is_string()) {
                    target->requirementLabel = ReadTrimmedString(req, "label");
                }
                if (req.contains("faction") && req["faction"].is_string()) {
                    target->requiredFactionKey = ReadTrimmedString(req, "faction");
                }
                if (req.contains("quest") && req["quest"].is_string()) {
                    target->requiredQuestKey = ReadTrimmedString(req, "quest");
                }
                if (req.contains("questStage") && req["questStage"].is_number_integer()) {
                    target->requiredQuestStage = req["questStage"].get<std::int32_t>();
                } else if (req.contains("minQuestStage") && req["minQuestStage"].is_number_integer()) {
                    target->requiredQuestStage = req["minQuestStage"].get<std::int32_t>();
                }
            }

            if (obj.contains("affiliation") && obj["affiliation"].is_object()) {
                const auto& aff = obj["affiliation"];
                if (aff.contains("memberFactions")) target->memberFactionKeys = ParseStringOrArrayKeys(aff, "memberFactions");
                if (aff.contains("memberQuests")) target->memberQuestKeys = ParseStringOrArrayKeys(aff, "memberQuests");
                if (aff.contains("memberWerewolf") && aff["memberWerewolf"].is_boolean()) {
                    target->memberWerewolf = aff["memberWerewolf"].get<bool>();
                }
                if (aff.contains("leaderFactions")) target->leaderFactionKeys = ParseStringOrArrayKeys(aff, "leaderFactions");
                if (aff.contains("leaderFactionRank") && aff["leaderFactionRank"].is_number_integer()) {
                    target->leaderFactionRank = aff["leaderFactionRank"].get<std::int32_t>();
                }
                if (aff.contains("leaderQuests")) target->leaderQuestKeys = ParseStringOrArrayKeys(aff, "leaderQuests");
                if (aff.contains("leaderNpcs")) target->leaderNpcKeys = ParseStringOrArrayKeys(aff, "leaderNpcs");
            }

            if (obj.contains("npcClass")) {
                target->classKeys = ParseStringOrArrayKeys(obj, "npcClass");
            } else if (obj.contains("class")) {
                target->classKeys = ParseStringOrArrayKeys(obj, "class");
            }
            if (obj.contains("combatStyle")) target->combatStyleKeys = ParseStringOrArrayKeys(obj, "combatStyle");
            if (obj.contains("outfit")) target->outfitKeys = ParseStringOrArrayKeys(obj, "outfit");
            if (obj.contains("skills") && obj["skills"].is_object()) target->skills = ParseSkillsMapJson(obj["skills"]);
            if (obj.contains("spells") && obj["spells"].is_array()) target->spellKeys = ParseStringOrArrayKeys(obj, "spells");
            if (obj.contains("perks") && obj["perks"].is_array()) target->perkKeys = ParseStringOrArrayKeys(obj, "perks");
            if (obj.contains("items") && obj["items"].is_array()) target->items = ParseItemsArrayJson(obj["items"]);
        }

        void ParseDaedricBlessingObject(const nlohmann::json& obj,
                                         std::vector<MARAS::Domain::Lineage::DaedricBlessingSpec>& blessings) {
            if (!obj.is_object()) {
                return;
            }
            std::string id = ReadTrimmedString(obj, "id");
            const std::string name = ReadTrimmedString(obj, "name");
            if (id.empty() && !name.empty()) {
                id = MARAS::Foundation::StringUtils::ToLowerStd(name);
            }
            if (id.empty()) {
                return;
            }

            MARAS::Domain::Lineage::DaedricBlessingSpec* target = nullptr;
            for (auto& existing : blessings) {
                if (MARAS::Foundation::StringUtils::CaseInsensitiveEquals(existing.id, id)) {
                    target = &existing;
                    break;
                }
            }
            if (!target) {
                MARAS::Domain::Lineage::DaedricBlessingSpec spec;
                spec.id = id;
                spec.name = name.empty() ? id : name;
                blessings.push_back(std::move(spec));
                target = &blessings.back();
            }

            if (!name.empty()) {
                target->name = name;
            }
            if (obj.contains("quest") && obj["quest"].is_string()) {
                target->questKey = ReadTrimmedString(obj, "quest");
            }
            if (obj.contains("hmsBonus") && obj["hmsBonus"].is_object()) {
                const auto& hms = obj["hmsBonus"];
                if (hms.contains("health") && hms["health"].is_number()) target->bonusHealth = hms["health"].get<float>();
                if (hms.contains("magicka") && hms["magicka"].is_number()) target->bonusMagicka = hms["magicka"].get<float>();
                if (hms.contains("stamina") && hms["stamina"].is_number()) target->bonusStamina = hms["stamina"].get<float>();
            }
            if (obj.contains("bonusHealth") && obj["bonusHealth"].is_number()) target->bonusHealth = obj["bonusHealth"].get<float>();
            if (obj.contains("bonusMagicka") && obj["bonusMagicka"].is_number()) target->bonusMagicka = obj["bonusMagicka"].get<float>();
            if (obj.contains("bonusStamina") && obj["bonusStamina"].is_number()) target->bonusStamina = obj["bonusStamina"].get<float>();
            if (obj.contains("skills") && obj["skills"].is_object()) target->skills = ParseSkillsMapJson(obj["skills"]);
            if (obj.contains("spells") && obj["spells"].is_array()) target->spellKeys = ParseStringOrArrayKeys(obj, "spells");
            if (obj.contains("perks") && obj["perks"].is_array()) target->perkKeys = ParseStringOrArrayKeys(obj, "perks");
        }

        [[nodiscard]] nlohmann::json YamlNodeToJson(const YAML::Node& node) {
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

        [[nodiscard]] bool LoadYamlConfigFile(
            const std::filesystem::path& filePath,
            std::vector<MARAS::Domain::Lineage::TrainingClassSpec>& classes,
            std::vector<MARAS::Domain::Lineage::DaedricBlessingSpec>& blessings) {
            try {
                const YAML::Node yamlRoot = YAML::LoadFile(filePath.string());
                const nlohmann::json root = YamlNodeToJson(yamlRoot);

                if (root.is_array()) {
                    for (const auto& item : root) {
                        ParseTrainingLocationObject(item, classes);
                    }
                    return true;
                }
                if (!root.is_object()) {
                    return false;
                }

                if (root.contains("trainingLocations") && root["trainingLocations"].is_array()) {
                    for (const auto& item : root["trainingLocations"]) {
                        ParseTrainingLocationObject(item, classes);
                    }
                } else if (root.contains("id") || root.contains("name") || root.contains("className")) {
                    ParseTrainingLocationObject(root, classes);
                }

                if (root.contains("daedricBlessings") && root["daedricBlessings"].is_array()) {
                    for (const auto& item : root["daedricBlessings"]) {
                        ParseDaedricBlessingObject(item, blessings);
                    }
                }

                return true;
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("[TrainingCatalogLoader] Exception loading '{}': {}", filePath.string(), e.what());
                return false;
            }
        }

    }  // namespace

    bool EngineParentAffiliationPort::MatchesNpcKey(MARAS::Foundation::ActorId parentId,
                                                    std::string_view npcKey) const {
        const RE::FormID parentRaw = parentId.raw();
        if (!IsValidActorLookupID(parentRaw)) {
            return false;
        }
        auto* leaderForm = ResolveFormFromKeyString<RE::TESForm>(npcKey, "training leaderNpc");
        if (!leaderForm) {
            return false;
        }
        if (MatchesActorID(parentRaw, leaderForm->GetFormID())) {
            return true;
        }

        auto* parentBase = ResolveActorBaseNPC(parentRaw);
        if (parentBase && parentBase->GetFormID() == leaderForm->GetFormID()) {
            return true;
        }
        if (auto* leaderActor = leaderForm->As<RE::Actor>()) {
            if (auto* leaderBase = leaderActor->GetActorBase()) {
                if (MatchesActorID(parentRaw, leaderBase->GetFormID()) ||
                    (parentBase && parentBase->GetFormID() == leaderBase->GetFormID())) {
                    return true;
                }
            }
        }
        return false;
    }

    std::int32_t EngineParentAffiliationPort::GetFactionRank(MARAS::Foundation::ActorId parentId,
                                                             std::string_view factionKey) const {
        const RE::FormID actorID = parentId.raw();
        if (!IsValidActorLookupID(actorID)) {
            return -1;
        }
        auto* faction = ResolveFormFromKeyString<RE::TESFaction>(factionKey, "training faction");
        if (!faction) {
            return -1;
        }

        auto* actor = ResolveActorPtr(actorID);
        RE::TESObjectREFR* ref = actor ? static_cast<RE::TESObjectREFR*>(actor)
                                       : RE::TESForm::LookupByID<RE::TESObjectREFR>(actorID);
        if (ref) {
            if (auto* xFac = ref->extraList.GetByType<RE::ExtraFactionChanges>()) {
                for (const auto& f : xFac->factionChanges) {
                    if (f.faction == faction) {
                        return static_cast<std::int32_t>(f.rank);
                    }
                }
            }
        }
        if (auto* base = ResolveActorBaseNPC(actorID)) {
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

    bool EngineParentAffiliationPort::IsQuestCompleted(std::string_view questKey) const {
        auto* quest = ResolveFormFromKeyString<RE::TESQuest>(questKey, "training quest");
        return quest && quest->IsCompleted();
    }

    std::int32_t EngineParentAffiliationPort::GetQuestStage(std::string_view questKey) const {
        auto* quest = ResolveFormFromKeyString<RE::TESQuest>(questKey, "training questStage");
        return quest ? static_cast<std::int32_t>(quest->GetCurrentStageID()) : 0;
    }

    std::string EngineParentAffiliationPort::GetParentDisplayName(MARAS::Foundation::ActorId parentId) const {
        const RE::FormID raw = parentId.raw();
        if (parentId.IsPlayer() || IsPlayerFormID(raw)) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                if (const char* name = player->GetName(); name && *name != '\0') {
                    return name;
                }
            }
            return "Player";
        }
        if (auto* actor = ResolveActorPtr(raw)) {
            if (const char* display = actor->GetDisplayFullName(); display && *display != '\0') {
                return display;
            }
            if (const char* name = actor->GetName(); name && *name != '\0') {
                return name;
            }
        }
        if (auto* base = ResolveActorBaseNPC(raw)) {
            if (const char* full = base->GetFullName(); full && *full != '\0') {
                return full;
            }
            if (const char* name = base->GetName(); name && *name != '\0') {
                return name;
            }
        }
        return "Parent";
    }

    TrainingCatalogLoader& TrainingCatalogLoader::GetSingleton() {
        static TrainingCatalogLoader instance;
        return instance;
    }

    bool TrainingCatalogLoader::Initialize() {
        std::error_code ec;
        const std::filesystem::path configPath =
            std::filesystem::exists(kDefaultConfigPath, ec) ? kDefaultConfigPath : kFallbackConfigPath;
        const std::filesystem::path extFolder =
            std::filesystem::exists(kDefaultExtFolder, ec) ? kDefaultExtFolder : kFallbackExtFolder;
        return LoadCatalog(configPath, extFolder);
    }

    bool TrainingCatalogLoader::LoadCatalog(const std::filesystem::path& configPath,
                                            const std::filesystem::path& extensionFolder) {
        std::vector<MARAS::Domain::Lineage::TrainingClassSpec> classes;
        std::vector<MARAS::Domain::Lineage::DaedricBlessingSpec> blessings;
        bool loadedAny = false;
        std::error_code ec;

        if (!configPath.empty() && std::filesystem::exists(configPath, ec) &&
            std::filesystem::is_regular_file(configPath, ec)) {
            if (LoadYamlConfigFile(configPath, classes, blessings)) {
                MARAS_LOG_INFO("[TrainingCatalogLoader] Loaded base training config '{}' ({} paths, {} Daedric blessings)",
                               configPath.string(), classes.size(), blessings.size());
                loadedAny = true;
            }
        } else if (!configPath.empty()) {
            MARAS_LOG_WARN("[TrainingCatalogLoader] Base training config not found at '{}'", configPath.string());
        }

        if (!extensionFolder.empty() && std::filesystem::exists(extensionFolder, ec) &&
            std::filesystem::is_directory(extensionFolder, ec)) {
            std::vector<std::filesystem::path> extFiles;
            for (const auto& entry : std::filesystem::directory_iterator(extensionFolder, ec)) {
                if (ec) {
                    break;
                }
                if (entry.is_regular_file(ec) &&
                    MARAS::Foundation::StringUtils::CaseInsensitiveEquals(entry.path().extension().string(), ".yaml")) {
                    extFiles.push_back(entry.path());
                }
            }
            std::sort(extFiles.begin(), extFiles.end());

            std::size_t extLoaded = 0;
            for (const auto& file : extFiles) {
                if (LoadYamlConfigFile(file, classes, blessings)) {
                    ++extLoaded;
                    loadedAny = true;
                }
            }
            if (extLoaded > 0) {
                MARAS_LOG_INFO("[TrainingCatalogLoader] Applied {} extension file(s) from '{}' ({} paths, {} blessings)",
                               extLoaded, extensionFolder.string(), classes.size(), blessings.size());
            }
        }

        for (std::size_t i = 0; i < classes.size(); ++i) {
            classes[i].index = static_cast<std::int32_t>(i);
        }

        catalog_.SetData(std::move(classes), std::move(blessings));
        loaded_.store(loadedAny, std::memory_order_release);
        return loadedAny;
    }

    void TrainingCatalogLoader::Clear() noexcept {
        catalog_.Clear();
        loaded_.store(false, std::memory_order_release);
    }

}  // namespace MARAS::Engine
