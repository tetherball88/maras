#include "core/MarriageDifficulty.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>

#include "core/NPCRelationshipManager.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"

namespace MARAS {

    float MarriageDifficulty::CalculateMarriageSuccessChance(RE::Actor* npc, float intimacyAdjustment, float mostGold,
                                                             float housesOwned, float horsesOwned,
                                                             float questsCompleted, float dungeonsCleared,
                                                             float dragonSoulsCollected) {
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
        if (GetPlayerKiller()) {
            complexity += GetParam("playerKillerPenalty");
            MARAS_LOG_DEBUG("Applied player killer penalty");
        }

        // Spouse count penalty
        float marriedScore = CountMarried() * GetParam("marriedCountMultiplier");
        complexity += marriedScore;

        // Divorced count penalty
        float divorcedScore = CountDivorced() * GetParam("divorcedCountMultiplier");
        complexity += divorcedScore;

        // Level difference
        float levelDiffScore = std::clamp(levelDiff * GetParam("levelDiffMultiplier"), -10.0f, 10.0f);
        complexity += levelDiffScore;

        // Speech bonus
        complexity +=
            GetParam("speechcraftMultiplier") * player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kSpeech);

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
        complexity += CalculateGuildAlignmentMod(npc);

        // Intimacy adjustment (passed from Papyrus)
        complexity += intimacyAdjustment;

        MARAS_LOG_DEBUG("Final complexity: {}", complexity);

        // === 2. Clamp difficulty 0-100 ===
        float difficulty = std::clamp(complexity, 0.0f, 100.0f);

        // === 3. Calculate success chance ===
        float chance = std::clamp(1.0f - difficulty / 100.0f, 0.0f, 1.0f);

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        MARAS_LOG_INFO("Marriage difficulty calculation completed for {} in {} microseconds (chance: {:.3f})",
                       npc->GetDisplayFullName(), duration.count(), chance);

        return chance;
    }

    bool MarriageDifficulty::CheckQuestStage(std::uint32_t questId, std::uint32_t stage) {
        auto quest = RE::TESForm::LookupByID<RE::TESQuest>(questId);
        if (!quest) {
            return false;
        }
        return quest->GetCurrentStageID() >= stage;
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
            {"levelDiffMultiplier", 0.10f},
            {"speechcraftMultiplier", -0.1f},
            {"relationshipRankMultiplier", -3.0f}};

        auto it = configParams.find(param);
        if (it != configParams.end()) {
            return it->second;
        }

        MARAS_LOG_WARN("Parameter '{}' not found in config, returning 0.0f", param);
        return 0.0f;
    }

    int MarriageDifficulty::GetThaneHolds() {
        int totalHolds = 0;

        // Check all thane quests
        if (CheckQuestStage(0xa2ca6, 25)) totalHolds++;  // Eastmarch
        if (CheckQuestStage(0xa34de, 25)) totalHolds++;  // Falkreath
        if (CheckQuestStage(0xa2c9b, 25)) totalHolds++;  // Haafingar
        if (CheckQuestStage(0xa34ce, 25)) totalHolds++;  // Hjaalmarch
        if (CheckQuestStage(0xa34d4, 25)) totalHolds++;  // Pale
        if (CheckQuestStage(0xa2c86, 25)) totalHolds++;  // Reach
        if (CheckQuestStage(0x65bdf, 25)) totalHolds++;  // Rift
        if (CheckQuestStage(0xa2c9e, 25)) totalHolds++;  // Whiterun
        if (CheckQuestStage(0xa34d7, 25)) totalHolds++;  // Winterhold

        return totalHolds;
    }

    bool MarriageDifficulty::IsGuildLeader() {
        // Check guild leader quests
        return CheckQuestStage(0x1cef6, 200) ||  // Companions
               CheckQuestStage(0x1f258, 200) ||  // College of Winterhold
               CheckQuestStage(0xd7d69, 40);     // Thieves Guild
    }

    float MarriageDifficulty::CalculatePlayerPrestige(float mostGold, float housesOwned, float horsesOwned,
                                                      float questsCompleted, float dungeonsCleared,
                                                      float dragonSoulsCollected) {
        float result = 0.0f;

        // Dragonborn status
        if (CheckQuestStage(0x2610c, 90)) {
            result += GetParam("prestigeDragonbornBonus");
        }

        // Thane holds
        result += GetThaneHolds() * GetParam("prestigeThaneHoldValue");

        // Guild leader
        if (IsGuildLeader()) {
            result += GetParam("prestigeGuildLeaderBonus");
        }

        // Wealth - use passed game statistics from Papyrus
        result += std::clamp(mostGold / GetParam("prestigeMostGoldDivisor"), 0.0f, 15.0f);

        // Houses and horses - use passed game statistics from Papyrus
        float houseCount = housesOwned * GetParam("prestigeHouseUnitMultiplier");
        float horsesCount = horsesOwned * GetParam("prestigeHorseUnitMultiplier");
        result += std::clamp(houseCount + horsesCount, 0.0f, 15.0f);

        // Heroic achievements - use passed game statistics from Papyrus
        float quests = questsCompleted * GetParam("prestigeQuestsMultiplier");
        float dungeons = dungeonsCleared * GetParam("prestigeDungeonsMultiplier");
        float souls = dragonSoulsCollected * GetParam("prestigeSoulsMultiplier");
        result += std::clamp(quests + dungeons + souls, 0.0f, 25.0f);

        return std::clamp(result, 0.0f, 100.0f);
    }

    float MarriageDifficulty::CalculateGuildAlignmentMod(RE::Actor* npc) {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !npc) return 0.0f;

        auto& manager = NPCRelationshipManager::GetSingleton();
        auto socialClass = manager.GetSocialClass(npc->GetFormID());

        // Use existing enum-to-string function and convert to lowercase for config lookup
        std::string spouseClass = Utils::ToLower(Utils::SocialClassToString(socialClass));

        float bestPos = 0.0f;  // track strongest positive modifier
        float bestNeg = 0.0f;  // track strongest negative modifier (stored as negative)
        float cand = 0.0f;
        float sameGuild = 0.0f;

        // === Companions ===
        auto companionsFaction = Utils::LookupForm<RE::TESFaction>(0x48362, "Skyrim.esm");
        if (companionsFaction && player->IsInFaction(companionsFaction)) {
            cand = GetParam("guildCompanions_" + spouseClass);
            if (cand > 0) {
                bestPos = std::max(bestPos, cand);
            } else if (cand < 0) {
                bestNeg = std::min(bestNeg, cand);
            }
            if (npc->IsInFaction(companionsFaction)) {
                sameGuild += GetParam("guild_sameGuildBonus");
            }
        }

        // === Thieves Guild ===
        auto thievesFaction = Utils::LookupForm<RE::TESFaction>(0x29da9, "Skyrim.esm");
        if (thievesFaction && player->IsInFaction(thievesFaction)) {
            cand = GetParam("guildThieves_" + spouseClass);
            if (cand > 0) {
                bestPos = std::max(bestPos, cand);
            } else if (cand < 0) {
                bestNeg = std::min(bestNeg, cand);
            }
            if (npc->IsInFaction(thievesFaction)) {
                sameGuild += GetParam("guild_sameGuildBonus");
            }
        }

        // === Dark Brotherhood ===
        auto brotherhoodFaction = Utils::LookupForm<RE::TESFaction>(0x1bdb3, "Skyrim.esm");
        if (brotherhoodFaction && player->IsInFaction(brotherhoodFaction)) {
            cand = GetParam("guildBrotherhood_" + spouseClass);
            if (cand > 0) {
                bestPos = std::max(bestPos, cand);
            } else if (cand < 0) {
                bestNeg = std::min(bestNeg, cand);
            }
            if (npc->IsInFaction(brotherhoodFaction)) {
                sameGuild += GetParam("guild_sameGuildBonus");
            }
        }

        // === College of Winterhold ===
        auto collegeFaction = Utils::LookupForm<RE::TESFaction>(0x1f259, "Skyrim.esm");
        if (collegeFaction && player->IsInFaction(collegeFaction)) {
            cand = GetParam("guildCollege_" + spouseClass);
            if (cand > 0) {
                bestPos = std::max(bestPos, cand);
            } else if (cand < 0) {
                bestNeg = std::min(bestNeg, cand);
            }
            if (npc->IsInFaction(collegeFaction)) {
                sameGuild += GetParam("guild_sameGuildBonus");
            }
        }

        // === Bards College ===
        auto bardsFaction = Utils::LookupForm<RE::TESFaction>(0xc13c7, "Skyrim.esm");
        if (CheckQuestStage(0x53511, 300)) {  // player is Bard
            cand = GetParam("guildBards_" + spouseClass);
            if (cand > 0) {
                bestPos = std::max(bestPos, cand);
            } else if (cand < 0) {
                bestNeg = std::min(bestNeg, cand);
            }
            if (bardsFaction && npc->IsInFaction(bardsFaction)) {
                sameGuild += GetParam("guild_sameGuildBonus");
            }
        }

        // Pick the single strongest effect by absolute value
        float res = bestPos;
        if (-bestNeg > bestPos) {
            res = bestNeg;  // more negative wins
        }

        return res + sameGuild;
    }

    bool MarriageDifficulty::IsJilted(RE::Actor* npc) {
        if (!npc) return false;
        auto& manager = NPCRelationshipManager::GetSingleton();
        return manager.IsJilted(npc->GetFormID());
    }

    bool MarriageDifficulty::IsDivorced(RE::Actor* npc) {
        if (!npc) return false;
        auto& manager = NPCRelationshipManager::GetSingleton();
        return manager.IsDivorced(npc->GetFormID());
    }

    bool MarriageDifficulty::GetPlayerKiller() {
        // Check global variable or quest flag for player killer status
        // For now, return false - would need to track this in the C++ plugin
        // This could be set when player kills spouses and tracked in NPCRelationshipManager
        return false;
    }

    int MarriageDifficulty::CountMarried() {
        auto& manager = NPCRelationshipManager::GetSingleton();
        return static_cast<int>(manager.GetMarriedCount());
    }

    int MarriageDifficulty::CountDivorced() {
        auto& manager = NPCRelationshipManager::GetSingleton();
        return static_cast<int>(manager.GetDivorcedCount());
    }

}  // namespace MARAS
