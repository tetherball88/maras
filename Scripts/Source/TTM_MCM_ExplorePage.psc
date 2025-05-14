scriptname TTM_MCM_ExplorePage

import TTM_JCDomain

Function RenderPage(TTM_MCM mcm) global
    mcm.SetCursorFillMode(mcm.TOP_TO_BOTTOM)
    RenderLeftColumn(mcm)
    mcm.SetCursorPosition(1)
    RenderRightColumn(mcm)
EndFunction

Function RenderLeftColumn(TTM_MCM mcm) global
    mcm.AddHeaderOption("Explore")
    mcm.oid_SearchSpouse = mcm.AddInputOption("Search Spouse", "")
    RenderSpousesList(mcm)
EndFunction

Function RenderRightColumn(TTM_MCM mcm) global
    
EndFunction

Function RenderSpousesList(TTM_MCM mcm) global
    mcm.AddHeaderOption("Spouses: ")
    int JSpouses = TTM_ServiceNpcs.GetSpouses()
    string searchValue = TTM_MCM_State.GetSearchValueSpouse()

    int count = JArray_count(jSpouses)
    int i = 0
    
    while(i < count)
        Actor spouse = JArray_getForm(jSpouses, i) as Actor
        string spouseName = TTM_Utils.GetActorName(spouse)
        bool shouldAdd = false
        if(searchValue != "") 
            shouldAdd = StringUtil.Find(spouseName, searchValue) != -1
        else
            shouldAdd = true
        endif
        
        if(shouldAdd)
            TTM_MCM_State.AddSpouseOption(mcm.AddTextOption(spouseName, ""), spouse)
        endif

        spouse = JFormMap_nextKey(JSpouses, previousKey=spouse, endKey=none) as Actor

        i += 1
    endwhile
EndFunction

Function OnOptionSelect(TTM_MCM mcm, int option) global
    Actor spouse = TTM_MCM_State.GetSpouseOption(option)
    if(spouse != none)
        TTM_MCM_State.SetSelectedSpouse(spouse)
        mcm.Navigate("spouse")
    endif
EndFunction

; Highlight
Function OnOptionHighlight(TTM_MCM mcm, int option) global
    if(option == mcm.oid_SearchSpouse)
        mcm.SetInfoText("Search characters by name(can be partial)")
    else
        Actor spouse = TTM_MCM_State.GetSpouseOption(option)
        if(spouse != none)
            mcm.SetInfoText("View " + TTM_Utils.GetActorName(spouse) + "'s data")
        endif
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