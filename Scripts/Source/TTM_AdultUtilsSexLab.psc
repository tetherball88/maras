scriptname TTM_AdultUtilsSexLab
{SexLab-only implementations. Only call through TTM_AdultUtils after verifying SexLab is installed.}

Function FinishPlayerThread() global
    SexLabThread playerThread = SexLabUtil.GetAPI().GetThreadByActor(TTM_Data.GetPlayer())
    if(playerThread)
        EndThread(playerThread.GetThreadID())
    endif
EndFunction

int Function AddParticipant(Actor participant, Actor newActor) global
    ; SexLab has no hot swap, so restart the scene with the extra participant
    SexLabFramework sexLab = SexLabUtil.GetAPI()
    SexLabThread oldThread = sexLab.GetThreadByActor(participant)
    if(!oldThread)
        TTM_Debug.debug("No SexLab thread for actor " + participant)
        return -1
    endif
    Actor[] oldPositions = oldThread.GetPositions()
    Actor[] newPositions = PapyrusUtil.PushActor(oldPositions, newActor)
    oldThread.StopAnimation()
    ; StopAnimation is asynchronous: wait until all old positions are released
    int waited = 0
    bool anyActive = true
    while(anyActive && waited < 20)
        Utility.Wait(0.25)
        anyActive = false
        int k = 0
        while(k < newPositions.Length)
            if(sexLab.IsActorActive(newPositions[k]) && newPositions[k] != newActor)
                anyActive = true
            endif
            k += 1
        endwhile
        waited += 1
    endwhile
    if(anyActive)
        TTM_Debug.debug("AddParticipant: SexLab thread did not stop in time")
        return -1
    endif
    SexLabThread newThread = sexLab.StartScene(newPositions, "")
    if(!newThread)
        return -1
    endif
    return newThread.GetThreadID()
EndFunction

bool Function HasBedsNearby(Actor npc1) global
    return SexLabUtil.GetAPI().FindBed(npc1, 1500.0) != none
EndFunction

int Function SetupQuickScene(Actor npc1, Actor npc2, bool stall, bool furnitureOptional = false) global
    SexLabFramework sexLab = SexLabUtil.GetAPI()
    ObjectReference slBed = sexLab.FindBed(npc1, 1500.0)
    if(!furnitureOptional && slBed == none)
        TTM_Debug.debug("TTM_AdultUtilsSexLab:SetupQuickScene:No bed found for " + npc1)
        return -1
    endif
    Actor[] slActors = SexLabUtil.MakeActorArray(npc1, npc2)
    TTM_Debug.debug("TTM_AdultUtilsSexLab:SetupQuickScene:SexLab:" + slBed + ":" + slActors + ": stall:"+stall)
    SexLabThread slThread = sexLab.StartScene(slActors, "", akCenter = slBed)
    if(!slThread)
        return -1
    endif
    if(stall)
        int slThreadID = slThread.GetThreadID()
        sslThreadController slController = sexLab.GetController(slThreadID)
        if(slController)
            StorageUtil.SetIntValue(none, "TTM_IntimateScene_Thread"+slThreadID+"_OriginalAutoMode", slController.AutoAdvance as int)
            slController.AutoAdvance = false
        endif
        slThread.DisableOrgasm(npc1, true)
        slThread.DisableOrgasm(npc2, true)
    endif
    return slThread.GetThreadID()
EndFunction

Function EndThread(int ThreadID) global
    SexLabThread slThread = SexLabUtil.GetAPI().GetThread(ThreadID)
    if(slThread)
        slThread.StopAnimation()
    endif
EndFunction

Function EndThreadActor(Actor participant) global
    SexLabThread slThread = SexLabUtil.GetAPI().GetThreadByActor(participant)
    if(slThread)
        slThread.StopAnimation()
    endif
EndFunction

Function RestoreOriginalAutoMode(int ThreadID) global
    SexLabThread slThread = SexLabUtil.GetAPI().GetThread(ThreadID)
    if(slThread)
        Actor[] positions = slThread.GetPositions()
        int i = 0
        while(i < positions.Length)
            slThread.DisableOrgasm(positions[i], false)
            i += 1
        endwhile
        sslThreadController slController = SexLabUtil.GetAPI().GetController(ThreadID)
        if(slController)
            slController.AutoAdvance = StorageUtil.GetIntValue(none, "TTM_IntimateScene_Thread"+ThreadID+"_OriginalAutoMode", 1) as bool
        endif
    endif
EndFunction


Actor[] Function GetThreadActors(int threadID) global
    SexLabThread slThread = SexLabUtil.GetAPI().GetThread(threadID)
    if(!slThread)
        return PapyrusUtil.ActorArray(0)
    endif
    return slThread.GetPositions()
EndFunction

Function ReportStageInteractions(int threadID) global
    SexLabThread slThread = SexLabUtil.GetAPI().GetThread(threadID)
    if(!slThread || !(slThread.IsSceneVaginal() || slThread.IsSceneAnal() || slThread.IsSceneOral()))
        return
    endif
    Actor[] actors = slThread.GetPositions()
    int i = 0
    while i < actors.Length
        int j = 0
        while j < actors.Length
            if(i != j && slThread.GetInteractionTypes(actors[i], actors[j]).Length > 0)
                MARAS.ReportIntimateInteraction("SexLab", threadID, actors[i], actors[j])
            endif
            j += 1
        endwhile
        i += 1
    endwhile
EndFunction
