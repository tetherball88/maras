scriptname TTM_MCM_ExplorePage

import TTM_JCDomain

Function RenderPage(TTM_MCM mcm) global
    mcm.SetCursorFillMode(mcm.TOP_TO_BOTTOM)
    RenderLeftColumn(mcm)
    mcm.SetCursorPosition(1)
    RenderRightColumn(mcm)
EndFunction

bool Function GetSearchAll() global
    int searchAll = TTM_MCM_State._GetMcmInt("searchAll", -1)
    ; if it isn't set at all make default value enabled
    if(searchAll == -1)
        searchAll = 1
    endif

    return searchAll == 1
EndFunction

string[] Function GetAliveDeceasedOptions() global
    string[] options = new string[3]
    options[0] = "all"
    options[1] = "alive"
    options[2] = "deceased"
    return options
EndFunction

Function RenderLeftColumn(TTM_MCM mcm) global
    mcm.AddHeaderOption("Explore")
    mcm.oid_SearchSpouse = mcm.AddInputOption("Search Spouse", "")
    RenderSpousesList(mcm)
EndFunction

Function RenderRightColumn(TTM_MCM mcm) global
    mcm.AddHeaderOption("Filter by type:")
    bool searchAll = GetSearchAll()
    int disableOtherFilters = 0
    if(searchAll)
        disableOtherFilters = 1
    endif
    mcm.oid_SearchFilterAll = mcm.AddToggleOption("Show all", searchAll)
    mcm.oid_SearchFilterCandidate = mcm.AddToggleOption("Show candidates", TTM_MCM_State._GetMcmBool("searchCandidates"), disableOtherFilters)
    mcm.oid_SearchFilterFiance = mcm.AddToggleOption("Show fiances", TTM_MCM_State._GetMcmBool("searchFiances"), disableOtherFilters)
    mcm.oid_SearchFilterSpouse = mcm.AddToggleOption("Show spouses", TTM_MCM_State._GetMcmBool("searchSpouses"), disableOtherFilters)
    mcm.oid_SearchFilterJilted = mcm.AddToggleOption("Show jilted", TTM_MCM_State._GetMcmBool("searchJilted"), disableOtherFilters)
    mcm.oid_SearchFilterDivorced = mcm.AddToggleOption("Show divorced", TTM_MCM_State._GetMcmBool("searchDivorced"), disableOtherFilters)
    mcm.AddEmptyOption()
    string[] options = GetAliveDeceasedOptions()
    mcm.oid_SearchFilterDeceased = mcm.AddMenuOption("Show alive/deceased", options[TTM_MCM_State._GetMcmInt("searchDeceased")])
EndFunction

Function RenderSpousesList(TTM_MCM mcm) global
    mcm.AddHeaderOption("Spouses: ")
    string searchValue = TTM_MCM_State.GetSearchValueSpouse()
    bool searchAll = GetSearchAll()
    bool searchCandidates = TTM_MCM_State._GetMcmBool("searchCandidates")
    bool searchFiances = TTM_MCM_State._GetMcmBool("searchFiances")
    bool searchSpouses = TTM_MCM_State._GetMcmBool("searchSpouses")
    bool searchJilted = TTM_MCM_State._GetMcmBool("searchJilted")
    bool searchDivorced = TTM_MCM_State._GetMcmBool("searchDivorced")
    int searchDeceased = TTM_MCM_State._GetMcmInt("searchDeceased")

    Actor npc = TTM_ServiceNpcs.NextTrackedNpcs()

    while(npc)
        bool skipNpc = false
        bool isDeceased = TTM_ServiceNpcs.IsDeceased(npc)
        if((searchDeceased == 1 && isDeceased) || (searchDeceased == 2 && !isDeceased))
            skipNpc = true
        else
            if(!searchAll)
                string status = TTM_Utils.GetRelationshipStatus(npc)
                if(status == "candidate")
                    skipNpc = !searchCandidates
                elseif(status == "engaged")
                    skipNpc = !searchFiances
                elseif(status == "married")
                    skipNpc = !searchSpouses
                elseif(status == "jilted")
                    skipNpc = !searchJilted
                elseif(status == "divorced")
                    skipNpc = !searchDivorced
                endif
            endif
        endif
        if(!skipNpc)
            string spouseName = TTM_Utils.GetActorName(npc)
            bool shouldAdd = false
            if(searchValue != "")
                shouldAdd = StringUtil.Find(spouseName, searchValue) != -1
            else
                shouldAdd = true
            endif

            if(shouldAdd)
                TTM_MCM_State.AddSpouseOption(mcm.AddTextOption(spouseName, ""), npc)
            endif
        endif
        npc = TTM_ServiceNpcs.NextTrackedNpcs(npc)
    endwhile
EndFunction

Function OnOptionSelect(TTM_MCM mcm, int option) global
    Actor spouse = TTM_MCM_State.GetSpouseOption(option)
    if(spouse != none)
        TTM_MCM_State.SetSelectedSpouse(spouse)
        mcm.Navigate("spouse")
    elseif(option == mcm.oid_SearchFilterAll)
        TTM_MCM_State._SetMcmBool("searchAll", !GetSearchAll())
        mcm.ForcePageReset()
    elseif(option == mcm.oid_SearchFilterCandidate)
        TTM_MCM_State._SetMcmBool("searchCandidates", !TTM_MCM_State._GetMcmBool("searchCandidates"))
        mcm.ForcePageReset()
    elseif(option == mcm.oid_SearchFilterFiance)
        TTM_MCM_State._SetMcmBool("searchFiances", !TTM_MCM_State._GetMcmBool("searchFiances"))
        mcm.ForcePageReset()
    elseif(option == mcm.oid_SearchFilterSpouse)
        TTM_MCM_State._SetMcmBool("searchSpouses", !TTM_MCM_State._GetMcmBool("searchSpouses"))
        mcm.ForcePageReset()
    elseif(option == mcm.oid_SearchFilterJilted)
        TTM_MCM_State._SetMcmBool("searchJilted", !TTM_MCM_State._GetMcmBool("searchJilted"))
        mcm.ForcePageReset()
    elseif(option == mcm.oid_SearchFilterDivorced)
        TTM_MCM_State._SetMcmBool("searchDivorced", !TTM_MCM_State._GetMcmBool("searchDivorced"))
        mcm.ForcePageReset()
    elseif(option == mcm.oid_SearchFilterDeceased)
        TTM_MCM_State._SetMcmBool("searchDeceased", !TTM_MCM_State._GetMcmBool("searchDeceased"))
        mcm.ForcePageReset()
    endif
EndFunction

; Highlight
Function OnOptionHighlight(TTM_MCM mcm, int option) global
    Actor spouse = TTM_MCM_State.GetSpouseOption(option)
    if(option == mcm.oid_SearchSpouse)
        mcm.SetInfoText("Search characters by name(can be partial)")
    elseif(spouse != none)
        mcm.SetInfoText("View " + TTM_Utils.GetActorName(spouse) + "'s data")
    elseif(option == mcm.oid_SearchFilterAll)
    elseif(option == mcm.oid_SearchFilterCandidate)
    elseif(option == mcm.oid_SearchFilterFiance)
    elseif(option == mcm.oid_SearchFilterSpouse)
    elseif(option == mcm.oid_SearchFilterJilted)
    elseif(option == mcm.oid_SearchFilterDivorced)
    elseif(option == mcm.oid_SearchFilterDeceased)
    endif

EndFunction

    ; Default
Function OnOptionDefault(TTM_MCM mcm, int option) global

EndFunction

Function OnOptionInputOpen(TTM_MCM mcm, int option) global

EndFunction

Function OnOptionInputAccept(TTM_MCM mcm, int option, string value) global
    if (mcm.oid_SearchSpouse == option)
        mcm.SetInputOptionValue(mcm.oid_SearchSpouse, mcm.SearchValueSpouse)
        TTM_MCM_State.SetSearchValueSpouse(value)
        mcm.ForcePageReset()
    endIf

EndFunction

Function OnOptionMenuOpen(TTM_MCM mcm, int option) global
    string[] options
    int start
    int default
    if(option == mcm.oid_SearchFilterDeceased)
        options = GetAliveDeceasedOptions()
        start = TTM_MCM_State._GetMcmInt("searchDeceased")
        default = 0
    endif
    mcm.SetMenuDialogOptions(options)
    mcm.SetMenuDialogStartIndex(start)
    mcm.SetMenuDialogDefaultIndex(default)
EndFunction

Function OnOptionMenuAccept(TTM_MCM mcm, int option, int index) global
    string opt
    if(option == mcm.oid_SearchFilterDeceased)
        string[] options = GetAliveDeceasedOptions()
        opt = options[index]
        TTM_MCM_State._SetMcmInt("searchDeceased", index)
        mcm.ForcePageReset()
    endif

    mcm.SetMenuOptionValue(option, opt)
EndFunction
