;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 9
Scriptname QF_TTM_AffectionEstrangedDivorce Extends Quest Hidden

;BEGIN ALIAS PROPERTY Spouse
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_Spouse Auto
;END ALIAS PROPERTY

;BEGIN FRAGMENT Fragment_6
Function Fragment_6()
;BEGIN CODE
; stage 200
    FailAllObjectives()
    Stop()
    TTM_Utils.SendRelationshipChangeEvent(Alias_Spouse.GetActorRef(), "divorced")
    TTM_ServiceAffection.SetAffectionRank(Alias_Spouse.GetActorRef(), 0)
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_5
Function Fragment_5()
;BEGIN CODE
; stage 100
    TTM_ServiceAffection.SetAffectionRank(Alias_Spouse.GetActorRef(), 50)
    CompleteAllObjectives()
    Stop()
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_4
Function Fragment_4()
;BEGIN CODE
; stage 10
    SetObjectiveCompleted(0)
    SetObjectiveDisplayed(10)
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
    TTM_ServiceAffection.SetAffectionRank(Alias_Spouse.GetActorRef(), 50)
    CompleteAllObjectives()
    Stop()
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment
