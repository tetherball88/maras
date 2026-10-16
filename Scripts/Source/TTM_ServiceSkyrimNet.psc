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
    DirectNarration("Time passed... " + TTM_Utils.GetActorName(player) + " and " + TTM_Utils.GetActorName(npc) + " got married!", npc)
EndFunction

Function DirectNarration(String content, Actor originatorActor = None, Actor targetActor = None) global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif
    SkyrimNetApi.DirectNarration(content, originatorActor, targetActor)
EndFunction

Function RegisterPlayerGotPolygamyBlessing(Actor spouse) global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif
    Actor player = TTM_Data.GetPlayer()
    string playerName = TTM_Utils.GetActorName(player)
    string spouseName = TTM_Utils.GetActorName(spouse)
    string msg = playerName + " got Mara's blessing to enter polygamy marriage. " + spouseName + " agreed with " + playerName + "'s new Dragonborn reality and new way of their marriage'."
    string json = BuildJson("ttm_polygamy_blessing", msg)
    SkyrimNetApi.RegisterEvent("ttm_event", json, player, spouse)
EndFunction

Function RegisterAffectionGift(string msg, Actor receiver) global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif
    Actor player = TTM_Data.GetPlayer()
    string json = BuildJson("ttm_affection_gift", msg)
    SkyrimNetApi.RegisterEvent("ttm_event", json, player, receiver)
EndFunction

Function RegisterMaritalStatusChangeEvent(Actor initiator, Actor acceptor, string newStatus) global
    if(!TTM_Data.GetHasSkyrimNet() || newStatus == "candidate")
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

    string json = BuildJson("ttm_marital_status_change", msg, newStatus = newStatus)

    SkyrimNetApi.RegisterEvent("ttm_event", json, initiator, acceptor)
EndFunction

Function RegisterOpenRelationshipChangeEvent(Actor initiator, Actor partner, bool isOpen) global
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
    string json = BuildJson("ttm_open_relationship_change", msg, parentA = initiatorName, parentB = partnerName, isOpen = isOpen, hasIsOpen = true)
    SkyrimNetApi.RegisterEvent("ttm_event", json, initiator, partner)
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
    string json = BuildJson("ttm_spouse_hierarchy_change", msg, isDemoted = isDemoted)
    SkyrimNetApi.RegisterEvent("ttm_event", json, player, spouse)
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
    string json = BuildJson("ttm_spouse_affection_change", msg, affectionLevel = affectionLevel, affectionDirection = direction)
    SkyrimNetApi.RegisterEvent("ttm_event", json, spouse, player)
EndFunction

Function RegisterProposalDecision(Actor initiator, Actor acceptor, string status, string decision, string reasoning = "") global
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

    string json = BuildJson("ttm_dating_proposal", msg)
    SkyrimNetApi.RegisterEvent("ttm_event", json, initiator, acceptor)
EndFunction

Function RegisterActionDeclineEvent(string json, Actor akSpeaker, Actor akTarget = none) global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif
    SkyrimNetApi.RegisterEvent("ttm_event", json, akSpeaker, akTarget)
EndFunction

Function RegisterActionDecline(Actor initiator, Actor acceptor, string actionType, string msg, bool skipTrigger = true) global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif
    string json = BuildJson("ttm_action_decline", msg, skipTrigger = skipTrigger, declinedAction = actionType)
    SkyrimNetApi.RegisterEvent("ttm_event", json, initiator, acceptor)
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
    string json = BuildJson("ttm_child_adopted", msg, child = childName, parentA = parentAName, parentB = TTM_Utils.GetActorName(parentB))
    SkyrimNetApi.RegisterEvent("ttm_event", json, parentA, child)
    if(parentB != none && parentB != parentA)
        SkyrimNetApi.RegisterEvent("ttm_event", json, parentB, child)
    endif
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
    string json = BuildJson("ttm_child_inherited", msg, child = childName, parentA = newParentName, parentB = TTM_Utils.GetActorName(coParent), reason = "spouse_death")
    SkyrimNetApi.RegisterEvent("ttm_event", json, newParent, child)
    if(coParent != none && coParent != newParent)
        SkyrimNetApi.RegisterEvent("ttm_event", json, coParent, child)
    endif
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
    string json = BuildJson("ttm_child_parents_changed", msg, child = childName, parentA = parentAName, parentB = TTM_Utils.GetActorName(parentB), reason = reason)
    SkyrimNetApi.RegisterEvent("ttm_event", json, parentA, child)
    if(parentB != none && parentB != parentA)
        SkyrimNetApi.RegisterEvent("ttm_event", json, parentB, child)
    endif
EndFunction

Function RegisterPregnancyAnnounced(Actor mother, Actor partner = none, Actor bioFather = none, string bioFatherName = "", int childCount = 1) global
    if (!TTM_Data.GetHasSkyrimNet() || mother == none)
        return
    endif

    string json = MARAS.BuildPregnancyAnnouncedJson(mother, partner, bioFather, bioFatherName, childCount)
    if (json != "")
        SkyrimNetApi.RegisterEvent("ttm_event", json, mother, none)
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
    string json = BuildJson("ttm_pregnancy_lost", msg, parentA = motherName, parentB = partnerName, reason = reason)
    SkyrimNetApi.RegisterEvent("ttm_event", json, mother, partner)
EndFunction

Function RegisterChildBirthBatch(Actor mother, int[] childIDs = none) global
    if (!TTM_Data.GetHasSkyrimNet() || mother == none)
        return
    endif

    string json = MARAS.BuildChildBirthJson(mother, childIDs)
    if (json != "")
        SkyrimNetApi.RegisterEvent("ttm_event", json, mother, none)
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
        string cJson = MARAS.BuildChildBirthJson(mother, childIDs)
        if (cJson != "")
            SkyrimNetApi.RegisterEvent("ttm_event", cJson, mother, none)
            return
        endif
    endif

    if(childName == "")
        childName = MARAS.GetChildName(childID)
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

    if(isSecretForAssumed && isSecretForBio && presumedFather != none)
        msg = motherName + " gave birth to " + childName + ". " + presumedFatherName + " believes the newborn " + childRelation + " is his, unaware that the true biological father is " + bioFatherName + ". Furthermore, " + bioFatherName + " has not been informed that he fathered the child."
    elseif(isSecretForAssumed && presumedFather != none)
        msg = motherName + " gave birth to " + childName + ". " + presumedFatherName + " believes the newborn " + childRelation + " is his, unaware that the true biological father is " + bioFatherName + "."
    elseif(isSecretForBio && bioFather != none)
        msg = motherName + " gave birth to " + childName + ", a " + childRelation + " fathered by " + bioFatherName + ", though " + bioFatherName + " is unaware of the child."
    elseif(bioFather != none)
        msg = motherName + " gave birth to a healthy " + childRelation + ", " + childName + ", fathered by " + bioFatherName + "."
    else
        msg = motherName + " gave birth to a newborn " + childRelation + ", " + childName + "."
    endif

    string json = BuildJson("ttm_child_birth", msg, child = childName, parentA = motherName, parentB = presumedFatherName, bioFather = bioFatherName, presumedFather = presumedFatherName, gender = childGenderStr, relation = childRelation)
    Actor speaker = mother
    if(speaker == none)
        speaker = presumedFather
    endif
    if(speaker == none)
        speaker = TTM_Data.GetPlayer()
    endif
    SkyrimNetApi.RegisterEvent("ttm_event", json, speaker, none)
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

    string json = BuildJson("ttm_child_grew_up", msg, child = childName, parentA = motherName, parentB = presumedFatherName, bioFather = bioFatherName, presumedFather = presumedFatherName, gender = childGenderStr, relation = childRelation)
    Actor speaker = mother
    if(speaker == none)
        speaker = presumedFather
    endif
    if(speaker == none)
        speaker = TTM_Data.GetPlayer()
    endif
    SkyrimNetApi.RegisterEvent("ttm_event", json, speaker, none)
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

    string json = BuildJson("ttm_child_sent_to_training", msg, child = childName, parentA = motherName, parentB = presumedFatherName, bioFather = bioFatherName, presumedFather = presumedFatherName, trainingLocation = trainingLocation, gender = childGenderStr, relation = childRelation)
    Actor speaker = mother
    if(speaker == none)
        speaker = presumedFather
    endif
    if(speaker == none)
        speaker = TTM_Data.GetPlayer()
    endif
    SkyrimNetApi.RegisterEvent("ttm_event", json, speaker, none)
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

    string json = BuildJson("ttm_child_became_adult", msg, child = childName, parentA = motherName, parentB = presumedFatherName, bioFather = bioFatherName, presumedFather = presumedFatherName, gender = childGenderStr, relation = childRelation)
    Actor speaker = mother
    if(speaker == none)
        speaker = presumedFather
    endif
    if(speaker == none)
        speaker = TTM_Data.GetPlayer()
    endif
    SkyrimNetApi.RegisterEvent("ttm_event", json, speaker, none)
EndFunction


;/
  RegisterPaternitySecretEvent:
  Dispatches a SkyrimNet event (skipTrigger=true) for paternity secret resolution.
  Handles exactly 5 canonical cases:
  - "mother_confessed_bio"
  - "mother_confessed_presumed"
  - "mother_confessed_both"
  - "bio_father_learnt"
  - "presumed_father_learnt"
/;
Function RegisterPaternitySecretEvent(int childID, string caseType, string customSummary = "") global
    if(!TTM_Data.GetHasSkyrimNet() || childID == 0)
        return
    endif

    Actor mother = MARAS.GetBioMother(childID)
    Actor presumedFather = MARAS.GetMotherPartner(childID)
    Actor bioFather = MARAS.GetBioFather(childID)

    string childName = MARAS.GetChildName(childID)
    if(childName == "")
        if(MARAS.GetChildStage(childID) == 0)
            childName = "unborn child"
        else
            childName = "the child"
        endif
    endif

    string motherName = TTM_Utils.GetActorName(mother)
    string presumedFatherName = TTM_Utils.GetActorName(presumedFather)
    string bioFatherName = TTM_Utils.GetActorName(bioFather)
    if(bioFatherName == "")
        bioFatherName = "another man"
    endif

    bool stillSecretAssumed = MARAS.IsSecretForPartner(childID)
    bool stillSecretBio = MARAS.IsSecretForBioFather(childID)

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
        if(stillSecretAssumed)
            msg = motherName + " confessed to " + bioFatherName + " that he is the true biological father of " + childName + ". However, " + presumedFatherName + " does not know and still believes the child is his."
        else
            msg = motherName + " confessed to " + bioFatherName + " that he is the true biological father of " + childName + ". Both " + presumedFatherName + " and " + bioFatherName + " now know the truth."
        endif
        speaker = mother
        target = bioFather

    ; =========================================================================
    ; 2. MOTHER CONFESSED TO PRESUMED FATHER
    ; =========================================================================
    elseif(caseType == "mother_confessed_presumed")
        if(stillSecretBio)
            msg = motherName + " confessed to " + presumedFatherName + " that " + childName + " was biologically fathered by " + bioFatherName + ", not him. However, " + bioFatherName + " does not know that he is the biological father."
        else
            msg = motherName + " confessed to " + presumedFatherName + " that " + childName + " was biologically fathered by " + bioFatherName + ", not him. Both " + presumedFatherName + " and " + bioFatherName + " now know the truth."
        endif
        speaker = mother
        target = presumedFather

    ; =========================================================================
    ; 3. MOTHER CONFESSED TO BOTH
    ; =========================================================================
    elseif(caseType == "mother_confessed_both")
        msg = motherName + " has revealed the truth of " + childName + "'s biological paternity: both " + presumedFatherName + " and " + bioFatherName + " now know that " + bioFatherName + " is the true biological father."
        speaker = mother
        target = none

    ; =========================================================================
    ; 4. BIO FATHER LEARNT
    ; =========================================================================
    elseif(caseType == "bio_father_learnt")
        if(stillSecretAssumed)
            msg = bioFatherName + " has learned that he is the true biological father of " + childName + " with " + motherName + ". However, " + presumedFatherName + " does not know and still believes the child is his."
        else
            msg = bioFatherName + " has learned that he is the true biological father of " + childName + " with " + motherName + ". Both " + presumedFatherName + " and " + bioFatherName + " are now aware of the true biological paternity."
        endif
        speaker = bioFather
        target = mother

    ; =========================================================================
    ; 5. PRESUMED FATHER LEARNT
    ; =========================================================================
    elseif(caseType == "presumed_father_learnt")
        if(stillSecretBio)
            msg = presumedFatherName + " has learned that " + childName + " was biologically fathered by " + bioFatherName + ", not him. However, " + bioFatherName + " does not know that he is the biological father."
        else
            msg = presumedFatherName + " has learned that " + childName + " was biologically fathered by " + bioFatherName + ", not him. Both " + presumedFatherName + " and " + bioFatherName + " are now aware of the true biological paternity."
        endif
        speaker = presumedFather
        target = mother

    ; =========================================================================
    ; 6. DIVORCE REVEALED
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

    string json = BuildJson("ttm_paternity_secret_resolved", msg, skipTrigger = false, child = childName, parentA = motherName, parentB = presumedFatherName, bioFather = bioFatherName, presumedFather = presumedFatherName, paternityOutcome = caseType)
    SkyrimNetApi.RegisterEvent("ttm_event", json, speaker, target)
    TTM_Debug.info("TTM_ServiceSkyrimNet.RegisterPaternitySecretEvent: [" + caseType + "] (skipTrigger=false) " + msg)
EndFunction

Function RegisterPaternitySecretLearned(int childID, Actor fatherWhoLearned, bool isDiscovery, Actor mother = none, bool isBoth = false, bool isConfession = false) global
    Actor bioFather = MARAS.GetBioFather(childID)
    string caseType = ""
    if (isConfession)
        if (isBoth)
            caseType = "mother_confessed_both"
        elseif (fatherWhoLearned == bioFather)
            caseType = "mother_confessed_bio"
        else
            caseType = "mother_confessed_presumed"
        endif
    else
        if (fatherWhoLearned == bioFather)
            caseType = "bio_father_learnt"
        else
            caseType = "presumed_father_learnt"
        endif
    endif
    RegisterPaternitySecretEvent(childID, caseType)
EndFunction

Function RegisterPaternitySecretResolved(Actor mother, string childName, Actor bioFather, Actor presumedFather, string outcome, string summary = "", int childID = 0) global
    if(!TTM_Data.GetHasSkyrimNet() || childID == 0)
        return
    endif
    string caseType = outcome
    if (outcome == "confession")
        caseType = "mother_confessed_presumed"
    elseif (outcome == "discovery" || outcome == "assumed_father_resolved")
        caseType = "presumed_father_learnt"
    elseif (outcome == "bio_father_resolved")
        caseType = "bio_father_learnt"
    elseif (outcome == "both_fathers_resolved")
        caseType = "mother_confessed_both"
    endif
    RegisterPaternitySecretEvent(childID, caseType)
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
    string json = BuildJson("ttm_affair_confessed", msg, skipTrigger = false, cheater = cheaterName, victim = victimName, affairPartner = partnerName)
    SkyrimNetApi.RegisterEvent("ttm_event", json, cheater, victim)
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
    string json = BuildJson("ttm_affair_exposed", msg, skipTrigger = false, cheater = cheaterName, victim = victimName, affairPartner = partnerName)
    SkyrimNetApi.RegisterEvent("ttm_event", json, affairPartner, victim)
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
    string json = BuildJson("ttm_affair_resolved", msg, skipTrigger = false, cheater = initName, victim = otherName, affairPartner = partnerName, resolution = resolution)
    SkyrimNetApi.RegisterEvent("ttm_event", json, initiator, otherParty)
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

    string json = BuildJson("ttm_cheating_caught", msg, skipTrigger = false, \
        cheater = cheaterName, victim = victimName, affairPartner = partnerName, \
        isWalkIn = isWalkIn, hasIsWalkIn = true)
    SkyrimNetApi.RegisterEvent("ttm_event", json, victim, cheater)
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

    string json = BuildJson("ttm_open_relationship_walkin", msg, skipTrigger = false, \
        partner = partnerName, victim = witnessName, affairPartner = sexPartnerName, \
        isOpen = true, hasIsOpen = true, isWalkIn = isWalkIn, hasIsWalkIn = true)
    SkyrimNetApi.RegisterEvent("ttm_event", json, witness, partner)
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
    string json = BuildJson("ttm_labor_started", msg, skipTrigger = false, mother = motherName, partner = partnerName)
    SkyrimNetApi.RegisterEvent("ttm_event", json, mother, partner)
EndFunction

Function RegisterLaborProgress(Actor mother, int bornCount, int totalCount) global
    if(!TTM_Data.GetHasSkyrimNet() || !mother)
        return
    endif
    string motherName = TTM_Utils.GetActorName(mother)
    int remaining = totalCount - bornCount
    string msg = motherName + "'s labor continues: " + bornCount + " out of " + totalCount + " children are born, " + remaining + " to go."
    string json = BuildJson("ttm_labor_progress", msg, skipTrigger = true, mother = motherName)
    SkyrimNetApi.RegisterEvent("ttm_event", json, mother, none)
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
    jsonData += ",\"skipTrigger\": "

    if(skipTrigger)
        jsonData += "true"
    else
        jsonData += "false"
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



