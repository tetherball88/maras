;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 3
Scriptname TTM_QuestCheckSpouseHome Extends Quest Hidden

;BEGIN ALIAS PROPERTY SpouseHomeCenter
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_SpouseHomeCenter Auto
;END ALIAS PROPERTY

;BEGIN ALIAS PROPERTY Spouse
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_Spouse Auto
;END ALIAS PROPERTY

;BEGIN FRAGMENT Fragment_0
Function Fragment_0()
;BEGIN CODE
ObjectReference LocationRef = Alias_SpouseHomeCenter.GetReference()
    ; check current cell if spouse shared their home inside their home, so you don't need to leave and enter again

;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment
