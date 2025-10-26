scriptname TTM_RequestLLMDialogue

Function RequestDialogue(string prompt, string values, Actor akSpeaker, Actor akTarget = none) global
    if(!TTM_JData.GetHasSkyrimNet())
        return
    endif

    ; int res = SkyrimNetApi.SendCustomPromptToLLM("enable_candidate", 0.7, 1000, "TTM_RequestLLMDialogue", "OnReceiveLLMResponse")

    ; TTM_Debug.trace("Sent LLM Dialogue Request with result: " + res)

    SkyrimNetApi.DirectNarration(SkyrimNetApi.ParseString(prompt, "values", values), akSpeaker, TTM_JData.GetPlayer())
EndFunction

; Function OnReceiveLLMResponse(string response) global
;     TTM_Debug.trace("LLM Response Received: " + response)
; EndFunction
