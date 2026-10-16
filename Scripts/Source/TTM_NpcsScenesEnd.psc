;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 1
Scriptname TTM_NpcsScenesEnd Extends Scene Hidden

ReferenceAlias Property npc1Alias Auto
ReferenceAlias Property npc1PartnerAlias Auto
ReferenceAlias Property npc2Alias Auto
ReferenceAlias Property npc2PartnerAlias Auto
Scene Property CheatingDiscoveredScene auto
Scene Property CheatingCaughtScene auto

;BEGIN FRAGMENT Fragment_0
Function Fragment_0()
;BEGIN CODE
    TTM_Debug.debug("TTM_NpcsScenesEnd: Scene ended.")
    TTM_NpcsScenes npcsSceneQst = GetOwningQuest() as TTM_NpcsScenes

    if(self == CheatingDiscoveredScene)
        Actor npc1 = npc1Alias.GetActorRef()
        Actor npc2 = npc2Alias.GetActorRef()
        Actor npc1Partner = npc1PartnerAlias.GetActorRef()
        if(npcsSceneQst.sceneIdx == 0 || npcsSceneQst.sceneIdx == 1 )
            MARAS.SetCheatingDiscovered(npc1, npc2, "breakup")
        elseif(npcsSceneQst.sceneIdx == 2 || npcsSceneQst.sceneIdx == 3 )
            MARAS.SetCheatingDiscovered(npc1, npc2, "confronted")
        elseif(npcsSceneQst.sceneIdx == 4 || npcsSceneQst.sceneIdx == 5 )
            MARAS.SetCheatingDiscovered(npc1, npc2, "allowed")
        endif
    elseif(self == CheatingCaughtScene)
        Actor npc1 = npc1Alias.GetActorRef()
        Actor npc2 = npc2Alias.GetActorRef()
        Actor npc1Partner = npc1PartnerAlias.GetActorRef()
        if(npcsSceneQst.sceneIdx == 0 || npcsSceneQst.sceneIdx == 1 )
            MARAS.SetCheatingDiscovered(npc1, npc2, "breakup")
            if(npcsSceneQst.sceneIdx == 0)
                TTM_AdultUtils.EndThread(npcsSceneQst.sexThread)
            else
                TTM_AdultUtils.RestoreOriginalAutoMode(npcsSceneQst.sexThread)
            endif
        elseif(npcsSceneQst.sceneIdx == 2 )
            MARAS.SetCheatingDiscovered(npc1, npc2, "confronted")
            TTM_AdultUtils.EndThread(npcsSceneQst.sexThread)
        elseif(npcsSceneQst.sceneIdx == 3)
            MARAS.SetCheatingDiscovered(npc1, npc2, "breakup")
            TTM_AdultUtils.EndThread(npcsSceneQst.sexThread)
            ; additionally cheater breaks with their affair partner
            MARAS.Breakup(npc1, npc2)
        elseif(npcsSceneQst.sceneIdx == 4)
            MARAS.SetCheatingDiscovered(npc1, npc2, "allowed")
            TTM_AdultUtils.AddParticipant(npc1, npc1Partner)
        endif
    endif

    npcsSceneQst.UnMuteParticipants()
    npc1Alias.Clear()
    npc2Alias.Clear()
    npc1PartnerAlias.Clear()
    npc2PartnerAlias.Clear()
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment
