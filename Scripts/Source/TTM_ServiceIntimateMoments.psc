scriptname TTM_ServiceIntimateMoments extends Quest

; This script is for integrations with animation frameworks

Function Maintenance()
    if(Game.GetModByName("TT_LoversLedger.esp") != 255)
        TTM_JData.SetHasTTLL()
    endif

    ; patch for ostim removing PapyrusUtils override packages
    RegisterForModEvent("ostim_thread_end", "OStimEnd")

    RegisterForModEvent("ttll_thread_data_event", "ThreadFinished")

    ; placeholder for any maintenance tasks on game load on main quest init
    ; register for events if needed
EndFunction

Event OStimEnd(string eventName, string json, float numArg, Form sender)
    TTM_Debug.trace("TTM_ServiceIntimateMoments:OStimEnd:"+json)
    Actor[] Actors = OJSON.GetActors(Json)
    Faction housedFaction = TTM_JData.GetSpouseHousedFaction()
    Package spousePlayerHomeSandbox = TTM_JData.GetHomeSandboxPackage()

    int i = 0
    while(i < actors.Length)
        Actor akActor = actors[i]

        if(akActor.IsInFaction(housedFaction) && akActor.GetCurrentPackage() != spousePlayerHomeSandbox)
            ActorUtil.RemovePackageOverride(akActor, spousePlayerHomeSandbox)
            ActorUtil.AddPackageOverride(akActor, spousePlayerHomeSandbox, 5)
            akActor.EvaluatePackage()
        endif

        i += 1
    endwhile
EndEvent

float Function GetNpcPlayerIntimacy(Actor npc)
    if(TTM_JData.GetHasTTLL())
        return 0
    endif

    float res = 0

    Actor player = TTM_JData.GetPlayer()
    if(TTLL_Store.GetLoverExclusiveSexCount(npc, player) >= 1)
        res += 10
    elseif(TTLL_Store.GetLoverGroupSexCount(npc, player) >= 1)
        res += 6
    endif

    if(TTLL_Store.IsNpcTopThreeLover(npc, TTM_JData.GetPlayer()))
        res += 6
    endif

    return res
EndFunction

Event ThreadFinished(int ThreadID)
    Form[] actors = TTLL_OstimThreadsCollector.GetActorsForms(ThreadID)
    int i = 0
    while(i < actors.Length)
        Actor akActor = actors[i] as Actor
        if(TTM_Utils.IsTracking(akActor))
            float affection = 15.0
            if(!TTLL_OstimThreadsCollector.GetHadSex(ThreadID))
                affection = 8.0
            endif
            TTM_ServiceAffection.AddIntimacyAffection(akActor, affection)
        endif
        i += 1
    endwhile
EndEvent
