;/
  TTM_MainController.psc

  Main quest controller for the MARAS mod.

  Main responsibilities:
    - Initializes and maintains core MARAS systems
    - Handles mod events for relationship changes and AI commands
    - Integrates with SkyrimNet and TTLL if present
    - Triggers maintenance for buffs, quest tracking, and conditions
/;
Scriptname TTM_MainController extends Quest

SPELL Property TTM_Debug_ToggleSpouse  Auto

;/
  OnInit event: Called when the quest initializes. Triggers maintenance.
/;
Event OnInit()
    Maintenance()
EndEvent

;/
  Main maintenance function. Imports static data, checks for integrations, and triggers maintenance for all subsystems.
/;
Function Maintenance()

    TTM_Debug.CleanOnLoad()
    TTM_JData.ImportStaticData()
    Quest _self = self as Quest
    TTM_QuestTracker questTracker = _self as TTM_QuestTracker
    TTM_Conditions conditions = _self as TTM_Conditions
    TTM_ServiceIntimateMoments intimateMoments = _self as TTM_ServiceIntimateMoments


    ; check if SkyrimNet present
    ; otherwise all SkyrimNet related logic will be bypassed
    bool hasSkyrimNet = false
    int hasSkyrimNetGlobal = 0
    if(Game.GetModByName("SkyrimNet.esp") != 255)
        hasSkyrimNet = true
        hasSkyrimNetGlobal = 1
        RegisterForModEvent("SkyrimNet_OnPackageRemoved", "OnPackageRemoved")
    endif

    TTM_JData.SetHasSkyrimNet(hasSkyrimNet)
    TTM_JData.GetSetHasSkyrimNetGlobal(hasSkyrimNetGlobal)


    TTM_JData.SetHasTTRF(Game.GetModByName("TT_RelationsFinder.esp") != 255)

    RegisterForModEvent("TTM_SpouseRelationshipChanged", "OnRelationshipChanged")
    RegisterForModEvent("TTM_ChangeLeadSpouseRankEvent", "OnChangeHierarchyRank")
    RegisterForModEvent("TTM_SpouseAffectionChanged", "OnSpouseAffectionChanged")

     ; ensure player has debug spell and check door perk

    Actor player = TTM_JData.GetPlayer()

    bool added = player.AddSpell(TTM_Debug_ToggleSpouse)

    Perk checkDoorPerk = TTM_JData.GetCheckDoorPerk()

    if(!player.hasPerk(checkDoorPerk))
        player.addPerk(checkDoorPerk)
    endif

    TTM_ServiceMarriageQuest.CheckOngoingMarriage()
    TTM_ServiceSpouseTypes.Maintenance()
    TTM_ServiceBuff.Maintenance()
    questTracker.Maintenance()
    conditions.Maintenance()
    intimateMoments.Maintenance()
    TTM_ServiceSkyrimNet.Maintenance()

    RegisterForMenu("Dialogue Menu")
    RegisterForMenu("GiftMenu")

    ; JValue.enableAPILog(TTM_MCM_State.GetLogLevel() == 0)
    JValue.enableAPILog(false)
EndFunction

;/
  Handles relationship change events. Updates tracked NPCs and their status, and syncs with CHIM if enabled.
  @param npc    The NPC whose relationship changed
  @param status The new relationship status (candidate, engaged, married, jilted, divorced)
/;
Event OnRelationshipChanged(Form npc, string status)
    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("MainController:OnRelationshipChanged: " + TTM_Utils.GetActorName(npc as Actor) + "; status: " + status)
    endif
    Actor npcA = npc as Actor
    string npcName = TTM_Utils.GetActorName(npcA)

    TTM_ServiceRelationships.AddTrackedNpc(npcA)

    string msg = ""

    if(status == "candidate")
        msg = "I think " + npcName + " is a good match."
        TTM_ServiceRelationships.MakeNpcCandidate(npcA)
    elseif(status == "engaged")
        msg = npcName + " and I are engaged to be married."
        TTM_ServiceRelationships.MakeNpcEngaged(npcA)
    elseif(status == "married")
        msg = npcName + " and I are now newlyweds."
        TTM_ServiceRelationships.MakeNpcMarried(npcA)
    elseif(status == "jilted")
        msg = "My engagement with " + npcName + " was called off."
        TTM_ServiceRelationships.MakeNpcJilted(npcA)
    elseif(status == "divorced")
        msg = "Me and " + npcName + " are now divorced."
        TTM_ServiceRelationships.MakeNpcDivorced(npcA)
    endif

    Debug.Notification(msg)

    TTM_ServiceRelationships.ManageFactions(npcA, status)
EndEvent


Event OnChangeHierarchyRank(Form spouse, int newRank, int oldRank)
    Actor spouseA = spouse as Actor
    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("MainController:OnChangeHierarchyRank: " + TTM_Utils.GetActorName(spouseA) + "; newRank: " + newRank + "; oldRank: " + oldRank)
    endif
    if(newRank == -1)
        newRank = 4
    endif
    if(oldRank == -1)
        oldRank = 4
    endif
    int rankDiff = -1 * (newRank - oldRank)
    if(rankDiff == 0)
        return
    endif
    if(rankDiff > 0)
        Debug.Notification("I made my spouse " + TTM_Utils.GetActorName(spouseA) + " a higher rank in our relationship hierarchy.")
    else
        Debug.Notification("I made my spouse " + TTM_Utils.GetActorName(spouseA) + " a lower rank in our relationship hierarchy.")
    endif

    TTM_ServiceSkyrimNet.RegisterPromotionEvent(spouseA, rankDiff < 0)
    TTM_ServiceAffection.AddPromotionAffection(spouseA, rankDiff)
    if(rankDiff < 0)
        spouseA.AddSpell(TTM_JData.GetDemotedCooldownSpell())
    endif
EndEvent

Event OnSpouseAffectionChanged(Form spouse, string level, bool up)
    Actor spouseA = spouse as Actor
    string spouseName = TTM_Utils.GetActorName(spouseA)
    string msg = ""

    if(up)
        if(level == "happy")
            msg = "I feel closer to " + spouseName + "."
        elseif(level == "content")
            msg = "Things are improving with " + spouseName + "."
        elseif(level == "troubled")
            msg = "My tension with " + spouseName + " is easing."
        endif

        Quest affectionEstrangedDivorce = TTM_JData.GetMarasAffectionEstrangedDivorceQuest()
        if(affectionEstrangedDivorce.IsRunning())
            Actor questSpouse = TTM_Utils.GetActorAlias(affectionEstrangedDivorce, "Spouse")
            if(questSpouse == spouseA)
                affectionEstrangedDivorce.SetStage(150)
            endif
        endif
    else
        if(level == "content")
            msg = "I'm starting to feel distant from " + spouseName + "."
        elseif(level == "troubled")
            msg = "I'm growing distant from " + spouseName + "."
            ; when affection drops to troubled, stop sharing home with player
            TTM_ServiceSpouseAssets.StopShareHomeWithPlayer(spouseA)
        elseif(level == "estranged")
            msg = "My relationship with " + spouseName + " has soured."
            ; when affection drops to estranged, stop sharing home with player and stop using player's home
            TTM_ServiceSpouseAssets.StopShareHomeWithPlayer(spouseA)
            TTM_ServicePlayerHouse.ReleaseSpouseFromPlayerHome(spouseA)
        endif
    endif

    Debug.Notification(msg)
EndEvent

Event OnMenuOpen(string menuName)
    if(menuName == "Dialogue Menu")
        Actor player = TTM_JData.GetPlayer()
        Actor[] actors = MiscUtil.ScanCellNPCs(player, 200)
        int i = 0
        while(i < actors.Length)
            Actor akActor = actors[i]
            if(TTM_Utils.IsTracking(akActor) && akActor.IsInDialogueWithPlayer())
                OnStartedDialogue(akActor)
            endif
            i += 1
        endwhile
    elseif(menuName == "GiftMenu")
        TTM_ServiceGift.OnGiftMenuOpen()
    endif
endEvent


Event OnMenuClose(string menuName)
    if(menuName == "GiftMenu")
        TTM_ServiceGift.OnGiftMenuClose()
    endif
endEvent

Function OnStartedDialogue(Actor npc)
    TTM_ServiceAffection.AddDialogueStartedAffection(npc)
EndFunction
