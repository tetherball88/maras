scriptname TTM_Actions extends Quest

; Used for actions whioch are supposed to be confirmed by other party
Function ProposeRelationshipChange(Actor akActor, Actor partner, string suggestedStatus)
    if(!partner)
        return
    endif

    string status = suggestedStatus
    string currentStatus = MARAS.GetNpcStatusNameWith(akActor, partner)

    if(status == currentStatus)
        return
    endif

    if(!MARAS.IsAvailableForStatusWith(akActor, partner, suggestedStatus))
        TTM_Debug.debug("ProposeRelationshipChange(" + suggestedStatus + "): " + TTM_Utils.GetActorName(akActor) + " and " + TTM_Utils.GetActorName(partner) + " are not eligible for this action.")
        return
    endif

    if(partner == TTM_Data.GetPlayer())
        Actor[] mainActors = new Actor[1]
        mainActors[0] = akActor
        Actor[] secondaryActors = new Actor[1]
        secondaryActors[0] = partner

        int modalResult = MARAS.ShowConfirmationModal(suggestedStatus, mainActors, secondaryActors)
        if (modalResult == 0 || modalResult == 4 || modalResult == 5)
            if (MARAS.GetNpcStatusNameWith(akActor, partner) != currentStatus || !MARAS.IsAvailableForStatusWith(akActor, partner, suggestedStatus))
                TTM_Debug.debug("ProposeRelationshipChange(" + suggestedStatus + "): State changed while confirmation was pending; ignoring.")
                return
            endif
            bool success = MARAS.PromoteNPCToStatusWith(partner, akActor, suggestedStatus)
            if (!success)
                TTM_Debug.debug("ProposeRelationshipChange: Action " + suggestedStatus + " blocked by relationship rules.")
            endif
        else
            TTM_Debug.debug("ProposeRelationshipChange: Action " + suggestedStatus + " declined or blocked (modalResult=" + modalResult + ")")
        endif

        return
    endif

    string msg = TTM_Utils.GetActorName(akActor) + " proposes "

    if(status == "dating")
        msg += "a dating relationship"
    elseif(status == "engaged")
        msg += "an engagement relationship"
    elseif(status == "lover")
        msg += "a lover relationship"
    endif

    msg += " to " + TTM_Utils.GetActorName(partner) + ". " + TTM_Utils.GetActorName(partner) + " must answer and accept or reject the proposal."

    ; prompt for partner to answer
    Utility.Wait(5.0)
    SkyrimNetApi.DirectNarration(msg, partner, akActor)
EndFunction

; One sided decision.
; In case of break ups you don't need consent of other party.
; Or used as an answer for somebody suggesting non break up relations.
Function ConsiderRelationshipChange(Actor akActor, Actor partner, string decision, string status, string reasoning)
    TTM_Debug.debug("ConsiderRelationshipChange(" + status + "): " + TTM_Utils.GetActorName(akActor) + " and " + TTM_Utils.GetActorName(partner) + " decided to " + decision + " with reasoning: " + reasoning)
    if(!partner)
        TTM_Debug.debug("ConsiderRelationshipChange(" + status + "): Partner not found for " + TTM_Utils.GetActorName(akActor))
        return
    endif

    string currentStatus = MARAS.GetNpcStatusNameWith(akActor, partner)

    if(status == currentStatus)
        TTM_Debug.debug("ConsiderRelationshipChange(" + status + "): " + TTM_Utils.GetActorName(akActor) + " and " + TTM_Utils.GetActorName(partner) + " are already " + status + ".")
        return
    endif

    if(decision == "accept")
        if(!MARAS.IsAvailableForStatusWith(partner, akActor, status))
            TTM_Debug.debug("ConsiderRelationshipChange(" + status + "): " + TTM_Utils.GetActorName(partner) + " and " + TTM_Utils.GetActorName(akActor) + " are not eligible for this action.")
            return
        endif

        Actor[] mainActors = new Actor[1]
        mainActors[0] = akActor
        Actor[] secondaryActors = new Actor[1]
        secondaryActors[0] = partner

        int modalResult = MARAS.ShowConfirmationModal(status, mainActors, secondaryActors)
        if (modalResult == 0 || modalResult == 4 || modalResult == 5)
            if (MARAS.GetNpcStatusNameWith(akActor, partner) != currentStatus || !MARAS.IsAvailableForStatusWith(partner, akActor, status))
                TTM_Debug.debug("ConsiderRelationshipChange(" + status + "): State changed while confirmation was pending; ignoring.")
                return
            endif
            bool success = MARAS.PromoteNPCToStatusWith(partner, akActor, status)
            if (!success)
                TTM_Debug.debug("ConsiderRelationshipChange: Action " + status + " blocked by relationship rules.")
                return
            endif
        else
            TTM_Debug.debug("ConsiderRelationshipChange: Action " + status + " declined or blocked (modalResult=" + modalResult + ")")
            return
        endif
    elseif(decision == "reject")
        Actor player = TTM_Data.GetPlayer()
        if (partner == player && akActor != none)
            TTM_ServiceAffection.AdjustPlayerPartnerAffection(akActor, -15.0, "proposal_rejected_by_partner")
        endif
        TTM_ServiceSkyrimNet.RegisterProposalDecision(partner, akActor, status, decision, reasoning)
    endif
EndFunction

; Breakup action handler.
; One sided decision using partner's string name to avoid SkyrimNet actor parsing.
Function BreakupRelationship(Actor akActor, string partnerName, string status, string reasoning = "")
    TTM_Debug.debug("BreakupRelationship(" + status + "): " + TTM_Utils.GetActorName(akActor) + " initiated breakup with '" + partnerName + "' with reasoning: " + reasoning)
    if(!akActor || partnerName == "")
        return
    endif

    string currentStatus = ""
    if(status == "exdating")
        currentStatus = "dating"
    elseif(status == "jilted")
        currentStatus = "engaged"
    elseif(status == "exlover")
        currentStatus = "lover"
    elseif(status == "divorced")
        currentStatus = "married"
    else
        TTM_Debug.warn("BreakupRelationship: Unknown breakup status '" + status + "'")
        return
    endif

    Actor[] partners = MARAS.GetRelationshipPartners(akActor, currentStatus)
    Actor partner = None
    int i = 0
    while(i < partners.Length)
        Actor candidate = partners[i]
        if(candidate && TTM_Utils.GetActorName(candidate) == partnerName)
            partner = candidate
            i = partners.Length ; break
        endif
        i += 1
    endwhile

    if(!partner)
        TTM_Debug.warn("BreakupRelationship(" + status + "): No partner matching '" + partnerName + "' found for " + TTM_Utils.GetActorName(akActor))
        return
    endif

   if(!MARAS.IsAvailableForStatusWith(partner, akActor, status))
        TTM_Debug.debug("BreakupRelationship(" + status + "): " + TTM_Utils.GetActorName(partner) + " and " + TTM_Utils.GetActorName(akActor) + " are not eligible for this action.")
        return
    endif

    Actor[] mainActors = new Actor[1]
    mainActors[0] = akActor
    Actor[] secondaryActors = new Actor[1]
    secondaryActors[0] = partner

    int modalResult = MARAS.ShowConfirmationModal(status, mainActors, secondaryActors)
    if (modalResult == 0 || modalResult == 4 || modalResult == 5)
        if (MARAS.GetNpcStatusNameWith(akActor, partner) != currentStatus || !MARAS.IsAvailableForStatusWith(partner, akActor, status))
            TTM_Debug.debug("BreakupRelationship(" + status + "): State changed while confirmation was pending; ignoring.")
            return
        endif
        bool success = MARAS.PromoteNPCToStatusWith(partner, akActor, status)
        if (!success)
            TTM_Debug.debug("BreakupRelationship: Action " + status + " blocked by relationship rules.")
            return
        endif
    else
        TTM_Debug.debug("BreakupRelationship: Action " + status + " declined or blocked (modalResult=" + modalResult + ")")
        return
    endif
EndFunction

Function SetOpenRelationshipChange(Actor akActor, Actor partner, bool isOpen, string reasoning = "")
    TTM_Debug.debug("SetOpenRelationshipChange: " + TTM_Utils.GetActorName(akActor) + " and " + TTM_Utils.GetActorName(partner) + " open=" + isOpen + " reasoning: " + reasoning)
    if(!partner)
        return
    endif

    string currentStatus = MARAS.GetNpcStatusNameWith(akActor, partner)
    if (MARAS.GetPairIsOpen(akActor, partner) == isOpen)
        return
    endif
    string actionName = "open"
    if(!isOpen)
        actionName = "close"
    endif

    string actionType = "open_relationship"
    if(!isOpen)
        actionType = "close_relationship"
    endif

    Actor[] mainActors = new Actor[1]
    mainActors[0] = akActor
    Actor[] secondaryActors = new Actor[1]
    secondaryActors[0] = partner

    int modalResult = MARAS.ShowConfirmationModal(actionType, mainActors, secondaryActors)
    if (modalResult == 0 || modalResult == 4 || modalResult == 5)
        if (MARAS.GetNpcStatusNameWith(akActor, partner) != currentStatus || MARAS.GetPairIsOpen(akActor, partner) == isOpen)
            TTM_Debug.debug("SetOpenRelationshipChange(" + actionType + "): State changed while confirmation was pending; ignoring.")
            return
        endif
        MARAS.SetPairIsOpen(akActor, partner, isOpen)
    else
        TTM_Debug.debug("SetOpenRelationshipChange: Action " + actionType + " declined (modalResult=" + modalResult + ")")
    endif
EndFunction

Function QuestDivorceResolutionActionExecute(Actor akActor, string decision)
    Quest estrangedQuest = TTM_Data.GetMarasAffectionEstrangedDivorceQuest()
    if(!estrangedQuest || !estrangedQuest.IsRunning())
        return
    endif
    if(decision == "reconcile")
        estrangedQuest.SetStage(100)
        return
    elseif(decision == "divorce")
        estrangedQuest.SetStage(200)
    endif
EndFunction

; Mother reveals paternity secret of child to presumed father, biological father, or both
Function RevealChildSecret(Actor akActor, string targetFather, string childName)
    TTM_Debug.debug("RevealChildSecret: " + TTM_Utils.GetActorName(akActor) + " revealing secret to '" + targetFather + "' for child '" + childName)
    if (!akActor)
        return
    endif

    bool isBoth = (targetFather == "both")
    Actor targetFatherActor = None
    if (!isBoth && targetFather != "")
        targetFatherActor = MARAS.FindActorByName(targetFather)
    endif

    int childID = MARAS.FindChildPaternitySecret(akActor, targetFatherActor, childName)
    if (childID == 0 && targetFatherActor != None)
        childID = MARAS.FindChildPaternitySecret(akActor, None, childName)
    endif
    if (childID == 0)
        TTM_Debug.warn("RevealChildSecret: No child with active paternity secret found for mother " + TTM_Utils.GetActorName(akActor) + " (childName='" + childName + "')")
        return
    endif

    Actor presumedFather = MARAS.GetMotherPartner(childID)
    Actor bioFather = MARAS.GetBioFather(childID)
    string bioFatherName = TTM_Utils.GetActorName(bioFather)
    string presumedFatherName = TTM_Utils.GetActorName(presumedFather)

    if (isBoth)
        MARAS.ResolvePaternitySecretForPartner(childID, false)
        MARAS.ResolvePaternitySecretForBioFather(childID, false)
    elseif (targetFatherActor == bioFather || (bioFatherName != "" && targetFather == bioFatherName) || StringUtil.Find(targetFather, "bio") != -1)
        MARAS.ResolvePaternitySecretForBioFather(childID, false)
    elseif (targetFatherActor == presumedFather || (presumedFatherName != "" && targetFather == presumedFatherName) || StringUtil.Find(targetFather, "presumed") != -1 || StringUtil.Find(targetFather, "assumed") != -1)
        MARAS.ResolvePaternitySecretForPartner(childID, false)
    else
        ; Fallback: resolve for both
        MARAS.ResolvePaternitySecretForPartner(childID, false)
        MARAS.ResolvePaternitySecretForBioFather(childID, false)
    endif

    ; If childName was 'all', resolve any other remaining active secrets for other fathers as well
    if (childName == "all")
        int nextChildID = MARAS.FindChildPaternitySecret(akActor, targetFatherActor, "all")
        int guardLoop = 0
        while (nextChildID != 0 && nextChildID != childID && guardLoop < 10)
            guardLoop += 1
            if (isBoth)
                MARAS.ResolvePaternitySecretForPartner(nextChildID, false)
                MARAS.ResolvePaternitySecretForBioFather(nextChildID, false)
            elseif (targetFatherActor == bioFather || (bioFatherName != "" && targetFather == bioFatherName) || StringUtil.Find(targetFather, "bio") != -1)
                MARAS.ResolvePaternitySecretForBioFather(nextChildID, false)
            elseif (targetFatherActor == presumedFather || (presumedFatherName != "" && targetFather == presumedFatherName) || StringUtil.Find(targetFather, "presumed") != -1 || StringUtil.Find(targetFather, "assumed") != -1)
                MARAS.ResolvePaternitySecretForPartner(nextChildID, false)
            else
                MARAS.ResolvePaternitySecretForPartner(nextChildID, false)
                MARAS.ResolvePaternitySecretForBioFather(nextChildID, false)
            endif
            nextChildID = MARAS.FindChildPaternitySecret(akActor, targetFatherActor, "all")
        endWhile
    endif
EndFunction

; Father discovers / learns / finds out about child paternity (from observing child, physical mismatch, or confrontation with mother)
Function DiscoverChildPaternity(Actor akActor, string childName, string motherName = "")
    TTM_Debug.debug("DiscoverChildPaternity: " + TTM_Utils.GetActorName(akActor) + " discovering truth about child '" + childName + "' (mother='" + motherName + "')")
    if (!akActor)
        return
    endif

    Actor motherActor = None
    if (motherName != "")
        motherActor = MARAS.FindActorByName(motherName)
    endif

    int childID = MARAS.FindChildPaternitySecret(akActor, motherActor, childName)
    if (childID == 0 && motherActor != None)
        childID = MARAS.FindChildPaternitySecret(akActor, None, childName)
    endif
    if (childID == 0)
        TTM_Debug.warn("DiscoverChildPaternity: No child with active paternity secret found for father " + TTM_Utils.GetActorName(akActor) + " (childName='" + childName + "')")
        return
    endif

    Actor presumedFather = MARAS.GetMotherPartner(childID)
    Actor bioFather = MARAS.GetBioFather(childID)
    if (motherActor == None)
        motherActor = MARAS.GetBioMother(childID)
    endif

    if (akActor == presumedFather || (akActor != bioFather && MARAS.IsSecretForPartner(childID)))
        MARAS.ResolvePaternitySecretForPartner(childID, true)
    else
        MARAS.ResolvePaternitySecretForBioFather(childID, true)
    endif

    ; If childName was 'all', resolve any other remaining active secrets for this father as well
    if (childName == "all")
        int nextChildID = MARAS.FindChildPaternitySecret(akActor, motherActor, "all")
        int guardLoop = 0
        while (nextChildID != 0 && nextChildID != childID && guardLoop < 10)
            guardLoop += 1
            if (akActor == presumedFather || MARAS.IsSecretForPartner(nextChildID))
                MARAS.ResolvePaternitySecretForPartner(nextChildID, true)
            else
                MARAS.ResolvePaternitySecretForBioFather(nextChildID, true)
            endif
            nextChildID = MARAS.FindChildPaternitySecret(akActor, motherActor, "all")
        endWhile
    endif
EndFunction

Actor Function ResolveActorByName(string name)
    if (name == "" || name == "None" || name == "none")
        return None
    endif
    Actor player = TTM_Data.GetPlayer()
    if (name == "Player" || name == "player" || name == "Dragonborn" || name == "dragonborn" || name == TTM_Utils.GetActorName(player))
        return player
    endif
    return MARAS.FindActorByName(name)
EndFunction

; Cheater confesses their secret affair to their partner (victim)
Function ConfessAffair(Actor akActor, string victimName, string affairPartnerName, string reasoning = "")
    TTM_Debug.debug("ConfessAffair: " + TTM_Utils.GetActorName(akActor) + " confessing affair with '" + affairPartnerName + "' to '" + victimName + "' (reasoning: " + reasoning + ")")
    if (!akActor)
        return
    endif

    Actor victim = ResolveActorByName(victimName)
    Actor affairPartner = ResolveActorByName(affairPartnerName)

    ; Mark affair as discovered in SKSE with type "confessed" - fires maras_affair_exposed
    bool success = MARAS.SetCheatingDiscovered(akActor, affairPartner, "confessed")
    if (!success)
        TTM_Debug.warn("ConfessAffair: SetCheatingDiscovered returned false for cheater=" + TTM_Utils.GetActorName(akActor) + ", partner=" + TTM_Utils.GetActorName(affairPartner))
    endif
EndFunction

; Affair partner exposes the secret affair to the victim
Function ExposeAffair(Actor akActor, string victimName, string cheaterName, string reasoning = "")
    TTM_Debug.debug("ExposeAffair: " + TTM_Utils.GetActorName(akActor) + " exposing affair with '" + cheaterName + "' to '" + victimName + "' (reasoning: " + reasoning + ")")
    if (!akActor)
        return
    endif

    Actor victim = ResolveActorByName(victimName)
    Actor cheater = ResolveActorByName(cheaterName)

    ; Mark affair as discovered in SKSE with type "exposed" - fires maras_affair_exposed
    bool success = MARAS.SetCheatingDiscovered(cheater, akActor, "exposed")
    if (!success)
        TTM_Debug.warn("ExposeAffair: SetCheatingDiscovered returned false for cheater=" + TTM_Utils.GetActorName(cheater) + ", partner=" + TTM_Utils.GetActorName(akActor))
    endif
EndFunction

; Resolution between cheater and victim regarding the exposed affair
Function ResolveAffair(Actor akActor, string partnerName, string affairPartnerName, string resolution, string reasoning = "")
    TTM_Debug.debug("ResolveAffair: " + TTM_Utils.GetActorName(akActor) + " resolving affair with partner '" + partnerName + "' (affairPartner: '" + affairPartnerName + "', res: " + resolution + ", reasoning: " + reasoning + ")")
    if (!akActor)
        return
    endif

    Actor otherParty = ResolveActorByName(partnerName)
    Actor affairPartner = ResolveActorByName(affairPartnerName)

    Actor cheater = akActor
    if (otherParty && MARAS.HasCheatedOn(otherParty, akActor))
        cheater = otherParty
    endif

    ; Mark affair resolution in SKSE - fires maras_affair_resolved
    bool success = MARAS.SetCheatingDiscovered(cheater, affairPartner, resolution)
    if (!success)
        TTM_Debug.warn("ResolveAffair: SetCheatingDiscovered returned false for cheater=" + TTM_Utils.GetActorName(cheater) + ", partner=" + TTM_Utils.GetActorName(affairPartner) + ", res=" + resolution)
    endif
EndFunction



