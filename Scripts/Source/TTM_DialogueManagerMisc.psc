;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 2
Scriptname TTM_DialogueManagerMisc Extends TopicInfo Hidden

String Property PromptKey Auto
Bool Property UseAI Auto

;BEGIN FRAGMENT Fragment_0
Function Fragment_0(ObjectReference akSpeakerRef)
Actor akSpeaker = akSpeakerRef as Actor
;BEGIN CODE
;OnBegin
    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("TTM_DialogueManagerMisc:OnBegin:"+PromptKey+":useAI:"+UseAI)
    endif

    if(PromptKey == "maras_enable_candidate")
        if(!TTM_Utils.IsTracking(akSpeaker))
            akSpeaker.AddToFaction(TTM_JData.GetTrackedNpcFaction())
            TTM_ServiceRelationsFinder.ScanRelationships(akSpeaker)
        endif
        TTM_Utils.SendRelationshipChangeEvent(akSpeaker, "candidate")
    elseif(PromptKey == "maras_no_ai_engagement")
        TTM_Utils.SendRelationshipChangeEvent(akSpeaker, "engaged")
    elseif(PromptKey == "maras_breakup_reconcile_accept")
        TTM_Utils.SendRelationshipChangeEvent(akSpeaker, "engaged")
    elseif(PromptKey == "maras_breakup")
        if(TTM_Utils.IsFiance(akSpeaker))
            TTM_Utils.SendRelationshipChangeEvent(akSpeaker, "jilted")
        elseif(TTM_Utils.IsSpouse(akSpeaker))
            TTM_Utils.SendRelationshipChangeEvent(akSpeaker, "divorced")
        endif
    endif

    if(UseAI)
        string prompt = TTM_JData.GetDialoguePrompt(PromptKey)
        string values = "{\"npcName\": \"" + TTM_Utils.GetActorName(akSpeaker) + "\", \"playerName\": \"" + TTM_Utils.GetActorName(TTM_JData.GetPlayer()) + "\"}"
        if(prompt != "")
            TTM_ServiceSkyrimNet.RequestDialogue(prompt, values, akSpeaker, TTM_JData.GetPlayer())
        endif
    endif
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment
