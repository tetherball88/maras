;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 2
Scriptname TTM_DialogueManagerMisc Extends TopicInfo Hidden

String Property PromptKey Auto
Bool Property UseAI Auto
FavorDialogueScript Property pFDS  Auto

;BEGIN FRAGMENT Fragment_2
Function Fragment_2(ObjectReference akSpeakerRef)
Actor akSpeaker = akSpeakerRef as Actor

;BEGIN CODE
    int endOp = MARAS.ApplyDialoguePromptEnd(akSpeaker, PromptKey)
    if(endOp == 1)
        TTM_AdultUtils.AddParticipant(TTM_Data.GetPlayer(), akSpeaker)
    elseif(endOp == 2)
        TTM_AdultUtils.AddParticipant(akSpeaker, TTM_Data.GetPlayer())
    elseif(endOp == 3)
        TTM_AdultUtils.EndThreadActor(akSpeaker)
    endif
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_0
Function Fragment_0(ObjectReference akSpeakerRef)
Actor akSpeaker = akSpeakerRef as Actor
;BEGIN CODE
;OnBegin
    int fx = MARAS.ApplyDialoguePromptBegin(akSpeaker, PromptKey)
    int speechOp = fx / 100
    int sceneOp = fx % 100
    if(speechOp == 1)
        pFDS.Persuade(akSpeaker)
    elseif(speechOp == 2)
        pFDS.Intimidate(akSpeaker)
    endif
    if(sceneOp > 0)
        if(sceneOp == 8)
            TTM_AdultUtils.SetupQuickScene(akSpeaker, TTM_Data.GetPlayer(), false, true)
        else
            if(sceneOp == 7)
                TTM_AdultUtils.FinishPlayerThread()
            endif
            TTM_NpcsScenes npcsScenes = TTM_Data.GetMarasNpcsScenesQuest() as TTM_NpcsScenes
            if(sceneOp == 1 || sceneOp == 7)
                npcsScenes.FreePartnerNoticeCheatingPlayer(akSpeaker)
            elseif(sceneOp == 2)
                npcsScenes.FreePartnerNoticeOpenRelPlayer(akSpeaker)
            elseif(sceneOp == 3)
                npcsScenes.FreePartnerSuggestOpen(akSpeaker)
            elseif(sceneOp == 4)
                npcsScenes.FreePartnerSuggestClose(akSpeaker)
            elseif(sceneOp == 5)
                npcsScenes.FreePartnerConfessTalking(akSpeaker)
            elseif(sceneOp == 6)
                npcsScenes.FreePartnerConfessPhysical(akSpeaker)
            endif
        endif
    endif

    if(UseAI)
        string prompt = TTM_Data.GetDialoguePrompt(PromptKey)
        string values = "{\"npcName\": \"" + TTM_Utils.GetActorName(akSpeaker) + "\", \"playerName\": \"" + TTM_Utils.GetActorName(TTM_Data.GetPlayer()) + "\"}"
        if(prompt != "")
            TTM_Utils.RequestDialogue(prompt, values, akSpeaker, TTM_Data.GetPlayer())
        endif
    endif
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment
