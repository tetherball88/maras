#pragma once

#include "utils/Common.h"

namespace MARAS {

    class MarriageDifficulty {
    public:
        // Calculate marriage success chance on-the-fly
        static float CalculateMarriageSuccessChance(RE::Actor* npc, float intimacyAdjustment, float mostGold,
                                                    float housesOwned, float horsesOwned, float questsCompleted,
                                                    float dungeonsCleared, float dragonSoulsCollected,
                                                    bool playerKiller);

    private:
        // Helper methods for individual calculations
        static bool CheckQuestStage(RE::TESQuest* quest, std::uint32_t stage);
        static float GetParam(const std::string& param);
        static int GetThaneHolds();
        static bool IsGuildLeader();
        static float CalculatePlayerPrestige(float mostGold, float housesOwned, float horsesOwned,
                                             float questsCompleted, float dungeonsCleared, float dragonSoulsCollected);
        static float CalculateGuildAlignmentMod(RE::Actor* npc);
        static bool IsAlwaysSuccessMarriage();
        static bool IsJilted(RE::Actor* npc);
        static bool IsDivorced(RE::Actor* npc);
        static int CountMarried();
        static int CountDivorced();
    };

}  // namespace MARAS
