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
    Actor player = TTM_Data.GetPlayer()
    Location currentLoc = player.GetCurrentLocation()
    TTM_Debug.trace("CheckPlayerHouseLocation:"+currentLoc.GetName())
    ObjectReference houseMarker = MARAS.GetHouseMarker(currentLoc)
    if(currentLoc.HasKeywordString("LocTypePlayerHouse") && houseMarker == none)
        Static homeMarker = TTM_Data.GetHomeSandboxMarkerStatic()
        ; Place marker as persistent so the reference FormID can be resolved later by the plugin
        ObjectReference placedMarker = player.PlaceAtMe(homeMarker, 1, true)
        MARAS.RegisterPlayerHouseCell(currentLoc, placedMarker)
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
        TTM_Messages.PlayerNoHomeMsg()
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

Function ReleaseSpouseFromPlayerHome(Actor spouse, string reason = "") global
    if(MARAS.GetTenantHouse(spouse) != none)
        if(reason == "affection")
            TTM_Messages.FreeSpouseFromPlayerHomeMsg(TTM_Utils.GetActorName(spouse))
        endif
    endif
    TTM_Debug.trace("ReleaseSpouseFromPlayerHome:spouse:"+spouse+":reason:"+reason)
    RemoveSpouseHouseSandboxPackage(spouse)
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
    ObjectReference houseMarker = MARAS.GetHouseMarker(houseLoc)
    TTM_Debug.trace("MoveSpouseToHouse:houseLoc:"+houseLoc.GetName()+":marker:"+houseMarker)
    spouseMarker.MoveTo(houseMarker)


    TTM_ServiceRelationships.SetTrackedNpcHome(spouse, houseLoc)

    MARAS.RegisterTenantInPlayerHouse(spouse, houseLoc)

    if(!MARAS.IsPlayerTeammate(spouse))
        AddSpouseHouseSandboxPackage(spouse)
    endif
EndFunction


Function AddSpouseHouseSandboxPackage(Actor spouse) global
    Package spousePlayerHomeSandbox = TTM_Data.GetHomeSandboxPackage()
    ActorUtil.RemovePackageOverride(spouse, spousePlayerHomeSandbox)
    ActorUtil.AddPackageOverride(spouse, spousePlayerHomeSandbox, 5)
    spouse.EvaluatePackage()
EndFunction

Function RemoveSpouseHouseSandboxPackage(Actor spouse) global
    Package spousePlayerHomeSandbox = TTM_Data.GetHomeSandboxPackage()
    ActorUtil.RemovePackageOverride(spouse, spousePlayerHomeSandbox)
    spouse.EvaluatePackage()
EndFunction

