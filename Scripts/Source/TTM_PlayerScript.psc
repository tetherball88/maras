Scriptname TTM_PlayerScript extends ReferenceAlias

import PO3_Events_Form
import TTM_JCDomain

Spell Property TTM_LoversRadianceAbility auto

Event OnPlayerLoadGame()
    TTM_MainController mainController = self.GetOwningQuest() as TTM_MainController
    mainController.Maintenance()
    Quest enablePolygamyQst = TTM_JData.GetMarasEnablePolygamyQuest()

    if(!enablePolygamyQst.IsRunning() && !enablePolygamyQst.IsCompleted())
        RegisterForSleep()
    endif
EndEvent

Event OnSleepStart(Float afSleepStartTime, Float afDesiredSleepEndTime)
    TTM_Debug.trace("OnSleepStart:default")
    Actor player = self.GetActorRef()
    Faction PlayerMarriedFaction = TTM_JData.GetMarriedFaction()
    Quest enablePolygamyQst = TTM_JData.GetMarasEnablePolygamyQuest()
    bool questIsntTouched = !enablePolygamyQst.IsRunning() && !enablePolygamyQst.IsCompleted()
    if(!questIsntTouched)
        TTM_Debug.trace("OnSleepStart:"+player.GetActorValue("DragonSouls"))
    endif

    if(questIsntTouched && player.GetActorValue("DragonSouls") >= 1 && player.IsInFaction(PlayerMarriedFaction))
        TTM_Debug.trace("Start Enable Poly")
        enablePolygamyQst.SetStage(0)
    endif
EndEvent

Event OnSleepStop(bool abInterrupted)
    Actor player = TTM_JData.GetPlayer()
    Location loc = player.GetCurrentLocation()
    int jTenants = TTM_ServicePlayerHouse.GetHomeTenants(loc)

    if(jTenants != 0)
        int i = 0
        while(i < JArray_count(jTenants))
            Actor tenant = JArray_getForm(jTenants, i) as Actor
            if(tenant.GetCurrentLocation() == loc)
                player.AddSpell(TTM_LoversRadianceAbility, false)
                Debug.Notification("You feel the warmth of your partner's love. Lover's Radiance granted.")
            endif
            i += 1
        endwhile
    endif
EndEvent

; Event received when every object in this object's parent cell is loaded (TODO: Find restrictions)
Event OnCellLoad()
    ; since follower can be added and removed and we don't have a way to track it
    ; check each cell load player's followers and try to update spouse follower bonuses
    TTM_ServiceBuff.CalculateFollowerMultipliers()

    TTM_ServiceSpouseAssets.CheckCell()

    TTM_ServicePlayerHouse.CheckPlayerHouseLocation()
EndEvent
