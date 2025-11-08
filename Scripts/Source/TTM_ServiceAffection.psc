scriptname TTM_ServiceAffection

Function Maintenance() global
    MARAS.SetAffectionMinMax("intimacy", -22, 22)
    MARAS.SetAffectionMinMax("gift", 0, 15)
    MARAS.SetAffectionMinMax("dialogueStarted", 0, 5)
    MARAS.SetAffectionMinMax("slept", 0, 8)
    MARAS.SetAffectionMinMax("promotion", -24, 24)
    MARAS.SetAffectionMinMax("loneliness", -4, 6)
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

