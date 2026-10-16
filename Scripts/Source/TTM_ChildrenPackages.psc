;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 1
Scriptname TTM_ChildrenPackages Extends Package Hidden

;BEGIN FRAGMENT Fragment_0
Function Fragment_0(Actor akActor)
;BEGIN CODE
if (GameHour.GetValue() < 16)
   akActor.SetAV("Variable06", 0)
EndIf
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment

GlobalVariable Property GameHour  Auto
