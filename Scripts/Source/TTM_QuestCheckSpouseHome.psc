;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 2
Scriptname TTM_QuestCheckSpouseHome Extends Quest Hidden

;BEGIN ALIAS PROPERTY Spouse
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_Spouse Auto
;END ALIAS PROPERTY

;BEGIN ALIAS PROPERTY SpouseHome
;ALIAS PROPERTY TYPE LocationAlias
LocationAlias Property Alias_SpouseHome Auto
;END ALIAS PROPERTY

;BEGIN ALIAS PROPERTY SpouseHomeCenter
;ALIAS PROPERTY TYPE ReferenceAlias
ReferenceAlias Property Alias_SpouseHomeCenter Auto
;END ALIAS PROPERTY

;BEGIN FRAGMENT Fragment_0
Function Fragment_0()
;BEGIN CODE
    ObjectReference LocationRef = Alias_SpouseHomeCenter.GetReference()
    Actor spouse = Alias_Spouse.GetActorRef()
    if(spouse == none)
        TTM_Debug.err("TTM_QuestCheckSpouseHome:No spouse assigned to alias")
        self.Reset()
        self.Stop()
        return
    endif
    string spouseName = TTM_Utils.GetActorName(spouse)
    Cell currentCell = LocationRef.GetParentCell()
    if(LocationRef != none && TTM_ServiceSpouseAssets.DoesSpouseOwnHome(spouse, currentCell))
        if(TTM_Debug.IsTrace())
            TTM_Debug.trace("TTM_QuestCheckSpouseHome:Spouse:"+spouseName+";HomeCenter:"+LocationRef)
        endif
        SetActive()
        SetObjectiveDisplayed(0)
    else
        if(TTM_Debug.IsTrace())
            TTM_Debug.trace("TTM_QuestCheckSpouseHome:Spouse:"+spouseName+";NO HomeCenter")
        endif
        Debug.Notification(TTM_Utils.GetActorName(spouse) + " doesn't have their own private home.")
        self.Reset()
        self.Stop()
        spouse.AddToFaction(TTM_JData.GetSpouseNoInitialHouseFaction())
        return
    endif

    ; 29 is for door
    int count = currentCell.GetNumRefs(29)
    int i = 0

    ; loop through all doors, and add all available keys for spouse location
    while(i < count)
        ObjectReference cellDoor = currentCell.GetNthRef(0, 29)
        Key doorKey = cellDoor.GetKey()
        if(doorKey)
            TTM_JData.GetPlayer().AddKeyIfNeeded(cellDoor)
        endif

        i += 1
    endwhile

    ; check current cell if spouse shared their home inside their home, so you don't need to leave and enter again
    TTM_ServiceSpouseAssets.CheckCell(spouse)

;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment
