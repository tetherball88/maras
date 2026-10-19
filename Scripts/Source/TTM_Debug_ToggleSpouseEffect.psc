Scriptname TTM_Debug_ToggleSpouseEffect extends activemagiceffect

; Debug magic effect used by the in-game debugging spell.
; When the player casts this spell on an NPC, it cycles that NPC's MARAS status in the order:
;   unknown -> candidate -> engaged -> married -> divorced
; This is intended for testing relationship and quest flows only.

Event OnEffectStart(Actor akTarget, Actor akCaster)
    if (!akTarget)
        return
    endif

    string status = MARAS.GetNpcStatusName(akTarget)
    string newStatus = MARAS.ShowDebugStatusModal(akTarget)
    if (newStatus == "")
        return
    endif

    TTM_Debug.debug("TTM_Debug_ToggleSpouseEffect: Changing status of " + TTM_Utils.GetActorName(akTarget) + " from " + status + " to " + newStatus)

    if (newStatus == "unregister")
        MARAS.UnregisterNPC(akTarget)
    else
        MARAS.PromoteNPCToStatus(akTarget, newStatus)
    endif
EndEvent
