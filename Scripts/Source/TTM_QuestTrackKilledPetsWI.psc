scriptname TTM_QuestTrackKilledPetsWI extends WorldInteractionsScript

TTM_ServiceChildrenLogic property childreLogicQst Auto

Event OnStoryKillActor(ObjectReference akVictim, ObjectReference akKiller, Location akLocation, int aiCrimeStatus, int aiRelationshipRank)
    TTM_Debug.debug("TTM_QuestTrackKilledPetsWI:OnStoryKillActor"+akVictim+":"+akKiller+":"+akLocation+":"+aiCrimeStatus+":"+aiRelationshipRank)
    Actor killer = akKiller as Actor
    Actor victim = akVictim as Actor
    Actor petsChild = MARAS.GetChildForPetOrCritter(victim)
    TTM_Debug.debug("TTM_QuestTrackKilledPetsWI:OnStoryKillActor:petsChild"+petsChild)

    if(petsChild)
        childreLogicQst.CritterOrPetDeathForChild(victim, killer, petsChild)
    endif
    stop()
EndEvent

