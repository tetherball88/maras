;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 1
Scriptname TTM_QuestTrackKilledSpouses Extends Quest Hidden

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
;BEGIN AUTOCAST TYPE TTM_QuestTrackKilledSpousesWI
Quest __temp = self as Quest
TTM_QuestTrackKilledSpousesWI kmyQuest = __temp as TTM_QuestTrackKilledSpousesWI
;END AUTOCAST
;BEGIN CODE
    Actor killer = Alias_Killer.GetActorRef()
    Actor victim = Alias_Victim.GetActorRef()
    Actor player = TTM_Data.GetPlayer()
    string victimName = TTM_Utils.GetActorName(victim)
    if(killer != player)
        ObjectReference letter
        int inheritanceValue = 0
        if(MARAS.IsNPCStatus(victim, "married"))
            letter = Alias_SpouseLetter.GetRef()
            int spouseSocialClass = MARAS.GetNpcCurrentTypeEnum(victim, "socialClass")
            inheritanceValue = (spouseSocialClass + 1) * 400
        elseif(MARAS.IsNPCStatus(victim, "engaged"))
            letter = Alias_FianceLetter.GetRef()
        elseif(MARAS.IsNPCStatus(victim, "divorced") || MARAS.IsNPCStatus(victim, "jilted"))
            letter = Alias_ExLetter.GetRef()
        endif
        WICourierScript vWICourierScript = kmyquest.pWICourier as WICourierScript
        vWICourierScript.addItemToContainer(letter)	;add the inheritance letter to the courier so he will deliver it
        if(inheritanceValue)
            vWICourierScript.addItemToContainer(TTM_Data.GetGoldMisc(), inheritanceValue)
        endif

            TTM_Debug.debug("TTM_QuestTrackKilledSpouses:VictimSpouse:"+victimName+":Letter:"+letter)
    else
        TTM_Debug.debug("TTM_QuestTrackKilledSpouses:VictimSpouse:"+victimName+":YouAreKiller:NoInheritance")
    endif

    kmyquest.SetNextEventGlobals()
    stop()
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment
