scriptname TTM_Messages

Function PlayerNoHomeMsg() global
    Debug.Notification("$TTM_MSG_PlayerNoHome")
EndFunction

Function FreeSpouseFromPlayerHomeMsg(string spouseName) global
    Debug.Notification("$TTM_MSG_FreeSpouseFromHome{" + spouseName + "}")
EndFunction
