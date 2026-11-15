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

    Actor target = akTarget
    if(!target)
        target = TTM_Data.GetPlayer()
    endif
    MARAS.DirectNarration(SkyrimNetApi.ParseString(prompt, "values", values), akSpeaker, target)
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

Actor Function ChooseSpouseToAdoptWithMsg(Actor child = none) global
    if(MARAS.GetStatusCount("married") < 1)
        return none
    endif
    return MARAS.ShowChooseAdoptionSpouseModal(child)
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
