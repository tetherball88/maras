scriptname TTM_ServiceRelationships

;/
  Marks an NPC as a candidate and adds to the candidate bucket.
  @param npc The NPC actor
/;
Function MakeNpcCandidate(Actor npc) global
EndFunction

;/
  Marks an NPC as engaged and adds to the fiances bucket.
  @param npc The NPC actor
/;
Function MakeNpcEngaged(Actor npc) global
    bool alwaysSkipWeddings = TTM_JData.GetSkipWedding()
    bool answerSkipWedding
    if(TTM_JData.GetPlayerHadWedding() && !alwaysSkipWeddings)
        answerSkipWedding = TTM_Utils.ShowMessageMessage("Do you want skip Wedding Ceremony next time you get married? You can set in MCM to always skip weddings.")
    endif
    if(alwaysSkipWeddings || answerSkipWedding)
        TTM_Utils.FadeToBlack()
        Debug.Notification("Congratulations! You and " + TTM_Utils.GetActorName(npc) + " got married!")
        TTM_ServiceSkyrimNet.SimulatePostWeddingIfSkippedBehavior(npc)
        return
    endif
    Actor player = TTM_JData.GetPlayer()
    npc.SetRelationshipRank(player, 3)
    TTM_ServiceMarriageQuest.StartEngagement(npc)
    if(npc.HasSpell(TTM_JData.GetBreakdownCooldownSpell()))
        npc.RemoveSpell(TTM_JData.GetBreakdownCooldownSpell())
    endif
    TTM_ServiceAffection.SetAffectionRank(npc, 100)
EndFunction

;/
  Marks an NPC as married, triggers wedding logic.
  @param npc The NPC actor
/;
Function MakeNpcMarried(Actor npc) global
    Actor player = TTM_JData.GetPlayer()

    ; TTM_ServiceHierarchy.AddLeadSpouse(npc)

    npc.SetRelationshipRank(player, 4)
    ;re-evaluate spouses bonuses on each spouse added
    TTM_ServiceBuff.CalculatePermanentMultipliers()
    TTM_ServiceBuff.CalculateFollowerMultipliers()

    ; it will check if npc who becomes spouse actually is current fiance with wedding about to happen
    TTM_ServiceMarriageQuest.SkipWedding(npc)

    TTM_ServiceSpouseAssets.FindSpouseHome(npc)
    if(npc.HasSpell(TTM_JData.GetBreakdownCooldownSpell()))
        npc.RemoveSpell(TTM_JData.GetBreakdownCooldownSpell())
    endif
    TTM_ServiceAffection.SetAffectionRank(npc, 100)
EndFunction

;/
  Marks an NPC as divorced, removes from spouse lists.
  @param npc The NPC actor
/;
Function MakeNpcDivorced(Actor npc) global
    Actor player = TTM_JData.GetPlayer()
    Faction playerFaction = TTM_JData.GetPlayerFaction()

    ; TTM_ServiceHierarchy.RemoveLeadSpouse(npc)
    npc.SetRelationshipRank(player, -2)
    ;re-evaluate spouses bonuses on each spouse removed
    TTM_ServiceBuff.CalculatePermanentMultipliers()
    TTM_ServiceBuff.CalculateFollowerMultipliers()

    TTM_ServiceSpouseAssets.StopShareHomeWithPlayer(npc)
    TTM_ServicePlayerHouse.ReleaseSpouseFromPlayerHome(npc)
    SetBrokeupTime(npc)
    npc.AddSpell(TTM_JData.GetBreakdownCooldownSpell())
    TTM_ServiceAffection.SetAffectionRank(npc, 0)
EndFunction

;/
  Marks an NPC as jilted and resets marriage quests.
  @param npc The NPC actor
/;
Function MakeNpcJilted(Actor npc) global
    Actor player = TTM_JData.GetPlayer()
    npc.SetRelationshipRank(player, -1)
    SetBrokeupTime(npc)
    npc.AddSpell(TTM_JData.GetBreakdownCooldownSpell())
    TTM_ServiceAffection.SetAffectionRank(npc, 100)
EndFunction

;/
  Marks an NPC as deceased
  @param npc The NPC actor
/;
Function MakeNpcDeceased(Actor npc, bool isPlayerKiller) global
    bool spouse = TTM_Utils.IsSpouse(npc)
    bool engaged = TTM_Utils.IsFiance(npc)
    if(spouse || engaged)
        if(spouse)
            ; TTM_ServiceHierarchy.RemoveLeadSpouse(npc)
            ;re-evaluate spouses bonuses on each spouse removed
            TTM_ServiceBuff.CalculatePermanentMultipliers()
            TTM_ServiceBuff.CalculateFollowerMultipliers()
        endif

        if(isPlayerKiller)
            TTM_JData.SetPlayerKiller(isPlayerKiller)
            SetKilledByPlayer(npc)
            TTM_ServiceSpouseAssets.StopShareHomeWithPlayer(npc)
        endif
    endif
    ; re-check marriage related quests and stop if any were ongoing for killed npc
    TTM_ServiceMarriageQuest.ResetMarriageQuests(npc)

EndFunction

; Manage TTM utility factions related to marriage status
Function ManageFactions(Actor npc, string status) global
    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("ManageFactions:npc:"+TTM_Utils.GetActorName(npc)+":"+status)
    endif
    TTM_Utils.SetRelationshipStatus(npc, status)
    if(status == "candidate")
        npc.AddToFaction(TTM_JData.GetMarriagePotentialFaction())
    elseif(status == "engaged")
        npc.AddToFaction(TTM_JData.GetMarriageAskedFaction())
        npc.AddToFaction(TTM_JData.GetCourtingFaction())
    elseif(status == "married")
        npc.RemoveFromFaction(TTM_JData.GetMarriageAskedFaction())
        npc.RemoveFromFaction(TTM_JData.GetCourtingFaction())
        ; double check if hirelings are still can be followers after marriage
        npc.RemoveFromFaction(TTM_JData.GetPotentialHirelingFaction())

        npc.AddToFaction(TTM_JData.GetMarriedFaction())
        npc.AddToFaction(TTM_JData.GetPlayerFaction())
        npc.AddToFaction(TTM_JData.GetPlayerBedOwnershipFaction())
    elseif(status == "jilted")
        npc.RemoveFromFaction(TTM_JData.GetMarriageAskedFaction())
        npc.RemoveFromFaction(TTM_JData.GetCourtingFaction())
    elseif(status == "divorced")
        npc.RemoveFromFaction(TTM_JData.GetMarriedFaction())
        npc.RemoveFromFaction(TTM_JData.GetPlayerFaction())
        npc.RemoveFromFaction(TTM_JData.GetPlayerBedOwnershipFaction())
    endif
EndFunction

; Dialogue line has 24 hours cooldown so will assume if it is from dialogue it already passed at least one day
Function ShareIncome(Actor spouse, bool fromDialogue = false) global
    float currentTime = Utility.GetCurrentGameTime()
    float lastTime = GetLastTimeSharedIncome(spouse)
    float diff = currentTime - lastTime

    if(lastTime == -1 || fromDialogue)
        diff = 1
    endif

    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("ShareIncome:current:"+currentTime+":diff:"+diff+":"+lastTime)
    endif

    if(diff >= 1)
        int diffInt = diff as int
        TTM_JData.GetPlayer().AddItem(TTM_JData.GetGoldMisc(), diffInt * 100)
        SetLastTimeSharedIncome(spouse)
    endif
EndFunction

;/ ==============================
   SECTION: trackedNpcs map
============================== /;
ObjectReference Function GetTrackedNpcHomeMarker(Actor npc) global
    if(TTM_JMethods.HasFormValue(npc, "HomeMarker"))
        return TTM_JMethods.GetFormValue(npc, "HomeMarker") as ObjectReference
    endif

    ObjectReference homeMarker = npc.PlaceAtMe(TTM_JData.GetHomeSandboxMarkerStatic(), 1, true)
    PO3_SKSEFunctions.SetLinkedRef(npc, homeMarker, TTM_JData.GetHomeSandboxKeyword())
    TTM_JMethods.SetFormValue(npc, "HomeMarker", homeMarker)
    return homeMarker
EndFunction

Function SetTrackedNpcHome(Actor npc, Location home) global
    TTM_JMethods.SetFormValue(npc, "HomeLocation", home)
EndFunction

Location Function GetTrackedNpcHome(Actor npc) global
    return TTM_JMethods.GetFormValue(npc, "HomeLocation") as Location
EndFunction

Function SetTrackedNpcMcmTypeChanged(Actor npc) global
    TTM_JMethods.SetIntValue(npc, "McmTypeChanged", 1)
EndFunction

bool Function GetTrackedNpcMcmTypeChanged(Actor npc) global
    return TTM_JMethods.GetIntValue(npc, "McmTypeChanged") == 1
EndFunction

Function SetLastTimeSharedIncome(Actor npc) global
    TTM_JMethods.SetFloatValue(npc, "LastTimeSharedIncome", Utility.GetCurrentGameTime())
EndFunction

float Function GetLastTimeSharedIncome(Actor npc) global
    return TTM_JMethods.GetFloatValue(npc, "LastTimeSharedIncome", -1.0)
EndFunction

Function SetBrokeupTime(Actor npc) global
    TTM_JMethods.SetFloatValue(npc, "BrokeupTime", Utility.GetCurrentGameTime())
EndFunction

float Function GetBrokeupTime(Actor npc) global
    return TTM_JMethods.GetFloatValue(npc, "BrokeupTime", -1.0)
EndFunction

Function SetKilledByPlayer(Actor npc) global
    TTM_JMethods.SetIntValue(npc, "KilledByPlayer", 1)
EndFunction

bool Function GetKilledByPlayer(Actor npc) global
    return TTM_JMethods.GetIntValue(npc, "KilledByPlayer") == 1
EndFunction

Function CountLoveInterests() global
    int count = MARAS.GetStatusCount("married") + MARAS.GetStatusCount("engaged")
    TTM_JData.GetSetSpouseCountGlobal(count)
EndFunction
