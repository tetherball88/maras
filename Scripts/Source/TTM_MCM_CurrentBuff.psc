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
    string[] types = TTM_Utils.GetSpouseSkillTypeByIndexes()

    while(i < types.Length)
        TTM_MCM_State.AddSpouseTypeOption(mcm.AddTextOption(types[i], multipliers[i]), types[i])
        i += 1
    endwhile
EndFunction

Function RenderRightColumn(TTM_MCM mcm) global
    mcm.AddHeaderOption("Permanent buff from spouses")

    int i = 0

    float[] multipliers = TTM_ServiceBuff.GetPermanentMultipliers()
    string[] types = TTM_Utils.GetSpouseSocialTypeByIndexes()

    while(i < types.Length)
        TTM_MCM_State.AddSpouseTypeOption(mcm.AddTextOption(types[i], multipliers[i]), types[i])
        i += 1
    endwhile
EndFunction

Function OnOptionSelect(TTM_MCM mcm, int option) global
    if (mcm.oid_SettingsClearData == option)

    elseif (mcm.oid_SettingsExportData == option)

    elseif (mcm.oid_SettingsImportData == option)

    endif
EndFunction

; Highlight
Function OnOptionHighlight(TTM_MCM mcm, int option) global
    string spouseType = TTM_MCM_State.GetSpouseTypeOption(option)
    if(spouseType != "")
        float[] multipliers
        int typeIndex = TTM_Utils.GetSpouseSkillIndexByType(spouseType)
        if(typeIndex == -1)
            multipliers = TTM_ServiceBuff.GetPermanentMultipliers()
            typeIndex = TTM_Utils.GetSpouseSocialIndexByType(spouseType)
        else
            multipliers = TTM_ServiceBuff.GetFollowersMultipliers()
        endif

        if(typeIndex != -1)
            float bonusValue = TTM_ServiceBuff.GetBonusPerkValue(spouseType, 0)
            string bonusDescription = TTM_ServiceBuff.GetBonusPerkDescription(spouseType, 0)
            string bonusUnit = TTM_ServiceBuff.GetBonusPerkUnit(spouseType, 0)
            float value = bonusValue * multipliers[typeIndex]
            if(bonusDescription != "")
                mcm.SetInfoText(bonusDescription + " " + value + " " + bonusUnit)
            endif
        endif
    elseif(option == mcm.oid_SettingsExportData)

    elseif(option == mcm.oid_SettingsClearData)

    endif

EndFunction

; Default
Function OnOptionDefault(TTM_MCM mcm, int option) global

EndFunction

Function OnOptionInputOpen(TTM_MCM mcm, int option) global

EndFunction

Function OnOptionInputAccept(TTM_MCM mcm, int option, string value) global

EndFunction
