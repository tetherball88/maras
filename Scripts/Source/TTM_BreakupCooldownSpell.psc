; magick effect to block npc from talking for 24 hours if player broke with them in any way
Scriptname TTM_BreakupCooldownSpell extends activemagiceffect

Event OnUpdateGameTime()
    TTM_Debug.trace("TTM_BreakdowCooldownSpell:Cleared:" + TTM_Utils.GetActorName(GetTargetActor()))
	GetTargetActor().RemoveSpell(TTM_JData.GetBreakdownCooldownSpell())
EndEvent

Event OnEffectStart(Actor akTarget, Actor akCaster)
	float brokeupTime = TTM_ServiceNpcs.GetBrokeupTime(akTarget)
    float diff = Utility.GetCurrentGameTime() - brokeupTime

    TTM_Debug.trace("TTM_BreakdowCooldownSpell:Start:diff" + diff)

    if(diff >= 1)
        GetTargetActor().RemoveSpell(TTM_JData.GetBreakdownCooldownSpell())
    endif
	; start timer
	RegisterForSingleUpdateGameTime(24)
EndEvent
