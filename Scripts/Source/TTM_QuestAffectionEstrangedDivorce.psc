;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 9
Scriptname TTM_QuestAffectionEstrangedDivorce Extends Quest Hidden

;BEGIN ALIAS PROPERTY Spouse
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_Spouse Auto
;END ALIAS PROPERTY

;BEGIN FRAGMENT Fragment_6
Function Fragment_6()
;BEGIN CODE
; stage 200
    Actor spouse = Alias_Spouse.GetActorRef()
    if(spouse)
        Actor[] mainActors = new Actor[1]
        mainActors[0] = spouse
        Actor[] secondaryActors = new Actor[1]
        secondaryActors[0] = TTM_Data.GetPlayer()
        int modalResult = MARAS.ShowConfirmationModal("low_affection_divorce", mainActors, secondaryActors)
        if(!MARAS.IsNPCStatus(spouse, "married"))
            FinishQuest(false)
            return
        endif
        if(modalResult != 0 && modalResult != 4 && modalResult != 5)
            self.SetStage(150)
            return
        endif
        MARAS.PromoteNPCToStatus(spouse, "divorced")
        MARAS.SetPermanentAffection(spouse, 0)
    endif
    FinishQuest(false)
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_5
Function Fragment_5()
;BEGIN CODE
; stage 100
    Actor spouse = Alias_Spouse.GetActorRef()
    if(spouse)
        MARAS.SetPermanentAffection(spouse, 50)
    endif
    FinishQuest()
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_4
Function Fragment_4()
;BEGIN CODE
; stage 10
    SetObjectiveCompleted(0)
    SetObjectiveDisplayed(10)
    RegisterForSingleUpdateGameTime(3 * 24)
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_0
Function Fragment_0()
;BEGIN CODE
; stage 0
    SetObjectiveDisplayed(0)
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_8
Function Fragment_8()
;BEGIN CODE
    ; stage 150
    Actor spouse = Alias_Spouse.GetActorRef()
    if(spouse)
        MARAS.SetPermanentAffection(spouse, 50)
    endif
    FinishQuest()
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment

Function FinishQuest(bool success = true)
    if(success)
        CompleteAllObjectives()
    else
        FailAllObjectives()
    endif
    Stop()
EndFunction

Event OnUpdateGameTime()
    self.SetStage(200)
EndEvent
