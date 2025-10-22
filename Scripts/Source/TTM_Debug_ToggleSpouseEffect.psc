Scriptname TTM_Debug_ToggleSpouseEffect extends activemagiceffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    string status = TTM_Utils.GetRelationshipStatus(akTarget)

    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("TTM_Debug_ToggleSpouseEffect:OnEffectStart:currentStatus:"+status)
    endif

    if(status == "")
        TTM_Utils.SendRelationshipChangeEvent(akTarget, "candidate")
    elseif(status == "candidate")
        TTM_Utils.SendRelationshipChangeEvent(akTarget, "engaged")
    elseif(status == "engaged")
        TTM_Utils.SendRelationshipChangeEvent(akTarget, "married")
    elseif(status == "married")
        TTM_Utils.SendRelationshipChangeEvent(akTarget, "divorced")
    endif
EndEvent
