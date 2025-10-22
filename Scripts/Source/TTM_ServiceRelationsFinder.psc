scriptname TTM_ServiceRelationsFinder

Function ScanRelationships(Actor npc) global
    if(TTM_JData.GetHasTTRF() && !Utility.IsInMenuMode())
        TTRF_Store.GetNpcRelationships(npc)
    endif
EndFunction

Actor Function GetExistingSpouse(Actor npc) global
    if(TTM_JData.GetHasTTRF() && !Utility.IsInMenuMode())
        return TTRF_Store.GetSpouse(npc)
    endif
    return none
EndFunction

Actor Function GetExistingCourting(Actor npc) global
    if(TTM_JData.GetHasTTRF() && !Utility.IsInMenuMode())
        return TTRF_Store.GetCourting(npc)
    endif
    return none
EndFunction

Form[] Function GetExistingLovers(Actor npc) global
    if(TTM_JData.GetHasTTRF() && !Utility.IsInMenuMode())
        return TTRF_Store.GetLovers(npc)
    endif
    return none
EndFunction

