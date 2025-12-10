#include "core/MarriageDifficulty.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "core/AffectionService.h"
#include "core/FormCache.h"
#include "core/NPCRelationshipManager.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"

namespace MARAS {

    // Static member initialization
    std::unordered_map<std::string, float> MarriageDifficulty::config_;
    bool MarriageDifficulty::configLoaded_ = false;

    namespace {
        // Helper: Get FormCache singleton
        FormCache& GetCache() { return FormCache::GetSingleton(); }

        // Helper: Get NPCRelationshipManager singleton
        NPCRelationshipManager& GetManager() { return NPCRelationshipManager::GetSingleton(); }

        // Helper: Clamp value to range
        template <typename T>
        T ClampValue(T value, T min, T max) {
            return std::clamp(value, min, max);
        }

        // Helper: Check if quest has reached stage
        bool QuestReachedStage(RE::TESQuest* quest, std::uint32_t stage) {
            return quest && quest->GetCurrentStageID() >= stage;
        }

        // Helper: Convert difficulty to success chance using sigmoid curve for smoother transitions
        // Options:
        // - Linear: simple inversion (current)
        // - Sigmoid: smooth S-curve with gentle extremes
        // - Quadratic: easing function for moderate smoothing
        float DifficultyToChance(float difficulty, float steepness, float center) {
            // Sigmoid curve: 1 / (1 + e^(k*(x-50)))
            // k controls steepness (0.08-0.12 works well)
            // Centered at difficulty=50 (50% chance)
            float exponent = steepness * (difficulty - center);
            return 1.0f / (1.0f + std::exp(exponent));
        }

        // Helper: Track best positive and negative guild modifiers
        struct GuildModifiers {
            float bestPositive = 0.0f;
            float bestNegative = 0.0f;
            float sameGuild = 0.0f;

            void UpdateWith(float modifier) {
                if (modifier > 0.0f) {
                    bestPositive = std::max(bestPositive, modifier);
                } else if (modifier < 0.0f) {
                    bestNegative = std::min(bestNegative, modifier);
                }
            }

            float GetStrongest() const { return (-bestNegative > bestPositive) ? bestNegative : bestPositive; }
        };

    }  // namespace

    float MarriageDifficulty::CalculateMarriageSuccessChance(RE::Actor* npc, float intimacyAdjustment, float mostGold,
                                                             float housesOwned, float horsesOwned,
                                                             float questsCompleted, float dungeonsCleared,
                                                             float dragonSoulsCollected, bool playerKiller) {
        auto startTime = std::chrono::high_resolution_clock::now();

        if (!npc) {
            MARAS_LOG_ERROR("NPC is null in marriage difficulty calculation");
            return 0.0f;
        }

        MARAS_LOG_DEBUG("Calculating marriage success chance for NPC: {}", npc->GetDisplayFullName());

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            MARAS_LOG_ERROR("Player is null");
            return 0.0f;
        }

        int npcLevel = npc->GetLevel();
        int playerLevel = player->GetLevel();
        float levelDiff = static_cast<float>(npcLevel - playerLevel);

        // Get social class directly as enum and cast to int for index
        auto& manager = NPCRelationshipManager::GetSingleton();
        int socialClassIndex = static_cast<int>(manager.GetSocialClass(npc->GetFormID()));

        // === 1. Complexity sum ===
        float complexity = GetParam("initialComplexity");

        // Prestige delta
        float target =
            GetParam("prestigeTargetBase") + GetParam("prestigeTargetPerSocialIndexMultiplier") * socialClassIndex;
        float playerPrestige = CalculatePlayerPrestige(mostGold, housesOwned, horsesOwned, questsCompleted,
                                                       dungeonsCleared, dragonSoulsCollected);
        float pDelta = (target - playerPrestige) * GetParam("prestigeDeltaMultiplier");
        complexity += pDelta;

        MARAS_LOG_DEBUG("Target prestige: {}, Player prestige: {}, Delta: {}", target, playerPrestige, pDelta);

        // Jilted penalty
        if (IsJilted(npc)) {
            complexity += GetParam("jiltedPenalty");
            MARAS_LOG_DEBUG("Applied jilted penalty");
        }

        // Divorced penalty
        if (IsDivorced(npc)) {
            complexity += GetParam("divorcedPenalty");
            MARAS_LOG_DEBUG("Applied divorced penalty");
        }

        // Player killer penalty
        if (playerKiller) {
            complexity += GetParam("playerKillerPenalty");
            MARAS_LOG_DEBUG("Applied player killer penalty");
        }

        // Spouse count penalty
        float marriedScore = CountMarried() * GetParam("marriedCountMultiplier");
        MARAS_LOG_DEBUG("Married count: {}, score: {}", CountMarried(), marriedScore);
        complexity += marriedScore;

        // Divorced count penalty
        float divorcedScore = CountDivorced() * GetParam("divorcedCountMultiplier");
        MARAS_LOG_DEBUG("Divorced count: {}, score: {}", CountDivorced(), divorcedScore);
        complexity += divorcedScore;

        // Level difference
        float levelDiffScore = std::clamp(levelDiff * GetParam("levelDiffMultiplier"), GetParam("levelDiffClampMin"),
                                          GetParam("levelDiffClampMax"));
        MARAS_LOG_DEBUG("Level difference: {}, score: {}", levelDiff, levelDiffScore);
        complexity += levelDiffScore;

        // Speech bonus
        float speechcraft = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kSpeech);
        float speechScore = GetParam("speechcraftMultiplier") * speechcraft;
        MARAS_LOG_DEBUG("Speechcraft value: {}, score: {}", speechcraft, speechScore);
        complexity += speechScore;

        // Relationship rank bonus - get actual relationship rank between NPC and player
        int relationshipRank = 0;
        if (auto npcBase = npc->GetActorBase()) {
            if (auto playerBase = player->GetActorBase()) {
                if (auto relationship = RE::BGSRelationship::GetRelationship(npcBase, playerBase)) {
                    relationshipRank = static_cast<int>(relationship->level.underlying());
                }
            }
        }

        float relationshipScore = GetParam("relationshipRankMultiplier") * static_cast<float>(relationshipRank);
        complexity += relationshipScore;

        MARAS_LOG_DEBUG("Relationship rank: {}, score: {}", relationshipRank, relationshipScore);

        // Guild alignment
        float guildAlignment = CalculateGuildAlignmentMod(npc);
        complexity += guildAlignment;
        MARAS_LOG_DEBUG("Guild alignment score: {}", guildAlignment);

        // Intimacy adjustment (passed from Papyrus)
        complexity += intimacyAdjustment;
        MARAS_LOG_DEBUG("Applied intimacy adjustment: {}", intimacyAdjustment);

        // Affection adjustment (permanent affection 0..100, baseline 50)
        // Values above 50 reduce complexity (improve chance), below 50 increase complexity (worsen chance)
        int permanentAffection = AffectionService::GetSingleton().GetPermanentAffection(npc->GetFormID());
        float affectionMultiplier = GetParam("affectionMultiplier");
        float affectionAdjustment = (static_cast<float>(permanentAffection) - 50.0f) * affectionMultiplier;
        complexity -= affectionAdjustment;
        MARAS_LOG_DEBUG("Permanent affection: {}, multiplier: {}, adjustment applied: {}", permanentAffection,
                        affectionMultiplier, affectionAdjustment);

        MARAS_LOG_DEBUG("Final complexity: {}", complexity);

        // === 2. Clamp difficulty 0-100 ===
        float difficulty = ClampValue(complexity, GetParam("difficultyClampMin"), GetParam("difficultyClampMax"));

        // === 3. Calculate success chance using sigmoid curve for smoother transitions ===
        float chance = DifficultyToChance(difficulty, GetParam("sigmoidSteepness"), GetParam("sigmoidCenter"));

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        MARAS_LOG_INFO(
            "Marriage difficulty calculation completed for {} in {} microseconds (chance: {:.3f}; difficulty: {:.2f})",
            npc->GetDisplayFullName(), duration.count(), chance, difficulty);

        return chance;
    }

    bool MarriageDifficulty::CheckQuestStage(RE::TESQuest* quest, std::uint32_t stage) {
        return QuestReachedStage(quest, stage);
    }

    bool MarriageDifficulty::LoadConfig() {
        std::filesystem::path path = "Data/SKSE/Plugins/MARAS/marriageDifficultyConfig.json";

        try {
            if (!std::filesystem::exists(path)) {
                MARAS_LOG_ERROR("MarriageDifficulty: config file not found at {}", path.string());
                return false;
            }

            std::ifstream in(path);
            if (!in.is_open()) {
                MARAS_LOG_ERROR("MarriageDifficulty: failed to open {}", path.string());
                return false;
            }

            nlohmann::json j;
            in >> j;

            config_.clear();

            // Load difficulty calculation parameters
            if (j.contains("difficultyCalculation")) {
                auto& dc = j["difficultyCalculation"];
                if (dc.contains("sigmoidSteepness")) config_["sigmoidSteepness"] = dc["sigmoidSteepness"].get<float>();
                if (dc.contains("sigmoidCenter")) config_["sigmoidCenter"] = dc["sigmoidCenter"].get<float>();
                if (dc.contains("difficultyClampMin"))
                    config_["difficultyClampMin"] = dc["difficultyClampMin"].get<float>();
                if (dc.contains("difficultyClampMax"))
                    config_["difficultyClampMax"] = dc["difficultyClampMax"].get<float>();
            }

            // Load complexity parameters
            if (j.contains("complexity")) {
                auto& c = j["complexity"];
                if (c.contains("initialComplexity")) config_["initialComplexity"] = c["initialComplexity"].get<float>();
                if (c.contains("levelDiffClampMin")) config_["levelDiffClampMin"] = c["levelDiffClampMin"].get<float>();
                if (c.contains("levelDiffClampMax")) config_["levelDiffClampMax"] = c["levelDiffClampMax"].get<float>();
            }

            // Load prestige parameters with renamed keys to match old naming
            if (j.contains("prestige")) {
                auto& p = j["prestige"];
                if (p.contains("dragonbornBonus"))
                    config_["prestigeDragonbornBonus"] = p["dragonbornBonus"].get<float>();
                if (p.contains("thaneHoldValue")) config_["prestigeThaneHoldValue"] = p["thaneHoldValue"].get<float>();
                if (p.contains("maxThaneHolds")) config_["prestigeMaxThaneHolds"] = p["maxThaneHolds"].get<float>();
                if (p.contains("guildLeaderBonus"))
                    config_["prestigeGuildLeaderBonus"] = p["guildLeaderBonus"].get<float>();
                if (p.contains("mostGoldDivisor"))
                    config_["prestigeMostGoldDivisor"] = p["mostGoldDivisor"].get<float>();
                if (p.contains("mostGoldClampMax"))
                    config_["prestigeMostGoldClampMax"] = p["mostGoldClampMax"].get<float>();
                if (p.contains("houseUnitMultiplier"))
                    config_["prestigeHouseUnitMultiplier"] = p["houseUnitMultiplier"].get<float>();
                if (p.contains("horseUnitMultiplier"))
                    config_["prestigeHorseUnitMultiplier"] = p["horseUnitMultiplier"].get<float>();
                if (p.contains("houseHorseClampMax"))
                    config_["prestigeHouseHorseClampMax"] = p["houseHorseClampMax"].get<float>();
                if (p.contains("questsMultiplier"))
                    config_["prestigeQuestsMultiplier"] = p["questsMultiplier"].get<float>();
                if (p.contains("dungeonsMultiplier"))
                    config_["prestigeDungeonsMultiplier"] = p["dungeonsMultiplier"].get<float>();
                if (p.contains("soulsMultiplier"))
                    config_["prestigeSoulsMultiplier"] = p["soulsMultiplier"].get<float>();
                if (p.contains("renownClampMax")) config_["prestigeRenownClampMax"] = p["renownClampMax"].get<float>();
                if (p.contains("prestigeClampMin"))
                    config_["prestigePrestigeClamp_min"] = p["prestigeClampMin"].get<float>();
                if (p.contains("prestigeClampMax"))
                    config_["prestigePrestigeClamp_max"] = p["prestigeClampMax"].get<float>();
                if (p.contains("targetBase")) config_["prestigeTargetBase"] = p["targetBase"].get<float>();
                if (p.contains("targetPerSocialIndexMultiplier"))
                    config_["prestigeTargetPerSocialIndexMultiplier"] =
                        p["targetPerSocialIndexMultiplier"].get<float>();
                if (p.contains("deltaMultiplier"))
                    config_["prestigeDeltaMultiplier"] = p["deltaMultiplier"].get<float>();
            }

            // Load penalties
            if (j.contains("penalties")) {
                auto& pen = j["penalties"];
                if (pen.contains("jiltedPenalty")) config_["jiltedPenalty"] = pen["jiltedPenalty"].get<float>();
                if (pen.contains("divorcedPenalty")) config_["divorcedPenalty"] = pen["divorcedPenalty"].get<float>();
                if (pen.contains("playerKillerPenalty"))
                    config_["playerKillerPenalty"] = pen["playerKillerPenalty"].get<float>();
            }

            // Load multipliers
            if (j.contains("multipliers")) {
                auto& m = j["multipliers"];
                if (m.contains("marriedCountMultiplier"))
                    config_["marriedCountMultiplier"] = m["marriedCountMultiplier"].get<float>();
                if (m.contains("divorcedCountMultiplier"))
                    config_["divorcedCountMultiplier"] = m["divorcedCountMultiplier"].get<float>();
                if (m.contains("levelDiffMultiplier"))
                    config_["levelDiffMultiplier"] = m["levelDiffMultiplier"].get<float>();
                if (m.contains("speechcraftMultiplier"))
                    config_["speechcraftMultiplier"] = m["speechcraftMultiplier"].get<float>();
                if (m.contains("relationshipRankMultiplier"))
                    config_["relationshipRankMultiplier"] = m["relationshipRankMultiplier"].get<float>();
                if (m.contains("affectionMultiplier"))
                    config_["affectionMultiplier"] = m["affectionMultiplier"].get<float>();
            }

            // Load guild parameters
            if (j.contains("guilds")) {
                auto& guilds = j["guilds"];
                if (guilds.contains("sameGuildBonus"))
                    config_["guild_sameGuildBonus"] = guilds["sameGuildBonus"].get<float>();

                // Load each guild's social class modifiers
                const std::vector<std::string> guildNames = {"companions", "thieves", "brotherhood", "college",
                                                             "bards"};
                const std::vector<std::string> socialClasses = {"outcast", "poverty",   "working", "middle",
                                                                "wealthy", "religious", "nobles",  "rulers"};

                for (const auto& guildName : guildNames) {
                    if (guilds.contains(guildName)) {
                        auto& guild = guilds[guildName];
                        for (const auto& socialClass : socialClasses) {
                            if (guild.contains(socialClass)) {
                                std::string key = "guild" + std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(guildName[0])))) +
                                                  guildName.substr(1) + "_" + socialClass;
                                config_[key] = guild[socialClass].get<float>();
                            }
                        }
                    }
                }
            }

            configLoaded_ = true;
            MARAS_LOG_INFO("MarriageDifficulty: loaded {} configuration parameters from {}", config_.size(),
                           path.string());
            return true;

        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("MarriageDifficulty::LoadConfig exception: {}", e.what());
            return false;
        }
    }

    float MarriageDifficulty::GetParam(const std::string& param) {
        auto it = config_.find(param);
        if (it != config_.end()) {
            return it->second;
        }

        MARAS_LOG_WARN("Parameter '{}' not found in config, returning 0.0f", param);
        return 0.0f;
    }

    int MarriageDifficulty::GetThaneHolds() {
        constexpr std::uint32_t THANE_STAGE = 25;
        auto& cache = GetCache();

        // Array of thane quest getters
        RE::TESQuest* thaneQuests[] = {
            cache.GetEastmarchThane(),  cache.GetFalkreathThane(), cache.GetHaafingarThane(),
            cache.GetHjaalmarchThane(), cache.GetPaleThane(),      cache.GetReachThane(),
            cache.GetRiftThane(),       cache.GetWhiterunThane(),  cache.GetWinterholdThane()};

        int totalHolds = 0;
        for (auto* quest : thaneQuests) {
            if (QuestReachedStage(quest, THANE_STAGE)) {
                ++totalHolds;
            }
        }

        return totalHolds;
    }

    bool MarriageDifficulty::IsGuildLeader() {
        auto& cache = GetCache();
        return QuestReachedStage(cache.GetCompanionsQuest(), 200) || QuestReachedStage(cache.GetCollegeQuest(), 200) ||
               QuestReachedStage(cache.GetThievesQuest(), 40);
    }

    float MarriageDifficulty::CalculatePlayerPrestige(float mostGold, float housesOwned, float horsesOwned,
                                                      float questsCompleted, float dungeonsCleared,
                                                      float dragonSoulsCollected) {
        float result = 0.0f;

        MARAS_LOG_DEBUG("Prestige inputs - gold: {}, houses: {}, horses: {}, quests: {}, dungeons: {}, souls: {}",
                        mostGold, housesOwned, horsesOwned, questsCompleted, dungeonsCleared, dragonSoulsCollected);

        // Dragonborn status
        float dragonbornBonus = 0.0f;
        if (QuestReachedStage(GetCache().GetDragonbornQuest(), 90)) {
            dragonbornBonus = GetParam("prestigeDragonbornBonus");
            result += dragonbornBonus;
        }
        MARAS_LOG_DEBUG("Dragonborn bonus: {}", dragonbornBonus);

        // Thane holds
        int thaneHolds = GetThaneHolds();
        float thaneScore = thaneHolds * GetParam("prestigeThaneHoldValue");
        result += thaneScore;
        MARAS_LOG_DEBUG("Thane holds: {}, score: {}", thaneHolds, thaneScore);

        // Guild leader
        float guildLeaderBonus = 0.0f;
        if (IsGuildLeader()) {
            guildLeaderBonus = GetParam("prestigeGuildLeaderBonus");
            result += guildLeaderBonus;
        }
        MARAS_LOG_DEBUG("Guild leader bonus: {}", guildLeaderBonus);

        // Wealth
        float wealthScore =
            ClampValue(mostGold / GetParam("prestigeMostGoldDivisor"), 0.0f, GetParam("prestigeMostGoldClampMax"));
        result += wealthScore;
        MARAS_LOG_DEBUG("Wealth score: {} (from {} gold)", wealthScore, mostGold);

        // Houses and horses
        float houseHorseScore = housesOwned * GetParam("prestigeHouseUnitMultiplier") +
                                horsesOwned * GetParam("prestigeHorseUnitMultiplier");
        float houseHorseClamped = ClampValue(houseHorseScore, 0.0f, GetParam("prestigeHouseHorseClampMax"));
        result += houseHorseClamped;
        MARAS_LOG_DEBUG("House/horse score: {} (clamped from {})", houseHorseClamped, houseHorseScore);

        // Heroic achievements
        float heroicScore = questsCompleted * GetParam("prestigeQuestsMultiplier") +
                            dungeonsCleared * GetParam("prestigeDungeonsMultiplier") +
                            dragonSoulsCollected * GetParam("prestigeSoulsMultiplier");
        float heroicClamped = ClampValue(heroicScore, 0.0f, GetParam("prestigeRenownClampMax"));
        result += heroicClamped;
        MARAS_LOG_DEBUG("Heroic achievements score: {} (clamped from {}) - quests: {}, dungeons: {}, souls: {}",
                        heroicClamped, heroicScore, questsCompleted * GetParam("prestigeQuestsMultiplier"),
                        dungeonsCleared * GetParam("prestigeDungeonsMultiplier"),
                        dragonSoulsCollected * GetParam("prestigeSoulsMultiplier"));

        float finalPrestige =
            ClampValue(result, GetParam("prestigePrestigeClamp_min"), GetParam("prestigePrestigeClamp_max"));
        MARAS_LOG_DEBUG("Total prestige: {} (clamped from {})", finalPrestige, result);

        return finalPrestige;
    }

    float MarriageDifficulty::CalculateGuildAlignmentMod(RE::Actor* npc) {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !npc) return 0.0f;

        auto socialClass = GetManager().GetSocialClass(npc->GetFormID());
        std::string spouseClass = Utils::ToLower(Utils::SocialClassToString(socialClass));

        GuildModifiers modifiers;
        auto& cache = GetCache();

        // Helper lambda to process guild faction
        auto processGuild = [&](RE::TESFaction* faction, const std::string& guildName, bool checkQuest = false,
                                RE::TESQuest* quest = nullptr, std::uint32_t stage = 0) {
            // Skip if quest check required and quest hasn't reached stage
            if (checkQuest && !QuestReachedStage(quest, stage)) return;

            // Check if player is in guild (or quest-based membership)
            bool playerInGuild = checkQuest || (faction && player->IsInFaction(faction));
            if (!playerInGuild) return;

            // Get and apply guild modifier for this social class
            float modifier = GetParam(guildName + "_" + spouseClass);
            modifiers.UpdateWith(modifier);

            // Check if NPC is also in same guild
            if (faction && npc->IsInFaction(faction)) {
                modifiers.sameGuild += GetParam("guild_sameGuildBonus");
            }
        };

        // Process each guild
        processGuild(cache.GetCompanionsFaction(), "guildCompanions");
        processGuild(cache.GetThievesFaction(), "guildThieves");
        processGuild(cache.GetBrotherhoodFaction(), "guildBrotherhood");
        processGuild(cache.GetCollegeFaction(), "guildCollege");
        processGuild(cache.GetBardsFaction(), "guildBards", true, cache.GetBardsQuest(), 300);

        return modifiers.GetStrongest() + modifiers.sameGuild;
    }

    bool MarriageDifficulty::IsJilted(RE::Actor* npc) { return npc && GetManager().IsJilted(npc->GetFormID()); }

    bool MarriageDifficulty::IsDivorced(RE::Actor* npc) { return npc && GetManager().IsDivorced(npc->GetFormID()); }

    int MarriageDifficulty::CountMarried() { return static_cast<int>(GetManager().GetMarriedCount()); }

    int MarriageDifficulty::CountDivorced() { return static_cast<int>(GetManager().GetDivorcedCount()); }

}  // namespace MARAS
