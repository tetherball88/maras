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
    float start = Utility.GetCurrentRealTime()
    TTM_Debug.trace("TTM_DialogueManager:OnBegin:"+PromptKey+":useAI:"+UseAI)
    string prompt = ""
    string values = "{\"npcName\": \"" + TTM_Utils.GetActorName(akSpeaker) + "\", \"playerName\": \"" + TTM_Utils.GetActorName(TTM_JData.GetPlayer()) + "\""
    int affectionRank = TTM_ServiceAffection.GetAffectionRank(akSpeaker)
    ; string affectionPrompt = ""
    ; if (affectionRank >= 75)
    ;     affectionPrompt += "{{values.npcName}} feels deeply attached to {{values.playerName}} and speaks warmly, playfully, and openly.";
    ; elseif (affectionRank >= 50)
    ;     affectionPrompt += "{{values.npcName}} feels generally positive toward {{values.playerName}} and speaks in a friendly, cooperative, trusting tone.";
    ; elseif (affectionRank >= 25)
    ;     affectionPrompt += "{{values.npcName}} has mixed feelings about {{values.playerName}} and speaks cautiously, probing for reassurance or clarity.";
    ; else
    ;     affectionPrompt += "{{values.npcName}} feels hurt or guarded around {{values.playerName}} and speaks with distance, brevity, or cool restraint.";
    ; endif
    if(PromptKey == "maras_enable_candidate")
        if(!TTM_Utils.IsTracking(akSpeaker))
            akSpeaker.AddToFaction(TTM_JData.GetTrackedNpcFaction())
        endif
        TTM_Utils.SendRelationshipChangeEvent(akSpeaker, "candidate")
        prompt = "{{values.npcName}} gives a response to player's hint of romantic interest. The reply is in-character, acknowledges the intent and contains no questions. It isn't marriage proposal yet."
    elseif(StringUtil.Find(PromptKey, "maras_enable_polygamy_") == 0)
        prompt = GetEnablePolygamyQstDialogues(PromptKey)
    elseif(StringUtil.Find(PromptKey, "maras_misc_") == 0)
        prompt = GetMiscDialogue(PromptKey)
        ; prompt = affectionPrompt + "\n" + prompt
    elseif(StringUtil.Find(PromptKey, "manage_spouse_") == 0)
        prompt = GetManageSpouseDialogue(PromptKey, akSpeaker)
    elseif(StringUtil.Find(PromptKey, "maras_affection_estranged_divorce_") == 0)
        prompt = GetEstrangedDivorceDialogue(PromptKey, akSpeaker)
    elseif(PromptKey == "maras_reconcile_accept")
        TTM_Utils.SendRelationshipChangeEvent(akSpeaker, "engaged")
        ; player apologised and npc accepted reconciliation
        prompt = "{{values.npcName}} responds warmly and forgivingly when {{values.playerName}} apologizes and seeks reconciliation. Response should be 8-15 words, expressing understanding and a willingness to move forward together."
    elseif(PromptKey == "maras_breakup")
        if(TTM_Utils.IsFiance(akSpeaker))
            TTM_Utils.SendRelationshipChangeEvent(akSpeaker, "jilted")
        elseif(TTM_Utils.IsSpouse(akSpeaker))
            TTM_Utils.SendRelationshipChangeEvent(akSpeaker, "divorced")
        endif
        prompt = "{{values.npcName}} responds with anger and hurt when {{values.playerName}} ends their relationship. Response should be 5-12 words, demanding the player leave and never return."
    else
        TTM_Debug.warn("TTM_DialogueManager:OnBegin:UnhandledKey:"+PromptKey)
    endif

    values += "}"

    if(useAI && prompt != "")
        TTM_ServiceSkyrimNet.DirectNarration(SkyrimNetApi.ParseString(prompt, "values", values), akSpeaker, TTM_JData.GetPlayer())
    endif
    TTM_Debug.trace("TTM_DialogueManager:OnBegin:FinishIn"+(Utility.GetCurrentRealTime() - start)+"s")
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

string Function GetEnablePolygamyQstDialogues(string promptKey)
    if(PromptKey == "maras_enable_polygamy_maramal_start")
        self.GetOwningQuest().SetStage(10)
        return "As Maramal, acknowledge the {{values.playerName}} Dragonborn's divine nature. Explain that traditional marriage rites are meant for singular mortal souls. Suggest that the Temple of Dibella, which holds broader interpretations of love and bonding, may have the insight the Dragonborn needs. Point the {{values.playerName}} to the priestesses in Markarth."
    elseif(PromptKey == "maras_enable_polygamy_senna_start")
        self.GetOwningQuest().SetStage(20)
        return "As Senna, express curiosity and reverence. Share a legend about the Scroll of Alessia's Grace, which speaks of Dragonborn-like beings able to love without limit. Say it may rest in Ragnvald, an ancient ruin in the Reach. Mark the location on the map. Send {{values.playerName}} to find the scroll."
    elseif(PromptKey == "maras_enable_polygamy_senna_finish")
        self.GetOwningQuest().SetStage(40)
        return "As Senna, examine the scroll and recognize it as written in an old dialect no longer taught. Suggest the College of Winterhold - specifically Urag gro-Shub - as the best hope for translation. Send {{values.playerName}} to Urag gro-Shub."
    elseif(PromptKey == "maras_enable_polygamy_urag_start")
        self.GetOwningQuest().SetStage(50)
        return "As Urag gro-Shub, grumble but agree. Recognize the script as ancient Alessian theological text. Say you'll need a day to translate it. Ask the {{values.playerName}} to return later."
    elseif(PromptKey == "maras_enable_polygamy_urag_finish")
        self.GetOwningQuest().SetStage(55)
        return "As Urag gro-Shub, hand over the translated scroll and summarize it. The scroll affirms that souls touched by divine fire - such as Dragonborn - may love and bind without harm or division. Suggest bringing it back to Mara's temple if spiritual sanction is needed."
    elseif(PromptKey == "maras_enable_polygamy_maramal_finish")
        self.GetOwningQuest().SetStage(70)
        return "As Maramal, read the scroll with solemnity. Do not ask questions. Acknowledge its authenticity and declare that the Temple cannot ignore doctrine rooted in divine precedent. State that Mara's blessing requires honesty - no bond may be expanded in secrecy. Instruct the {{values.playerName}} to speak with their spouse before the Divine Exception is granted. Speak with calm authority."
    elseif(PromptKey == "maras_enable_polygamy_current_spouse")
        self.GetOwningQuest().SetStage(80)
        return "As {{values.npcName}}, {{values.playerName}}'s spouse, respond to their explanation with sincerity. {{values.playerName}} tells you they spoke to Mara's priest, then the priestesses of Dibella, and uncovered an ancient scroll written during the early Alessian era — one that speaks of Dragonborn souls touched by divine fire, capable of holding multiple sacred bonds. They explain that Mara's priest, Maramal, acknowledged this doctrine and is willing to bless it as a Divine Exception — but only if their existing bond remains whole and honest. You are not being replaced or cast aside. React according to your nature: if supportive, express understanding and trust. If cautious, voice your feelings but do not reject them outright. This is about affirming love, not competition. Speak from the heart."
    endif

    return ""
EndFunction

string Function GetMiscDialogue(string promptKey)
    if(PromptKey == "maras_misc_hello_spouse")
        return "As the {{player.name}}'s spouse, say a short, affectionate greeting. Keep it under one sentence. Offer help or attention in a loving, familiar tone - like \"Yes, my love?\" or \"What do you need, dear?\""
    elseif(PromptKey == "maras_misc_hello_spouse_new_home")
        return "As the {{player.name}}'s spouse, greet them briefly after moving into their new home. Say no more than two short sentences. Express approval of the house, and optionally mention starting a small store or routine to stay busy. Keep it warm and content."
    elseif(PromptKey == "maras_misc_hello_fiance")
        return "{{values.fianceName}} is engaged to {{values.playerName}}. Generate a single warm, excited sentence reminding {{values.playerName}} to arrange the wedding at the Temple of Mara."
    elseif(PromptKey == "maras_misc_hello_fiance_before_wedding")
        return "{{values.npcName}} is engaged to {{values.playerName}} and the wedding is scheduled. Generate a single short, warm, anticipatory sentence acknowledging the upcoming wedding, like \"Our wedding is soon. I'll see you there!\"."
    elseif(PromptKey == "maras_misc_goodbye_spouse")
        return "As the {{values.playerName}}'s spouse, say a short, affectionate goodbye. Keep it under one sentence. Tone should be warm and familiar - use terms like \"love\" or \"dear\" - like \"Goodbye, my love.\" or \"I'll be here, my dear.\""
    endif

    return ""
EndFunction

string Function GetManageSpouseDialogue(string promptKey, Actor akSpeaker)
    if(PromptKey == "manage_spouse_set_new_home_ask" && UseAI)
        TTM_ServicePlayerHouse.ChooseHomeForSpouseMsg(akSpeaker)
        return "{{values.npcName}} briefly asks where {{values.playerName}} wants to move, showing slight surprise but willingness. Response should be 5-12 words, ending with a simple question about the destination."
    elseif(PromptKey == "manage_spouse_un_set_new_home")
        TTM_ServicePlayerHouse.ReleaseSpouseFromPlayerHome(akSpeaker)
        return "{{values.npcName}} responds casually when {{values.playerName}} releases them from staying at an assigned home. Response should be 4-8 words, indicating availability at their original location."
    elseif(PromptKey == "manage_spouse_check_spouse_home")
        TTM_ServiceSpouseAssets.StartShareHomeWithPlayer(akSpeaker)
        return "{{values.npcName}} responds positively to {{values.playerName}}'s request to access their house, expressing that their property is now shared due to their marriage. Response should be 5-10 words, conveying welcome and openness."
    elseif(PromptKey == "manage_spouse_leave_spouse_home")
        TTM_ServiceSpouseAssets.StopShareHomeWithPlayer(akSpeaker)
        return "{{values.npcName}} responds casually when {{values.playerName}} states they won't be using the spouse's house. Response should be 4-8 words, showing simple acknowledgment."
    elseif(PromptKey == "maras_no_ai_engagement")
        TTM_Utils.SendRelationshipChangeEvent(akSpeaker, "engaged")
    elseif(PromptKey == "manage_spouse_cooking")
        TTM_JData.GetPlayer().AddItem(FoodMarriageMeal)
        return "{{values.npcName}} responds to {{values.playerName}}'s request for food by offering a meal with a brief comment about it. Response should be 12-20 words, mentioning the food, possibly its qualities, and stating they'll cook again tomorrow."
    elseif(PromptKey == "manage_spouse_store")
        TTM_ServiceNpcs.ShareIncome(akSpeaker)
        return "{{values.npcName}} responds positively when {{values.playerName}} asks about store profits, mentioning they've made money and offering the player's share. Response should be 6-15 words, including a term of endearment like \"love\" or \"dear.\""
    elseif(PromptKey == "manage_spouse_set_hierarchy_0")
        TTM_ServiceNpcs.ChangeSpouseRank(akSpeaker, 0)
        ; write prompt for spouse responding to being set to rank 1,
        return "{{values.npcName}} responds with surprise and curiosity when {{values.playerName}} sets them to rank 1. Response should be 5-10 words, expressing interest in their new role."
    elseif(PromptKey == "manage_spouse_set_hierarchy_1")
        TTM_ServiceNpcs.ChangeSpouseRank(akSpeaker, 1)
        return "{{values.npcName}} responds with intrigue and excitement when {{values.playerName}} sets them to rank 2. Response should be 5-10 words, expressing eagerness for their new responsibilities."
    elseif(PromptKey == "manage_spouse_set_hierarchy_2")
        TTM_ServiceNpcs.ChangeSpouseRank(akSpeaker, 2)
        return "{{values.npcName}} responds with determination and readiness when {{values.playerName}} sets them to rank 3. Response should be 5-10 words, expressing confidence in their new role."
    elseif(PromptKey == "manage_spouse_set_new_home_ask")
        return "{{values.npcName}} briefly asks where {{values.playerName}} wants to move, showing slight surprise but willingness. Response should be 5-12 words, ending with a simple question about the destination."
    elseif(PromptKey == "manage_spouse_set_new_home_do")
        return "{{values.npcName}} briefly responds with agreement to {{values.playerName}}'s selected home location, showing readiness to relocate. Response should be 3-8 words, conveying they will meet the player at the new location."
    endif

    return ""
EndFunction

string Function GetEstrangedDivorceDialogue(string promptKey, Actor akSpeaker)
    if(PromptKey == "maras_affection_estranged_divorce_persuasion_success" \
        || PromptKey == "maras_affection_estranged_divorce_intimidation_success")
        ; player persuaded or intimidated spouse to agree to not divorce
        FavorDialogueScript pFDS = TTM_JData.GetDialogueFavorQuest() as FavorDialogueScript
        if(PromptKey == "maras_affection_estranged_divorce_intimidation_success")
            pFDS.Intimidate(akSpeaker)
        else
            pFDS.Persuade(akSpeaker)
        endif
        GetOwningQuest().SetStage(100)
    elseif(PromptKey == "maras_affection_estranged_divorce_persuasion_fail" \
        || PromptKey == "maras_affection_estranged_divorce_intimidation_fail"\
        || PromptKey == "maras_affection_estranged_divorce_let_go")
        ; player didn't persuade or intimidate or just let go spouse and proceed with divorce
        FavorDialogueScript pFDS = TTM_JData.GetDialogueFavorQuest() as FavorDialogueScript
        TTM_Debug.trace("TTM_DialogueManager:OnBegin:DivorceFail:"+GetOwningQuest())
        GetOwningQuest().SetStage(200)
    elseif(PromptKey == "maras_affection_estranged_divorce_intro")
        ; player is using SkyrimNet to sway spouse to divorce or keep marriage
        GetOwningQuest().SetStage(10)
        return "As {{values.npcName}}, you are on verge of divorce. {{values.playerName}} approaches you to discuss your feelings. Be honest about your needs and that you need to feel valued again."
    endif

    return ""
EndFunction
