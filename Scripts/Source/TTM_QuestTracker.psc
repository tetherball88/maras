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
    Quest childrenSandboxQst = TTM_Data.GetOriginalChildrenSandboxQuest()
    Quest childrenAdoptionQst = TTM_Data.GetOriginalChildrenAdoptionQuest()
    Quest childrenNewAdoptionQst = TTM_Data.GetOriginalChildrenNewAdoptionQuest()
    Quest turnSpouseToVampireQst = TTM_Data.GetTurnSpouseToVampireQuest()

    RegisterForQuestStage(self, weddinCeremonyQst)
    RegisterForQuestStage(self, marriageBreakupQst)

    ; RegisterForQuestStage(self, childrenSandboxQst)
    ; RegisterForQuestStage(self, childrenAdoptionQst)
    ; RegisterForQuestStage(self, childrenNewAdoptionQst)

    RegisterForQuest(self, marriageMainQst)
    RegisterForQuest(self, marriageFinQst)

    RegisterForQuest(self, turnSpouseToVampireQst)

    RegisterForQuest(self, childrenSandboxQst)
    RegisterForQuest(self, childrenAdoptionQst)
    RegisterForQuest(self, childrenNewAdoptionQst)
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
    elseif(akQuest == TTM_Data.GetOriginalChildrenAdoptionQuest())
        Actor newlyAdoptedChild = TTM_Utils.GetActorAlias(akQuest, "Child1")
        int retries = 5
        while(retries > 0 && !newlyAdoptedChild)
            Utility.Wait(1)
            newlyAdoptedChild = TTM_Utils.GetActorAlias(akQuest, "Child1")
            retries -= 1
        endwhile
        if(newlyAdoptedChild)
            TTM_Debug.debug("TTM_QuestTracker: OnQuestStart: OnOriginalChildrenAdoptionQuest()")
            TTM_ServiceChildrenLogic childrenLogicQst = TTM_Data.GetChildLogicQuest() as TTM_ServiceChildrenLogic
            childrenLogicQst.AdoptChild(newlyAdoptedChild, none, none, 2)
        else
            TTM_Debug.warn("TTM_QuestTracker: OnQuestStart: OnOriginalChildrenAdoptionQuest() No child found in alias Child1.")
        endif
    elseif(akQuest == TTM_Data.GetOriginalChildrenSandboxQuest())
        akQuest.Reset()
        akQuest.Stop()
    elseif(akQuest == TTM_Data.GetOriginalChildrenNewAdoptionQuest())
        akQuest.Reset()
        akQuest.Stop()
    elseif(akQuest == TTM_Data.GetTurnSpouseToVampireQuest())
        Actor[] spouses = MARAS.GetNPCsByStatus("married")
        int i = 0
        Actor candidate = none

        while(i < spouses.Length && candidate == none)
            if(!spouses[i].HasKeywordString("Vampire"))
                candidate = spouses[i]
            endif
            i += 1
        endwhile

        if(candidate != none)
            ReferenceAlias spouseAlias = akQuest.GetAliasByName("Spouse") as ReferenceAlias
            spouseAlias.ForceRefTo(candidate)
        else
            TTM_Debug.err("TTM_QuestTracker: OnQuestStart: OnTurnSpouseToVampireQuest() No non-vampire spouse found!")
            akQuest.Reset()
            akQuest.SetStage(300)
        endif
    endif
EndEvent

Event OnQuestStop(Quest akQuest)
    if(akQuest == TTM_Data.GetMarriageMainQuest())
        TTM_ServiceMarriageQuest.OnMarriageQuestStop()
    elseif(akQuest == TTM_Data.GetTurnSpouseToVampireQuest())
        ; reset quest repeatability
        TTM_Data.ResetDLC1RV06DoOnceGlobal()
    endif
EndEvent

