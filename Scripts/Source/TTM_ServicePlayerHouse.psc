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
    if(currentLoc && currentLoc.HasKeywordString("LocTypePlayerHouse"))
        RegisterPlayerHouseLocation(currentLoc, player)
    endif
EndFunction

Function RegisterPlayerHouseLocation(Location akLocation, ObjectReference akHouseRef) global
    if(!akLocation.HasKeywordString("LocTypePlayerHouse") || !akHouseRef)
        TTM_Debug.trace("RegisterPlayerHouseLocation:Invalid location "+akLocation+" or house reference " + akHouseRef)
        return
    endif
    ObjectReference houseMarker = MARAS.GetHouseMarker(akLocation)
    if(houseMarker == none)
        Static homeMarker = TTM_Data.GetHomeSandboxMarkerStatic()
        ; Place marker as persistent so the reference FormID can be resolved later by the plugin
        ObjectReference placedMarker = akHouseRef.PlaceAtMe(homeMarker, 1, true)
        MARAS.RegisterPlayerHouseCell(akLocation, placedMarker)
    endif
EndFunction

;/
  Shows a message to choose a house for a spouse, with paging support.
  @param spouse    The spouse actor to assign a house
  @param fromIndex The index to start paging from (default 0)
/;
Location Function ChooseHomeForSpouseMsg(Actor spouse, int page = 0, bool isChild = false) global
    if(page < 0)
        page = 0
    endif
    int perPage = 10
    Location[] playerHomes = MARAS.GetAllPlayerHouses()
    int count = playerHomes.Length

    TTM_Debug.debug("ChooseHomeForSpouseMsg: count = " + count)

    if(count < 1)
        TTM_Messages.PlayerNoHomeMsg()
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
        if(isChild)
            MoveChildToHouse(spouse, homeChoice as Location)
        else
            bool stayAtHome = true
            UIListMenu scheduleMenu = UIExtensions.GetMenu("UIListMenu", true) as UIListMenu
            if(scheduleMenu)
                scheduleMenu.AddEntryItem("Stay at Home")
                scheduleMenu.AddEntryItem("Keep Original Schedule")
                scheduleMenu.OpenMenu()
                int schedChoice = scheduleMenu.GetResultInt()
                if(schedChoice == 1)
                    stayAtHome = false
                endif
            endif
            MoveSpouseToHouse(spouse, homeChoice as Location, stayAtHome)
        endif
        return homeChoice as Location
    elseif(choice == prevIdx)
        ChooseHomeForSpouseMsg(spouse, page - 1, isChild)
    elseif(choice == nextIdx)
        ChooseHomeForSpouseMsg(spouse, page + 1, isChild)
    endif

    StorageUtil.FormListClear(none, "SpouseHomeChoiceCache")
    return none
EndFunction

Function ReleaseSpouseFromPlayerHome(Actor spouse, string reason = "") global
    if(MARAS.GetChildren(TTM_Data.GetPlayer(), spouse).Length > 0)
        Debug.Notification("Cannot send " + TTM_Utils.GetActorName(spouse) + " away while you have children together. Move them to a new home instead.")
        return
    endif
    if(MARAS.GetTenantHouse(spouse) != none)
        if(reason == "affection")
            TTM_Messages.FreeSpouseFromPlayerHomeMsg(TTM_Utils.GetActorName(spouse))
        endif
    endif
    TTM_Debug.trace("ReleaseSpouseFromPlayerHome:spouse:"+spouse+":reason:"+reason)
    MARAS.RemoveTenantFromPlayerHouse(spouse)
EndFunction

Function ReleaseChildFromPlayerHome(Actor child) global
    TTM_Debug.trace("ReleaseChildFromPlayerHome:child:"+child)
    MARAS.RemoveTenantFromPlayerHouse(child)
EndFunction

;/
  Moves a spouse to a specified house location, updating their home marker and AI package.
  @param spouse     The spouse actor to move
  @param houseLoc   The house location (optional, defaults to first house)
  @param stayAtHome Whether spouse stays and sandboxes at home (true) or keeps original routine (false)
/;
Function MoveSpouseToHouse(Actor spouse, Location houseLoc, bool stayAtHome = true) global
    TTM_Debug.trace("MoveSpouseToHouse:houseLoc:"+houseLoc.GetName()+":stayAtHome:"+stayAtHome)

    MARAS.RegisterTenantInPlayerHouse(spouse, houseLoc, stayAtHome)
EndFunction

ObjectReference Function GetChildHomeMarker(Actor child) global
    return MARAS.GetChildHomeMarker(child)
EndFunction

Function MoveChildToHouse(Actor child, Location houseLoc) global
    ObjectReference childMarker = GetChildHomeMarker(child)
    ObjectReference houseMarker = MARAS.GetHouseMarker(houseLoc)
    TTM_Debug.trace("MoveChildToHouse:houseLoc:"+houseLoc.GetName()+":marker:"+houseMarker)
    if(childMarker && houseMarker)
        childMarker.MoveTo(houseMarker)
    endif

    MARAS.AssignChildToHouse(child, houseLoc)
EndFunction

Function CleanPapyrusUtilPackages() global
    ActorUtil.RemoveAllPackageOverride(TTM_Data.GetHomeSandboxPackage())
EndFunction

; Maps vanilla marriage fin quest spouse assigned location to player houses center marks
ObjectReference Function TrnaslateVanillaSpousePlayerHouseToLoc(int idx) global
    if (idx == 1) ; Solitude
		return Game.GetFormFromFile(0x20097, "Skyrim.esm") as ObjectReference
	ElseIf (idx == 2) ; Windhelm
		return Game.GetFormFromFile(0x209d6, "Skyrim.esm") as ObjectReference
	ElseIf (idx == 3) ; Markarth
		return Game.GetFormFromFile(0x1f323, "Skyrim.esm") as ObjectReference
	ElseIf (idx == 4) ; Riften
		return Game.GetFormFromFile(0x42a0, "Hearthfire.esm") as ObjectReference
	ElseIf (idx == 5) ; Whiterun
		return Game.GetFormFromFile(0x1f88e, "Skyrim.esm") as ObjectReference
	ElseIf (idx == 6) ; Falkreath
		return Game.GetFormFromFile(0x309b, "Hearthfire.esm") as ObjectReference
	ElseIf (idx == 7) ; Hjaalmarch
		return Game.GetFormFromFile(0x1205c, "Hearthfire.esm") as ObjectReference
	ElseIf (idx == 8) ; The Pale
		return Game.GetFormFromFile(0x16e05, "Hearthfire.esm") as ObjectReference
    endif
	return none
EndFunction
