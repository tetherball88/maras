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
    string[] options = new string[6]
    options[0] = "trace"
    options[1] = "debug"
    options[2] = "info"
    options[3] = "warning"
    options[4] = "error"
    options[5] = "none"
    return options
EndFunction

Function RenderLeftColumn(TTM_MCM mcm) global
    mcm.AddHeaderOption("Settings")
    mcm.oid_SkipNextWeddings = mcm.AddToggleOption("Skip wedding ceremonies after first?", TTM_Data.GetSkipWedding())
    mcm.AddHeaderOption("AI dialogues")
    string[] genderOptions = GetGendersOptions()
    int hasSkyrimNet = 0
    if(!TTM_Data.GetHasSkyrimNet())
        hasSkyrimNet = 1
    endif
    mcm.oid_SettingsStartDialGender = mcm.AddMenuOption("Show start dialogue to genders: ", genderOptions[mcm.MCM_StartDialGender.GetValue() as int])
    mcm.oid_SettingsPreferVanillaAudio= mcm.AddToggleOption("Use vanilla voiced dialogues if possible: ", mcm.TTM_MCM_PreferVanillaAudio.GetValue() as int, hasSkyrimNet)
    mcm.oid_SettingsAllowAIDial = mcm.AddToggleOption("Use AI generated responses in dialogues: ", mcm.TTM_MCM_AllowAIDial.GetValue() as int, hasSkyrimNet)
EndFunction

Function RenderRightColumn(TTM_MCM mcm) global
    mcm.AddHeaderOption("Logging")
    string[] logLevelOptions = GetLogLevelOptions()
    mcm.oid_SettingsLogLevel = mcm.AddMenuOption("Log level", logLevelOptions[MARAS.GetLogLevel()])
    string[] logDestinationOptions = GetLogDestinationOptions()
    mcm.oid_SettingsLogDestination = mcm.AddMenuOption("Log destination ", logDestinationOptions[TTM_Data.GetLogDestination()])
    mcm.AddHeaderOption("Cheats")
    mcm.oid_SettingsCheatAlwaysSuccess = mcm.AddToggleOption("Enable always success for engagement", TTM_Data.GetAlwaysSuccessMarriage())
    ; Debug Spell toggle: add/remove debug spell to player
    Actor player = TTM_Data.GetPlayer()
    bool hasDebug = false
    if(player != none)
        hasDebug = player.HasSpell(TTM_Data.GetDebugSpell())
    endif
    mcm.oid_SettingsCheatDebugSpell = mcm.AddToggleOption("Enable debug spell (grants a debug power)", hasDebug)
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
    endif
EndFunction

; Highlight
Function OnOptionHighlight(TTM_MCM mcm, int option) global
    if(option == mcm.oid_SettingsPreferVanillaAudio)
        mcm.SetInfoText("If true it will try to use vanilla voiced dialogues when possible, otherwise it will use AI generated responses.\n Pros: fast but hardcoded. \nCons: slower but more dynamic.")
    elseif(option == mcm.oid_SettingsAllowAIDial)
        mcm.SetInfoText("If true it will use AI generated responses in quest dialogues, otherwise un-voiced hardcoded dialogues. \n Pros: dynamic and context/persoanlity-aware. \nCons: slower and hardcoded.")
    elseif(option == mcm.oid_SettingsLogDestination)
        mcm.SetInfoText("Select where logs will be shown/written: to file, in console, as floating message.")
    elseif(option == mcm.oid_SettingsLogLevel)
        mcm.SetInfoText("Sets how much detail is logged.\nHigher levels (Trace, Debug) show more technical data — useful for troubleshooting.\nLower levels (Warning, Error) show only important issues.")
    elseif(option == mcm.oid_SkipNextWeddings)
        mcm.SetInfoText("If enabled - you will have to attend wedding ceremony only once, after that you will get married without Wedding Ceremony quest.")
    elseif(option == mcm.oid_SettingsCheatDebugSpell)
        mcm.SetInfoText("Toggles a debug spell on the player. When cast on an NPC, the spell cycles that NPC's MARAS status through: unknown -> candidate -> engaged -> married -> divorced. Useful for testing romance flows and status transitions.")
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
    endif
EndFunction

Function OnOptionInputOpen(TTM_MCM mcm, int option) global

EndFunction

Function OnOptionInputAccept(TTM_MCM mcm, int option, string value) global

EndFunction
