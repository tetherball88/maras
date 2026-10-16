Scriptname TTM_ChildrenSandbox extends Quest
{Quest script for BYOHRelationshipAdoptionScheduler. Primarily handles updates for the Child's Chest system.}

;---This is NOT the main Adoption Script. If you're looking for that, you want BYOHRelationshipAdoptionScript. :) ---
;
;This script is responsible for periodically refilling the chest in the children's bedroom and performing some utility functions.
; - It lives on the Scheduler subquest because it's the Scheduler that has the chest alias for the current house.
; - It calls back to RelationshipAdoption to fill the chest in order to keep that functionality in one place.
;
;Note that, after filling the chest, we only start the timer to refill it after the player has interacted with it.
;This keeps the chest from 'overflowing', and prevents us from having to remove items from the chest as new ones are added
;(since other Adoption events push things into it, and we don't want to remove items from containers in the player's house).

TTM_ServiceChildrenLogic property childrenLogic Auto			;Main Adoption quest.
ReferenceAlias property ChildChest Auto			;Child's chest in the current house.
bool waitForPlayerInteraction = False			;Are we waiting for the player to interact with this chest before refilling it?
ReferenceAlias property Child1 Auto				;Alias of Child 1 on this quest.
ReferenceAlias property Child2 Auto				;Alias of Child 2 on this quest.

;Called by the chest when the player activates it.
;If we were waiting for the player to interact with it, start the timer to refill the chest.
Function PlayerChestInteraction()
; 	;Debug.Trace("Player/Chest Interaction")
	if (waitForPlayerInteraction)
		waitForPlayerInteraction = False
		RegisterForChestUpdate()
	EndIf
EndFunction

;Schedules an update to refill the chest.
Function RegisterForChestUpdate()
; 	;Debug.Trace("Registering for Chest Update")
	Self.UnRegisterForUpdateGameTime()
	Self.RegisterForSingleUpdateGameTime(120) ;5d Cooldown Timer
EndFunction

;Fill the chest, then wait for the player to interact with it before starting the cooldown again.
Event OnUpdateGameTime()
; 	;Debug.Trace("Scheduler OnUpdate. Refilling Chest.")
	childrenLogic.RefillChest(ChildChest.GetReference())
	waitForPlayerInteraction = True
EndEvent
