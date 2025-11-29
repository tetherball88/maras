Scriptname TTM_PlayerScript extends ReferenceAlias

import PO3_Events_Form

Spell Property TTM_LoversRadianceAbility auto

Event OnInit()
    InitPlayer()
EndEvent

Event OnPlayerLoadGame()
    InitPlayer()
    TTM_MainController mainController = self.GetOwningQuest() as TTM_MainController
    mainController.Maintenance()
EndEvent

Function InitPlayer()
    Quest enablePolygamyQst = TTM_Data.GetMarasEnablePolygamyQuest()
    Quest main = self.GetOwningQuest()
    TTM_MCM mcm = main as TTM_MCM

    if(!enablePolygamyQst.IsRunning() && !enablePolygamyQst.IsCompleted() && mcm.TTM_EnablePolygamyToggle.GetValue() == 0)
        RegisterForSleep()
    endif

    CheckCell()
EndFunction

Event OnSleepStop(bool abInterrupted)
    Actor player = TTM_Data.GetPlayer()
    Location loc = player.GetCurrentLocation()
    Actor[] tenants = MARAS.GetPlayerHouseTenants(loc)

    Quest enablePolygamyQst = TTM_Data.GetMarasEnablePolygamyQuest()
    bool questIsntTouched = !enablePolygamyQst.IsRunning() && !enablePolygamyQst.IsCompleted()
    Quest main = self.GetOwningQuest()
    TTM_MCM mcm = main as TTM_MCM

    if(questIsntTouched && player.GetActorValue("DragonSouls") >= 1 &&  MARAS.GetStatusCount("married") == 1 && mcm.TTM_EnablePolygamyToggle.GetValue() == 0)
        enablePolygamyQst.SetStage(0)
    endif

    int i = 0
    bool addedSpell = false
    while(i < tenants.Length)
        Actor tenant = tenants[i]
        if(tenant.GetCurrentLocation() == loc)
            TTM_ServiceAffection.AddSleptAffection(tenant)
            if(!addedSpell)
                addedSpell = true
                player.AddSpell(TTM_LoversRadianceAbility, false)
                TTM_Messages.LoversRadianceEffectMsg()
                return
            endif
        endif
        i += 1
    endwhile
EndEvent

Event OnItemRemoved(Form akBaseItem, int aiItemCount, ObjectReference akItemReference, ObjectReference akDestContainer)
    TTM_ServiceGift.OnItemGifted(akDestContainer, akBaseItem, aiItemCount)
EndEvent

Function CheckCell()
    TTM_ServiceSpouseAssets.CheckCell()
    TTM_ServicePlayerHouse.CheckPlayerHouseLocation()
EndFunction

Event OnLocationChange(Location akOldLoc, Location akNewLoc)
    CheckCell()
EndEvent

