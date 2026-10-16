scriptname TTM_ServiceIntimateMoments extends Quest

; This script is for integrations with animation frameworks

Function Maintenance()
    if(Game.GetModByName("TT_LoversLedger.esp") != 255)
        TTM_Data.SetHasTTLL()
    endif

    ; OStim intimate thread events for cheating detection
    RegisterForModEvent("ostim_thread_start", "OnOStimStart")
    RegisterForModEvent("ostim_thread_scenechanged", "OnOStimSceneChange")
    RegisterForModEvent("ostim_thread_end", "OnOStimEnd")
    TTM_Debug.info("TTM_ServiceIntimateMoments: Maintenance: registered for OStim thread events")

    ; SexLab intimate thread events for cheating detection
    RegisterForModEvent("HookAnimationStart", "OnSexLabStart")
    RegisterForModEvent("HookStageStart", "OnSexLabStageStart")
    RegisterForModEvent("HookAnimationEnd", "OnSexLabEnd")
    TTM_Debug.info("TTM_ServiceIntimateMoments: Maintenance: registered for SexLab thread events")

    RegisterForModEvent("ttll_thread_data_event", "ThreadFinished")

    ; C++ poll asks us to confirm tracked threads are still running (covers missed end events)
    RegisterForModEvent("maras_verify_intimate_thread", "OnVerifyIntimateThread")

    ; placeholder for any maintenance tasks on game load on main quest init
    ; register for events if needed
EndFunction

Event OnOStimStart(string eventName, string strArg, float numArg, Form sender)
    int threadID = numArg as int
    Actor[] actors = OThread.GetActors(threadID)
    TTM_Debug.info("TTM_ServiceIntimateMoments: OnOStimStart: thread=" + threadID + " actors=" + actors.Length)
    bool hasCheaters = MARAS.RegisterIntimateThread("OStim", threadID, actors)
    TTM_Debug.info("TTM_ServiceIntimateMoments: OnOStimStart: registered thread=" + threadID + " hasCheaters=" + hasCheaters)
    Actor player = TTM_Data.GetPlayer()
    Faction cheatingNpcCaughtFaction = TTM_Data.GetCheatingNpcCaughtFaction()
    if(hasCheaters)
        int i = 0
        while(i < actors.Length)
            Actor participant = actors[i]
            if((MARAS.IsNPCStatus(participant, "dating") || MARAS.IsNPCStatus(participant, "engaged") || MARAS.IsNPCStatus(participant, "married")) && MARAS.IsNPCCheating(participant) && MARAS.HasCheatedOn(participant, player))
                if(cheatingNpcCaughtFaction)
                    ; add faction for dialogues
                    participant.AddToFaction(cheatingNpcCaughtFaction)
                    StorageUtil.FormListAdd(none, "TTM_CheatersFormList_" + threadID, participant)
                endif
            endif
            i += 1
        endwhile
    endif
    StorageUtil.SetIntValue(none, "MARAS_OStim_Track_" + threadID, hasCheaters as int)
EndEvent

Event OnOStimSceneChange(string eventName, string strArg, float numArg, Form sender)
    int threadID = numArg as int
    TTM_Debug.debug("TTM_ServiceIntimateMoments: OnOStimSceneChange: thread=" + threadID + " tracked=" + StorageUtil.GetIntValue(none, "MARAS_OStim_Track_" + threadID, 0))
    if (StorageUtil.GetIntValue(none, "MARAS_OStim_Track_" + threadID, 0) == 1)
        string sceneId = OThread.GetScene(threadID)
        int[] actions = OMetadata.FindActionsSuperloadCSVv2(sceneId, AnyActionTag = "sexual")
        int j = 0
        while j < actions.Length
            int actionIndex = actions[j]
            Actor osActor = OThread.GetActor(threadID, OMetadata.GetActionActor(sceneId, actionIndex))
            Actor osTarget = OThread.GetActor(threadID, OMetadata.GetActionTarget(sceneId, actionIndex))
            if (osActor && osTarget && osActor != osTarget)
                MARAS.ReportIntimateInteraction("OStim", threadID, osActor, osTarget)
            endif
            j += 1
        endwhile
    endif
EndEvent

Event OnOStimEnd(string eventName, string strArg, float numArg, Form sender)
    int threadID = numArg as int
    TTM_Debug.info("TTM_ServiceIntimateMoments: OnOStimEnd: thread=" + threadID)
    CleanupOStimThread(threadID)
EndEvent

Function CleanupOStimThread(int threadID)
    MARAS.ClearIntimateThread("OStim", threadID)
    StorageUtil.UnsetIntValue(none, "MARAS_OStim_Track_" + threadID)
    Faction cheatingNpcCaughtFaction = TTM_Data.GetCheatingNpcCaughtFaction()
    Form[] cheaters = StorageUtil.FormListToArray(none, "TTM_CheatersFormList_" + threadID)
    int i = 0
    while(i < cheaters.Length)
        Actor cheater = cheaters[i] as Actor
        if(cheater && cheatingNpcCaughtFaction)
            ; clear faction for dialogues
            cheater.RemoveFromFaction(cheatingNpcCaughtFaction)
        endif
        i += 1
    endwhile
    StorageUtil.ClearObjFormListPrefix(none, "TTM_CheatersFormList_" + threadID)
EndFunction

; Sent by the C++ poll for every tracked thread. Safety net for framework end events that never arrived.
Event OnVerifyIntimateThread(string eventName, string framework, float numArg, Form sender)
    int threadID = numArg as int
    if(framework == "OStim")
        if(!OThread.IsRunning(threadID))
            TTM_Debug.warn("TTM_ServiceIntimateMoments: OStim thread " + threadID + " is no longer running but end event was missed, cleaning up")
            CleanupOStimThread(threadID)
        endif
    elseif(framework == "SexLab")
        if(TTM_AdultUtilsSexLab.GetThreadActors(threadID).Length == 0)
            TTM_Debug.warn("TTM_ServiceIntimateMoments: SexLab thread " + threadID + " is no longer running but end event was missed, cleaning up")
            CleanupSexLabThread(threadID)
        endif
    endif
EndEvent


Event OnSexLabStart(string eventName, string strArg, float numArg, Form sender)
    int threadID = numArg as int
    Actor[] actors = TTM_AdultUtilsSexLab.GetThreadActors(threadID)
    if(actors.Length == 0)
        TTM_Debug.warn("TTM_ServiceIntimateMoments: OnSexLabStart: thread=" + threadID + " not found, skipping")
        return
    endif
    TTM_Debug.info("TTM_ServiceIntimateMoments: OnSexLabStart: thread=" + threadID + " actors=" + actors.Length)
    bool hasCheaters = MARAS.RegisterIntimateThread("SexLab", threadID, actors)
    TTM_Debug.info("TTM_ServiceIntimateMoments: OnSexLabStart: registered thread=" + threadID + " hasCheaters=" + hasCheaters)
    Actor player = TTM_Data.GetPlayer()
    Faction cheatingNpcCaughtFaction = TTM_Data.GetCheatingNpcCaughtFaction()
    if(hasCheaters)
        int i = 0
        while(i < actors.Length)
            Actor participant = actors[i]
            if((MARAS.IsNPCStatus(participant, "dating") || MARAS.IsNPCStatus(participant, "engaged") || MARAS.IsNPCStatus(participant, "married")) && MARAS.IsNPCCheating(participant) && MARAS.HasCheatedOn(participant, player))
                if(cheatingNpcCaughtFaction)
                    ; add faction for dialogues
                    participant.AddToFaction(cheatingNpcCaughtFaction)
                    StorageUtil.FormListAdd(none, "TTM_CheatersFormList_SL_" + threadID, participant)
                endif
            endif
            i += 1
        endwhile
    endif
    StorageUtil.SetIntValue(none, "MARAS_SexLab_Track_" + threadID, hasCheaters as int)
EndEvent

Event OnSexLabStageStart(string eventName, string strArg, float numArg, Form sender)
    int threadID = numArg as int
    TTM_Debug.debug("TTM_ServiceIntimateMoments: OnSexLabStageStart: thread=" + threadID + " tracked=" + StorageUtil.GetIntValue(none, "MARAS_SexLab_Track_" + threadID, 0))
    if (StorageUtil.GetIntValue(none, "MARAS_SexLab_Track_" + threadID, 0) != 1)
        return
    endif
    TTM_AdultUtilsSexLab.ReportStageInteractions(threadID)
EndEvent

Event OnSexLabEnd(string eventName, string strArg, float numArg, Form sender)
    int threadID = numArg as int
    TTM_Debug.info("TTM_ServiceIntimateMoments: OnSexLabEnd: thread=" + threadID)
    CleanupSexLabThread(threadID)
EndEvent

Function CleanupSexLabThread(int threadID)
    MARAS.ClearIntimateThread("SexLab", threadID)
    StorageUtil.UnsetIntValue(none, "MARAS_SexLab_Track_" + threadID)
    Faction cheatingNpcCaughtFaction = TTM_Data.GetCheatingNpcCaughtFaction()
    Form[] cheaters = StorageUtil.FormListToArray(none, "TTM_CheatersFormList_SL_" + threadID)
    int i = 0
    while(i < cheaters.Length)
        Actor cheater = cheaters[i] as Actor
        if(cheater && cheatingNpcCaughtFaction)
            ; clear faction for dialogues
            cheater.RemoveFromFaction(cheatingNpcCaughtFaction)
        endif
        i += 1
    endwhile
    StorageUtil.ClearObjFormListPrefix(none, "TTM_CheatersFormList_SL_" + threadID)
EndFunction

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

    Form[] topThreeLovers = TTLL_Store.GetAllLovers(npc, 3)
    int isPlayerTopLover = 0
    int i = 0
    while(i < topThreeLovers.Length)
        ActorBase topLoverBase = topThreeLovers[i] as ActorBase
        if(topLoverBase == TTM_Data.GetPlayer().GetActorBase())
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
            bool hadSex = TTLL_ThreadsCollector.GetThreadBool(ThreadID, "hadsex")
            if(!hadSex)
                affection = 8.0
            endif
            TTM_ServiceAffection.AddIntimacyAffection(akActor, affection)

            if(hadSex && (MARAS.IsNPCStatus(akActor, "candidate") || MARAS.IsNPCStatus(akActor, "dating")))
                ; MARAS.PromoteNPCToStatus(akActor, "lover")
            endif
        endif
        i += 1
    endwhile
EndEvent


