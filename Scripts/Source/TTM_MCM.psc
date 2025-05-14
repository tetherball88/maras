Scriptname TTM_MCM extends SKI_ConfigBase

GlobalVariable Property MCM_StartDialGender auto
GlobalVariable Property TTM_MCM_UseAIMiscDial  Auto
GlobalVariable Property TTM_MCM_UseAIQuestDial  Auto

int property oid_SearchSpouse auto
int property oid_ReturnToExplore auto
int property oid_SettingsClearData auto
int property oid_SettingsExportData auto
int property oid_SettingsImportData auto
int property oid_SettingsStartDialGender auto
int property oid_SettingsAIMiscDial auto
int property oid_SettingsAIQuestDial auto
int property oid_SettingsLogLevel auto
int property oid_SettingsLogDestination auto
int property oid_SkipNextWeddings auto

int property oid_SpousePageSkillType auto
int property oid_SpousePageSocialClass auto
int property oid_SpousePageRank auto

string property SearchValueSpouse auto
string property SearchValueLover auto

string selectedPage

Event OnConfigInit()
    ModName = "M.A.R.A.S"
    Pages = new string[3]

    Pages[0] = "Explore"
    Pages[1] = "Settings"
    Pages[2] = "Spouse buff"
    TTM_MCM_State.SetCurrentPage("Explore")
EndEvent

Event OnPageReset(string page)
    if(page == "Explore")
        string subPage = TTM_MCM_State.GetCurrentPage()
        if(subPage == "Spouse")
            TTM_MCM_SpousePage.RenderPage(self)
        else
            TTM_MCM_ExplorePage.RenderPage(self)
        endif
    elseif(page == "Settings")
        TTM_MCM_State.Clean()
        TTM_MCM_SettingsPage.RenderPage(self)
    elseif(page == "Spouse buff")
        TTM_MCM_State.Clean()
        TTM_MCM_CurrentBuff.RenderPage(self)
    endif
EndEvent

; Select
event OnOptionSelect(int option)
    if(currentPage == "Explore")
        string subPage = TTM_MCM_State.GetCurrentPage()
        if(subPage == "Spouse")
            TTM_MCM_SpousePage.OnOptionSelect(self, option)
        else
            TTM_MCM_ExplorePage.OnOptionSelect(self, option)
        endif
    elseif(currentPage == "Settings")
        TTM_MCM_SettingsPage.OnOptionSelect(self, option)
    elseif(currentPage == "Spouse buff")
        TTM_MCM_CurrentBuff.OnOptionSelect(self, option)
    endif
endevent

; Highlight
event OnOptionHighlight(int option)
    if(currentPage == "Explore")
        string subPage = TTM_MCM_State.GetCurrentPage()
        if(subPage == "Spouse")
            TTM_MCM_SpousePage.OnOptionHighlight(self, option)
        else
            TTM_MCM_ExplorePage.OnOptionHighlight(self, option)
        endif
    elseif(currentPage == "Settings")
        TTM_MCM_SettingsPage.OnOptionHighlight(self, option)
    elseif(currentPage == "Spouse buff")
        TTM_MCM_CurrentBuff.OnOptionHighlight(self, option)
    endif
endevent

; Default
event OnOptionDefault(int option)
    if(currentPage == "Explore")
        string subPage = TTM_MCM_State.GetCurrentPage()
        if(subPage == "Spouse")
            TTM_MCM_SpousePage.OnOptionDefault(self, option)
        else
            TTM_MCM_ExplorePage.OnOptionDefault(self, option)
        endif
    elseif(currentPage == "Settings")
        TTM_MCM_SettingsPage.OnOptionDefault(self, option)
    elseif(currentPage == "Spouse buff")
        TTM_MCM_CurrentBuff.OnOptionDefault(self, option)
    endif
endevent

event OnOptionInputOpen(int option)
    if(currentPage == "Explore")
        string subPage = TTM_MCM_State.GetCurrentPage()
        if(subPage == "Spouse")
            TTM_MCM_SpousePage.OnOptionInputOpen(self, option)
        else
            TTM_MCM_ExplorePage.OnOptionInputOpen(self, option)
        endif
    elseif(currentPage == "Settings")
        TTM_MCM_SettingsPage.OnOptionInputOpen(self, option)
    elseif(currentPage == "Spouse buff")
        TTM_MCM_CurrentBuff.OnOptionInputOpen(self, option)
    endif
endEvent

event OnOptionInputAccept(int option, string value)
    if(currentPage == "Explore")
        string subPage = TTM_MCM_State.GetCurrentPage()
        if(subPage == "Spouse")
            TTM_MCM_SpousePage.OnOptionInputAccept(self, option, value)
        else
            TTM_MCM_ExplorePage.OnOptionInputAccept(self, option, value)
        endif
    elseif(currentPage == "Settings")
        TTM_MCM_SettingsPage.OnOptionInputAccept(self, option, value)
    elseif(currentPage == "Spouse buff")
        TTM_MCM_CurrentBuff.OnOptionInputAccept(self, option, value)
    endif
endEvent

event OnOptionMenuOpen(int a_option)
    if(currentPage == "Settings")
        TTM_MCM_SettingsPage.OnOptionMenuOpen(self, a_option)
    endif
endEvent

event OnOptionMenuAccept(int a_option, int a_index)
	if(currentPage == "Settings")
        TTM_MCM_SettingsPage.OnOptionMenuAccept(self, a_option, a_index)
    endif
endEvent

event OnConfigClose()
    TTM_MCM_State.Clean()
endEvent

Function Navigate(string page)
    TTM_MCM_State.SetCurrentPage(page)
    ForcePageReset()
EndFunction
