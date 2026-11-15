Scriptname TTM_QuestDivorceOnAcquireDivorceLetter extends ReferenceAlias

Event OnContainerChanged(ObjectReference akNewContainer, ObjectReference akOldContainer)
    Actor player = TTM_Data.GetPlayer()

    if (player == akNewContainer)
        TTM_QuestDivorce divorceQst = GetOwningQuest() as TTM_QuestDivorce
        Actor exSpouse = none
        if (divorceQst)
            exSpouse = divorceQst.Alias_Spouse.GetActorRef()
        endif
        MARAS.ProcessDivorceSettlement(exSpouse, divorceQst)
    endif
endEvent
