scriptname TTM_Actions extends Quest

; Used for actions which are supposed to be confirmed by other party
Function ProposeRelationshipChange(Actor akActor, Actor partner, string suggestedStatus)
    MARAS.ExecuteProposeRelationshipAction(akActor, partner, suggestedStatus)
EndFunction

; One sided decision.
; In case of break ups you don't need consent of other party.
; Or used as an answer for somebody suggesting non break up relations.
Function ConsiderRelationshipChange(Actor akActor, Actor partner, string decision, string status, string reasoning)
    MARAS.ExecuteConsiderRelationshipAction(akActor, partner, decision, status, reasoning)
EndFunction

; Breakup action handler.
; One sided decision using partner's string name to avoid SkyrimNet actor parsing.
Function BreakupRelationship(Actor akActor, string partnerName, string status, string reasoning = "")
    MARAS.ExecuteBreakupRelationshipAction(akActor, partnerName, status, reasoning)
EndFunction

Function SetOpenRelationshipChange(Actor akActor, Actor partner, bool isOpen, string reasoning = "")
    MARAS.ExecuteSetOpenRelationshipAction(akActor, partner, isOpen, reasoning)
EndFunction

Function QuestDivorceResolutionActionExecute(Actor akActor, string decision)
    int stage = MARAS.GetDivorceResolutionQuestStage(decision)
    if (stage <= 0)
        return
    endif
    Quest estrangedQuest = TTM_Data.GetMarasAffectionEstrangedDivorceQuest()
    if (!estrangedQuest || !estrangedQuest.IsRunning())
        return
    endif
    estrangedQuest.SetStage(stage)
EndFunction

; Mother reveals paternity secret of child to presumed father, biological father, or both
Function RevealChildSecret(Actor akActor, string targetFather, string childName, string revealMode = "true_paternity")
    MARAS.ExecuteRevealChildSecretAction(akActor, targetFather, childName, revealMode)
EndFunction

; Father discovers / learns / finds out about child paternity (from observing child, physical mismatch, or confrontation with mother)
Function DiscoverChildPaternity(Actor akActor, string childName, string motherName = "")
    MARAS.ExecuteDiscoverChildPaternityAction(akActor, childName, motherName)
EndFunction

; Cheater confesses their secret affair to their partner (victim)
Function ConfessAffair(Actor akActor, string victimName, string affairPartnerName, string reasoning = "")
    MARAS.ExecuteConfessAffairAction(akActor, victimName, affairPartnerName, reasoning)
EndFunction

; Affair partner exposes the secret affair to the victim
Function ExposeAffair(Actor akActor, string victimName, string cheaterName, string reasoning = "")
    MARAS.ExecuteExposeAffairAction(akActor, victimName, cheaterName, reasoning)
EndFunction

; Resolution between cheater and victim regarding the exposed affair
Function ResolveAffair(Actor akActor, string partnerName, string affairPartnerName, string resolution, string reasoning = "")
    MARAS.ExecuteResolveAffairAction(akActor, partnerName, affairPartnerName, resolution, reasoning)
EndFunction
