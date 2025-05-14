scriptname TTM_Debug

import TTM_JCDomain

Function log(string msg) global
    int logDestination = TTM_MCM_State.GetLogDestination()
    if(logDestination == 0)
        JWriteToFile(msg)
    elseif(logDestination == 1)
        MiscUtil.PrintConsole(msg)
    elseif(logDestination == 2)
        Debug.Notification(msg)
    endif
EndFunction

Function trace(string msg) global
    int logLevel = TTM_MCM_State.GetLogLevel()
    if(logLevel == 0)
        log("[maras(trace)]: " + msg)
    endif
EndFunction

Function debug(string msg) global
    int logLevel = TTM_MCM_State.GetLogLevel()
    if(logLevel <= 1)
        log("[maras(debug)]: " + msg)
    endif
EndFunction

Function warn(string msg) global
    int logLevel = TTM_MCM_State.GetLogLevel()
    if(logLevel <= 2)
        log("[maras(warn)]: " + msg)
    endif
EndFunction

Function err(string msg) global
    int logLevel = TTM_MCM_State.GetLogLevel()
    if(logLevel <= 3)
        log("[maras(err)]: " + msg)
    endif
EndFunction

Function SetupLogger() global
    JDB_solveObjSetter(TTM_JData.GetNamespaceKey() + ".logger", JArray_object(), true)
EndFunction

Function JWriteToFile(string msg) global
    int JLogs = JDB_solveObj(TTM_JData.GetNamespaceKey() + ".logger")
    ; keep maximum 100 logs
    if(JArray_count(JLogs) >= 100)
        JArray_eraseIndex(JLogs, 0)
    endif
    float ts = Utility.GetCurrentRealTime()
    int entry = JMap_object()
    JMap_setFlt(entry, "time", ts)
    JMap_setStr(entry, "msg", msg)
    JArray_addObj(JLogs, entry)
    JValue_release(entry)
EndFunction
