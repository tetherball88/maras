scriptname TTM_MCM_State

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


Function _SetMcmInt(string propName, int value = 0) global
    TTM_JMethods.SetIntValue(none, "MCMState." + propName, value)
    TTM_JMethods.ExportStorage()
EndFunction
int Function _GetMcmInt(string propName, int default = 0) global
    return TTM_JMethods.GetIntValue(none, "MCMState." + propName, default)
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

Function SetCurrentPage(string page) global
    TTM_JMethods.SetStringValue(none, "MCMState.currentPage", page)
EndFunction

string Function GetCurrentPage() global
    return TTM_JMethods.GetStringValue(none, "MCMState.currentPage", "Explore")
EndFunction

Function AddNpcOption(int id, Actor npc) global
    TTM_JMethods.SetFormValue(none, "MCMState.npcs." + id, npc)
    TTM_JMethods.ExportStorage()
EndFunction

Actor Function GetNpcOption(int id) global
    return TTM_JMethods.GetFormValue(none, "MCMState.npcs." + id) as Actor
EndFunction

Function SetSelectedNpc(Actor npc) global
    TTM_JMethods.SetFormValue(none, "MCMState.selectedNpc", npc)
EndFunction

Actor Function GetSelectedNpc() global
    return TTM_JMethods.GetFormValue(none, "MCMState.selectedNpc") as Actor
EndFunction

Function SetSearchValueNpc(string value) global
    TTM_JMethods.SetStringValue(none, "MCMState.searchValue", value)
EndFunction

string Function GetSearchValueNpc() global
    return TTM_JMethods.GetStringValue(none, "MCMState.searchValue", "")
EndFunction

Function Clean() global
    TTM_JMethods.ClearValue(none, "MCMState")
EndFunction

Function CleanSelectedNpc() global
    TTM_JMethods.SetFormValue(none, "MCMState.selectedNpc", none)
EndFunction

Function AddNpcTypeOption(int id, string type) global
    TTM_JMethods.StringListSet(none, "MCMState.npcTypesOptions", id, type)
EndFunction

string Function GetNpcTypeOption(int id) global
    return TTM_JMethods.StringListGet(none, "MCMState.npcTypesOptions", id)
EndFunction
