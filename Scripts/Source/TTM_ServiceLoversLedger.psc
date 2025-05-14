scriptname TTM_ServiceLoversLedger

import TTM_JCDomain

float Function GetNpcPlayerIntimacy(Actor npc) global
    if(TTM_JData.GetHasTTLL())
        return 0
    endif

    float res = 0

    Actor player = TTM_JData.GetPlayer()
    if(TTLL_Store.GetLoverExclusiveSexCount(npc, player) >= 1)
        res += 10
    elseif(TTLL_Store.GetLoverGroupSexCount(npc, player) >= 1)
        res += 6
    endif

    if(TTLL_Store.IsNpcTopThreeLover(npc, TTM_JData.GetPlayer()))
        res += 6
    endif

    return res
EndFunction

Function UpdateNpcCurrentRelationships(int jNpc, Actor npc) global
    if(TTM_JData.GetHasTTRF())
        int jExistingRelationships = JArray_object()
        Actor spouse = TTRF_Store.GetSpouse(npc)
        Actor courting = TTRF_Store.GetCourting(npc)
        Form[] lovers = TTRF_Store.GetLovers(npc)

        if(spouse)
            CreateExistingForTTRF(npc, spouse, jExistingRelationships, true)
        endif

        if(courting)
            CreateExistingForTTRF(npc, courting, jExistingRelationships, false, true)
        endif

        int i = 0

        while(i < lovers.Length)
            CreateExistingForTTRF(npc, lovers[i] as Actor, jExistingRelationships)
            i += 1
        endwhile
    endif
EndFunction

Function CreateExistingForTTRF(Actor npc, Actor existing, int jExistingRelationships, bool isSpouse = false, bool isCourting = false) global
    int jExistingPartner = JMap_object()
    bool isLover = npc.GetRelationshipRank(existing) == 4

    JMap_setStr(jExistingPartner, "name", TTM_Utils.GetActorName(existing))
    JMap_setStr(jExistingPartner, "isSpouse", isSpouse)
    JMap_setStr(jExistingPartner, "isCourting", isCourting)
    JMap_setStr(jExistingPartner, "isLover", isLover)

    JArray_addObj(jExistingRelationships, jExistingPartner)
EndFunction
