scriptname TTM_MCM_SettingsPage

Function RenderPage(TTM_MCM mcm) global
    mcm.SetCursorFillMode(mcm.TOP_TO_BOTTOM)
    RenderLeftColumn(mcm)
    mcm.SetCursorPosition(1)
    RenderRightColumn(mcm)
EndFunction

string[] Function GetGendersOptions() global
    string[] options = new string[3]
    options[0] = "$TTM_MCM_GenderOpposite"
    options[1] = "$TTM_MCM_GenderSame"
    options[2] = "$TTM_MCM_GenderAll"
    return options
EndFunction

string[] Function GetLogDestinationOptions() global
    string[] options = new string[3]
    options[0] = "$TTM_MCM_LogDestFile"
    options[1] = "$TTM_MCM_LogDestConsole"
    options[2] = "$TTM_MCM_LogDestFloating"
    return options
EndFunction

string[] Function GetLogLevelOptions() global
    string[] options = new string[6]
    options[0] = "$TTM_MCM_LogLevelTrace"
    options[1] = "$TTM_MCM_LogLevelDebug"
    options[2] = "$TTM_MCM_LogLevelInfo"
    options[3] = "$TTM_MCM_LogLevelWarning"
    options[4] = "$TTM_MCM_LogLevelError"
    options[5] = "$TTM_MCM_LogLevelNone"
    return options
EndFunction

Function RenderLeftColumn(TTM_MCM mcm) global
    mcm.AddHeaderOption("$TTM_MCM_HeaderSettings")
    mcm.oid_SkipNextWeddings = mcm.AddToggleOption("$TTM_MCM_SkipWedding", TTM_Data.GetSkipWedding())
    mcm.oid_AffectionDecayMult = mcm.AddSliderOption("$TTM_MCM_AffectionDecayMult", MARAS.GetAffectionDecayMultiplier(), "{1}x")
    mcm.AddHeaderOption("$TTM_MCM_HeaderAIDialogues")
    string[] genderOptions = GetGendersOptions()
    int hasSkyrimNet = 0
    if(!TTM_Data.GetHasSkyrimNet())
        hasSkyrimNet = 1
    endif
    mcm.oid_SettingsStartDialGender = mcm.AddMenuOption("$TTM_MCM_StartDialGender", genderOptions[mcm.MCM_StartDialGender.GetValue() as int])
    mcm.oid_MinRelRankForDial = mcm.AddSliderOption("$TTM_MCM_MinRelRankForDial", mcm.TTM_MinRelRankForDial.GetValue(), "{0}")
    mcm.AddHeaderOption("$TTM_MCM_HeaderSettingsSkyrimNet")
    mcm.oid_SettingsPreferVanillaAudio= mcm.AddToggleOption("$TTM_MCM_PreferVanillaAudio", mcm.TTM_MCM_PreferVanillaAudio.GetValue(), hasSkyrimNet)
    mcm.oid_SettingsAllowAIDial = mcm.AddToggleOption("$TTM_MCM_AllowAIDial", mcm.TTM_MCM_AllowAIDial.GetValue(), hasSkyrimNet)
    mcm.AddHeaderOption("$TTM_MCM_HeaderSettingsSkyrimNetActionsConfirmation")
    mcm.oid_ConfirmAcceptProposal = mcm.AddToggleOption("$TTM_MCM_ConfirmAcceptProposal", TTM_MCM_State.GetConfirmAcceptProposal(), hasSkyrimNet)
    mcm.oid_ConfirmBreakupEngagement = mcm.AddToggleOption("$TTM_MCM_ConfirmBreakupEngagement", TTM_MCM_State.GetConfirmBreakupEngagement(), hasSkyrimNet)
    mcm.oid_ConfirmDivorce = mcm.AddToggleOption("$TTM_MCM_ConfirmDivorce", TTM_MCM_State.GetConfirmDivorce(), hasSkyrimNet)
    mcm.oid_ConfirmDivorceDuringLowAffectionQuest = mcm.AddToggleOption("$TTM_MCM_ConfirmDivorceDuringLowAffectionQuest", TTM_MCM_State.GetConfirmDivorceDuringLowAffectionQuest(), hasSkyrimNet)
EndFunction

Function RenderRightColumn(TTM_MCM mcm) global
    mcm.AddHeaderOption("$TTM_MCM_HeaderLogging")
    string[] logLevelOptions = GetLogLevelOptions()
    mcm.oid_SettingsLogLevel = mcm.AddMenuOption("$TTM_MCM_LogLevel", logLevelOptions[MARAS.GetLogLevel()])
    string[] logDestinationOptions = GetLogDestinationOptions()
    mcm.oid_SettingsLogDestination = mcm.AddMenuOption("$TTM_MCM_LogDestination", logDestinationOptions[TTM_Data.GetLogDestination()])
    mcm.AddHeaderOption("$TTM_MCM_HeaderCheats")
    mcm.oid_SettingsCheatAlwaysSuccess = mcm.AddToggleOption("$TTM_MCM_CheatAlwaysSuccess", TTM_Data.GetAlwaysSuccessMarriage())
    ; Debug Spell toggle: add/remove debug spell to player
    Actor player = TTM_Data.GetPlayer()
    bool hasDebug = false
    if(player != none)
        hasDebug = player.HasSpell(TTM_Data.GetDebugSpell())
    endif
    mcm.oid_SettingsCheatDebugSpell = mcm.AddToggleOption("$TTM_MCM_CheatDebugSpell", hasDebug)
    mcm.oid_EnablePolygamyToggle = mcm.AddToggleOption("$TTM_MCM_EnablePolygamy", mcm.TTM_EnablePolygamyToggle.GetValue() as int)
EndFunction

Function OnOptionSelect(TTM_MCM mcm, int option) global
    if(option == mcm.oid_SettingsStartDialGender)
    elseif(option == mcm.oid_SettingsPreferVanillaAudio)
        mcm.SetToggleOptionValue(option, TTM_Utils.ToggleGlobalVariable(mcm.TTM_MCM_PreferVanillaAudio))
        Quest main = TTM_Data.GetMarasMainQuest()
        TTM_Conditions conditions = main as TTM_Conditions
        conditions.Maintenance()
    elseif(option == mcm.oid_SettingsAllowAIDial)
        mcm.SetToggleOptionValue(option, TTM_Utils.ToggleGlobalVariable(mcm.TTM_MCM_AllowAIDial))
        Quest main = TTM_Data.GetMarasMainQuest()
        TTM_Conditions conditions = main as TTM_Conditions
        conditions.Maintenance()
    elseif(option == mcm.oid_SkipNextWeddings)
        bool val = TTM_Data.GetSkipWedding()
        mcm.SetToggleOptionValue(option, !val)
        TTM_Data.SetSkipWedding(!val)
    elseif(option == mcm.oid_SettingsCheatAlwaysSuccess)
        bool val = TTM_Data.GetAlwaysSuccessMarriage()
        mcm.SetToggleOptionValue(option, !val)
        TTM_Data.SetAlwaysSuccessMarriage(!val)
    elseif(option == mcm.oid_SettingsCheatDebugSpell)
        Actor player = TTM_Data.GetPlayer()
        Spell debugSpell = TTM_Data.GetDebugSpell()
        if(player != none && debugSpell != none)
            bool has = player.HasSpell(debugSpell)
            if(!has)
                player.AddSpell(debugSpell)
                mcm.SetToggleOptionValue(option, 1)
            else
                player.RemoveSpell(debugSpell)
                mcm.SetToggleOptionValue(option, 0)
            endif
        endif
    elseif(option == mcm.oid_EnablePolygamyToggle)
        mcm.SetToggleOptionValue(option, TTM_Utils.ToggleGlobalVariable(mcm.TTM_EnablePolygamyToggle))
    elseif(option == mcm.oid_ConfirmAcceptProposal)
        bool newVal = !TTM_MCM_State.GetConfirmAcceptProposal()
        mcm.SetToggleOptionValue(option, newVal)
        TTM_MCM_State.SetConfirmAcceptProposal(newVal)
    elseif(option == mcm.oid_ConfirmBreakupEngagement)
        bool newVal = !TTM_MCM_State.GetConfirmBreakupEngagement()
        mcm.SetToggleOptionValue(option, newVal)
        TTM_MCM_State.SetConfirmBreakupEngagement(newVal)
    elseif(option == mcm.oid_ConfirmDivorce)
        bool newVal = !TTM_MCM_State.GetConfirmDivorce()
        mcm.SetToggleOptionValue(option, newVal)
        TTM_MCM_State.SetConfirmDivorce(newVal)
    elseif(option == mcm.oid_ConfirmDivorceDuringLowAffectionQuest)
        bool newVal = !TTM_MCM_State.GetConfirmDivorceDuringLowAffectionQuest()
        mcm.SetToggleOptionValue(option, newVal)
        TTM_MCM_State.SetConfirmDivorceDuringLowAffectionQuest(newVal)
    elseif(option == mcm.oid_ConfirmDivorceDuringLowAffectionQuest)
        bool newVal = !TTM_MCM_State.GetConfirmDivorceDuringLowAffectionQuest()
        mcm.SetToggleOptionValue(option, newVal)
        TTM_MCM_State.SetConfirmDivorceDuringLowAffectionQuest(newVal)
    endif
EndFunction

; Highlight
Function OnOptionHighlight(TTM_MCM mcm, int option) global
    if(option == mcm.oid_SettingsPreferVanillaAudio)
        mcm.SetInfoText("$TTM_MCM_TT_PreferVanilla")
    elseif(option == mcm.oid_SettingsAllowAIDial)
        mcm.SetInfoText("$TTM_MCM_TT_AllowAIDial")
    elseif(option == mcm.oid_SettingsLogDestination)
        mcm.SetInfoText("$TTM_MCM_TT_LogDestination")
    elseif(option == mcm.oid_SettingsLogLevel)
        mcm.SetInfoText("$TTM_MCM_TT_LogLevel")
    elseif(option == mcm.oid_SkipNextWeddings)
        mcm.SetInfoText("$TTM_MCM_TT_SkipWedding")
    elseif(option == mcm.oid_SettingsCheatDebugSpell)
        mcm.SetInfoText("$TTM_MCM_TT_DebugSpell")
    elseif(option == mcm.oid_EnablePolygamyToggle)
        mcm.SetInfoText("$TTM_MCM_TT_EnablePolygamy")
    elseif(option == mcm.oid_MinRelRankForDial)
        mcm.SetInfoText("$TTM_MCM_TT_MinRelRank")
    elseif(option == mcm.oid_AffectionDecayMult)
        mcm.SetInfoText("$TTM_MCM_TT_AffectionDecayMult")
    endif
EndFunction

Function OnOptionMenuOpen(TTM_MCM mcm, int option) global
    string[] options
    if(option == mcm.oid_SettingsStartDialGender)
        options = GetGendersOptions()
        ; TTM_MCM_SettingsPage.OnOptionMenuOpen(a_option)
    elseif(option == mcm.oid_SettingsLogDestination)
        options = GetLogDestinationOptions()
    elseif(option == mcm.oid_SettingsLogLevel)
        options = GetLogLevelOptions()
    endif
    mcm.SetMenuDialogOptions(options)
EndFunction

Function OnOptionMenuAccept(TTM_MCM mcm, int option, int index) global
    string[] options
	if(option == mcm.oid_SettingsStartDialGender)
        options = GetGendersOptions()
        mcm.MCM_StartDialGender.SetValue(index)
    elseif(option == mcm.oid_SettingsLogDestination)
        options = GetLogDestinationOptions()
        TTM_Data.SetLogDestination(index)
    elseif(option == mcm.oid_SettingsLogLevel)
        options = GetLogLevelOptions()
        MARAS.SetLogLevel(index)
    endif

    mcm.SetMenuOptionValue(option, options[index])
EndFunction

; Default
Function OnOptionDefault(TTM_MCM mcm, int option) global
    if(option == mcm.oid_SettingsStartDialGender)
        mcm.SetMenuOptionValue(mcm.oid_SettingsStartDialGender, 0)
        mcm.MCM_StartDialGender.SetValue(0)
    elseif(option == mcm.oid_SettingsLogDestination)
        mcm.SetMenuOptionValue(mcm.oid_SettingsLogDestination, 0)
        TTM_Data.SetLogDestination(0)
    elseif(option == mcm.oid_SettingsLogLevel)
        mcm.SetMenuOptionValue(mcm.oid_SettingsLogLevel, 2)
        MARAS.SetLogLevel(2)
    elseif(option == mcm.oid_SettingsCheatDebugSpell)
        Actor _player = TTM_Data.GetPlayer()
        Spell _debug = TTM_Data.GetDebugSpell()
        if(_player != none && _debug != none)
            _player.RemoveSpell(_debug)
        endif
        mcm.SetToggleOptionValue(mcm.oid_SettingsCheatDebugSpell, 0)
    elseif(option == mcm.oid_MinRelRankForDial)
        mcm.TTM_MinRelRankForDial.SetValue(-4)
        mcm.SetSliderOptionValue(option, -4, "{0}")
    elseif(option == mcm.oid_AffectionDecayMult)
        MARAS.SetAffectionDecayMultiplier(1.0)
        mcm.SetSliderOptionValue(option, 1.0, "{1}x")
    endif
EndFunction

Function OnOptionInputOpen(TTM_MCM mcm, int option) global

EndFunction

Function OnOptionInputAccept(TTM_MCM mcm, int option, string value) global

EndFunction

Function OnOptionSliderOpen(TTM_MCM mcm, int option) global
    if(option == mcm.oid_MinRelRankForDial)
        mcm.SetSliderDialogStartValue(mcm.TTM_MinRelRankForDial.GetValue())
        mcm.SetSliderDialogDefaultValue(-4)
        mcm.SetSliderDialogRange(-4, 4)
        mcm.SetSliderDialogInterval(1)
    elseif(option == mcm.oid_AffectionDecayMult)
        mcm.SetSliderDialogStartValue(MARAS.GetAffectionDecayMultiplier())
        mcm.SetSliderDialogDefaultValue(1.0)
        mcm.SetSliderDialogRange(0.0, 2.0)
        mcm.SetSliderDialogInterval(0.1)
    endif
EndFunction

Function OnOptionSliderAccept(TTM_MCM mcm, int option, float value) global
    if(option == mcm.oid_MinRelRankForDial)
        mcm.TTM_MinRelRankForDial.SetValue(value)
        mcm.SetSliderOptionValue(option, value, "{0}")
    elseif(option == mcm.oid_AffectionDecayMult)
        MARAS.SetAffectionDecayMultiplier(value)
        mcm.SetSliderOptionValue(option, value, "{1}x")
    endif
EndFunction
