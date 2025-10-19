scriptname TTM_Debug

import TTM_JCDomain

Function log(string msg) global
    int logDestination = TTM_JData.GetLogDestination()
    if(logDestination == 0)
        JWriteToFile(msg)
    elseif(logDestination == 1)
        MiscUtil.PrintConsole(msg)
    elseif(logDestination == 2)
        Debug.Notification(msg)
    endif
EndFunction

Function trace(string msg) global
    int logLevel = TTM_JData.GetLogLevel()
    if(isTrace())
        log("[maras(trace)]: " + msg)
    endif
EndFunction

Function debug(string msg) global
    int logLevel = TTM_JData.GetLogLevel()
    if(logLevel <= 1)
        log("[maras(debug)]: " + msg)
    endif
EndFunction

Function warn(string msg) global
    int logLevel = TTM_JData.GetLogLevel()
    if(logLevel <= 2)
        log("[maras(warn)]: " + msg)
    endif
EndFunction

Function err(string msg) global
    int logLevel = TTM_JData.GetLogLevel()
    if(logLevel <= 3)
        log("[maras(err)]: " + msg)
    endif
EndFunction

Function CleanOnLoad() global
    MiscUtil.WriteToFile("MARAS_Log.txt", "", false, false)
EndFunction

Function JWriteToFile(string msg) global
    MiscUtil.WriteToFile("MARAS_Log.txt", "\n" + msg, true, true)
EndFunction

bool Function IsTrace() global
    int logLevel = TTM_JData.GetLogLevel()
    return logLevel == 0
EndFunction
