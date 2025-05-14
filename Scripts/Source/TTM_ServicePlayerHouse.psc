;/
  TTM_ServicePlayerHouse.psc

  Manages player houses and spouse housing assignments for the MARAS mod.

  Main responsibilities:
    - Tracking and storing player-owned houses
    - Allowing the player to assign spouses to houses
    - Managing house tenants and home markers
    - Integrating with JContainers for persistent storage

  Dependencies:
    - TTM_JCDomain
    - TTM_JData
    - TTM_JUtils
    - TTM_ServiceNpcs
    - TTM_Utils
    - JContainers (JFormMap, JMap, JArray, etc)
    - SkyMessage, MiscUtil, ActorUtil
/;
scriptname TTM_ServicePlayerHouse

import TTM_JCDomain

;/
  Checks if the player is in a player house location and registers it if so.
/;
Function CheckPlayerHouseLocation() global
    Actor player = TTM_JData.GetPlayer()
    Location currentLoc = player.GetCurrentLocation()
    TTM_Debug.trace("CheckPlayerHouseLocation:"+currentLoc)
    if(currentLoc.HasKeywordString("LocTypePlayerHouse"))
        TTM_Debug.trace("CheckPlayerHouseLocation:playerHome")
        SetPlayerHouse(currentLoc)
        GetSetPlayerHouseCell(currentLoc, player.GetParentCell())
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
    int jPlayerHomes = GetPlayerHouses()

    int count = JFormMap_count(jPlayerHomes)
    if(count < 1)
        Debug.Notification("You don't have any home to choose from.")
        return
    endif
    UIListMenu listMenu = UIExtensions.GetMenu("UIListMenu", true) as UIListMenu

    Form[] playerHomes = JFormMap_allKeysPArray(jPlayerHomes)
    int jMessageOptions = JValue_retain(JMap_object())

    int startIdx = page * perPage
    int endIdx = startIdx + perPage
    if(endIdx > count)
        endIdx = count
    endif

    int menuIdx = 0
    int i = startIdx

    while(i < endIdx)
        string houseName = playerHomes[i].GetName()
        JMap_setForm(jMessageOptions, menuIdx, playerHomes[i])
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

    if(JMap_hasKey(jMessageOptions, choice))
        MoveSpouseToHouse(spouse, JMap_getForm(jMessageOptions, choice) as Location)
    elseif(choice == prevIdx)
        ChooseHomeForSpouseMsg(spouse, page - 1)
    elseif(choice == nextIdx)
        ChooseHomeForSpouseMsg(spouse, page + 1)
    endif

    JValue_release(jMessageOptions)
EndFunction

Function ReleaseSpouseFromPlayerHome(Actor spouse) global
    Package spousePlayerHomeSandbox = TTM_JData.GetHomeSandboxPackage()
    ActorUtil.RemovePackageOverride(spouse, spousePlayerHomeSandbox)
    spouse.EvaluatePackage()
    spouse.RemoveFromFaction(TTM_JData.GetSpouseHousedFaction())
EndFunction

;/
  Moves a spouse to a specified house location, updating their home marker and AI package.
  @param spouse   The spouse actor to move
  @param houseLoc The house location (optional, defaults to first house)
/;
Function MoveSpouseToHouse(Actor spouse, Location houseLoc = none ) global
    houseLoc = JFormMap_nextKey(GetPlayerHouses()) as Location
    ObjectReference spouseMarker = TTM_ServiceNpcs.GetTrackedNpcHomeMarker(spouse)
    spouseMarker.MoveTo(GetPlayerHouseMarker(houseLoc))

    Package spousePlayerHomeSandbox = TTM_JData.GetHomeSandboxPackage()

    ActorUtil.RemovePackageOverride(spouse, spousePlayerHomeSandbox)
    ActorUtil.AddPackageOverride(spouse, spousePlayerHomeSandbox, 100)
    spouse.EvaluatePackage()
    spouse.AddToFaction(TTM_JData.GetSpouseHousedFaction())

    Location spouseCurrentHome = TTM_ServiceNpcs.GetTrackedNpcHome(spouse)
    if(spouseCurrentHome)
        RemovePlayerHomeTenant(spouseCurrentHome, spouse)
    endif
    TTM_ServiceNpcs.SetTrackedNpcHome(spouse, houseLoc)
    AddPlayerHomeTenant(houseLoc, spouse)
EndFunction

;/
  Returns the JFormMap of all player houses.
/;
int Function GetPlayerHouses() global
    return TTM_JUtils._GetOrCreateJFormMap(TTM_JData.GetJSaveData(), "playerHouses")
EndFunction

;/
  Registers a location as a player house if not already present.
  @param houseLoc The house location to register
/;
int Function SetPlayerHouse(Location houseLoc) global
    TTM_Debug.trace("SetPlayerHouse")
    int jHouse = JFormMap_getObj(GetPlayerHouses(), houseLoc)

    if(jHouse == 0)
        jHouse = JMap_object()

        JMap_setStr(jHouse, "name", houseLoc.GetName())
        JMap_setForm(jHouse, "houseMarker", TTM_JData.GetPlayer().PlaceAtMe(TTM_JData.GetHomeSandboxMarkerStatic()))

        int jTenants = JArray_Object()
        JMap_setObj(jHouse, "tenants", jTenants)

        JFormMap_setObj(GetPlayerHouses(), houseLoc, jHouse)

        CountPlayerHouses()
    endif
EndFunction

;/
  Gets the JMap object for a player house, creating it if needed.
  @param houseLoc The house location
  @return         The JMap object for the house
/;
int Function GetPlayerHouse(Location houseLoc) global
   return JFormMap_getObj(GetPlayerHouses(), houseLoc)
EndFunction

;/
  Gets or creates the JFormMap of cells for a house.
  @param houseLoc The house location
  @return         The JFormMap of cells
/;
int Function GetSetPlayerHouseCells(Location houseLoc) global
    int jHouse = GetPlayerHouse(houseLoc)
    return TTM_JUtils._GetOrCreateJFormMap(jHouse, "cells")
EndFunction

;/
  Gets or creates the JMap object for a specific cell in a house.
  @param houseLoc The house location
  @param houseCell The cell to get or create
  @return         The JMap object for the cell
/;
int Function GetSetPlayerHouseCell(Location houseLoc, Cell houseCell) global
    int jCells = GetSetPlayerHouseCells(houseLoc)
    int jCell = JFormMap_getObj(jCells, houseCell)

    if(jCell == 0)
        jCell = JMap_object()
        JMap_setStr(jCell, "name", houseCell.GetName())
        JFormMap_setObj(jCells, houseCell, jCell)
    endif

    return jCell
EndFunction

;/
  Gets the house marker object reference for a house location.
  @param houseLoc The house location
  @return         The house marker reference
/;
ObjectReference Function GetPlayerHouseMarker(Location houseLoc) global
    return JMap_getForm(GetPlayerHouse(houseLoc), "houseMarker") as ObjectReference
EndFunction

;/
  Gets the JArray of tenants for a house.
  @param home The house location
  @return     The JArray of tenants
/;
int Function GetHomeTenants(Location home) global
    if(!JFormMap_hasKey(GetPlayerHouses(), home))
        return 0
    endif
    int jHome = GetPlayerHouse(home)
    return JMap_getObj(jHome, "tenants")
EndFunction

;/
  Adds a tenant (spouse) to a house if not already present.
  @param home   The house location
  @param tenant The actor to add as tenant
/;
Function AddPlayerHomeTenant(Location home, Actor tenant) global
    int jTenants = GetHomeTenants(home)
    if(JArray_findForm(jTenants, tenant) == -1)
        JArray_addForm(jTenants, tenant)
    endif
EndFunction

;/
  Removes a tenant (spouse) from a house if present.
  @param home   The house location
  @param tenant The actor to remove as tenant
/;
Function RemovePlayerHomeTenant(Location home, Actor tenant) global
    int jTenants = GetHomeTenants(home)
    if(JArray_findForm(jTenants, tenant) != -1)
        JArray_eraseForm(jTenants, tenant)
    endif
EndFunction

Function CountPlayerHouses() global
    TTM_Debug.trace("CountPlayerHouses:"+JFormMap_count(GetPlayerHouses()))
    TTM_JData.GetSetPlayerHousesCountGlobal(JFormMap_count(GetPlayerHouses()))
EndFunction
