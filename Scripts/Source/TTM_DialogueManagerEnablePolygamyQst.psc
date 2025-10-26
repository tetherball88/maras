;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 2
Scriptname TTM_DialogueManagerEnablePolygamyQst Extends TopicInfo Hidden

String Property PromptKey Auto
Bool Property UseAI Auto
Int Property StageTarget Auto

;BEGIN FRAGMENT Fragment_0
Function Fragment_0(ObjectReference akSpeakerRef)
Actor akSpeaker = akSpeakerRef as Actor
;BEGIN CODE
;OnBegin
    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("TTM_DialogueManagerEnablePolygamyQst:OnBegin:"+PromptKey+":useAI:"+UseAI)
    endif
    self.GetOwningQuest().SetStage(StageTarget)

    if(UseAI)
        string prompt = TTM_JData.GetDialoguePrompt(PromptKey)
        TTM_Debug.trace("TTM_DialogueManagerEnablePolygamyQst:Prompt:"+prompt)
        string values = "{\"npcName\": \"" + TTM_Utils.GetActorName(akSpeaker) + "\", \"playerName\": \"" + TTM_Utils.GetActorName(TTM_JData.GetPlayer()) + "\"}"
        if(prompt != "")
            TTM_RequestLLMDialogue.RequestDialogue(prompt, values, akSpeaker, TTM_JData.GetPlayer())
        endif
    endif
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_1
Function Fragment_1(ObjectReference akSpeakerRef)
Actor akSpeaker = akSpeakerRef as Actor
;BEGIN CODE
;OnEnd
    self.GetOwningQuest().SetStage(StageTarget)
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment
