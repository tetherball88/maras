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

int Property sexThread = -1 Auto

Function SetupSceneOnLocationChange()
    Actor player = TTM_Data.GetPlayer()
    if(!MARAS.IsLocationAllowedForScenes(player.GetCurrentLocation()))
        TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: Location not allowed for scenes by settings")
        return
    endif
    Actor[] couple = MARAS.FindBanterCoupleCandidates()
    if(couple.length < 2)
        TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: Not enough participants: " + couple.Length)
        return
    endif
    Actor npc1 = couple[0]
    Actor npc2 = couple[1]
    Actor npc1MainPartner = none
    Actor npc2MainPartner = none
    bool shouldSetupSexScene = false
    sexThread = -1

    if(couple.Length < 2)
        TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: Not enough participants: " + couple.Length)
        return
    endif
    bool isCheating = MARAS.IsRelationshipCheating(npc1, npc2)
    bool isOpen = MARAS.IsNPCInOpenRelationship(npc1) || MARAS.IsNPCInOpenRelationship(npc2)
    sceneIdx = Utility.RandomInt(0, 4)
    Scene selectedScene = None
    TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: isCheating: " + isCheating + ", isOpen: "+isOpen+", initial sceneIdx: " + sceneIdx)
    if(isCheating)
        int typeOfCheatingScene = GetCheatingSceneWithFallbacks(npc1, npc2)
        if(typeOfCheatingScene == -1)
            return
        endif

        selectedScene = CheatingTypeToScene(typeOfCheatingScene)

        sceneIdx = GetCheatingSceneIdx(npc1, npc2, typeOfCheatingScene)

        bool isNpc1Cheating = MARAS.IsNPCCheating(npc1)
        bool isNpc2Cheating = MARAS.IsNPCCheating(npc2)
        if((isNpc1Cheating && npc1 == player) || (isNpc2Cheating && npc2 == player))
            TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: player is cheating, it will be handled in other way")
            return
        endif
        TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: isNpc1Cheating: " + isNpc1Cheating + ", isNpc2Cheating: " + isNpc2Cheating)
        if(isNpc1Cheating && isNpc2Cheating)
            bool swap = Utility.RandomInt(0, 1) == 0
            if(swap)
                Actor temp = npc1
                npc1 = npc2
                npc2 = temp
            endif
            npc1MainPartner = MARAS.GetPrimaryPartner(npc1)
            npc2MainPartner = MARAS.GetPrimaryPartner(npc2)
        else
            ; in scenes first actor is the one cheating
            if(isNpc2Cheating)
                Actor temp = npc1
                npc1 = npc2
                npc2 = temp
            endif
            npc1MainPartner = MARAS.GetPrimaryPartner(npc1)
        endif

        if(typeOfCheatingScene == 2)
            shouldSetupSexScene = true
        endif

        if(npc1MainPartner && npc1MainPartner.GetCurrentScene() != none)
            TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: can't start scene npc1MainPartner is in a scene: " + npc1MainPartner.GetCurrentScene())
            return
        endif
        if(npc1MainPartner && typeOfCheatingScene >= 1)
            ; move npc1's partner since they active aprticipant of a scene
            MoveNpc(npc1MainPartner, player)
        endif
    elseif(isOpen)
        if(!MARAS.GetSettingBool("enableScenes_OpenRelationship"))
            TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: Open relationship scenes are disabled")
            return
        endif
        selectedScene = OpenRelationshipScene
        bool isNpc1Open = MARAS.IsNPCInOpenRelationship(npc1)
        bool isNpc2Open = MARAS.IsNPCInOpenRelationship(npc2)
        if(isNpc1Open && isNpc2Open)
            bool swap = Utility.RandomInt(0, 1) == 0
            if(swap)
                Actor temp = npc1
                npc1 = npc2
                npc2 = temp
                sceneIdx = 4 ; 5th open relationship scene is for when both npcs are in open relationships
            endif
            npc1MainPartner = MARAS.GetPrimaryPartner(npc1)
            npc2MainPartner = MARAS.GetPrimaryPartner(npc2)
        else
            if(sceneIdx == 4)
                sceneIdx = Utility.RandomInt(0, 3)
            endif
            if(isNpc2Open)
                Actor temp = npc1
                npc1 = npc2
                npc2 = temp
            endif
            npc1MainPartner = MARAS.GetPrimaryPartner(npc1)
        endif
    elseif(MARAS.IsNPCStatusWith(npc1, npc2, "lover"))
        if(MARAS.GetSettingBool("enableScenes_Lover"))
            selectedScene = LoversScene
        endif
    elseif(MARAS.IsNPCStatusWith(npc1, npc2, "dating"))
        if(MARAS.GetSettingBool("enableScenes_Dating"))
            selectedScene = DatingScene
        endif
    elseif(MARAS.IsNPCStatusWith(npc1, npc2, "engaged"))
        if(MARAS.GetSettingBool("enableScenes_Engaged"))
            selectedScene = EngagedScene
        endif
    elseif(MARAS.IsNPCStatusWith(npc1, npc2, "married"))
        if(MARAS.GetSettingBool("enableScenes_Married"))
            selectedScene = MarriedScene
        endif
    endif
    TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: final sceneIdx: " + sceneIdx)

    if(!selectedScene)
        TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: selectedScene is None")
        return
    endif

    bool npc1InSameCell = npc1.GetParentCell() == player.GetParentCell()
    bool npc2InSameCell = npc2.GetParentCell() == player.GetParentCell()

    if(!npc1InSameCell && !npc2InSameCell)
        TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: npc1InSameCell is False and npc2InSameCell is False")
        return
    endif

    if(npc1 && npc1.GetCurrentScene() != none)
        TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: npc1 is in a scene: " + npc1.GetCurrentScene())
        return
    endif
    if(npc2 && npc2.GetCurrentScene() != none)
        TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: npc2 is in a scene: " + npc2.GetCurrentScene())
        return
    endif

    if(!npc1InSameCell)
        MoveNpc(npc1, npc2)
    endif
    if(!npc2InSameCell)
        MoveNpc(npc2, npc1)
    endif

    MuteParticipants(npc1, npc2, npc1MainPartner, shouldSetupSexScene)

    if(shouldSetupSexScene)
        sexThread = TTM_AdultUtils.SetupQuickScene(npc1, npc2, true)
        ; if we couldn't start sex scene, try to start a different cheating scene type
        if(sexThread == -1)
            TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: TTM_AdultUtils.SetupQuickScene failed")
            UnMuteParticipants()
            int typeOfCheatingScene = GetCheatingSceneWithFallbacks(npc1, npc2, false)
            if(typeOfCheatingScene == -1)
                TTM_Debug.debug("TTM_NpcsScenes: SetupSceneOnLocationChange: GetCheatingSceneWithFallbacks returned -1")
                return
            endif
            selectedScene = CheatingTypeToScene(typeOfCheatingScene)
            sceneIdx = GetCheatingSceneIdx(npc1, npc2, typeOfCheatingScene)
        endif
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
    Form[] movedNpcs = StorageUtil.FormListToArray(none, "TTM_MovedNpcs")
    if(movedNpcs.Length == 0)
        return
    endif
    Actor player = TTM_Data.GetPlayer()
    int i = 0
    while(i < movedNpcs.Length)
        Actor npc = movedNpcs[i] as Actor
        bool npcInSameCell = npc.GetParentCell() == player.GetParentCell()
        if(!npcInSameCell)
            RestoreNpc(npc)
        endif
        i += 1
    endwhile
EndFunction

Function MoveNpc(Actor npc, Actor target)
    ObjectReference originalPlaceMarker = npc.PlaceAtMe(TTM_Data.GetRegularMarker(), 1, true)
    StorageUtil.SetFormValue(npc, "TTM_NpcOriginalLocation", originalPlaceMarker)
    StorageUtil.FormListAdd(none, "TTM_MovedNpcs", npc)
    TTM_Debug.debug("TTM_NpcsScenes: MoveNpc: npc: " + npc + ", target: " + target + ", originalPlaceMarker: " + originalPlaceMarker)
    npc.MoveTo(target, afZOffset = 35)
    PO3_SKSEFunctions.MoveToNearestNavmeshLocation(npc)
EndFunction

Function RestoreNpc(Actor npc)
    ObjectReference originalLocation = StorageUtil.GetFormValue(npc, "TTM_NpcOriginalLocation") as ObjectReference
    if(!originalLocation)
        return
    endif
    npc.MoveTo(originalLocation)
    StorageUtil.UnsetFormValue(npc, "TTM_NpcOriginalLocation")
    StorageUtil.FormListRemove(none, "TTM_MovedNpcs", npc)
    originalLocation.Disable()
    originalLocation.Delete()
EndFunction

Function MuteParticipants(Actor npc1, Actor npc2, Actor npc1Partner, bool shouldMutePartner)
    if(TTM_Data.GetHasSkyrimNet())
        Faction SkyrimNetBlacklistFaction = TTM_Data.GetSkyrimNetFilterFaction()
        if(SkyrimNetBlacklistFaction)
            npc1.AddToFaction(SkyrimNetBlacklistFaction)
            npc2.AddToFaction(SkyrimNetBlacklistFaction)
            StorageUtil.FormListAdd(none, "SkyrimNet_Blacklisted_Actors", npc1)
            StorageUtil.FormListAdd(none, "SkyrimNet_Blacklisted_Actors", npc2)
            if(shouldMutePartner)
                npc1Partner.AddToFaction(SkyrimNetBlacklistFaction)
                StorageUtil.FormListAdd(none, "SkyrimNet_Blacklisted_Actors", npc1Partner)
            endif
        endif
        if(Game.GetModByName("TT_OStimNet.esp") != 255)
            OStimNet.SetMuted(true)
        endif
    endif
EndFunction

Function UnMuteParticipants()
    Form[] blacklistedActors = StorageUtil.FormListToArray(none, "SkyrimNet_Blacklisted_Actors")
    if(blacklistedActors.Length == 0)
        return
    endif
    int i = 0
    Faction SkyrimNetBlacklistFaction = TTM_Data.GetSkyrimNetFilterFaction()
    while(i < blacklistedActors.Length)
        Actor npc = blacklistedActors[i] as Actor
        npc.RemoveFromFaction(SkyrimNetBlacklistFaction)
        i += 1
    endwhile
    StorageUtil.FormListClear(none, "SkyrimNet_Blacklisted_Actors")
    if(Game.GetModByName("TT_OStimNet.esp") != 255)
        OStimNet.SetMuted(false)
    endif
EndFunction

int Function GetCheatingSceneWithFallbacks(Actor npc1, Actor npc2, bool allowCaught = true)
    bool allowCheatingSecret = MARAS.GetSettingBool("enableScenes_Cheating")
    bool allowCheatingDiscovered = MARAS.GetSettingBool("enableScenes_CheatingDiscovered")
    bool allowCheatingCaught = MARAS.GetSettingBool("enableScenes_CheatingCaught")
    if(allowCaught == false)
        allowCheatingCaught = false
    endif
    Actor player = TTM_Data.GetPlayer()
    int sexFramework = TTM_AdultUtils.GetEngine()

    if(!allowCheatingSecret && !allowCheatingDiscovered && !allowCheatingCaught)
        TTM_Debug.debug("TTM_NpcsScenes: GetCheatingSceneWithFallbacks: All cheating scenes are disabled")
        return -1
    endif
    ; 0 = secret occuring; 1 - discovered; 2 - caught mid act
    int typeOfCheatingScene = Utility.RandomInt(0, 1)
    bool isPlayerVictim = MARAS.HasCheatedOn(npc1, player) || MARAS.HasCheatedOn(npc2, player)
    bool hasBedsNearby = TTM_AdultUtils.HasBedsNearby(npc1)
    if(sexFramework > -1 && npc1.GetParentCell().IsInterior() && !isPlayerVictim && hasBedsNearby)
        typeOfCheatingScene = Utility.RandomInt(0, 2)
    endif
    ; if one of npcs is cheating on player select only scene which involves cheater and affair partner.
    ; other types of cheating scenes also require cheater main partner which would be player
    ; if player walks in on their partner cheating player can intervene on their own, they don't need scene to control them
    if(isPlayerVictim)
        typeOfCheatingScene = 0
    endif
    ; Validate and fallback if chosen type is disabled
    if(typeOfCheatingScene == 2 && !allowCheatingCaught)
        if(allowCheatingDiscovered && !isPlayerVictim)
            typeOfCheatingScene = 1
        elseif(allowCheatingSecret)
            typeOfCheatingScene = 0
        else
            TTM_Debug.debug("TTM_NpcsScenes: GetCheatingSceneWithFallbacks: No permitted cheating scene type available")
            return -1
        endif
    endif
    if(typeOfCheatingScene == 1 && !allowCheatingDiscovered)
        if(allowCheatingSecret)
            typeOfCheatingScene = 0
        elseif(allowCheatingCaught && sexFramework > -1 && npc1.GetParentCell().IsInterior() && !isPlayerVictim && hasBedsNearby)
            typeOfCheatingScene = 2
        else
            TTM_Debug.debug("TTM_NpcsScenes: GetCheatingSceneWithFallbacks: No permitted cheating scene type available")
            return -1
        endif
    endif
    if(typeOfCheatingScene == 0 && !allowCheatingSecret)
        if(allowCheatingDiscovered && !isPlayerVictim)
            typeOfCheatingScene = 1
        elseif(allowCheatingCaught && sexFramework > -1 && npc1.GetParentCell().IsInterior() && !isPlayerVictim && hasBedsNearby)
            typeOfCheatingScene = 2
        else
            TTM_Debug.debug("TTM_NpcsScenes: SetupGetCheatingSceneWithFallbacksSceneOnLocationChange: No permitted cheating scene type available")
            return -1
        endif
    endif
EndFunction

int Function GetCheatingSceneIdx(Actor npc1, Actor npc2, int typeOfCheatingScene)
    bool isNpc1Cheating = MARAS.IsNPCCheating(npc1)
    bool isNpc2Cheating = MARAS.IsNPCCheating(npc2)
    TTM_Debug.debug("TTM_NpcsScenes: GetCheatingSceneIdx: isNpc1Cheating: " + isNpc1Cheating + ", isNpc2Cheating: " + isNpc2Cheating)
    if(isNpc1Cheating && isNpc2Cheating)
        sceneIdx = 4 ; 5th cheating scene is for when both npcs are cheating on their main partners
    else
        if(sceneIdx == 4)
            sceneIdx = Utility.RandomInt(0, 3)
        endif
    endif

    if(typeOfCheatingScene == 1)
        ; discovered has 6 scenes
        sceneIdx = Utility.RandomInt(0, 5)
    elseif(typeOfCheatingScene == 2)
        ; caught mid act has 5 scenes
        sceneIdx = Utility.RandomInt(0, 4)
    endif

    return sceneIdx
EndFunction

Scene Function CheatingTypeToScene(int typeOfCheatingScene)
    if(typeOfCheatingScene == 1)
        return CheatingDiscoveredScene
    elseif(typeOfCheatingScene == 2)
        return CheatingCaughtScene
    endif
    return CheatingScene
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

    Scene selectedScene = CheatingTypeToScene(2)
    int idx = GetCheatingSceneIdx(cheater, affairPartner, 2)
    if(idx == -1)
        TTM_Debug.debug("TTM_NpcsScenes: StartCheatingScene: Could not get scene index")
        return
    endif

    npc1Alias.ForceRefTo(cheater)
    npc2Alias.ForceRefTo(affairPartner)
    npc1PartnerAlias.ForceRefTo(victim)

    MARAS.RecordScenePlayed(cheater, affairPartner)
    selectedScene.Start()
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
