scriptname TTM_ServiceSpouseAssets

Function StartShareHomeWithPlayer(Actor spouse) global
    Quest checkSpouseHome = TTM_Data.GetMarasCheckSpouseHomeQuest()
    if(checkSpouseHome.IsRunning())
        TTM_Debug.debug("StartShareHomeWithPlayer:AlreadyRunning")
        return
    endif

    if(MARAS.ShareHouseWithPlayer(spouse))
        StartQuest(spouse)
        TTM_ServiceSpouseAssets.CheckCell(spouse)
    else
        Faction noHomeFac = Game.GetFormFromFile(0x7F, "TT_MARAS.esp") as Faction
        if(!noHomeFac || !spouse.IsInFaction(noHomeFac))
            TTM_Debug.err("StartShareHomeWithPlayer:ShareFailed")
        endif
    endif
EndFunction

Function CheckCell(Actor spouse = none) global
    TTM_Debug.trace("TTM_ServiceSpouseAssets:CheckCell")
    Actor player = TTM_Data.GetPlayer()
    Cell currentCell = player.GetParentCell()
    if(!currentCell.IsInterior() || !MARAS.IsHouseSharedWithPlayer(currentCell))
        TTM_Debug.trace("CheckCell:NotSharedWithPlayer")
        return
    endif
    StopQuest()
EndFunction


Function StopShareHouseWithPlayer(Actor spouse, string reason = "") global
    MARAS.StopShareHouseWithPlayer(spouse, reason)
    StopQuest(spouse)
EndFunction

Function StartQuest(Actor spouse) global
    Quest checkSpouseHomeQst = TTM_Data.GetMarasCheckSpouseHomeQuest()
    ObjectReference spouseHomeMarker = MARAS.GetNpcOriginalHouseCenterMarker(spouse)
    if(spouseHomeMarker)
        if(checkSpouseHomeQst.SetStage(0))
            ReferenceAlias spouseAlias = checkSpouseHomeQst.GetAliasByName("Spouse") as ReferenceAlias
            spouseAlias.ForceRefTo(spouse)

            ReferenceAlias homeCellMarker = checkSpouseHomeQst.GetAliasByName("SpouseHomeCenter") as ReferenceAlias
            homeCellMarker.ForceRefTo(MARAS.GetNpcOriginalHouseCenterMarker(spouse))
            checkSpouseHomeQst.SetObjectiveDisplayed(0, true)
            checkSpouseHomeQst.SetActive(true)
        else
            TTM_Debug.err("StartShareHomeWithPlayer:StartedQuest:Failed")
        endif
    endif
EndFunction

Function StopQuest(Actor spouse = none) global
    Quest checkSpouseHomeQst = TTM_Data.GetMarasCheckSpouseHomeQuest()
    if(checkSpouseHomeQst.IsRunning())
        if(spouse && TTM_Utils.GetActorAlias(checkSpouseHomeQst, "Spouse") != spouse)
            return
        endif
        checkSpouseHomeQst.SetObjectiveCompleted(0)
        checkSpouseHomeQst.SetObjectiveDisplayed(0, false)
        checkSpouseHomeQst.Reset()
        checkSpouseHomeQst.Stop()
    endif
EndFunction
