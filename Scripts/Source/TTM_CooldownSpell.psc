; magick effect to block npc from talking for 24 hours if player broke with them in any way
Scriptname TTM_CooldownSpell extends activemagiceffect

Spell Property currentSpell Auto

Event OnUpdateGameTime()
    Actor akTarget = GetTargetActor()
    TTM_Debug.trace("TTM_CooldownSpell:"+currentSpell+":Cleared:" + TTM_Utils.GetActorName(akTarget))
	akTarget.RemoveSpell(currentSpell)
EndEvent

Event OnEffectStart(Actor akTarget, Actor akCaster)
	float brokeupTime = TTM_ServiceRelationships.GetBrokeupTime(akTarget)
    float diff = Utility.GetCurrentGameTime() - brokeupTime

    TTM_Debug.trace("TTM_CooldownSpell:" + currentSpell + ":Start:diff" + diff)

    if(diff >= 1)
        akTarget.RemoveSpell(currentSpell)
    endif
	; start timer
	RegisterForSingleUpdateGameTime(24)
EndEvent
