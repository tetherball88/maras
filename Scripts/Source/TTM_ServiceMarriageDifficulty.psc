scriptname TTM_ServiceMarriageDifficulty

float Function intimacyAdjustment(Actor npc) global
    float res = 0
    Actor[] spouses = MARAS.GetRelationshipPartners(npc, "married")
    Actor existingSpouse = none
    if (spouses.Length > 0)
        existingSpouse = spouses[0]
    endif

    Actor[] courtings = MARAS.GetRelationshipPartners(npc, "engaged")
    Actor existingCourting = none
    if (courtings.Length > 0)
        existingCourting = courtings[0]
    endif

    Actor[] existingLovers = MARAS.GetRelationshipPartners(npc, "lover")

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


int Function calcMarriageSuccessChance(Actor npc) global
    int mostGold = Game.QueryStat("Most Gold Carried")
    int housesOwned = Game.QueryStat("Houses Owned")
    int horsesOwned = Game.QueryStat("Horses Owned")
    int questsCompleted = Game.QueryStat("Quests Completed")
    int dungeonsCleared = Game.QueryStat("Dungeons Cleared")
    int dragonSoulsCollected = Game.QueryStat("Dragon Souls Collected")

    int chance = MARAS.CalculateMarriageSuccessChance(npc, intimacyAdjustment(npc), mostGold, housesOwned, horsesOwned, questsCompleted, dungeonsCleared, dragonSoulsCollected, TTM_Data.GetPlayerKiller()) as int

    return chance * 100
EndFunction
