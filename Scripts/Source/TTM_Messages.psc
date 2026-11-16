scriptname TTM_Messages

Function PlayerNoHomeMsg() global
    MARAS.NotifyLocalized("hud.player_no_home")
EndFunction

Function FreeSpouseFromPlayerHomeMsg(string spouseName) global
    MARAS.NotifyLocalized("hud.free_spouse_from_home", spouseName)
EndFunction
