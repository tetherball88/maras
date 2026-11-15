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

    RegisterForUpdateGameTime(3*24)
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment

Event OnUpdateGameTime()
    ; in case player was avoiding courier progress and add fee anyway
    ; remove letter from courier
    if(self.IsRunning())
        Actor spouse = Alias_Spouse.GetActorRef()
        ObjectReference letter = Alias_DivorceLetter.GetRef()
        MARAS.ProcessDivorceSettlement(spouse, self)
        if(letter)
            pWICourier.removeRefFromContainer(letter)
        endif
    endif
EndEvent
