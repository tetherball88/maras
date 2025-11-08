Scriptname TTM_Debug_ToggleSpouseEffect extends activemagiceffect

; Debug magic effect used by the in-game debugging spell.
; When the player casts this spell on an NPC, it cycles that NPC's MARAS status in the order:
;   unknown -> candidate -> engaged -> married -> divorced
; This is intended for testing relationship and quest flows only.

Event OnEffectStart(Actor akTarget, Actor akCaster)
    string status = MARAS.GetNpcStatusName(akTarget)

     UIListMenu listMenu = UIExtensions.GetMenu("UIListMenu", true) as UIListMenu
    int i = 0
    string[] statuses = new string[5]
    statuses[0] = "candidate"
    statuses[1] = "engaged"
    statuses[2] = "married"
    statuses[3] = "divorced"
    statuses[4] = "jilted"

    while(i < 5)
        listMenu.AddEntryItem("Change status to: " + statuses[i])
        i += 1
    endwhile

    listMenu.OpenMenu()
    int choice = listMenu.GetResultInt()

    TTM_Debug.debug("TTM_Debug_ToggleSpouseEffect: Changing status of " + TTM_Utils.GetActorName(akTarget) + " from " + status + " to " + statuses[choice])

    MARAS.PromoteNPCToStatusByEnum(akTarget, choice)
EndEvent
