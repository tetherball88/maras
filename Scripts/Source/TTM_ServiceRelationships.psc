scriptname TTM_ServiceRelationships

;/
  Marks an NPC as a candidate and adds to the candidate bucket.
  @param npc The NPC actor
/;
Function MakeNpcCandidate(Actor npc) global
    int playerRank = TTM_Data.GetPlayer().GetRelationshipRank(npc)
    MARAS.SetPermanentAffection(npc, 50 + (playerRank * 10))
    string msg = "I think " + TTM_Utils.GetActorName(npc) + " is a good match."
    Debug.Notification(msg)
EndFunction

;/
  Marks an NPC as engaged and adds to the fiances bucket.
  @param npc The NPC actor
/;
Function MakeNpcEngaged(Actor npc) global
    bool alwaysSkipWeddings = TTM_Data.GetSkipWedding()
    int answerSkipWedding = -1
    if(TTM_Data.GetPlayerHadWedding() && !alwaysSkipWeddings && StorageUtil.GetIntValue(none, "TTM_DontShowSkipWeddingMessage", 0) == 0)
        answerSkipWedding = TTM_Data.GetSkipWeddingMsg().Show()
    endif
    if(answerSkipWedding == 2)
        StorageUtil.SetIntValue(none, "TTM_DontShowSkipWeddingMessage", 1)
    endif
    if(alwaysSkipWeddings || answerSkipWedding == 0)
        TTM_Utils.FadeToBlack()
        MARAS.PromoteNPCToStatus(npc, "married")
        TTM_ServiceSkyrimNet.SimulatePostWeddingIfSkippedBehavior(npc)
        return
    endif
    Actor player = TTM_Data.GetPlayer()
    npc.SetRelationshipRank(player, 3)
    TTM_ServiceMarriageQuest.StartEngagement(npc)
    if(npc.HasSpell(TTM_Data.GetBreakupCooldownSpell()))
        npc.RemoveSpell(TTM_Data.GetBreakupCooldownSpell())
    endif
    MARAS.SetPermanentAffection(npc, 100)
    string msg = TTM_Utils.GetActorName(npc) + " and I are engaged to be married."
    Debug.Notification(msg)

    PacifyFianceOrSpouse(npc)
EndFunction

;/
  Marks an NPC as married, triggers wedding logic.
  @param npc The NPC actor
/;
Function MakeNpcMarried(Actor npc) global
    Actor player = TTM_Data.GetPlayer()
    npc.SetRelationshipRank(player, 4)
    ;re-evaluate spouses bonuses on each spouse added
    TTM_ServiceBuff.CalculatePermanentMultipliers()
    TTM_ServiceBuff.CalculateFollowerMultipliers()

    ; it will check if npc who becomes spouse actually is current fiance with wedding about to happen
    TTM_ServiceMarriageQuest.SkipWedding(npc)
    if(npc.HasSpell(TTM_Data.GetBreakupCooldownSpell()))
        npc.RemoveSpell(TTM_Data.GetBreakupCooldownSpell())
    endif
    MARAS.SetPermanentAffection(npc, 100)
    string msg = TTM_Utils.GetActorName(npc) + " and I are now newlyweds."
    Debug.Notification(msg)

    if(npc.GetActorValue("Aggression") > 0)
        npc.AddSpell(TTM_Data.GetPacifyFianceOrSpouseSpell())
    endif
    PacifyFianceOrSpouse(npc)
EndFunction

;/
  Marks an NPC as divorced, removes from spouse lists.
  @param npc The NPC actor
/;
Function MakeNpcDivorced(Actor npc) global
    bool res = TTM_Data.GetDivorceKeyword().SendStoryEventAndWait(akRef1 = npc)
    Actor player = TTM_Data.GetPlayer()
    npc.SetRelationshipRank(player, -2)
    ;re-evaluate spouses bonuses on each spouse removed
    TTM_ServiceBuff.CalculatePermanentMultipliers()
    TTM_ServiceBuff.CalculateFollowerMultipliers()

    TTM_ServiceSpouseAssets.StopShareHouseWithPlayer(npc, "divorce")
    TTM_ServicePlayerHouse.ReleaseSpouseFromPlayerHome(npc)
    SetBrokeupTime(npc)
    npc.AddSpell(TTM_Data.GetBreakupCooldownSpell())
    MARAS.SetPermanentAffection(npc, 0)
    PacifyFianceOrSpouse(npc, false)
    string msg = "Me and " + TTM_Utils.GetActorName(npc) + " are now divorced."
    Debug.Notification(msg)
EndFunction

;/
  Marks an NPC as jilted and resets marriage quests.
  @param npc The NPC actor
/;
Function MakeNpcJilted(Actor npc) global
    Actor player = TTM_Data.GetPlayer()
    npc.SetRelationshipRank(player, -1)
    SetBrokeupTime(npc)
    npc.AddSpell(TTM_Data.GetBreakupCooldownSpell())
    MARAS.SetPermanentAffection(npc, 0)
    PacifyFianceOrSpouse(npc, false)
    string msg = "My engagement with " + TTM_Utils.GetActorName(npc) + " was called off."
    Debug.Notification(msg)
EndFunction

;/
  Marks an NPC as deceased
  @param npc The NPC actor
/;
Function MakeNpcDeceased(Actor npc, bool isPlayerKiller) global
    bool spouse = MARAS.IsNPCStatus(npc, "married")
    bool engaged = MARAS.IsNPCStatus(npc, "engaged")
    if(spouse || engaged)
        if(spouse)
            ;re-evaluate spouses bonuses on each spouse removed
            TTM_ServiceBuff.CalculatePermanentMultipliers()
            TTM_ServiceBuff.CalculateFollowerMultipliers()
        endif

        if(isPlayerKiller)
            TTM_Data.SetPlayerKiller(isPlayerKiller)
            SetKilledByPlayer(npc)
            TTM_ServiceSpouseAssets.StopShareHouseWithPlayer(npc, "deceased")
        endif
    endif
    ; re-check marriage related quests and stop if any were ongoing for killed npc
    TTM_ServiceMarriageQuest.ResetMarriageQuests(npc)

EndFunction

Function ShareIncome(Actor spouse) global
    float currentTime = Utility.GetCurrentGameTime()
    float lastTime = GetLastTimeSharedIncome(spouse)
    float diff = currentTime - lastTime

    if(lastTime == -1)
        diff = 1
    endif

    if(diff >= 1)
        int diffInt = diff as int
        TTM_Data.GetPlayer().AddItem(TTM_Data.GetGoldMisc(), PapyrusUtil.ClampInt(diffInt * 100, 0, 500))
        SetLastTimeSharedIncome(spouse)
    endif
EndFunction

Function AddDivorceFee(Quest divorceQuest) global
    float lastTimePayedDivorceFee = StorageUtil.GetFloatValue(none, "TTM_LastTimePayedDivorceFee", -1)
    float now = Utility.GetCurrentGameTime()

    if(lastTimePayedDivorceFee != -1 && now - lastTimePayedDivorceFee < 7)
        ; less than 7 days since last payment
        return
    endif

    Actor spouse = StorageUtil.GetFormValue(none, "TTM_DivorceLetter_Spouse") as Actor
    Actor player = TTM_Data.GetPlayer()
    Faction crimeFaction = spouse.GetCrimeFaction()


    if(crimeFaction == none)
        crimeFaction = TTM_Data.GetDefaultCrimeFaction()
    endif

    int playerWealth = PapyrusUtil.ClampInt((player.GetGoldAmount() * 0.3) as int, 0, 1000)
    int housesOwned = Game.QueryStat("Houses Owned") * 700
    int horsesOwned = Game.QueryStat("Horses Owned") * 500
    int questsCompleted = PapyrusUtil.ClampInt(Game.QueryStat("Quests Completed") * 100, 0, 1000)
    int dungeonsCleared = PapyrusUtil.ClampInt(Game.QueryStat("Dungeons Cleared") * 100, 0, 1000)

    int fee = 100 + playerWealth + housesOwned + horsesOwned + questsCompleted + dungeonsCleared

    crimeFaction.SetCrimeGold(fee)
    StorageUtil.SetFloatValue(none, "TTM_LastTimePayedDivorceFee", now)
    StorageUtil.SetFormValue(none, "TTM_DivorceLetter_Spouse", none)
    divorceQuest.Stop()

    TTM_Data.GetDivorceSettlementMsg().Show(fee)
EndFunction

;/ ==============================
   SECTION: trackedNpcs map
============================== /;
ObjectReference Function GetTrackedNpcHomeMarker(Actor npc) global
    if(StorageUtil.HasFormValue(npc, "HomeMarker"))
        return StorageUtil.GetFormValue(npc, "HomeMarker") as ObjectReference
    endif

    ObjectReference homeMarker = npc.PlaceAtMe(TTM_Data.GetHomeSandboxMarkerStatic(), 1, true)
    PO3_SKSEFunctions.SetLinkedRef(npc, homeMarker, TTM_Data.GetHomeSandboxKeyword())
    StorageUtil.SetFormValue(npc, "HomeMarker", homeMarker)
    return homeMarker
EndFunction

Function SetTrackedNpcHome(Actor npc, Location home) global
    StorageUtil.SetFormValue(npc, "HomeLocation", home)
EndFunction

Location Function GetTrackedNpcHome(Actor npc) global
    return StorageUtil.GetFormValue(npc, "HomeLocation") as Location
EndFunction


Function SetLastTimeSharedIncome(Actor npc) global
    StorageUtil.SetFloatValue(npc, "LastTimeSharedIncome", Utility.GetCurrentGameTime())
EndFunction

float Function GetLastTimeSharedIncome(Actor npc) global
    return StorageUtil.GetFloatValue(npc, "LastTimeSharedIncome", -1.0)
EndFunction

Function SetBrokeupTime(Actor npc) global
    StorageUtil.SetFloatValue(npc, "BrokeupTime", Utility.GetCurrentGameTime())
EndFunction

float Function GetBrokeupTime(Actor npc) global
    return StorageUtil.GetFloatValue(npc, "BrokeupTime", -1.0)
EndFunction

Function SetKilledByPlayer(Actor npc) global
    StorageUtil.SetIntValue(npc, "KilledByPlayer", 1)
EndFunction

bool Function GetKilledByPlayer(Actor npc) global
    return StorageUtil.GetIntValue(npc, "KilledByPlayer") == 1
EndFunction

; If player somehow engaged with hostile by default NPC, we need to pacify them
; If player broke up with fiance/spouse, we need to remove the pacify spell
Function PacifyFianceOrSpouse(Actor npc, bool add = true) global
    float aggr = npc.GetActorValue("Aggression")
    if(add)
        if(!npc.HasSpell(TTM_Data.GetPacifyFianceOrSpouseSpell()) && (aggr > 0 || aggr == -1))
            npc.AddSpell(TTM_Data.GetPacifyFianceOrSpouseSpell())
        endif
    else
        if(npc.HasSpell(TTM_Data.GetPacifyFianceOrSpouseSpell()))
            npc.RemoveSpell(TTM_Data.GetPacifyFianceOrSpouseSpell())
        endif
    endif
EndFunction
