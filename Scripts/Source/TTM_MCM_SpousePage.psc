scriptname TTM_MCM_SpousePage

import TTM_JCDomain

Function RenderPage(TTM_MCM mcm) global
    mcm.SetCursorFillMode(mcm.TOP_TO_BOTTOM)
    RenderLeftColumn(mcm)
    mcm.SetCursorPosition(1)
    RenderRightColumn(mcm)
EndFunction

Function RenderLeftColumn(TTM_MCM mcm) global
    Actor spouse = TTM_MCM_State.GetSelectedSpouse()
    string spouseName = TTM_Utils.GetActorName(spouse)
    string skillType = TTM_Utils.GetSpouseSkillType(spouse)
    string socialClass = TTM_Utils.GetSpouseSocialClass(spouse)
    string rank = TTM_ServiceNpcs.GetSpouseRank(spouse)
    string count = TTM_ServiceNpcs.GetSpousesCount()
    string rankText = "other"

    if(rank == 0)
        rankText = "1st spouse"
    elseif(rank == 1)
        rankText = "2nd spouse"
    elseif(rank ==2)
        rankText = "3nd spouse"
    endif

    if(count == 1)
        rankText = "The only one!"
    endif
    
    mcm.oid_ReturnToExplore = mcm.AddTextOption("", "Return to explore")
    mcm.AddHeaderOption(TTM_Utils.GetActorName(spouse) + "'s data")
    mcm.oid_SpousePageRank = mcm.AddTextOption("Rank: ", rankText)
    mcm.oid_SpousePageSocialClass = mcm.AddTextOption("Social class: ", socialClass)
    mcm.oid_SpousePageSkillType = mcm.AddTextOption("Skilled as: ", skillType)    
EndFunction

Function RenderRightColumn(TTM_MCM mcm) global
    Actor spouse = TTM_MCM_State.GetSelectedSpouse()
    mcm.AddHeaderOption(TTM_Utils.GetActorName(spouse) + "'s original house/s")
    int jSpouseCells = TTM_ServiceNpcs.GetTrackedNpcCells(spouse)
    int i = 0

    while(i < JArray_count(jSpouseCells))
        Cell home = JArray_getForm(jSpouseCells, i) as Cell
        mcm.AddTextOption(home.GetName(), "")

        i += 1
    endwhile

    mcm.AddHeaderOption(TTM_Utils.GetActorName(spouse) + "'s owns furniture")
    int jSpouseObjects = TTM_ServiceNpcs.GetTrackedNpcObjects(spouse)
    i = 0

    while(i < JArray_count(jSpouseObjects))
        ObjectReference obj = JArray_getForm(jSpouseObjects, i) as ObjectReference
        ; Faction objOwnerF = TTM_ServiceSpouseAssets.GetMaritalAssetsObjectFaction(obj)
        ; ActorBase objOwnerAB = TTM_ServiceSpouseAssets.GetMaritalAssetsObjectActor(obj)
        string ownerTxt = ""
        ; if(objOwnerAB)
        ;     ownerTxt = "(personal)"
        ; elseif(objOwnerF)
        ;     ownerTxt = "(faction)"
        ; endif
        mcm.AddTextOption(obj.GetBaseObject().GetName() + ownerTxt, "")

        i += 1
    endwhile

EndFunction

Function RenderLoverData(TTM_MCM mcm, Actor npc, Actor lover) global
    
EndFunction

Function OnOptionSelect(TTM_MCM mcm, int option) global
    if(option == mcm.oid_ReturnToExplore)
        mcm.Navigate("Explore")
    else
    endif
EndFunction

; Highlight
Function OnOptionHighlight(TTM_MCM mcm, int option) global
    Actor spouse = TTM_MCM_State.GetSelectedSpouse()
    string rank = TTM_ServiceNpcs.GetSpouseRank(spouse)
    string count = TTM_ServiceNpcs.GetSpousesCount()
    string rankText = "This spouse is one of many, they will give only 10% of their permanent bonus."

    if(rank == 0)
        rankText = "This spouse is your 1st, they will give 100% of their permanent bonus."
    elseif(rank == 1)
        rankText = "This spouse is your 2nd, they will give 50% of their permanent bonus."
    elseif(rank == 2)
        rankText = "This spouse is your 3rd, they will give 25% of their permanent bonus."
    endif

    if(count == 1)
        rankText = "Well you have only one spouse. It means they will give you 200% of their permanent bonus."
    endif
    
    if(option == mcm.oid_SpousePageSkillType)
        mcm.SetInfoText("Follower bonus - " + TTM_ServiceBuff.GetSpouseFollowerBuffs(spouse)) 
    elseif(option == mcm.oid_SpousePageSocialClass)
        mcm.SetInfoText("Permanent bonus - " + TTM_ServiceBuff.GetSpousePermanentBuffs(spouse))
    elseif(option == mcm.oid_SpousePageRank)
        mcm.SetInfoText(rankText)
    endif
EndFunction
    
; Default
Function OnOptionDefault(TTM_MCM mcm, int option) global
    if(option == mcm.oid_SearchSpouse)
    endif
EndFunction

Function OnOptionInputOpen(TTM_MCM mcm, int option) global
	; if (option == inputOID_I)
	; 	; Fill input box with current value
	; 	SetInputDialogStartText(myName)
	; endIf
EndFunction

Function OnOptionInputAccept(TTM_MCM mcm, int option, string value) global
	
EndFunction

