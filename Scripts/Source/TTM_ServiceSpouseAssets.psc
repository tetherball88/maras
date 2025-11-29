scriptname TTM_ServiceSpouseAssets

Function StartShareHomeWithPlayer(Actor spouse) global
    Quest checkSpouseHome = TTM_Data.GetMarasCheckSpouseHomeQuest()
    if(checkSpouseHome.IsRunning())
        TTM_Debug.debug("StartShareHomeWithPlayer:AlreadyRunning")
        return
    endif

    if(MARAS.GetNpcOriginalHouse(spouse) == none)
        TTM_Debug.warn("StartShareHomeWithPlayer:NoHomeCell")
        spouse.AddToFaction(TTM_Data.GetSpouseNoInitialHouseFaction())
        TTM_Messages.SpouseNoHomeMsg(TTM_Utils.GetActorName(spouse))
        return
    endif

    if(MARAS.ShareHouseWithPlayer(spouse))
        Cell spouseHomeCell = MARAS.GetNpcOriginalHouse(spouse)
        TTM_Messages.SpouseSharedHomeMsg(TTM_Utils.GetActorName(spouse), spouseHomeCell.GetName())
        spouse.AddToFaction(TTM_Data.GetSpouseSharedHouseFaction())
        ObjectReference[] doors = MARAS.GetCellDoors(spouseHomeCell)
        int i = 0
        while(i < doors.Length)
            key NeededKey = doors[i].GetKey()
            if(!NeededKey)
                ; if door can be lock picked, just unlock it
                if(doors[i].GetLockLevel() != 5)
                    doors[i].Lock(false)
                endif
            else
                Actor player = TTM_Data.GetPlayer()
                if player.GetItemCount(NeededKey) == 0
                    player.AddItem(NeededKey)
                EndIf
            endif

            i += 1
        endwhile
        StartQuest(spouse)
        TTM_ServiceSpouseAssets.CheckCell(spouse)
    else
        TTM_Debug.err("StartShareHomeWithPlayer:ShareFailed")
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
    string spouseName = TTM_Utils.GetActorName(spouse)
    if(MARAS.HasSpouseSharedHouseWithPlayer(spouse))
        TTM_Messages.SpouseSharedHomeStoppedMsg(spouseName, reason)
    endif
    MARAS.StopShareHouseWithPlayer(spouse)
    StopQuest()
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

Function StopQuest() global
    Quest checkSpouseHomeQst = TTM_Data.GetMarasCheckSpouseHomeQuest()
    if(checkSpouseHomeQst.IsRunning())
        checkSpouseHomeQst.SetObjectiveCompleted(0)
        checkSpouseHomeQst.SetObjectiveDisplayed(0, false)
        checkSpouseHomeQst.Reset()
        checkSpouseHomeQst.Stop()
    endif
EndFunction
