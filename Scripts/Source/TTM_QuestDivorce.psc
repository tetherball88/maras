;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 1
Scriptname TTM_QuestDivorce Extends Quest Hidden

;BEGIN ALIAS PROPERTY Spouse
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_Spouse Auto
;END ALIAS PROPERTY

;BEGIN ALIAS PROPERTY DivorceLetter
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_DivorceLetter Auto
;END ALIAS PROPERTY

WICourierScript Property pWICourier  Auto

;BEGIN FRAGMENT Fragment_0
Function Fragment_0()
;BEGIN CODE
    Actor spouse = Alias_Spouse.GetActorRef()
    Actor player = TTM_Data.GetPlayer()
    ObjectReference letter = Alias_DivorceLetter.GetRef()
    pWICourier.addItemToContainer(letter)
    StorageUtil.SetFormValue(none, "TTM_DivorceLetter_Spouse", spouse)
    StorageUtil.SetFormValue(none, "TTM_DivorceLetter_Letter", letter)

    RegisterForUpdateGameTime(3*24)
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment

Event OnUpdateGameTime()
    ; in case player was avoiding courier progress and add fee anyway
    ; remove letter from courier
    if(self.IsRunning())
        TTM_ServiceRelationships.AddDivorceFee(self)
        ObjectReference letter = StorageUtil.GetFormValue(none, "TTM_DivorceLetter_Letter") as ObjectReference
        pWICourier.removeRefFromContainer(letter)
    endif
EndEvent
