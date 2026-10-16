scriptname TTM_ServiceAffection

Function Maintenance() global
    MARAS.SetAffectionMinMax("intimacy", -22, 22)
    MARAS.SetAffectionMinMax("gift", 0, 15)
    MARAS.SetAffectionMinMax("dialogueStarted", 0, 5)
    MARAS.SetAffectionMinMax("slept", 0, 8)
    MARAS.SetAffectionMinMax("promotion", -24, 24)
    MARAS.SetAffectionMinMax("loneliness", -16, 6)
EndFunction

Function AddIntimacyAffection(Actor spouse, float affection) global
    string temperament = MARAS.GetNpcCurrentTypeName(spouse, "temperament")
    float mlt = 1.0
    if(temperament == "Romantic" || temperament == "Jealous")
        mlt = 1.5
    elseif(temperament == "Independent")
        mlt = 0.5
    endif

    return MARAS.AddAffection(spouse, affection * mlt, "Intimacy")
EndFunction

Function AddGiftAffection(Actor spouse, float value) global
    int socialClass = MARAS.GetNpcCurrentTypeEnum(spouse, "socialClass")

    ;/
    Assuming value = 1000 gold

    socialClass | spouseSocialClassFactor | giftAffection (points)
    ---------------------------------------------------------------
        0      | 0.667                   | 15.00
        1      | 0.810                   | 12.35
        2      | 0.952                   | 10.50
        3      | 1.095                   | 9.13
        4      | 1.238                   | 8.08
        5      | 1.381                   | 7.24
        6      | 1.524                   | 6.56
        7      | 1.667                   | 6.00
    ---------------------------------------------------------------
    Range: Outcast ≈ 15 pts → Ruler ≈ 6 pts
    /;
    float spouseSocialClassFactor = 0.667 + (socialClass / 7.0)
    float giftAffection = value / (spouseSocialClassFactor * 100.0)
    if(MARAS.GetNpcCurrentTypeName(spouse, "temperament") == "Romantic")
        giftAffection = giftAffection * 1.5
    endif
    MARAS.AddAffection(spouse, giftAffection, "Gift")
EndFunction

Function AddDialogueStartedAffection(Actor spouse) global
    MARAS.AddAffection(spouse, 1.0, "DialogueStarted")
EndFunction

Function AddPromotionAffection(Actor spouse, float rankDiff) global
    float promoAffection = 8.0
    string temperament = MARAS.GetNpcCurrentTypeName(spouse, "temperament")
    if(temperament == "Proud")
        promoAffection = 12.0
    elseif(temperament == "Humble")
        promoAffection = 4.0
    endif
    MARAS.AddAffection(spouse, promoAffection * rankDiff, "Promotion")
EndFunction

Function AddSleptAffection(Actor spouse) global
    float affection = 3.0
    string temperament = MARAS.GetNpcCurrentTypeName(spouse, "temperament")
    if(temperament == "Romantic")
        affection = 5.0
    elseif(temperament == "Independent")
        affection = 1.0
    elseif(temperament == "Jealous")
        affection = 4.0
    endif
    MARAS.AddAffection(spouse, affection, "Slept")
EndFunction

;/
  AdjustPlayerPartnerAffection:
  Authoritative helper for life-event affection changes between the Player and an NPC partner.
  Enforces player invariant (only partner's affection toward player is modified) and scales
  deltas based on the NPC's temperament.
/;
Function AdjustPlayerPartnerAffection(Actor partner, float baseDelta, string contextReason = "") global
    if (!partner || partner == TTM_Data.GetPlayer())
        return
    endif

    bool isPartner = MARAS.IsNPCStatus(partner, "married") || MARAS.IsNPCStatus(partner, "engaged") || MARAS.IsNPCStatus(partner, "dating") || MARAS.IsNPCStatus(partner, "lover") || MARAS.IsNPCStatus(partner, "candidate")
    if (!isPartner)
        return
    endif

    string temperament = MARAS.GetNpcCurrentTypeName(partner, "temperament")
    float multiplier = 1.0

    if (baseDelta > 0.0)
        if (temperament == "Romantic")
            multiplier = 1.35
        elseif (temperament == "Proud")
            multiplier = 1.20
        elseif (temperament == "Independent")
            multiplier = 0.80
        endif
    else
        if (temperament == "Jealous")
            multiplier = 1.50
        elseif (temperament == "Romantic")
            multiplier = 1.25
        elseif (temperament == "Independent")
            multiplier = 0.70
        elseif (temperament == "Humble")
            multiplier = 0.85
        endif
    endif

    int finalDelta = (baseDelta * multiplier) as int
    int currentAffection = MARAS.GetPermanentAffection(partner)
    int newAffection = PapyrusUtil.ClampInt(currentAffection + finalDelta, 0, 100)

    MARAS.SetPermanentAffection(partner, newAffection)
    TTM_Debug.info("TTM_ServiceAffection.AdjustPlayerPartnerAffection: " + TTM_Utils.GetActorName(partner) + " delta=" + finalDelta + " (" + contextReason + ", temp=" + temperament + ") " + currentAffection + " -> " + newAffection)
EndFunction

Function ApplyPregnancyConceivedAffection(Actor partner, bool isPlayerPregnant = false) global
    float baseDelta = 20.0
    string reason = "pregnancy_conceived"
    if (isPlayerPregnant)
        reason = "player_pregnant"
    endif
    AdjustPlayerPartnerAffection(partner, baseDelta, reason)
EndFunction

Function ApplyPregnancyLostAffection(Actor partner, string reason = "miscarriage") global
    float baseDelta = -15.0
    AdjustPlayerPartnerAffection(partner, baseDelta, "pregnancy_lost_" + reason)
EndFunction

Function ApplyChildBirthAffection(Actor partner, int childCount = 1) global
    float baseDelta = 25.0
    if (childCount > 1)
        baseDelta += ((childCount - 1) * 10.0)
    endif
    AdjustPlayerPartnerAffection(partner, baseDelta, "child_birth")
EndFunction

Function ApplyChildAdoptedAffection(Actor partner) global
    AdjustPlayerPartnerAffection(partner, 20.0, "child_adopted")
EndFunction

Function ApplyChildMilestoneAffection(Actor partner, string milestone = "child") global
    float baseDelta = 10.0
    if (milestone == "adult")
        baseDelta = 15.0
    endif
    AdjustPlayerPartnerAffection(partner, baseDelta, "child_milestone_" + milestone)
EndFunction


Function ApplyCheatingCaughtAffection(Actor partner, bool playerIsCheater = true) global
    float baseDelta = -30.0
    if (playerIsCheater)
        baseDelta = -35.0
    endif
    AdjustPlayerPartnerAffection(partner, baseDelta, "cheating_caught")
EndFunction

Function ApplyOpenRelationshipToggleAffection(Actor partner, bool isOpen) global
    if (!partner)
        return
    endif
    string temperament = MARAS.GetNpcCurrentTypeName(partner, "temperament")
    float baseDelta = 0.0
    if (isOpen)
        if (temperament == "Independent")
            baseDelta = 15.0
        elseif (temperament == "Jealous")
            baseDelta = -35.0
        elseif (temperament == "Romantic")
            baseDelta = -20.0
        else
            baseDelta = -10.0
        endif
    else
        if (temperament == "Jealous")
            baseDelta = 20.0
        elseif (temperament == "Romantic")
            baseDelta = 15.0
        elseif (temperament == "Independent")
            baseDelta = -10.0
        else
            baseDelta = 10.0
        endif
    endif
    AdjustPlayerPartnerAffection(partner, baseDelta, "open_relationship_toggle")
EndFunction

Function ApplyOpenRelationshipWalkInAffection(Actor partner) global
    if (!partner)
        return
    endif
    string temperament = MARAS.GetNpcCurrentTypeName(partner, "temperament")
    if (temperament == "Jealous")
        AdjustPlayerPartnerAffection(partner, -10.0, "open_relationship_walkin")
    endif
EndFunction

Function ApplyAffairConfessedAffection(Actor cheater, Actor victim) global
    Actor player = TTM_Data.GetPlayer()
    if (cheater == player && victim != None && victim != player)
        AdjustPlayerPartnerAffection(victim, -30.0, "affair_confessed_by_player")
    elseif (victim == player && cheater != None && cheater != player)
        AdjustPlayerPartnerAffection(cheater, -20.0, "affair_confessed_to_player")
    endif
EndFunction

Function ApplyAffairExposedAffection(Actor affairPartner, Actor victim, Actor cheater) global
    Actor player = TTM_Data.GetPlayer()
    if (cheater == player && victim != None && victim != player)
        AdjustPlayerPartnerAffection(victim, -50.0, "affair_exposed_player_cheated")
    elseif (victim == player)
        if (cheater != None && cheater != player)
            AdjustPlayerPartnerAffection(cheater, -50.0, "affair_exposed_partner_cheated")
        endif
        if (affairPartner != None && affairPartner != player)
            AdjustPlayerPartnerAffection(affairPartner, -30.0, "affair_exposed_affair_partner")
        endif
    endif
EndFunction

Function ApplyAffairResolvedAffection(Actor cheater, Actor victim, string resolution) global
    Actor player = TTM_Data.GetPlayer()
    float delta = 0.0
    if (resolution == "confronted")
        delta = 10.0
    elseif (resolution == "breakup")
        delta = -20.0
    endif

    if (delta != 0.0)
        if (cheater == player && victim != None && victim != player)
            AdjustPlayerPartnerAffection(victim, delta, "affair_resolved_" + resolution)
        elseif (victim == player && cheater != None && cheater != player)
            AdjustPlayerPartnerAffection(cheater, delta, "affair_resolved_" + resolution)
        else
            AdjustPlayerPartnerAffection(cheater, delta, "affair_resolved_" + resolution)
            AdjustPlayerPartnerAffection(victim, delta, "affair_resolved_" + resolution)
        endif
    endif
EndFunction

Function ApplyPaternitySecretForChild(int childID, string outcomeType) global
    if (childID == 0)
        return
    endif
    Actor player = TTM_Data.GetPlayer()
    Actor mother = MARAS.GetBioMother(childID)
    Actor presumedFather = MARAS.GetMotherPartner(childID)
    Actor bioFather = MARAS.GetBioFather(childID)

    if (outcomeType == "mother_confessed_bio" || outcomeType == "bio_father_learnt")
        if (bioFather == player && mother)
            AdjustPlayerPartnerAffection(mother, 15.0, "paternity_secret_" + outcomeType)
        elseif (mother == player && bioFather)
            AdjustPlayerPartnerAffection(bioFather, 15.0, "paternity_secret_" + outcomeType)
        endif
    elseif (outcomeType == "mother_confessed_presumed")
        if (presumedFather == player && mother)
            AdjustPlayerPartnerAffection(mother, -30.0, "paternity_secret_" + outcomeType)
        elseif (mother == player && presumedFather)
            AdjustPlayerPartnerAffection(presumedFather, -30.0, "paternity_secret_" + outcomeType)
        endif
    elseif (outcomeType == "presumed_father_learnt")
        if (presumedFather == player && mother)
            AdjustPlayerPartnerAffection(mother, -40.0, "paternity_secret_" + outcomeType)
        elseif (mother == player && presumedFather)
            AdjustPlayerPartnerAffection(presumedFather, -40.0, "paternity_secret_" + outcomeType)
        endif
    elseif (outcomeType == "mother_confessed_both")
        if (presumedFather == player && mother)
            AdjustPlayerPartnerAffection(mother, -30.0, "paternity_secret_mother_confessed_presumed")
        elseif (bioFather == player && mother)
            AdjustPlayerPartnerAffection(mother, 15.0, "paternity_secret_mother_confessed_bio")
        elseif (mother == player)
            if (presumedFather)
                AdjustPlayerPartnerAffection(presumedFather, -30.0, "paternity_secret_mother_confessed_presumed")
            endif
            if (bioFather)
                AdjustPlayerPartnerAffection(bioFather, 15.0, "paternity_secret_mother_confessed_bio")
            endif
        endif
    else
        float baseDelta = -35.0
        if (presumedFather == player && mother)
            AdjustPlayerPartnerAffection(mother, baseDelta, "paternity_secret_" + outcomeType)
        elseif (mother == player && presumedFather)
            AdjustPlayerPartnerAffection(presumedFather, baseDelta, "paternity_secret_" + outcomeType)
        elseif (bioFather == player && mother)
            AdjustPlayerPartnerAffection(mother, baseDelta, "paternity_secret_" + outcomeType)
        endif
    endif
EndFunction

Function ApplyChildMilestoneForParents(int childID, string milestone = "child") global
    if (childID == 0)
        return
    endif
    Actor player = TTM_Data.GetPlayer()
    Actor bioMother = MARAS.GetBioMother(childID)
    Actor bioFather = MARAS.GetBioFather(childID)
    Actor presumedFather = MARAS.GetMotherPartner(childID)

    if (player == bioMother)
        if (presumedFather)
            ApplyChildMilestoneAffection(presumedFather, milestone)
        elseif (bioFather)
            ApplyChildMilestoneAffection(bioFather, milestone)
        endif
    else
        if (presumedFather == player)
            ApplyChildMilestoneAffection(bioMother, milestone)
        elseif (presumedFather == none && bioFather == player)
            ApplyChildMilestoneAffection(bioMother, milestone)
        endif
    endif
EndFunction



