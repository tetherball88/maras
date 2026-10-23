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
    Quest main = self.GetOwningQuest()
    TTM_MCM mcm = main as TTM_MCM

    if(!questDisabled && questIsntTouched && player.GetActorValue("DragonSouls") >= 1 &&  MARAS.GetStatusCount("married") == 1 && mcm.TTM_EnablePolygamyToggle.GetValue() == 0)
        enablePolygamyQst.SetStage(0)
    endif

    ApplySleepRestedSpell()
EndEvent

Event OnItemRemoved(Form akBaseItem, int aiItemCount, ObjectReference akItemReference, ObjectReference akDestContainer)
    TTM_ServiceGift.OnItemGifted(akDestContainer, akBaseItem, aiItemCount)
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

Function ApplySleepRestedSpell()
    Actor player = TTM_Data.GetPlayer()
    Location loc = player.GetCurrentLocation()
    Actor[] tenants = MARAS.GetPlayerHouseTenants(loc)
    PlayerSleepQuestScript sleepQst = TTM_Data.GetPlayerSleepQuest() as PlayerSleepQuestScript
    ; -1 - none; 0 - lover; 2 - spouse; 3 - child
    bool hasLoverNonSpouse = false
    bool hasSpouse = false
    bool hasChild = false

    int i = 0
    while(i < tenants.Length)
        Actor tenant = tenants[i]
        if(tenant.GetCurrentLocation() == loc)
            if(MARAS.IsNPCStatus(tenant, "lover") || MARAS.IsNPCStatus(tenant, "dating") || MARAS.IsNPCStatus(tenant, "engaged"))
                TTM_Debug.debug("TTM_PlayerScript: Player has a lover("+tenant+") in the house. Applying Lover Rested Spell.")
                hasLoverNonSpouse = true
            elseif(MARAS.IsNPCStatus(tenant, "married"))
                TTM_Debug.debug("TTM_PlayerScript: Player has a spouse("+tenant+") in the house. Applying Spouse Rested Spell.")
                hasSpouse = true
            elseif(MARAS.IsChildPlayerAdopted(tenant))
                TTM_Debug.debug("TTM_PlayerScript: Player has a child("+tenant+") in the house. Applying Child Rested Spell.")
                hasChild = true
            endif
        endif
        i += 1
    endwhile

    if(hasLoverNonSpouse)
        sleepQst.RemoveRested()
        player.AddSpell(TTM_LoversRadianceAbility, false)
        TTM_Debug.debug("TTM_PlayerScript: Player has a lover in the house. Applying Lover Rested Spell: "+TTM_LoversRadianceAbility)
    elseif(hasSpouse)
        sleepQst.RemoveRested()
        player.AddSpell(MarriageSleepAbility, false)
        TTM_Debug.debug("TTM_PlayerScript: Player has a spouse in the house. Applying Spouse Rested Spell: "+MarriageSleepAbility)
    endif

    if(hasChild)
        sleepQst.RemoveAdoptionRested()
        if (Game.GetPlayer().GetActorBase().GetSex() == 0)
            player.AddSpell(BYOHAdoptionSleepAbilityMale, false)
            TTM_Debug.debug("TTM_PlayerScript: Player has a child in the house. Applying Male Adoption Rested Spell: "+BYOHAdoptionSleepAbilityMale)
        else
            player.AddSpell(BYOHAdoptionSleepAbilityFemale, false)
            TTM_Debug.debug("TTM_PlayerScript: Player has a child in the house. Applying Female Adoption Rested Spell: "+BYOHAdoptionSleepAbilityFemale)
        endif
    endif
EndFunction
