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
    TTM_ServiceAffection.Maintenance()
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

    TTM_Data.SetHasSkyrimNet(hasSkyrimNet)
    TTM_Data.GetSetHasSkyrimNetGlobal(hasSkyrimNetGlobal)


    TTM_Data.SetHasTTRF(Game.GetModByName("TT_RelationsFinder.esp") != 255)

    RegisterForModEvent("maras_status_changed", "OnRelationshipChanged")
    RegisterForModEvent("maras_hierarchy_changed", "OnChangeHierarchyRank")
    RegisterForModEvent("maras_change_affection", "OnSpouseAffectionChanged")
    RegisterForModEvent("maras_teammate_change", "OnTeammateChange")

     ; ensure player has debug spell and check door perk

    Actor player = TTM_Data.GetPlayer()

    TTM_ServiceMarriageQuest.CheckOngoingMarriage()
    TTM_ServiceBuff.Maintenance()
    questTracker.Maintenance()
    conditions.Maintenance()
    intimateMoments.Maintenance()
    TTM_ServiceSkyrimNet.Maintenance()

    RegisterForMenu("Dialogue Menu")
    RegisterForMenu("GiftMenu")

    Quest enablePolygamyQst = TTM_Data.GetMarasEnablePolygamyQuest()
    if(enablePolygamyQst.IsCompleted())
        Quest mcmQ = self as Quest
        TTM_MCM mcm = mcmQ as TTM_MCM
        mcm.TTM_EnablePolygamyToggle.SetValue(1)
    endif
EndFunction

;/
  Handles relationship change events. Updates tracked NPCs and their status, and syncs with CHIM if enabled.
  @param npc    The NPC whose relationship changed
  @param status The new relationship status (candidate, engaged, married, jilted, divorced)
  @param statusEnum The enum value of the new status
/;
Event OnRelationshipChanged(String EventName, String status, Float statusEnum, Form npc)
    Actor npcA = npc as Actor
    string npcName = TTM_Utils.GetActorName(npcA)
    TTM_Debug.debug("MainController:OnRelationshipChanged: " + npcName + "; status: " + status)

    if(status == "candidate")
        TTM_ServiceRelationships.MakeNpcCandidate(npcA)
    elseif(status == "engaged")
        TTM_ServiceRelationships.MakeNpcEngaged(npcA)
    elseif(status == "married")
        TTM_ServiceRelationships.MakeNpcMarried(npcA)
    elseif(status == "jilted")
        TTM_ServiceRelationships.MakeNpcJilted(npcA)
    elseif(status == "divorced")
        TTM_ServiceRelationships.MakeNpcDivorced(npcA)
    endif
EndEvent


Event OnChangeHierarchyRank(String EventName, String promoteDemote, Float rankDiff, Form spouse)
    Actor spouseA = spouse as Actor
    string spouseName = TTM_Utils.GetActorName(spouseA)
    TTM_Debug.debug("MainController:OnChangeHierarchyRank:"+promoteDemote+" " + spouseName + ";  rankDiff: " + rankDiff)
    if(rankDiff == 0)
        return
    endif
    if(rankDiff > 0)
        Debug.Notification("I made my spouse " + spouseName + " a higher rank in our relationship hierarchy.")
    else
        Debug.Notification("I made my spouse " + spouseName + " a lower rank in our relationship hierarchy.")
    endif

    TTM_ServiceSkyrimNet.RegisterPromotionEvent(spouseA, rankDiff < 0)
    TTM_ServiceAffection.AddPromotionAffection(spouseA, rankDiff)
    if(rankDiff < 0)
        spouseA.AddSpell(TTM_Data.GetDemotedCooldownSpell())
    endif
EndEvent

Event OnSpouseAffectionChanged(String EventName, String level, Float affectionDiff, Form spouse)
    Actor spouseA = spouse as Actor
    string spouseName = TTM_Utils.GetActorName(spouseA)
    string msg = ""

    if(affectionDiff > 0)
        if(level == "happy")
            msg = "I feel closer to " + spouseName + "."
        elseif(level == "content")
            msg = "Things are improving with " + spouseName + "."
        elseif(level == "troubled")
            msg = "My tension with " + spouseName + " is easing."
        endif

        ; if affection level restored complete affection estranged divorce quest
        if(level == "content" || level == "happy")
            Quest affectionEstrangedDivorce = TTM_Data.GetMarasAffectionEstrangedDivorceQuest()
            if(affectionEstrangedDivorce.IsRunning())
                Actor questSpouse = TTM_Utils.GetActorAlias(affectionEstrangedDivorce, "Spouse")
                if(questSpouse == spouseA)
                    affectionEstrangedDivorce.SetStage(150)
                endif
            endif
        endif
    elseif(affectionDiff < 0)
        if(level == "content")
            msg = "I'm starting to feel distant from " + spouseName + "."
        elseif(level == "troubled")
            msg = "I'm growing distant from " + spouseName + "."
            ; when affection drops to troubled, stop sharing home with player
            TTM_ServiceSpouseAssets.StopShareHouseWithPlayer(spouseA, "affection")
        elseif(level == "estranged")
            msg = "My relationship with " + spouseName + " has soured."
            ; when affection drops to estranged, stop sharing home with player and stop using player's home
            TTM_ServiceSpouseAssets.StopShareHouseWithPlayer(spouseA, "affection")
            TTM_ServicePlayerHouse.ReleaseSpouseFromPlayerHome(spouseA, "affection")
            ; TODO check that quest isn't running already
            TTM_Data.GetAffectionQuestKeyword("estranged").SendStoryEvent()
        endif
    endif

    Debug.Notification(msg)
EndEvent

Event OnTeammateChange(String eventName, string strArg, float fltArg, Form sender)
    TTM_ServiceBuff.CalculateFollowerMultipliers()
EndEvent

Event OnMenuOpen(string menuName)
    if(menuName == "Dialogue Menu")
        Actor player = TTM_Data.GetPlayer()
        ObjectReference ref = Game.GetCurrentCrosshairRef()
        Actor akActor = ref as Actor
        TTM_Debug.debug("MainController:OnMenuOpen: Dialogue Menu with " + TTM_Utils.GetActorName(akActor))
        if(MARAS.IsNPCStatus(akActor, "any") && akActor.IsInDialogueWithPlayer())
            OnStartedDialogue(akActor)
        endif
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
