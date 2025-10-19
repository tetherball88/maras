;/
  TTM_ServiceSpouseAssets.psc

  Manages marital assets for the MARAS mod, including homes, cells, objects, and keys shared with the player through marriage.

  Main responsibilities:
    - Track and manage locations, cells, and objects associated with each spouse
    - Handle sharing and restoring of object ownership
    - Manage keys and access for the player and spouses
    - Integrate with JContainers for persistent storage
/;
scriptname TTM_ServiceSpouseAssets

Function StartShareHomeWithPlayer(Actor spouse) global
    spouse.AddToFaction(TTM_JData.GetCheckSpouseHomeFaction())
    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("StartShareHomeWithPlayer:"+TTM_JData.GetMarasCheckSpouseHomeQuest())
    endif
    TTM_JData.GetMarasCheckSpouseHomeQuest().SetStage(0)
EndFunction

Function CheckCell(Actor spouse = none) global
    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("CheckCell")
    endif
    Actor player = TTM_JData.GetPlayer()
    Cell currentCell = player.GetParentCell()

    if(!currentCell.IsInterior())
        if(TTM_Debug.IsTrace())
            TTM_Debug.trace("CheckCell:NotInterior")
        endif
        return
    endif

    Location currentLoc = player.GetCurrentLocation()
    if(!IsLocationRegistered(currentLoc))
        if(TTM_Debug.IsTrace())
            TTM_Debug.trace("CheckCell:Not registered spouse location")
        endif
        return
    endif

    if(IsCellOwnedBySpouse(currentCell))
        if(TTM_Debug.IsTrace())
            TTM_Debug.trace("CheckCell:AlreadyOwnedBySpouse")
        endif
        currentCell.SetPublic()
    endif

    Quest checkSpouseHomeQst = TTM_JData.GetMarasCheckSpouseHomeQuest()
    if(checkSpouseHomeQst.IsRunning())
        LocationAlias spouseLocAl = checkSpouseHomeQst.GetAliasByName("spouseHome") as LocationAlias
        Location spouseLoc = spouseLocAl.GetLocation()

        if(spouseLoc != currentLoc)
            if(TTM_Debug.IsTrace())
                TTM_Debug.trace("CheckCell:NotSpouseLocation")
            endif
            return
        endif

        if(!spouse)
            spouse = TTM_Utils.GetActorAlias(checkSpouseHomeQst, "spouse")
            if(TTM_Debug.IsTrace())
                TTM_Debug.trace("CheckCell:SetSpouseFromQuest")
            endif
        endif
    endif

    if(!spouse)
        if(TTM_Debug.IsTrace())
            TTM_Debug.trace("CheckCell:NoSpouseProvided")
        endif
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
        if(TTM_Debug.IsTrace())
            TTM_Debug.trace("CheckCell:SpouseOwnsThisHome")
        endif
        AddCell(currentCell, spouse)
        Form[] objects = GetCellObjectsWithOwners(currentCell)
        int i = 0

        ActorBase spouseAB = spouse.GetActorBase()

        while(i < objects.Length)
            ObjectReference obj = objects[i] as ObjectReference
            if(spouseAB == obj.GetActorOwner())
                ShareObjectWithPlayer(spouse, obj)
            endif
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
    Form[] objects = GetSpouseObjs(spouse)
    int i = 0
    while(i < objects.Length)
        RestoreObjectOwnership(objects[i] as ObjectReference)
        i += 1
    endwhile

    Form[] cells = GetSpouseCells(spouse)
    i = 0
    while(i < cells.Length)
        RemoveCellSpouse(cells[i] as Cell, spouse)
        i += 1
    endwhile

    Form[] locations = GetSpouseLocations(spouse)
    i = 0
    while(i < locations.Length)
        RemoveSpouseFromLocation(locations[i] as Location, spouse)
        i += 1
    endwhile

    Form[] keys = GetSpouseKeys(spouse)
    Actor player = TTM_JData.GetPlayer()
    i = 0
    while(i < keys.Length)
        Key doorKey = keys[i] as Key
        RemoveSpouseKey(doorKey, spouse)
        int keyCount = player.GetItemCount(doorKey)
        if(keyCount > 0)
            player.RemoveItem(doorKey, keyCount)
        endif

        i += 1
    endwhile
EndFunction


Location Function GetSpouseOriginalHome(Actor spouse) global
    Location loc = spouse.GetEditorLocation()

    if(loc.HasKeyword(TTM_JData.GetLocTypeDwellingKeyword()) || loc.HasKeyword(TTM_JData.GetLocTypeHouseKeyword()))
        return loc
    endif

    return none
EndFunction

Function FindSpouseHome(Actor spouse) global
    Actor player = TTM_JData.GetPlayer()
    Location spouseHome = GetSpouseOriginalHome(spouse)
    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("FindSpouseHome:"+spouseHome)
    endif
    if(!spouseHome)
        return
    endif

    AddLocation(spouseHome, spouse)
EndFunction

Function ShareObjectWithPlayer(Actor spouse, ObjectReference obj) global
    Faction playerFaction = TTM_JData.GetPlayerFaction()
    ActorBase actorOwner = obj.GetActorOwner()

    if(!actorOwner || actorOwner != spouse.GetActorBase())
        return
    endif

    AddSpouseObj(spouse, obj)

    obj.SetActorOwner(none)
    obj.SetFactionOwner(playerFaction)
EndFunction

Function RestoreObjectOwnership(ObjectReference obj) global
    ActorBase actorOwnership = GetOriginalOwnerObject(obj) as ActorBase

    if(actorOwnership)
        obj.SetFactionOwner(none)
        obj.SetActorOwner(actorOwnership)
    endif
EndFunction

Function ScanCell(Cell currentCell) global
    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("ScanCell")
    endif
    if(TTM_JMethods.GetIntValue(currentCell, "CellScanned") == 1)
        if(TTM_Debug.IsTrace())
            TTM_Debug.trace("ScanCell:AlreadyScanned:SKIP")
        endif
        return
    endif
    int numFurns = currentCell.GetNumRefs(40)

    int i = 0

    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("ScanCell:objectsCount:"+numFurns)
    endif

    while(i < numFurns)
        ObjectReference obj = currentCell.GetNthRef(i, 40)
        AddObjectsWithOwners(currentCell, obj)
        i += 1
    endwhile

    TTM_JMethods.SetIntValue(currentCell, "CellScanned", 1)
EndFunction

Form[] Function GetSpouseArray(Actor spouse, string keyName) global
    return TTM_JMethods.FormListToArray(spouse, "Spouse" + keyName)
EndFunction

Function AddSpouseItem(Actor spouse, string type, Form obj) global
    TTM_JMethods.FormListAdd(spouse, "Spouse" + type, obj)
EndFunction

Function RemoveSpouseItem(Actor spouse, string type, Form obj) global
    TTM_JMethods.FormListRemove(spouse, "Spouse" + type, obj)
EndFunction

Function AddLocation(Location loc, Actor spouse) global
    TTM_JMethods.SetIntValue(loc, "Registered", 1)
    TTM_JMethods.FormListAdd(loc, "LocationSpousesCache", spouse)
    AddSpouseLocation(spouse, loc)
EndFunction

Form[] Function GetLocationSpouses(Location loc) global
    return TTM_JMethods.FormListToArray(loc, "LocationSpousesCache")
EndFunction

Function RemoveSpouseFromLocation(Location loc, Actor spouse) global
    TTM_JMethods.FormListRemove(loc, "LocationSpousesCache", spouse)

    if(GetLocationSpouses(loc).Length == 0)
        TTM_JMethods.SetIntValue(loc, "Registered", 0)
    endif
EndFunction

bool Function IsLocationRegistered(Location loc) global
    return TTM_JMethods.GetIntValue(loc, "Registered") == 1
EndFunction

Form[] Function GetCellSpouseOwners(Cell currentCell) global
    return TTM_JMethods.FormListToArray(currentCell, "CellSpouseOwnersCache")
EndFunction

Function AddCell(Cell currentCell, Actor spouse) global
    TTM_JMethods.FormListAdd(currentCell, "CellSpouseOwnersCache", spouse)
    AddSpouseCell(spouse, currentCell)
    ScanCell(currentCell)
EndFunction

Function RemoveCellSpouse(Cell currentCell, Actor spouse) global
    TTM_JMethods.FormListRemove(currentCell, "CellSpouseOwnersCache", spouse)
    RemoveSpouseCell(spouse, currentCell)
EndFunction

bool Function IsCellOwnedBySpouse(Cell currentCell) global
    return TTM_JMethods.FormListToArray(currentCell, "CellSpouseOwnersCache").Length > 0
EndFunction

Function AddObjectsWithOwners(Cell currentCell, ObjectReference obj) global
    ActorBase actorOwner = obj.GetActorOwner()

    if(actorOwner)
        TTM_JMethods.SetFormValue(obj, "ObjectOwnerActorBase", actorOwner)
        TTM_JMethods.FormListAdd(currentCell, "CellObjectsWithOwnersCache", obj)
    endif
EndFunction

Form[] Function GetCellObjectsWithOwners(Cell currentCell) global
    return TTM_JMethods.FormListToArray(currentCell, "CellObjectsWithOwnersCache")
EndFunction

Form Function GetOriginalOwnerObject(Form obj) global
    return TTM_JMethods.GetFormValue(obj, "ObjectOwnerActorBase")
EndFunction

Function AddSpouseKey(Key doorKey, Actor spouse) global
    AddSpouseItem(spouse, "keys", doorKey)
EndFunction

Function RemoveSpouseKey(Key doorKey, Actor spouse) global
    RemoveSpouseItem(spouse, "keys", doorKey)
EndFunction

Form[] Function GetSpouseKeys(Actor spouse) global
    return GetSpouseArray(spouse, "keys")
EndFunction

Function AddSpouseObj(Actor spouse, ObjectReference obj) global
    AddSpouseItem(spouse, "objects", obj)
EndFunction

Function RemoveSpouseObj(Actor spouse, ObjectReference obj) global
    RemoveSpouseItem(spouse, "objects", obj)
EndFunction

Form[] Function GetSpouseObjs(Actor spouse) global
    return GetSpouseArray(spouse, "objects")
EndFunction

Function AddSpouseCell(Actor spouse, Cell currentCell) global
    AddSpouseItem(spouse, "cells", currentCell)
EndFunction

Function RemoveSpouseCell(Actor spouse, Cell currentCell) global
    RemoveSpouseItem(spouse, "cells", currentCell)
EndFunction

Form[] Function GetSpouseCells(Actor spouse) global
    return GetSpouseArray(spouse, "cells")
EndFunction

Function AddSpouseLocation(Actor spouse, Location loc) global
    AddSpouseItem(spouse, "locations", loc)
EndFunction

Function RemoveSpouseLocation(Actor spouse, Location loc) global
    RemoveSpouseItem(spouse, "locations", loc)
EndFunction

Form[] Function GetSpouseLocations(Actor spouse) global
    return GetSpouseArray(spouse, "locations")
EndFunction
