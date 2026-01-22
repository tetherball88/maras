Scriptname TTM_MCM extends SKI_ConfigBase

GlobalVariable Property MCM_StartDialGender auto
GlobalVariable Property TTM_MCM_PreferVanillaAudio  Auto
GlobalVariable Property TTM_MCM_AllowAIDial  Auto
GlobalVariable Property TTM_EnablePolygamyToggle Auto
GlobalVariable Property TTM_MinRelRankForDial Auto


int property oid_SettingsStartDialGender auto
int property oid_SettingsAllowAIDial auto
int property oid_SettingsPreferVanillaAudio auto
int property oid_SettingsLogLevel auto
int property oid_SettingsLogDestination auto
int property oid_SettingsCheatAlwaysSuccess auto
int property oid_SkipNextWeddings auto
int property oid_SettingsCheatDebugSpell auto
int property oid_EnablePolygamyToggle auto
int property oid_MinRelRankForDial auto
int property oid_ConfirmAcceptProposal auto
int property oid_ConfirmBreakupEngagement auto
int property oid_ConfirmDivorce auto
int property oid_ConfirmDivorceDuringLowAffectionQuest auto
int property oid_AffectionDecayMult auto

int property oid_SearchNpc auto
int property oid_SearchFilterAll auto
int property oid_SearchFilterCandidate auto
int property oid_SearchFilterFiance auto
int property oid_SearchFilterSpouse auto
int property oid_SearchFilterJilted auto
int property oid_SearchFilterDivorced auto

int property oid_ReturnToExplore auto
int property oid_NpcPageSkillType auto
int property oid_NpcPageSocialClass auto
int property oid_NpcPageTemperament auto
int property oid_NpcPageRank auto
int property oid_CandidateChance auto
int property oid_NpcPageAffection auto
int property oid_NpcPagePlayerHome auto
int property oid_NpcPageShareTheirHome auto

string property SearchValueNpc auto
string property SearchValueLover auto

string selectedPage

bool property oneTimeUpdate auto

int Function GetVersion()
	Return 2
EndFunction

Event OnVersionUpdate(int Version)
	SetupPages()
EndEvent


Event OnConfigInit()
    ModName = "M.A.R.A.S"
    SetupPages()
EndEvent

Function SetupPages()
    Pages = new string[3]

    Pages[0] = "$TTM_MCM_PageExplore"
    Pages[1] = "$TTM_MCM_PageSettings"
    Pages[2] = "$TTM_MCM_PageSpouseBuff"
    TTM_MCM_State.SetCurrentPage("$TTM_MCM_PageExplore")
EndFunction

Event OnPageReset(string page)
    if(!oneTimeUpdate)
        SetupPages()
        oneTimeUpdate = true
    endif

    if(TTM_MCM_PreferVanillaAudio == none)
        TTM_MCM_PreferVanillaAudio = Game.GetFormFromFile(0x18, "TT_MARAS.esp") as GlobalVariable
    endif
    if(TTM_MCM_AllowAIDial == none)
        TTM_MCM_AllowAIDial = Game.GetFormFromFile(0x1c, "TT_MARAS.esp") as GlobalVariable
    endif
    if(page == "$TTM_MCM_PageExplore")
        string subPage = TTM_MCM_State.GetCurrentPage()
        if(subPage == "Npc")
            TTM_MCM_NpcPage.RenderPage(self)
        else
            TTM_MCM_ExplorePage.RenderPage(self)
        endif
    elseif(page == "$TTM_MCM_PageSettings")
        TTM_MCM_State.Clean()
        TTM_MCM_SettingsPage.RenderPage(self)
    elseif(page == "$TTM_MCM_PageSpouseBuff")
        TTM_MCM_State.Clean()
        TTM_MCM_CurrentBuff.RenderPage(self)
    endif
EndEvent

; Select
event OnOptionSelect(int option)
    if(currentPage == "$TTM_MCM_PageExplore")
        string subPage = TTM_MCM_State.GetCurrentPage()
        if(subPage == "Npc")
            TTM_MCM_NpcPage.OnOptionSelect(self, option)
        else
            TTM_MCM_ExplorePage.OnOptionSelect(self, option)
        endif
    elseif(currentPage == "$TTM_MCM_PageSettings")
        TTM_MCM_SettingsPage.OnOptionSelect(self, option)
    elseif(currentPage == "$TTM_MCM_PageSpouseBuff")
        TTM_MCM_CurrentBuff.OnOptionSelect(self, option)
    endif
endevent

; Highlight
event OnOptionHighlight(int option)
    if(currentPage == "$TTM_MCM_PageExplore")
        string subPage = TTM_MCM_State.GetCurrentPage()
        if(subPage == "Npc")
            TTM_MCM_NpcPage.OnOptionHighlight(self, option)
        else
            TTM_MCM_ExplorePage.OnOptionHighlight(self, option)
        endif
    elseif(currentPage == "$TTM_MCM_PageSettings")
        TTM_MCM_SettingsPage.OnOptionHighlight(self, option)
    elseif(currentPage == "$TTM_MCM_PageSpouseBuff")
        TTM_MCM_CurrentBuff.OnOptionHighlight(self, option)
    endif
endevent

; Default
event OnOptionDefault(int option)
    if(currentPage == "$TTM_MCM_PageExplore")
        string subPage = TTM_MCM_State.GetCurrentPage()
        if(subPage == "Npc")
            TTM_MCM_NpcPage.OnOptionDefault(self, option)
        else
            TTM_MCM_ExplorePage.OnOptionDefault(self, option)
        endif
    elseif(currentPage == "$TTM_MCM_PageSettings")
        TTM_MCM_SettingsPage.OnOptionDefault(self, option)
    elseif(currentPage == "$TTM_MCM_PageSpouseBuff")
        TTM_MCM_CurrentBuff.OnOptionDefault(self, option)
    endif
endevent

event OnOptionInputOpen(int option)
    if(currentPage == "$TTM_MCM_PageExplore")
        string subPage = TTM_MCM_State.GetCurrentPage()
        if(subPage == "Npc")
            TTM_MCM_NpcPage.OnOptionInputOpen(self, option)
        else
            TTM_MCM_ExplorePage.OnOptionInputOpen(self, option)
        endif
    elseif(currentPage == "$TTM_MCM_PageSettings")
        TTM_MCM_SettingsPage.OnOptionInputOpen(self, option)
    elseif(currentPage == "$TTM_MCM_PageSpouseBuff")
        TTM_MCM_CurrentBuff.OnOptionInputOpen(self, option)
    endif
endEvent

event OnOptionInputAccept(int option, string value)
    if(currentPage == "$TTM_MCM_PageExplore")
        string subPage = TTM_MCM_State.GetCurrentPage()
        if(subPage == "Npc")
            TTM_MCM_NpcPage.OnOptionInputAccept(self, option, value)
        else
            TTM_MCM_ExplorePage.OnOptionInputAccept(self, option, value)
        endif
    elseif(currentPage == "$TTM_MCM_PageSettings")
        TTM_MCM_SettingsPage.OnOptionInputAccept(self, option, value)
    elseif(currentPage == "$TTM_MCM_PageSpouseBuff")
        TTM_MCM_CurrentBuff.OnOptionInputAccept(self, option, value)
    endif
endEvent

event OnOptionMenuOpen(int a_option)
    if(currentPage == "$TTM_MCM_PageExplore")
        TTM_MCM_ExplorePage.OnOptionMenuOpen(self, a_option)
        string subPage = TTM_MCM_State.GetCurrentPage()
        if(subPage == "Npc")
            TTM_MCM_NpcPage.OnOptionMenuOpen(self, a_option)
        endif
    elseif(currentPage == "$TTM_MCM_PageSettings")
        TTM_MCM_SettingsPage.OnOptionMenuOpen(self, a_option)
    endif
endEvent

event OnOptionMenuAccept(int a_option, int a_index)
    if(currentPage == "$TTM_MCM_PageExplore")
        TTM_MCM_ExplorePage.OnOptionMenuAccept(self, a_option, a_index)
        string subPage = TTM_MCM_State.GetCurrentPage()
        if(subPage == "Npc")
            TTM_MCM_NpcPage.OnOptionMenuAccept(self, a_option, a_index)
        endif
	elseif(currentPage == "$TTM_MCM_PageSettings")
        TTM_MCM_SettingsPage.OnOptionMenuAccept(self, a_option, a_index)
    endif
endEvent

event OnOptionSliderOpen(int option)
    if(currentPage == "$TTM_MCM_PageExplore")
        string subPage = TTM_MCM_State.GetCurrentPage()
        if(subPage == "Npc")
            TTM_MCM_NpcPage.OnOptionSliderOpen(self, option)
        endif
    elseif(currentPage == "$TTM_MCM_PageSettings")
        TTM_MCM_SettingsPage.OnOptionSliderOpen(self, option)
    endif
endEvent

event OnOptionSliderAccept(int option, float value)
    if(currentPage == "$TTM_MCM_PageExplore")
        string subPage = TTM_MCM_State.GetCurrentPage()
        if(subPage == "Npc")
            TTM_MCM_NpcPage.OnOptionSliderAccept(self, option, value)
        endif
    elseif(currentPage == "$TTM_MCM_PageSettings")
        TTM_MCM_SettingsPage.OnOptionSliderAccept(self, option, value)
    endif
endEvent

event OnConfigClose()
    TTM_MCM_State.Clean()
endEvent

Function Navigate(string page)
    TTM_MCM_State.SetCurrentPage(page)
    ForcePageReset()
EndFunction
