scriptname TTM_MCM_SettingsPage

Function RenderPage(TTM_MCM mcm) global
    mcm.SetCursorFillMode(mcm.TOP_TO_BOTTOM)
    RenderLeftColumn(mcm)
    mcm.SetCursorPosition(1)
    RenderRightColumn(mcm)
EndFunction

string[] Function GetGendersOptions() global
    string[] options = new string[3]
    options[0] = "opposite"
    options[1] = "same"
    options[2] = "all"
    return options
EndFunction

string[] Function GetLogDestinationOptions() global
    string[] options = new string[3]
    options[0] = "file"
    options[1] = "console"
    options[2] = "floating"
    return options
EndFunction

string[] Function GetLogLevelOptions() global
    string[] options = new string[5]
    options[0] = "trace"
    options[1] = "debug"
    options[2] = "warning"
    options[3] = "error"
    options[4] = "none"
    return options
EndFunction

Function RenderLeftColumn(TTM_MCM mcm) global
    mcm.AddHeaderOption("Settings")
    mcm.oid_SkipNextWeddings = mcm.AddToggleOption("Skip wedding ceremonies after first?", TTM_MCM_State.GetSkipWedding())
    mcm.AddHeaderOption("AI dialogues")
    string[] genderOptions = GetGendersOptions()
    mcm.oid_SettingsStartDialGender = mcm.AddMenuOption("Show start dialogue to genders: ", genderOptions[mcm.MCM_StartDialGender.GetValue() as int])
    mcm.oid_SettingsAIMiscDial = mcm.AddToggleOption("Use AI generated misc dialogues: ", mcm.TTM_MCM_UseAIMiscDial.GetValue() as int)
    mcm.oid_SettingsAIQuestDial = mcm.AddToggleOption("Use AI generated quest dialogues: ", mcm.TTM_MCM_UseAIQuestDial.GetValue() as int)
EndFunction

Function RenderRightColumn(TTM_MCM mcm) global
    mcm.AddHeaderOption("Logging")
    string[] logLevelOptions = GetLogLevelOptions()
    mcm.oid_SettingsLogLevel = mcm.AddMenuOption("Log level", logLevelOptions[TTM_MCM_State.GetLogLevel()])
    string[] logDestinationOptions = GetLogDestinationOptions()
    mcm.oid_SettingsLogDestination = mcm.AddMenuOption("Log destination ", logDestinationOptions[TTM_MCM_State.GetLogDestination()])
    mcm.AddHeaderOption("Export/import whole data")
    mcm.oid_SettingsExportData = mcm.AddTextOption("", "Export data to file")
    mcm.oid_SettingsImportData = mcm.AddTextOption("", "Import data from file")
    mcm.oid_SettingsClearData = mcm.AddTextOption("", "Clear whole data")
EndFunction

Function OnOptionSelect(TTM_MCM mcm, int option) global
    if (mcm.oid_SettingsClearData == option)
        bool yes = mcm.ShowMessage("Are you sure you want to clear all data?")
        if(yes)
            TTM_JData.Clear()
        endif
    elseif (mcm.oid_SettingsExportData == option)
        TTM_JData.ExportData()
    elseif (mcm.oid_SettingsImportData == option)
        TTM_JData.ImportData()
    elseif(option == mcm.oid_SettingsStartDialGender)
    elseif(option == mcm.oid_SettingsAIMiscDial)
        mcm.SetToggleOptionValue(option, TTM_Utils.ToggleGlobalVariable(mcm.TTM_MCM_UseAIMiscDial))
        Quest main = TTM_JData.GetMarasMainQuest()
        TTM_Conditions conditions = main as TTM_Conditions
        conditions.CheckUseMiscDialogAI()
    elseif(option == mcm.oid_SettingsAIQuestDial)
        mcm.SetToggleOptionValue(option, TTM_Utils.ToggleGlobalVariable(mcm.TTM_MCM_UseAIQuestDial))
        Quest main = TTM_JData.GetMarasMainQuest()
        TTM_Conditions conditions = main as TTM_Conditions
        conditions.CheckUseQuestDialogAI()
    elseif(option == mcm.oid_SkipNextWeddings)
        bool val = TTM_MCM_State.GetSkipWedding()
        mcm.SetToggleOptionValue(option, !val)
        TTM_MCM_State.SetSkipWedding(!val)
    endif
EndFunction

; Highlight
Function OnOptionHighlight(TTM_MCM mcm, int option) global
    if(option == mcm.oid_SettingsClearData)
        mcm.SetInfoText("Clears whole data from save")
    elseif(option == mcm.oid_SettingsExportData)
        mcm.SetInfoText("Exports json data to file in Documents\\My Games\\Skyrim Special Edition\\JCUser\\MARAS\\store.json")
    elseif(option == mcm.oid_SettingsImportData)
        mcm.SetInfoText("Imports data from file in Documents\\My Games\\Skyrim Special Edition\\JCUser\\MARAS\\store.json")
    elseif(option == mcm.oid_SettingsAIMiscDial)
        mcm.SetInfoText("For Misc Topics (e.g., hellos): \nIf enabled, uses AI-generated dialogue lines on the fly. If disabled, falls back to predefined, unvoiced dialogue.")
    elseif(option == mcm.oid_SettingsAIQuestDial)
        mcm.SetInfoText("For Custom Quest Dialogue: \nIf enabled, uses AI-generated dialogue lines on the fly. If disabled, uses predefined dialogue without voice.")
    elseif(option == mcm.oid_SettingsLogDestination)
        mcm.SetInfoText("Select where logs will be shown/written: to file, in console, as floating message.")
    elseif(option == mcm.oid_SettingsLogLevel)
        mcm.SetInfoText("Sets how much detail is logged.\nHigher levels (Trace, Debug) show more technical data — useful for troubleshooting.\nLower levels (Warning, Error) show only important issues.")
    elseif(option == mcm.oid_SkipNextWeddings)
        mcm.SetInfoText("If enabled - you will have to attend wedding ceremony only once, after that you will get married without Wedding Ceremony quest.")
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
    MiscUtil.PrintConsole("OnOptionMenuOpen:"+options)
    mcm.SetMenuDialogOptions(options)
EndFunction

Function OnOptionMenuAccept(TTM_MCM mcm, int option, int index) global
    string[] options
	if(option == mcm.oid_SettingsStartDialGender)
        options = GetGendersOptions()
        mcm.MCM_StartDialGender.SetValue(index)
    elseif(option == mcm.oid_SettingsLogDestination)
        options = GetLogDestinationOptions()
        TTM_MCM_State.SetLogDestination(index)
    elseif(option == mcm.oid_SettingsLogLevel)
        options = GetLogLevelOptions()
        TTM_MCM_State.SetLogLevel(index)
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
        TTM_MCM_State.SetLogDestination(0)
    elseif(option == mcm.oid_SettingsLogLevel)
        mcm.SetMenuOptionValue(mcm.oid_SettingsLogLevel, 2)
        TTM_MCM_State.SetLogLevel(2)
    endif
EndFunction

Function OnOptionInputOpen(TTM_MCM mcm, int option) global

EndFunction

Function OnOptionInputAccept(TTM_MCM mcm, int option, string value) global

EndFunction
