scriptname TTM_Utils

string Function GetActorName(actor akActor) global
    if akActor == TTM_Data.GetPlayer()
      return akActor.GetActorBase().GetName()
    else
      return akActor.GetDisplayName()
    EndIf
EndFunction

Actor Function GetActorAlias(Quest qst, string name) global
    ReferenceAlias al = qst.GetAliasByName(name) as ReferenceAlias
    return al.GetActorRef()
EndFunction

int Function ToggleGlobalVariable(GlobalVariable gVar) global
    float val = gVar.GetValue()
    int newVal = 0
    if(val == 0)
        newVal = 1
    endif
    gVar.SetValue(newVal)

    return newVal
EndFunction

string Function GetActorsNamesJson(Actor[] actors, Actor excluded = none) global
    if(actors.length == 0)
        return ""
    endif
    int i = 0
    string listName = "TmpActorNames" + Utility.GetCurrentRealTime()

    int aLength = actors.length

    while(i < actors.length)
        Actor currentActor = actors[i]

        if(currentActor && currentActor != excluded)
            string actorName = GetActorName(currentActor)
            if(actorName != "")
                StorageUtil.StringListAdd(none, listName, actorName)
            else
                TTM_Debug.warn("GetActorsNamesJson: Actor has no name! Actor FormID: " + currentActor.GetFormID())
            endif
        endif
        i += 1
    endwhile

    return PapyrusUtil.StringJoin(StorageUtil.StringListToArray(none, listName), ", ")
EndFunction

Function FadeToBlack() global
    ImageSpaceModifier fadeToBlack = Game.GetFormFromFile(0xf756d, "Skyrim.esm") as ImageSpaceModifier
    ImageSpaceModifier fadeToBlackHold = Game.GetFormFromFile(0xf756e, "Skyrim.esm") as ImageSpaceModifier
    ImageSpaceModifier fadeToBlackBack = Game.GetFormFromFile(0xf756f, "Skyrim.esm") as ImageSpaceModifier

    fadeToBlack.Apply()
    Utility.Wait(2.0)
    fadeToBlack.PopTo(fadeToBlackHold)
    Utility.Wait(2.0)
    fadeToBlackHold.PopTo(fadeToBlackBack)
EndFunction

Function RequestDialogue(string prompt, string values, Actor akSpeaker, Actor akTarget = none) global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif

    SkyrimNetApi.DirectNarration(SkyrimNetApi.ParseString(prompt, "values", values), akSpeaker, TTM_Data.GetPlayer())
EndFunction
