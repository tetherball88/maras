;/
  TTM_MainController.psc

  Main quest controller for the MARAS mod.

  Main responsibilities:
    - Initializes and maintains core MARAS systems
    - Handles mod events for relationship changes and AI commands
    - Integrates with SkyrimNet and TTLL if present
    - Triggers maintenance for buffs, quest tracking, and conditions

  Dependencies:
    - TTM_JData
    - TTM_ServiceBuff
    - TTM_QuestTracker
    - TTM_Conditions
    - TTM_ServiceSkyrimNet
    - TTM_ServiceNpcs
    - TTM_Utils
    - AIAgentFunctions
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
    TTM_Debug.SetupLogger()
    TTM_JData.ImportStaticData()
    Quest _self = self as Quest
    TTM_QuestTracker questTracker = _self as TTM_QuestTracker
    TTM_Conditions conditions = _self as TTM_Conditions


    ; check if SkyrimNet present
    ; otherwise all SkyrimNet related logic will be bypassed
    if(Game.GetModByName("SkyrimNet.esp") != 255)
        TTM_JData.SetHasSkyrimNet()
        TTM_JData.GetSetHasSkyrimNetGlobal(1)
        RegisterForModEvent("SkyrimNet_OnPackageRemoved", "OnPackageRemoved")
    endif

    if(Game.GetModByName("TT_LoversLedger.esp") != 255)
        TTM_JData.SetHasTTLL()
    endif

    if(Game.GetModByName("TT_RelationsFinder.esp") != 255)
        TTM_JData.SetHasTTRF()
    endif

    RegisterForModEvent("TTM_SpouseRelationshipChanged", "OnRelationshipChanged")
    RegisterForModEvent("TTM_ChangeLeadSpouseRankEvent", "OnChangeHierarchyRank")

    Actor player = TTM_JData.GetPlayer()

    player.AddSpell(TTM_Debug_ToggleSpouse)

    Perk checkDoorPerk = TTM_JData.GetCheckDoorPerk()

    if(!player.hasPerk(checkDoorPerk))
        player.addPerk(checkDoorPerk)
    endif

    TTM_ServiceMarriageQuest.CheckOngoingMarriage()
    TTM_ServiceSpouseTypes.Maintenance()
    TTM_ServiceBuff.Maintenance()
    questTracker.Maintenance()
    conditions.Maintenance()
    TTM_ServiceSkyrimNet.Maintenance()

    ; patch for ostim removing PapyrusUtils override packages
    RegisterForModEvent("ostim_thread_end", "OStimEnd")
EndFunction

;/
  Handles relationship change events. Updates tracked NPCs and their status, and syncs with CHIM if enabled.
  @param npc    The NPC whose relationship changed
  @param status The new relationship status (candidate, engaged, married, jilted, divorced)
/;
Event OnRelationshipChanged(Form npc, string status)
    TTM_Debug.trace("MainController:OnRelationshipChanged: " + npc + "; status: " + status)
    Actor npcA = npc as Actor

    TTM_ServiceNpcs.AddTrackedNpc(npcA)

    if(status == "candidate")
        TTM_ServiceNpcs.MakeNpcCandidate(npcA)
    elseif(status == "engaged")
        TTM_ServiceNpcs.MakeNpcEngaged(npcA)
    elseif(status == "married")
        TTM_ServiceNpcs.MakeNpcMarried(npcA)
    elseif(status == "jilted")
        TTM_ServiceNpcs.MakeNpcJilted(npcA)
    elseif(status == "divorced")
        TTM_ServiceNpcs.MakeNpcDivorced(npcA)
    endif

    TTM_ServiceNpcs.ManageFactions(npcA, status)
EndEvent

Event OStimEnd(string eventName, string json, float numArg, Form sender)
    Actor[] Actors = OJSON.GetActors(Json)
    Faction housedFaction = TTM_JData.GetSpouseHousedFaction()
    Package spousePlayerHomeSandbox = TTM_JData.GetHomeSandboxPackage()

    int i = 0
    while(i < actors.Length)
        Actor akActor = actors[i]

        if(akActor.IsInFaction(housedFaction) && akActor.GetCurrentPackage() != spousePlayerHomeSandbox)
            ActorUtil.RemovePackageOverride(akActor, spousePlayerHomeSandbox)
            ActorUtil.AddPackageOverride(akActor, spousePlayerHomeSandbox, 5)
            akActor.EvaluatePackage()
        endif

        i += 1
    endwhile
EndEvent

Event OnChangeHierarchyRank(Form spouse, int newRank, int oldRank)
    Actor spouseA = spouse as Actor
    TTM_Debug.trace("MainController:OnChangeHierarchyRank: " + TTM_Utils.GetActorName(spouseA) + "; newRank: " + newRank + "; oldRank: " + oldRank)
    bool isDemoted = newRank == -1 || newRank > oldRank
    if(isDemoted)
        spouseA.AddSpell(TTM_JData.GetDemotedCooldownSpell())
        TTM_ServiceSkyrimNet.RegisterDemotedEvent(spouseA, newRank, oldRank)
    else
        TTM_ServiceSkyrimNet.RegisterPromotedEvent(spouseA, newRank, oldRank)
    endif
EndEvent
