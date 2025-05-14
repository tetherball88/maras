scriptname TTM_QuestTracker extends Quest

import PO3_Events_Form
import TTM_JCDomain

Function Maintenance()
    ImportJson()
    RegisterQuests()
EndFunction

Function RegisterQuests()
    int jQuests = GetQuests()
    string qstKey = JMap_nextKey(jQuests)

    while(qstKey)
        ; on quest finish its form resets to none in jcontainer
        ; store them as strings and decode to form here
        Quest qst = JString.decodeFormStringToForm(qstKey) as Quest
        RegisterQuest(qst)
        qstKey = JMap_nextKey(jQuests, qstKey)
    endwhile
EndFunction

Function RegisterQuest(Quest qst)
    TTM_Debug.trace("TTM_QuestTracker:RegisterQuest:CHECK:"+qst)
    int jQst = GetQuestObj(qst)
    int jStages = GetQstStages(jQst)
    string stage = JMap_nextKey(jStages)
    bool allStagesPassed = true

    while(stage)
        if(stage == "start" || stage == "stop")
            RegisterForQuest(self, qst)
        elseif(!qst.IsStageDone(stage as int))
            allStagesPassed = false
        ; if used in ongoing game check if this quest stage is completed
        else
            ProcessStage(jQst, stage, true)
        endif
        stage = JMap_nextKey(jStages, stage)
    endwhile

    ; todo we can't remove quest from jcontainers if it is repeatable
    ; check if all tracked stages for this quest already passed, we can remove whole quest from tracked
    if(allStagesPassed)
        TTM_Debug.trace("TTM_QuestTracker:RegisterQuest:SKIP:"+qst)
        ; TTM_JData.RemoveQuest(qst)
    ; otherwise subscribe to quest stage changes
    else
        TTM_Debug.trace("TTM_QuestTracker:RegisterQuest:DONE:"+qst)
        RegisterForQuestStage(self, qst)
    endif
EndFunction

Function ProcessStage(int jQuest, string stage, bool onLoad = false)
    int jStage = GetQstStage(jQuest, stage)
    string stageAction = JMap_getStr(jStage, "action")
    bool stageProcessOnLoad = JMap_getInt(jStage, "checkOnLoad") == 1

    if(onLoad && !stageProcessOnLoad)
        TTM_Debug.trace("TTM_QuestTracker:ProcessStage:SKIP:"+stage+":action:"+stageAction)
        return
    endif

    TTM_Debug.trace("TTM_QuestTracker:ProcessStage:"+stage+":action:"+stageAction)

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
    int jQuest = GetQuestObj(akQuest)
    int jStage = GetQstStage(jQuest, aiNewStage as string)
    TTM_Debug.trace("TTM_QuestTracker:OnQuestStageChange:"+akQuest+"; stage:"+aiNewStage+"; jStage:"+jStage+"; jQuest:"+jQuest)
    if(jStage != 0)
        ProcessStage(jQuest, aiNewStage)
    endif
EndEvent

Event OnQuestStart(Quest akQuest)
    int jQuest = GetQuestObj(akQuest)
    string jStage = GetQstStage(jQuest, "start")
    TTM_Debug.trace("TTM_QuestTracker:OnQuestStageChange:"+akQuest+"; stage:START; jStage:"+jStage+"; jQuest:"+jQuest)
    if(jStage != 0)
        ProcessStage(jQuest, "start")
    endif
EndEvent

Event OnQuestStop(Quest akQuest)
    int jQuest = GetQuestObj(akQuest)
    string jStage = GetQstStage(jQuest, "stop")
    TTM_Debug.trace("TTM_QuestTracker:OnQuestStageChange:"+akQuest+"; stage:STOP; jStage:"+jStage+"; jQuest:"+jQuest)
    if(jStage != 0)
        ProcessStage(jQuest, "stop")
    endif
EndEvent


;/ ==============================
   SECTION: JContainers functions
============================== /;

string Function GetQuestsNamespace()
    return TTM_JData.GetNamespaceKey() + ".quests"
EndFunction

Function ImportJson()
    int JQuestsFromFiles = TTM_JUtils.LoadMultipleFiles("Data/SKSE/Plugins/MARAS/quests")
    JDB_solveObjSetter(GetQuestsNamespace(), JQuestsFromFiles, true)
EndFunction

int Function GetQuests()
    return JDB_solveObj(GetQuestsNamespace())
EndFunction

int Function GetQuestObj(Quest qst)
    string qstKey = JString.encodeFormToString(qst)
    int jQuests = GetQuests()
    int jQuest = JMap_getObj(jQuests, qstKey)
    return jQuest
EndFunction

int Function GetQstStages(int jQuest)
    return JMap_getObj(jQuest, "stages")
EndFunction

int Function GetQstStage(int jQuest, string stage)
    return JMap_getObj(GetQstStages(jQuest), stage)
EndFunction

int Function RemoveStage(int jQuest, int stage)
    JMap_removeKey(GetQstStages(jQuest), stage)
    return JMap_count(GetQstStages(jQuest))
EndFunction

