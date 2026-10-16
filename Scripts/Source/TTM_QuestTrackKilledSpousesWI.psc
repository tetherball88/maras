scriptname TTM_QuestTrackKilledSpousesWI extends WorldInteractionsScript

Quest Property pWICourier  Auto
{pointer to WICourier quest}

Event OnStoryKillActor(ObjectReference akVictim, ObjectReference akKiller, Location akLocation, int aiCrimeStatus, int aiRelationshipRank)
    TTM_Debug.debug("TTM_QuestTrackKilledSpouses:OnStoryKillActor"+akVictim+":"+akKiller+":"+akLocation+":"+aiCrimeStatus+":"+aiRelationshipRank)
    Actor victim = akVictim as Actor

    Actor player = TTM_Data.GetPlayer()
    if(MARAS.IsNPCStatus(victim, "any") || MARAS.WasMarriedTo(victim, player) || MARAS.WasEngagedTo(victim, player) || MARAS.WasExOf(victim, player))
        SetStage(1)
    else
        stop()
    endif
EndEvent
