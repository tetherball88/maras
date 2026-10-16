scriptname TTM_AdultUtils
{Framework-agnostic dispatcher. Must not reference any SexLab/OStim types directly - those live in TTM_AdultUtilsSexLab / TTM_AdultUtilsOStim.}

int Function GetEngine() global
    bool ostim = Game.GetModByName("OStim.esp") != 255
    bool sexlab = Game.GetModByName("SexLab.esm") != 255
    if(ostim && sexlab)
        return 0
    elseif(ostim)
        return 1
    elseif(sexlab)
        return 2
    endif

    return -1
EndFunction

Function FinishPlayerThread() global
    int engine = GetEngine()
    if(engine == 2)
        TTM_AdultUtilsSexLab.FinishPlayerThread()
    elseif(engine == 1)
        EndThread(0)
    endif
EndFunction

int Function AddParticipant(Actor participant, Actor newActor) global
    int engine = TTM_AdultUtils.GetEngine()
    if(engine == 2)
        return TTM_AdultUtilsSexLab.AddParticipant(participant, newActor)
    elseif(engine == 1)
        return TTM_AdultUtilsOStim.AddParticipant(participant, newActor)
    endif

    return -1
EndFunction

bool Function HasBedsNearby(Actor npc1) global
    int engine = GetEngine()
    if(engine == 2)
        return TTM_AdultUtilsSexLab.HasBedsNearby(npc1)
    elseif(engine >= 0)
        return TTM_AdultUtilsOStim.HasBedsNearby(npc1)
    endif
    return false
EndFunction

int Function SetupQuickScene(Actor npc1, Actor npc2, bool stall, bool furnitureOptional = false) global
    int engine = TTM_AdultUtils.GetEngine()
    if(engine == 2)
        return TTM_AdultUtilsSexLab.SetupQuickScene(npc1, npc2, stall, furnitureOptional)
    elseif(engine == 1)
        return TTM_AdultUtilsOStim.SetupQuickScene(npc1, npc2, stall, furnitureOptional)
    endif

    return -1
EndFunction

Function EndThread(int ThreadID) global
    int engine = TTM_AdultUtils.GetEngine()
    if(engine == 2)
        TTM_AdultUtilsSexLab.EndThread(ThreadID)
    elseif(engine == 1)
        TTM_AdultUtilsOStim.EndThread(ThreadID)
    endif

    StorageUtil.UnsetIntValue(none, "TTM_IntimateScene_Thread"+ThreadID+"_OriginalAutoMode")
EndFunction

Function EndThreadActor(Actor participant) global
    int engine = TTM_AdultUtils.GetEngine()
    if(engine == 2)
        TTM_AdultUtilsSexLab.EndThreadActor(participant)
    elseif(engine == 1)
        TTM_AdultUtilsOStim.EndThreadActor(participant)
    endif
EndFunction

Function RestoreOriginalAutoMode(int ThreadID) global
    int engine = GetEngine()
    if(engine == 2)
        TTM_AdultUtilsSexLab.RestoreOriginalAutoMode(ThreadID)
    elseif(engine >= 0)
        TTM_AdultUtilsOStim.RestoreOriginalAutoMode(ThreadID)
    endif
    StorageUtil.UnsetIntValue(none, "TTM_IntimateScene_Thread"+ThreadID+"_OriginalAutoMode")
EndFunction
