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
    Faction cheatingNpcCaught = TTM_Data.GetCheatingNpcCaughtFaction()
    Actor player = TTM_Data.GetPlayer()
    if(PromptKey == "maras_sex_walkin_cheating_sorry_no_persuade_failure")
        MARAS.Breakup(player, akSpeaker)
    elseif(PromptKey == "maras_sex_walkin_cheating_join_yes")
        TTM_AdultUtils.AddParticipant(player, akSpeaker)
    elseif(PromptKey == "maras_sex_walkin_cheating_join_no_persuade_success")
        TTM_AdultUtils.AddParticipant(player, akSpeaker)
    elseif(PromptKey == "maras_sex_walkin_cheating_join_no_persuade_failure")
        MARAS.Breakup(player, akSpeaker)
    elseif(PromptKey == "maras_sex_walkin_or_join_yes")
        TTM_AdultUtils.AddParticipant(player, akSpeaker)
    elseif(PromptKey == "maras_sex_walkin_or_join_no_persuade_success")
        TTM_AdultUtils.AddParticipant(player, akSpeaker)
    elseif(PromptKey == "maras_no_ai_cheating_demand_success"\
        || PromptKey == "maras_no_ai_cheating_demand_intimidate_success" \
        || PromptKey == "maras_no_ai_cheating_demand_persuade_success" \
        || PromptKey == "maras_no_ai_cheating_demand_giveup_stop"\
    )
        if(akSpeaker.IsInFaction(cheatingNpcCaught))
            TTM_AdultUtils.EndThreadActor(akSpeaker)
        endif
    elseif(PromptKey == "maras_player_caught_cheater_join__persuade_success" || PromptKey == "maras_player_caught_cheater_join_success")
        TTM_AdultUtils.AddParticipant(akSpeaker, player)
    endif
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_0
Function Fragment_0(ObjectReference akSpeakerRef)
Actor akSpeaker = akSpeakerRef as Actor
;BEGIN CODE
;OnBegin
    Actor player = TTM_Data.GetPlayer()
    if(PromptKey == "maras_enable_candidate")
        MARAS.RegisterCandidate(akSpeaker)
    elseif(PromptKey == "maras_start_dating")
        MARAS.PromoteNPCToStatus(akSpeaker, "dating")
    elseif(PromptKey == "maras_start_lover")
        MARAS.PromoteNPCToStatus(akSpeaker, "lover")
    elseif(PromptKey == "maras_end_premarital")
        MARAS.PromoteNPCToStatus(akSpeaker, "candidate")
    elseif(PromptKey == "maras_no_ai_engagement")
        MARAS.PromoteNPCToStatus(akSpeaker, "engaged")
    elseif(PromptKey == "maras_no_ai_dating")
        MARAS.PromoteNPCToStatus(akSpeaker, "dating")
    elseif(PromptKey == "maras_no_ai_lover")
        MARAS.PromoteNPCToStatus(akSpeaker, "lover")
    elseif(PromptKey == "maras_breakup_reconcile_accept")
        MARAS.PromoteNPCToStatus(akSpeaker, "engaged")
    elseif(PromptKey == "maras_breakup" || PromptKey == "maras_no_ai_cheating_breakup")
        if(MARAS.IsNPCStatus(akSpeaker, "engaged"))
            MARAS.PromoteNPCToStatus(akSpeaker, "jilted")
        elseif(MARAS.IsNPCStatus(akSpeaker, "married"))
            MARAS.PromoteNPCToStatus(akSpeaker, "divorced")
        elseif(MARAS.IsNPCStatus(akSpeaker, "dating"))
            MARAS.PromoteNPCToStatus(akSpeaker, "exdating")
        elseif(MARAS.IsNPCStatus(akSpeaker, "lover"))
            MARAS.PromoteNPCToStatus(akSpeaker, "exlover")
        endif
    elseif(PromptKey == "maras_no_ai_cheating_demand_success")
        MARAS.SetCheatingDiscovered(akSpeaker, none, "confronted")
    elseif(PromptKey == "maras_no_ai_cheating_demand_fail")
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -50)
    elseif(PromptKey == "maras_no_ai_cheating_demand_intimidate_success")
        pFDS.Intimidate(akSpeaker)
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -20)
        MARAS.SetCheatingDiscovered(akSpeaker, none, "confronted")
    elseif(PromptKey == "maras_no_ai_cheating_demand_intimidate_fail")
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -25)
    elseif(PromptKey == "maras_no_ai_cheating_demand_persuade_success")
        pFDS.Persuade(akSpeaker)
        MARAS.SetCheatingDiscovered(akSpeaker, none, "confronted")
    elseif(PromptKey == "maras_no_ai_cheating_demand_persuade_fail")
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -15)
    elseif(PromptKey == "maras_no_ai_cheating_demand_or")
        MARAS.SetCheatingDiscovered(akSpeaker, none, "allowed")
        MARAS.SetPairIsOpen(TTM_Data.GetPlayer(), akSpeaker, true)
    elseif(PromptKey == "maras_no_ai_cheating_demand_giveup_continue")
        MARAS.SetCheatingDiscovered(akSpeaker, none, "allowed")
    elseif(PromptKey == "maras_no_ai_cheating_demand_giveup_stop")
        MARAS.SetCheatingDiscovered(akSpeaker, none, "confronted")
    elseif(PromptKey == "maras_no_ai_cheating_insist_success")
        MARAS.SetCheatingDiscovered(akSpeaker, none, "confessed")
    elseif(PromptKey == "maras_no_ai_cheating_insist_fail")
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -5)
    elseif(PromptKey == "maras_no_ai_cheating_insist_no_cheating")
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -25)
    elseif(PromptKey == "maras_no_ai_cheating_intimidate_success")
        pFDS.Intimidate(akSpeaker)
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -5)
        MARAS.SetCheatingDiscovered(akSpeaker, none, "confessed")
    elseif(PromptKey == "maras_no_ai_cheating_intimidate_fail")
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -15)
    elseif(PromptKey == "maras_no_ai_cheating_intimidate_no_cheating")
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -40)
    elseif(PromptKey == "maras_no_ai_cheating_persuade_success")
        pFDS.Persuade(akSpeaker)
        MARAS.SetCheatingDiscovered(akSpeaker, none, "confessed")
    elseif(PromptKey == "maras_no_ai_cheating_persuade_fail")
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -15)
    elseif(PromptKey == "maras_no_ai_cheating_persuade_no_cheating")
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -25)
    elseif(PromptKey == "maras_no_ai_or_clarification_success")
        MARAS.SetPairIsOpen(TTM_Data.GetPlayer(), akSpeaker, true)
    elseif(PromptKey == "maras_no_ai_or_intimidate_success")
        pFDS.Intimidate(akSpeaker)
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -10)
        MARAS.SetPairIsOpen(TTM_Data.GetPlayer(), akSpeaker, true)
    elseif(PromptKey == "maras_no_ai_or_intimidate_fail")
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -25)
    elseif(PromptKey == "maras_no_ai_or_persuade_success")
        pFDS.Persuade(akSpeaker)
        MARAS.SetPairIsOpen(TTM_Data.GetPlayer(), akSpeaker, true)
    elseif(PromptKey == "maras_no_ai_or_persuade_fail")
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -15)
    elseif(PromptKey == "maras_no_ai_cr_clarification_success")
        MARAS.SetPairIsOpen(TTM_Data.GetPlayer(), akSpeaker, false)
    elseif(PromptKey == "maras_no_ai_cr_intimidate_success")
        pFDS.Intimidate(akSpeaker)
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -10)
        MARAS.SetPairIsOpen(TTM_Data.GetPlayer(), akSpeaker, false)
    elseif(PromptKey == "maras_no_ai_cr_intimidate_fail")
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -35)
    elseif(PromptKey == "maras_no_ai_cr_persuade_success")
        pFDS.Persuade(akSpeaker)
        MARAS.SetPairIsOpen(TTM_Data.GetPlayer(), akSpeaker, false)
    elseif(PromptKey == "maras_no_ai_cr_persuade_fail")
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -25)
    elseif(PromptKey == "maras_sex_walkin_cheating_sorry_yes" || PromptKey == "maras_sex_walkin_cheating_sorry_no" || PromptKey == "maras_sex_walkin_cheating_sorry_nothing")
        MARAS.SetCheatingDiscovered(player, none, "confronted")
        TTM_AdultUtils.FinishPlayerThread()
        TTM_NpcsScenes npcsScenes = TTM_Data.GetMarasNpcsScenesQuest() as TTM_NpcsScenes
        npcsScenes.FreePartnerNoticeCheatingPlayer(akSpeaker)
    elseif(PromptKey == "maras_sex_walkin_cheating_sorry_no_persuade_success")
        pFDS.Persuade(akSpeaker)
    elseif(PromptKey == "maras_sex_walkin_cheating_join_yes")
        TTM_NpcsScenes npcsScenes = TTM_Data.GetMarasNpcsScenesQuest() as TTM_NpcsScenes
        npcsScenes.FreePartnerNoticeCheatingPlayer(akSpeaker)
    elseif(PromptKey == "maras_sex_walkin_cheating_join_no")
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -20)
    elseif(PromptKey == "maras_sex_walkin_cheating_join_no_persuade_success")
        pFDS.Persuade(akSpeaker)
        TTM_NpcsScenes npcsScenes = TTM_Data.GetMarasNpcsScenesQuest() as TTM_NpcsScenes
        npcsScenes.FreePartnerNoticeCheatingPlayer(akSpeaker)
    elseif(PromptKey == "maras_sex_walkin_cheating_continue")
        MARAS.Breakup(player, akSpeaker)
        TTM_NpcsScenes npcsScenes = TTM_Data.GetMarasNpcsScenesQuest() as TTM_NpcsScenes
        npcsScenes.FreePartnerNoticeCheatingPlayer(akSpeaker)
    elseif(PromptKey == "maras_sex_walkin_or_join_yes")
        TTM_NpcsScenes npcsScenes = TTM_Data.GetMarasNpcsScenesQuest() as TTM_NpcsScenes
        npcsScenes.FreePartnerNoticeOpenRelPlayer(akSpeaker)
    elseif(PromptKey == "maras_sex_walkin_or_join_no")
        TTM_NpcsScenes npcsScenes = TTM_Data.GetMarasNpcsScenesQuest() as TTM_NpcsScenes
        npcsScenes.FreePartnerNoticeOpenRelPlayer(akSpeaker)
    elseif(PromptKey == "maras_sex_walkin_or_join_no_persuade_success")
        pFDS.Persuade(akSpeaker)
        TTM_NpcsScenes npcsScenes = TTM_Data.GetMarasNpcsScenesQuest() as TTM_NpcsScenes
        npcsScenes.FreePartnerNoticeOpenRelPlayer(akSpeaker)
    elseif(PromptKey == "maras_sex_walkin_or_join_no_persuade_failure")
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -10)
        TTM_NpcsScenes npcsScenes = TTM_Data.GetMarasNpcsScenesQuest() as TTM_NpcsScenes
        npcsScenes.FreePartnerNoticeOpenRelPlayer(akSpeaker)
    elseif(PromptKey == "maras_player_caught_cheater_join__persuade_success")
        pFDS.Persuade(akSpeaker)
    elseif(PromptKey == "maras_player_caught_cheater_join_failure" || PromptKey == "maras_player_caught_cheater_join__persuade_failure")
        TTM_ServiceAffection.AdjustPlayerPartnerAffection(akSpeaker, -10)
    elseif(PromptKey == "maras_no_ai_initiate_sex")
        TTM_AdultUtils.SetupQuickScene(akSpeaker, TTM_Data.GetPlayer(), false, true)
    elseif(PromptKey == "maras_non_marriage_management")
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
