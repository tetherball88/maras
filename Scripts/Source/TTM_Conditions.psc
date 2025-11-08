scriptname TTM_Conditions extends Quest conditional

Bool Property AllowAIDial = false auto conditional hidden
Bool Property PreferVanillaAudio = false auto conditional hidden

GlobalVariable Property TTM_MCM_PreferVanillaAudio auto
GlobalVariable Property TTM_MCM_AllowAIDial auto

Function Maintenance()
    if(TTM_MCM_PreferVanillaAudio == none)
        TTM_MCM_PreferVanillaAudio = Game.GetFormFromFile(0x18, "TT_MARAS.esp") as GlobalVariable
    endif
    if(TTM_MCM_AllowAIDial == none)
        TTM_MCM_AllowAIDial = Game.GetFormFromFile(0x1c, "TT_MARAS.esp") as GlobalVariable
    endif
    bool hasSkyrimNet = TTM_Data.GetHasSkyrimNet()
    AllowAIDial = hasSkyrimNet && TTM_MCM_AllowAIDial.GetValue() == 1
    PreferVanillaAudio = TTM_MCM_PreferVanillaAudio.GetValue() == 1
EndFunction
