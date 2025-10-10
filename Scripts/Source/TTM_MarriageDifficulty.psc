scriptname TTM_MarriageDifficulty

bool Function CheckQuestStage(int questId, int stage) global
    Quest holdQuest = Game.GetFormFromFile(questId, "Skyrim.esm") as quest
    if holdQuest
        return holdQuest.GetStageDone(stage)
    endif
    return false
EndFunction

int Function GetThaneHolds() global
    int totalHolds = 0

    if(CheckQuestStage(0xa2ca6, 25)) ; isThane_Eastmarch
        totalHolds += 1
    endif
    if(CheckQuestStage(0xa34de, 25)) ; isThane_Falkreath
        totalHolds += 1
    endif
    if(CheckQuestStage(0xa2c9b, 25)) ; isThane_Haafingar
        totalHolds += 1
    endif
    if(CheckQuestStage(0xa34ce, 25)) ; isThane_Hjaalmarch
        totalHolds += 1
    endif
    if(CheckQuestStage(0xa34d4, 25)) ; isThane_Pale
        totalHolds += 1
    endif
    if(CheckQuestStage(0xa2c86, 25)) ; isThane_Reach
        totalHolds += 1
    endif
    if(CheckQuestStage(0x65bdf, 25)) ; isThane_Rift
        totalHolds += 1
    endif
    if(CheckQuestStage(0xa2c9e, 25)) ; isThane_Whiterun
        totalHolds += 1
    endif
    if(CheckQuestStage(0xa34d7, 25)) ; isThane_Winterhold
        totalHolds += 1
    endif
    return totalHolds
EndFunction

bool Function IsGuildLeader() global
    ; isGuildLeader_companions || isGuildLeader_winterhold || isGuildLeader_thieves
    return CheckQuestStage(0x1cef6, 200) || CheckQuestStage(0x1f258, 200) || CheckQuestStage(0xd7d69, 40)
EndFunction

float Function calcPlayerPrestige() global
    float result = 0

    ; titles & fame

    ; isDragonborn, officially dragonborn
    if(CheckQuestStage(0x2610c, 90))
        result += 25
    endif

    result += GetThaneHolds() * 4 ;  9 thanedoms  = +36

    if(IsGuildLeader())
        result += 8
    endif

    ; wealth & property
    result += PapyrusUtil.ClampFloat(Game.QueryStat("Most Gold Carried") / 5000, 0, 15)

    int houseCount = Game.QueryStat("Houses Owned") * 3
    int horsesCount = Game.QueryStat("Horses Owned") * 3
    result += PapyrusUtil.ClampFloat(houseCount + horsesCount, 0, 15)

    ; heroic renown

    float quests = Game.QueryStat("Quests Completed") * 0.2
    float dungeons = Game.QueryStat("Dungeons Cleared") * 0.1
    float souls = Game.QueryStat("Dragon Souls Collected") * 0.5

    result += PapyrusUtil.ClampFloat(quests + dungeons + souls, 0, 25)

    return PapyrusUtil.ClampFloat(result, 0, 100)
EndFunction

float Function calcGuildAlignmentMod(Actor npc) global
    Actor player = TTM_JData.GetPlayer()
    string spouseClass = TTM_Utils.GetSpouseSocialClass(npc)

    float bestPos = 0.0 ; track strongest positive modifier
    float bestNeg = 0.0 ; track strongest negative modifier (stored as negative)
    float cand = 0.0
    float sameGuild = 0.0

    ; === Companions ===
    Faction companionsFCTN = TTM_JData.GetGameFaction("companions")
    if player.IsInFaction(companionsFCTN)
        cand = 0.0
        if(spouseClass == "working")
            cand = -2
        elseif(spouseClass == "middle")
            cand = -1
        elseif(spouseClass == "religious")
            cand = 1
        elseif(spouseClass == "nobles")
            cand = -1
        elseif(spouseClass == "rulers")
            cand = -2
        endif
        if cand > 0
            bestPos = TTM_Utils.GetMax(bestPos, cand)
        elseif cand < 0
            bestNeg = TTM_Utils.GetMin(bestNeg, cand)
        endif
        if npc.IsInFaction(companionsFCTN)
            sameGuild = -4
        endif
    endif

    ; === Thieves Guild ===
    Faction thievesFCTN = TTM_JData.GetGameFaction("thieves")
    if player.IsInFaction(thievesFCTN)
        cand = 0.0
        if(spouseClass == "outcast")
            cand = -2
        elseif(spouseClass == "poverty")
            cand = -1
        elseif(spouseClass == "middle")
            cand = 1
        elseif(spouseClass == "wealthy")
            cand = 2
        elseif(spouseClass == "religious")
            cand = 2
        elseif(spouseClass == "nobles")
            cand = 2
        elseif(spouseClass == "rulers")
            cand = 3
        endif
        if cand > 0
            bestPos = TTM_Utils.GetMax(bestPos, cand)
        elseif cand < 0
            bestNeg = TTM_Utils.GetMin(bestNeg, cand)
        endif
        if npc.IsInFaction(thievesFCTN)
            sameGuild = -4
        endif
    endif

    ; === Dark Brotherhood ===
    Faction brotherhoodFCTN = TTM_JData.GetGameFaction("brotherhood")
    if player.IsInFaction(brotherhoodFCTN)
        cand = 0.0
        if(spouseClass == "outcast")
            cand = -3
        elseif(spouseClass == "poverty")
            cand = -2
        elseif(spouseClass == "working")
            cand = 1
        elseif(spouseClass == "middle")
            cand = 2
        elseif(spouseClass == "wealthy")
            cand = 3
        elseif(spouseClass == "religious")
            cand = 4
        elseif(spouseClass == "nobles")
            cand = 3
        elseif(spouseClass == "rulers")
            cand = 4
        endif
        if cand > 0
            bestPos = TTM_Utils.GetMax(bestPos, cand)
        elseif cand < 0
            bestNeg = TTM_Utils.GetMin(bestNeg, cand)
        endif
        if npc.IsInFaction(brotherhoodFCTN)
            sameGuild = -4
        endif
    endif

    ; === College of Winterhold ===
    Faction collegeFCTN = TTM_JData.GetGameFaction("college")
    if player.IsInFaction(collegeFCTN)
        cand = 0.0
        if(spouseClass == "outcast")
            cand = 1
        elseif(spouseClass == "working")
            cand = 1
        elseif(spouseClass == "wealthy")
            cand = 1
        elseif(spouseClass == "religious")
            cand = -2
        elseif(spouseClass == "rulers")
            cand = -3
        endif
        if cand > 0
            bestPos = TTM_Utils.GetMax(bestPos, cand)
        elseif cand < 0
            bestNeg = TTM_Utils.GetMin(bestNeg, cand)
        endif
        if npc.IsInFaction(collegeFCTN)
            sameGuild = -4
        endif
    endif

    ; === Bards College ===
    Faction bardsFCTN = TTM_JData.GetGameFaction("bards")
    if CheckQuestStage(0x53511, 300) ; player is Bard
        cand = 0.0
        if(spouseClass == "outcast")
            cand = 1
        elseif(spouseClass == "middle")
            cand = -1
        elseif(spouseClass == "wealthy")
            cand = -1
        elseif(spouseClass == "nobles")
            cand = -2
        elseif(spouseClass == "rulers")
            cand = -1
        endif
        if cand > 0
            bestPos = TTM_Utils.GetMax(bestPos, cand)
        elseif cand < 0
            bestNeg = TTM_Utils.GetMin(bestNeg, cand)
        endif
        if npc.IsInFaction(bardsFCTN)
            sameGuild = -4
        endif
    endif

    ; pick the single strongest effect by absolute value
    float res = bestPos
    if (-bestNeg) > bestPos
        res = bestNeg ; more negative wins
    endif

    return res + sameGuild
EndFunction

float Function intimacyAdjustment(Actor npc) global
    float res = 0
    Actor existingSpouse = TTRF_Store.GetSpouse(npc)
    Actor existingCourting = TTRF_Store.GetCourting(npc)
    Form[] existingLovers = TTRF_Store.GetLovers(npc)

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

    float intimacy = TTM_ServiceLoversLedger.GetNpcPlayerIntimacy(npc)

    return res - intimacy
EndFunction


float Function calcMarriageSuccessChance(Actor npc) global
    Actor player = TTM_JData.GetPlayer()
    int npcLevel = npc.GetLevel()
    int playerLevel = player.GetLevel()
    float levelDiff = npcLevel - playerLevel
    int socialClassIndex = TTM_Utils.GetSpouseSocialIndexByType(TTM_Utils.GetSpouseSocialClass(npc))

    ;=== 1. Complexity sum ===
    float complexity = 50.0

    ; prestige delta
    float target = 10 + 10 * socialClassIndex ; // 10 … 70
    TTM_Debug.trace("calcMarriageSuccessChance:targetScore:"+target)
    float pDelta = (target - calcPlayerPrestige()) * 0.30
    TTM_Debug.trace("calcMarriageSuccessChance:prestigeDelta:"+pDelta)
    complexity += pDelta

    ; player were engaged but it was jilted, it's harder to get another try */
    if (TTM_ServiceNpcs.IsJilted(npc))
        TTM_Debug.trace("calcMarriageSuccessChance:jilted")
        complexity += 10
    endif

    ; player were married to npc at some point but divorced, so it's harder to re-marry
    if (TTM_ServiceNpcs.IsDivorced(npc))
        TTM_Debug.trace("calcMarriageSuccessChance:divorced")
        complexity += 20
    endif

    ; player killed spouse or fiance without reason - red flag for future potential candidates
    if(TTM_JData.GetPlayerKiller())
        complexity += 50
    endif

    ; spouse count
    complexity += TTM_ServiceNpcs.CountBucket("married") * 2
    TTM_Debug.trace("calcMarriageSuccessChance:marriedCount:"+TTM_ServiceNpcs.CountBucket("married") * 2)
    ; divorced times before
    complexity += TTM_ServiceNpcs.CountBucket("divorced")
    TTM_Debug.trace("calcMarriageSuccessChance:divorcedCount:"+TTM_ServiceNpcs.CountBucket("divorced"))
    ; level diff
    complexity += PapyrusUtil.ClampFloat(levelDiff * 0.10, -10, 10)
    TTM_Debug.trace("calcMarriageSuccessChance:levelDiff:"+PapyrusUtil.ClampFloat(levelDiff * 0.10, -10, 10))
    ; speech bonus
    complexity += -0.1 * player.GetActorValue("speechcraft")
    TTM_Debug.trace("calcMarriageSuccessChance:Speech:" + (-0.05 * player.GetActorValue("speechcraft")))
    ; relationship rank
    complexity += -3 * npc.GetRelationshipRank(player)
    TTM_Debug.trace("calcMarriageSuccessChance:relationshipRanjk:"+ (-3 * npc.GetRelationshipRank(player)))
    ; guild alignment
    complexity += calcGuildAlignmentMod(npc)
    TTM_Debug.trace("calcMarriageSuccessChance:guilds:"+ calcGuildAlignmentMod(npc))
    ; intimacy adjustment
    complexity += intimacyAdjustment(npc)
    TTM_Debug.trace("calcMarriageSuccessChance:intimacyAdjustment:"+ intimacyAdjustment(npc))


    ; === 2. Clamp difficulty 0-100 ===
    float difficulty = PapyrusUtil.ClampFloat(complexity, 0, 100);

    ; === 3. Logistic baseline ===
    float chance = PapyrusUtil.ClampFloat(1 - difficulty / 100, 0.0, 1.0)

    TTM_Debug.trace("calcMarriageSuccessChance:finalChance:"+ chance)

    return chance
    ; return 1
EndFunction
