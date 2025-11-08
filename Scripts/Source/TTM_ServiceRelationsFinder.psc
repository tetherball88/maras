scriptname TTM_ServiceRelationsFinder

Actor Function GetExistingSpouse(Actor npc) global
    if(TTM_Data.GetHasTTRF())
        Actor spouse = TTRF_RelationsFinder.GetSpouse(npc)
        return spouse
    endif
    return none
EndFunction

Actor Function GetExistingCourting(Actor npc) global
    if(TTM_Data.GetHasTTRF())
        Actor courting = TTRF_RelationsFinder.GetCourting(npc)
        return courting
    endif
    return none
EndFunction

Actor[] Function GetExistingLovers(Actor npc) global
    if(TTM_Data.GetHasTTRF())
        return TTRF_RelationsFinder.GetAllLovers(npc)
    endif
    return none
EndFunction

