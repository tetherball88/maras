scriptname TTM_MCM_NpcPage

Function RenderPage(TTM_MCM mcm) global
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
    string skillType = MARAS.GetNpcCurrentTypeName(npc, "skillType")
    string socialClass = MARAS.GetNpcCurrentTypeName(npc, "socialClass")
    string temperament = MARAS.GetNpcCurrentTypeName(npc, "temperament")
    int count = MARAS.GetStatusCount("married")
    string status = MARAS.GetNpcStatusName(npc)
    bool isDeceased = MARAS.IsNPCStatus(npc, "deceased")
    string deceased = ""

    if(isDeceased)
        deceased = "(deceased)"
    endif

    bool isCandidate = MARAS.IsNPCStatus(npc, "candidate")
    bool isFiance = MARAS.IsNPCStatus(npc, "engaged")
    bool isSpouse = MARAS.IsNPCStatus(npc, "married")
    bool isJilted = MARAS.IsNPCStatus(npc, "jilted")
    bool isDivorced = MARAS.IsNPCStatus(npc, "divorced")

    mcm.oid_ReturnToExplore = mcm.AddTextOption("", "$TTM_MCM_ReturnToExplore")
    mcm.AddHeaderOption("$TTM_MCM_NpcDataHeader{" + TTM_Utils.GetActorName(npc) + "}")
    mcm.AddTextOption("$TTM_MCM_NpcStatus", status+deceased)
    if(isSpouse && !isDeceased)
        if(count == 1)
            mcm.oid_NpcPageRank = mcm.AddTextOption("$TTM_MCM_NpcRankHierarchy", "$TTM_MCM_NpcRankOnly")
        else
            int rank = MARAS.GetHierarchyRank(npc)
            string rankText = "$TTM_MCM_NpcRank4th"
            if(rank == 0)
                rankText = "$TTM_MCM_NpcRank1st"
            elseif(rank == 1)
                rankText = "$TTM_MCM_NpcRank2nd"
            elseif(rank == 2)
                rankText = "$TTM_MCM_NpcRank3rd"
            endif
            mcm.oid_NpcPageRank = mcm.AddMenuOption("$TTM_MCM_NpcRankHierarchy", rankText)
        endif
    endif

    if(!isFiance && !isSpouse)
        float chance = TTM_ServiceMarriageDifficulty.calcMarriageSuccessChance(npc)
        mcm.oid_CandidateChance = mcm.AddTextOption("$TTM_MCM_NpcChances", 100 * chance)
    endif

    int affection = MARAS.GetPermanentAffection(npc)
    mcm.oid_NpcPageAffection = mcm.AddSliderOption("$TTM_MCM_NpcAffection", affection as float, "{0}%")

    mcm.oid_NpcPageSocialClass = mcm.AddMenuOption("$TTM_MCM_NpcSocialClass", socialClass)
    mcm.oid_NpcPageSkillType = mcm.AddMenuOption("$TTM_MCM_NpcSkillType", skillType)
    mcm.oid_NpcPageTemperament = mcm.AddMenuOption("$TTM_MCM_NpcTemperament", temperament)

    if(isSpouse && !isDeceased)
        Location playerHouse = TTM_ServiceRelationships.GetTrackedNpcHome(npc)
        string playerHouseName = playerHouse.GetName()
        if(playerHouse == none)
            playerHouseName = "$TTM_MCM_NpcHomeUnset"
        endif
        mcm.oid_NpcPagePlayerHome = mcm.AddMenuOption("$TTM_MCM_NpcPlayerHome", playerHouseName)
    endif

    if(isSpouse)
        bool spouseShareHome = npc.IsInFaction(TTM_Data.GetSpouseSharedHouseFaction())
        if(npc.IsInFaction(TTM_Data.GetSpouseNoInitialHouseFaction()))
            mcm.oid_NpcPageShareTheirHome = mcm.AddTextOption("$TTM_MCM_NpcNoHome{" + TTM_Utils.GetActorName(npc) + "}", "")
        else
            if(TTM_Data.GetMarasCheckSpouseHomeQuest().IsRunning() && !spouseShareHome)
                mcm.AddTextOption("$TTM_MCM_NpcShareHomeRunning", "")
            else
                if(!isDeceased)
                    mcm.oid_NpcPageShareTheirHome = mcm.AddToggleOption("$TTM_MCM_NpcShareTheirHome", spouseShareHome)
                elseif(TTM_ServiceRelationships.GetKilledByPlayer(npc))
                    mcm.oid_NpcPageShareTheirHome = mcm.AddTextOption("$TTM_MCM_NpcKilledByPlayer{" + TTM_Utils.GetActorName(npc) + "}", "")
                else
                    mcm.oid_NpcPageShareTheirHome = mcm.AddToggleOption("$TTM_MCM_NpcInheritedHome", spouseShareHome)
                endif
            endif
        endif
    endif
EndFunction

int Function ShareHomeIsAvailable(Actor spouse) global
    if(TTM_Data.GetMarasCheckSpouseHomeQuest().IsRunning() || spouse.IsInFaction(TTM_Data.GetSpouseNoInitialHouseFaction()))
        return 1
    endif
    return 0
EndFunction

Function RenderRightColumn(TTM_MCM mcm) global
    Actor npc = TTM_MCM_State.GetSelectedNpc()
    if(!MARAS.IsNPCStatus(npc, "married"))
        mcm.AddTextOption("$TTM_MCM_NpcNotMarried{" + TTM_Utils.GetActorName(npc) + "}", "")
        return
    endif
    string npcName = TTM_Utils.GetActorName(npc)
    mcm.AddHeaderOption("$TTM_MCM_NpcPropertyHeader{" + npcName + "}")
    Cell npcOriginalHouse = MARAS.GetNpcOriginalHouse(npc)
    if(!npcOriginalHouse)
        mcm.AddTextOption("$TTM_MCM_NpcNoOwnHome{" + npcName + "}", "")
        return
    endif
    bool isShared = MARAS.IsHouseSharedWithPlayer(npcOriginalHouse)
    string sharedString
    if(isShared)
        sharedString = "$TTM_MCM_NpcHomeShared"
    else
        sharedString = "$TTM_MCM_NpcHomeNotShared"
    endif
    mcm.AddTextOption(sharedString + "{" + npcName + "}", "")

    mcm.AddHeaderOption("$TTM_MCM_NpcBedsHeader{" + TTM_Utils.GetActorName(npc) + "}")
    ObjectReference[] beds = MARAS.GetNpcBeds(npc)
    if(beds.Length == 0)
        mcm.AddTextOption("$TTM_MCM_NpcNoPersonalBed{" + TTM_Utils.GetActorName(npc) + "}", "")
        return
    endif

    int i = 0
    while(i < beds.Length)
        mcm.AddTextOption(beds[i].GetName() + sharedString, "")
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
        if(npc.IsInFaction(TTM_Data.GetSpouseSharedHouseFaction()))
            TTM_ServiceSpouseAssets.StopShareHouseWithPlayer(npc)
        else
            TTM_ServiceSpouseAssets.StartShareHomeWithPlayer(npc)
        endif
    endif
EndFunction

; Highlight
Function OnOptionHighlight(TTM_MCM mcm, int option) global
    Actor npc = TTM_MCM_State.GetSelectedNpc()

    if(option == mcm.oid_NpcPageSkillType)
        mcm.SetInfoText("$TTM_MCM_TT_NpcSkillType{" + TTM_ServiceBuff.GetSpouseFollowerBuffs(npc) + "}")
    elseif(option == mcm.oid_NpcPageSocialClass)
        mcm.SetInfoText("$TTM_MCM_TT_NpcSocialClass{" + TTM_ServiceBuff.GetSpousePermanentBuffs(npc) + "}")
    elseif(option == mcm.oid_NpcPageTemperament)
        mcm.SetInfoText("$TTM_MCM_TT_NpcTemperament")
    elseif(option == mcm.oid_NpcPageRank)
        int rank = MARAS.GetHierarchyRank(npc)
        int count = MARAS.GetStatusCount("married")
        string rankText = "$TTM_MCM_TT_NpcRank4th"

        if(rank == 0)
            rankText = "$TTM_MCM_TT_NpcRank1st"
        elseif(rank == 1)
            rankText = "$TTM_MCM_TT_NpcRank2nd"
        elseif(rank == 2)
            rankText = "$TTM_MCM_TT_NpcRank3rd"
        endif

        if(count == 1)
            rankText = "$TTM_MCM_TT_NpcRankOnly"
        endif
        mcm.SetInfoText(rankText)
    elseif(option == mcm.oid_CandidateChance)
        float chance = TTM_ServiceMarriageDifficulty.calcMarriageSuccessChance(npc)
        string npcName = TTM_Utils.GetActorName(npc)
        string tooltip
        if(chance >= 0.95)
            tooltip = "$TTM_MCM_TT_ChanceFated{" + npcName + "}"
        elseif(chance >= 0.8)
            tooltip = "$TTM_MCM_TT_ChanceEager{" + npcName + "}"
        elseif(chance >= 0.6)
            tooltip = "$TTM_MCM_TT_ChanceProbably{" + npcName + "}"
        elseif(chance >= 0.45)
            tooltip = "$TTM_MCM_TT_ChanceFence{" + npcName + "}"
        elseif(chance >= 0.25)
            tooltip = "$TTM_MCM_TT_ChanceUnlikely{" + npcName + "}"
        else
            tooltip = "$TTM_MCM_TT_ChanceRefuse{" + npcName + "}"
        endif
        mcm.SetInfoText(tooltip)
    elseif(option == mcm.oid_NpcPageAffection)
        mcm.SetInfoText("$TTM_MCM_TT_NpcAffection")
    elseif(option == mcm.oid_NpcPagePlayerHome)
        mcm.SetInfoText("$TTM_MCM_TT_NpcPlayerHome{" + TTM_Utils.GetActorName(npc) + "}")
    elseif(option == mcm.oid_NpcPageShareTheirHome)
        string tooltip = "$TTM_MCM_TT_NpcShareHome{" + TTM_Utils.GetActorName(npc) + "}"
        if(TTM_Data.GetMarasCheckSpouseHomeQuest().IsRunning())
            tooltip = "$TTM_MCM_TT_NpcShareHomeRunning"
        elseif(npc.IsInFaction(TTM_Data.GetSpouseNoInitialHouseFaction()))
            tooltip = "$TTM_MCM_TT_NpcShareHomeNoHouse{" + TTM_Utils.GetActorName(npc) + "}"
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
        options = MARAS.GetNpcTypes("socialClass")
        start = MARAS.GetNpcCurrentTypeEnum(spouse, "socialClass")
        default = MARAS.DetermineSocialClass(spouse)
    elseif(option == mcm.oid_NpcPageSkillType)
        options = MARAS.GetNpcTypes("skillType")
        start = MARAS.GetNpcCurrentTypeEnum(spouse, "skillType")
        default = MARAS.DetermineSkillType(spouse)
    elseif(option == mcm.oid_NpcPageTemperament)
        options = MARAS.GetNpcTypes("temperament")
        start = MARAS.GetNpcCurrentTypeEnum(spouse, "temperament")
        default = MARAS.DetermineTemperament(spouse)
    elseif(option == mcm.oid_NpcPagePlayerHome)
        StorageUtil.StringListCopy(none, "SpouseHomeChoiceCache", MARAS.GetAllPlayerHousesNames())
        StorageUtil.StringListAdd(none, "SpouseHomeChoiceCache", "unset")
        options = StorageUtil.StringListToArray(none, "SpouseHomeChoiceCache")
        start = StorageUtil.StringListFind(none, "SpouseHomeChoiceCache", MARAS.GetTenantHouse(spouse).GetName())
        default = MARAS.CountPlayerHouses()  ; unset
        StorageUtil.StringListClear(none, "SpouseHomeChoiceCache")
        if(start == -1)
            start = default
        endif
    elseif(option == mcm.oid_NpcPageRank)
        options = GetHierarchyOptions()
        int rank = MARAS.GetHierarchyRank(spouse)
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
        string[] options = MARAS.GetNpcTypes("socialClass")
        opt = options[index]
        MARAS.SetNpcCharacteristics(spouse, "socialClass", index)
    elseif(option == mcm.oid_NpcPageSkillType)
        string[] options = MARAS.GetNpcTypes("skillType")
        opt = options[index]
        MARAS.SetNpcCharacteristics(spouse, "skillType", index)
    elseif(option == mcm.oid_NpcPageTemperament)
        string[] options = MARAS.GetNpcTypes("temperament")
        opt = options[index]
        MARAS.SetNpcCharacteristics(spouse, "temperament", index)
    elseif(option == mcm.oid_NpcPagePlayerHome)
        string[] names = MARAS.GetAllPlayerHousesNames()
        names = PapyrusUtil.PushString(names, "unset")
        Location[] houses = MARAS.GetAllPlayerHouses()
        if(index >= houses.Length)
            TTM_ServicePlayerHouse.ReleaseSpouseFromPlayerHome(spouse)
        else
            TTM_ServicePlayerHouse.MoveSpouseToHouse(spouse, houses[index] as Location)
        endif
        opt = names[index]
    elseif(option == mcm.oid_NpcPageRank)
        int rank = MARAS.GetHierarchyRank(spouse)
        if(rank == index)
            return
        endif
        opt = GetHierarchyOptions()[index]
        MARAS.SetHierarchyRank(spouse, index)
    endif

    mcm.SetMenuOptionValue(option, opt)
EndFunction

Function OnOptionSliderOpen(TTM_MCM mcm, int option) global
    if(option == mcm.oid_NpcPageAffection)
        Actor spouse = TTM_MCM_State.GetSelectedNpc()
        int affection = MARAS.GetPermanentAffection(spouse)
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
        int previousAffection = MARAS.GetPermanentAffection(spouse)
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

        MARAS.SetPermanentAffection(spouse, newAffection)
        mcm.SetSliderOptionValue(option, newAffection as float, "{0}%")
    endif
EndFunction

; Default
Function OnOptionDefault(TTM_MCM mcm, int option) global
EndFunction

string[] Function GetHierarchyOptions() global
    string[] options = new string[3]

    options[0] = "$TTM_MCM_NpcRank1st"
    options[1] = "$TTM_MCM_NpcRank2nd"
    options[2] = "$TTM_MCM_NpcRank3rd"

    return options
EndFunction
