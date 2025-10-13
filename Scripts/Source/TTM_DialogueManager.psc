;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 2
Scriptname TTM_DialogueManager Extends TopicInfo Hidden

String Property PromptKey Auto
Bool Property UseAI Auto
Potion Property FoodMarriageMeal Auto

;BEGIN FRAGMENT Fragment_0
Function Fragment_0(ObjectReference akSpeakerRef)
Actor akSpeaker = akSpeakerRef as Actor
;BEGIN CODE
;OnBegin
    TTM_Debug.trace("TTM_DialogueManager:OnBegin:"+PromptKey+":useAI:"+UseAI)
    if(PromptKey == "maras_enable_candidate")
        if(!akSpeaker.IsInFaction(TTM_JData.GetTrackedNpcFaction()))
            akSpeaker.AddToFaction(TTM_JData.GetTrackedNpcFaction())
        endif
        TTM_Utils.SendRelationshipChangeEvent(akSpeaker, "candidate")
    elseif(PromptKey == "maras_enable_polygamy_maramal_start")
        self.GetOwningQuest().SetStage(10)
    elseif(PromptKey == "maras_enable_polygamy_senna_start")
        self.GetOwningQuest().SetStage(20)
    elseif(PromptKey == "maras_enable_polygamy_senna_finish")
        self.GetOwningQuest().SetStage(40)
    elseif(PromptKey == "maras_enable_polygamy_urag_start")
        self.GetOwningQuest().SetStage(50)
    elseif(PromptKey == "maras_enable_polygamy_urag_finish")
        self.GetOwningQuest().SetStage(55)
    elseif(PromptKey == "maras_enable_polygamy_maramal_finish")
        self.GetOwningQuest().SetStage(70)
    elseif(PromptKey == "maras_enable_polygamy_current_spouse")
        self.GetOwningQuest().SetStage(80)
    elseif(PromptKey == "manage_spouse_set_new_home_ask" && UseAI)
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
        TTM_ServiceNpcs.ShareIncome(akSpeaker)
    elseif(PromptKey == "manage_spouse_set_hierarchy_1")
        TTM_ServiceNpcs.ChangeSpouseRank(akSpeaker, 0)
    elseif(PromptKey == "manage_spouse_set_hierarchy_2")
        TTM_ServiceNpcs.ChangeSpouseRank(akSpeaker, 1)
    elseif(PromptKey == "manage_spouse_set_hierarchy_3")
        TTM_ServiceNpcs.ChangeSpouseRank(akSpeaker, 2)
    elseif(PromptKey == "maras_reconcile_accept")
        TTM_Utils.SendRelationshipChangeEvent(akSpeaker, "engaged")
    elseif(PromptKey == "maras_breakup")
        if(TTM_Utils.IsFiance(akSpeaker))
            TTM_Utils.SendRelationshipChangeEvent(akSpeaker, "jilted")
        elseif(TTM_Utils.IsSpouse(akSpeaker))
            TTM_Utils.SendRelationshipChangeEvent(akSpeaker, "divorced")
        endif
    elseif(PromptKey == "manage_spouse_set_new_home_do" || PromptKey == "maras_spouse_hello")
        ; no special action
    else
        TTM_Debug.warn("TTM_DialogueManager:OnBegin:UnhandledKey:"+PromptKey)
    endif

    if(useAI)
        SkyrimNetApi.DirectNarration(SkyrimNetApi.RenderTemplate(PromptKey, "spouseName", TTM_Utils.GetActorName(akSpeaker)), akSpeaker, TTM_JData.GetPlayer())
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
    endif
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment
