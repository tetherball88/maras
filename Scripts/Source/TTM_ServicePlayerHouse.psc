;/
  TTM_ServicePlayerHouse.psc

  Manages player houses and spouse housing assignments for the MARAS mod.

  Main responsibilities:
    - Tracking and storing player-owned houses
    - Allowing the player to assign spouses to houses
    - Managing house tenants and home markers
    - Integrating with JContainers for persistent storage
/;
scriptname TTM_ServicePlayerHouse

;/
  Checks if the player is in a player house location and registers it if so.
/;
Function CheckPlayerHouseLocation() global
    Actor player = TTM_JData.GetPlayer()
    Location currentLoc = player.GetCurrentLocation()
    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("CheckPlayerHouseLocation:"+currentLoc.GetName())
    endif
    if(currentLoc.HasKeywordString("LocTypePlayerHouse"))
        if(TTM_Debug.IsTrace())
            TTM_Debug.trace("CheckPlayerHouseLocation:playerHome")
        endif
        if(MARAS.RegisterPlayerHouseCell(currentLoc, TTM_JData.GetPlayer().PlaceAtMe(TTM_JData.GetHomeSandboxMarkerStatic())))
            TTM_JData.GetSetPlayerHousesCountGlobal(MARAS.CountPlayerHouses())
        endif
    endif
EndFunction

;/
  Shows a message to choose a house for a spouse, with paging support.
  @param spouse    The spouse actor to assign a house
  @param fromIndex The index to start paging from (default 0)
/;
Function ChooseHomeForSpouseMsg(Actor spouse, int page = 0) global
    if(page < 0)
        page = 0
    endif
    int perPage = 10
    Location[] playerHomes = MARAS.GetAllPlayerHouses()
    int count = playerHomes.Length

    if(count < 1)
        Debug.Notification("You don't have any home to choose from.")
        return
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
        string houseName = playerHomes[i].GetName()
        StorageUtil.FormListAdd(none, "SpouseHomeChoiceCache", playerHomes[i])
        listMenu.AddEntryItem(houseName)
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
    Form homeChoice = StorageUtil.FormListGet(none, "SpouseHomeChoiceCache", choice)

    if(homeChoice != none)
        MoveSpouseToHouse(spouse, homeChoice as Location)
    elseif(choice == prevIdx)
        ChooseHomeForSpouseMsg(spouse, page - 1)
    elseif(choice == nextIdx)
        ChooseHomeForSpouseMsg(spouse, page + 1)
    endif

    StorageUtil.FormListClear(none, "SpouseHomeChoiceCache")
EndFunction

Function ReleaseSpouseFromPlayerHome(Actor spouse) global
    Package spousePlayerHomeSandbox = TTM_JData.GetHomeSandboxPackage()
    ActorUtil.RemovePackageOverride(spouse, spousePlayerHomeSandbox)
    spouse.EvaluatePackage()
    TTM_ServiceRelationships.SetTrackedNpcHome(spouse, none)
    MARAS.RemoveTenantFromPlayerHouse(spouse)
EndFunction

;/
  Moves a spouse to a specified house location, updating their home marker and AI package.
  @param spouse   The spouse actor to move
  @param houseLoc The house location (optional, defaults to first house)
/;
Function MoveSpouseToHouse(Actor spouse, Location houseLoc ) global
    ObjectReference spouseMarker = TTM_ServiceRelationships.GetTrackedNpcHomeMarker(spouse)
    spouseMarker.MoveTo(MARAS.GetHouseMarker(houseLoc))

    Package spousePlayerHomeSandbox = TTM_JData.GetHomeSandboxPackage()

    ActorUtil.RemovePackageOverride(spouse, spousePlayerHomeSandbox)
    ActorUtil.AddPackageOverride(spouse, spousePlayerHomeSandbox, 5)
    spouse.EvaluatePackage()

    MARAS.RegisterTenantInPlayerHouse(spouse, houseLoc)
EndFunction


