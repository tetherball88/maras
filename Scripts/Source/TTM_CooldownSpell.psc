; magick effect to block npc from talking for 24 hours if player broke with them in any way
Scriptname TTM_CooldownSpell extends activemagiceffect

Spell Property currentSpell Auto

Event OnUpdateGameTime()
    Actor akTarget = GetTargetActor()
    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("TTM_CooldownSpell:"+currentSpell.GetName()+":Cleared:" + TTM_Utils.GetActorName(akTarget))
    endif
	akTarget.RemoveSpell(currentSpell)
EndEvent

Event OnEffectStart(Actor akTarget, Actor akCaster)
	float brokeupTime = TTM_ServiceRelationships.GetBrokeupTime(akTarget)
    float diff = Utility.GetCurrentGameTime() - brokeupTime
    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("TTM_CooldownSpell:" + currentSpell.GetName() + ":Start:diff" + diff)
    endif

    if(diff >= 1)
        akTarget.RemoveSpell(currentSpell)
    endif
	; start timer
	RegisterForSingleUpdateGameTime(24)
EndEvent
