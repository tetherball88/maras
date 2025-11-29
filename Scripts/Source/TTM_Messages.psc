scriptname TTM_Messages

Function RankChangeMsg(string spouseName, bool up) global
    if(up)
        Debug.Notification("$TTM_MSG_RankUp{" + spouseName + "}")
    else
        Debug.Notification("$TTM_MSG_RankDown{" + spouseName + "}")
    endif
EndFunction

Function AffectionChangeMsg(string npcName, string level, bool improving) global
    string msg = ""
    if(improving)
        if(level == "happy")
            msg = "$TTM_MSG_AffectionImproveHappy{" + npcName + "}"
        elseif(level == "content")
            msg = "$TTM_MSG_AffectionImproveContent{" + npcName + "}"
        elseif(level == "troubled")
            msg = "$TTM_MSG_AffectionImproveTroubled{" + npcName + "}"
        endif
    else
        if(level == "content")
            msg = "$TTM_MSG_AffectionDeclineContent{" + npcName + "}"
        elseif(level == "troubled")
            msg = "$TTM_MSG_AffectionDeclineTroubled{" + npcName + "}"
        elseif(level == "estranged")
            msg = "$TTM_MSG_AffectionDeclineEstranged{" + npcName + "}"
        endif
    endif
    Debug.Notification(msg)
EndFunction

Function LoversRadianceEffectMsg() global
    Debug.Notification("$TTM_MSG_LoversRadiance")
EndFunction

Function PlayerNoHomeMsg() global
    Debug.Notification("$TTM_MSG_PlayerNoHome")
EndFunction

Function FreeSpouseFromPlayerHomeMsg(string spouseName) global
    Debug.Notification("$TTM_MSG_FreeSpouseFromHome{" + spouseName + "}")
EndFunction

Function RegisterCandidateMsg(string npcName) global
    Debug.Notification("$TTM_MSG_RegisterCandidate{" + npcName + "}")
EndFunction

Function EngagementMsg(string npcName) global
    Debug.Notification("$TTM_MSG_Engagement{" + npcName + "}")
EndFunction

Function MarriedMsg(string npcName) global
    Debug.Notification("$TTM_MSG_Married{" + npcName + "}")
EndFunction

Function DivorceMsg(string npcName) global
    Debug.Notification("$TTM_MSG_Divorce{" + npcName + "}")
EndFunction

Function JiltedMsg(string npcName) global
    Debug.Notification("$TTM_MSG_Jilted{" + npcName + "}")
EndFunction

Function SpouseNoHomeMsg(string npcName) global
    Debug.Notification("$TTM_MSG_SpouseNoHome{" + npcName + "}")
EndFunction

Function SpouseSharedHomeMsg(string npcName, string homeName) global
    Debug.Notification("$TTM_MSG_SpouseSharedHome{" + npcName + "}{" + homeName + "}")
EndFunction

Function SpouseSharedHomeStoppedMsg(string npcName, string reason) global
    if(reason == "divorce")
        Debug.Notification("$TTM_MSG_SpouseSharedHomeStoppedDivorce{" + npcName + "}")
    elseif(reason == "deceased")
        Debug.Notification("$TTM_MSG_SpouseSharedHomeStoppedDeceased{" + npcName + "}")
    elseif(reason == "affection")
        Debug.Notification("$TTM_MSG_SpouseSharedHomeStoppedAffection{" + npcName + "}")
    else
        Debug.Notification("$TTM_MSG_SpouseSharedHomeStoppedOther{" + npcName + "}")
    endif
EndFunction
