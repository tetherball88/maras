scriptname TTM_MCM_ExplorePage

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

Function RenderLeftColumn(TTM_MCM mcm) global
    mcm.AddHeaderOption("$TTM_MCM_HeaderExplore")
    mcm.oid_SearchNpc = mcm.AddInputOption("$TTM_MCM_SearchNPC", "")
    RenderNpcsList(mcm)
EndFunction

Function RenderRightColumn(TTM_MCM mcm) global
    mcm.AddHeaderOption("$TTM_MCM_HeaderFilterByType")
    bool searchAll = GetSearchAll()
    int disableOtherFilters = 0
    if(searchAll)
        disableOtherFilters = 1
    endif
    mcm.oid_SearchFilterAll = mcm.AddToggleOption("$TTM_MCM_FilterShowAll", searchAll)
    mcm.oid_SearchFilterCandidate = mcm.AddToggleOption("$TTM_MCM_FilterShowCandidates", TTM_MCM_State._GetMcmBool("searchCandidates"), disableOtherFilters)
    mcm.oid_SearchFilterFiance = mcm.AddToggleOption("$TTM_MCM_FilterShowFiances", TTM_MCM_State._GetMcmBool("searchFiances"), disableOtherFilters)
    mcm.oid_SearchFilterSpouse = mcm.AddToggleOption("$TTM_MCM_FilterShowSpouses", TTM_MCM_State._GetMcmBool("searchSpouses"), disableOtherFilters)
    mcm.oid_SearchFilterJilted = mcm.AddToggleOption("$TTM_MCM_FilterShowJilted", TTM_MCM_State._GetMcmBool("searchJilted"), disableOtherFilters)
    mcm.oid_SearchFilterDivorced = mcm.AddToggleOption("$TTM_MCM_FilterShowDivorced", TTM_MCM_State._GetMcmBool("searchDivorced"), disableOtherFilters)
EndFunction

Function RenderNpcsList(TTM_MCM mcm) global
    mcm.AddHeaderOption("$TTM_MCM_HeaderNPCs")
    string searchValue = TTM_MCM_State.GetSearchValueNpc()
    bool searchAll = GetSearchAll()
    bool searchCandidates = TTM_MCM_State._GetMcmBool("searchCandidates")
    bool searchFiances = TTM_MCM_State._GetMcmBool("searchFiances")
    bool searchSpouses = TTM_MCM_State._GetMcmBool("searchSpouses")
    bool searchJilted = TTM_MCM_State._GetMcmBool("searchJilted")
    bool searchDivorced = TTM_MCM_State._GetMcmBool("searchDivorced")

    Actor[] npcs = MARAS.GetNPCsByStatus("all")
    int i = 0

    while(i < npcs.Length)
        Actor npc = npcs[i]
        bool skipNpc = false
        string status = MARAS.GetNpcStatusName(npc)
        if(!searchAll)
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
        if(!skipNpc)
            string npcName = TTM_Utils.GetActorName(npc)
            bool shouldAdd = false
            if(searchValue != "")
                shouldAdd = StringUtil.Find(npcName, searchValue) != -1
            else
                shouldAdd = true
            endif

            if(shouldAdd)
                TTM_MCM_State.AddNpcOption(mcm.AddTextOption(npcName, ""), npc)
            endif
        endif
        i += 1
    endwhile
EndFunction

Function OnOptionSelect(TTM_MCM mcm, int option) global
    Actor npc = TTM_MCM_State.GetNpcOption(option)
    if(npc != none)
        TTM_MCM_State.SetSelectedNpc(npc)
        mcm.Navigate("npc")
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
    endif
EndFunction

; Highlight
Function OnOptionHighlight(TTM_MCM mcm, int option) global
    Actor npc = TTM_MCM_State.GetNpcOption(option)
    if(option == mcm.oid_SearchNpc)
        mcm.SetInfoText("$TTM_MCM_TT_SearchNPC")
    elseif(npc != none)
        mcm.SetInfoText("$TTM_MCM_TT_ViewNpcData{" + TTM_Utils.GetActorName(npc) + "}")
    elseif(option == mcm.oid_SearchFilterAll)
    elseif(option == mcm.oid_SearchFilterCandidate)
    elseif(option == mcm.oid_SearchFilterFiance)
    elseif(option == mcm.oid_SearchFilterSpouse)
    elseif(option == mcm.oid_SearchFilterJilted)
    elseif(option == mcm.oid_SearchFilterDivorced)
    endif

EndFunction

    ; Default
Function OnOptionDefault(TTM_MCM mcm, int option) global

EndFunction

Function OnOptionInputOpen(TTM_MCM mcm, int option) global

EndFunction

Function OnOptionInputAccept(TTM_MCM mcm, int option, string value) global
    if (mcm.oid_SearchNpc == option)
        mcm.SetInputOptionValue(mcm.oid_SearchNpc, mcm.SearchValueNpc)
        TTM_MCM_State.SetSearchValueNpc(value)
        mcm.ForcePageReset()
    endIf

EndFunction

Function OnOptionMenuOpen(TTM_MCM mcm, int option) global
EndFunction

Function OnOptionMenuAccept(TTM_MCM mcm, int option, int index) global
EndFunction
