scriptname TTM_QuestTracker extends Quest

import PO3_Events_Form

Function Maintenance()
    RegisterQuests()
EndFunction

Function RegisterQuests()
    Quest weddinCeremonyQst = TTM_Data.GetMarriageWeddingQuest()
    Quest marriageMainQst = TTM_Data.GetMarriageMainQuest()
    Quest marriageFinQst = TTM_Data.GetMarriageFinQuest()
    Quest marriageBreakupQst = TTM_Data.GetMarriageBreakupQuest()
    RegisterForQuestStage(self, weddinCeremonyQst)
    RegisterForQuestStage(self, marriageBreakupQst)
    RegisterForQuest(self, marriageMainQst)
    RegisterForQuest(self, marriageFinQst)
EndFunction

Event OnQuestStageChange(Quest akQuest, Int aiNewStage)
    if(akQuest == TTM_Data.GetMarriageWeddingQuest())
        if(aiNewStage == 100)
            TTM_ServiceMarriageQuest.OnWeddingQstFinish()
        elseif(aiNewStage == 200)
            TTM_ServiceMarriageQuest.OnWeddingQstCancel()
        endif
    elseif(akQuest == TTM_Data.GetMarriageBreakupQuest())
        if(aiNewStage == 100)
            TTM_ServiceMarriageQuest.OnMarriageBreakupReconcile()
        endif
    endif
EndEvent

Event OnQuestStart(Quest akQuest)
    if(akQuest == TTM_Data.GetMarriageFinQuest())
        TTM_ServiceMarriageQuest.OnMarriageFinQstStart()
    endif
EndEvent

Event OnQuestStop(Quest akQuest)
    if(akQuest == TTM_Data.GetMarriageMainQuest())
        TTM_ServiceMarriageQuest.OnMarriageQuestStop()
    endif
EndEvent

