scriptname TTM_ServiceGift

Function OnGiftMenuOpen() global
        TTM_Debug.trace("TTM_ServiceGift:OnMenuOpen:GiftMenu")
        TTM_JMethods.SetIntValue(none, "Gift.Started", 1)
EndFunction

Function OnGiftMenuClose() global
    Actor receiver = TTM_JMethods.GetFormValue(none, "Gift.Receiver") as Actor
    TTM_Debug.trace("TTM_ServiceGift:OnMenuClose:GiftMenu:Receiver:"+receiver)
    if(receiver)
        int giftValue = TTM_JMethods.GetIntValue(none, "Gift.Value")
        string giftItems = TTM_JMethods.GetStringValue(none, "Gift.Items")
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
        TTM_JMethods.ClearValue(none, "Gift")
    endif
EndFunction

Function OnItemGifted(Form akDestContainer, Form akBaseItem, int aiItemCount) global
    if(TTM_JMethods.GetIntValue(none, "Gift.Started") == 0)
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
    TTM_JMethods.SetFormValue(none, "Gift.Receiver", npcDestination)
    string itemsString = TTM_JMethods.GetStringValue(none, "Gift.Items")
    if(itemsString != "")
        itemsString += ","
    endif
    itemsString += akBaseItem.GetName() + "(" + aiItemCount + ")"
    int totalValue = TTM_JMethods.GetIntValue(none, "Gift.Value") + value
    TTM_JMethods.SetStringValue(none, "Gift.Items", itemsString)
    TTM_JMethods.SetIntValue(none, "Gift.Value", totalValue)
    TTM_Debug.trace("TTM_ServiceGift:Gift was given to " + TTM_Utils.GetActorName(npcDestination) + ": " + itemsString + " total value: " + totalValue)
EndFunction
