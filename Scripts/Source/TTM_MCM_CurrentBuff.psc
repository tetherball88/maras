scriptname TTM_MCM_CurrentBuff

Function RenderPage(TTM_MCM mcm) global
    mcm.SetCursorFillMode(mcm.TOP_TO_BOTTOM)
    RenderLeftColumn(mcm)
    mcm.SetCursorPosition(1)
    RenderRightColumn(mcm)
EndFunction

Function RenderLeftColumn(TTM_MCM mcm) global
    mcm.AddHeaderOption("Follower buff from spouses")
    Actor[] followers = PO3_SKSEFunctions.GetPlayerFollowers()
    float[] followerSkillMultipliers = Utility.CreateFloatArray(6)
    float[] followerSocialMultipliers = Utility.CreateFloatArray(8)

    int i = 0

    float[] multipliers = TTM_ServiceBuff.GetFollowersMultipliers()
    string[] types = MARAS.GetNpcTypes("skillType")

    while(i < types.Length)
        TTM_MCM_State.AddNpcSkillTypeOption(mcm.AddTextOption(types[i], multipliers[i]), types[i])
        i += 1
    endwhile
EndFunction

Function RenderRightColumn(TTM_MCM mcm) global
    mcm.AddHeaderOption("Permanent buff from spouses")

    int i = 0

    float[] multipliers = MARAS.GetPermanentMultipliers()
    string[] types = MARAS.GetNpcTypes("socialClass")

    while(i < types.Length)
        TTM_MCM_State.AddNpcSocialTypeOption(mcm.AddTextOption(types[i], multipliers[i]), types[i])
        i += 1
    endwhile
EndFunction

Function OnOptionSelect(TTM_MCM mcm, int option) global

EndFunction

; Highlight
Function OnOptionHighlight(TTM_MCM mcm, int option) global
    string skillType = TTM_MCM_State.GetNpcSkillTypeOption(option)
    string socialType = TTM_MCM_State.GetNpcSocialTypeOption(option)
    int typeIndex = -1
    string npcType = ""
    float[] multipliers
    if(skillType != "")
        multipliers = TTM_ServiceBuff.GetFollowersMultipliers()
        typeIndex = MARAS.GetNpcTypeEnum("skillType", skillType)
        npcType = skillType
    elseif(socialType != "")
        multipliers = MARAS.GetPermanentMultipliers()
        typeIndex = MARAS.GetNpcTypeEnum("socialClass", socialType)
        npcType = socialType
    endif
    if(npcType != "" && typeIndex != -1)
        float bonusValue = MARAS.GetBonusPerkValue(npcType, 0)
        string bonusDescription = MARAS.GetBonusPerkDescription(npcType, 0)
        string bonusUnit = MARAS.GetBonusPerkUnit(npcType, 0)
        float value = bonusValue * multipliers[typeIndex]
        if(bonusDescription != "")
            mcm.SetInfoText(bonusDescription + " " + value + " " + bonusUnit)
        endif
    endif

EndFunction

; Default
Function OnOptionDefault(TTM_MCM mcm, int option) global

EndFunction

Function OnOptionInputOpen(TTM_MCM mcm, int option) global

EndFunction

Function OnOptionInputAccept(TTM_MCM mcm, int option, string value) global

EndFunction
