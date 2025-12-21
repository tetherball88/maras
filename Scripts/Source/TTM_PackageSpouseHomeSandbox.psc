;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 3
Scriptname TTM_PackageSpouseHomeSandbox Extends Package Hidden

;BEGIN FRAGMENT Fragment_1
Function Fragment_1(Actor akActor)
;BEGIN CODE
TTM_Debug.trace("End TTM spouse home package for " + TTM_Utils.GetActorName(akActor))
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_0
Function Fragment_0(Actor akActor)
;BEGIN CODE
TTM_Debug.trace("Start TTM spouse home package for " + TTM_Utils.GetActorName(akActor))
MiscUtil.PrintConsole("LinkedRef:"+TTM_Data.GetHomeSandboxKeyword()+":"+akActor.GetLinkedRef(TTM_Data.GetHomeSandboxKeyword()))
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_2
Function Fragment_2(Actor akActor)
;BEGIN CODE
TTM_Debug.trace("Change TTM spouse home package for " + TTM_Utils.GetActorName(akActor))
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment
