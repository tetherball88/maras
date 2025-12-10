scriptname TTM_ServiceMarriageDifficulty

float Function intimacyAdjustment(Actor npc) global
    float res = 0
    Actor existingSpouse = TTM_ServiceRelationsFinder.GetExistingSpouse(npc)
    Actor existingCourting = TTM_ServiceRelationsFinder.GetExistingCourting(npc)
    Actor[] existingLovers = TTM_ServiceRelationsFinder.GetExistingLovers(npc)

    int i = 0

    bool isSpouseLover = false
    bool isCourtingLover = false
    bool isAnotherLover = false

    while(i < existingLovers.length)
        if(existingLovers[i] == existingSpouse)
            isSpouseLover = true
        elseif(existingLovers[i] == existingCourting)
            isCourtingLover = true
        else
            isAnotherLover = true
        endif

        i += 1
    endwhile

    if(existingSpouse)
        if(isSpouseLover)
            res += 30
        else
            res += 25
        endif
    elseif(existingCourting)
        if(isCourtingLover)
            res += 20
        else
            res += 15
        endif
    elseif(existingLovers.Length >= 1)
        res += 10
    endif

    TTM_ServiceIntimateMoments intimacyScript = TTM_Data.GetMarasMainQuest() as TTM_ServiceIntimateMoments

    float intimacy = intimacyScript.GetNpcPlayerIntimacy(npc)

    return res - intimacy
EndFunction


float Function calcMarriageSuccessChance(Actor npc) global
    if(TTM_Data.GetAlwaysSuccessMarriage())
        TTM_Debug.debug("calcMarriageSuccessChance: Always success enabled, returning 1.0")
        return 1.0
    endif

    int mostGold = Game.QueryStat("Most Gold Carried")
    int housesOwned = Game.QueryStat("Houses Owned")
    int horsesOwned = Game.QueryStat("Horses Owned")
    int questsCompleted = Game.QueryStat("Quests Completed")
    int dungeonsCleared = Game.QueryStat("Dungeons Cleared")
    int dragonSoulsCollected = Game.QueryStat("Dragon Souls Collected")

    float chance = MARAS.CalculateMarriageSuccessChance(npc, intimacyAdjustment(npc), mostGold, housesOwned, horsesOwned, questsCompleted, dungeonsCleared, dragonSoulsCollected, TTM_Data.GetPlayerKiller())

    return chance
EndFunction
