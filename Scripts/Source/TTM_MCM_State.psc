scriptname TTM_MCM_State

import TTM_JCDomain

;/
{
    MCM: {
        currentPage: string
        spouses: {
            [optionId]: Actor
        }
        spouseTypesOptions: {
            [optionId]: string
        }
        selectedSpouse: Actor
    }
}
/;

int Function GetJMCM() global
    int res = JDB_solveObj(".TT_MARAS.MCM")
    if(!res)
        res = JMap_object()
        JDB_solveObjSetter(".TT_MARAS.MCM", res, true)
    endif

    return res
EndFunction

Function _SetMcmInt(string propName, int value = 0) global
    JDB_solveIntSetter(".TT_MARAS.saveData.MCM.settings" + "." + propName, value, true)
EndFunction

int Function _GetMcmInt(string propName, int default = 0) global
    return JDB_solveInt(".TT_MARAS.saveData.MCM.settings" + "." + propName, default)
EndFunction

Function _SetMcmBool(string propName, bool flag = true) global
    if(flag)
        _SetMcmInt(propName, 1)
    else
        _SetMcmInt(propName, 0)
    endif
EndFunction

bool Function _GetMcmBool(string propName) global
    return _GetMcmInt(propName) == 1
EndFunction

Function SetLogLevel(int logLevel) global
    _SetMcmInt("logLevel", logLevel)
EndFunction

int Function GetLogLevel() global
    int logLevel = _GetMcmInt("logLevel", -1)
    if(logLevel == -1)
        return 2
    endif
    return logLevel
EndFunction

Function SetLogDestination(int logDestination) global
    _SetMcmInt("logDestination", logDestination)
EndFunction

int Function GetLogDestination() global
    int logDestination = _GetMcmInt("logDestination", -1)
    if(logDestination == -1)
        return 0
    endif
    return logDestination
EndFunction

Function SetSkipWedding(bool toggle) global
    _SetMcmBool("skipWedding", toggle)
EndFunction

bool Function GetSkipWedding() global
    return _GetMcmBool("skipWedding")
EndFunction

Function SetCurrentPage(string page) global
    JMap_setStr(GetJMCM(), "currentPage", page)
EndFunction

string Function GetCurrentPage() global
    string page = JMap_getStr(GetJMCM(), "currentPage")
    if(page != "")
        return page
    endif

    return "Explore"
EndFunction

int Function GetSpouseOptions() global
    return TTM_JUtils._GetOrCreateJMap(GetJMCM(), "spouses")
EndFunction

Function AddSpouseOption(int id, Actor spouse) global
    JMap_setForm(GetSpouseOptions(), id, spouse)
EndFunction

Actor Function GetSpouseOption(int id) global
    return JMap_getForm(GetSpouseOptions(), id) as Actor
EndFunction

Function SetSelectedSpouse(Actor spouse) global
    JMap_setForm(GetJMCM(), "selectedSpouse", spouse)
EndFunction

Actor Function GetSelectedSpouse() global
    return JMap_getForm(GetJMCM(), "selectedSpouse") as Actor
EndFunction

Function SetSearchValueSpouse(string value) global
    JMap_setStr(GetJMCM(), "searchValue", value)
EndFunction

string Function GetSearchValueSpouse() global
    return JMap_getStr(GetJMCM(), "searchValue")
EndFunction

Function Clean() global
    JDB_solveObjSetter(".TT_MARAS.MCM", JMap_object(), true)
EndFunction

Function CleanSelectedSpouse() global
    JMap_setForm(GetJMCM(), "selectedSpouse", none)
EndFunction

int Function GetSpouseTypeOptions() global
    return TTM_JUtils._GetOrCreateJMap(GetJMCM(), "spouseTypesOptions")
EndFunction

Function AddSpouseTypeOption(int id, string type) global
    JMap_setStr(GetSpouseTypeOptions(), id, type)
EndFunction

string Function GetSpouseTypeOption(int id) global
    return JMap_getStr(GetSpouseTypeOptions(), id)
EndFunction
