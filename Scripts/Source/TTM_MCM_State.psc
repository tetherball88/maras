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
    StorageUtil.SetIntValue(none, "TTM_PermanentMCMState." + propName, value)
EndFunction
int Function _GetMcmInt(string propName, int default = 0) global
    return StorageUtil.GetIntValue(none, "TTM_PermanentMCMState." + propName, default)
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
    StorageUtil.SetStringValue(none, "TTM_MCMState.currentPage", page)
EndFunction

string Function GetCurrentPage() global
    return StorageUtil.GetStringValue(none, "TTM_MCMState.currentPage", "Explore")
EndFunction

Function AddNpcOption(int id, Actor npc) global
    StorageUtil.SetFormValue(none, "TTM_MCMState.npcs." + id, npc)
EndFunction

Actor Function GetNpcOption(int id) global
    return StorageUtil.GetFormValue(none, "TTM_MCMState.npcs." + id) as Actor
EndFunction

Function SetSelectedNpc(Actor npc) global
    StorageUtil.SetFormValue(none, "TTM_MCMState.selectedNpc", npc)
EndFunction

Actor Function GetSelectedNpc() global
    return StorageUtil.GetFormValue(none, "TTM_MCMState.selectedNpc") as Actor
EndFunction

Function SetSearchValueNpc(string value) global
    StorageUtil.SetStringValue(none, "TTM_MCMState.searchValue", value)
EndFunction

string Function GetSearchValueNpc() global
    return StorageUtil.GetStringValue(none, "TTM_MCMState.searchValue", "")
EndFunction

Function Clean() global
    StorageUtil.ClearAllPrefix("TTM_MCMState")
EndFunction

Function CleanSelectedNpc() global
    StorageUtil.SetFormValue(none, "TTM_MCMState.selectedNpc", none)
EndFunction

Function AddNpcSkillTypeOption(int id, string type) global
    StorageUtil.SetStringValue(none, "TTM_MCMState.npcSkillTypesOptions"+id, type)
EndFunction

string Function GetNpcSkillTypeOption(int id) global
    return StorageUtil.GetStringValue(none, "TTM_MCMState.npcSkillTypesOptions"+id, "")
EndFunction

Function AddNpcSocialTypeOption(int id, string type) global
    StorageUtil.SetStringValue(none, "TTM_MCMState.npcSocialTypesOptions"+id, type)
EndFunction

string Function GetNpcSocialTypeOption(int id) global
    return StorageUtil.GetStringValue(none, "TTM_MCMState.npcSocialTypesOptions"+id, "")
EndFunction

bool Function GetConfirmMcm(string type) global
    int val = _GetMcmInt(type, -1)
    if(val == -1)
        if(type == "ConfirmAcceptProposal")
           return true
        else
            return false
        endif
    endif
    return val == 1
EndFunction

Function SetConfirmAcceptProposal(bool val) global
    return _SetMcmBool("ConfirmAcceptProposal", val)
EndFunction
bool Function GetConfirmAcceptProposal() global
    return GetConfirmMcm("ConfirmAcceptProposal")
EndFunction

Function SetConfirmBreakupEngagement(bool val) global
    return _SetMcmBool("ConfirmBreakupEngagement", val)
EndFunction
bool Function GetConfirmBreakupEngagement() global
    return GetConfirmMcm("ConfirmBreakupEngagement")
EndFunction

Function SetConfirmDivorce(bool val) global
    return _SetMcmBool("ConfirmDivorce", val)
EndFunction
bool Function GetConfirmDivorce() global
    return GetConfirmMcm("ConfirmDivorce")
EndFunction

Function SetConfirmDivorceDuringLowAffectionQuest(bool val) global
    return _SetMcmBool("ConfirmDivorceDuringLowAffectionQuest", val)
EndFunction
bool Function GetConfirmDivorceDuringLowAffectionQuest() global
    return GetConfirmMcm("ConfirmDivorceDuringLowAffectionQuest")
EndFunction
