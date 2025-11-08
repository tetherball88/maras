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
    StorageUtil.SetIntValue(none, "MCMState." + propName, value)
EndFunction
int Function _GetMcmInt(string propName, int default = 0) global
    return StorageUtil.GetIntValue(none, "MCMState." + propName, default)
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
    StorageUtil.SetStringValue(none, "MCMState.currentPage", page)
EndFunction

string Function GetCurrentPage() global
    return StorageUtil.GetStringValue(none, "MCMState.currentPage", "Explore")
EndFunction

Function AddNpcOption(int id, Actor npc) global
    StorageUtil.SetFormValue(none, "MCMState.npcs." + id, npc)
EndFunction

Actor Function GetNpcOption(int id) global
    return StorageUtil.GetFormValue(none, "MCMState.npcs." + id) as Actor
EndFunction

Function SetSelectedNpc(Actor npc) global
    StorageUtil.SetFormValue(none, "MCMState.selectedNpc", npc)
EndFunction

Actor Function GetSelectedNpc() global
    return StorageUtil.GetFormValue(none, "MCMState.selectedNpc") as Actor
EndFunction

Function SetSearchValueNpc(string value) global
    StorageUtil.SetStringValue(none, "MCMState.searchValue", value)
EndFunction

string Function GetSearchValueNpc() global
    return StorageUtil.GetStringValue(none, "MCMState.searchValue", "")
EndFunction

Function Clean() global
    StorageUtil.ClearAllPrefix("MCMState")
EndFunction

Function CleanSelectedNpc() global
    StorageUtil.SetFormValue(none, "MCMState.selectedNpc", none)
EndFunction

Function AddNpcSkillTypeOption(int id, string type) global
    StorageUtil.SetStringValue(none, "MCMState.npcSkillTypesOptions"+id, type)
EndFunction

string Function GetNpcSkillTypeOption(int id) global
    return StorageUtil.GetStringValue(none, "MCMState.npcSkillTypesOptions"+id, "")
EndFunction

Function AddNpcSocialTypeOption(int id, string type) global
    StorageUtil.SetStringValue(none, "MCMState.npcSocialTypesOptions"+id, type)
EndFunction

string Function GetNpcSocialTypeOption(int id) global
    return StorageUtil.GetStringValue(none, "MCMState.npcSocialTypesOptions"+id, "")
EndFunction
