;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 2
Scriptname TTM_DialogueManagerSpouseManagement Extends TopicInfo Hidden

String Property PromptKey Auto
Bool Property UseAI Auto
Potion Property FoodMarriageMeal Auto

;BEGIN FRAGMENT Fragment_0
Function Fragment_0(ObjectReference akSpeakerRef)
Actor akSpeaker = akSpeakerRef as Actor
;BEGIN CODE
;OnBegin
    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("TTM_DialogueManagerSpouseManagement:OnBegin:"+PromptKey+":useAI:"+UseAI)
    endif
    if(PromptKey == "manage_spouse_set_new_home_ask" && UseAI)
        TTM_ServicePlayerHouse.ChooseHomeForSpouseMsg(akSpeaker)
    elseif(PromptKey == "manage_spouse_un_set_new_home")
        TTM_ServicePlayerHouse.ReleaseSpouseFromPlayerHome(akSpeaker)
    elseif(PromptKey == "manage_spouse_check_spouse_home")
        TTM_ServiceSpouseAssets.StartShareHomeWithPlayer(akSpeaker)
    elseif(PromptKey == "manage_spouse_leave_spouse_home")
        TTM_ServiceSpouseAssets.StopShareHomeWithPlayer(akSpeaker)
    elseif(PromptKey == "maras_no_ai_engagement")
        TTM_Utils.SendRelationshipChangeEvent(akSpeaker, "engaged")
    elseif(PromptKey == "manage_spouse_cooking")
        TTM_JData.GetPlayer().AddItem(FoodMarriageMeal)
    elseif(PromptKey == "manage_spouse_store")
        TTM_ServiceRelationships.ShareIncome(akSpeaker, true)
    elseif(PromptKey == "manage_spouse_set_hierarchy_0")
        TTM_ServiceHierarchy.ChangeSpouseRank(akSpeaker, 0)
        ; write prompt for spouse responding to being set to rank 1,
    elseif(PromptKey == "manage_spouse_set_hierarchy_1")
        TTM_ServiceHierarchy.ChangeSpouseRank(akSpeaker, 1)
    elseif(PromptKey == "manage_spouse_set_hierarchy_2")
        TTM_ServiceHierarchy.ChangeSpouseRank(akSpeaker, 2)
    endif

    if(useAI)
        string prompt = TTM_JData.GetDialoguePrompt(PromptKey)
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
    if(PromptKey == "manage_spouse_set_new_home_ask" && !UseAI)
        TTM_ServicePlayerHouse.ChooseHomeForSpouseMsg(akSpeakerRef as Actor)
    elseif(PromptKey == "maras_npc_management_gift")
        akSpeaker.ShowGiftMenu(true, none, true)
    endif
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment
