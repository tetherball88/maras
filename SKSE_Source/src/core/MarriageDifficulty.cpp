#include "core/MarriageDifficulty.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>

#include "core/AffectionService.h"
#include "core/FormCache.h"
#include "core/NPCRelationshipManager.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"

namespace MARAS {

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
        float DifficultyToChance(float difficulty) {
            // Sigmoid curve: 1 / (1 + e^(k*(x-50)))
            // k controls steepness (0.08-0.12 works well)
            // Centered at difficulty=50 (50% chance)
            constexpr float steepness = 0.10f;
            constexpr float center = 50.0f;
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
        float levelDiffScore = std::clamp(levelDiff * GetParam("levelDiffMultiplier"), -10.0f, 10.0f);
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
        float difficulty = ClampValue(complexity, 0.0f, 100.0f);

        // === 3. Calculate success chance using sigmoid curve for smoother transitions ===
        float chance = DifficultyToChance(difficulty);

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

    float MarriageDifficulty::GetParam(const std::string& param) {
        // Use hardcoded values from the actual JSON file since JSON parsing is complex
        // These values are directly from marriageComplexityVariables.json
        static const std::unordered_map<std::string, float> configParams = {
            {"prestigeDragonbornBonus", 25.0f},
            {"prestigeThaneHoldValue", 4.0f},
            {"prestigeMaxThaneHolds", 9.0f},
            {"prestigeGuildLeaderBonus", 8.0f},
            {"prestigeMostGoldDivisor", 5000.0f},
            {"prestigeMostGoldClampMax", 15.0f},
            {"prestigeHouseUnitMultiplier", 3.0f},
            {"prestigeHorseUnitMultiplier", 3.0f},
            {"prestigeHouseHorseClampMax", 15.0f},
            {"prestigeQuestsMultiplier", 0.2f},
            {"prestigeDungeonsMultiplier", 0.1f},
            {"prestigeSoulsMultiplier", 0.5f},
            {"prestigeRenownClampMax", 25.0f},
            {"prestigePrestigeClamp_min", 0.0f},
            {"prestigePrestigeClamp_max", 100.0f},
            {"guild_sameGuildBonus", -4.0f},
            {"guildCompanions_outcast", 0.0f},
            {"guildCompanions_poverty", 0.0f},
            {"guildCompanions_working", -2.0f},
            {"guildCompanions_middle", -1.0f},
            {"guildCompanions_wealthy", 0.0f},
            {"guildCompanions_religious", 1.0f},
            {"guildCompanions_nobles", -1.0f},
            {"guildCompanions_rulers", -2.0f},
            {"guildThieves_outcast", -2.0f},
            {"guildThieves_poverty", -1.0f},
            {"guildThieves_middle", 1.0f},
            {"guildThieves_working", 0.0f},
            {"guildThieves_wealthy", 2.0f},
            {"guildThieves_religious", 2.0f},
            {"guildThieves_nobles", 2.0f},
            {"guildThieves_rulers", 3.0f},
            {"guildBrotherhood_outcast", -3.0f},
            {"guildBrotherhood_poverty", -2.0f},
            {"guildBrotherhood_working", 1.0f},
            {"guildBrotherhood_middle", 2.0f},
            {"guildBrotherhood_wealthy", 3.0f},
            {"guildBrotherhood_religious", 4.0f},
            {"guildBrotherhood_nobles", 3.0f},
            {"guildBrotherhood_rulers", 4.0f},
            {"guildCollege_outcast", 1.0f},
            {"guildCollege_poverty", 0.0f},
            {"guildCollege_working", 1.0f},
            {"guildCollege_middle", 0.0f},
            {"guildCollege_wealthy", 1.0f},
            {"guildCollege_religious", -2.0f},
            {"guildCollege_nobles", 0.0f},
            {"guildCollege_rulers", -3.0f},
            {"guildBards_outcast", 1.0f},
            {"guildBards_poverty", 0.0f},
            {"guildBards_working", 0.0f},
            {"guildBards_middle", -1.0f},
            {"guildBards_wealthy", -1.0f},
            {"guildBards_religious", 0.0f},
            {"guildBards_nobles", -2.0f},
            {"guildBards_rulers", -1.0f},
            {"intimacySpouseLoverPenalty", 30.0f},
            {"intimacySpouseNotLoverPenalty", 25.0f},
            {"intimacyCourtingLoverPenalty", 20.0f},
            {"intimacyCourtingNotLoverPenalty", 15.0f},
            {"intimacyOtherLoversPenalty", 10.0f},
            {"intimacyPlayerMultiplier", 1.0f},
            {"initialComplexity", 50.0f},
            {"prestigeTargetBase", 10.0f},
            {"prestigeTargetPerSocialIndexMultiplier", 10.0f},
            {"prestigeDeltaMultiplier", 0.30f},
            {"jiltedPenalty", 10.0f},
            {"divorcedPenalty", 20.0f},
            {"playerKillerPenalty", 50.0f},
            {"marriedCountMultiplier", 2.0f},
            {"divorcedCountMultiplier", 1.0f},
            {"levelDiffMultiplier", 0.50f},
            {"speechcraftMultiplier", -0.1f},
            {"relationshipRankMultiplier", -3.0f},
            {"affectionMultiplier", 0.50f}};

        auto it = configParams.find(param);
        if (it != configParams.end()) {
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

        // Dragonborn status
        if (QuestReachedStage(GetCache().GetDragonbornQuest(), 90)) {
            result += GetParam("prestigeDragonbornBonus");
        }

        // Thane holds
        result += GetThaneHolds() * GetParam("prestigeThaneHoldValue");

        // Guild leader
        if (IsGuildLeader()) {
            result += GetParam("prestigeGuildLeaderBonus");
        }

        // Wealth
        result +=
            ClampValue(mostGold / GetParam("prestigeMostGoldDivisor"), 0.0f, GetParam("prestigeMostGoldClampMax"));

        // Houses and horses
        float houseHorseScore = housesOwned * GetParam("prestigeHouseUnitMultiplier") +
                                horsesOwned * GetParam("prestigeHorseUnitMultiplier");
        result += ClampValue(houseHorseScore, 0.0f, GetParam("prestigeHouseHorseClampMax"));

        // Heroic achievements
        float heroicScore = questsCompleted * GetParam("prestigeQuestsMultiplier") +
                            dungeonsCleared * GetParam("prestigeDungeonsMultiplier") +
                            dragonSoulsCollected * GetParam("prestigeSoulsMultiplier");
        result += ClampValue(heroicScore, 0.0f, GetParam("prestigeRenownClampMax"));

        return ClampValue(result, GetParam("prestigePrestigeClamp_min"), GetParam("prestigePrestigeClamp_max"));
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
