scriptname TTM_ServiceGift

Function OnGiftMenuOpen() global
    StorageUtil.SetIntValue(none, "Gift.Started", 1)
EndFunction

Function OnGiftMenuClose() global
    Actor receiver = StorageUtil.GetFormValue(none, "Gift.Receiver") as Actor
    TTM_Debug.trace("TTM_ServiceGift:OnMenuClose:GiftMenu:Receiver:"+receiver)
    if(receiver)
        int giftValue = StorageUtil.GetIntValue(none, "Gift.Value")
        string giftItems = StorageUtil.GetStringValue(none, "Gift.Items")
        TTM_Debug.trace("TTM_ServiceGift:OnMenuClose:GiftValue:"+giftValue)
        string playerName = TTM_Utils.GetActorName(TTM_Data.GetPlayer())
        if(giftItems != "")
            string msg = playerName + " gifted " + giftItems + " to " + TTM_Utils.GetActorName(receiver) + " of total value " + giftValue + "."
            TTM_ServiceSkyrimNet.DirectNarration(msg, TTM_Data.GetPlayer(), receiver)
            TTM_ServiceAffection.AddGiftAffection(receiver, giftValue)
            TTM_Debug.trace("TTM_ServiceGift:Gift was given: " + msg)
        else
            TTM_ServiceSkyrimNet.DirectNarration(playerName + " gifted nothing to " + TTM_Utils.GetActorName(receiver) + ".", TTM_Data.GetPlayer(), receiver)
            TTM_Debug.trace("TTM_ServiceGift:No gift was given.")
        endif
        StorageUtil.ClearAllPrefix("Gift")
    endif
    StorageUtil.SetIntValue(none, "Gift.Started", 0)
EndFunction

Function OnItemGifted(Form akDestContainer, Form akBaseItem, int aiItemCount) global
    if(StorageUtil.GetIntValue(none, "Gift.Started") == 0)
        return
    endif
    Actor npcDestination = akDestContainer as Actor
    if(!npcDestination)
        return
    endif
    if(!MARAS.IsNPCStatus(npcDestination, "any"))
        return
    endif
    ; player adding something from npc, so it will add affection
    int value = aiItemCount * akBaseItem.GetGoldValue()
    StorageUtil.SetFormValue(none, "Gift.Receiver", npcDestination)
    string itemsString = StorageUtil.GetStringValue(none, "Gift.Items")
    if(itemsString != "")
        itemsString += ","
    endif
    itemsString += akBaseItem.GetName()
    if(aiItemCount > 1)
        itemsString += "(" + aiItemCount + ")"
    endif
    int totalValue = StorageUtil.GetIntValue(none, "Gift.Value") + value
    StorageUtil.SetStringValue(none, "Gift.Items", itemsString)
    StorageUtil.SetIntValue(none, "Gift.Value", totalValue)
    TTM_Debug.trace("TTM_ServiceGift:Gift was given to " + TTM_Utils.GetActorName(npcDestination) + ": " + itemsString + " total value: " + totalValue)
EndFunction
