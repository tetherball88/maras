;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 25
Scriptname TTM_QuestEnablePolygamy Extends Quest Hidden

;BEGIN ALIAS PROPERTY OldScrollTranslated
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_OldScrollTranslated Auto
;END ALIAS PROPERTY

;BEGIN ALIAS PROPERTY OldScroll
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_OldScroll Auto
;END ALIAS PROPERTY

;BEGIN ALIAS PROPERTY Urag
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_Urag Auto
;END ALIAS PROPERTY

;BEGIN ALIAS PROPERTY CurrentSpouse
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_CurrentSpouse Auto
;END ALIAS PROPERTY

;BEGIN ALIAS PROPERTY Maramal
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_Maramal Auto
;END ALIAS PROPERTY

;BEGIN ALIAS PROPERTY Senna
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_Senna Auto
;END ALIAS PROPERTY

;BEGIN ALIAS PROPERTY OldScrollContainer
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_OldScrollContainer Auto
;END ALIAS PROPERTY

;BEGIN FRAGMENT Fragment_18
Function Fragment_18()
;BEGIN CODE
    ; stage 50
    Actor player = TTM_JData.GetPlayer()
    player.RemoveItem(Alias_OldScroll.GetReference())
    TTM_Debug.trace("TTM_QuestEnablePolygamy:Give scroll for translation")
    ; todo make 24 hours after development
    self.RegisterForUpdateGameTime(1)
    self.SetObjectiveCompleted(40)
    self.SetObjectiveDisplayed(50)
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_24
Function Fragment_24()
;BEGIN CODE
    ; stage 55
    ObjectReference translatedScroll = Alias_OldScrollTranslated.GetReference()
    translatedScroll.Enable()
    Actor player = TTM_JData.GetPlayer()
    player.AddItem(translatedScroll)
    TTM_Debug.trace("TTM_QuestEnablePolygamy:Player gets translated scroll")
    self.SetObjectiveCompleted(52)
    self.SetObjectiveDisplayed(55)
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_25
Function Fragment_25()
;BEGIN CODE
    ; stage 52
    self.SetObjectiveCompleted(50)
    self.SetObjectiveDisplayed(52)
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_14
Function Fragment_14()
;BEGIN CODE
    ; stage 10
    self.SetObjectiveCompleted(0)
    self.SetObjectiveDisplayed(10)
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_20
Function Fragment_20()
;BEGIN CODE
    ; stage 70
    Actor player = TTM_JData.GetPlayer()
    player.RemoveItem(Alias_OldScrollTranslated.GetReference())
    TTM_Debug.trace("TTM_QuestEnablePolygamy:Player gives away translated scroll")
    self.SetObjectiveCompleted(60)
    self.SetObjectiveDisplayed(70)
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_19
Function Fragment_19()
;BEGIN CODE
    ; stage 60
    self.SetObjectiveCompleted(55)
    self.SetObjectiveDisplayed(60)
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_15
Function Fragment_15()
;BEGIN CODE
    ; stage 20
    Alias_OldScroll.GetReference().Enable()
    TTM_Debug.trace("TTM_QuestEnablePolygamy:Scroll appears in chest")
    self.SetObjectiveCompleted(10)
    self.SetObjectiveDisplayed(20)
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_23
Function Fragment_23()
;BEGIN CODE
    ; stage 0
    self.SetObjectiveDisplayed(0)
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_17
Function Fragment_17()
;BEGIN CODE
    ; stage 40
    self.SetObjectiveCompleted(30)
    self.SetObjectiveDisplayed(40)
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_16
Function Fragment_16()
;BEGIN CODE
    ; stage 30
    self.SetObjectiveCompleted(25)
    self.SetObjectiveDisplayed(30)
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_22
Function Fragment_22()
;BEGIN CODE
    ; stage 25
    self.SetObjectiveCompleted(20)
    self.SetObjectiveDisplayed(25)
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_21
Function Fragment_21()
;BEGIN CODE
    ; stage 80
    self.SetObjectiveCompleted(70)
    self.CompleteAllObjectives()
    Actor player = TTM_JData.GetPlayer()
    Faction married = TTM_JData.GetMarriedFaction()
    if(player.IsInFaction(married))
        player.RemoveFromFaction(married)
    endif
    TTM_Debug.trace("TTM_QuestEnablePolygamy:Removes player from married faction so player can re-marry")
    self.CompleteQuest()
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment

Event OnUpdateGameTime()
	if(GetStage() == 50)
		SetStage(52)
		Self.UnregisterForUpdateGameTime()
        TTM_Debug.trace("TTM_QuestEnablePolygamy:Player waited for translation")
	endif
EndEvent
