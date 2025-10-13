; magick effect to block npc from talking for 24 hours if player broke with them in any way
Scriptname TTM_CooldownSpell extends activemagiceffect

Spell Property currentSpell Auto

Event OnUpdateGameTime()
    TTM_Debug.trace("TTM_CooldownSpell:"+currentSpell+":Cleared:" + TTM_Utils.GetActorName(GetTargetActor()))
	GetTargetActor().RemoveSpell(currentSpell)
EndEvent

Event OnEffectStart(Actor akTarget, Actor akCaster)
	float brokeupTime = TTM_ServiceNpcs.GetBrokeupTime(akTarget)
    float diff = Utility.GetCurrentGameTime() - brokeupTime

    TTM_Debug.trace("TTM_CooldownSpell:" + currentSpell + ":Start:diff" + diff)

    if(diff >= 1)
        GetTargetActor().RemoveSpell(currentSpell)
    endif
	; start timer
	RegisterForSingleUpdateGameTime(24)
EndEvent
