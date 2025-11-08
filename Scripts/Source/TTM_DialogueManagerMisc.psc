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
    if(PromptKey == "maras_enable_candidate")
        MARAS.RegisterCandidate(akSpeaker)
    elseif(PromptKey == "maras_no_ai_engagement")
        MARAS.PromoteNPCToStatus(akSpeaker, "engaged")
    elseif(PromptKey == "maras_breakup_reconcile_accept")
        MARAS.PromoteNPCToStatus(akSpeaker, "engaged")
    elseif(PromptKey == "maras_breakup")
        if(MARAS.IsNPCStatus(akSpeaker, "engaged"))
            MARAS.PromoteNPCToStatus(akSpeaker, "jilted")
        elseif(MARAS.IsNPCStatus(akSpeaker, "married"))
            MARAS.PromoteNPCToStatus(akSpeaker, "divorced")
        endif
    elseif(PromptKey == "maras_non_marriage_management")
        if(!TTM_Data.GetHasSkyrimNet() && !MARAS.IsNPCStatus(akSpeaker, "married"))
            TTM_Data.GetSetGameNoAIMarriageGlobal(akSpeaker)
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
