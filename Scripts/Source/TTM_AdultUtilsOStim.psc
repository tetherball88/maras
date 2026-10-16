scriptname TTM_AdultUtilsOStim
{OStim-only implementations. Only call through TTM_AdultUtils after verifying OStim is installed.}

int Function AddParticipant(Actor participant, Actor newActor) global
    int ThreadID = OActor.GetSceneID(participant)
    if(ThreadID == -1)
        TTM_Debug.debug("ThreadID is -1 for actor " + participant)
        return -1
    endif
    return OStimHotSwap.AddActorToThread(ThreadID, newActor)
EndFunction

bool Function HasBedsNearby(Actor npc1) global
    return OFurniture.FindFurnitureOfType("bed", npc1, 1500, 1500) != none
EndFunction

int Function SetupQuickScene(Actor npc1, Actor npc2, bool stall, bool furnitureOptional = false) global
    ObjectReference bed = OFurniture.FindFurnitureOfType("bed", npc1, 1500, 1500)
    if(!furnitureOptional && bed == none)
        TTM_Debug.debug("TTM_AdultUtils:SetupQuickScene:No bed found for " + npc1)
        return -1
    endif
    Actor[] actors = OActorUtil.Sort(OActorUtil.ToArray(npc1, npc2), OActorUtil.EmptyArray())
    string sceneID = OLibrary.GetRandomSceneSuperloadCSV(actors, AnyActionType = "intercourse")
    TTM_Debug.debug("TTM_AdultUtils:SetupQuickScene:" + sceneID + ":" + bed + ":" + actors + ": stall:"+stall)
    int ThreadID = OThread.QuickStart(actors, sceneID, bed)
    if(stall)
        StorageUtil.SetIntValue(none, "TTM_IntimateScene_Thread"+ThreadID+"_OriginalAutoMode", OThread.IsInAutoMode(ThreadID) as int)
        OThread.StopAutoMode(ThreadID)
        Utility.wait(0.5)
        TTM_Debug.debug("TTM_AdultUtils:IsInAutoMode:" + OThread.IsInAutoMode(ThreadID))
        OThread.StallClimax(ThreadID)
    endif
    return ThreadID
EndFunction

Function EndThread(int ThreadID) global
    OThread.Stop(ThreadID)
EndFunction

Function EndThreadActor(Actor participant) global
    int ThreadID = OActor.GetSceneID(participant)
    TTM_Debug.debug("TTM_AdultUtils:EndThreadActor:" + participant + ":" + ThreadID)
    if(ThreadID == -1)
        return
    endif
    OThread.Stop(ThreadID)
    StorageUtil.UnsetIntValue(none, "TTM_IntimateScene_Thread"+ThreadID+"_OriginalAutoMode")
EndFunction

Function RestoreOriginalAutoMode(int ThreadID) global
    int originalAutoMode = StorageUtil.GetIntValue(none, "TTM_IntimateScene_Thread"+ThreadID+"_OriginalAutoMode")
    if(originalAutoMode == 0)
        OThread.StopAutoMode(ThreadID)
    else
        OThread.StartAutoMode(ThreadID)
    endif
    OThread.PermitClimax(ThreadID)
EndFunction

