scriptname TTM_ServiceAffection

Function UpdateAffectionFaction(Actor spouse) global
    if(!spouse)
        return
    endif

    Faction affectionFaction = TTM_JData.GetAffectionFaction()
    int prevAffection = PapyrusUtil.ClampInt(spouse.GetFactionRank(affectionFaction), 0, 100)
    int affection = 15
    bool isSpouse = TTM_Utils.IsSpouse(spouse)
    affection += PapyrusUtil.ClampFloat(GetIntimacyAffection(spouse), -22.0, 22.0) as int
    affection += PapyrusUtil.ClampFloat(GetGiftAffection(spouse), 0.0, 25.0) as int
    affection += PapyrusUtil.ClampFloat(GetDialogueStartedAffection(spouse), 0.0, 5.0) as int
    if(isSpouse)
        affection += PapyrusUtil.ClampFloat(GetLonelinessAffection(spouse), -4.0, 6.0) as int
        affection += PapyrusUtil.ClampFloat(GetSleptAffection(spouse), 0.0, 8.0) as int
        affection += PapyrusUtil.ClampFloat(GetPromotionAffection(spouse), 0.0, 24.0) as int
    endif

    TTM_Debug.trace("TTM_ServiceAffection:"+TTM_Utils.GetActorName(spouse)+":UpdateAffectionFaction:"+affection)
    spouse.SetFactionRank(affectionFaction, PapyrusUtil.ClampFloat(affection, 0.0, 100.0) as int)
    int chance = Utility.RandomInt(1, 100)
    if(affection >= 75)
        ; try to run happy quests from SM Events. It has 5 days cooldown and will hit for any spouse with happy affection
        ; currently there is no quest
        TTM_JData.GetAffectionQuestKeyword("happy").SendStoryEvent()
        if(prevAffection < 75)
            TTM_Utils.SendAffectionChangeThresholdEvent(spouse, "happy", true)
        endif
    elseif(affection >= 50)
        ; try to run content quests from SM Events. It has 5 days cooldown and will hit for any spouse with content affection
        ; currently there is no quest
        TTM_JData.GetAffectionQuestKeyword("content").SendStoryEvent()
        if(prevAffection < 50 || prevAffection >= 75)
            TTM_Utils.SendAffectionChangeThresholdEvent(spouse, "content", prevAffection < 50)
        endif
    elseif(affection >= 25)
        ; try to run troubled quests from SM Events. It has 5 days cooldown and will hit for any spouse with troubled affection
        ; currently there is no quest
        TTM_JData.GetAffectionQuestKeyword("troubled").SendStoryEvent()
        if(prevAffection < 25 || prevAffection >= 50)
            TTM_Utils.SendAffectionChangeThresholdEvent(spouse, "troubled", prevAffection < 25)
        endif
    else
        ; try to run estranged quests from SM Events. It has 5 days cooldown and will hit for any spouse with estranged affection
        ; currently there is only divorce quest
        TTM_JData.GetAffectionQuestKeyword("estranged").SendStoryEvent()
        if(prevAffection >= 25)
            TTM_Utils.SendAffectionChangeThresholdEvent(spouse, "estranged", false)
        endif
    endif
    StorageUtil.ClearAllObjPrefix(spouse, "TTM_Affection_")
EndFunction

int Function GetAffectionRank(Actor spouse) global
    Faction affectionFaction = TTM_JData.GetAffectionFaction()
    int rank = spouse.GetFactionRank(affectionFaction)
    if(rank < 0)
        int relationshipRank = spouse.GetRelationshipRank(TTM_JData.GetPlayer())
        int adj = relationshipRank * 10
        rank = 50 + adj
        SetAffectionRank(spouse, rank)
    endif

    return rank
EndFunction

Function SetAffectionRank(Actor spouse, int amount) global
    Faction affectionFaction = TTM_JData.GetAffectionFaction()
    spouse.SetFactionRank(affectionFaction, amount)
EndFunction

Function AddAffection(Actor spouse, float amount, string suffix) global
    if(!spouse)
        return
    endif

    if(amount > 0)
        SetLastTimeGotAffection(spouse)
    endif

    float currentAffection = GetAffection(spouse, suffix)
    float newAffection = currentAffection + amount
    StorageUtil.SetFloatValue(spouse, "TTM_Affection_" + suffix, newAffection)
    TTM_Debug.trace("TTM_ServiceAffection:"+TTM_Utils.GetActorName(spouse)+":AddAffection("+suffix+"):"+amount+" + "+currentAffection+" = "+newAffection)
EndFunction

float Function GetAffection(Actor spouse, string suffix) global
    if(!spouse)
        return -1.0
    endif

    float affection = StorageUtil.GetFloatValue(spouse, "TTM_Affection_" + suffix)

    TTM_Debug.trace("TTM_ServiceAffection:"+TTM_Utils.GetActorName(spouse)+":GetAffection("+suffix+"):"+affection)

    return affection
EndFunction

float Function GetLonelinessAffection(Actor spouse) global
    float affection = 0
    float daysSince = GetDaysSinceLastGotAffection(spouse)
    string temperament = TTM_Utils.GetSpouseTemperament((spouse))
    if(StorageUtil.HasIntValue(spouse, "following") && StorageUtil.GetIntValue(spouse, "following") == 1)
        return 6.0
    endif

    if(daysSince > 2)
        int count = TTM_ServiceNpcs.GetSpousesCount()
        float perDay = 3.0
        if(temperament == "Independent")
            perDay = 1.0
        elseif(temperament == "humble")
            perDay = 2.0
        elseif(temperament == "romantic")
            perDay = 4.0
        endif

        float mult = 1.0
        if(count >= 3)
            mult = 0.75
        elseif(count >= 5)
            mult = 0.6
        elseif(count >= 7)
            mult = 0.5
        endif
        return perDay * mult
    endif

    return 0.0
EndFunction

Function AddIntimacyAffection(Actor spouse, float affection) global
    string temperament = TTM_Utils.GetSpouseTemperament((spouse))
    float mlt = 1.0
    if(temperament == "Romantic" || temperament == "Jealous")
        mlt = 1.5
    elseif(temperament == "Independent")
        mlt = 0.5
    endif

    return AddAffection(spouse, affection * mlt, "Intimacy")
EndFunction

float Function GetIntimacyAffection(Actor spouse) global
    return GetAffection(spouse, "Intimacy")
EndFunction

Function AddGiftAffection(Actor spouse, float value) global
    int socialClass = TTM_Utils.GetSpouseSocialClassIndex((spouse))

    ;/
    Assuming value = 1000 gold

    socialClass | spouseSocialClassFactor | giftAffection (points)
    ---------------------------------------------------------------
        0      | 0.667                   | 15.00
        1      | 0.810                   | 12.35
        2      | 0.952                   | 10.50
        3      | 1.095                   | 9.13
        4      | 1.238                   | 8.08
        5      | 1.381                   | 7.24
        6      | 1.524                   | 6.56
        7      | 1.667                   | 6.00
    ---------------------------------------------------------------
    Range: Outcast ≈ 15 pts → Ruler ≈ 6 pts
    /;
    float spouseSocialClassFactor = 0.667 + (socialClass / 7.0)
    float giftAffection = value / (spouseSocialClassFactor * 100.0)
    if(TTM_Utils.GetSpouseTemperament((spouse)) == "Romantic")
        giftAffection = giftAffection * 1.5
    endif
    AddAffection(spouse, giftAffection, "Gift")
EndFunction

float Function GetGiftAffection(Actor spouse) global
    return GetAffection(spouse, "Gift")
EndFunction

Function AddDialogueStartedAffection(Actor spouse) global
    AddAffection(spouse, 1.0, "DialogueStarted")
EndFunction

float Function GetDialogueStartedAffection(Actor spouse) global
    return GetAffection(spouse, "DialogueStarted")
EndFunction

Function AddPromotionAffection(Actor spouse, int rankDiff) global
    float promoAffection = 8.0
    string temperament = TTM_Utils.GetSpouseTemperament((spouse))
    if(temperament == "Proud")
        promoAffection = 12.0
    elseif(temperament == "Humble")
        promoAffection = 4.0
    endif
    AddAffection(spouse, promoAffection * rankDiff, "Promotion")
EndFunction

float Function GetPromotionAffection(Actor spouse) global
    return GetAffection(spouse, "Promotion")
EndFunction

Function AddSleptAffection(Actor spouse) global
    float affection = 3.0
    string temperament = TTM_Utils.GetSpouseTemperament((spouse))
    if(temperament == "Romantic")
        affection = 5.0
    elseif(temperament == "Independent")
        affection = 1.0
    elseif(temperament == "Jealous")
        affection = 4.0
    endif
    AddAffection(spouse, affection, "Slept")
EndFunction

float Function GetSleptAffection(Actor spouse) global
    return GetAffection(spouse, "Slept")
EndFunction

Function SetLastTimeGotAffection(Actor spouse) global
    StorageUtil.SetFloatValue(spouse, "TTM_LastGotAffection", Utility.GetCurrentGameTime())
EndFunction

float Function GetDaysSinceLastGotAffection(Actor spouse) global
    float lastGotAffection = StorageUtil.GetFloatValue(spouse, "TTM_LastGotAffection")
    float currentTime = Utility.GetCurrentGameTime()
    float daysSince = currentTime - lastGotAffection
    TTM_Debug.trace("TTM_ServiceAffection:"+TTM_Utils.GetActorName(spouse)+":DaysSinceLastGotAffection:"+daysSince)
    return daysSince
EndFunction

Float Function GetAffectionBuffMultiplier(Actor spouse) global
    int affection = GetAffectionRank(spouse)
    If affection >= 75
        return 1.25
    ElseIf affection >= 50
        return 1
    ElseIf affection >= 25
        return 0.25
    Else
        return 0
    EndIf
EndFunction
