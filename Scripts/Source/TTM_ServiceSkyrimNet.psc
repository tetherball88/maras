;/
  TTM_ServiceSkyrimNet.psc

  Integrates MARAS mod with the SkyrimNet AI system.
  Handles syncing of player and NPC data, configuration, and polygamy state with SkyrimNet.

  Main responsibilities:
    - Syncing tracked NPCs and player stats to SkyrimNet
    - Updating and clearing NPC/quest data in SkyrimNet
    - Handling configuration options and polygamy state
    - All actions are conditional on SkyrimNet being enabled (TTM_Data.GetHasSkyrimNet())
/;
scriptname TTM_ServiceSkyrimNet

;/
  Main maintenance function. Syncs all relevant data to SkyrimNet if enabled.
/;
Function Maintenance() global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif

    TTM_Debug.debug("TTM_ServiceSkyrimNet:Maintenance: Register MARAS event schema with SkyrimNet")

    RegisterEventSchema()
EndFunction

Function SimulatePostWeddingIfSkippedBehavior(Actor npc) global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif
    Actor player = TTM_Data.GetPlayer()
    DirectNarration("Time passed... " + TTM_Utils.GetActorName(player) + " and " + TTM_Utils.GetActorName(npc) + " got married!", npc, player)
EndFunction

Function DirectNarration(String content, Actor originatorActor = None, Actor targetActor = None, bool exactAudience = false, Actor[] audience = None) global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif
    if(originatorActor != None && !MARAS.ShouldTriggerSkyrimNetNarration(originatorActor, targetActor))
        return
    endif
    MARAS.RegisterSkyrimNetEvent("direct_narration", content, originatorActor, targetActor, exactAudience, audience)
EndFunction

Actor[] Function MakeAudience2(Actor a, Actor b = None) global
    Actor[] aud = new Actor[2]
    aud[0] = a
    aud[1] = b
    return aud
EndFunction

Actor[] Function MakeAudience3(Actor a, Actor b = None, Actor c = None) global
    Actor[] aud = new Actor[3]
    aud[0] = a
    aud[1] = b
    aud[2] = c
    return aud
EndFunction

;/
  Marks a short real-time suppression window so thatModCallbackEvents fired by
  SkyrimNet dialogue actions (where the speaker already spoke the line in dialogue)
  or cascading secondary events still log to recent_events with skipTrigger = true
  instead of firing a redundant second direct_narration trigger.
/;
Function SuppressNextActionNarration(float durationSeconds = 5.0) global
    StorageUtil.SetFloatValue(none, "TTM_SkyrimNet_SuppressActionNarrationUntil", Utility.GetCurrentRealTime() + durationSeconds)
EndFunction

bool Function IsActionNarrationSuppressed() global
    float until = StorageUtil.GetFloatValue(none, "TTM_SkyrimNet_SuppressActionNarrationUntil", 0.0)
    if(until > 0.0 && Utility.GetCurrentRealTime() < until)
        return true
    endif
    return false
EndFunction

Function RegisterPlayerGotPolygamyBlessing(Actor spouse) global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif
    Actor player = TTM_Data.GetPlayer()
    string playerName = TTM_Utils.GetActorName(player)
    string spouseName = TTM_Utils.GetActorName(spouse)
    string msg = playerName + " got Mara's blessing to enter polygamy marriage. " + spouseName + " agreed with " + playerName + "'s new Dragonborn reality and new way of their marriage'."
    bool skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(player, spouse)
    string json = BuildJson("ttm_polygamy_blessing", msg, skipTrigger = skipTrigger)
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, player, spouse)
EndFunction

Function RegisterAffectionGift(string msg, Actor receiver) global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif
    Actor player = TTM_Data.GetPlayer()
    bool skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(player, receiver)
    string json = BuildJson("ttm_affection_gift", msg, skipTrigger = skipTrigger)
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, player, receiver)
EndFunction

Function RegisterMaritalStatusChangeEvent(Actor initiator, Actor acceptor, string newStatus, bool skipTrigger = false) global
    if(!TTM_Data.GetHasSkyrimNet() || newStatus == "candidate" || !initiator || !acceptor)
        return
    endif
    string type = "ttm_marital_status_change"
    Actor player = TTM_Data.GetPlayer()
    string initiatorName = TTM_Utils.GetActorName(initiator)
    string acceptorName = TTM_Utils.GetActorName(acceptor)
    string msg = initiatorName + " and " + acceptorName

    if(newStatus == "lover")
        msg += " become long term lovers."
    elseif(newStatus == "dating")
        msg += " become dating couple."
    else
        msg +=  " just got " + newStatus + "."
    endif

    if(!skipTrigger)
        if(IsActionNarrationSuppressed())
            skipTrigger = true
        else
            skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(initiator, acceptor)
        endif
    endif
    string json = BuildJson("ttm_marital_status_change", msg, skipTrigger = skipTrigger, newStatus = newStatus)

    MARAS.RegisterSkyrimNetEvent("ttm_event", json, initiator, acceptor)
    if(!skipTrigger && newStatus != "lover" && newStatus != "engaged")
        SuppressNextActionNarration(3.0)
    endif
EndFunction

Function RegisterOpenRelationshipChangeEvent(Actor initiator, Actor partner, bool isOpen, bool skipTrigger = false) global
    if(!TTM_Data.GetHasSkyrimNet() || !initiator || !partner)
        return
    endif
    string initiatorName = TTM_Utils.GetActorName(initiator)
    string partnerName = TTM_Utils.GetActorName(partner)
    string msg = ""
    if(isOpen)
        msg = initiatorName + " and " + partnerName + " opened their relationship to allow secondary romantic or casual partners."
    else
        msg = initiatorName + " and " + partnerName + " closed their open relationship, returning to exclusive monogamy."
    endif
    if(!skipTrigger)
        if(IsActionNarrationSuppressed())
            skipTrigger = true
        else
            skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(initiator, partner)
        endif
    endif
    string json = BuildJson("ttm_open_relationship_change", msg, skipTrigger = skipTrigger, parentA = initiatorName, parentB = partnerName, isOpen = isOpen, hasIsOpen = true)
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, initiator, partner)
    if(!skipTrigger)
        SuppressNextActionNarration(3.0)
    endif
EndFunction

Function RegisterMaritalHiearchyChangeEvent(Actor spouse, bool isDemoted) global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif
    string type = "ttm_spouse_hierarchy_change"
    string promotionKeyword = "promoted"
    if(isDemoted)
        promotionKeyword = "demoted"
    endif
    Actor player = TTM_Data.GetPlayer()
    string playerName = TTM_Utils.GetActorName(player)
    string spouseName = TTM_Utils.GetActorName(spouse)
    string msg = playerName + " has " + promotionKeyword + " " + spouseName + " in the household hierarchy."
    bool skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(player, spouse)
    string json = BuildJson("ttm_spouse_hierarchy_change", msg, skipTrigger = skipTrigger, isDemoted = isDemoted)
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, player, spouse)
EndFunction

Function RegisterMariatalAffectionChangeEvent(Actor spouse, string affectionLevel, string direction) global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif
    string type = "ttm_spouse_affection_change"
    Actor player = TTM_Data.GetPlayer()
    string playerName = TTM_Utils.GetActorName(player)
    string spouseName = TTM_Utils.GetActorName(spouse)
    string changeType = direction
    string msg = spouseName + "'s affection with " + playerName + " has " + changeType + " and "
    if(affectionLevel == "estranged")
        msg += " their relationship is on the verge of being broken."
    elseif(affectionLevel == "troubled")
        msg += " their relationship is in bad shape."
    elseif(affectionLevel == "content")
        msg += " their relationship is in a good shape."
    elseif(affectionLevel == "happy")
        msg += " they can't wish for a better relationship."
    endif
    bool skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(spouse, player)
    string json = BuildJson("ttm_spouse_affection_change", msg, skipTrigger = skipTrigger, affectionLevel = affectionLevel, affectionDirection = direction)
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, spouse, player)
EndFunction

Function RegisterProposalDecision(Actor initiator, Actor acceptor, string status, string decision, string reasoning = "", bool skipTrigger = true) global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif

    string msg = TTM_Utils.GetActorName(initiator) + " proposed "

    if(status == "dating")
        msg += "a dating relationship"
    elseif(status == "engaged")
        msg += "an engagement relationship"
    elseif(status == "lover")
        msg += "a lover relationship"
    endif

    msg += " to " + TTM_Utils.GetActorName(acceptor) + "."

    if(decision == "accept")
        msg += " " + TTM_Utils.GetActorName(acceptor) + " accepted the proposal."
    elseif(decision == "reject")
        msg += " " + TTM_Utils.GetActorName(acceptor) + " rejected the proposal. "
    endif

    if(!skipTrigger)
        if(IsActionNarrationSuppressed())
            skipTrigger = true
        else
            skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(initiator, acceptor)
        endif
    endif
    string json = BuildJson("ttm_dating_proposal", msg, skipTrigger = skipTrigger)
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, initiator, acceptor)
EndFunction

Function RegisterActionDeclineEvent(string json, Actor akSpeaker, Actor akTarget = none) global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, akSpeaker, akTarget)
EndFunction

Function RegisterActionDecline(Actor initiator, Actor acceptor, string actionType, string msg, bool skipTrigger = true) global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif
    if(!skipTrigger)
        skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(initiator, acceptor)
    endif
    string json = BuildJson("ttm_action_decline", msg, skipTrigger = skipTrigger, declinedAction = actionType)
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, initiator, acceptor)
EndFunction

Function RegisterChildAdopted(Actor child, Actor parentA, Actor parentB = none) global
    if(!TTM_Data.GetHasSkyrimNet() || child == none)
        return
    endif
    string childName = TTM_Utils.GetActorName(child)
    string parentAName = TTM_Utils.GetActorName(parentA)
    string msg = ""
    if(parentB != none)
        string parentBName = TTM_Utils.GetActorName(parentB)
        msg = parentAName + " and " + parentBName + " have adopted " + childName + ". " + childName + " is now part of their family."
    else
        msg = parentAName + " has adopted " + childName + ". " + childName + " is now part of " + parentAName + "'s family."
    endif
    bool pAPresent = (parentA != none && MARAS.ShouldTriggerSkyrimNetNarration(parentA, none))
    bool pBPresent = (parentB != none && parentB != parentA && MARAS.ShouldTriggerSkyrimNetNarration(parentB, none))
    Actor speaker = parentA
    Actor target = none
    bool skipTrigger = true
    if(pAPresent && pBPresent)
        speaker = parentA
        target = parentB
        skipTrigger = false
    elseif(pAPresent)
        speaker = parentA
        if(MARAS.ShouldTriggerSkyrimNetNarration(parentA, child))
            target = child
        endif
        skipTrigger = false
    elseif(pBPresent)
        speaker = parentB
        if(MARAS.ShouldTriggerSkyrimNetNarration(parentB, child))
            target = child
        endif
        skipTrigger = false
    else
        if(speaker == none)
            speaker = parentB
        endif
        if(parentB != none && parentB != speaker)
            target = parentB
        else
            target = child
        endif
    endif
    string json = BuildJson("ttm_child_adopted", msg, skipTrigger = skipTrigger, child = childName, parentA = parentAName, parentB = TTM_Utils.GetActorName(parentB))
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, target)
EndFunction

Function RegisterChildInherited(Actor child, Actor deceasedParent, Actor newParent, Actor coParent = none) global
    if(!TTM_Data.GetHasSkyrimNet() || child == none)
        return
    endif
    string childName = TTM_Utils.GetActorName(child)
    string deceasedName = TTM_Utils.GetActorName(deceasedParent)
    string newParentName = TTM_Utils.GetActorName(newParent)
    string msg = ""
    if(newParent != none && coParent != none && newParent != coParent)
        string coParentName = TTM_Utils.GetActorName(coParent)
        msg = "Following the death of " + deceasedName + ", " + newParentName + " has taken custody of " + childName + " and inherited parental responsibilities alongside " + coParentName + "."
    else
        msg = "Following the death of " + deceasedName + ", " + newParentName + " has become the sole guardian of " + childName + "."
    endif
    bool pAPresent = (newParent != none && MARAS.ShouldTriggerSkyrimNetNarration(newParent, none))
    bool pBPresent = (coParent != none && coParent != newParent && MARAS.ShouldTriggerSkyrimNetNarration(coParent, none))
    Actor speaker = newParent
    Actor target = none
    bool skipTrigger = true
    if(pAPresent && pBPresent)
        speaker = newParent
        target = coParent
        skipTrigger = false
    elseif(pAPresent)
        speaker = newParent
        if(MARAS.ShouldTriggerSkyrimNetNarration(newParent, child))
            target = child
        endif
        skipTrigger = false
    elseif(pBPresent)
        speaker = coParent
        if(MARAS.ShouldTriggerSkyrimNetNarration(coParent, child))
            target = child
        endif
        skipTrigger = false
    else
        if(speaker == none)
            speaker = coParent
        endif
        if(coParent != none && coParent != speaker)
            target = coParent
        else
            target = child
        endif
    endif
    string json = BuildJson("ttm_child_inherited", msg, skipTrigger = skipTrigger, child = childName, parentA = newParentName, parentB = TTM_Utils.GetActorName(coParent), reason = "spouse_death")
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, target)
EndFunction

Function RegisterChildParentsChanged(Actor child, Actor parentA, Actor parentB = none, string reason = "marriage") global
    if(!TTM_Data.GetHasSkyrimNet() || child == none)
        return
    endif
    string childName = TTM_Utils.GetActorName(child)
    string parentAName = TTM_Utils.GetActorName(parentA)
    string msg = ""
    if(reason == "marriage")
        string spouseName = TTM_Utils.GetActorName(parentB)
        msg = "With " + parentAName + " and " + spouseName + " getting married, " + spouseName + " has become a parent to " + childName + "."
    elseif(reason == "divorce")
        msg = "Following the divorce, " + childName + " is now in the sole custody of " + parentAName + "."
    else
        msg = "The parental custody of " + childName + " has been updated to " + parentAName
        if(parentB != none)
            msg += " and " + TTM_Utils.GetActorName(parentB) + "."
        else
            msg += " as sole guardian."
        endif
    endif
    bool isCascade = (reason == "marriage" || reason == "divorce" || IsActionNarrationSuppressed())
    bool pAPresent = (parentA != none && MARAS.ShouldTriggerSkyrimNetNarration(parentA, none))
    bool pBPresent = (parentB != none && parentB != parentA && MARAS.ShouldTriggerSkyrimNetNarration(parentB, none))
    Actor speaker = parentA
    Actor target = none
    bool skipTrigger = true
    if(pAPresent && pBPresent)
        speaker = parentA
        target = parentB
        skipTrigger = isCascade
    elseif(pAPresent)
        speaker = parentA
        if(MARAS.ShouldTriggerSkyrimNetNarration(parentA, child))
            target = child
        endif
        skipTrigger = isCascade
    elseif(pBPresent)
        speaker = parentB
        if(MARAS.ShouldTriggerSkyrimNetNarration(parentB, child))
            target = child
        endif
        skipTrigger = isCascade
    else
        if(speaker == none)
            speaker = parentB
        endif
        if(parentB != none && parentB != speaker)
            target = parentB
        else
            target = child
        endif
    endif
    string json = BuildJson("ttm_child_parents_changed", msg, skipTrigger = skipTrigger, child = childName, parentA = parentAName, parentB = TTM_Utils.GetActorName(parentB), reason = reason)
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, target)
EndFunction

Function RegisterPregnancyAnnounced(Actor mother, Actor partner = none, Actor bioFather = none, string bioFatherName = "", int childCount = 1) global
    if (!TTM_Data.GetHasSkyrimNet() || mother == none)
        return
    endif

    string json = MARAS.BuildPregnancyAnnouncedJson(mother, partner, bioFather, bioFatherName, childCount)
    if (json != "")
        MARAS.RegisterSkyrimNetEvent("ttm_event", json, mother, none, true)
    endif
EndFunction

Function RegisterPregnancyLost(Actor mother, Actor partner = none, string reason = "miscarriage") global
    if(!TTM_Data.GetHasSkyrimNet() || mother == none)
        return
    endif
    string motherName = TTM_Utils.GetActorName(mother)
    string partnerName = ""
    if(partner != none)
        partnerName = TTM_Utils.GetActorName(partner)
    endif
    string msg = motherName + " has suffered a tragic pregnancy loss (" + reason + ")."
    bool skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(mother, none)
    Actor eventTarget = partner
    if(eventTarget != none && !MARAS.ShouldTriggerSkyrimNetNarration(mother, eventTarget))
        eventTarget = none
    endif
    string json = BuildJson("ttm_pregnancy_lost", msg, skipTrigger = skipTrigger, parentA = motherName, parentB = partnerName, reason = reason)
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, mother, eventTarget)
EndFunction

Function RegisterChildBirthBatch(Actor mother, int[] childIDs = none) global
    if (!TTM_Data.GetHasSkyrimNet() || mother == none)
        return
    endif

    string json = MARAS.BuildChildBirthJson(mother, childIDs, false)
    if (json == "")
        return
    endif

    Actor partner = MARAS.GetPrimaryPartner(mother)
    if (partner == none && childIDs.Length > 0)
        partner = MARAS.GetMotherPartner(childIDs[0])
    endif
    if (partner == mother)
        partner = none
    endif

    bool anySecretForPartner = false
    bool anySecretForBio = false
    Actor awareBioFather = none
    Actor firstBioFather = none
    int i = 0
    while (i < childIDs.Length)
        int cID = childIDs[i]
        if (cID != 0)
            Actor bFather = MARAS.GetBioFather(cID)
            if (firstBioFather == none && bFather != none)
                firstBioFather = bFather
            endif
            bool secP = MARAS.IsSecretForPartner(cID)
            bool secB = MARAS.IsSecretForBioFather(cID)
            if (secP)
                anySecretForPartner = true
            endif
            if (secB)
                anySecretForBio = true
            elseif (bFather != none && bFather != partner && awareBioFather == none)
                awareBioFather = bFather
            endif
        endif
        i += 1
    endWhile

    if (!anySecretForPartner && !anySecretForBio)
        Actor publicTarget = partner
        if (publicTarget == none)
            publicTarget = firstBioFather
        endif
        MARAS.RegisterSkyrimNetEvent("ttm_event", json, mother, publicTarget, false)
        return
    endif

    Actor privyPartner = none
    if (!anySecretForPartner)
        privyPartner = partner
    endif
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, mother, none, true, MakeAudience3(mother, privyPartner, awareBioFather))

    if (anySecretForPartner && partner != none)
        string maskedJson = MARAS.BuildChildBirthJson(mother, childIDs, true)
        if (maskedJson != "")
            MARAS.RegisterSkyrimNetEvent("ttm_event", maskedJson, partner, none, true, MakeAudience2(partner, none))
        endif
    endif
EndFunction

Function RegisterChildBirth(int childID, string childName = "") global
    if(!TTM_Data.GetHasSkyrimNet() || childID == 0)
        return
    endif

    Actor mother = MARAS.GetBioMother(childID)
    if (mother != none)
        int[] childIDs = new int[1]
        childIDs[0] = childID
        RegisterChildBirthBatch(mother, childIDs)
        return
    endif

    if(childName == "")
        childName = MARAS.GetChildName(childID)
    endif
    bool hasGivenName = (childName != "" && childName != "Newborn")
    if(!hasGivenName)
        childName = "child"
    endif
    Actor presumedFather = MARAS.GetMotherPartner(childID)
    Actor bioFather = MARAS.GetBioFather(childID)
    bool isSecretForAssumed = MARAS.IsSecretForPartner(childID)
    bool isSecretForBio = MARAS.IsSecretForBioFather(childID)

    if (presumedFather == mother || presumedFather == none)
        presumedFather = none
        isSecretForAssumed = false
    endif

    string motherName = TTM_Utils.GetActorName(mother)
    string bioFatherName = TTM_Utils.GetActorName(bioFather)
    string presumedFatherName = TTM_Utils.GetActorName(presumedFather)
    int childGender = MARAS.GetChildGender(childID)
    string childRelation = "child"
    string childGenderStr = "unknown"
    if (childGender == 0)
        childRelation = "son"
        childGenderStr = "male"
    elseif (childGender == 1)
        childRelation = "daughter"
        childGenderStr = "female"
    endif
    string msg = ""
    string nameClause = ""
    if(hasGivenName)
        nameClause = ", " + childName + ","
    endif
    string birthTarget = "a newborn " + childRelation
    if(hasGivenName)
        birthTarget = childName
    endif

    if(isSecretForAssumed && isSecretForBio && presumedFather != none)
        msg = motherName + " gave birth to " + birthTarget + ". " + presumedFatherName + " believes the newborn " + childRelation + " is his, unaware that the true biological father is " + bioFatherName + ". Furthermore, " + bioFatherName + " has not been informed that he fathered the child."
    elseif(isSecretForAssumed && presumedFather != none)
        msg = motherName + " gave birth to " + birthTarget + ". " + presumedFatherName + " believes the newborn " + childRelation + " is his, unaware that the true biological father is " + bioFatherName + "."
    elseif(isSecretForBio && bioFather != none)
        if(hasGivenName)
            msg = motherName + " gave birth to " + childName + ", a " + childRelation + " fathered by " + bioFatherName + ", though " + bioFatherName + " is unaware of the child."
        else
            msg = motherName + " gave birth to a " + childRelation + " fathered by " + bioFatherName + ", though " + bioFatherName + " is unaware of the child."
        endif
    elseif(bioFather != none)
        msg = motherName + " gave birth to a healthy " + childRelation + nameClause + " fathered by " + bioFatherName + "."
    else
        if(hasGivenName)
            msg = motherName + " gave birth to a newborn " + childRelation + ", " + childName + "."
        else
            msg = motherName + " gave birth to a newborn " + childRelation + "."
        endif
    endif

    bool skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(mother, none)
    string json = BuildJson("ttm_child_birth", msg, skipTrigger = skipTrigger, child = childName, parentA = motherName, parentB = presumedFatherName, bioFather = bioFatherName, presumedFather = presumedFatherName, gender = childGenderStr, relation = childRelation)
    Actor speaker = mother
    if(speaker == none)
        speaker = presumedFather
    endif
    if(speaker == none)
        speaker = TTM_Data.GetPlayer()
    endif
    if (isSecretForAssumed && presumedFather != none)
        Actor awareBio = bioFather
        if (isSecretForBio)
            awareBio = none
        endif
        if (mother != none || awareBio != none)
            MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, none, true, MakeAudience2(mother, awareBio))
        endif
        string maskedMsg = motherName + " gave birth to a healthy " + childRelation + nameClause + " fathered by " + presumedFatherName + "."
        string maskedJson = BuildJson("ttm_child_birth", maskedMsg, skipTrigger = true, child = childName, parentA = motherName, parentB = presumedFatherName, bioFather = presumedFatherName, presumedFather = presumedFatherName, gender = childGenderStr, relation = childRelation)
        MARAS.RegisterSkyrimNetEvent("ttm_event", maskedJson, presumedFather, none, true, MakeAudience2(presumedFather, none))
    elseif (isSecretForBio)
        MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, none, true, MakeAudience2(mother, presumedFather))
    else
        MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, presumedFather, false)
    endif
EndFunction

Function RegisterChildGrewUp(int childID, string childName = "") global
    if(!TTM_Data.GetHasSkyrimNet() || childID == 0)
        return
    endif
    if(childName == "")
        childName = MARAS.GetChildName(childID)
    endif
    Actor mother = MARAS.GetBioMother(childID)
    Actor presumedFather = MARAS.GetMotherPartner(childID)
    Actor bioFather = MARAS.GetBioFather(childID)
    bool isSecretForAssumed = MARAS.IsSecretForPartner(childID)
    bool isSecretForBio = MARAS.IsSecretForBioFather(childID)

    if (presumedFather == mother || presumedFather == none)
        presumedFather = none
        isSecretForAssumed = false
    endif

    string motherName = TTM_Utils.GetActorName(mother)
    string presumedFatherName = TTM_Utils.GetActorName(presumedFather)
    string bioFatherName = TTM_Utils.GetActorName(bioFather)
    int childGender = MARAS.GetChildGender(childID)
    string childRelation = "child"
    string childGenderStr = "unknown"
    if (childGender == 0)
        childRelation = "son"
        childGenderStr = "male"
    elseif (childGender == 1)
        childRelation = "daughter"
        childGenderStr = "female"
    endif
    string msg = ""

    if(isSecretForAssumed && isSecretForBio && presumedFather != none)
        msg = childName + " has grown from infancy into a young " + childRelation + ". " + presumedFatherName + " continues to raise " + childName + " believing the " + childRelation + " is his, unaware that the true biological father is " + bioFatherName + ". Furthermore, " + bioFatherName + " has not been informed that he fathered the child."
    elseif(isSecretForAssumed && presumedFather != none)
        msg = childName + " has grown from infancy into a young " + childRelation + ". " + presumedFatherName + " continues to raise " + childName + " as his own " + childRelation + ", completely unaware that " + bioFatherName + " is the true biological father."
    elseif(isSecretForBio && bioFather != none)
        msg = childName + " has grown from infancy into a young " + childRelation + ", raised by " + motherName + ", while the biological father " + bioFatherName + " remains unaware of the " + childRelation + "."
    elseif(motherName != "" && presumedFatherName != "")
        msg = motherName + " and " + presumedFatherName + "'s " + childRelation + ", " + childName + ", has grown from infancy into a young child."
    elseif(motherName != "")
        msg = motherName + "'s " + childRelation + ", " + childName + ", has grown from infancy into a young child."
    else
        msg = childName + " has grown from infancy into a young " + childRelation + "."
    endif

    Actor childActor = Game.GetForm(childID) as Actor
    bool shouldTrigger = (childActor != none && MARAS.ShouldTriggerSkyrimNetNarration(childActor, none)) || (mother != none && MARAS.ShouldTriggerSkyrimNetNarration(mother, none))
    bool skipTrigger = !shouldTrigger
    string json = BuildJson("ttm_child_grew_up", msg, skipTrigger = skipTrigger, child = childName, parentA = motherName, parentB = presumedFatherName, bioFather = bioFatherName, presumedFather = presumedFatherName, gender = childGenderStr, relation = childRelation)
    Actor speaker = mother
    if(speaker == none)
        speaker = presumedFather
    endif
    if(speaker == none)
        speaker = TTM_Data.GetPlayer()
    endif
    Actor awareBio = bioFather
    if(isSecretForBio)
        awareBio = none
    endif
    if(isSecretForAssumed && presumedFather != none)
        if(mother != none || awareBio != none)
            MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, none, true, MakeAudience2(mother, awareBio))
        endif
        string maskedMsg = ""
        if(motherName != "" && presumedFatherName != "")
            maskedMsg = motherName + " and " + presumedFatherName + "'s " + childRelation + ", " + childName + ", has grown from infancy into a young child."
        else
            maskedMsg = childName + " has grown from infancy into a young " + childRelation + "."
        endif
        string maskedJson = BuildJson("ttm_child_grew_up", maskedMsg, skipTrigger = true, child = childName, parentA = motherName, parentB = presumedFatherName, bioFather = presumedFatherName, presumedFather = presumedFatherName, gender = childGenderStr, relation = childRelation)
        MARAS.RegisterSkyrimNetEvent("ttm_event", maskedJson, presumedFather, none, true, MakeAudience2(presumedFather, childActor))
    elseif(isSecretForBio)
        MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, none, true, MakeAudience3(mother, presumedFather, childActor))
    else
        MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, childActor, false)
    endif
EndFunction

Function RegisterChildSentToTraining(int childID, string childName = "", string trainingLocation = "") global
    if(!TTM_Data.GetHasSkyrimNet() || childID == 0)
        return
    endif
    if(childName == "")
        childName = MARAS.GetChildName(childID)
    endif
    Actor mother = MARAS.GetBioMother(childID)
    Actor presumedFather = MARAS.GetMotherPartner(childID)
    Actor bioFather = MARAS.GetBioFather(childID)
    bool isSecretForAssumed = MARAS.IsSecretForPartner(childID)
    bool isSecretForBio = MARAS.IsSecretForBioFather(childID)

    if (presumedFather == mother || presumedFather == none)
        presumedFather = none
        isSecretForAssumed = false
    endif

    string motherName = TTM_Utils.GetActorName(mother)
    string presumedFatherName = TTM_Utils.GetActorName(presumedFather)
    string bioFatherName = TTM_Utils.GetActorName(bioFather)
    int childGender = MARAS.GetChildGender(childID)
    string childRelation = "child"
    string childGenderStr = "unknown"
    string possPronoun = "their"
    if (childGender == 0)
        childRelation = "son"
        childGenderStr = "male"
        possPronoun = "his"
    elseif (childGender == 1)
        childRelation = "daughter"
        childGenderStr = "female"
        possPronoun = "her"
    endif
    string locText = ""
    if(trainingLocation != "")
        locText = " at " + trainingLocation
    endif
    string msg = ""

    if(isSecretForAssumed && isSecretForBio && presumedFather != none)
        msg = childName + " has been sent away from home for training" + locText + ". " + presumedFatherName + " continues to support " + possPronoun + " " + childRelation + " believing the child is his, unaware that the true biological father is " + bioFatherName + ". Furthermore, " + bioFatherName + " has not been informed that he fathered the child."
    elseif(isSecretForAssumed && presumedFather != none)
        msg = childName + " has been sent away from home for training" + locText + ". " + presumedFatherName + " continues to support " + possPronoun + " " + childRelation + " as his own, completely unaware that " + bioFatherName + " is the true biological father."
    elseif(isSecretForBio && bioFather != none)
        msg = childName + " has been sent away from home for training" + locText + ", arranged by " + motherName + ", while the biological father " + bioFatherName + " remains unaware of his " + childRelation + "."
    elseif(motherName != "" && presumedFatherName != "")
        msg = motherName + " and " + presumedFatherName + " have sent their " + childRelation + ", " + childName + ", away from home for training" + locText + "."
    elseif(motherName != "")
        msg = motherName + " has sent her " + childRelation + ", " + childName + ", away from home for training" + locText + "."
    elseif(presumedFatherName != "")
        msg = presumedFatherName + " has sent his " + childRelation + ", " + childName + ", away from home for training" + locText + "."
    else
        msg = childName + " has been sent away from home for training" + locText + "."
    endif

    bool skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(mother, none)
    string json = BuildJson("ttm_child_sent_to_training", msg, skipTrigger = skipTrigger, child = childName, parentA = motherName, parentB = presumedFatherName, bioFather = bioFatherName, presumedFather = presumedFatherName, trainingLocation = trainingLocation, gender = childGenderStr, relation = childRelation)
    Actor speaker = mother
    if(speaker == none)
        speaker = presumedFather
    endif
    if(speaker == none)
        speaker = TTM_Data.GetPlayer()
    endif
    Actor awareBio = bioFather
    if(isSecretForBio)
        awareBio = none
    endif
    if(isSecretForAssumed && presumedFather != none)
        if(mother != none || awareBio != none)
            MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, none, true, MakeAudience2(mother, awareBio))
        endif
        string maskedMsg = ""
        if(motherName != "" && presumedFatherName != "")
            maskedMsg = motherName + " and " + presumedFatherName + " have sent their " + childRelation + ", " + childName + ", away from home for training" + locText + "."
        elseif(presumedFatherName != "")
            maskedMsg = presumedFatherName + " has sent his " + childRelation + ", " + childName + ", away from home for training" + locText + "."
        else
            maskedMsg = childName + " has been sent away from home for training" + locText + "."
        endif
        string maskedJson = BuildJson("ttm_child_sent_to_training", maskedMsg, skipTrigger = true, child = childName, parentA = motherName, parentB = presumedFatherName, bioFather = presumedFatherName, presumedFather = presumedFatherName, trainingLocation = trainingLocation, gender = childGenderStr, relation = childRelation)
        MARAS.RegisterSkyrimNetEvent("ttm_event", maskedJson, presumedFather, none, true, MakeAudience2(presumedFather, none))
    elseif(isSecretForBio)
        MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, none, true, MakeAudience2(mother, presumedFather))
    else
        MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, presumedFather, false)
    endif
EndFunction

Function RegisterChildBecameAdult(int childID, string childName = "") global
    if(!TTM_Data.GetHasSkyrimNet() || childID == 0)
        return
    endif
    if(childName == "")
        childName = MARAS.GetChildName(childID)
    endif
    Actor mother = MARAS.GetBioMother(childID)
    Actor presumedFather = MARAS.GetMotherPartner(childID)
    Actor bioFather = MARAS.GetBioFather(childID)
    bool isSecretForAssumed = MARAS.IsSecretForPartner(childID)
    bool isSecretForBio = MARAS.IsSecretForBioFather(childID)

    if (presumedFather == mother || presumedFather == none)
        presumedFather = none
        isSecretForAssumed = false
    endif

    string motherName = TTM_Utils.GetActorName(mother)
    string presumedFatherName = TTM_Utils.GetActorName(presumedFather)
    string bioFatherName = TTM_Utils.GetActorName(bioFather)
    int childGender = MARAS.GetChildGender(childID)
    string childRelation = "child"
    string childGenderStr = "unknown"
    string possPronoun = "their"
    if (childGender == 0)
        childRelation = "son"
        childGenderStr = "male"
        possPronoun = "his"
    elseif (childGender == 1)
        childRelation = "daughter"
        childGenderStr = "female"
        possPronoun = "her"
    endif
    string msg = ""

    if(isSecretForAssumed && isSecretForBio && presumedFather != none)
        msg = childName + " has completed " + possPronoun + " training and grown into an adult. " + presumedFatherName + " believes " + childName + " is his adult " + childRelation + ", unaware that the true biological father is " + bioFatherName + ". Furthermore, " + bioFatherName + " has not been informed that he fathered the child."
    elseif(isSecretForAssumed && presumedFather != none)
        msg = childName + " has completed " + possPronoun + " training and grown into an adult. " + presumedFatherName + " proudly sees " + childName + " come of age as his own " + childRelation + ", completely unaware that " + bioFatherName + " is the true biological father."
    elseif(isSecretForBio && bioFather != none)
        msg = childName + " has completed " + possPronoun + " training and grown into an adult, raised by " + motherName + ", while the biological father " + bioFatherName + " remains unaware of his adult " + childRelation + "."
    elseif(motherName != "" && presumedFatherName != "")
        msg = motherName + " and " + presumedFatherName + "'s " + childRelation + ", " + childName + ", has completed " + possPronoun + " training and come of age as an adult."
    elseif(motherName != "")
        msg = motherName + "'s " + childRelation + ", " + childName + ", has completed " + possPronoun + " training and come of age as an adult."
    elseif(presumedFatherName != "")
        msg = presumedFatherName + "'s " + childRelation + ", " + childName + ", has completed " + possPronoun + " training and come of age as an adult."
    else
        msg = childName + " has completed " + possPronoun + " training and come of age as an adult."
    endif

    Actor adultActor = Game.GetForm(childID) as Actor
    bool shouldTrigger = (adultActor != none && MARAS.ShouldTriggerSkyrimNetNarration(adultActor, none)) || (mother != none && MARAS.ShouldTriggerSkyrimNetNarration(mother, none))
    bool skipTrigger = !shouldTrigger
    string json = BuildJson("ttm_child_became_adult", msg, skipTrigger = skipTrigger, child = childName, parentA = motherName, parentB = presumedFatherName, bioFather = bioFatherName, presumedFather = presumedFatherName, gender = childGenderStr, relation = childRelation)
    Actor speaker = mother
    if(speaker == none)
        speaker = presumedFather
    endif
    if(speaker == none)
        speaker = TTM_Data.GetPlayer()
    endif
    Actor awareBio = bioFather
    if(isSecretForBio)
        awareBio = none
    endif
    if(isSecretForAssumed && presumedFather != none)
        if(mother != none || awareBio != none)
            MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, none, true, MakeAudience2(mother, awareBio))
        endif
        string maskedMsg = ""
        if(motherName != "" && presumedFatherName != "")
            maskedMsg = motherName + " and " + presumedFatherName + "'s " + childRelation + ", " + childName + ", has completed " + possPronoun + " training and come of age as an adult."
        elseif(presumedFatherName != "")
            maskedMsg = presumedFatherName + "'s " + childRelation + ", " + childName + ", has completed " + possPronoun + " training and come of age as an adult."
        else
            maskedMsg = childName + " has completed " + possPronoun + " training and come of age as an adult."
        endif
        string maskedJson = BuildJson("ttm_child_became_adult", maskedMsg, skipTrigger = true, child = childName, parentA = motherName, parentB = presumedFatherName, bioFather = presumedFatherName, presumedFather = presumedFatherName, gender = childGenderStr, relation = childRelation)
        MARAS.RegisterSkyrimNetEvent("ttm_event", maskedJson, presumedFather, none, true, MakeAudience2(presumedFather, adultActor))
    elseif(isSecretForBio)
        MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, none, true, MakeAudience3(mother, presumedFather, adultActor))
    else
        MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, adultActor, false)
    endif
EndFunction


;/
  RegisterPaternitySecretEvent:
  Dispatches a SkyrimNet event for paternity secret resolution.
  Handles canonical cases:
  - "mother_confessed_bio"
  - "mother_confessed_presumed"
  - "mother_confessed_both"
  - "bio_father_learnt"
  - "presumed_father_learnt"
  - "mother_shared_pregnancy_presumed"
  - "pregnancy_visible_presumed"
  - "divorce_revealed"
/;
Function RegisterPaternitySecretEvent(int childID, string caseType, string customSummary = "") global
    if(!TTM_Data.GetHasSkyrimNet() || childID == 0)
        return
    endif

    bool wasPregUnannounced = false
    if(caseType == "mother_confessed_presumed_unannounced")
        caseType = "mother_confessed_presumed"
        wasPregUnannounced = true
    elseif(caseType == "presumed_father_learnt_unannounced")
        caseType = "presumed_father_learnt"
        wasPregUnannounced = true
    elseif(caseType == "mother_confessed_both_unannounced")
        caseType = "mother_confessed_both"
        wasPregUnannounced = true
    endif

    Actor mother = MARAS.GetBioMother(childID)
    Actor presumedFather = MARAS.GetMotherPartner(childID)
    Actor bioFather = MARAS.GetBioFather(childID)

    bool isUnborn = (MARAS.GetChildStage(childID) == 0)
    string childName = MARAS.GetChildName(childID)
    if(isUnborn)
        childName = "unborn child"
    elseif(childName == "" || childName == "Newborn")
        childName = "the child"
    endif

    string motherName = TTM_Utils.GetActorName(mother)
    string presumedFatherName = TTM_Utils.GetActorName(presumedFather)
    string bioFatherName = TTM_Utils.GetActorName(bioFather)
    if(bioFatherName == "")
        bioFatherName = "another man"
    endif

    bool stillSecretAssumed = MARAS.IsSecretForPartner(childID)
    bool stillSecretBio = MARAS.IsSecretForBioFather(childID)
    bool stillPregSecretAssumed = MARAS.IsPregnancySecretForPartner(childID)

    if (presumedFather == mother || presumedFather == none)
        presumedFather = none
        if (presumedFatherName == "" || presumedFatherName == motherName)
            presumedFatherName = "her former husband"
        endif
        stillSecretAssumed = false
    endif

    string msg = ""
    Actor speaker = none
    Actor target = none

    ; =========================================================================
    ; 1. MOTHER CONFESSED TO BIO FATHER
    ; =========================================================================
    if(caseType == "mother_confessed_bio")
        if(isUnborn)
            if(stillSecretAssumed)
                if(stillPregSecretAssumed)
                    msg = motherName + " confessed to " + bioFatherName + " that she is pregnant with his child. However, her partner " + presumedFatherName + " does not even know she is pregnant yet."
                else
                    msg = motherName + " confessed to " + bioFatherName + " that she is pregnant with his child. However, her partner " + presumedFatherName + " does not know the truth and believes the unborn baby is his."
                endif
            elseif(presumedFather != none)
                msg = motherName + " confessed to " + bioFatherName + " that she is pregnant with his child. Both " + presumedFatherName + " and " + bioFatherName + " now know the truth."
            else
                msg = motherName + " confessed to " + bioFatherName + " that she is pregnant with his child."
            endif
        else
            if(stillSecretAssumed)
                msg = motherName + " confessed to " + bioFatherName + " that he is the true biological father of " + childName + ". However, " + presumedFatherName + " does not know and still believes the child is his."
            else
                msg = motherName + " confessed to " + bioFatherName + " that he is the true biological father of " + childName + ". Both " + presumedFatherName + " and " + bioFatherName + " now know the truth."
            endif
        endif
        speaker = mother
        target = bioFather

    ; =========================================================================
    ; 2. MOTHER CONFESSED TO PRESUMED FATHER
    ; =========================================================================
    elseif(caseType == "mother_confessed_presumed")
        if(isUnborn)
            if(wasPregUnannounced)
                if(stillSecretBio)
                    msg = motherName + " confessed to her partner " + presumedFatherName + " that she is pregnant, and that the baby was fathered by " + bioFatherName + ", not him (" + presumedFatherName + " did not previously know she was pregnant). However, " + bioFatherName + " has not been told yet."
                else
                    msg = motherName + " confessed to her partner " + presumedFatherName + " that she is pregnant, and that the baby was fathered by " + bioFatherName + ", not him (" + presumedFatherName + " did not previously know she was pregnant). Both " + presumedFatherName + " and " + bioFatherName + " now know the truth."
                endif
            else
                if(stillSecretBio)
                    msg = motherName + " confessed to " + presumedFatherName + " that the unborn baby she is carrying (which he believed was his) was actually fathered by " + bioFatherName + ", not him. However, " + bioFatherName + " does not know that he is the biological father."
                else
                    msg = motherName + " confessed to " + presumedFatherName + " that the unborn baby she is carrying (which he believed was his) was actually fathered by " + bioFatherName + ", not him. Both " + presumedFatherName + " and " + bioFatherName + " now know the truth."
                endif
            endif
        else
            if(stillSecretBio)
                msg = motherName + " confessed to " + presumedFatherName + " that " + childName + " was biologically fathered by " + bioFatherName + ", not him. However, " + bioFatherName + " does not know that he is the biological father."
            else
                msg = motherName + " confessed to " + presumedFatherName + " that " + childName + " was biologically fathered by " + bioFatherName + ", not him. Both " + presumedFatherName + " and " + bioFatherName + " now know the truth."
            endif
        endif
        speaker = mother
        target = presumedFather

    ; =========================================================================
    ; 3. MOTHER CONFESSED TO BOTH
    ; =========================================================================
    elseif(caseType == "mother_confessed_both")
        if(isUnborn)
            if(wasPregUnannounced)
                msg = motherName + " revealed in front of both her partner " + presumedFatherName + " and " + bioFatherName + " that she is pregnant with " + bioFatherName + "'s child. " + presumedFatherName + " did not previously know she was pregnant and has just learned both that she is expecting and that " + bioFatherName + " is the father."
            else
                msg = motherName + " revealed to both her partner " + presumedFatherName + " and " + bioFatherName + " that the unborn baby she is carrying was fathered by " + bioFatherName + ", not " + presumedFatherName + "."
            endif
        else
            msg = motherName + " has revealed the truth of " + childName + "'s biological paternity: both " + presumedFatherName + " and " + bioFatherName + " now know that " + bioFatherName + " is the true biological father."
        endif
        speaker = mother
        target = none

    ; =========================================================================
    ; 4. BIO FATHER LEARNT
    ; =========================================================================
    elseif(caseType == "bio_father_learnt")
        if(isUnborn)
            if(stillSecretAssumed)
                if(stillPregSecretAssumed)
                    msg = bioFatherName + " has learned that " + motherName + " is pregnant with his child. However, her partner " + presumedFatherName + " does not even know she is pregnant yet."
                else
                    msg = bioFatherName + " has learned that " + motherName + " is pregnant with his child. However, her partner " + presumedFatherName + " does not know and still believes the unborn baby is his."
                endif
            else
                msg = bioFatherName + " has learned that " + motherName + " is pregnant with his child. Both " + presumedFatherName + " and " + bioFatherName + " are now aware of the true biological paternity."
            endif
        else
            if(stillSecretAssumed)
                msg = bioFatherName + " has learned that he is the true biological father of " + childName + " with " + motherName + ". However, " + presumedFatherName + " does not know and still believes the child is his."
            else
                msg = bioFatherName + " has learned that he is the true biological father of " + childName + " with " + motherName + ". Both " + presumedFatherName + " and " + bioFatherName + " are now aware of the true biological paternity."
            endif
        endif
        speaker = bioFather
        target = mother

    ; =========================================================================
    ; 5. PRESUMED FATHER LEARNT
    ; =========================================================================
    elseif(caseType == "presumed_father_learnt")
        if(isUnborn)
            if(wasPregUnannounced)
                if(stillSecretBio)
                    msg = presumedFatherName + " has discovered that his partner " + motherName + " is pregnant with a child fathered by " + bioFatherName + ", not him (" + presumedFatherName + " did not previously know she was pregnant). However, " + bioFatherName + " does not know yet."
                else
                    msg = presumedFatherName + " has discovered that his partner " + motherName + " is pregnant with a child fathered by " + bioFatherName + ", not him (" + presumedFatherName + " did not previously know she was pregnant). Both " + presumedFatherName + " and " + bioFatherName + " are now aware of the truth."
                endif
            else
                if(stillSecretBio)
                    msg = presumedFatherName + " has learned that the unborn baby " + motherName + " is carrying (which he believed was his) was actually fathered by " + bioFatherName + ", not him. However, " + bioFatherName + " does not know that he is the biological father."
                else
                    msg = presumedFatherName + " has learned that the unborn baby " + motherName + " is carrying (which he believed was his) was actually fathered by " + bioFatherName + ", not him. Both " + presumedFatherName + " and " + bioFatherName + " are now aware of the truth."
                endif
            endif
        else
            if(stillSecretBio)
                msg = presumedFatherName + " has learned that " + childName + " was biologically fathered by " + bioFatherName + ", not him. However, " + bioFatherName + " does not know that he is the biological father."
            else
                msg = presumedFatherName + " has learned that " + childName + " was biologically fathered by " + bioFatherName + ", not him. Both " + presumedFatherName + " and " + bioFatherName + " are now aware of the true biological paternity."
            endif
        endif
        speaker = presumedFather
        target = mother

    ; =========================================================================
    ; 6. MOTHER SHARED PREGNANCY (PARTNER PRESUMES PATERNITY)
    ; =========================================================================
    elseif(caseType == "mother_shared_pregnancy_presumed")
        if(stillSecretBio)
            msg = motherName + " shared the news of her pregnancy with her partner " + presumedFatherName + ", letting him believe the unborn baby is his while concealing that " + bioFatherName + " is the true biological father. " + bioFatherName + " still does not know about the pregnancy."
        else
            msg = motherName + " shared the news of her pregnancy with her partner " + presumedFatherName + ", letting him believe the unborn baby is his while concealing that " + bioFatherName + " is the true biological father (though " + bioFatherName + " already knows the truth)."
        endif
        speaker = mother
        target = presumedFather

    ; =========================================================================
    ; 7. PREGNANCY VISIBLY SHOWS (PARTNER PRESUMES PATERNITY)
    ; =========================================================================
    elseif(caseType == "pregnancy_visible_presumed")
        if(stillSecretBio)
            msg = motherName + "'s pregnancy has become visibly rounded and can no longer be hidden. Because she never confessed the affair, her partner " + presumedFatherName + " now knows she is pregnant and naturally assumes the unborn baby is his, unaware that " + bioFatherName + " is the true biological father."
        else
            msg = motherName + "'s pregnancy has become visibly rounded and can no longer be hidden. Her partner " + presumedFatherName + " now knows she is pregnant and assumes the unborn baby is his, unaware that " + bioFatherName + " is the true biological father (while " + bioFatherName + " already knows the truth)."
        endif
        speaker = presumedFather
        if(speaker == none)
            speaker = mother
        endif
        target = mother

    ; =========================================================================
    ; 8. DIVORCE REVEALED
    ; =========================================================================
    elseif(caseType == "divorce_revealed")
        msg = "Following " + motherName + "'s divorce, the paternity secret of " + childName + " has been resolved: " + bioFatherName + " is recognized as the true biological father."
        speaker = mother
        target = none

    ; =========================================================================
    ; FALLBACK: BOTH NOW KNOW
    ; =========================================================================
    else
        msg = "Both " + presumedFatherName + " and " + bioFatherName + " now know that " + bioFatherName + " is the true biological father of " + childName + "."
        speaker = bioFather
        if(speaker == none)
            speaker = presumedFather
        endif
        target = mother
    endif

    if(speaker == none)
        speaker = mother
    endif
    if(speaker == none)
        speaker = TTM_Data.GetPlayer()
    endif

    bool skipTrigger = true
    if(caseType == "divorce_revealed" || IsActionNarrationSuppressed())
        skipTrigger = true
    elseif(caseType == "mother_confessed_both")
        skipTrigger = !(MARAS.ShouldTriggerSkyrimNetNarration(mother, presumedFather) || MARAS.ShouldTriggerSkyrimNetNarration(mother, bioFather))
    elseif(caseType == "mother_confessed_bio")
        skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(mother, bioFather)
    elseif(caseType == "mother_confessed_presumed" || caseType == "mother_shared_pregnancy_presumed")
        skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(mother, presumedFather)
    elseif(caseType == "bio_father_learnt")
        skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(bioFather, none)
        if(target != none && !MARAS.ShouldTriggerSkyrimNetNarration(bioFather, target))
            target = none
        endif
    elseif(caseType == "presumed_father_learnt")
        skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(presumedFather, none)
        if(target != none && !MARAS.ShouldTriggerSkyrimNetNarration(presumedFather, target))
            target = none
        endif
    elseif(caseType == "pregnancy_visible_presumed")
        Actor pSpeaker = presumedFather
        if(pSpeaker == none)
            pSpeaker = mother
        endif
        skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(pSpeaker, mother)
    else
        Actor fSpeaker = bioFather
        if(fSpeaker == none)
            fSpeaker = presumedFather
        endif
        skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(fSpeaker, mother)
    endif

    string json = BuildJson("ttm_paternity_secret_resolved", msg, skipTrigger = skipTrigger, child = childName, parentA = motherName, parentB = presumedFatherName, bioFather = bioFatherName, presumedFather = presumedFatherName, paternityOutcome = caseType)
    if(caseType == "mother_confessed_bio")
        if(stillSecretAssumed)
            MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, target, true, MakeAudience2(mother, bioFather))
        else
            MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, target, false)
        endif
    elseif(caseType == "mother_confessed_presumed")
        if(stillSecretBio)
            MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, target, true, MakeAudience2(mother, presumedFather))
        else
            MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, target, false)
        endif
    elseif(caseType == "mother_confessed_both")
        MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, target, false)
    elseif(caseType == "bio_father_learnt")
        if(stillSecretAssumed)
            MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, target, true, MakeAudience2(bioFather, mother))
        else
            MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, target, false)
        endif
    elseif(caseType == "presumed_father_learnt")
        if(stillSecretBio)
            MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, target, true, MakeAudience2(presumedFather, mother))
        else
            MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, target, false)
        endif
    elseif(caseType == "mother_shared_pregnancy_presumed")
        Actor awareBio = bioFather
        if(stillSecretBio)
            awareBio = none
        endif
        bool skipTriggerMother = skipTrigger
        if(mother == TTM_Data.GetPlayer())
            skipTriggerMother = true
        endif
        string jsonMother = BuildJson("ttm_paternity_secret_resolved", msg, skipTrigger = skipTriggerMother, child = childName, parentA = motherName, parentB = presumedFatherName, bioFather = bioFatherName, presumedFather = presumedFatherName, paternityOutcome = caseType)
        MARAS.RegisterSkyrimNetEvent("ttm_event", jsonMother, mother, none, true, MakeAudience2(mother, awareBio))
        if(presumedFather != none)
            bool skipTriggerPartner = true
            if(mother == TTM_Data.GetPlayer() && !IsActionNarrationSuppressed())
                skipTriggerPartner = !MARAS.ShouldTriggerSkyrimNetNarration(presumedFather, mother)
                if(!skipTriggerPartner)
                    skipTrigger = false
                endif
            endif
            string maskedMsg = motherName + " shared the news of her pregnancy with her partner " + presumedFatherName + ", telling him that she is expecting his child."
            string jsonPartner = BuildJson("ttm_paternity_secret_resolved", maskedMsg, skipTrigger = skipTriggerPartner, child = childName, parentA = motherName, parentB = presumedFatherName, bioFather = presumedFatherName, presumedFather = presumedFatherName, paternityOutcome = caseType)
            MARAS.RegisterSkyrimNetEvent("ttm_event", jsonPartner, presumedFather, none, true, MakeAudience2(presumedFather, none))
        endif
    elseif(caseType == "pregnancy_visible_presumed")
        Actor awareBio = bioFather
        if(stillSecretBio)
            awareBio = none
        endif
        string jsonMother = BuildJson("ttm_paternity_secret_resolved", msg, skipTrigger = true, child = childName, parentA = motherName, parentB = presumedFatherName, bioFather = bioFatherName, presumedFather = presumedFatherName, paternityOutcome = caseType)
        MARAS.RegisterSkyrimNetEvent("ttm_event", jsonMother, mother, none, true, MakeAudience2(mother, awareBio))
        if(presumedFather != none)
            string maskedMsg = motherName + "'s pregnancy has become visibly rounded. Her partner " + presumedFatherName + " now knows she is pregnant and naturally assumes the unborn baby is his."
            string jsonPartner = BuildJson("ttm_paternity_secret_resolved", maskedMsg, skipTrigger = skipTrigger, child = childName, parentA = motherName, parentB = presumedFatherName, bioFather = presumedFatherName, presumedFather = presumedFatherName, paternityOutcome = caseType)
            MARAS.RegisterSkyrimNetEvent("ttm_event", jsonPartner, presumedFather, none, true, MakeAudience2(presumedFather, none))
        endif
    else
        MARAS.RegisterSkyrimNetEvent("ttm_event", json, speaker, target, false)
    endif
    if(!skipTrigger)
        SuppressNextActionNarration(3.0)
    endif
    TTM_Debug.info("TTM_ServiceSkyrimNet.RegisterPaternitySecretEvent: [" + caseType + "] (skipTrigger=" + skipTrigger + ") " + msg)
EndFunction

Function RegisterAffairConfessed(Actor cheater, Actor victim, Actor affairPartner, string reasoning = "") global
    if(!TTM_Data.GetHasSkyrimNet() || !cheater || !victim)
        return
    endif
    string cheaterName = TTM_Utils.GetActorName(cheater)
    string victimName = TTM_Utils.GetActorName(victim)
    string partnerName = ""
    if (affairPartner)
        partnerName = TTM_Utils.GetActorName(affairPartner)
    endif
    string msg = cheaterName + " confessed to their partner " + victimName + " that they have been having an affair"
    if (partnerName != "")
        msg += " with " + partnerName
    endif
    msg += "."
    bool skipTrigger = true
    if(!IsActionNarrationSuppressed())
        skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(cheater, victim)
    endif
    string json = BuildJson("ttm_affair_confessed", msg, skipTrigger = skipTrigger, cheater = cheaterName, victim = victimName, affairPartner = partnerName)
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, cheater, victim)
    if(!skipTrigger)
        SuppressNextActionNarration(3.0)
    endif
EndFunction

Function RegisterAffairExposed(Actor affairPartner, Actor victim, Actor cheater, string reasoning = "") global
    if(!TTM_Data.GetHasSkyrimNet() || !affairPartner || !victim)
        return
    endif
    string partnerName = TTM_Utils.GetActorName(affairPartner)
    string victimName = TTM_Utils.GetActorName(victim)
    string cheaterName = ""
    if (cheater)
        cheaterName = TTM_Utils.GetActorName(cheater)
    endif
    string msg = partnerName + " exposed the secret affair they have been having"
    if (cheaterName != "")
        msg += " with " + cheaterName
    endif
    msg += " directly to " + victimName + "."
    bool skipTrigger = true
    if(!IsActionNarrationSuppressed())
        skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(affairPartner, victim)
    endif
    string json = BuildJson("ttm_affair_exposed", msg, skipTrigger = skipTrigger, cheater = cheaterName, victim = victimName, affairPartner = partnerName)
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, affairPartner, victim)
    if(!skipTrigger)
        SuppressNextActionNarration(3.0)
    endif
EndFunction

Function RegisterAffairResolved(Actor initiator, Actor otherParty, Actor affairPartner, string resolution, string reasoning = "") global
    if(!TTM_Data.GetHasSkyrimNet() || !initiator || !otherParty)
        return
    endif
    string initName = TTM_Utils.GetActorName(initiator)
    string otherName = TTM_Utils.GetActorName(otherParty)
    string partnerName = ""
    if (affairPartner)
        partnerName = TTM_Utils.GetActorName(affairPartner)
    endif
    string msg = ""
    if (resolution == "confronted")
        msg = initName + " and " + otherName + " resolved the affair: the affair is ended and they chose to stay together."
    elseif (resolution == "allowed")
        msg = initName + " and " + otherName + " resolved the affair: they agreed to open their relationship and allow the connection."
    elseif (resolution == "breakup")
        msg = initName + " and " + otherName + " separated and ended their relationship over the affair."
    else
        msg = initName + " and " + otherName + " resolved their situation (" + resolution + ")."
    endif
    bool skipTrigger = true
    if(!IsActionNarrationSuppressed())
        skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(initiator, otherParty)
    endif
    string json = BuildJson("ttm_affair_resolved", msg, skipTrigger = skipTrigger, cheater = initName, victim = otherName, affairPartner = partnerName, resolution = resolution)
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, initiator, otherParty)
    if(!skipTrigger)
        SuppressNextActionNarration(3.0)
    endif
EndFunction

Function RegisterCheatingCaught(Actor victim, Actor cheater, Actor affairPartner, bool isWalkIn = false) global
    if(!TTM_Data.GetHasSkyrimNet() || !victim || !cheater)
        return
    endif
    string victimName = TTM_Utils.GetActorName(victim)
    string cheaterName = TTM_Utils.GetActorName(cheater)
    string partnerName = ""
    if(affairPartner)
        partnerName = TTM_Utils.GetActorName(affairPartner)
    endif

    string msg = ""
    if(isWalkIn)
        msg = victimName + " walked in and caught " + cheaterName + " in the act with " + partnerName + "!"
    else
        msg = victimName + " discovered and caught " + cheaterName + " cheating with " + partnerName + "!"
    endif

    bool skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(victim, cheater)
    string json = BuildJson("ttm_cheating_caught", msg, skipTrigger = skipTrigger, \
        cheater = cheaterName, victim = victimName, affairPartner = partnerName, \
        isWalkIn = isWalkIn, hasIsWalkIn = true)
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, victim, cheater)
EndFunction

Function RegisterOpenRelationshipWalkIn(Actor witness, Actor partner, Actor sexPartner, bool isWalkIn = false) global
    if(!TTM_Data.GetHasSkyrimNet() || !witness || !partner)
        return
    endif
    string witnessName = TTM_Utils.GetActorName(witness)
    string partnerName = TTM_Utils.GetActorName(partner)
    string sexPartnerName = ""
    if(sexPartner)
        sexPartnerName = TTM_Utils.GetActorName(sexPartner)
    endif

    string msg = witnessName + " walked in on " + partnerName + " being intimate with " + sexPartnerName + " under their open relationship arrangement."

    bool skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(witness, partner)
    string json = BuildJson("ttm_open_relationship_walkin", msg, skipTrigger = skipTrigger, \
        partner = partnerName, victim = witnessName, affairPartner = sexPartnerName, \
        isOpen = true, hasIsOpen = true, isWalkIn = isWalkIn, hasIsWalkIn = true)
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, witness, partner)
EndFunction

Function RegisterLaborStarted(Actor mother, Actor partner = none, int childCount = 1) global
    if(!TTM_Data.GetHasSkyrimNet() || !mother)
        return
    endif
    string motherName = TTM_Utils.GetActorName(mother)
    string partnerName = ""
    if(partner)
        partnerName = TTM_Utils.GetActorName(partner)
    endif
    string msg = motherName + " has gone into labor and the delivery has begun!"
    if(childCount > 1)
        msg += " She is carrying " + childCount + " children and will deliver them one after another in this same labor."
    endif
    if(partnerName != "")
        msg += " " + partnerName + " is at her side supporting her."
    endif
    bool skipTrigger = !MARAS.ShouldTriggerSkyrimNetNarration(mother, none)
    Actor eventTarget = partner
    if(eventTarget != none && !MARAS.ShouldTriggerSkyrimNetNarration(mother, eventTarget))
        eventTarget = none
    endif
    string json = BuildJson("ttm_labor_started", msg, skipTrigger = skipTrigger, mother = motherName, partner = partnerName)
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, mother, eventTarget, false)
    if(!skipTrigger)
        SuppressNextActionNarration(3.0)
    endif
EndFunction

Function RegisterLaborProgress(Actor mother, int bornCount, int totalCount) global
    if(!TTM_Data.GetHasSkyrimNet() || !mother)
        return
    endif
    string motherName = TTM_Utils.GetActorName(mother)
    int remaining = totalCount - bornCount
    string msg = motherName + "'s labor continues: " + bornCount + " out of " + totalCount + " children are born, " + remaining + " to go."
    string json = BuildJson("ttm_labor_progress", msg, skipTrigger = true, mother = motherName)
    MARAS.RegisterSkyrimNetEvent("ttm_event", json, mother, none, false)
EndFunction

Function RegisterEventSchema() global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif
    string name = "MARAS Event"
    string description = "Happens on different occasions, related to marriage life"
    string jsonParams = "[" \
        + "{\"name\": \"type\", \"type\": 0, \"required\": true, \"description\": \"Type of MARAS event.\"}," \
        + "{\"name\": \"declinedAction\", \"type\": 0, \"required\": false, \"description\": \"If player declined what kind of declanation it was.\"}," \
        + "{\"name\": \"msg\", \"type\": 0, \"required\": true, \"description\": \"Event message to send.\"}," \
        + "{\"name\": \"skipTrigger\", \"type\": 2, \"required\": false, \"description\": \"If trigger should be skipped.\"}," \
        + "{\"name\": \"newStatus\", \"type\": 0, \"required\": false, \"description\": \"New marital status in case of marital status change event.\"}," \
        + "{\"name\": \"affectionLevel\", \"type\": 0, \"required\": false, \"description\": \"New affection level in case of marital affection change event.\"}," \
        + "{\"name\": \"affectionDirection\", \"type\": 0, \"required\": false, \"description\": \"Direction of affection change in case of marital affection change event.\"}," \
        + "{\"name\": \"isDemoted\", \"type\": 2, \"required\": false, \"description\": \"Whether the spouse was demoted in case of hierarchy change event.\"}," \
        + "{\"name\": \"child\", \"type\": 0, \"required\": false, \"description\": \"Child name or form ID in case of child event.\"}," \
        + "{\"name\": \"parentA\", \"type\": 0, \"required\": false, \"description\": \"Parent A in case of child event.\"}," \
        + "{\"name\": \"parentB\", \"type\": 0, \"required\": false, \"description\": \"Parent B in case of child event.\"}," \
        + "{\"name\": \"reason\", \"type\": 0, \"required\": false, \"description\": \"Reason for child custody or parent change.\"}," \
        + "{\"name\": \"bioFather\", \"type\": 0, \"required\": false, \"description\": \"Biological father name in paternity events.\"}," \
        + "{\"name\": \"presumedFather\", \"type\": 0, \"required\": false, \"description\": \"Presumed father name in paternity events.\"}," \
        + "{\"name\": \"paternityOutcome\", \"type\": 0, \"required\": false, \"description\": \"Outcome of paternity intrigue evaluation.\"}," \
        + "{\"name\": \"trainingLocation\", \"type\": 0, \"required\": false, \"description\": \"Training location for child training events.\"}," \
        + "{\"name\": \"gender\", \"type\": 0, \"required\": false, \"description\": \"Gender of the child (male/female).\"}," \
        + "{\"name\": \"relation\", \"type\": 0, \"required\": false, \"description\": \"Relationship noun of the child (son/daughter).\"}," \
        + "{\"name\": \"cheater\", \"type\": 0, \"required\": false, \"description\": \"Cheater actor name.\"}," \
        + "{\"name\": \"victim\", \"type\": 0, \"required\": false, \"description\": \"Victim partner actor name.\"}," \
        + "{\"name\": \"affairPartner\", \"type\": 0, \"required\": false, \"description\": \"Affair partner actor name.\"}," \
        + "{\"name\": \"resolution\", \"type\": 0, \"required\": false, \"description\": \"Resolution of affair (confronted, allowed, breakup).\"}," \
        + "{\"name\": \"isOpen\", \"type\": 2, \"required\": false, \"description\": \"Whether open relationship is active.\"}," \
        + "{\"name\": \"isWalkIn\", \"type\": 2, \"required\": false, \"description\": \"Whether event was a walk-in discovery.\"}," \
        + "{\"name\": \"mother\", \"type\": 0, \"required\": false, \"description\": \"Mother actor name.\"}," \
        + "{\"name\": \"partner\", \"type\": 0, \"required\": false, \"description\": \"Partner actor name.\"}" \
        + "]"
    string renderParams = "{\"recent_events\":\"{{msg}}\",\"raw\":\"{{msg}}\",\"compact\":\"{{msg}}\",\"verbose\":\"{{msg}}\"}"
    SkyrimnetApi.RegisterEventSchema("ttm_event", name, description, jsonParams, renderParams, false, 0)
EndFunction

string Function BuildJson(string type, \
                        string msg, \
                        bool skipTrigger = false, \
                        string declinedAction = "", \
                        string newStatus = "", \
                        string affectionLevel = "", \
                        string affectionDirection = "", \
                        bool isDemoted = false, \
                        string proposalResult = "", \
                        string child = "", \
                        string parentA = "", \
                        string parentB = "", \
                        string reason = "", \
                        string bioFather = "", \
                        string presumedFather = "", \
                        string paternityOutcome = "", \
                        string trainingLocation = "", \
                        string gender = "", \
                        string relation = "", \
                        string cheater = "", \
                        string victim = "", \
                        string affairPartner = "", \
                        string resolution = "", \
                        bool isOpen = false, \
                        bool hasIsOpen = false, \
                        bool isWalkIn = false, \
                        bool hasIsWalkIn = false, \
                        string mother = "", \
                        string partner = "" \
) global
    string jsonData = "{"
    jsonData += "\"type\": \""+type+"\""
    jsonData += ",\"msg\": \""+msg+"\""
    if(skipTrigger)
        jsonData += ", \"skipTrigger\": true"
    else
        jsonData += ", \"skipTrigger\": false"
    endif

    if(declinedAction != "")
        jsonData += ", \"declinedAction\": \""+declinedAction+"\""
    endif

    if(newStatus != "")
        jsonData += ", \"newStatus\": \""+newStatus+"\""
    endif

    if(affectionLevel != "")
        jsonData += ", \"affectionLevel\": \""+affectionLevel+"\""
    endif

    if(affectionDirection != "")
        jsonData += ", \"affectionDirection\": \""+affectionDirection+"\""
    endif

    if(isDemoted)
        jsonData += ", \"isDemoted\": true"
    endif

    if(proposalResult != "")
        jsonData += ", \"proposalResult\": \""+proposalResult+"\""
    endif

    if(child != "")
        jsonData += ", \"child\": \""+child+"\""
    endif

    if(parentA != "")
        jsonData += ", \"parentA\": \""+parentA+"\""
    endif

    if(parentB != "")
        jsonData += ", \"parentB\": \""+parentB+"\""
    endif

    if(reason != "")
        jsonData += ", \"reason\": \""+reason+"\""
    endif

    if(bioFather != "")
        jsonData += ", \"bioFather\": \""+bioFather+"\""
    endif

    if(presumedFather != "")
        jsonData += ", \"presumedFather\": \""+presumedFather+"\""
    endif

    if(paternityOutcome != "")
        jsonData += ", \"paternityOutcome\": \""+paternityOutcome+"\""
    endif

    if(trainingLocation != "")
        jsonData += ", \"trainingLocation\": \""+trainingLocation+"\""
    endif

    if(gender != "")
        jsonData += ", \"gender\": \""+gender+"\""
    endif

    if(relation != "")
        jsonData += ", \"relation\": \""+relation+"\""
    endif

    if(cheater != "")
        jsonData += ", \"cheater\": \""+cheater+"\""
    endif

    if(victim != "")
        jsonData += ", \"victim\": \""+victim+"\""
    endif

    if(affairPartner != "")
        jsonData += ", \"affairPartner\": \""+affairPartner+"\""
    endif

    if(resolution != "")
        jsonData += ", \"resolution\": \""+resolution+"\""
    endif

    if(hasIsOpen)
        if(isOpen)
            jsonData += ", \"isOpen\": true"
        else
            jsonData += ", \"isOpen\": false"
        endif
    endif

    if(hasIsWalkIn)
        if(isWalkIn)
            jsonData += ", \"isWalkIn\": true"
        else
            jsonData += ", \"isWalkIn\": false"
        endif
    endif

    if(mother != "")
        jsonData += ", \"mother\": \""+mother+"\""
    endif

    if(partner != "")
        jsonData += ", \"partner\": \""+partner+"\""
    endif

    jsonData += "}"
    return jsonData
EndFunction
