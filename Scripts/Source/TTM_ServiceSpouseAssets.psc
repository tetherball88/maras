;/
  TTM_ServiceSpouseAssets.psc

  Manages marital assets for the MARAS mod, including homes, cells, objects, and keys shared with the player through marriage.

  Main responsibilities:
    - Track and manage locations, cells, and objects associated with each spouse
    - Handle sharing and restoring of object ownership
    - Manage keys and access for the player and spouses
    - Integrate with JContainers for persistent storage

  Dependencies:
    - TTM_JCDomain
    - TTM_JData
    - TTM_JUtils
    - TTM_Debug
    - TTM_Utils
/;
scriptname TTM_ServiceSpouseAssets

import TTM_JCDomain

;/
  Data structure for marital assets (see below for details)
/
{
    assets: {
        ; set location when spouse added to narrow search when checking cell
        locations: {
            [Location]: {
                name: string
                spouses: Actor[]
            }
        }
        ; map between shared with player objects(beds) and their original owners to restore if needed
        originalOwnersObject: {
            [ReferenceObject]: ActorBase
        }
        ; check everything what was added from spouse for easier find and remove if needed(divorce)
        spouses: {
            [Actor]: {
                locations: Location[]
                cells: Cell[]
                objects: ObjectReference[]
                keys: Key[]
            }
        }
        keys: {
            [Key]: {
                [Actor]: true | false
            }
        }
        ; map of checked and scanned cells to avoid multiple heavy scanning if same cell
        ; also stores which spouses provide access to this cell
        cells: {
            [Cell]: {
                isScanned: true | false
                name: string
                spouses: {
                    [Actor]: true | false
                }
                ownersActors: {
                    [ActorBase]: ReferenceObject[]
                }
            }
        }
    }
}
/;

Function StartShareHomeWithPlayer(Actor spouse) global
    spouse.AddToFaction(TTM_JData.GetCheckSpouseHomeFaction())
    TTM_JData.GetMarasCheckSpouseHomeQuest().SetStage(0)
EndFunction

;/
  Checks the current cell for spouse ownership and shares objects with the player if owned.
  @param spouse The spouse actor to check (optional, will use quest alias if not provided)
/;
Function CheckCell(Actor spouse = none) global
    TTM_debug.trace("CheckCell")
    Actor player = TTM_JData.GetPlayer()
    Cell currentCell = player.GetParentCell()
    
    if(!currentCell.IsInterior())
        TTM_debug.trace("CheckCell:NotInterior")
        return
    endif

    Location currentLoc = player.GetCurrentLocation()

    if(!JFormMap_hasKey(GetLocations(), currentLoc))
        TTM_debug.trace("CheckCell:Not registered spouse location")
        return
    endif

    Quest checkSpouseHomeQst = TTM_JData.GetMarasCheckSpouseHomeQuest()
    LocationAlias spouseLocAl = checkSpouseHomeQst.GetAliasByName("spouseHome") as LocationAlias
    Location spouseLoc = spouseLocAl.GetLocation()

    if(spouseLoc != currentLoc)
        TTM_debug.trace("CheckCell:NotSpouseHome")
        return
    endif

    if(!spouse)
        ReferenceAlias spouseAl = checkSpouseHomeQst.GetAliasByName("spouse") as ReferenceAlias
        spouse = spouseAl.GetActorRef()
        TTM_debug.trace("CheckCell:SetSpouseFromQuest")
    endif

    ; already written to JContainers storage
    if(HasCellSpouse(currentCell, spouse))
        TTM_debug.trace("CheckCell:ThisCellIsInStorage")
        currentCell.SetPublic()
        return
    endif

    ActorBase actorOwner = currentCell.GetActorOwner()
    Faction factionOwner = currentCell.GetFactionOwner()
    bool spouseOwnsCell = false
    if(actorOwner)
        spouseOwnsCell = actorOwner == spouse.GetActorBase()
    elseif(factionOwner)
        spouseOwnsCell = spouse.IsInFaction(factionOwner)
    endif

    if(spouseOwnsCell)
        TTM_debug.trace("CheckCell:SpouseOwnsThisHome")
        AddCellSpouse(currentCell, spouse)
        int jObjects = GetActorObjects(currentCell, spouse.GetActorBase())
        int i = 0
        int count = JArray_count(jObjects)

        while(i < count)
            ShareObjectWithPlayer(spouse, JArray_getForm(jObjects, i) as ObjectReference)
            i += 1
        endwhile

        spouse.AddToFaction(TTM_JData.GetSpouseSharedHouseFaction())
        spouse.RemoveFromFaction(TTM_JData.GetCheckSpouseHomeFaction())
        checkSpouseHomeQst.SetObjectiveCompleted(0)
        checkSpouseHomeQst.SetObjectiveDisplayed(0, false)
        checkSpouseHomeQst.Reset()
        checkSpouseHomeQst.Stop()
    endif
EndFunction

Function StopShareHomeWithPlayer(Actor spouse) global
    spouse.RemoveFromFaction(TTM_JData.GetSpouseSharedHouseFaction())
    int jSpouseObjects = GetSpouseArray(spouse, "objects")
    int i = 0
    while(i < JArray_count(jSpouseObjects))
        RestoreObjectOwnership(JArray_getForm(jSpouseObjects, i) as ObjectReference)
        i += 1
    endwhile

    int jSpouseCells = GetSpouseArray(spouse, "cells")
    i = 0
    while(i < JArray_count(jSpouseCells))
        RemoveCellSpouse(JArray_getForm(jSpouseCells, i) as Cell, spouse)
        i += 1
    endwhile

    int jSpouseLocations = GetSpouseArray(spouse, "locations")
    i = 0
    while(i < JArray_count(jSpouseLocations))
        RemoveLocationSpouse(JArray_getForm(jSpouseLocations, i) as Location, spouse)
        i += 1
    endwhile

    int jSpouseKeys = GetSpouseArray(spouse, "keys")
    Actor player = TTM_JData.GetPlayer()
    i = 0
    while(i < JArray_count(jSpouseKeys))
        Key doorKey = JArray_getForm(jSpouseKeys, i) as Key
        RemoveKeySpouse(doorKey, spouse)
        int keyCount = player.GetItemCount(doorKey)
        if(keyCount > 0)
            player.RemoveItem(doorKey, keyCount)
        endif
        
        i += 1
    endwhile

    JFormMap_removeKey(GetSpouses(), spouse)
EndFunction

;/
  Finds and registers the original home location for a spouse.
  @param spouse The spouse actor
/;
Function FindSpouseHome(Actor spouse) global
    Actor player = TTM_JData.GetPlayer()
    Location spouseHome = GetSpouseOriginalHome(spouse)
    TTM_Debug.trace("FindSpouseHome:"+spouseHome)
    if(!spouseHome)
        return
    endif

    AddLocationSpouse(spouseHome, spouse)
    AddSpouseRecord(spouse, "locations", spouseHome)
EndFunction

;/
  Shares an object with the player by setting ownership and recording the original owner.
  @param spouse The spouse actor
  @param obj    The object to share
/;
Function ShareObjectWithPlayer(Actor spouse, ObjectReference obj) global
    ActorBase actorOwner
    Faction playerFaction = TTM_JData.GetPlayerFaction()
    actorOwner = obj.GetActorOwner()
    
    if(!actorOwner)
        return
    endif

    SetOriginalOwnerObject(obj, actorOwner)
    AddSpouseRecord(spouse, "objects", obj)

    obj.SetActorOwner(none)
    obj.SetFactionOwner(playerFaction)
EndFunction

;/
  Restores the original ownership of an object.
  @param obj The object to restore
/;
Function RestoreObjectOwnership(ObjectReference obj) global
    ActorBase actorOwnership = GetOriginalOwnerObject(obj) as ActorBase
    
    if(actorOwnership)
        obj.SetFactionOwner(none)
        obj.SetActorOwner(actorOwnership)
    endif
EndFunction

;/
  Gets the original home location for a spouse, if it is a dwelling or house.
  @param spouse The spouse actor
  @return      The original home location or none
/;
Location Function GetSpouseOriginalHome(Actor spouse) global
    Location loc = spouse.GetEditorLocation()
    
    if(loc.HasKeyword(TTM_JData.GetLocTypeDwellingKeyword()) || loc.HasKeyword(TTM_JData.GetLocTypeHouseKeyword()))
        return loc
    endif
    
    return none
EndFunction

;/
  Gets or creates the JMap for all assets.
/;
int Function GetAssets() global
    return TTM_JUtils._GetOrCreateJMap(TTM_JData.GetJSaveData(), "assets")
EndFunction

;/
  Gets or creates the JFormMap for all locations.
/;
int Function GetLocations() global
    return TTM_JUtils._GetOrCreateJFormMap(GetAssets(), "locations")
EndFunction

;/
  Gets or creates the JMap for a specific location.
  @param loc The location
  @return    The JMap for the location
/;
int Function GetLocation(Location loc) global
    int jLoc = JFormMap_getObj(GetLocations(), loc)
    
    if(jLoc == 0)
        jLoc = JMap_object()
        JMap_setStr(jLoc, "name", loc.GetName())
        JFormMap_setObj(GetLocations(), loc, jLoc)
    endif

    return jLoc
EndFunction

;/
  Removes a location from the assets.
  @param loc The location to remove
/;
Function RemoveLocation(Location loc) global
    int jLoc = GetLocation(loc)

    if(jLoc != 0)
        JFormMap_removeKey(GetLocations(), loc)
    endif
EndFunction

;/
  Gets or creates the JFormMap of spouses for a location.
  @param loc The location
  @return    The JFormMap of spouses
/;
int Function GetLocationSpouses(Location loc) global
    return TTM_JUtils._GetOrCreateJFormMap(GetLocation(loc), "spouses")
EndFunction

;/
  Adds a spouse to a location's spouse list.
  @param loc    The location
  @param spouse The spouse actor
/;
Function AddLocationSpouse(Location loc, Actor spouse) global
    int jSpouses = GetLocationSpouses(loc)

    if(!JFormMap_hasKey(jSpouses, spouse))
        JFormMap_setStr(jSpouses, spouse, "true")
    endif
EndFunction

;/
  Removes a spouse from a location's spouse list, and removes the location if empty.
  @param loc    The location
  @param spouse The spouse actor
/;
Function RemoveLocationSpouse(Location loc, Actor spouse) global
    int jSpouses = GetLocationSpouses(loc)

    if(JFormMap_hasKey(jSpouses, spouse))
        JFormMap_removeKey(jSpouses, spouse)

        if(JFormMap_count(jSpouses) == 0)
            RemoveLocation(loc)
        endif
    endif
EndFunction

;/
  Gets or creates the JFormMap for all cells.
/;
int Function GetCells() global
    return TTM_JUtils._GetOrCreateJFormMap(GetAssets(), "cells")
EndFunction

;/
  Gets or creates the JMap for a specific cell, scanning for objects if new.
  @param currentCell The cell
  @return           The JMap for the cell
/;
int Function GetCell(Cell currentCell) global
    TTM_Debug.trace("GetCell")
    int jCells = GetCells()
    int jCell = JFormMap_getObj(jCells, currentCell)

    if(jCell == 0)
        TTM_Debug.trace("GetCell:CreateNew")
        jCell = JMap_object()
        int jOwners = JMap_object()
        JMap_setStr(jCell, "name", currentCell.GetName())
        JMap_setObj(jCell, "ownersActors", JFormMap_object())
        JMap_setObj(jCell, "spouses", JFormMap_object())

        JFormMap_setObj(jCells, currentCell, jCell)

        ScanCell(currentCell)
    endif

    return jCell
EndFunction

;/
  Gets the JFormMap of owner actors for a cell.
  @param currentCell The cell
  @return           The JFormMap of owner actors
/;
int Function GetOwnersActors(Cell currentCell) global
    return JMap_getObj(GetCell(currentCell), "ownersActors")
EndFunction

;/
  Gets the JFormMap of spouses for a cell.
  @param currentCell The cell
  @return           The JFormMap of spouses
/;
int Function GetCellSpouses(Cell currentCell) global
    return JMap_getObj(GetCell(currentCell), "spouses")
EndFunction

;/
  Adds a spouse to a cell's spouse list and records the cell for the spouse.
  @param currentCell The cell
  @param spouse      The spouse actor
/;
Function AddCellSpouse(Cell currentCell, Actor spouse) global
    JFormMap_setStr(GetCellSpouses(currentCell), spouse, "true")
    AddSpouseRecord(spouse, "cells", currentCell)
EndFunction

;/
  Removes a spouse from a cell's spouse list.
  @param currentCell The cell
  @param spouse      The spouse actor
/;
Function RemoveCellSpouse(Cell currentCell, Actor spouse) global
    JFormMap_removeKey(GetCellSpouses(currentCell), spouse)
EndFunction

;/
  Checks if a spouse is recorded for a cell.
  @param currentCell The cell
  @param spouse      The spouse actor
  @return           True if spouse is recorded for the cell
/;
bool Function HasCellSpouse(Cell currentCell, Actor spouse) global
    return JMap_hasKey(GetCellSpouses(currentCell), spouse)
EndFunction

;/
  Adds an object to the list of owner objects for a cell.
  @param currentCell The cell
  @param obj        The object to add
/;
Function AddOwnersObject(Cell currentCell, ObjectReference obj) global
    ActorBase actorOwner = obj.GetActorOwner()

    TTM_Debug.trace("AddOwnersObject:"+actorOwner)
    
    if(actorOwner)
        int jActorObjects = GetActorObjects(currentCell, actorOwner)
        JArray_addForm(jActorObjects, obj)
    endif
EndFunction

;/
  Gets or creates the JArray of objects owned by an actor in a cell.
  @param currentCell The cell
  @param spouseAB    The actor base
  @return           The JArray of objects
/;
int Function GetActorObjects(Cell currentCell, ActorBase spouseAB) global
    int jActorsOwners = GetOwnersActors(currentCell)
    int jActorObjects = JFormMap_getObj(jActorsOwners, spouseAB)
    if(jActorObjects == 0)
        jActorObjects = JArray_object()
        JFormMap_setObj(jActorsOwners, spouseAB, jActorObjects)
    endif

    return jActorObjects
EndFunction

;/
  Scans a cell for objects and records them for ownership tracking.
  @param currentCell The cell to scan
/;
Function ScanCell(Cell currentCell) global
    TTM_Debug.trace("ScanCell")
    int jCell = GetCell(currentCell)
    ; this cell was already scanned
    if(JMap_hasKey(GetCell(currentCell), "isScanned"))
        TTM_Debug.trace("ScanCell:scanned")
        return
    endif
    int numFurns = currentCell.GetNumRefs(40)

    int i = 0

    TTM_Debug.trace("ScanCell:objectsCount:"+numFurns)

    while(i < numFurns)
        ObjectReference obj = currentCell.GetNthRef(i, 40)
        AddOwnersObject(currentCell, obj)
        i += 1
    endwhile

    JMap_setStr(GetCell(currentCell), "isScanned", "true")
EndFunction

;/
  Gets or creates the JMap of original owners for all objects.
/;
int Function GetOriginalOwnersObjects() global
    return TTM_JUtils._GetOrCreateJMap(GetAssets(), "originalOwnersObjects")
EndFunction

;/
  Sets the original owner for an object.
  @param obj      The object
  @param spouseAB The actor base of the spouse
/;
int Function SetOriginalOwnerObject(ObjectReference obj, ActorBase spouseAB) global
    int jCont = GetOriginalOwnersObjects()
    JFormMap_setForm(JCont, obj, spouseAB)
EndFunction

;/
  Gets the original owner for an object.
  @param obj The object
  @return    The original owner form
/;
Form Function GetOriginalOwnerObject(Form obj) global
    int jCont = GetOriginalOwnersObjects()
    return JFormMap_getForm(jCont, obj)
EndFunction

;/
  Gets or creates the JFormMap of all spouses.
/;
int Function GetSpouses() global
    return TTM_JUtils._GetOrCreateJFormMap(GetAssets(), "spouses")
EndFunction

;/
  Gets or creates the JMap for a specific spouse, initializing arrays for their assets.
  @param spouse The spouse actor
  @return      The JMap for the spouse
/;
int Function GetSpouse(Actor spouse) global
    int jSpouses = GetSpouses()
    int jSpouse = JFormMap_getObj(jSpouses, spouse)
    if(jSpouse == 0)
        jSpouse = JMap_object()
        JMap_setObj(jSpouse, "locations", JArray_object())
        JMap_setObj(jSpouse, "cells", JArray_object())
        JMap_setObj(jSpouse, "objects", JArray_object())
        JMap_setObj(jSpouse, "keys", JArray_object())
        JFormMap_setObj(jSpouses, spouse, jSpouse)
    endif

    return jSpouse
EndFunction

;/
  Gets the JArray for a specific asset type for a spouse.
  @param spouse  The spouse actor
  @param keyName The asset type key ("locations", "cells", "objects", "keys")
  @return       The JArray for the asset type
/;
int Function GetSpouseArray(Actor spouse, string keyName) global
    return JMap_getObj(GetSpouse(spouse), keyName)
EndFunction

;/
  Adds a record (form) to a spouse's asset array.
  @param spouse The spouse actor
  @param type   The asset type
  @param obj    The form to add
/;
Function AddSpouseRecord(Actor spouse, string type, Form obj) global
    int JSpouseRecords = GetSpouseArray(spouse, type)
    if(JArray_findForm(JSpouseRecords, obj) == -1)
        JArray_addForm(JSpouseRecords, obj)
    endif
EndFunction

;/
  Gets or creates the JFormMap of all keys.
/;
int Function GetKeys() global
    return TTM_JUtils._GetOrCreateJFormMap(GetAssets(), "keys")
EndFunction

;/
  Adds a key to a spouse's asset list and records it in the keys map.
  @param doorKey The key to add
  @param spouse  The spouse actor
/;
Function AddKey(Key doorKey, Actor spouse) global
    int jKeys = GetKeys()
    int jKey = JFormMap_getObj(jKeys, doorKey)
    if(jKey == 0)
        jKey = JFormMap_object()
        JFormMap_setObj(jKeys, doorKey, jKey)
    endif
    JFormMap_setStr(jKey, spouse, "true")
    AddSpouseRecord(spouse, "keys", doorKey)
EndFunction

;/
  Checks if a key is present in the keys map.
  @param doorKey The key to check
  @return       True if the key is present
/;
bool Function HasKey(Key doorKey) global
    return JFormMap_hasKey(GetKeys(), doorKey)
EndFunction

;/
  Removes a key from a spouse's asset list and from the keys map if no spouses remain.
  @param doorKey The key to remove
  @param spouse  The spouse actor
/;
Function RemoveKeySpouse(Key doorKey, Actor spouse) global
    int jKeys = GetKeys()
    int jKey = JFormMap_getObj(jKeys, doorKey)
    if(jKey == 0)
        return
    endif

    if(JFormMap_hasKey(jKey, spouse))
        JFormMap_removeKey(jKey, spouse)
        if(JFormMap_count(jKey) == 0)
            JFormMap_removeKey(jKeys, doorKey)
        endif
    endif
EndFunction