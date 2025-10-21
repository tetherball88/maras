scriptname TTM_ServiceHierarchy

Form[] Function GetLeadSpouses() global
    return TTM_JMethods.FormListToArray(none, "SpouseHierarchyCache")
EndFunction

Actor Function GetSpouseByHierarchyRank(int rank) global
    return TTM_JMethods.FormListGet(none, "SpouseHierarchyCache", rank) as Actor
EndFunction

int Function GetSpouseRank(Actor spouse) global
    return spouse.GetFactionRank(TTM_JData.GetSpouseHierarchyFaction())
EndFunction

Function AddLeadSpouse(Actor spouse) global
    ; if not all top 3 spots are filled, add this spouse to the list
    if(GetLeadSpouses().Length < 3)
        int newIndex = TTM_JMethods.FormListAdd(none, "SpouseHierarchyCache", spouse)
        spouse.SetFactionRank(TTM_JData.GetSpouseHierarchyFaction(), newIndex)
        TTM_ServiceBuff.CalculateFollowerMultipliers()
        TTM_ServiceBuff.CalculatePermanentMultipliers()
        return
    endif
EndFunction

Function RemoveLeadSpouse(Actor spouse) global
    int currentRank = GetSpouseRank(spouse)
    ; if this spouse is not in the list, do nothing
    if(currentRank == -1)
        return
    endif

    ; remove this spouse from the lead list
    TTM_JMethods.FormListRemove(none, "SpouseHierarchyCache", spouse)
    spouse.RemoveFromFaction(TTM_JData.GetSpouseHierarchyFaction())

    ; attempt to find a new lead spouse
    int i = 0
    bool found = false
    Form[] spouses = TTM_ServiceRelationships.GetSpouses()
    while(i < spouses.Length && !found)
        Actor leadCandidateSpouse = spouses[i] as Actor
        if(leadCandidateSpouse != spouse && TTM_JMethods.FormListFind(none, "SpouseHierarchyCache", leadCandidateSpouse) == -1)
            found = true
            AddLeadSpouse(leadCandidateSpouse)
            TTM_Utils.SendChangeLeadSpouseRankEvent(leadCandidateSpouse, GetLeadSpouses().Length - 1, -1)
        endif
        i += 1
    endwhile

    TTM_ServiceBuff.CalculateFollowerMultipliers()
    TTM_ServiceBuff.CalculatePermanentMultipliers()
EndFunction

Function ChangeSpouseRank(Actor spouse, int newRank = -1) global
    Form[] spouses = TTM_ServiceRelationships.GetSpouses()

    ; if total number of spouses is less than 2, do nothing
    if(spouses.Length < 2)
        return
    endif

    ; if specific order is not specified, do nothing
    if(newRank == -1)
        return
    endif

    int currentRank = GetSpouseRank(spouse)

    ; if this spouse is already in the list, do nothing
    if(currentRank == newRank)
        return
    endif

    Actor newRankSpouse = spouses[newRank] as Actor
    ; swap their places if this spouse was already on top 3 list
    TTM_JMethods.FormListSet(none, "SpouseHierarchyCache", newRank, spouse)
    spouse.SetFactionRank(TTM_JData.GetSpouseHierarchyFaction(), newRank)
    TTM_Debug.trace("ChangeSpouseRank:"+TTM_Utils.GetActorName(spouse)+":"+newRank)
    TTM_Utils.SendChangeLeadSpouseRankEvent(spouse, newRank, currentRank)
    if(newRankSpouse != none)
        if(currentRank == -1)
            newRankSpouse.RemoveFromFaction(TTM_JData.GetSpouseHierarchyFaction())
            TTM_Debug.trace("ChangeSpouseRank:"+TTM_Utils.GetActorName(newRankSpouse)+":-1")
        else
            TTM_JMethods.FormListSet(none, "SpouseHierarchyCache", currentRank, newRankSpouse)
            newRankSpouse.SetFactionRank(TTM_JData.GetSpouseHierarchyFaction(), currentRank)
            TTM_Debug.trace("ChangeSpouseRank:"+TTM_Utils.GetActorName(newRankSpouse)+":"+currentRank)
        endif
        TTM_Utils.SendChangeLeadSpouseRankEvent(newRankSpouse, currentRank, newRank)
    endif
    TTM_ServiceBuff.CalculateFollowerMultipliers()
    TTM_ServiceBuff.CalculatePermanentMultipliers()
EndFunction
