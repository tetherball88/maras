;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 18
Scriptname TTM_PerkCheckDoor Extends Perk Hidden

;BEGIN FRAGMENT Fragment_16
Function Fragment_16(ObjectReference akTargetRef, Actor akActor)
;BEGIN CODE
Quest checkHomeQst = TTM_JData.GetMarasCheckSpouseHomeQuest()

ReferenceAlias spouseAl = checkHomeQst.GetAliasByName("Spouse") as ReferenceAlias
Actor spouse = spouseAl.GetActorRef()

Key doorKey = akTargetRef.GetKey()

; if(doorKey && TTM_ServiceSpouseAssets.HasKey(doorKey))
;     akActor.AddKeyIfNeeded(akTargetRef)
; endif

akTargetRef.Activate(akActor)
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment
