scriptname TTM_Debug

Function log(string msg, int severity = 2) global
    int logDestination = MARAS.GetSettingInt("logDestination")
    if(logDestination == 0)
        WriteToFile(msg, severity)
    elseif(logDestination == 1)
        MiscUtil.PrintConsole(msg)
    elseif(logDestination == 2)
        Debug.Notification(msg)
    endif
EndFunction

Function trace(string msg) global
    int logLevel = MARAS.GetLogLevel()
    if(logLevel == 0)
        log("[maras(trace)]: " + msg, 0)
    endif
EndFunction

Function debug(string msg) global
    int logLevel = MARAS.GetLogLevel()
    if(logLevel <= 1)
        log("[maras(debug)]: " + msg, 1)
    endif
EndFunction

Function info(string msg) global
    int logLevel = MARAS.GetLogLevel()
    if(logLevel <= 2)
        log("[maras(info)]: " + msg, 2)
    endif
EndFunction

Function warn(string msg) global
    int logLevel = MARAS.GetLogLevel()
    if(logLevel <= 3)
        log("[maras(warn)]: " + msg, 3)
    endif
EndFunction

Function err(string msg) global
    int logLevel = MARAS.GetLogLevel()
    if(logLevel <= 4)
        log("[maras(err)]: " + msg, 4)
    endif
EndFunction


Function WriteToFile(string msg, int severity = 2) global
    MARAS.Log(msg, severity)
EndFunction
