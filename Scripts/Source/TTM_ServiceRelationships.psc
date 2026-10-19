scriptname TTM_ServiceRelationships

;/
  Marks an NPC as a candidate and adds to the candidate bucket.
  @param npc The NPC actor
/;
Function MakeNpcCandidate(Actor npc) global
    int playerRank = TTM_Data.GetPlayer().GetRelationshipRank(npc)
    MARAS.SetPermanentAffection(npc, 50 + (playerRank * 10))
    TTM_Messages.RegisterCandidateMsg(TTM_Utils.GetActorName(npc))
EndFunction

;/
  Marks an NPC as dating.
  @param npc The NPC actor
/;
Function MakeNpcDating(Actor npc) global
    Actor player = TTM_Data.GetPlayer()
    if(npc.GetRelationshipRank(player) < 3)
        npc.SetRelationshipRank(player, 3)
    endif
    TTM_Messages.DatingMsg(TTM_Utils.GetActorName(npc))
    if(MARAS.GetPermanentAffection(npc) < 80)
        MARAS.SetPermanentAffection(npc, 80)
    endif
EndFunction

;/
  Marks an NPC as a lover.
  @param npc The NPC actor
/;
Function MakeNpcLover(Actor npc) global
    Actor player = TTM_Data.GetPlayer()
    if(npc.GetRelationshipRank(player) < 3)
        npc.SetRelationshipRank(player, 3)
    endif
    TTM_Messages.LoverMsg(TTM_Utils.GetActorName(npc))
    if(MARAS.GetPermanentAffection(npc) < 70)
        MARAS.SetPermanentAffection(npc, 70)
    endif
EndFunction

;/
  Marks an NPC as engaged and adds to the fiances bucket.
  @param npc The NPC actor
/;
Function MakeNpcEngaged(Actor npc) global
    bool alwaysSkipWeddings = MARAS.GetSettingBool("skipWedding")
    int answerSkipWedding = -1
    if(TTM_Data.GetPlayerHadWedding() && !alwaysSkipWeddings && StorageUtil.GetIntValue(none, "TTM_DontShowSkipWeddingMessage", 0) == 0)
        answerSkipWedding = MARAS.ShowSkipWeddingModal(npc)
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
    TTM_Messages.EngagementMsg(TTM_Utils.GetActorName(npc))
    MARAS.SetPermanentAffection(npc, 100)

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

    TTM_Messages.MarriedMsg(TTM_Utils.GetActorName(npc))
    MARAS.SetPermanentAffection(npc, 100)

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
    TTM_Messages.DivorceMsg(TTM_Utils.GetActorName(npc))
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
    TTM_Messages.JiltedMsg(TTM_Utils.GetActorName(npc))
EndFunction

;/
  Marks an NPC as ex-dating, resets affection and cooldown.
  @param npc The NPC actor
/;
Function MakeNpcExDating(Actor npc) global
    Actor player = TTM_Data.GetPlayer()
    npc.SetRelationshipRank(player, -1)
    if(npc.IsInFaction(TTM_Data.GetSpouseCheatingFaction()))
        SetBrokeupTime(npc)
        npc.AddSpell(TTM_Data.GetBreakupCooldownSpell())
    endif
    MARAS.SetPermanentAffection(npc, 0)
    PacifyFianceOrSpouse(npc, false)
    TTM_Messages.ExDatingMsg(TTM_Utils.GetActorName(npc))
EndFunction

;/
  Marks an NPC as ex-lover, resets affection and cooldown.
  @param npc The NPC actor
/;
Function MakeNpcExLover(Actor npc) global
    Actor player = TTM_Data.GetPlayer()
    npc.SetRelationshipRank(player, -1)
    if(npc.IsInFaction(TTM_Data.GetSpouseCheatingFaction()))
        SetBrokeupTime(npc)
        npc.AddSpell(TTM_Data.GetBreakupCooldownSpell())
    endif
    MARAS.SetPermanentAffection(npc, 0)
    PacifyFianceOrSpouse(npc, false)
    TTM_Messages.ExLoverMsg(TTM_Utils.GetActorName(npc))
EndFunction

;/
  Handles cleanup when an NPC spouse or fiancé dies.
  @param npc The deceased NPC actor
  @param isPlayerKiller True if the player was the killer
/;
Function HandleSpouseDeath(Actor npc, bool isPlayerKiller) global
    if(!npc)
        return
    endif

    Actor player = TTM_Data.GetPlayer()
    bool spouse = MARAS.IsNPCStatus(npc, "married") || MARAS.WasMarriedTo(npc, player)
    bool engaged = MARAS.IsNPCStatus(npc, "engaged") || MARAS.WasEngagedTo(npc, player)

    if(spouse || engaged)
        if(spouse)
            ; re-evaluate spouse bonuses on each spouse removed
            TTM_ServiceBuff.CalculatePermanentMultipliers()
            TTM_ServiceBuff.CalculateFollowerMultipliers()
        endif

        if(isPlayerKiller)
            TTM_Data.SetPlayerKiller(isPlayerKiller)
            SetKilledByPlayer(npc)
            TTM_ServiceSpouseAssets.StopShareHouseWithPlayer(npc, "deceased")
        endif
    endif

    MARAS.UnregisterNPC(npc)
    ; re-check marriage related quests and stop if any were ongoing for killed npc
    TTM_ServiceMarriageQuest.ResetMarriageQuests(npc)
EndFunction

;/
  Marks an NPC as deceased
  @param npc The NPC actor
/;
Function MakeNpcDeceased(Actor npc, bool isPlayerKiller) global
    HandleSpouseDeath(npc, isPlayerKiller)
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
    Faction crimeFaction = none
    if(spouse != none)
        crimeFaction = spouse.GetCrimeFaction()
    endif

    if(crimeFaction == none)
        crimeFaction = TTM_Data.GetDefaultCrimeFaction()
    endif

    int playerWealth = PapyrusUtil.ClampInt((player.GetGoldAmount() * 0.3) as int, 0, 1000)
    int housesOwned = Game.QueryStat("Houses Owned") * 700
    int horsesOwned = Game.QueryStat("Horses Owned") * 500
    int questsCompleted = PapyrusUtil.ClampInt(Game.QueryStat("Quests Completed") * 100, 0, 1000)
    int dungeonsCleared = PapyrusUtil.ClampInt(Game.QueryStat("Dungeons Cleared") * 100, 0, 1000)

    int assetsShare = housesOwned + horsesOwned + questsCompleted + dungeonsCleared
    int fee = 100 + playerWealth + assetsShare
    bool canPayDirectly = player.GetGoldAmount() >= fee

    StorageUtil.SetFloatValue(none, "TTM_LastTimePayedDivorceFee", now)
    StorageUtil.SetFormValue(none, "TTM_DivorceLetter_Spouse", none)
    divorceQuest.Stop()

    int choice = MARAS.ShowDivorceSettlementModal(spouse, fee, playerWealth, assetsShare, canPayDirectly)
    if(choice == 0 && canPayDirectly)
        player.RemoveItem(TTM_Data.GetGoldMisc(), fee)
    else
        crimeFaction.SetCrimeGold(fee)
    endif
EndFunction

;/ ==============================
   SECTION: trackedNpcs map
============================== /;
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
