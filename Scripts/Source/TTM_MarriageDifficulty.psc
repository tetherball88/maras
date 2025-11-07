scriptname TTM_MarriageDifficulty

float Function GetParam(string param) global
    return TTM_JData.GetComplexityVariable(param)
EndFunction

float Function intimacyAdjustment(Actor npc) global
    float res = 0
    Actor existingSpouse = TTM_ServiceRelationsFinder.GetExistingSpouse(npc)
    Actor existingCourting = TTM_ServiceRelationsFinder.GetExistingCourting(npc)
    Form[] existingLovers = TTM_ServiceRelationsFinder.GetExistingLovers(npc)

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
            ; res += 30
            res += GetParam("intimacySpouseLoverPenalty")
        else
            ; res += 25
            res += GetParam("intimacySpouseNotLoverPenalty")
        endif
    elseif(existingCourting)
        if(isCourtingLover)
            ; res += 20
            res += GetParam("intimacyCourtingLoverPenalty")
        else
            ; res += 15
            res += GetParam("intimacyCourtingNotLoverPenalty")
        endif
    elseif(existingLovers.Length >= 1)
        ; res += 10
        res += GetParam("intimacyOtherLoversPenalty")
    endif

    TTM_ServiceIntimateMoments intimacyScript = TTM_JData.GetMarasMainQuest() as TTM_ServiceIntimateMoments

    float intimacy = intimacyScript.GetNpcPlayerIntimacy(npc) * GetParam("intimacyPlayerMultiplier")

    return res - intimacy
EndFunction


float Function calcMarriageSuccessChance(Actor npc) global
    Trace("calcMarriageSuccessChance:Actor:"+TTM_Utils.GetActorName(npc))
    if(TTM_JData.GetAlwaysSuccessMarriage())
        return 1.0
    endif

    int mostGold = Game.QueryStat("Most Gold Carried")
    int housesOwned = Game.QueryStat("Houses Owned")
    int horsesOwned = Game.QueryStat("Horses Owned")
    int questsCompleted = Game.QueryStat("Quests Completed")
    int dungeonsCleared = Game.QueryStat("Dungeons Cleared")
    int dragonSoulsCollected = Game.QueryStat("Dragon Souls Collected")

    float chance = MARAS.CalculateMarriageSuccessChance(npc, intimacyAdjustment(npc), mostGold, housesOwned, horsesOwned, questsCompleted, dungeonsCleared, dragonSoulsCollected)

    Trace("calcMarriageSuccessChance:Chance:"+chance)

    return chance
EndFunction

Function Trace(string msg) global
    TTM_Debug.trace("TTM_MarriageDifficulty:" + msg)
EndFunction
