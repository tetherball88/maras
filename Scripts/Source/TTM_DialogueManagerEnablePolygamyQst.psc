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
    self.GetOwningQuest().SetStage(StageTarget)

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
