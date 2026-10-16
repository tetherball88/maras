;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 2
Scriptname TTM_DialogueManagerChildren Extends TopicInfo Hidden

;BEGIN FRAGMENT Fragment_0
Function Fragment_0(ObjectReference akSpeakerRef)
Actor akSpeaker = akSpeakerRef as Actor
;BEGIN CODE
    if(DialogueType == "pet_bailout")
        akSpeaker.SetActorValue("Variable07", 10)
    endif
;END CODE
EndFunction
;END FRAGMENT

;BEGIN FRAGMENT Fragment_1
Function Fragment_1(ObjectReference akSpeakerRef)
Actor akSpeaker = akSpeakerRef as Actor
;BEGIN CODE
;OnBegin
    TTM_ServiceChildrenLogic childrenLogicQst = GetOwningQuest() as TTM_ServiceChildrenLogic
    if(DialogueType == "allowance_topic" || DialogueType == "gift_topic" || DialogueType == "chest" \
        || DialogueType == "critter_topic" || DialogueType == "pet_topic" || DialogueType == "game_ask")
        childrenLogicQst.ResetForceGreeting(akSpeaker)
    elseif(DialogueType == "allowance_give")
        childrenLogicQst.GiveAllowance(akSpeaker, AllowanceAmount)
    elseif(DialogueType == "gift_give")
        childrenLogicQst.GiveChildGift(akSpeaker)
    elseif(DialogueType == "gift_react")
        if(childrenLogicQst.GiftReaction > 0)
            childrenLogicQst.PlayerBeggarAbility()
        endif
    elseif(DialogueType == "critter_ban")
        childrenLogicQst.BanCritterForChild(akSpeaker)
    elseif(DialogueType == "critter_reject")
        childrenLogicQst.RejectCritterForChild(akSpeaker)
    elseif(DialogueType == "critter_keep")
        childrenLogicQst.AdoptCritter(akSpeaker)
    elseif(DialogueType == "gift_receive_poor")
        childrenLogicQst.FGReceivePoorGift(akSpeaker)
        childrenLogicQst.ResetForceGreeting(akSpeaker)
    elseif(DialogueType == "gift_receive_regular")
        childrenLogicQst.FGReceiveGift(akSpeaker)
        childrenLogicQst.ResetForceGreeting(akSpeaker)
    elseif(DialogueType == "break_name_calling")
        childrenLogicQst.BreakNameCalling(OrderToRoom, akSpeaker)
    elseif(DialogueType == "pet_topic_scared")
        childrenLogicQst.ResetForceGreeting(akSpeaker)
        childrenLogicQst.RejectPetForChild(akSpeaker)
    elseif(DialogueType == "pet_ban")
        childrenLogicQst.BanPetForChild(akSpeaker)
    elseif(DialogueType == "pet_decline")
        childrenLogicQst.RejectPetForChild(akSpeaker)
    elseif(DialogueType == "pet_keep")
        childrenLogicQst.AdoptPetForChild(akSpeaker)
    elseif(DialogueType == "game_start")
        childrenLogicQst.StartGame(GameToPlay, akSpeaker)
    elseif(DialogueType == "new_home_ask")
        TTM_ServicePlayerHouse.ChooseHomeForSpouseMsg(akSpeakerRef as Actor, 0, true)
    elseif(DialogueType == "order_bed_reject" || DialogueType == "order_chores_reject" || DialogueType == "order_inside_reject" \
        || DialogueType == "order_outside_reject")
        childrenLogicQst.PendingOrderToConfirm = OrderId
    elseif(DialogueType == "game_bedtime" || DialogueType == "order_bed_accept" || DialogueType == "order_chores_accept" || DialogueType == "order_inside_accept" \
        || DialogueType == "order_outside_accept")
        childrenLogicQst.ExecuteOrderById(akSpeaker, OrderId)
    elseif(DialogueType == "reject_force")
        childrenLogicQst.ExecutePendingOrder(akSpeaker)
    elseif(DialogueType == "make_pet_follower")
        childrenLogicQst.MakePetFollower(akSpeaker)
    elseif(DialogueType == "all_children_to_leave")
        MARAS.FleeChildrenFromPlayerCell()
    endif
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment

; allowance_topic
; allowance_give
; gift_topic
; gift_give
; gift_react
; chest
; critter_topic
; critter_ban
; critter_reject
; critter_keep
; gift_receive_poor
; gift_receive_regular
; break_name_calling
; pet_bailout
; pet_topic_scared
; pet_topic
; pet_ban
; pet_decline
; pet_keep
; game_ask
; game_bedtime
; game_start
; new_home_ask
; order_bed_reject
; order_bed_accept
; order_chores_reject
; order_chores_accept
; order_inside_reject
; order_inside_accept
; order_outside_reject
; order_outside_accept
; reject_force
; make_pet_follower
String Property DialogueType Auto
int Property AllowanceAmount Auto
bool Property OrderToRoom Auto
int Property GameToPlay Auto
float Property OrderId Auto
