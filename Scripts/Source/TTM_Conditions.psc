scriptname TTM_Conditions extends Quest conditional

Bool Property UseQuestDialAI = false auto conditional hidden
Bool Property UseMiscDialAI = false auto conditional hidden

GlobalVariable Property TTM_MCM_UseAIMiscDial auto
GlobalVariable Property TTM_MCM_UseAIQuestDial auto


Function Maintenance()
    CheckUseMiscDialogAI()
    CheckUseQuestDialogAI()
EndFunction

Function CheckUseQuestDialogAI()
    UseQuestDialAI = TTM_JData.GetHasSkyrimNet() && TTM_MCM_UseAIQuestDial.GetValue() == 1
    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("TTM_Conditions:CheckUseQuestDialogAI:" + UseQuestDialAI)
    endif
EndFunction

Function CheckUseMiscDialogAI()
    UseMiscDialAI = TTM_JData.GetHasSkyrimNet() && TTM_MCM_UseAIMiscDial.GetValue() == 1
    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("TTM_Conditions:CheckUseMiscDialogAI:" + UseMiscDialAI)
    endif
EndFunction
