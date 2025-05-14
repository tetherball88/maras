;/
  TTM_JUtils.psc

  Utility functions for working with JContainers objects in the MARAS mod.

  Main responsibilities:
    - Get or create JMap, JArray, and JFormMap objects by key
    - Helpers for checking spouse ownership of objects and cells
    - Faction cleanup utilities

  Dependencies:
    - TTM_JCDomain
    - TTM_JData
    - TTM_ServiceNpcs
    - TTM_Utils
    - JContainers (JMap, JArray, JFormMap, etc)
;/;
scriptname TTM_JUtils

import TTM_JCDomain

;/
  Helper to get a JMap object, creating it if it doesn't exist.
  @param parentMap The parent JMap object
  @param k         The key to look up or create
  @return          The JMap object
/;
int Function _GetOrCreateJMap(int parentMap, string k) global
    int res = JMap_getObj(parentMap, k)
    if (!res)
        res = JMap_object()
        JMap_setObj(parentMap, k, res)
    endif
    return res
EndFunction

;/
  Helper to get a JArray object, creating it if it doesn't exist.
  @param parentMap The parent JMap object
  @param k         The key to look up or create
  @return          The JArray object
/;
int Function _GetOrCreateJArray(int parentMap, string k) global
    int res = JMap_getObj(parentMap, k)
    if (!res)
        res = JArray_object()
        JMap_setObj(parentMap, k, res)
    endif
    return res
EndFunction

;/
  Helper to get a JFormMap object, creating it if it doesn't exist.
  @param parentMap The parent JMap object
  @param k         The key to look up or create
  @return          The JFormMap object
/;
int Function _GetOrCreateJFormMap(int parentMap, string k) global
    int res = JMap_getObj(parentMap, k)
    if (!res)
        res = JFormMap_object()
        JMap_setObj(parentMap, k, res)
    endif
    return res
EndFunction

;/
  Checks if a spouse owns a given object (by ActorBase or Faction).
  @param obj    The object reference to check
  @param spouse The spouse actor
  @return       True if spouse owns the object
/;
bool Function SpouseOwnsObject(ObjectReference obj, Actor spouse) global
    ActorBase ownerAB = obj.GetActorOwner()
    Faction ownerF = obj.GetFactionOwner()

    if(ownerAB && spouse.GetActorBase() == ownerAB)
        return true
    endif

    if(ownerF && spouse.IsInFaction(ownerF))
        return true
    endif

    return false
EndFunction

;/
  Checks if a spouse owns a given cell (delegates to TTM_Utils).
  @param currentCell The cell to check
  @param spouse      The spouse actor
  @return           True if spouse owns the cell
/;
bool Function GetSpouseOwnsCell(Cell currentCell, Actor spouse) global
    return TTM_Utils.SpouseOwnsCell(spouse, currentCell)
EndFunction

;/
  Removes the player from a faction if no tracked spouse is in it.
  @param fact The faction to check and potentially remove
/;
Function CheckFactionsToRemove(Faction fact) global
    Actor player = TTM_JData.GetPlayer()
    int jSpouses = TTM_ServiceNpcs.GetSpouses()
    Actor spouse = JFormMap_nextKey(jSpouses) as Actor

    while(spouse)
        if(spouse.IsInFaction(fact))
            return
        endif
        spouse = JFormMap_nextKey(jSpouses, spouse) as Actor
    endwhile

    player.RemoveFromFaction(fact)
EndFunction

int Function LoadMultipleFiles(string folderPath, bool isFormMap = false) global
    int JTarget
    if(isFormMap)
        JTarget = JFormMap_object()
    else
        JTarget = JMap_object()
    endif
    int JFilesMap = JValue_readFromDirectory(folderPath)
    string nextKey = JMap_nextKey(JFilesMap)
    while(nextKey)
      if(isFormMap)
          JFormMap_addPairs(JTarget, JMap_getObj(JFilesMap, nextKey), true)
      else
          JMap_addPairs(JTarget, JMap_getObj(JFilesMap, nextKey), true)
      endif
          nextKey = JMap_nextKey(JFilesMap, nextKey)
    endwhile
    JValue_release(JFilesMap)
    return JTarget
EndFunction
