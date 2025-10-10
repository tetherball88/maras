;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 1
Scriptname QF_TTM_TrackKilledSpouses Extends Quest Hidden

;BEGIN ALIAS PROPERTY Victim
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_Victim Auto
;END ALIAS PROPERTY

;BEGIN ALIAS PROPERTY Killer
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_Killer Auto
;END ALIAS PROPERTY

;BEGIN ALIAS PROPERTY FianceLetter
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_FianceLetter Auto
;END ALIAS PROPERTY

;BEGIN ALIAS PROPERTY ExLetter
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_ExLetter Auto
;END ALIAS PROPERTY

;BEGIN ALIAS PROPERTY Maramal
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_Maramal Auto
;END ALIAS PROPERTY

;BEGIN ALIAS PROPERTY SpouseLetter
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_SpouseLetter Auto
;END ALIAS PROPERTY

;BEGIN FRAGMENT Fragment_0
Function Fragment_0()
;BEGIN AUTOCAST TYPE TTM_QuestTrackKilledSpouses
Quest __temp = self as Quest
TTM_QuestTrackKilledSpouses kmyQuest = __temp as TTM_QuestTrackKilledSpouses
;END AUTOCAST
;BEGIN CODE
    Actor killer = Alias_Killer.GetActorRef()
    Actor victim = Alias_Victim.GetActorRef()
    Actor player = TTM_JData.GetPlayer()
    if(TTM_ServiceNpcs.GetKilledByPlayer(victim))
        ObjectReference letter
        int inheritanceValue = 0
        if(TTM_ServiceNpcs.IsSpouse(victim))
            letter = Alias_SpouseLetter.GetRef()
            int spouseSocialClass = TTM_Utils.GetSpouseSocialClassIndex(victim)
            inheritanceValue = (spouseSocialClass + 1) * 400
        elseif(TTM_ServiceNpcs.IsFiance(victim))
            letter = Alias_FianceLetter.GetRef()
        elseif(TTM_ServiceNpcs.IsDivorced(victim) || TTM_ServiceNpcs.IsJilted(victim))
            letter = Alias_ExLetter.GetRef()
        endif
        WICourierScript vWICourierScript = kmyquest.pWICourier as WICourierScript
        vWICourierScript.addItemToContainer(letter)	;add the inheritance letter to the courier so he will deliver it
        if(inheritanceValue)
            vWICourierScript.addItemToContainer(TTM_JData.GetGoldMisc(), inheritanceValue)
        endif
        TTM_Debug.trace("TTM_QuestTrackKilledSpouses:VictimSpouse:"+TTM_Utils.GetActorName(victim)+":Letter:"+Letter)
    else
        TTM_Debug.trace("TTM_QuestTrackKilledSpouses:VictimSpouse:"+TTM_Utils.GetActorName(victim)+":YouAreKiller:NoInheritance")
    endif

    kmyquest.SetNextEventGlobals()
    stop()
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment
