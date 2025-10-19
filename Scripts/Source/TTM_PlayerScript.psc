Scriptname TTM_PlayerScript extends ReferenceAlias

import PO3_Events_Form

Spell Property TTM_LoversRadianceAbility auto

Event OnPlayerLoadGame()
    int logDestination = TTM_JData.GetLogDestination()
    TTM_MainController mainController = self.GetOwningQuest() as TTM_MainController
    mainController.Maintenance()
    Quest enablePolygamyQst = TTM_JData.GetMarasEnablePolygamyQuest()

    if(!enablePolygamyQst.IsRunning() && !enablePolygamyQst.IsCompleted())
        RegisterForSleep()
    endif

    RegisterForUpdateGameTime(1)
EndEvent

Event OnSleepStart(Float afSleepStartTime, Float afDesiredSleepEndTime)
    Actor player = self.GetActorRef()
    Faction PlayerMarriedFaction = TTM_JData.GetMarriedFaction()
    Quest enablePolygamyQst = TTM_JData.GetMarasEnablePolygamyQuest()
    bool questIsntTouched = !enablePolygamyQst.IsRunning() && !enablePolygamyQst.IsCompleted()

    if(questIsntTouched && player.GetActorValue("DragonSouls") >= 1 && player.IsInFaction(PlayerMarriedFaction))
        TTM_Debug.trace("Start Enable Poly")
        enablePolygamyQst.SetStage(0)
    endif
EndEvent

Event OnSleepStop(bool abInterrupted)
    Actor player = TTM_JData.GetPlayer()
    Location loc = player.GetCurrentLocation()
    Form[] tenants = TTM_ServicePlayerHouse.GetHomeTenants(loc)

    ; todo scan nearby npcs instead of tenants from player house
    int i = 0
    bool addedSpell = false
    while(i < tenants.Length)
        Actor tenant = tenants[i] as Actor
        if(tenant.GetCurrentLocation() == loc)
            TTM_ServiceAffection.AddSleptAffection(tenant)
            if(!addedSpell)
                addedSpell = true
                player.AddSpell(TTM_LoversRadianceAbility, false)
                Debug.Notification("You feel the warmth of your partner's love. Lover's Radiance granted.")
            endif
        endif
        i += 1
    endwhile
EndEvent

Event OnItemRemoved(Form akBaseItem, int aiItemCount, ObjectReference akItemReference, ObjectReference akDestContainer)
    TTM_ServiceGift.OnItemGifted(akDestContainer, akBaseItem, aiItemCount)
EndEvent

; Event received when every object in this object's parent cell is loaded (TODO: Find restrictions)
Event OnCellLoad()
    ; since follower can be added and removed and we don't have a way to track it
    ; check each cell load player's followers and try to update spouse follower bonuses
    TTM_ServiceBuff.CalculateFollowerMultipliers()

    TTM_ServiceSpouseAssets.CheckCell()

    TTM_ServicePlayerHouse.CheckPlayerHouseLocation()
EndEvent

Event OnUpdateGameTime()
    Form[] npcs = TTM_ServiceRelationships.GetTrackedNpcs()
    int i = 0
    while(i < npcs.Length)
        Actor nextNpc = npcs[i] as Actor
        if(nextNpc.IsPlayerTeammate() || nextNpc.IsInFaction(TTM_JData.GetCurrentFollowerFaction()))
            TTM_JMethods.SetIntValue(nextNpc, "following", 1)
        else
            TTM_JMethods.SetIntValue(nextNpc, "following", 0)
        endif
        TTM_ServiceAffection.UpdateAffectionFaction(nextNpc)
        i += 1
    endwhile

    RegisterForUpdateGameTime(1)
EndEvent
