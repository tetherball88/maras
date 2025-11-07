scriptname TTM_MCM_NpcPage

Function RenderPage(TTM_MCM mcm) global
    TTM_JMethods.ExportStorage()
    CleanMcmOids(mcm)
    mcm.SetCursorFillMode(mcm.TOP_TO_BOTTOM)
    RenderLeftColumn(mcm)
    mcm.SetCursorPosition(1)
    RenderRightColumn(mcm)
EndFunction

Function CleanMcmOids(TTM_MCM mcm) global
    mcm.oid_ReturnToExplore = -1
    mcm.oid_NpcPageRank = -1
    mcm.oid_CandidateChance = -1
    mcm.oid_NpcPageAffection = -1
    mcm.oid_NpcPageSocialClass = -1
    mcm.oid_NpcPageSkillType = -1
    mcm.oid_NpcPageTemperament = -1
    mcm.oid_NpcPagePlayerHome = -1
    mcm.oid_NpcPageShareTheirHome = -1
EndFunction

Function RenderLeftColumn(TTM_MCM mcm) global
    Actor npc = TTM_MCM_State.GetSelectedNpc()
    string npcName = TTM_Utils.GetActorName(npc)
    string skillType = TTM_Utils.GetSpouseSkillType(npc)
    string socialClass = TTM_Utils.GetSpouseSocialClass(npc)
    string temperament = TTM_Utils.GetSpouseTemperament(npc)
    int count = MARAS.GetStatusCount("married")
    string status = TTM_Utils.GetRelationshipStatus(npc)
    bool isDeceased = MARAS.IsNPCStatus(npc, "deceased")
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
    mcm.AddHeaderOption(TTM_Utils.GetActorName(npc) + "'s data")
    mcm.AddTextOption("Their status is ", status+deceased)
    if(isSpouse && !isDeceased)
        if(count == 1)
            mcm.oid_NpcPageRank = mcm.AddTextOption("Hierarchy Rank: ", "The only one!")
        else
            int rank = TTM_ServiceHierarchy.GetSpouseRank(npc)
            string rankText = "4th+ spouse"
            if(rank == 0)
                rankText = "1st spouse"
            elseif(rank == 1)
                rankText = "2nd spouse"
            elseif(rank == 2)
                rankText = "3rd spouse"
            endif
            mcm.oid_NpcPageRank = mcm.AddMenuOption("Hierarchy Rank: ", rankText)
        endif
    endif

    if(!isFiance && !isSpouse)
        float chance = TTM_MarriageDifficulty.calcMarriageSuccessChance(npc)
        mcm.oid_CandidateChance = mcm.AddTextOption("Your chances to get engaged: ", 100 * chance)
    endif

    int affection = TTM_ServiceAffection.GetAffectionRank(npc)
    mcm.oid_NpcPageAffection = mcm.AddSliderOption("Affection: ", affection as float, "{0}%")

    mcm.oid_NpcPageSocialClass = mcm.AddMenuOption("Social class: ", socialClass)
    mcm.oid_NpcPageSkillType = mcm.AddMenuOption("Skilled as: ", skillType)
    mcm.oid_NpcPageTemperament = mcm.AddMenuOption("Temperament: ", temperament)

    if(isSpouse && !isDeceased)
        Location playerHouse = TTM_ServiceRelationships.GetTrackedNpcHome(npc)
        string playerHouseName = playerHouse.GetName()
        if(playerHouse == none)
            playerHouseName = "unset"
        endif
        mcm.oid_NpcPagePlayerHome = mcm.AddMenuOption("Assigned player's home: ", playerHouseName)
    endif

    if(isSpouse)
        bool spouseShareHome = npc.IsInFaction(TTM_JData.GetSpouseSharedHouseFaction())
        if(npc.IsInFaction(TTM_JData.GetSpouseNoInitialHouseFaction()))
            mcm.oid_NpcPageShareTheirHome = mcm.AddTextOption(TTM_Utils.GetActorName(npc) + " doesn't have any place they can call home.", "")
        else
            if(TTM_JData.GetMarasCheckSpouseHomeQuest().IsRunning() && !spouseShareHome)
                mcm.AddTextOption("You can't start share home at this moment because of another share home quest running.", "")
            else
                if(!isDeceased)
                    mcm.oid_NpcPageShareTheirHome = mcm.AddToggleOption("Share their home with player: ", spouseShareHome)
                elseif(TTM_ServiceRelationships.GetKilledByPlayer(npc))
                    mcm.oid_NpcPageShareTheirHome = mcm.AddTextOption(TTM_Utils.GetActorName(npc) + " was killed by you. You can't use thier property", "")
                else
                    mcm.oid_NpcPageShareTheirHome = mcm.AddToggleOption("Inhereted home from deceased spouse", spouseShareHome)
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
    Actor npc = TTM_MCM_State.GetSelectedNpc()
    mcm.AddHeaderOption(TTM_Utils.GetActorName(npc) + "'s original house they share with you.")
    Form[] cells = TTM_ServiceSpouseAssets.GetSpouseCells(npc)
    int i = 0

    if(cells.Length == 0)
        mcm.AddTextOption(TTM_Utils.GetActorName(npc) + " doesn't share any property with you.", "")
    endif

    while(i < cells.Length)
        Cell home = cells[i] as Cell
        mcm.AddTextOption(home.GetName(), "")

        i += 1
    endwhile

    mcm.AddHeaderOption(TTM_Utils.GetActorName(npc) + "'s owns furniture and share it with you")
    Form[] objects = TTM_ServiceSpouseAssets.GetSpouseObjs(npc)
    i = 0

    if(objects.Length == 0)
        mcm.AddTextOption(TTM_Utils.GetActorName(npc) + " doesn't share any furniture with you.", "")
    endif

    while(i < objects.Length)
        ObjectReference obj = objects[i] as ObjectReference
        mcm.AddTextOption(obj.GetBaseObject().GetName(), "")

        i += 1
    endwhile
EndFunction

Function OnOptionSelect(TTM_MCM mcm, int option) global
    Actor npc = TTM_MCM_State.GetSelectedNpc()

    if(option == mcm.oid_ReturnToExplore)
        mcm.Navigate("Explore")
    elseif(option == mcm.oid_NpcPageShareTheirHome)
        if(ShareHomeIsAvailable(npc) == 1)
            return
        endif
        if(npc.IsInFaction(TTM_JData.GetSpouseSharedHouseFaction()))
            TTM_ServiceSpouseAssets.StopShareHomeWithPlayer(npc)
        else
            TTM_ServiceSpouseAssets.StartShareHomeWithPlayer(npc)
        endif
    endif
EndFunction

; Highlight
Function OnOptionHighlight(TTM_MCM mcm, int option) global
    Actor npc = TTM_MCM_State.GetSelectedNpc()

    if(option == mcm.oid_NpcPageSkillType)
        mcm.SetInfoText("Follower bonus - " + TTM_ServiceBuff.GetSpouseFollowerBuffs(npc))
    elseif(option == mcm.oid_NpcPageSocialClass)
        mcm.SetInfoText("Permanent bonus - " + TTM_ServiceBuff.GetSpousePermanentBuffs(npc))
    elseif(option == mcm.oid_NpcPageTemperament)
        mcm.SetInfoText("Temperament shapes affection gains, jealousy, and how spouses react to you.")
    elseif(option == mcm.oid_NpcPageRank)
        int rank = TTM_ServiceHierarchy.GetSpouseRank(npc)
        int count = MARAS.GetStatusCount("married")
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
        float chance = TTM_MarriageDifficulty.calcMarriageSuccessChance(npc)
        string tooltip = TTM_Utils.GetActorName(npc)
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
    elseif(option == mcm.oid_NpcPageAffection)
        mcm.SetInfoText("Adjust their affection directly. Higher scores unlock happier relationship events; lower scores risk estrangement.")
    elseif(option == mcm.oid_NpcPagePlayerHome)
        mcm.SetInfoText(TTM_Utils.GetActorName(npc) + " will spend time in assigned player's house.\nBe careful with characters who should be somewhere by quest.\n You always can unassign here or throug dialogue.")
    elseif(option == mcm.oid_NpcPageShareTheirHome)
        string tooltip = "By enabling you will start quest to check " + TTM_Utils.GetActorName(npc) + "'s own home, and you will get permanent access to it.\nBy disabling you will loose access to their home.\n Close menu to start quest."
        if(TTM_JData.GetMarasCheckSpouseHomeQuest().IsRunning())
            tooltip = "You are already trying to get access to spouse's home, check your journal and finish it before you can toggle this checkbox.\nClicking on this option won't do anything."
        elseif(npc.IsInFaction(TTM_JData.GetSpouseNoInitialHouseFaction()))
            tooltip = TTM_Utils.GetActorName(npc) + " doesn't have their own home.\nClicking on this option won't do anything."
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
    Actor spouse = TTM_MCM_State.GetSelectedNpc()
    string[] options
    int start
    int default
    if(option == mcm.oid_NpcPageSocialClass)
        options = TTM_Utils.GetSpouseSocialTypeByIndexes()
        start = TTM_Utils.GetSpouseSocialIndexByType(TTM_Utils.GetSpouseSocialClass(spouse))
        default = TTM_Utils.GetSpouseSocialIndexByType(TTM_ServiceSpouseTypes.DetermineSocialClass(spouse))
    elseif(option == mcm.oid_NpcPageSkillType)
        options = TTM_Utils.GetSpouseSkillTypeByIndexes()
        start = TTM_Utils.GetSpouseSkillIndexByType(TTM_Utils.GetSpouseSkillType(spouse))
        default = TTM_Utils.GetSpouseSkillIndexByType(TTM_ServiceSpouseTypes.DetermineSkillType(spouse))
    elseif(option == mcm.oid_NpcPageTemperament)
        options = TTM_Utils.GetSpouseTemperamentByIndexes()
        start = TTM_Utils.GetSpouseTemperamentIndex(spouse)
        default = TTM_Utils.GetSpouseTemperamentIndexByType(TTM_ServiceSpouseTypes.DetermineTemperament(spouse))
    elseif(option == mcm.oid_NpcPagePlayerHome)
        options = PapyrusUtil.PushString(TTM_ServicePlayerHouse.GetPlayerHomesNames(), "unset")
        start = TTM_ServicePlayerHouse.FindPlayerHouseIndex(TTM_ServiceRelationships.GetTrackedNpcHome(spouse))
        default = TTM_ServicePlayerHouse.GetPlayerHouses().Length  ; unset
        if(start == -1)
            start = default
        endif
    elseif(option == mcm.oid_NpcPageRank)
        options = GetHierarchyOptions()
        int rank = TTM_ServiceHierarchy.GetSpouseRank(spouse)
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
    Actor spouse = TTM_MCM_State.GetSelectedNpc()
    string opt
	if(option == mcm.oid_NpcPageSocialClass)
        string[] options = TTM_Utils.GetSpouseSocialTypeByIndexes()
        opt = options[index]
        TTM_ServiceSpouseTypes.SetSpouseSocialClass(spouse, opt)
        TTM_ServiceRelationships.SetTrackedNpcMcmTypeChanged(spouse)
    elseif(option == mcm.oid_NpcPageSkillType)
        string[] options = TTM_Utils.GetSpouseSkillTypeByIndexes()
        opt = options[index]
        TTM_ServiceSpouseTypes.SetSpouseSkillType(spouse, opt)
        TTM_ServiceRelationships.SetTrackedNpcMcmTypeChanged(spouse)
    elseif(option == mcm.oid_NpcPageTemperament)
        string[] options = TTM_Utils.GetSpouseTemperamentByIndexes()
        opt = options[index]
        TTM_ServiceSpouseTypes.SetSpouseTemperament(spouse, opt)
        TTM_ServiceRelationships.SetTrackedNpcMcmTypeChanged(spouse)
    elseif(option == mcm.oid_NpcPagePlayerHome)
        string[] names = TTM_ServicePlayerHouse.GetPlayerHomesNames()
        names = PapyrusUtil.PushString(names, "unset")
        Form[] houses = TTM_ServicePlayerHouse.GetPlayerHouses()
        if(index >= houses.Length)
            TTM_ServicePlayerHouse.ReleaseSpouseFromPlayerHome(spouse)
        else
            TTM_ServicePlayerHouse.MoveSpouseToHouse(spouse, houses[index] as Location)
        endif
        opt = names[index]
    elseif(option == mcm.oid_NpcPageRank)
        int rank = TTM_ServiceHierarchy.GetSpouseRank(spouse)
        if(rank == index)
            return
        endif
        opt = GetHierarchyOptions()[index]
        TTM_ServiceHierarchy.ChangeSpouseRank(spouse, index)
    endif

    mcm.SetMenuOptionValue(option, opt)
EndFunction

Function OnOptionSliderOpen(TTM_MCM mcm, int option) global
    if(option == mcm.oid_NpcPageAffection)
        Actor spouse = TTM_MCM_State.GetSelectedNpc()
        int affection = TTM_ServiceAffection.GetAffectionRank(spouse)
        float affectionValue = affection as float
        mcm.SetSliderDialogStartValue(affectionValue)
        mcm.SetSliderDialogDefaultValue(affectionValue)
        mcm.SetSliderDialogRange(0.0, 100.0)
        mcm.SetSliderDialogInterval(1.0)
    endif
EndFunction

Function OnOptionSliderAccept(TTM_MCM mcm, int option, float value) global
    if(option == mcm.oid_NpcPageAffection)
        Actor spouse = TTM_MCM_State.GetSelectedNpc()
        int previousAffection = TTM_ServiceAffection.GetAffectionRank(spouse)
        int newAffection = value as int
        if(newAffection < 0)
            newAffection = 0
        elseif(newAffection > 100)
            newAffection = 100
        endif

        if(newAffection == previousAffection)
            mcm.SetSliderOptionValue(option, newAffection as float, "{0}%")
            return
        endif

        TTM_ServiceAffection.SetAffectionRank(spouse, newAffection)
        TriggerAffectionThresholdEvents(spouse, previousAffection, newAffection)
        mcm.SetSliderOptionValue(option, newAffection as float, "{0}%")
    endif
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

Function TriggerAffectionThresholdEvents(Actor spouse, int previousAffection, int newAffection) global
    if(newAffection >= 75)
        if(previousAffection < 75)
            TTM_Utils.SendAffectionChangeThresholdEvent(spouse, "happy", true)
        endif
    elseif(newAffection >= 50)
        if(previousAffection < 50 || previousAffection >= 75)
            TTM_Utils.SendAffectionChangeThresholdEvent(spouse, "content", previousAffection < 50)
        endif
    elseif(newAffection >= 25)
        if(previousAffection < 25 || previousAffection >= 50)
            TTM_Utils.SendAffectionChangeThresholdEvent(spouse, "troubled", previousAffection < 25)
        endif
    else
        if(previousAffection >= 25)
            TTM_Utils.SendAffectionChangeThresholdEvent(spouse, "estranged", false)
        endif
    endif
EndFunction
