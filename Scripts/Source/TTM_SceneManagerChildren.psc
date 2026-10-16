;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 1
Scriptname TTM_SceneManagerChildren Extends Scene Hidden

;BEGIN FRAGMENT Fragment_0
Function Fragment_0()
;BEGIN CODE
TTM_Debug.debug("TTM_SceneManagerChildren.End")
TTM_ServiceChildrenLogic childrenLogicQst = TTM_Data.GetChildLogicQuest() as TTM_ServiceChildrenLogic
Actor child1Ak = Child1.GetActorRef()
Actor child2Ak = Child2.GetActorRef()
childrenLogicQst.ResetForceGreeting(child1Ak)
childrenLogicQst.ResetForceGreeting(child2Ak)
childrenLogicQst.UnblockGames(child1Ak)
childrenLogicQst.UnblockGames(child2Ak)
GetOwningQuest().Reset()
GetOwningQuest().Stop()
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment

ReferenceAlias Property Child1  Auto
ReferenceAlias Property Child2  Auto
