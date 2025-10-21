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
    if(logLevel == 0)
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

Function JWriteToFile(string msg) global
    MiscUtil.WriteToFile("MARAS_Log.txt", msg, true, true)
EndFunction
