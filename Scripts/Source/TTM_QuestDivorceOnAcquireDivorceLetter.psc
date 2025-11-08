Scriptname TTM_QuestDivorceOnAcquireDivorceLetter extends ReferenceAlias

Event OnContainerChanged(ObjectReference akNewContainer, ObjectReference akOldContainer)
    Actor player = TTM_Data.GetPlayer()

    if (player == akNewContainer)
        TTM_ServiceRelationships.AddDivorceFee(GetOwningQuest())
    endif
endEvent
