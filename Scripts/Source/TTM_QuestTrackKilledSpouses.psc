scriptname TTM_QuestTrackKilledSpouses extends WorldInteractionsScript

Quest Property pWICourier  Auto
{pointer to WICourier quest}

Event OnStoryKillActor(ObjectReference akVictim, ObjectReference akKiller, Location akLocation, int aiCrimeStatus, int aiRelationshipRank)
    TTM_Debug.debug("TTM_QuestTrackKilledSpouses:OnStoryKillActor"+akVictim+":"+akKiller+":"+akLocation+":"+aiCrimeStatus+":"+aiRelationshipRank)

    SetStage(1)
EndEvent
