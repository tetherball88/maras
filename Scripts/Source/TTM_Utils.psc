scriptname TTM_Utils

string Function GetActorName(actor akActor) global
    if (!akActor)
      return "Unknown"
    endif
    if akActor == TTM_Data.GetPlayer()
      return akActor.GetActorBase().GetName()
    else
      return akActor.GetDisplayName()
    EndIf
EndFunction

Actor Function GetActorAlias(Quest qst, string name) global
    ReferenceAlias al = qst.GetAliasByName(name) as ReferenceAlias
    return al.GetActorRef()
EndFunction

Function SetActorAlias(Quest qst, string name, Actor akActor) global
    ReferenceAlias al = qst.GetAliasByName(name) as ReferenceAlias
    al.ForceRefTo(akActor)
EndFunction

int Function ToggleGlobalVariable(GlobalVariable gVar) global
    float val = gVar.GetValue()
    int newVal = 0
    if(val == 0)
        newVal = 1
    endif
    gVar.SetValue(newVal)

    return newVal
EndFunction

string Function GetActorsNamesJson(Actor[] actors, Actor excluded = none) global
    if(actors.length == 0)
        return ""
    endif
    int i = 0
    string listName = "TmpActorNames" + Utility.GetCurrentRealTime()

    int aLength = actors.length

    while(i < actors.length)
        Actor currentActor = actors[i]

        if(currentActor && currentActor != excluded)
            string actorName = GetActorName(currentActor)
            if(actorName != "")
                StorageUtil.StringListAdd(none, listName, actorName)
            else
                TTM_Debug.warn("GetActorsNamesJson: Actor has no name! Actor FormID: " + currentActor.GetFormID())
            endif
        endif
        i += 1
    endwhile

    return PapyrusUtil.StringJoin(StorageUtil.StringListToArray(none, listName), ", ")
EndFunction

Function FadeToBlack() global
    ImageSpaceModifier fadeToBlack = Game.GetFormFromFile(0xf756d, "Skyrim.esm") as ImageSpaceModifier
    ImageSpaceModifier fadeToBlackHold = Game.GetFormFromFile(0xf756e, "Skyrim.esm") as ImageSpaceModifier
    ImageSpaceModifier fadeToBlackBack = Game.GetFormFromFile(0xf756f, "Skyrim.esm") as ImageSpaceModifier

    fadeToBlack.Apply()
    Utility.Wait(2.0)
    fadeToBlack.PopTo(fadeToBlackHold)
    Utility.Wait(2.0)
    fadeToBlackHold.PopTo(fadeToBlackBack)
EndFunction

Function RequestDialogue(string prompt, string values, Actor akSpeaker, Actor akTarget = none) global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif

    SkyrimNetApi.DirectNarration(SkyrimNetApi.ParseString(prompt, "values", values), akSpeaker, TTM_Data.GetPlayer())
EndFunction

Function ReinforceEnablePolygamySpouseAlias() global
    Quest enablePolygamyQst = TTM_Data.GetMarasEnablePolygamyQuest()
    ReferenceAlias spouseAlias = enablePolygamyQst.GetAliasByName("CurrentSpouse") as ReferenceAlias
    Actor spouseRef = spouseAlias.GetActorRef()
    if(enablePolygamyQst.IsRunning() && !spouseRef)
        Actor[] spouses = MARAS.GetNPCsByStatus("married")
        if(spouses.Length == 0)
            TTM_Debug.err("MainController:Maintenance: EnablePolygamyQuest is running but no married spouses found!")
        else
            TTM_Debug.debug("MainController:Maintenance: Forcing spouse alias to " + TTM_Utils.GetActorName(spouse))
            Actor spouse = spouses[0]
            spouseAlias.ForceRefTo(spouse)
        endif
    endif
EndFunction

; bool Function ShowAIConfirmationMessage(string type, Actor npc) global
;     if(!TTM_Data.GetHasSkyrimNet())
;         return false
;     endif

;     if(!MARAS.GetSettingBool(type))
;         return true
;     endif

;     string actorName = GetActorName(npc)
;     string msg = actorName + " "
;     if(type == "ConfirmAcceptProposal")
;         msg += "accepted your proposal."
;     elseif(type == "ConfirmBreakupEngagement")
;         msg += "wants to breakup with you."
;     elseif(type == "ConfirmDivorce")
;         msg += "wants a divorce."
;     elseif(type == "ConfirmDivorceDuringLowAffectionQuest")
;         msg = "decided to divorce you after you neglected them."
;     elseif(type == "ConfirmSetRelationshipStatus")
;         msg += "wants to change relationship status."
;     endif

;     string[] buttons = new string[2]
;     buttons[0] = "$TTM_MSG_ConfirmActionYes"
;     buttons[1] = "$TTM_MSG_ConfirmActionNo"

;     int buttonIndex = SkyMessage.ShowArray(msg, buttons, getIndex = true) as int

;     if(buttonIndex == 0)
;         return true
;     elseif(buttonIndex == 1)
;         StorageUtil.SetFloatValue(npc, "TTM_ConfirmationActionCooldown." + type, Utility.GetCurrentGameTime())
;         return false
;     endif
; EndFunction

bool Function OpenMisclickedMenu(string type, Actor npc) global
    UIListMenu listMenu = UIExtensions.GetMenu("UIListMenu", true) as UIListMenu
    listMenu.AddEntryItem("$TTM_MSG_ConfirmActionMisclick")
    listMenu.AddEntryItem("$TTM_MSG_ConfirmActionYes")
    listMenu.AddEntryItem("$TTM_MSG_ConfirmActionNo")

    listMenu.OpenMenu()
    int choice = listMenu.GetResultInt()

    if(choice == 1)
        return true
    elseif(choice == 2)
        StorageUtil.SetFloatValue(npc, "TTM_ConfirmationActionCooldown." + type, Utility.GetCurrentGameTime())
        return false
    elseif(choice == 0)
        return OpenMisclickedMenu(type, npc)
    else
        return true
    endif
EndFunction

bool Function ActionConfirmationCooldownPassed(string type, Actor npc, int cooldownInDays = 1) global
    float lastActionTime = StorageUtil.GetFloatValue(npc, "TTM_ConfirmationActionCooldown." + type, 0.0)
    return Utility.GetCurrentGameTime() >= lastActionTime + cooldownInDays
EndFunction

Actor Function ChooseSpouseToAdoptWithMsg(int page = 0) global
    if(page < 0)
        page = 0
    endif
    int perPage = 10
    Actor[] spouses = MARAS.GetNPCsByStatus("married")
    int count = spouses.Length

    if(count < 1)
        return none
    endif
    UIListMenu listMenu = UIExtensions.GetMenu("UIListMenu", true) as UIListMenu

    int startIdx = page * perPage
    int endIdx = startIdx + perPage
    if(endIdx > count)
        endIdx = count
    endif

    int menuIdx = 0
    int i = startIdx

    while(i < endIdx)
        string spouseName = GetActorName(spouses[i])
        StorageUtil.FormListAdd(none, "SpouseToAdoptWithChoiceCache", spouses[i])
        listMenu.AddEntryItem(spouseName)
        menuIdx += 1
        i += 1
    endwhile

    int prevIdx = -1
    int nextIdx = -1
    if(page > 0)
        prevIdx = listMenu.AddEntryItem("Previous page")
    endif
    if(endIdx < count)
        nextIdx = listMenu.AddEntryItem("Next page")
    endif
    listMenu.AddEntryItem("Cancel")

    listMenu.OpenMenu()
    int choice = listMenu.GetResultInt()
    Actor spouseChoice = StorageUtil.FormListGet(none, "SpouseToAdoptWithChoiceCache", choice) as Actor

    if(spouseChoice)
        StorageUtil.FormListClear(none, "SpouseToAdoptWithChoiceCache")
        return spouseChoice
    elseif(choice == prevIdx)
        return ChooseSpouseToAdoptWithMsg(page - 1)
    elseif(choice == nextIdx)
        return ChooseSpouseToAdoptWithMsg(page + 1)
    endif

    StorageUtil.FormListClear(none, "SpouseToAdoptWithChoiceCache")
EndFunction

Location Function GetSpouseMigrationPlayerHouse() global
    Location playerHouseLoc = GetChildMigrationPlayerHouse()
    if(playerHouseLoc)
        return playerHouseLoc
    endif

    RelationshipMarriageSpouseHouseScript marFinQst = TTM_Data.GetMarriageFinQuest() as RelationshipMarriageSpouseHouseScript
    if(marFinQst.IsRunning())
        int spouseHouseId = marFinQst.SpouseCurrentHome
        ObjectReference spouseHouseRef = TTM_ServicePlayerHouse.TrnaslateVanillaSpousePlayerHouseToLoc(spouseHouseId)
        playerHouseLoc = spouseHouseRef.GetCurrentLocation()
        if(playerHouseLoc && spouseHouseRef)
            TTM_ServicePlayerHouse.RegisterPlayerHouseLocation(playerHouseLoc, spouseHouseRef)
            return playerHouseLoc
        endif
    endif

    return none
EndFunction

string Function GetChildNameByParents(Actor mother, Actor father, int gender = -1) global
    if(gender == -1)
        gender = Utility.RandomInt(0, 1)
    endif
    Race raceForName = mother.GetRace()
    if(Utility.RandomInt(0,1) == 0 && father)
        raceForName = father.GetRace()
    endif
    return MARAS.GenerateRandomChildName(raceForName, gender == 1)
EndFunction

Location Function GetChildMigrationPlayerHouse() global
    Quest childSandboxQst = TTM_Data.GetOriginalChildrenSandboxQuest()
    if(childSandboxQst.IsRunning())
        LocationAlias playerHouseAlias = childSandboxQst.GetAliasByName("CurrentHomeLocation") as LocationAlias
        Location playerHouseLoc = playerHouseAlias.GetLocation()
        if(playerHouseLoc.HasKeywordString("LocTypePlayerHouse"))
            ReferenceAlias playerHouseAliasRef = childSandboxQst.GetAliasByName("Bed1") as ReferenceAlias
            ObjectReference houseRef = playerHouseAliasRef.GetRef()
            TTM_ServicePlayerHouse.RegisterPlayerHouseLocation(playerHouseLoc, houseRef)
            return playerHouseLoc
        endif
    endif
    return none
EndFunction
