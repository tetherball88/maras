Scriptname TTM_NpcsScenes extends Quest conditional

; 5 dialogue variants in each scene type
Scene Property LoversScene auto
Scene Property DatingScene auto
Scene Property EngagedScene auto
Scene Property MarriedScene auto
Scene Property CheatingScene auto
Scene Property OpenRelationshipScene auto
Scene Property CheatingDiscoveredScene auto
Scene Property CheatingCaughtScene auto

int Property sceneIdx = -1 auto conditional

ReferenceAlias Property npc1Alias Auto
ReferenceAlias Property npc1PartnerAlias Auto
ReferenceAlias Property npc2Alias Auto
ReferenceAlias Property npc2PartnerAlias Auto

ReferenceAlias Property PlayersPartnerWalkInCheating Auto
ReferenceAlias Property PlayersPartnerWalkInOpenRelationship Auto
ReferenceAlias Property PlayersPartnerSuggestOpen Auto
ReferenceAlias Property PlayersPartnerSuggestClose Auto
ReferenceAlias Property PlayersPartnerConfessTalking Auto
ReferenceAlias Property PlayersPartnerConfessPhysical Auto

int Property sexThread = -1 Auto

Function SetupSceneOnLocationChange(Actor forcedNpc1 = none, Actor forcedNpc2 = none, string forcedScenario = "")
    Actor player = TTM_Data.GetPlayer()
    bool isForced = (forcedNpc1 != none && forcedNpc2 != none && forcedScenario != "")
    Actor npc1 = none
    Actor npc2 = none

    if(isForced)
        npc1 = forcedNpc1
        npc2 = forcedNpc2
    else
        if(!MARAS.IsLocationAllowedForScenes(player.GetCurrentLocation()))
            TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: Location not allowed for scenes by settings")
            return
        endif
        Actor[] couple = MARAS.FindBanterCoupleCandidates()
        if(couple.Length < 2)
            TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: Not enough participants: " + couple.Length)
            return
        endif
        npc1 = couple[0]
        npc2 = couple[1]
    endif

    sexThread = -1
    bool hasFramework = (TTM_AdultUtils.GetEngine() > -1)
    bool hasBeds = TTM_AdultUtils.HasBedsNearby(npc1)

    string planJson = MARAS.PrepareBanterScene(npc1, npc2, forcedScenario, hasFramework, hasBeds)
    if(!MARAS.GetJsonBool(planJson, "valid"))
        TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: PrepareBanterScene returned invalid")
        return
    endif

    int sceneKind = MARAS.GetJsonInt(planJson, "sceneKind")
    sceneIdx = MARAS.GetJsonInt(planJson, "sceneIdx", -1)
    npc1 = MARAS.GetJsonActor(planJson, "npc1")
    npc2 = MARAS.GetJsonActor(planJson, "npc2")
    Actor npc1MainPartner = MARAS.GetJsonActor(planJson, "npc1MainPartner")
    Actor npc2MainPartner = MARAS.GetJsonActor(planJson, "npc2MainPartner")
    bool shouldSetupSexScene = MARAS.GetJsonBool(planJson, "shouldSetupSexScene")
    bool shouldMutePartner = MARAS.GetJsonBool(planJson, "shouldMutePartner")
    Actor[] movedActors = MARAS.GetJsonActorArray(planJson, "movedActors")

    int m = 0
    while(m < movedActors.Length)
        Actor moved = movedActors[m]
        if(moved)
            PO3_SKSEFunctions.MoveToNearestNavmeshLocation(moved)
            moved.EvaluatePackage()
        endif
        m += 1
    endwhile

    Scene selectedScene = SceneKindToScene(sceneKind)
    TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: sceneKind=" + sceneKind + ", sceneIdx=" + sceneIdx + ", shouldSetupSexScene=" + shouldSetupSexScene)

    if(isForced && (forcedScenario == "cheating_physical_in_house" || forcedScenario == "cheating_physical_letter"))
        sexThread = TTM_AdultUtils.SetupQuickScene(npc1, npc2, false)
        if(sexThread != -1)
            MARAS.RecordScenePlayed(npc1, npc2)
            return
        endif
        TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: forced " + forcedScenario + " SetupQuickScene failed, falling back to CheatingScene")
        shouldSetupSexScene = false
        shouldMutePartner = false
    elseif(isForced && forcedScenario == "open_date_away")
        sexThread = TTM_AdultUtils.SetupQuickScene(npc1, npc2, false)
        if(sexThread != -1)
            MARAS.RecordScenePlayed(npc1, npc2)
            return
        endif
        TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: forced open_date_away SetupQuickScene failed, falling back to OpenRelationshipScene")
    endif

    MuteParticipants(npc1, npc2, npc1MainPartner, shouldMutePartner)

    if(shouldSetupSexScene)
        sexThread = TTM_AdultUtils.SetupQuickScene(npc1, npc2, true)
        ; if we couldn't start sex scene, try to start a different cheating scene type
        if(sexThread == -1)
            TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: TTM_AdultUtils.SetupQuickScene failed")
            UnMuteParticipants()
            string fbJson = MARAS.PlanBanterCheatingFallback(npc1, npc2, hasFramework, hasBeds)
            int fbKind = MARAS.GetJsonInt(fbJson, "sceneKind", 0)
            if(fbKind <= 0)
                TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: PlanBanterCheatingFallback returned none")
                return
            endif
            selectedScene = SceneKindToScene(fbKind)
            sceneIdx = MARAS.GetJsonInt(fbJson, "sceneIdx", 0)
            MuteParticipants(npc1, npc2, npc1MainPartner, false)
        endif
    endif

    if(!selectedScene)
        UnMuteParticipants()
        return
    endif

    npc1Alias.ForceRefTo(npc1)
    npc2Alias.ForceRefTo(npc2)
    if(npc1MainPartner)
        TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: npc1MainPartner: " + npc1MainPartner)
        npc1PartnerAlias.ForceRefTo(npc1MainPartner)
    endif
    if(npc2MainPartner)
        TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: npc2MainPartner: " + npc2MainPartner)
        npc2PartnerAlias.ForceRefTo(npc2MainPartner)
    endif

    MARAS.RecordScenePlayed(npc1, npc2)
    selectedScene.Start()

    TTM_Debug.debug("Scene " + selectedScene + " is running - " + selectedScene.IsPlaying())
EndFunction

Function RestoreMovedNpcsOnLocationChange()
    MARAS.RestoreMovedBanterSceneNpcs()
EndFunction

Function MuteParticipants(Actor npc1, Actor npc2, Actor npc1Partner, bool shouldMutePartner)
    if(TTM_Data.GetHasSkyrimNet())
        MARAS.MuteBanterSceneParticipants(npc1, npc2, npc1Partner, shouldMutePartner)
        if(Game.GetModByName("TT_OStimNet.esp") != 255)
            OStimNet.SetMuted(true)
        endif
    endif
EndFunction

Function UnMuteParticipants()
    bool wasMuted = MARAS.UnMuteBanterSceneParticipants()
    if(wasMuted && Game.GetModByName("TT_OStimNet.esp") != 255)
        OStimNet.SetMuted(false)
    endif
EndFunction

Scene Function SceneKindToScene(int sceneKind)
    if(sceneKind == 1)
        return LoversScene
    elseif(sceneKind == 2)
        return DatingScene
    elseif(sceneKind == 3)
        return EngagedScene
    elseif(sceneKind == 4)
        return MarriedScene
    elseif(sceneKind == 5)
        return OpenRelationshipScene
    elseif(sceneKind == 6)
        return CheatingScene
    elseif(sceneKind == 7)
        return CheatingDiscoveredScene
    elseif(sceneKind == 8)
        return CheatingCaughtScene
    endif
    return None
EndFunction

Function StartCheatingScene(Actor cheater, Actor affairPartner, Actor victim)
    if(!cheater || !affairPartner || !victim)
        TTM_Debug.debug("TTM_NpcsScenes: StartCheatingScene: Invalid participants")
        return
    endif
    if(cheater.GetCurrentScene() || affairPartner.GetCurrentScene() || victim.GetCurrentScene())
        TTM_Debug.debug("TTM_NpcsScenes: StartCheatingScene: One of the participants is already in a scene")
        return
    endif
    if(cheater == TTM_Data.GetPlayer())
        TTM_Debug.debug("TTM_NpcsScenes: StartCheatingScene: cheater is player")
        return
    endif

    int idx = MARAS.PlanCaughtCheatingSceneIndex(cheater, affairPartner)
    if(idx < 0)
        TTM_Debug.debug("TTM_NpcsScenes: StartCheatingScene: Could not get scene index")
        return
    endif
    sceneIdx = idx

    npc1Alias.ForceRefTo(cheater)
    npc2Alias.ForceRefTo(affairPartner)
    npc1PartnerAlias.ForceRefTo(victim)

    MARAS.RecordScenePlayed(cheater, affairPartner)
    CheatingCaughtScene.Start()
EndFunction

Function PartnerNoticedCheatingPlayer(Actor npc)
    TTM_Debug.debug("TTM_NpcsScenes: PartnerNoticedCheatingPlayer: npc: " + npc + ": alias: " + PlayersPartnerWalkInCheating)
    PlayersPartnerWalkInCheating.ForceRefTo(npc)
    npc.EvaluatePackage()
EndFunction

Function PartnerNoticedOpenRelPlayer(Actor npc)
    TTM_Debug.debug("TTM_NpcsScenes: PartnerNoticedOpenRelPlayer: npc: " + npc + ": alias: " + PlayersPartnerWalkInOpenRelationship)
    PlayersPartnerWalkInOpenRelationship.ForceRefTo(npc)
    npc.EvaluatePackage()
EndFunction

Function FreePartnerNoticeCheatingPlayer(Actor npc)
    TTM_Debug.debug("TTM_NpcsScenes: FreePartnerNoticeCheatingPlayer: Clearing PlayersPartnerWalkInCheating alias")
    PlayersPartnerWalkInCheating.Clear()
    npc.EvaluatePackage()
EndFunction

Function FreePartnerNoticeOpenRelPlayer(Actor npc)
    TTM_Debug.debug("TTM_NpcsScenes: FreePartnerNoticeOpenRelPlayer: Clearing PlayersPartnerWalkInOpenRelationship alias")
    PlayersPartnerWalkInOpenRelationship.Clear()
    npc.EvaluatePackage()
EndFunction


Function PartnerSuggestOpen(Actor npc)
    if(!npc)
        return
    endif
    if(npc.GetCurrentScene() != none)
        TTM_Debug.debug("TTM_NpcsScenes: PartnerSuggestOpen: npc is in a scene: " + npc.GetCurrentScene())
        return
    endif
    TTM_Debug.debug("TTM_NpcsScenes: PartnerSuggestOpen: npc: " + npc + ": alias: " + PlayersPartnerSuggestOpen)
    PlayersPartnerSuggestOpen.ForceRefTo(npc)
    npc.EvaluatePackage()
EndFunction

Function FreePartnerSuggestOpen(Actor npc)
    TTM_Debug.debug("TTM_NpcsScenes: FreePartnerSuggestOpen: Clearing PlayersPartnerSuggestOpen alias")
    PlayersPartnerSuggestOpen.Clear()
    if(npc)
        npc.EvaluatePackage()
    endif
EndFunction

Function PartnerSuggestClose(Actor npc)
    if(!npc)
        return
    endif
    if(npc.GetCurrentScene() != none)
        TTM_Debug.debug("TTM_NpcsScenes: PartnerSuggestClose: npc is in a scene: " + npc.GetCurrentScene())
        return
    endif
    TTM_Debug.debug("TTM_NpcsScenes: PartnerSuggestClose: npc: " + npc + ": alias: " + PlayersPartnerSuggestClose)
    PlayersPartnerSuggestClose.ForceRefTo(npc)
    npc.EvaluatePackage()
EndFunction

Function FreePartnerSuggestClose(Actor npc)
    TTM_Debug.debug("TTM_NpcsScenes: FreePartnerSuggestClose: Clearing PlayersPartnerSuggestClose alias")
    PlayersPartnerSuggestClose.Clear()
    if(npc)
        npc.EvaluatePackage()
    endif
EndFunction

Function PartnerConfessTalking(Actor npc)
    if(!npc)
        return
    endif
    if(npc.GetCurrentScene() != none)
        TTM_Debug.debug("TTM_NpcsScenes: PartnerConfessTalking: npc is in a scene: " + npc.GetCurrentScene())
        return
    endif
    TTM_Debug.debug("TTM_NpcsScenes: PartnerConfessTalking: npc: " + npc + ": alias: " + PlayersPartnerConfessTalking)
    PlayersPartnerConfessTalking.ForceRefTo(npc)
    npc.EvaluatePackage()
EndFunction

Function FreePartnerConfessTalking(Actor npc)
    TTM_Debug.debug("TTM_NpcsScenes: FreePartnerConfessTalking: Clearing PlayersPartnerConfessTalking alias")
    PlayersPartnerConfessTalking.Clear()
    if(npc)
        npc.EvaluatePackage()
    endif
EndFunction

Function PartnerConfessPhysical(Actor npc)
    if(!npc)
        return
    endif
    if(npc.GetCurrentScene() != none)
        TTM_Debug.debug("TTM_NpcsScenes: PartnerConfessPhysical: npc is in a scene: " + npc.GetCurrentScene())
        return
    endif
    TTM_Debug.debug("TTM_NpcsScenes: PartnerConfessPhysical: npc: " + npc + ": alias: " + PlayersPartnerConfessPhysical)
    PlayersPartnerConfessPhysical.ForceRefTo(npc)
    npc.EvaluatePackage()
EndFunction

Function FreePartnerConfessPhysical(Actor npc)
    TTM_Debug.debug("TTM_NpcsScenes: FreePartnerConfessPhysical: Clearing PlayersPartnerConfessPhysical alias")
    PlayersPartnerConfessPhysical.Clear()
    if(npc)
        npc.EvaluatePackage()
    endif
EndFunction
