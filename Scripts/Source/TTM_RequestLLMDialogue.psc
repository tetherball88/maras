scriptname TTM_RequestLLMDialogue

Function RequestDialogue(string prompt, string values, Actor akSpeaker, Actor akTarget = none) global
    if(!TTM_JData.GetHasSkyrimNet())
        return
    endif
    int origDelay = UI.GetInt("Dialogue Menu", "_global.DialogueMenu.ALLOW_PROGRESS_DELAY")

    SkyrimNetApi.DirectNarration(SkyrimNetApi.ParseString(prompt, "values", values), akSpeaker, TTM_JData.GetPlayer())

    if(UI.IsMenuOpen("Dialogue Menu"))
        ; UI.Invoke("Dialogue Menu", "_root.DialogueMenu_mc.TopicList.ClearList")
        ; Utility.Wait(5)
        ; UI.Invoke("Dialogue Menu", "_root.DialogueMenu_mc.PopulateDialogueLists")
        ; TTM_Debug.trace("TTM_RequestLLMDialogue:RequestDialogue:Status:"+ UI.GetInt("Dialogue Menu", "_root.DialogueMenu_mc.eMenuState") )
        ; UI.SetBool("Dialogue Menu", "_root.DialogueMenu_mc.eMenuState", 4)

        ; TTM_Debug.trace("TTM_RequestLLMDialogue:RequestDialogue:Disabling Progress:" + UI.GetBool("Dialogue Menu", "_root.DialogueMenu_mc.bAllowProgress"))
        ; UI.SetBool("Dialogue Menu", "_root.DialogueMenu_mc.bAllowProgress", true)
        UI.SetInt("Dialogue Menu", "_global.DialogueMenu.ALLOW_PROGRESS_DELAY", 5000)
        UI.Invoke("Dialogue Menu", "_root.DialogueMenu_mc.StartProgressTimer")
        ; TTM_Debug.trace("TTM_RequestLLMDialogue:RequestDialogue:Disabling Progress:" + UI.GetBool("Dialogue Menu", "_root.DialogueMenu_mc.bAllowProgress"))
        UI.SetBool("Dialogue Menu", "_root.DialogueMenu_mc.TopicListHolder._visible", false)
        TTM_Debug.trace("TTM_RequestLLMDialogue:RequestDialogue:" + UI.GetInt("Dialogue Menu", "_global.DialogueMenu.ALLOW_PROGRESS_DELAY"))
        UI.InvokeString("Dialogue Menu", "_root.DialogueMenu_mc.SubtitleText.SetText", "thinking...")
        Utility.wait(5)
        ; UI.SetBool("Dialogue Menu", "_root.DialogueMenu_mc.eMenuState", 2)
        UI.SetBool("Dialogue Menu", "_root.DialogueMenu_mc.TopicListHolder._visible", true)
        ; UI.SetBool("Dialogue Menu", "_root.DialogueMenu_mc.bAllowProgress", true)
        UI.Invoke("Dialogue Menu", "_root.DialogueMenu_mc.SetAllowProgress")
        UI.SetInt("Dialogue Menu", "_global.DialogueMenu.ALLOW_PROGRESS_DELAY", origDelay)

        ; UI.SetInt("Dialogue Menu", "_global.DialogueMenu.ALLOW_PROGRESS_DELAY", 5000)
        ; TTM_Debug.trace("TTM_RequestLLMDialogue:RequestDialogue:Starting Progress Timer")
        ; ui.Invoke("Dialogue Menu", "_root.DialogueMenu_mc.StartProgressTimer")
        ;
        ; TTM_Debug.trace("TTM_RequestLLMDialogue:RequestDialogue:Allowing Progress")
        ; ui.Invoke("Dialogue Menu", "_root.DialogueMenu_mc.SetAllowProgress")
    endif



EndFunction
