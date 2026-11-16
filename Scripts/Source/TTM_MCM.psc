Scriptname TTM_MCM extends SKI_ConfigBase

GlobalVariable Property MCM_StartDialGender auto
GlobalVariable Property TTM_EnablePolygamyToggle Auto
GlobalVariable Property TTM_MinRelRankForDial Auto

int _oidOpenDashboard = -1

Event OnInit()
    ; New games do not register a SkyUI MCM menu; configuration lives in the Magelight UI Dashboard.
EndEvent

Event OnGameReload()
    ; Ongoing saves where TTM_MCM was already registered keep a minimal stub without re-registering on new games.
    if (ModName != "")
        parent.OnGameReload()
        Pages = Utility.CreateStringArray(0)
    endif
EndEvent

Event OnConfigOpen()
    Pages = Utility.CreateStringArray(0)
EndEvent

Event OnPageReset(string a_page)
    SetCursorFillMode(TOP_TO_BOTTOM)
    AddHeaderOption(MARAS.TranslateKey("mcm.header"))
    string modPrefix = ""
    if (MARAS.GetSettingBool("toggleUIModCtrl"))
        modPrefix = "Ctrl"
    endif
    if (MARAS.GetSettingBool("toggleUIModShift"))
        if (modPrefix != "")
            modPrefix += "+Shift"
        else
            modPrefix = "Shift"
        endif
    endif
    if (MARAS.GetSettingBool("toggleUIModAlt"))
        if (modPrefix != "")
            modPrefix += "+Alt"
        else
            modPrefix = "Alt"
        endif
    endif
    string hotkeyLabel = MARAS.TranslateKey("mcm.hotkey_label")
    if (modPrefix != "")
        hotkeyLabel = MARAS.TranslateKey("mcm.hotkey_label_with_mods", modPrefix)
    endif
    AddKeyMapOption(hotkeyLabel, MARAS.GetSettingInt("toggleUIKey"), OPTION_FLAG_DISABLED)
    _oidOpenDashboard = AddTextOption(MARAS.TranslateKey("mcm.open_label"), MARAS.TranslateKey("mcm.open_value"))
EndEvent

Event OnOptionSelect(int a_option)
    if (a_option == _oidOpenDashboard)
        MARAS.ShowUI()
    endif
EndEvent

Event OnOptionHighlight(int a_option)
    if (a_option == _oidOpenDashboard)
        SetInfoText(MARAS.TranslateKey("mcm.open_tooltip"))
    endif
EndEvent
