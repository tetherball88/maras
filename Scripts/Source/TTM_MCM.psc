Scriptname TTM_MCM extends SKI_ConfigBase

GlobalVariable Property MCM_StartDialGender auto
GlobalVariable Property TTM_EnablePolygamyToggle Auto
GlobalVariable Property TTM_MinRelRankForDial Auto

int _oidOpenDashboard = -1

Event OnInit()
    ; New games do not register a SkyUI MCM menu; configuration lives in the Prisma UI Dashboard.
EndEvent

Event OnGameReload()
    ; Ongoing saves where TTM_MCM was already registered keep a minimal stub without re-registering on new games.
    if (ModName != "")
        Pages = Utility.CreateStringArray(0)
    endif
EndEvent

Event OnConfigOpen()
    Pages = Utility.CreateStringArray(0)
EndEvent

Event OnPageReset(string a_page)
    SetCursorFillMode(TOP_TO_BOTTOM)
    AddHeaderOption("M.A.R.A.S Dashboard")
    AddKeyMapOption("Dashboard Hotkey", MARAS.GetSettingInt("toggleUIKey"), OPTION_FLAG_DISABLED)
    _oidOpenDashboard = AddTextOption("Open M.A.R.A.S Dashboard", "OPEN")
EndEvent

Event OnOptionSelect(int a_option)
    if (a_option == _oidOpenDashboard)
        MARAS.ShowUI()
    endif
EndEvent

Event OnOptionHighlight(int a_option)
    if (a_option == _oidOpenDashboard)
        SetInfoText("Closes this menu and opens the M.A.R.A.S Prisma UI Dashboard.")
    endif
EndEvent
