;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 1
Scriptname TTM_QuestTrackNewAdoption Extends Quest Hidden

;BEGIN ALIAS PROPERTY Child
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_Child Auto
;END ALIAS PROPERTY

;BEGIN FRAGMENT Fragment_0
Function Fragment_0()
;BEGIN CODE
; stage 0
TTM_Debug.debug("TTM_QuestTrackNewAdoption: Fragment_0: Alias_Child = " + Alias_Child.GetRef())
TTM_ServiceChildrenLogic childrenLogicQst = TTM_Data.GetChildLogicQuest() as TTM_ServiceChildrenLogic
childrenLogicQst.AdoptChild(Alias_Child.GetRef() as Actor)
Reset()
Stop()
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment
