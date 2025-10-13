scriptname TTM_MarriageDifficulty

bool Function CheckQuestStage(int questId, int stage) global
    Quest holdQuest = Game.GetFormFromFile(questId, "Skyrim.esm") as quest
    if holdQuest
        return holdQuest.GetStageDone(stage)
    endif
    return false
EndFunction

float Function GetParam(string param) global
    return TTM_JData.GetComplexityVariable(param)
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
        ; result += 25
        result += GetParam("prestigeDragonbornBonus")
    endif

    ; result += GetThaneHolds() * 4 ;  9 thanedoms  = +36
    result += GetThaneHolds() * GetParam("prestigeThaneHoldValue") ;  9 thanedoms  = +36

    if(IsGuildLeader())
        ; result += 8
        result += GetParam("prestigeGuildLeaderBonus")
    endif

    ; wealth & property
    ; result += PapyrusUtil.ClampFloat(Game.QueryStat("Most Gold Carried") / 5000, 0, 15)
    result += PapyrusUtil.ClampFloat(Game.QueryStat("Most Gold Carried") / GetParam("prestigeMostGoldDivisor"), 0, 15)

    ;int houseCount = Game.QueryStat("Houses Owned") * 3
    ;int horsesCount = Game.QueryStat("Horses Owned") * 3
    float houseCount = Game.QueryStat("Houses Owned") * GetParam("prestigeHouseUnitMultiplier")
    float horsesCount = Game.QueryStat("Horses Owned") * GetParam("prestigeHorseUnitMultiplier")
    result += PapyrusUtil.ClampFloat(houseCount + horsesCount, 0, 15)

    ; heroic renown

    ; float quests = Game.QueryStat("Quests Completed") * 0.2
    ; float dungeons = Game.QueryStat("Dungeons Cleared") * 0.1
    ; float souls = Game.QueryStat("Dragon Souls Collected") * 0.5
    float quests = Game.QueryStat("Quests Completed") * GetParam("prestigeQuestsMultiplier")
    float dungeons = Game.QueryStat("Dungeons Cleared") * GetParam("prestigeDungeonsMultiplier")
    float souls = Game.QueryStat("Dragon Souls Collected") * GetParam("prestigeSoulsMultiplier")

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
        if(spouseClass == "outcast")
            ; cand = 0
            cand = GetParam("guildCompanions_outcast")
        elseif(spouseClass == "poverty")
            ; cand = 0
            cand = GetParam("guildCompanions_poverty")
        elseif(spouseClass == "working")
            ; cand = -2
            cand = GetParam("guildCompanions_working")
        elseif(spouseClass == "middle")
            ; cand = -1
            cand = GetParam("guildCompanions_middle")
        elseif(spouseClass == "wealthy")
            ; cand = 0
            cand = GetParam("guildCompanions_wealthy")
        elseif(spouseClass == "religious")
            ; cand = 1
            cand = GetParam("guildCompanions_religious")
        elseif(spouseClass == "nobles")
            ; cand = -1
            cand = GetParam("guildCompanions_nobles")
        elseif(spouseClass == "rulers")
            ; cand = -2
            cand = GetParam("guildCompanions_rulers")
        endif
        if cand > 0
            bestPos = TTM_Utils.GetMax(bestPos, cand)
        elseif cand < 0
            bestNeg = TTM_Utils.GetMin(bestNeg, cand)
        endif
        if npc.IsInFaction(companionsFCTN)
            ; sameGuild = -4
            sameGuild = GetParam("guild_sameGuildBonus")
        endif
    endif

    ; === Thieves Guild ===
    Faction thievesFCTN = TTM_JData.GetGameFaction("thieves")
    if player.IsInFaction(thievesFCTN)
        cand = 0.0
        if(spouseClass == "outcast")
            ; cand = -2
            cand = GetParam("guildThieves_outcast")
        elseif(spouseClass == "poverty")
            ; cand = -1
            cand = GetParam("guildThieves_poverty")
        elseif(spouseClass == "working")
            ; cand = 0
            cand = GetParam("guildThieves_working")
        elseif(spouseClass == "middle")
            ; cand = 1
            cand = GetParam("guildThieves_middle")
        elseif(spouseClass == "wealthy")
            ; cand = 2
            cand = GetParam("guildThieves_wealthy")
        elseif(spouseClass == "religious")
            ; cand = 2
            cand = GetParam("guildThieves_religious")
        elseif(spouseClass == "nobles")
            ; cand = 2
            cand = GetParam("guildThieves_nobles")
        elseif(spouseClass == "rulers")
            ; cand = 3
            cand = GetParam("guildThieves_rulers")
        endif
        if cand > 0
            bestPos = TTM_Utils.GetMax(bestPos, cand)
        elseif cand < 0
            bestNeg = TTM_Utils.GetMin(bestNeg, cand)
        endif
        if npc.IsInFaction(thievesFCTN)
            ; sameGuild = -4
            sameGuild = GetParam("guild_sameGuildBonus")
        endif
    endif

    ; === Dark Brotherhood ===
    Faction brotherhoodFCTN = TTM_JData.GetGameFaction("brotherhood")
    if player.IsInFaction(brotherhoodFCTN)
        cand = 0.0
        if(spouseClass == "outcast")
            ; cand = -3
            cand = GetParam("guildBrotherhood_outcast")
        elseif(spouseClass == "poverty")
            ; cand = -2
            cand = GetParam("guildBrotherhood_poverty")
        elseif(spouseClass == "working")
            ; cand = 1
            cand = GetParam("guildBrotherhood_working")
        elseif(spouseClass == "middle")
            ; cand = 2
            cand = GetParam("guildBrotherhood_middle")
        elseif(spouseClass == "wealthy")
            ; cand = 3
            cand = GetParam("guildBrotherhood_wealthy")
        elseif(spouseClass == "religious")
            ; cand = 4
            cand = GetParam("guildBrotherhood_religious")
        elseif(spouseClass == "nobles")
            ; cand = 3
            cand = GetParam("guildBrotherhood_nobles")
        elseif(spouseClass == "rulers")
            ; cand = 4
            cand = GetParam("guildBrotherhood_rulers")
        endif
        if cand > 0
            bestPos = TTM_Utils.GetMax(bestPos, cand)
        elseif cand < 0
            bestNeg = TTM_Utils.GetMin(bestNeg, cand)
        endif
        if npc.IsInFaction(brotherhoodFCTN)
            ; sameGuild = -4
            sameGuild = GetParam("guild_sameGuildBonus")
        endif
    endif

    ; === College of Winterhold ===
    Faction collegeFCTN = TTM_JData.GetGameFaction("college")
    if player.IsInFaction(collegeFCTN)
        cand = 0.0
        if(spouseClass == "outcast")
            ; cand = 1
            cand = GetParam("guildCollege_outcast")
        elseif(spouseClass == "poverty")
            ; cand = 0
            cand = GetParam("guildCollege_poverty")
        elseif(spouseClass == "working")
            ; cand = 1
            cand = GetParam("guildCollege_working")
        elseif(spouseClass == "middle")
            ; cand = 0
            cand = GetParam("guildCollege_middle")
        elseif(spouseClass == "wealthy")
            ; cand = 1
            cand = GetParam("guildCollege_wealthy")
        elseif(spouseClass == "religious")
            ; cand = -2
            cand = GetParam("guildCollege_religious")
        elseif(spouseClass == "nobles")
            ; cand = 0
            cand = GetParam("guildCollege_nobles")
        elseif(spouseClass == "rulers")
            ; cand = -3
            cand = GetParam("guildCollege_rulers")
        endif
        if cand > 0
            bestPos = TTM_Utils.GetMax(bestPos, cand)
        elseif cand < 0
            bestNeg = TTM_Utils.GetMin(bestNeg, cand)
        endif
        if npc.IsInFaction(collegeFCTN)
            ; sameGuild = -4
            sameGuild = GetParam("guild_sameGuildBonus")
        endif
    endif

    ; === Bards College ===
    Faction bardsFCTN = TTM_JData.GetGameFaction("bards")
    if CheckQuestStage(0x53511, 300) ; player is Bard
        cand = 0.0
        if(spouseClass == "outcast")
            ; cand = 1
            cand = GetParam("guildBards_outcast")
        elseif(spouseClass == "poverty")
            ; cand = 0
            cand = GetParam("guildBards_poverty")
        elseif(spouseClass == "working")
            ; cand = 0
            cand = GetParam("guildBards_working")
        elseif(spouseClass == "middle")
            ; cand = -1
            cand = GetParam("guildBards_middle")
        elseif(spouseClass == "wealthy")
            ; cand = -1
            cand = GetParam("guildBards_wealthy")
        elseif(spouseClass == "religious")
            ; cand = 0
            cand = GetParam("guildBards_religious")
        elseif(spouseClass == "nobles")
            ; cand = -2
            cand = GetParam("guildBards_nobles")
        elseif(spouseClass == "rulers")
            ; cand = -1
            cand = GetParam("guildBards_rulers")
        endif
        if cand > 0
            bestPos = TTM_Utils.GetMax(bestPos, cand)
        elseif cand < 0
            bestNeg = TTM_Utils.GetMin(bestNeg, cand)
        endif
        if npc.IsInFaction(bardsFCTN)
            ; sameGuild = -4
            sameGuild = GetParam("guild_sameGuildBonus")
        endif
    endif

    ; pick the single strongest effect by absolute value
    float res = bestPos
    if -bestNeg > bestPos
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
            ; res += 30
            res += GetParam("intimacySpouseLoverBonus")
        else
            ; res += 25
            res += GetParam("intimacySpouseNotLoverBonus")
        endif
    elseif(existingCourting)
        if(isCourtingLover)
            ; res += 20
            res += GetParam("intimacyCourtingLoverBonus")
        else
            ; res += 15
            res += GetParam("intimacyCourtingNotLoverBonus")
        endif
    elseif(existingLovers.Length >= 1)
        ; res += 10
        res += GetParam("intimacyOtherLoversBonus")
    endif

    TTM_ServiceIntimateMoments intimacyScript = TTM_JData.GetGameQuest("marasMain") as TTM_ServiceIntimateMoments

    float intimacy = intimacyScript.GetNpcPlayerIntimacy(npc) * GetParam("intimacyPlayerMultiplier")

    return res - intimacy
EndFunction


float Function calcMarriageSuccessChance(Actor npc) global
    Trace("calcMarriageSuccessChance:Actor:"+TTM_Utils.GetActorName(npc))
    if(TTM_MCM_State.GetAlwaysSuccessMarriage())
        return 1.0
    endif
    Actor player = TTM_JData.GetPlayer()
    int npcLevel = npc.GetLevel()
    int playerLevel = player.GetLevel()
    float levelDiff = npcLevel - playerLevel
    int socialClassIndex = TTM_Utils.GetSpouseSocialIndexByType(TTM_Utils.GetSpouseSocialClass(npc))

    ;=== 1. Complexity sum ===
    ; float complexity = 50.0
    float complexity = GetParam("initialComplexity")

    ; prestige delta
    ; float target = 10 + 10 * socialClassIndex ; // 10 … 70
    float target = GetParam("prestigeTargetBase") + GetParam("prestigeTargetPerSocialIndexMultiplier") * socialClassIndex ; // 10 … 70
    Trace("calcMarriageSuccessChance:targetScore:"+target)
    ; float pDelta = (target - calcPlayerPrestige()) * 0.30
    float playerPrestige = calcPlayerPrestige()
    float pDelta = (target - playerPrestige) * GetParam("prestigeDeltaMultiplier")
    Trace("calcMarriageSuccessChance:player'sPrestige:"+playerPrestige+":target:"+target+":prestigeDelta:"+pDelta)
    complexity += pDelta

    ; player were engaged but it was jilted, it's harder to get another try */
    if (TTM_Utils.IsJilted(npc))
        Trace("calcMarriageSuccessChance:jilted")
        ; complexity += 10
        complexity += GetParam("jiltedPenalty")
    endif

    ; player were married to npc at some point but divorced, so it's harder to re-marry
    if (TTM_Utils.IsDivorced(npc))
        Trace("calcMarriageSuccessChance:divorced")
        ; complexity += 20
        complexity += GetParam("divorcedPenalty")
    endif

    ; player killed spouse or fiance without reason - red flag for future potential candidates
    if(TTM_JData.GetPlayerKiller())
        Trace("calcMarriageSuccessChance:playerKilledSpouseBefore")
        ; complexity += 50
        complexity += GetParam("playerKillerPenalty")
    endif

    ; spouse count
    ; complexity += TTM_ServiceNpcs.CountBucket("married") * 2
    float marriedScore = TTM_ServiceNpcs.CountBucket("married") * GetParam("marriedCountMultiplier")
    complexity += marriedScore
    Trace("calcMarriageSuccessChance:marriedCount:"+marriedScore)
    ; divorced times before
    ; complexity += TTM_ServiceNpcs.CountBucket("divorced")
    float divorcedScore = TTM_ServiceNpcs.CountBucket("divorced") * GetParam("divorcedCountMultiplier")
    complexity += divorcedScore
    Trace("calcMarriageSuccessChance:divorcedCount:"+divorcedScore)
    ; level diff
    ; complexity += PapyrusUtil.ClampFloat(levelDiff * 0.10, -10, 10)
    float levelDiffScore = PapyrusUtil.ClampFloat(levelDiff * GetParam("levelDiffMultiplier"), -10, 10)
    complexity += levelDiffScore
    Trace("calcMarriageSuccessChance:levelDiff:"+levelDiffScore)
    ; speech bonus
    ; complexity += -0.1 * player.GetActorValue("speechcraft")
    complexity += GetParam("speechcraftMultiplier") * player.GetActorValue("speechcraft")
    Trace("calcMarriageSuccessChance:Speech:" + (-0.05 * player.GetActorValue("speechcraft")))
    ; relationship rank
    ; complexity += -3 * npc.GetRelationshipRank(player)
    float relationshipScore = GetParam("relationshipRankMultiplier") * npc.GetRelationshipRank(player)
    complexity += relationshipScore
    Trace("calcMarriageSuccessChance:relationshipRanjk:" + relationshipScore)
    ; guild alignment
    complexity += calcGuildAlignmentMod(npc)
    Trace("calcMarriageSuccessChance:guilds:"+ calcGuildAlignmentMod(npc))
    ; intimacy adjustment
    complexity += intimacyAdjustment(npc)
    Trace("calcMarriageSuccessChance:intimacyAdjustment:"+ intimacyAdjustment(npc))


    ; === 2. Clamp difficulty 0-100 ===
    float difficulty = PapyrusUtil.ClampFloat(complexity, 0, 100);

    ; === 3. Logistic baseline ===
    float chance = PapyrusUtil.ClampFloat(1 - difficulty / 100, 0.0, 1.0)

    Trace("calcMarriageSuccessChance:finalChance:"+ chance)

    return chance
EndFunction

Function Trace(string msg) global
    ; TTM_Debug.trace("TTM_MarriageDifficulty:" + msg)
EndFunction
