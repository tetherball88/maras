#include "maras/engine/MarriageDifficultyLoader.h"

#include "maras/engine/EngineLogging.h"
#include "maras/engine/JsonOverrideLoader.h"

#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace MARAS::Engine {

    namespace {

        constexpr std::array<const char*, 4> kDefaultConfigPaths = {
            "Data/SKSE/Plugins/MARAS/marriage_difficulty.json",
            "Data/SKSE/Plugins/MARAS/marriageDifficultyConfig.json",
            "SKSE/Plugins/MARAS/marriage_difficulty.json",
            "SKSE/Plugins/MARAS/marriageDifficultyConfig.json"
        };

        struct JsonParamMapping {
            const char* section;
            const char* jsonKey;
            const char* paramKey;
        };

        // Ordered mapping from JSON sections/keys onto MarriageDifficultyConfig::params keys.
        // Repeated target keys (e.g. datingBonus/loverBonus) represent precedence where later sections override earlier fallbacks.
        constexpr JsonParamMapping kScalarMappings[] = {
            { "difficultyCalculation", "sigmoidSteepness", "sigmoidSteepness" },
            { "difficultyCalculation", "sigmoidCenter", "sigmoidCenter" },
            { "difficultyCalculation", "difficultyClampMin", "difficultyClampMin" },
            { "difficultyCalculation", "difficultyClampMax", "difficultyClampMax" },

            { "complexity", "initialComplexity", "initialComplexity" },
            { "complexity", "levelDiffClampMin", "levelDiffClampMin" },
            { "complexity", "levelDiffClampMax", "levelDiffClampMax" },

            { "prestige", "dragonbornBonus", "prestigeDragonbornBonus" },
            { "prestige", "thaneHoldValue", "prestigeThaneHoldValue" },
            { "prestige", "maxThaneHolds", "prestigeMaxThaneHolds" },
            { "prestige", "guildLeaderBonus", "prestigeGuildLeaderBonus" },
            { "prestige", "mostGoldDivisor", "prestigeMostGoldDivisor" },
            { "prestige", "mostGoldClampMax", "prestigeMostGoldClampMax" },
            { "prestige", "houseUnitMultiplier", "prestigeHouseUnitMultiplier" },
            { "prestige", "horseUnitMultiplier", "prestigeHorseUnitMultiplier" },
            { "prestige", "houseHorseClampMax", "prestigeHouseHorseClampMax" },
            { "prestige", "questsMultiplier", "prestigeQuestsMultiplier" },
            { "prestige", "dungeonsMultiplier", "prestigeDungeonsMultiplier" },
            { "prestige", "soulsMultiplier", "prestigeSoulsMultiplier" },
            { "prestige", "renownClampMax", "prestigeRenownClampMax" },
            { "prestige", "prestigeClampMin", "prestigePrestigeClamp_min" },
            { "prestige", "prestigeClampMax", "prestigePrestigeClamp_max" },
            { "prestige", "targetBase", "prestigeTargetBase" },
            { "prestige", "targetPerSocialIndexMultiplier", "prestigeTargetPerSocialIndexMultiplier" },
            { "prestige", "deltaMultiplier", "prestigeDeltaMultiplier" },

            { "penalties", "jiltedPenalty", "jiltedPenalty" },
            { "penalties", "divorcedPenalty", "divorcedPenalty" },
            { "penalties", "playerKillerPenalty", "playerKillerPenalty" },
            { "penalties", "datingBonus", "datingBonus" },
            { "penalties", "loverBonus", "loverBonus" },

            { "multipliers", "marriedCountMultiplier", "marriedCountMultiplier" },
            { "multipliers", "divorcedCountMultiplier", "divorcedCountMultiplier" },
            { "multipliers", "levelDiffMultiplier", "levelDiffMultiplier" },
            { "multipliers", "speechcraftMultiplier", "speechcraftMultiplier" },
            { "multipliers", "relationshipRankMultiplier", "relationshipRankMultiplier" },
            { "multipliers", "affectionMultiplier", "affectionMultiplier" },
            { "multipliers", "datingBonus", "datingBonus" },
            { "multipliers", "loverBonus", "loverBonus" },

            { "bonuses", "datingBonus", "datingBonus" },
            { "bonuses", "loverBonus", "loverBonus" },

            { "guilds", "sameGuildBonus", "guild_sameGuildBonus" }
        };

        struct GuildJsonInfo {
            const char* jsonKey;
            const char* paramPrefix;
        };

        constexpr GuildJsonInfo kGuilds[] = {
            { "companions", "guildCompanions" },
            { "thieves", "guildThieves" },
            { "brotherhood", "guildBrotherhood" },
            { "college", "guildCollege" },
            { "bards", "guildBards" }
        };

        constexpr std::array<const char*, 8> kSocialClassKeys = {
            "outcast", "poverty", "working", "middle", "wealthy", "religious", "nobles", "rulers"
        };

        void ApplyNumericField(const nlohmann::json& obj,
                               const char* jsonKey,
                               const std::string& paramKey,
                               MARAS::Domain::Relationships::MarriageDifficultyConfig& cfg) {
            if (obj.contains(jsonKey) && obj[jsonKey].is_number()) {
                cfg.params[paramKey] = obj[jsonKey].get<float>();
            }
        }

        [[nodiscard]] std::filesystem::path ResolveConfigPath(const std::filesystem::path& customPath) {
            std::error_code ec;
            if (!customPath.empty()) {
                return customPath;
            }
            for (const char* candidate : kDefaultConfigPaths) {
                const std::filesystem::path candidatePath(candidate);
                if (std::filesystem::exists(candidatePath, ec)) {
                    return candidatePath;
                }
            }
            return {};
        }

    }  // namespace

    MarriageDifficultyLoader& MarriageDifficultyLoader::GetSingleton() {
        static MarriageDifficultyLoader instance;
        return instance;
    }

    bool MarriageDifficultyLoader::LoadConfig(const std::filesystem::path& customPath) {
        auto parsedConfig = MARAS::Domain::Relationships::MarriageDifficultyConfig::CreateDefault();
        const std::filesystem::path path = ResolveConfigPath(customPath);
        if (path.empty()) {
            MARAS_LOG_WARN("[MarriageDifficultyLoader] Config file not found; using built-in defaults");
            config_ = std::move(parsedConfig);
            loaded_.store(false, std::memory_order_release);
            return false;
        }

        nlohmann::json root;
        if (!JsonOverrideLoader::LoadJsonFile(path, root, "MarriageDifficultyLoader") || !root.is_object()) {
            MARAS_LOG_ERROR("[MarriageDifficultyLoader] Failed to parse '{}'; using built-in defaults",
                            path.string());
            config_ = std::move(parsedConfig);
            loaded_.store(false, std::memory_order_release);
            return false;
        }

        for (const auto& mapping : kScalarMappings) {
            if (root.contains(mapping.section) && root[mapping.section].is_object()) {
                ApplyNumericField(root[mapping.section], mapping.jsonKey, mapping.paramKey, parsedConfig);
            }
        }

        if (root.contains("guilds") && root["guilds"].is_object()) {
            const auto& guildsObj = root["guilds"];
            for (const auto& guild : kGuilds) {
                if (!guildsObj.contains(guild.jsonKey) || !guildsObj[guild.jsonKey].is_object()) {
                    continue;
                }
                const auto& guildMap = guildsObj[guild.jsonKey];
                for (const char* classKey : kSocialClassKeys) {
                    const std::string paramKey = std::string(guild.paramPrefix) + "_" + classKey;
                    ApplyNumericField(guildMap, classKey, paramKey, parsedConfig);
                }
            }
        }

        config_ = std::move(parsedConfig);
        loaded_.store(true, std::memory_order_release);
        MARAS_LOG_INFO("[MarriageDifficultyLoader] Loaded marriage difficulty configuration from '{}'", path.string());
        return true;
    }

}  // namespace MARAS::Engine
