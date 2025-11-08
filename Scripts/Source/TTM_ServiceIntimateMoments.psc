scriptname TTM_ServiceIntimateMoments extends Quest

; This script is for integrations with animation frameworks

Function Maintenance()
    if(Game.GetModByName("TT_LoversLedger.esp") != 255)
        TTM_Data.SetHasTTLL()
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
    Faction housedFaction = TTM_Data.GetSpouseHousedFaction()
    Package spousePlayerHomeSandbox = TTM_Data.GetHomeSandboxPackage()

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
    if(TTM_Data.GetHasTTLL())
        return 0
    endif

    float res = 0

    Actor player = TTM_Data.GetPlayer()
    if(TTLL_Store.GetLoverInt(npc, player, "exclusivesex") >= 1)
        res += 10
    elseif(TTLL_Store.GetLoverInt(npc, player, "groupsex") >= 1)
        res += 6
    endif

    Actor[] topThreeLovers = TTLL_Store.GetAllLovers(npc, 3)
    int isPlayerTopLover = 0
    int i = 0
    while(i < topThreeLovers.Length)
        if(topThreeLovers[i] == TTM_Data.GetPlayer())
            isPlayerTopLover = i
        endif
        i += 1
    endwhile

    if(isPlayerTopLover)
        res += 2 * (3 - isPlayerTopLover)
    endif

    return res
EndFunction

Event ThreadFinished(int ThreadID)
    Actor[] actors = TTLL_ThreadsCollector.GetActors(ThreadID)
    int i = 0
    while(i < actors.Length)
        Actor akActor = actors[i]
        if(MARAS.IsNPCStatus(akActor, "any"))
            float affection = 15.0
            if(!TTLL_ThreadsCollector.GetThreadBool(ThreadID, "hadsex"))
                affection = 8.0
            endif
            TTM_ServiceAffection.AddIntimacyAffection(akActor, affection)
        endif
        i += 1
    endwhile
EndEvent
