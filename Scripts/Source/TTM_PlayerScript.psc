Scriptname TTM_PlayerScript extends ReferenceAlias

import PO3_Events_Form

Spell Property TTM_LoversRadianceAbility auto

Spell Property MarriageSleepAbility Auto
Spell Property BYOHAdoptionSleepAbilityMale Auto
Spell Property BYOHAdoptionSleepAbilityFemale Auto

; Scene[] Property TTM_NpcsScenes auto

Event OnInit()
    InitPlayer()
EndEvent

Event OnPlayerLoadGame()
    InitPlayer()
    TTM_MainController mainController = self.GetOwningQuest() as TTM_MainController
    mainController.Maintenance()
    TTM_ServiceChildrenLogic childrenLogicQst = TTM_Data.GetChildLogicQuest() as TTM_ServiceChildrenLogic
    childrenLogicQst.PlayerLocationChanged(Game.GetPlayer().GetCurrentLocation(), Game.GetPlayer().GetCurrentLocation(), true)
EndEvent

Function InitPlayer()
    RegisterForSleep()

    CheckCell()
EndFunction

Event OnSleepStop(bool abInterrupted)
    Actor player = TTM_Data.GetPlayer()
    Quest enablePolygamyQst = TTM_Data.GetMarasEnablePolygamyQuest()
    bool questDisabled = MARAS.GetDisablePolygamyQuest()
    bool questIsntTouched = !enablePolygamyQst.IsRunning() && !enablePolygamyQst.IsCompleted()

    if(!questDisabled && questIsntTouched && player.GetActorValue("DragonSouls") >= 1 &&  MARAS.GetStatusCount("married") == 1 && !MARAS.GetEnablePolygamy())
        enablePolygamyQst.SetStage(0)
    endif
EndEvent

Function CheckCell()
    TTM_ServiceSpouseAssets.CheckCell()
    TTM_ServicePlayerHouse.CheckPlayerHouseLocation()
EndFunction

Event OnLocationChange(Location akOldLoc, Location akNewLoc)
    string oldLocName = "None"
    if (akOldLoc)
        oldLocName = akOldLoc.GetName()
    endif
    string newLocName = "None"
    if (akNewLoc)
        newLocName = akNewLoc.GetName()
    endif
    TTM_Debug.debug("TTM_PlayerScript: OnLocationChange: '" + oldLocName + "' (" + akOldLoc + ") -> '" + newLocName + "' (" + akNewLoc + ")")
    CheckCell()
    TTM_ServiceChildrenLogic childrenLogicQst = TTM_Data.GetChildLogicQuest() as TTM_ServiceChildrenLogic
    childrenLogicQst.PlayerLocationChanged(akNewLoc, akOldLoc)
    TTM_QuestPartnerAway partnerAwayQst = TTM_Data.GetMarasPartnerAwayQuest() as TTM_QuestPartnerAway
    bool handledRendezvous = false
    if (partnerAwayQst)
        handledRendezvous = partnerAwayQst.CheckPlayerArrival(akNewLoc)
    endif
    TTM_NpcsScenes npcScenes = TTM_Data.GetMarasNpcsScenesQuest() as TTM_NpcsScenes
    bool handledHomeArrival = false
    if (!handledRendezvous)
        handledHomeArrival = MARAS.EvaluatePartnerHomeArrival(akNewLoc)
    endif
    TTM_Debug.debug("TTM_PlayerScript: OnLocationChange results for '" + newLocName + "': handledRendezvous=" + handledRendezvous + ", handledHomeArrival=" + handledHomeArrival)
    if (!handledRendezvous && !handledHomeArrival && MARAS.IsLocationAllowedForScenes(akNewLoc))
        TTM_Debug.debug("TTM_PlayerScript: OnLocationChange proceeding to regular SetupSceneOnLocationChange for '" + newLocName + "'")
        npcScenes.SetupSceneOnLocationChange()
    endif

    npcScenes.RestoreMovedNpcsOnLocationChange()
EndEvent
