scriptname TTM_MCM_SpousePage

import TTM_JCDomain

Function RenderPage(TTM_MCM mcm) global
    CleanMcmOids(mcm)
    mcm.SetCursorFillMode(mcm.TOP_TO_BOTTOM)
    RenderLeftColumn(mcm)
    mcm.SetCursorPosition(1)
    RenderRightColumn(mcm)
EndFunction

Function CleanMcmOids(TTM_MCM mcm) global
    mcm.oid_ReturnToExplore = -1
    mcm.oid_SpousePageRank = -1
    mcm.oid_CandidateChance = -1
    mcm.oid_SpousePageSocialClass = -1
    mcm.oid_SpousePageSkillType = -1
    mcm.oid_SpousePagePlayerHome = -1
    mcm.oid_SpouseShareTheirHome = -1
EndFunction

Function RenderLeftColumn(TTM_MCM mcm) global
    Actor spouse = TTM_MCM_State.GetSelectedSpouse()
    string spouseName = TTM_Utils.GetActorName(spouse)
    string skillType = TTM_Utils.GetSpouseSkillType(spouse)
    string socialClass = TTM_Utils.GetSpouseSocialClass(spouse)
    string count = TTM_ServiceNpcs.GetSpousesCount()
    string status = TTM_Utils.GetRelationshipStatus(spouse)
    bool isDeceased = TTM_ServiceNpcs.IsDeceased(spouse)
    string deceased = ""

    if(isDeceased)
        deceased = "(deceased)"
    endif

    bool isCandidate = status == "candidate"
    bool isFiance = status == "engaged"
    bool isSpouse = status == "married"
    bool isJilted = status == "jilted"
    bool isDivorced = status == "divorced"

    mcm.oid_ReturnToExplore = mcm.AddTextOption("", "Return to explore")
    mcm.AddHeaderOption(TTM_Utils.GetActorName(spouse) + "'s data")
    mcm.AddTextOption("Their status is ", status+deceased)
    if(isSpouse && !isDeceased)
        if(count == 1)
            mcm.oid_SpousePageRank = mcm.AddTextOption("Hierarchy Rank: ", "The only one!")
        else
            int rank = TTM_ServiceNpcs.GetSpouseRank(spouse)
            string rankText = "4th+ spouse"
            if(rank == 0)
                rankText = "1st spouse"
            elseif(rank == 1)
                rankText = "2nd spouse"
            elseif(rank == 2)
                rankText = "3rd spouse"
            endif
            mcm.oid_SpousePageRank = mcm.AddMenuOption("Hierarchy Rank: ", rankText)
            TTM_Debug.trace("SpousePage: RenderLeftColumn: rank: " + rank + "; rankText: " + rankText)
        endif
    endif

    if(!isFiance && !isSpouse)
        float chance = TTM_MarriageDifficulty.calcMarriageSuccessChance(spouse)
        mcm.oid_CandidateChance = mcm.AddTextOption("Your chances to get engaged: ", 100 * chance)
    endif

    mcm.oid_SpousePageSocialClass = mcm.AddMenuOption("Social class: ", socialClass)
    mcm.oid_SpousePageSkillType = mcm.AddMenuOption("Skilled as: ", skillType)

    if(isSpouse && !isDeceased)
        Location playerHouse = TTM_ServiceNpcs.GetTrackedNpcHome(spouse)
        string playerHouseName = playerHouse.GetName()
        if(playerHouse == none)
            playerHouseName = "unset"
        endif
        mcm.oid_SpousePagePlayerHome = mcm.AddMenuOption("Assigned player's home: ", playerHouseName)
    endif

    if(isSpouse)
        bool spouseShareHome = spouse.IsInFaction(TTM_JData.GetSpouseSharedHouseFaction())
        if(spouse.IsInFaction(TTM_JData.GetSpouseNoInitialHouseFaction()))
            mcm.oid_SpouseShareTheirHome = mcm.AddTextOption(TTM_Utils.GetActorName(spouse) + " doesn't have any place they can call home.", "")
        else
            if(TTM_JData.GetMarasCheckSpouseHomeQuest().IsRunning() && !spouseShareHome)
                mcm.AddTextOption("You can't start share home at this moment because of another share home quest running.", "")
            else
                if(!isDeceased)
                    mcm.oid_SpouseShareTheirHome = mcm.AddToggleOption("Share their home with player: ", spouseShareHome)
                elseif(TTM_ServiceNpcs.GetKilledByPlayer(spouse))
                    mcm.oid_SpouseShareTheirHome = mcm.AddTextOption(TTM_Utils.GetActorName(spouse) + " was killed by you. You can't use thier property", "")
                else
                    mcm.oid_SpouseShareTheirHome = mcm.AddToggleOption("Inhereted home from deceased spouse", spouseShareHome)
                endif
            endif
        endif
    endif
EndFunction

int Function ShareHomeIsAvailable(Actor spouse) global
    if(TTM_JData.GetMarasCheckSpouseHomeQuest().IsRunning() || spouse.IsInFaction(TTM_JData.GetSpouseNoInitialHouseFaction()))
        return 1
    endif
    return 0
EndFunction

Function RenderRightColumn(TTM_MCM mcm) global
    Actor spouse = TTM_MCM_State.GetSelectedSpouse()
    mcm.AddHeaderOption(TTM_Utils.GetActorName(spouse) + "'s original house they share with you.")
    int jSpouseCells = TTM_ServiceSpouseAssets.GetSpouseArray(spouse, "cells")
    int i = 0

    if(JArray_count(jSpouseCells))
        mcm.AddTextOption(TTM_Utils.GetActorName(spouse) + " doesn't share any property with you.", "")
    endif

    while(i < JArray_count(jSpouseCells))
        Cell home = JArray_getForm(jSpouseCells, i) as Cell
        mcm.AddTextOption(home.GetName(), "")

        i += 1
    endwhile

    mcm.AddHeaderOption(TTM_Utils.GetActorName(spouse) + "'s owns furniture and share it with you")
    int jSpouseObjects = TTM_ServiceSpouseAssets.GetSpouseArray(spouse, "objects")
    i = 0

    if(JArray_count(jSpouseObjects))
        mcm.AddTextOption(TTM_Utils.GetActorName(spouse) + " doesn't share any furniture with you.", "")
    endif

    while(i < JArray_count(jSpouseObjects))
        ObjectReference obj = JArray_getForm(jSpouseObjects, i) as ObjectReference
        mcm.AddTextOption(obj.GetBaseObject().GetName(), "")

        i += 1
    endwhile
EndFunction

Function OnOptionSelect(TTM_MCM mcm, int option) global
    Actor spouse = TTM_MCM_State.GetSelectedSpouse()

    if(option == mcm.oid_ReturnToExplore)
        mcm.Navigate("Explore")
    elseif(option == mcm.oid_SpouseShareTheirHome)
        if(ShareHomeIsAvailable(spouse) == 1)
            return
        endif
        if(spouse.IsInFaction(TTM_JData.GetSpouseSharedHouseFaction()))
            TTM_ServiceSpouseAssets.StopShareHomeWithPlayer(spouse)
        else
            TTM_ServiceSpouseAssets.StartShareHomeWithPlayer(spouse)
        endif
    endif
EndFunction

; Highlight
Function OnOptionHighlight(TTM_MCM mcm, int option) global
    Actor spouse = TTM_MCM_State.GetSelectedSpouse()

    if(option == mcm.oid_SpousePageSkillType)
        mcm.SetInfoText("Follower bonus - " + TTM_ServiceBuff.GetSpouseFollowerBuffs(spouse))
    elseif(option == mcm.oid_SpousePageSocialClass)
        mcm.SetInfoText("Permanent bonus - " + TTM_ServiceBuff.GetSpousePermanentBuffs(spouse))
    elseif(option == mcm.oid_SpousePageRank)
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
        mcm.SetInfoText(rankText)
    elseif(option == mcm.oid_CandidateChance)
        float chance = TTM_MarriageDifficulty.calcMarriageSuccessChance(spouse)
        string tooltip = TTM_Utils.GetActorName(spouse)
        if(chance >= 0.95)
            tooltip += " accepts as though it were fated—unquestioning and unwise"
        elseif(chance >= 0.8)
            tooltip += " is eager to accept"
        elseif(chance >= 0.6)
            tooltip += " would probably accept if approached kindly"
        elseif(chance >= 0.45)
            tooltip += " is on the fence and might be swayed with the right words"
        elseif(chance >= 0.25)
            tooltip += " is unlikely to accept; it would take exceptional persuasion"
        else
            tooltip += " will almost certainly refuse, though nothing is truly impossible"
        endif
        mcm.SetInfoText(tooltip)
    elseif(option == mcm.oid_SpousePagePlayerHome)
        mcm.SetInfoText(TTM_Utils.GetActorName(spouse) + " will spend time in assigned player's house.\nBe careful with characters who should be somewhere by quest.\n You always can unassign here or throug dialogue.")
    elseif(option == mcm.oid_SpouseShareTheirHome)
        string tooltip = "By enabling you will start quest to check " + TTM_Utils.GetActorName(spouse) + "'s own home, and you will get permanent access to it.\nBy disabling you will loose access to their home.\n Close menu to start quest."
        if(TTM_JData.GetMarasCheckSpouseHomeQuest().IsRunning())
            tooltip = "You are already trying to get access to spouse's home, check your journal and finish it before you can toggle this checkbox.\nClicking on this option won't do anything."
        elseif(spouse.IsInFaction(TTM_JData.GetSpouseNoInitialHouseFaction()))
            tooltip = TTM_Utils.GetActorName(spouse) + " doesn't have their own home.\nClicking on this option won't do anything."
        endif
        mcm.SetInfoText(tooltip)
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

Function OnOptionMenuOpen(TTM_MCM mcm, int option) global
    Actor spouse = TTM_MCM_State.GetSelectedSpouse()
    string[] options
    int start
    int default
    if(option == mcm.oid_SpousePageSocialClass)
        options = TTM_Utils.GetSpouseSocialTypeByIndexes()
        start = TTM_Utils.GetSpouseSocialIndexByType(TTM_Utils.GetSpouseSocialClass(spouse))
        default = TTM_Utils.GetSpouseSocialIndexByType(TTM_ServiceSpouseTypes.DetermineSocialClass(spouse))
    elseif(option == mcm.oid_SpousePageSkillType)
        options = TTM_Utils.GetTrackedNpcSkillTypeByIndexes()
        start = TTM_Utils.GetSpouseSkillIndexByType(TTM_Utils.GetSpouseSkillType(spouse))
        default = TTM_Utils.GetSpouseSkillIndexByType(TTM_ServiceSpouseTypes.DetermineSkillType(spouse))
    elseif(option == mcm.oid_SpousePagePlayerHome)
        options = PapyrusUtil.PushString(TTM_ServicePlayerHouse.GetPlayerHomesNames(), "unset")
        start = JArray_findForm(JFormMap_allKeys(TTM_ServicePlayerHouse.GetPlayerHouses()), TTM_ServiceNpcs.GetTrackedNpcHome(spouse))
        default = JFormMap_count(TTM_ServicePlayerHouse.GetPlayerHouses())
        if(start == -1)
            start = default
        endif
    elseif(option == mcm.oid_SpousePageRank)
        options = GetHierarchyOptions()
        int rank = TTM_ServiceNpcs.GetSpouseRank(spouse)
        if(rank != -1)
            start = rank
            default = rank
        endif
    endif
    mcm.SetMenuDialogOptions(options)
    mcm.SetMenuDialogStartIndex(start)
    mcm.SetMenuDialogDefaultIndex(default)
EndFunction

Function OnOptionMenuAccept(TTM_MCM mcm, int option, int index) global
    Actor spouse = TTM_MCM_State.GetSelectedSpouse()
    string opt
	if(option == mcm.oid_SpousePageSocialClass)
        string[] options = TTM_Utils.GetSpouseSocialTypeByIndexes()
        opt = options[index]
        TTM_ServiceSpouseTypes.SetSpouseSocialClass(spouse, opt)
        TTM_ServiceNpcs.SetTrackedNpcMcmTypeChanged(spouse)
    elseif(option == mcm.oid_SpousePageSkillType)
        string[] options = TTM_Utils.GetTrackedNpcSkillTypeByIndexes()
        opt = options[index]
        TTM_ServiceSpouseTypes.SetSpouseSkillType(spouse, opt)
        TTM_ServiceNpcs.SetTrackedNpcMcmTypeChanged(spouse)
    elseif(option == mcm.oid_SpousePagePlayerHome)
        string[] names = TTM_ServicePlayerHouse.GetPlayerHomesNames()
        names = PapyrusUtil.PushString(names, "unset")
        Form[] houses = TTM_ServicePlayerHouse.GetPlayerHomesForms()
        if(index >= houses.Length)
            TTM_ServicePlayerHouse.ReleaseSpouseFromPlayerHome(spouse)
        else
            TTM_ServicePlayerHouse.MoveSpouseToHouse(spouse, houses[index] as Location)
        endif
        opt = names[index]
    elseif(option == mcm.oid_SpousePageRank)
        int rank = TTM_ServiceNpcs.GetSpouseRank(spouse)
        if(rank == index)
            return
        endif
        opt = GetHierarchyOptions()[index]
        TTM_ServiceNpcs.ChangeSpouseRank(spouse, index)
    endif

    mcm.SetMenuOptionValue(option, opt)
EndFunction

; Default
Function OnOptionDefault(TTM_MCM mcm, int option) global
EndFunction

string[] Function GetHierarchyOptions() global
    string[] options = new string[3]

    options[0] = "1st spouse"
    options[1] = "2nd spouse"
    options[2] = "3rd spouse"

    return options
EndFunction
