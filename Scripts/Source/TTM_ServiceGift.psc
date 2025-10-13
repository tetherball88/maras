scriptname TTM_ServiceGift

Function OnGiftMenuOpen() global
        TTM_Debug.trace("TTM_ServiceGift:OnMenuOpen:GiftMenu")
        StorageUtil.SetIntValue(none, "TTM_GiftStarted", 1)
EndFunction

Function OnGiftMenuClose() global
    Actor receiver = StorageUtil.GetFormValue(none, "TTM_GiftReceiver") as Actor
    TTM_Debug.trace("TTM_ServiceGift:OnMenuClose:GiftMenu:Receiver:"+receiver)
    if(receiver)
        int giftValue = StorageUtil.GetIntValue(none, "TTM_GiftValue")
        string giftItems = StorageUtil.GetStringValue(none, "TTM_GiftItems")
        TTM_Debug.trace("TTM_ServiceGift:OnMenuClose:GiftValue:"+giftValue)
        string playerName = TTM_Utils.GetActorName(TTM_JData.GetPlayer())
        if(giftItems != "")
            string msg = playerName + " gifted " + giftItems + " to " + TTM_Utils.GetActorName(receiver) + " of total value " + giftValue + "."
            TTM_ServiceSkyrimNet.DirectNarration(msg, TTM_JData.GetPlayer(), receiver)
            TTM_ServiceAffection.AddGiftAffection(receiver, giftValue)
            TTM_Debug.trace("TTM_ServiceGift:Gift was given: " + msg)
        else
            TTM_ServiceSkyrimNet.DirectNarration(playerName + " gifted nothing to " + TTM_Utils.GetActorName(receiver) + ".", TTM_JData.GetPlayer(), receiver)
            TTM_Debug.trace("TTM_ServiceGift:No gift was given.")
        endif
        StorageUtil.ClearAllObjPrefix(none, "TTM_Gift")
    endif
EndFunction

Function OnItemGifted(Form akDestContainer, Form akBaseItem, int aiItemCount) global
    if(StorageUtil.GetIntValue(none, "TTM_GiftStarted") == 0)
        return
    endif
    Actor npcDestination = akDestContainer as Actor
    if(!npcDestination)
        return
    endif
    if(!TTM_Utils.IsTracking(npcDestination))
        return
    endif
    ; player adding something from npc, so it will add affection
    int value = aiItemCount * akBaseItem.GetGoldValue()
    StorageUtil.SetFormValue(none, "TTM_GiftReceiver", npcDestination)
    string itemsString = StorageUtil.GetStringValue(none, "TTM_GiftItems")
    if(itemsString != "")
        itemsString += ","
    endif
    itemsString += akBaseItem.GetName() + "(" + aiItemCount + ")"
    int totalValue = StorageUtil.GetIntValue(none, "TTM_GiftValue") + value
    StorageUtil.SetStringValue(none, "TTM_GiftItems", itemsString)
    StorageUtil.SetIntValue(none, "TTM_GiftValue", totalValue)
    TTM_Debug.trace("TTM_ServiceGift:Gift was given to " + TTM_Utils.GetActorName(npcDestination) + ": " + itemsString + " total value: " + totalValue)
EndFunction
