scriptname TTM_QuestTracker extends Quest

import PO3_Events_Form
import TTM_JCDomain

Function Maintenance()
    ImportJson()
    RegisterQuests()
EndFunction

Function RegisterQuests()

    string[] allQstKeys = GetAllQuests()
    int i = 0
    while(i < allQstKeys.Length)
        ; on quest finish its form resets to none in jcontainer
        ; store them as strings and decode to form here
        Quest qst = JString.decodeFormStringToForm(allQstKeys[i]) as Quest
        RegisterQuest(qst)
        i += 1
    endwhile
EndFunction

Function RegisterQuest(Quest qst)
    string[] allStages = GetAllQuestStages(qst)
    bool allStagesPassed = true
    int i = 0

    TTM_Debug.trace("TTM_QuestTracker:RegisterQuest:CHECK:"+qst.GetName()+"; stagesCount:"+allStages.Length)

    while(i < allStages.Length)
        string stage = allStages[i]
        if(stage == "start" || stage == "stop")
            RegisterForQuest(self, qst)
        elseif(!qst.IsStageDone(stage as int))
            allStagesPassed = false
        ; if used in ongoing game check if this quest stage is completed
        else
            ProcessStage(qst, stage, true)
        endif
        i += 1
    endwhile

    ; todo we can't remove quest from jcontainers if it is repeatable
    ; check if all tracked stages for this quest already passed, we can remove whole quest from tracked
    if(allStagesPassed)
        TTM_Debug.trace("TTM_QuestTracker:RegisterQuest:SKIP:"+qst.GetName())
        ; TTM_JData.RemoveQuest(qst)
    ; otherwise subscribe to quest stage changes
    else
        TTM_Debug.trace("TTM_QuestTracker:RegisterQuest:DONE:"+qst.GetName())
        RegisterForQuestStage(self, qst)
    endif
EndFunction

Function ProcessStage(Quest qst, string stage, bool onLoad = false)
    string stageAction = GetQstStageAction(qst, stage)
    bool stageProcessOnLoad = GetQstStageCheckOnLoad(qst, stage) == 1

    if(onLoad && !stageProcessOnLoad)
        TTM_Debug.trace("TTM_QuestTracker:ProcessStage:SKIP:"+stage+":action:"+stageAction)
        return
    endif

    TTM_Debug.trace("TTM_QuestTracker:Quest:"+qst.GetName()+":ProcessStage:"+stage+":action:"+stageAction)

    if(stageAction == "weddingFinish")
        TTM_ServiceMarriageQuest.OnWeddingQstFinish()
    elseif(stageAction == "weddingCancel")
        TTM_ServiceMarriageQuest.OnWeddingQstCancel()
    elseif(stageAction == "marriageFinStart")
        TTM_ServiceMarriageQuest.OnMarriageFinQstStart()
    elseif(stageAction == "marriageMainStop")
        TTM_ServiceMarriageQuest.OnMarriageQuestStop()
    endif

EndFunction

Event OnQuestStageChange(Quest akQuest, Int aiNewStage)
    TTM_Debug.trace("TTM_QuestTracker:OnQuestStageChange:"+akQuest.GetName()+"; stage:"+aiNewStage)
    ProcessStage(akQuest, aiNewStage)
EndEvent

Event OnQuestStart(Quest akQuest)
    TTM_Debug.trace("TTM_QuestTracker:OnQuestStageChange:"+akQuest.GetName()+"; stage:START")
    ProcessStage(akQuest, "start")
EndEvent

Event OnQuestStop(Quest akQuest)
    TTM_Debug.trace("TTM_QuestTracker:OnQuestStageChange:"+akQuest.GetName()+"; stage:STOP")
    ProcessStage(akQuest, "stop")
EndEvent


;/ ==============================
   SECTION: JContainers functions
============================== /;
Function ImportJson()
    TTM_JMethods.ImportDataFromFile("quests","Data/SKSE/Plugins/MARAS/quests", true)
EndFunction

int Function GetQsts()
    return TTM_JMethods.GetObjStaticData("quests")
EndFunction

int Function GetQst(Quest qst)
    string questKey = JString.encodeFormToString(qst)
    return JMap_getObj(GetQsts(), questKey)
EndFunction

string[] Function GetAllQuests()
    return JMap_allKeysPArray(GetQsts())
EndFunction

int Function GetStages(Quest qst)
    return JMap_getObj(GetQst(qst), "stages")
EndFunction

string[] Function GetAllQuestStages(Quest qst)
    return JMap_allKeysPArray(GetStages(qst))
EndFunction

int Function GetQstStage(Quest qst, string stage)
    return JMap_getObj(GetStages(qst), stage)
EndFunction

string Function GetQstStageAction(Quest qst, string stage)
    return JMap_getStr(GetQstStage(qst, stage), "action")
EndFunction

int Function GetQstStageCheckOnLoad(Quest qst, string stage)
    return JMap_getInt(GetQstStage(qst, stage), "checkOnLoad")
EndFunction
