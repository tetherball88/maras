scriptname TTM_ServiceRelationsFinder

import TTM_JCDomain

Actor Function GetExistingSpouse(Actor npc) global
    if(TTM_JData.GetHasTTRF())
        return TTRF_Store.GetSpouse(npc)
    endif
    return none
EndFunction

Actor Function GetExistingCourting(Actor npc) global
    if(TTM_JData.GetHasTTRF())
        return TTRF_Store.GetCourting(npc)
    endif
    return none
EndFunction

Form[] Function GetExistingLovers(Actor npc) global
    if(TTM_JData.GetHasTTRF())
        return TTRF_Store.GetLovers(npc)
    endif
    return none
EndFunction

