scriptname TTM_QuestTrackKilledSpousesWI extends WorldInteractionsScript

Quest Property pWICourier  Auto
{pointer to WICourier quest}

Event OnStoryKillActor(ObjectReference akVictim, ObjectReference akKiller, Location akLocation, int aiCrimeStatus, int aiRelationshipRank)
    TTM_Debug.debug("TTM_QuestTrackKilledSpouses:OnStoryKillActor"+akVictim+":"+akKiller+":"+akLocation+":"+aiCrimeStatus+":"+aiRelationshipRank)
    Actor killer = akKiller as Actor
    Actor victim = akVictim as Actor
    Actor player = TTM_Data.GetPlayer()
    ; if player killed without reason, I assume crime triggers when it wasn't provoked or by quest
    bool isPlayerKiller = killer == player && aiCrimeStatus == 1

    if(MARAS.IsNPCStatus(victim, "any"))
        TTM_ServiceRelationships.MakeNpcDeceased(victim, isPlayerKiller)
        SetStage(1)
    else
        stop()
    endif
EndEvent
