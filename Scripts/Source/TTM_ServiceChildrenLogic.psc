scriptname TTM_ServiceChildrenLogic extends Quest Conditional

int Property GiftReaction Auto Hidden Conditional
int Property PoorCounter Auto Hidden Conditional
bool Property ForcegreetEventReady Auto Hidden Conditional

MiscObject Property Gold001  Auto
SPELL Property FavorJobsBeggarsAbility  Auto
Message Property FavorJobsBeggarMessage  Auto
Formlist property BYOHRelationshipAdoptionPlayerGiftChildMale Auto
Formlist property BYOHRelationshipAdoptionPlayerGiftChildFemale Auto
Keyword property BYOHAdoptionClothesKeyword Auto	;Keyword to identify children's clothes tokens.
Keyword property BYOHAdoptionToyKeyword Auto		;Keyword to identify children's toys (dolls, etc.).

Armor property ClothesChild01 Auto				;Armor forms for the childrens' outfits.
Armor property ClothesChild02 Auto
Armor property ClothesChild03 Auto
Armor property ClothesChild04 Auto
Armor property ClothesChild05 Auto
Outfit property ChildOutfit01 Auto		;Outfit forms for the childrens' outfits.
Outfit property ChildOutfit02 Auto
Outfit property ChildOutfit03 Auto
Outfit property ChildOutfit04 Auto
Outfit property ChildOutfit05 Auto
MiscObject property ClothesChild01Male Auto	;Since children's clothes are non-Playable, the system manipulates MiscObject 'tokens' that represent them.
MiscObject property ClothesChild02Male Auto	;These are the tokens for the boy versions of the clothes.
MiscObject property ClothesChild03Male Auto
MiscObject property ClothesChild04Male Auto
MiscObject property ClothesChild05Male Auto
MiscObject property ClothesChild01Female Auto	;Tokens for the girl versions of the clothes.
MiscObject property ClothesChild02Female Auto
MiscObject property ClothesChild03Female Auto
MiscObject property ClothesChild04Female Auto
MiscObject property ClothesChild05Female Auto

Formlist property BYOHRelationshipAdoption_CrittersMale Auto
Formlist property BYOHRelationshipAdoption_CrittersFemale Auto

;Aliases on the Scheduler subquest representing scene markers.
ReferenceAlias property SchedulerSceneMarker1 Auto
ReferenceAlias property SchedulerSceneMarker2 Auto
ReferenceAlias property SceneChild1 Auto
ReferenceAlias property SceneChild2 Auto
Scene property TTM_DialoguesChildrenScenes_SceneNameCalling01 Auto
Scene property TTM_DialoguesChildrenScenes_SceneNameCalling02 Auto
bool NameCallingUsedLast														;Was the last forcegreet event a Name-Calling Scene? If so, don't do another one.
int nameCallingChance = 10

ReferenceAlias property AnimalCompanion Auto
ReferenceAlias property TransientPet Auto
Faction property BYOHRelationshipPotentialPetFaction Auto						;Faction, applied via the TransientPet alias, whose rank represents the pet's suitability for adoption.
Formlist property BYOHRelationshipAdoption_PetAllowedRacesList Auto				;Races of pets that any kid can adopt.
Formlist property BYOHRelationshipAdoption_PetDogsList Auto						;Races of pets that are also dogs.

FormList property BYOHRelationshipAdoptionGifts_Poor Auto			;Formlists of gifts the child can give.
FormList property BYOHRelationshipAdoptionGifts_Junk Auto
FormList property BYOHRelationshipAdoptionGifts_0000 Auto
FormList property BYOHRelationshipAdoptionGifts_0025 Auto
FormList property BYOHRelationshipAdoptionGifts_0050 Auto
FormList property BYOHRelationshipAdoptionGifts_0100 Auto
FormList property BYOHRelationshipAdoptionGifts_0250 Auto
FormList property BYOHRelationshipAdoptionGifts_0500 Auto
FormList property BYOHRelationshipAdoptionGifts_1000 Auto

Faction property WINeverFillAliasesFaction Auto
Faction property BYOHRelationshipAdoptableFaction Auto
Quest property BYOHRelationshipAdoptable Auto						;Pre-Adoption functions, behaviors, and house tracking.
Quest property BYOHRelationshipAdoptableOrphanage Auto			;Pre-Adoption Orphanage Manager.
Quest property BYOHRelationshipAdoptableUrchins Auto				;Pre-Adoption Urchin Manager.

Quest property WIGamesTag Auto										;Game: Tag
Quest property WIGamesHideAndSeek Auto								;Game: Hide and Seek
Keyword property WIGamesTagStart Auto
Keyword property WIGamesHideAndSeekStart Auto

LocationAlias property CurrentHomeHouse Auto
LocationAlias property CurrentHomeExterior Auto

Weapon weaponToEquip = None		;Internal: Which weapon do we want to equip when gift-giving ends?
MiscObject clothesToEquip = None	;Internal: Which clothes do we want to equip when gift-giving ends?
Armor clothesToRemove = None		;Internal: Which clothes do we want to unequip when gift-giving ends?
bool wasGivenToy = False			;Internal: Were we just given a toy?

DialogueFollowerScript property DialogueFollower auto

int numOfChildrenPerLocation = 20
int critterChance = 10

bool ChildChestIntroduced = False										;Has one of the children mentioned the chest (enqueued Event 2 or 3)?


float property playerLastSeen = 0.0 Auto Hidden Conditional			;Timestamp of when we last 'saw' the player.

float property lastChildAdoptedTimestamp Auto Conditional Hidden

Quest property WIKill05 Auto											;WI: Related actor has been killed. We need for force-shutdown this in some cases.

;Orders System Properties
float property PendingOrderToConfirm = 0.0 Auto Hidden		;Holding variable for an order the player has requested, but not yet confirmed.

bool migrationDone = false
bool Function GetMigrationDone()
    return migrationDone
EndFunction

bool adoptionInProgress = false

Function AdoptChild(Actor child, Actor spouseToAdopWith = none, Location houseToMove = None, int childStage = 2, bool multipleAdoptions = false, Actor bioMother = none, Actor bioFather = none, int syntheticID = 0, bool forceSingleParent = false, bool isMigrating = false)
    if(child == none)
        TTM_Debug.debug("TTM_ServiceChildrenLogic: AdoptChild: child is none.")
        return
    endif
    if(adoptionInProgress)
        TTM_Debug.debug("TTM_ServiceChildrenLogic: AdoptChild: adoption already in progress.")
        return
    endif
    if (MARAS.IsChildPlayerAdopted(child))
        TTM_Debug.debug("TTM_ServiceChildrenLogic: AdoptChild: child is already adopted.")
        return
    endif
    adoptionInProgress = true
    Actor player = TTM_Data.GetPlayer()

    ;Remove the child from interfering World Interactions.
	StopGames()
	if (WIKill05.IsRunning())
		WIKill05.Stop()
	EndIf

    int syntheticChildID = syntheticID
    string childDisplayName = child.GetDisplayName()
    if (syntheticChildID == 0)
        if (spouseToAdopWith != none)
            syntheticChildID = MARAS.FindSyntheticChild(spouseToAdopWith, childDisplayName)
        endif
        if (syntheticChildID == 0 && bioMother != none)
            syntheticChildID = MARAS.FindSyntheticChild(bioMother, childDisplayName)
        endif
        if (syntheticChildID == 0)
            syntheticChildID = MARAS.FindSyntheticChild(none, childDisplayName)
        endif
    endif

    TTM_Debug.debug("TTM_ServiceChildrenLogic: syntheticChildID: " + syntheticChildID)

    ; Resolve biological parents if not already passed
    if (bioMother == none && syntheticChildID != 0)
        bioMother = MARAS.GetBioMother(syntheticChildID)
    endif
    if (bioMother == none && syntheticChildID == 0 && child != none)
        bioMother = MARAS.GetBioMother(child.GetFormID())
    endif
    if (bioFather == none && syntheticChildID != 0)
        bioFather = MARAS.GetBioFather(syntheticChildID)
    endif
    if (bioFather == none && syntheticChildID == 0 && child != none)
        bioFather = MARAS.GetBioFather(child.GetFormID())
    endif

    ; If this is a biological child where player is NOT involved and NOT married to either parent, do not adopt into player family
    if (syntheticChildID != 0 && (bioMother != none || bioFather != none))
        bool isPlayerBioParent = (player == bioMother || player == bioFather)
        bool isBioParentMarriedToPlayer = (bioMother != none && MARAS.IsNPCStatus(bioMother, "married")) || (bioFather != none && MARAS.IsNPCStatus(bioFather, "married"))
        if (!isPlayerBioParent && !isBioParentMarriedToPlayer)
            TTM_Debug.debug("TTM_ServiceChildrenLogic: Player not actively involved in child lineage - skip player adoption.")
            adoptionInProgress = false
            return
        endif
    endif

    ;Ping the Adoptable-Orphanage quest so it updates if you've adopted one of its children.
	if (BYOHRelationshipAdoptableOrphanage.IsRunning())
		(BYOHRelationshipAdoptableOrphanage as BYOHRelationshipAdoptableOrphanageSc).CheckAdoptOrphanageChild(child, 1)
	EndIf

	;Ping the Adoptable-Urchin quest so it updates if you've adopted one of its children.
	if (BYOHRelationshipAdoptableUrchins.IsRunning())
		(BYOHRelationshipAdoptableUrchins as BYOHRelationshipAdoptableUrchinScript).CheckAdoptUrchinChild(child, 1)
	EndIf

    ;Clear out the child's Actor Variables.
	child.SetActorValue("Variable06", 0)
	child.SetActorValue("Variable07", 0)
	child.SetActorValue("Variable08", 0)

    TTM_Debug.debug("TTM_ServiceChildrenLogic: AdoptChild: child = " + child)

    ; Select co-parent spouse if not yet set
    if (spouseToAdopWith == none && !forceSingleParent)
        if (bioMother != none && MARAS.IsNPCStatus(bioMother, "married"))
            spouseToAdopWith = bioMother
        elseif (bioFather != none && MARAS.IsNPCStatus(bioFather, "married"))
            spouseToAdopWith = bioFather
        else
            int spousesCount = MARAS.GetStatusCount("married")
            if (spousesCount == 1)
                Actor[] spouses = MARAS.GetNPCsByStatus("married")
                if (spouses.Length > 0)
                    spouseToAdopWith = spouses[0]
                endif
            elseif (spousesCount > 1)
                spouseToAdopWith = TTM_Utils.ChooseSpouseToAdoptWithMsg()
            endif
            ; 0 spouses: adopt as single parent (spouseToAdopWith stays none)
        endif
    endif

    ; Select house
    if (spouseToAdopWith != none && houseToMove == none)
        Location spouseHouse = MARAS.GetTenantHouse(spouseToAdopWith)
        if (spouseHouse != none)
            houseToMove = spouseHouse
        endif
    endif
    if (houseToMove == none)
        houseToMove = TTM_ServicePlayerHouse.ChooseHomeForSpouseMsg(child, 0, true)
    endif
    TTM_Debug.debug("TTM_ServiceChildrenLogic: AdoptChild: spouseToAdopWith = " + spouseToAdopWith + ", houseToMove = " + houseToMove)

    ; Register child with MARAS using the single authoritative ingestion pipeline
    ; C++ RegisterChildActor snapshots native factions, clears external factions, and assigns household factions
    MARAS.RegisterChildActor(child, player, spouseToAdopWith, houseToMove, bioMother, bioFather, syntheticChildID, childStage, sendEvent = !isMigrating)

    if (!isMigrating && spouseToAdopWith != none && spouseToAdopWith != player)
        TTM_ServiceAffection.ApplyChildAdoptedAffection(spouseToAdopWith)
    endif

    if (!isMigrating)
        lastChildAdoptedTimestamp = Utility.GetCurrentGameTime() + 3
        playerLastSeen = Utility.GetCurrentGameTime()
        child.EvaluatePackage()
        Game.AddAchievement(61)
    else
        child.EvaluatePackage()
    endif

    if (!multipleAdoptions)
        Quest adoptionQst = TTM_Data.GetOriginalChildrenAdoptionQuest()
        if (adoptionQst.IsRunning())
            adoptionQst.Reset()
            adoptionQst.Stop()
        endif
    endif

    if (syntheticChildID != 0)
        child.MoveToPackageLocation()
    endif

    adoptionInProgress = false
EndFunction

Function AdoptChildGlobal(Actor child, Actor spouseToAdoptWith = none, Form houseToMove = none) global
    TTM_ServiceChildrenLogic logic = TTM_Data.GetChildLogicQuest() as TTM_ServiceChildrenLogic
    if (logic)
        bool forceSingle = (spouseToAdoptWith == none)
        logic.AdoptChild(child, spouseToAdoptWith, houseToMove as Location, forceSingleParent = forceSingle)
    endif
EndFunction

Function ResetForceGreeting(Actor child)
    TTM_Debug.debug("ResetForceGreeting() child(" + child + ")")
    ForcegreetEventReady = false
    MARAS.ResetChildForceGreeting(child)
    child.SetActorValue("Variable07", 0)
    child.EvaluatePackage()
EndFunction

;----------------------------------------------------------------------------------------------------
;GAMES SYSTEM
;------------

;Starts one of the WI Games. 1 = Tag, 2 = HideAndSeek
Function StartGame(int gameToPlay, Actor childToPlay)
	;Cleanup any previous games to make sure the new one starts sucessfully.
	StopGames()
	;Unblock games for this child, if they were blocked.
	UnblockGames(childToPlay)
	;Run the game.
	if gameToPlay == 1
		WIGamesTagStart.SendStoryEvent(CurrentHomeExterior.GetLocation(), Game.GetPlayer(), childToPlay)
	else
		WIGamesHideAndSeekStart.SendStoryEvent(CurrentHomeExterior.GetLocation(), Game.GetPlayer(), childToPlay)
	EndIf
EndFunction

;Stop any active WI Games.
Function StopGames()
	WIGamesTag.Stop()
	WIGamesHideAndSeek.Stop()
EndFunction

;Block the child from being added to any new WE Games.
;Used to avoid problems when they're in a scene or forcegreet we don't want them to be pulled out of.
Function BlockGames(Actor child)
	child.AddToFaction(WINeverFillAliasesFaction)
EndFunction

;Release the block, allowing children to be added to new WE Games.
Function UnblockGames(Actor child)
	child.RemoveFromFaction(WINeverFillAliasesFaction)
EndFunction

;----------------------------------------------------------------------------------------------------
;ORDERS SYSTEM
;-------------

Function ExecutePendingOrder(Actor child, int duration = 3)
    TTM_Debug.debug("ExecutePendingOrder: child = " + child + ", duration = " + duration + ", PendingOrderToConfirm = " + PendingOrderToConfirm)
    if(PendingOrderToConfirm)
        ExecuteOrderById(child, PendingOrderToConfirm, duration)
        PendingOrderToConfirm = 0
    endif
EndFunction

Function ExecuteOrderById(Actor child, float orderId, int duration = 3)
    TTM_Debug.debug("ExecuteOrderById: child = " + child + ", orderId = " + orderId + ", duration = " + duration)
    MARAS.IssueChildOrder(child, orderId, duration)
EndFunction

;Issue the 'Never Speak to You Again' order, which causes both kids to flee to their rooms and lasts for 24h.
Function IssueOrderNeverSpeakToYouAgain(Actor child)
    MARAS.IssueChildOrder(child, 10, 24)
EndFunction

;----------------------------------------------------------------------------------------------------
;GIFT-GIVING SYSTEM
;------------------

;Display the gift menu, process the results, and return control to dialogue when done.
Function GiveChildGift(Actor child)
	;Clear the variable that stores the child's immediate response.
	GiftReaction = 0

	;Determine which gift list to use based on the child's gender.
	Formlist giftList
	if (child.GetActorBase().GetSex() == 0)
		giftList = BYOHRelationshipAdoptionPlayerGiftChildMale
	Else
		giftList = BYOHRelationshipAdoptionPlayerGiftChildFemale
	EndIf

	;Player gives gifts to the child.
	;As the player gives gifts, BYOHRelationshipAdoptionChildScript's OnItemAdded event will process them and call the secondary functions below.
	bool gaveAnyGift = child.ShowGiftMenu(True, giftList)
EndFunction

Function ChildReceiveGiftItem(Actor child, Form item, int count = 1)
    ;Calculate the item's gold value for later reference.
    int value = item.GetGoldValue() * count

    ;Determine what type of item this is.
    if ((item as Weapon) != None)
        ;WEAPONS: Equip the new weapon. Send the previously-equipped weapon (and any excess copies of this one) to the child's chest.
        ;The logic here is complicated because RemoveItem sometimes creates new items the child didn't have when it 'removes' them to the other container.
        if (child.GetEquippedWeapon() != None)
            Weapon weaponToUnequip = child.GetEquippedWeapon()
            child.UnEquipItem(weaponToUnequip)
            child.RemoveItem(weaponToUnequip, 1, false)
        EndIf
        if (weaponToEquip != None && child.GetItemCount(weaponToEquip) > 0)
            if (weaponToEquip == (item as Weapon))
                child.RemoveItem(weaponToEquip, child.GetItemCount(weaponToEquip) - 1, false)
            Else
                child.RemoveItem(weaponToEquip, child.GetItemCount(weaponToEquip), false)
            EndIf
        EndIf
        if (child.GetItemCount(item) > 1)
            child.RemoveItem(item, child.GetItemCount(item) - 1, false)
        EndIf
        weaponToEquip = (item as Weapon)
        UpdateGiftValues(child, value, 0)
    ElseIf ((item as MiscObject) != None)
        ;Determine what type of misc object this is...
        if (item.HasKeyword(BYOHAdoptionClothesKeyword))
            ;CLOTHES (TOKENS): Store one instance of this token in clothesToEquip. Send any excess, or anything already in this variable, to the chest.
            ;Don't bother equipping the clothes until the gift-giving process is over.
            if (clothesToEquip != None)
                child.RemoveItem(clothesToEquip, 1, false)
            EndIf
            if (child.GetItemCount(item) > 1)
                child.RemoveItem(item, child.GetItemCount(item) - 1, false)
            EndIf
            clothesToEquip = (item as MiscObject)
            UpdateGiftValues(child, value, 0)
        ElseIf (item.HasKeyword(BYOHAdoptionToyKeyword))
            ;TOYS (Doll, etc.): Keep one, send excess to the chest.
            wasGivenToy = True
            child.RemoveItem(item, child.GetItemCount(item) - 1, false)
            UpdateGiftValues(child, value, 0)
        Else
        EndIf
    ElseIf ((item as Book) != None)
        ;BOOKS: Send any and all books to the chest.
        child.RemoveItem(item, count, false)
        UpdateGiftValues(child, value, 0)
    ElseIf ((item as Potion) != None)
        ;POTIONS (FOOD): Consume food, and give the child it's equivalent gold value.
        child.RemoveItem(item, count)
        UpdateGiftValues(child, value, value)
    Else
; 			Debug.Trace("ERROR: Unrecognized Item: " + item)
    EndIf
EndFunction

Function UpdateGiftValues(Actor child, int EmotionalValue, int RealValue)
	GiftReaction = GiftReaction + EmotionalValue
EndFunction

Function GiftFinish(Actor child)
    ;If given a doll, play with it.
	if (wasGivenToy)
        MARAS.IssueChildOrder(child, 1.2, 1)	;Order 1.2 - Play with doll for 1h.
		wasGivenToy = False
	EndIf

	;If given a weapon, equip it, then go spar.
	if (weaponToEquip != None)
; 		;Debug.Trace("Now Equipping: " + weaponToEquip)
		child.EquipItem(weaponToEquip)
		MARAS.IssueChildOrder(child, 1.1, 1)	;Order 1.1 - Spar with practice dummy for 1h.
		weaponToEquip = None
	EndIf

	if (clothesToEquip != None)
; 		;Debug.Trace("Now Equipping: " + clothesToEquip)
		EquipNewClothes(child)
		RemoveOldClothes(child)
		clothesToEquip = None
	EndIf
EndFunction

;----------------------------------------------------------------------------------------------------
;CHILD CHEST SYSTEM
;------------------

;When requested by the Scheduler, refill the child's chest. This is handled like a give player gift event, using similar formlists.
Function RefillChest(ObjectReference childChest)
; 	;Debug.Trace("RefillChest refills chest.")
	;Generate 1-3 instances of 3-5 items from the Junk list.
    Quest TTM_ChildreSandboxQst = TTM_Data.GetChildreSandboxQuest()
	int roll = Utility.RandomInt(3, 5)
	int random
; 	;Debug.Trace("Level 1 Items: " + roll)
	While (roll > 0)
		random = Utility.RandomInt(0, BYOHRelationshipAdoptionGifts_Junk.GetSize() - 1)
		childChest.AddItem(BYOHRelationshipAdoptionGifts_Junk.GetAt(random), Utility.RandomInt(1,3))
		roll = roll - 1
	EndWhile

	;Generate 1-3 items from the 0025 list.
	roll = Utility.RandomInt(1, 3)
; 	;Debug.Trace("Level 2 Items: " + roll)
	While (roll > 0)
		random = Utility.RandomInt(0, BYOHRelationshipAdoptionGifts_0025.GetSize() - 1)
		childChest.AddItem(BYOHRelationshipAdoptionGifts_0025.GetAt(random), 1)
		roll = roll - 1
	EndWhile

    int i = 0
    int max = 0
    Actor child = none

    while(i< numOfChildrenPerLocation)
        int childAliasIndex = i + 1
        string normalizedIndex = ""
        if(childAliasIndex < 10)
            normalizedIndex = "0" + childAliasIndex
        else
            normalizedIndex = "" + childAliasIndex
        endif
        Actor currentChild = TTM_Utils.GetActorAlias(TTM_ChildreSandboxQst, "Child0" + normalizedIndex)
        int gold = currentChild.GetItemCount(Gold001)
        if(gold > max)
            max = gold
            child = currentChild
        endif
        i = i + 1
    endwhile

	if (child.GetItemCount(Gold001) > 25)
; 		;Debug.Trace("Level 3 Item Added")
		Formlist list = PickGiftList(child.GetItemCount(Gold001))
		roll = Utility.RandomInt(0, list.GetSize())
		childChest.AddItem(list.GetAt(roll), 1)
		int deduction = PickDeduction(child.GetItemCount(Gold001))
		deduction = (deduction / 2) + Utility.RandomInt(0, deduction / 2)
; 		;Debug.Trace("Deducting: " + deduction)
		child.RemoveItem(Gold001, deduction)
	EndIf
EndFunction

;Give Player Gift Event: Helper Function to select the appropriate formlist.
FormList Function PickGiftList(int gold)
	if (gold < 25)
		return BYOHRelationshipAdoptionGifts_0000
	ElseIf (gold < 50)
; 		;Debug.Trace("Pick gift from list 0025")
		return BYOHRelationshipAdoptionGifts_0025
	ElseIf (gold < 100)
; 		;Debug.Trace("Pick gift from list 050")
		return BYOHRelationshipAdoptionGifts_0050
	ElseIf (gold < 250)
; 		;Debug.Trace("Pick gift from list 0100")
		return BYOHRelationshipAdoptionGifts_0100
	ElseIf (gold < 500)
; 		;Debug.Trace("Pick gift from list 0250")
		return BYOHRelationshipAdoptionGifts_0250
	ElseIf (gold < 1000)
; 		;Debug.Trace("Pick gift from list 500")
		return BYOHRelationshipAdoptionGifts_0500
	EndIf
	;Else case...
; 	;Debug.Trace("Pick gift from list 1000")
	return BYOHRelationshipAdoptionGifts_1000
EndFunction

;Give Player Gift Event: Helper Function to select the appropriate deduction.
int Function PickDeduction(int gold)
	if (gold < 25)
		return 1
	ElseIf (gold < 50)
		return 25
	ElseIf (gold < 100)
		return 50
	ElseIf (gold < 250)
		return 100
	ElseIf (gold < 500)
		return 250
	ElseIf (gold < 1000)
		return 500
	EndIf
	;Else case...
	return 1000
EndFunction

;----------------------------------------------------------------------------------------------------
;PET EVENTS
;----------
;Reminder: Pets are player follower animals (dogs), Critters are kid animals (hares, foxes, etc.).

;Adopt the pet under consideration into the family for a child.
Function AdoptPetForChild(Actor child)
	Actor newPet = TransientPet.GetActorRef()
	TransientPet.Clear()
	MARAS.AssignChildPet(child, newPet)
EndFunction

;Reject the pet under consideration for a child, recording retry timestamp (15d cooldown).
Function RejectPetForChild(Actor child)
	MARAS.RecordChildPetRetry(TransientPet.GetActorRef())
EndFunction

;Ban pet events for a specific child or globally.
Function BanPetForChild(Actor child)
	MARAS.SetChildPetBanned(child, true)
EndFunction

Function MakePetFollower(Actor dog)
    DialogueFollower.SetAnimal(dog)
EndFunction


Function GiveAllowance(Actor child, int AllowanceAmount)
    if(AllowanceAmount > 0)
        Game.GetPlayer().RemoveItem(Gold001, AllowanceAmount)
        child.AddItem(Gold001, AllowanceAmount)
        SetPoor(AllowanceAmount)
        PlayerBeggarAbility()
    else
        SetPoor(0)
    endif
EndFunction

Function SetPoor(int AllowanceAmount = 0)
    Actor player = Game.GetPlayer()
    int delta = 0
    if(AllowanceAmount <= 0)
        delta = 1
    elseif(AllowanceAmount <= 25)
        delta = -1
    elseif(AllowanceAmount <= 100)
        delta = -2
    elseif(AllowanceAmount <= 500)
        delta = -3
    else
        delta = -5
    endif
    ChangePoorCount(delta)
EndFunction

Function ChangePoorCount(int delta)
    PoorCounter += delta
    if(PoorCounter < 0)
        PoorCounter = 0
    endif
EndFunction

;Evaluate the pet in the TransientPet alias to determine whether it's suitable for adoption, and whether it's a dog.
;Returns the int (Faction Rank) representing the suitability. Stores the 'dog' status as AV06=1.
int Function EvaluatePetSuitability()
	int suitability = 1	;1=None, 2=UNUSED, 3=All Children

	Race transient = TransientPet.GetActorRef().GetRace()
	Race current

	;Is this pet a dog? If so, store it as AV06=0.
	int i = 0
	While (i < BYOHRelationshipAdoption_PetDogsList.GetSize())
		current = BYOHRelationshipAdoption_PetDogsList.GetAt(i) as Race
; 		;Debug.Trace("Comparing: " + transient + " : " + current)
		if (current == transient)
; 			;Debug.Trace("Pet is a dog.")
			TransientPet.GetActorRef().SetAV("Variable06", 1)
			i = BYOHRelationshipAdoption_PetDogsList.GetSize()
		EndIf
		i = i + 1
	EndWhile

	;Is this pet adoptable by any child?
	i = 0
	While (i < BYOHRelationshipAdoption_PetAllowedRacesList.GetSize())
		current = BYOHRelationshipAdoption_PetAllowedRacesList.GetAt(i) as Race
; 		;Debug.Trace("Comparing: " + transient + " : " + current)
		if (current == transient)
; 			;Debug.Trace("EvalPetSuitability found pet in General list.")
			suitability = 3
			i = BYOHRelationshipAdoption_PetAllowedRacesList.GetSize()
		EndIf
		i = i + 1
	EndWhile

	;Update the faction rank to store this result for use as a dialogue condition.
	TransientPet.GetActorRef().SetFactionRank(BYOHRelationshipPotentialPetFaction, suitability)

	;Return the rank so we can decide which child to use for this event.
; 	;Debug.Trace("EvalPetSuitability returns: " + suitability)
	return suitability
EndFunction

;----------------------------------------------------------------------------------------------------
;CRITTER EVENTS
;--------------
;Reminder: Pets are player follower animals (dogs), Critters are kid animals (hares, foxes, etc.).


;Spawn a new critter the child will ask to keep.
Function SetupCritter(Actor child)
; 	;Debug.Trace("Now setting up the critter.")

	;Pick a critter from the formlist. Anything but the last one we rejected will do.
	Actor newCritter = SpawnTransientCritter(child)

	;EVP Child and Critter, then move them to their scene locations.
	child.EvaluatePackage()
	child.MoveToPackageLocation()
	newCritter.EvaluatePackage()
	newCritter.MoveToPackageLocation()
EndFunction

;Adopt the critter under consideration into the family.
Function AdoptCritter(Actor child)
	if (!child)
		return
	endif
    Actor critterAk = StorageUtil.GetFormValue(child, "TransientCritter") as Actor
    StorageUtil.FormListClear(child, "TransientCritter")

    if (critterAk)
		MARAS.AssignChildCritter(child, critterAk)
		critterAk.EvaluatePackage()
	endif
EndFunction

;Reject the critter under consideration for a child, recording retry timestamp (15d cooldown).
Function RejectCritterForChild(Actor child)
	if (!child)
		return
	endif
	MARAS.RecordChildCritterRetry(child)
    RemoveTransientCritter(child)
EndFunction

;Ban critter events for a specific child or globally.
Function BanCritterForChild(Actor child, bool globalBan = false)
	if (globalBan)
		MARAS.SetGlobalCritterBanned(true)
	elseif (child)
		MARAS.SetChildCritterBanned(child, true)
	endif
	RemoveTransientCritter(child)
EndFunction

Actor Function SpawnTransientCritter(Actor child)
    ActorBase critterBase
    If (child.GetActorBase().GetSex() == 0)
        critterBase = 	BYOHRelationshipAdoption_CrittersMale.GetAt(Utility.RandomInt(0, BYOHRelationshipAdoption_CrittersMale.GetSize() - 1)) as ActorBase
    Else
        critterBase = 	BYOHRelationshipAdoption_CrittersFemale.GetAt(Utility.RandomInt(0, BYOHRelationshipAdoption_CrittersFemale.GetSize() - 1)) as ActorBase
    EndIf

	;Create the critter and force it into aliases.
; 	;Debug.Trace("Spawning: " + critterBase)
	Actor newCritter = child.PlaceAtMe(critterBase, 1, true) as Actor
    string critterName = MARAS.GenerateRandomCritterNameForActor(newCritter)
    string childName = TTM_Utils.GetActorName(child)
    newCritter.SetDisplayName(childName + "'s " + critterName)

    StorageUtil.SetFormValue(child, "TransientCritter", newCritter)

    return newCritter
EndFunction

Function RemoveTransientCritter(Actor child)
    Actor critterAk = StorageUtil.GetFormValue(child, "TransientCritter") as Actor
    StorageUtil.FormListClear(child, "TransientCritter")
    if (critterAk)
        critterAk.Disable(True)
        critterAk.Delete()
    endif
EndFunction

;When a critter dies for a child:
Function CritterOrPetDeathForChild(Actor akVictim, Actor akKiller, Actor child)
    TTM_Debug.debug("TTM_ServiceChildrenLogic:CritterOrPetDeathForChild"+akVictim+":"+akKiller+":"+child)
    if(MARAS.GetChildPet(child) == akVictim)
        MARAS.RemoveChildPet(child)
        MARAS.RecordChildPetRetry(child)
    elseif(MARAS.GetChildCritter(child) == akVictim)
        MARAS.RemoveChildCritter(child)
	    MARAS.RecordChildCritterRetry(child)
    endif

	if (akKiller == Game.GetPlayer())
		IssueOrderNeverSpeakToYouAgain(child)
	endif
EndFunction

;----------------------------------------------------------------------------------------------------
;WELCOME HOME SYSTEM
;-------------------

;Set up a Forcegreet Event when requested by a Location Change event.
Function ReadyForcegreetEvent(Location currentLoc)
    TTM_Debug.debug("ReadyForcegreetEvent() currentLoc(" + currentLoc + "): "+ ForcegreetEventReady)
; 	Debug.Trace("Attempting to ready Forcegreet Event")
	if (!ForcegreetEventReady)
		ForcegreetEventReady = True

		;Make sure the children aren't in any games.
		StopGames()


		;Declare some local variables.
		int eventNumber = 0
		Actor eventChild

		;Select the event.

		;EVENT 1 - CHILD THANKS PLAYER FOR ADOPTION, NO CHEST INTRO
		;Occurs if: Child newly adopted, Initial Move is a Dirty Move
; 		Debug.Trace("New Adoption: " + (Child1NewlyAdopted || Child2NewlyAdopted) + "  Initial Move: " + initialMoveDone + "  Chest Intro'd: " + ChildChestIntroduced)
        Actor newlyAdoptedChild = MARAS.GetNewlyAdoptedChild(currentLoc)
        TTM_Debug.debug("ReadyForcegreetEvent() newlyAdoptedChild(" + newlyAdoptedChild + ")")
		if (newlyAdoptedChild)
			eventNumber = 1
            ;EVENT 2 - CHILD THANKS PLAYER FOR ADOPTION, INCLUDES CHEST INTRO
            if(!ChildChestIntroduced)
                ChildChestIntroduced = True
                eventNumber = 2
                TTM_Debug.debug("ReadyForcegreetEvent() ThankForAdoption:ChestIntro eventChild(" + newlyAdoptedChild + ")")
            else
                TTM_Debug.debug("ReadyForcegreetEvent() ThankForAdoption:NoChestIntro eventChild(" + newlyAdoptedChild + ")")
            endif

            eventChild = newlyAdoptedChild
            MARAS.SetChildIsNewlyAdopted(eventChild, false, true)
        endif

		;EVENT 3 - CHILD INTRODUCES CHEST SYSTEM
		;Occurs if: Child previously performed
		if(eventNumber == 0)
            if(!ChildChestIntroduced)
                ChildChestIntroduced = True
                eventNumber = 3
                eventChild = MARAS.GetWeightedRandomChild(currentLoc)
                TTM_Debug.debug("ReadyForcegreetEvent() ChestIntro: eventChild(" + eventChild + ")")
            endif
        endif

		;EVENT 4 - ADOPT A PET
		;Occurs if: FamilyPet is empty, Player has an Animal Companion, time > petRetryTime
        if (eventNumber == 0)
            Actor childAllowedToHavePet = MARAS.GetChildWithoutPet(currentLoc)
            TTM_Debug.debug("ReadyForcegreetEvent() GetChildWithoutPet: eventChild(" + childAllowedToHavePet + ")")

            if(AnimalCompanion.GetActorRef() != None && childAllowedToHavePet)
                TransientPet.ForceRefTo(AnimalCompanion.GetActorRef())
                int suitability = EvaluatePetSuitability()
                eventNumber = 4
                eventChild = childAllowedToHavePet
                TTM_Debug.debug("ReadyForcegreetEvent() AdoptAPet: eventChild(" + eventChild + ")")
            endif
        endif

		;EVENT 5 - ADOPT A CRITTER
		;Occurs if: FamilyCritter is empty, time > critterRetryTime, 10% Chance
		if (eventNumber == 0)
            Actor childAllowedToHaveCritter = MARAS.GetChildWithoutCritter(currentLoc)
            TTM_Debug.debug("ReadyForcegreetEvent() GetChildWithoutCritter: eventChild(" + childAllowedToHaveCritter + ")")
            if(childAllowedToHaveCritter && Utility.RandomInt(1, 100) < critterChance)
                eventNumber = 5
                eventChild = childAllowedToHaveCritter
                SetupCritter(eventChild)
                TTM_Debug.debug("ReadyForcegreetEvent() AdoptACritter: eventChild(" + eventChild + ")")
            endif
        endif


		;EVENT 6 - NAME CALLING 1
		;Occurs if: Player has two children, they haven't played this scene before, they didn't do a Name Calling Scene last time, 10% chance.
        if (eventNumber == 0)
            Actor[] scene1Children = MARAS.GetChildrenWithoutScene(currentLoc, 1)
            TTM_Debug.debug("ReadyForcegreetEvent() GetChildrenWithoutScene: scene1Children: child1: " + scene1Children[0] + "  child2: " + scene1Children[1])
            if(scene1Children.Length == 2 && !NameCallingUsedLast && Utility.RandomInt(1, 100) < nameCallingChance)
                Quest sceneQst = SceneChild1.GetOwningQuest()
                bool started = sceneQst.Start()
                TTM_Debug.debug("ReadyForcegreetEvent() NameCalling01: sceneQst: " + sceneQst + "  started: " + started)
                if(started)
                    eventNumber = 6
                    NameCallingUsedLast = True
                    ; ResortChildrenForNameCalling()
                    BlockGames(scene1Children[0])
                    BlockGames(scene1Children[1])
                    SceneChild1.ForceRefTo(scene1Children[0])
                    SceneChild2.ForceRefTo(scene1Children[1])
                    scene1Children[0].SetActorValue("Variable07", eventNumber)
                    scene1Children[1].SetActorValue("Variable07", eventNumber)
                    scene1Children[0].MoveTo(SchedulerSceneMarker1.GetReference())
                    scene1Children[1].MoveTo(SchedulerSceneMarker2.GetReference())
                    scene1Children[0].EvaluatePackage()
                    scene1Children[1].EvaluatePackage()
                    TTM_DialoguesChildrenScenes_SceneNameCalling01.Start()
                    TTM_Debug.debug("ReadyForcegreetEvent() NameCalling01: sceneChild1: " + scene1Children[0] + "  sceneChild2: " + scene1Children[1])
                    return
                endif
            endif
        endif

		;EVENT 7 - NAME CALLING 2
		;Occurs if: Player has two children, they haven't played this scene before, they didn't do a Name Calling Scene last time, 10% chance.
		if (eventNumber == 0)
            Actor[] scene2Children = MARAS.GetChildrenWithoutScene(currentLoc, 2)
            TTM_Debug.debug("ReadyForcegreetEvent() GetChildrenWithoutScene: scene2Children: child1: " + scene2Children[0] + "  child2: " + scene2Children[1])
            if(scene2Children.Length == 2 && !NameCallingUsedLast && Utility.RandomInt(1, 100) < nameCallingChance)
                Quest sceneQst = SceneChild1.GetOwningQuest()
                bool started = sceneQst.Start()
                TTM_Debug.debug("ReadyForcegreetEvent() NameCalling02: sceneQst: " + sceneQst + "  started: " + started)
                if(started)
                    eventNumber = 7
                    NameCallingUsedLast = True
                    ; ResortChildrenForNameCalling()
                    BlockGames(scene2Children[0])
                    BlockGames(scene2Children[1])
                    SceneChild1.ForceRefTo(scene2Children[0])
                    SceneChild2.ForceRefTo(scene2Children[1])
                    scene2Children[0].SetActorValue("Variable07", eventNumber)
                    scene2Children[1].SetActorValue("Variable07", eventNumber)
                    scene2Children[0].MoveTo(SchedulerSceneMarker1.GetReference())
                    scene2Children[1].MoveTo(SchedulerSceneMarker2.GetReference())
                    scene2Children[0].EvaluatePackage()
                    scene2Children[1].EvaluatePackage()
                    TTM_DialoguesChildrenScenes_SceneNameCalling02.Start()
                    TTM_Debug.debug("ReadyForcegreetEvent() NameCalling02: sceneChild1: " + scene2Children[0] + "  sceneChild2: " + scene2Children[1])
                    return
                endif
            endif
        endif

		;EVENT 10-13 - GENERIC EVENTS
		;Randomly select from one of the other events.
		if (eventNumber == 0)
			eventNumber = Utility.RandomInt(10, 13)
			eventChild = MARAS.GetWeightedRandomChild(currentLoc)
            TTM_Debug.debug("ReadyForcegreetEvent() Generic: eventChild: " + eventChild + "  eventNumber: " + eventNumber)
		endif

		;For all of the non-Generic Forcegreets, block games on the selected child to prevent interference.
		if (eventNumber < 10)
			BlockGames(eventChild)
		EndIf

		;If we used a Name Calling scene last time around, but we're doing something different this time, clear the flag.
		if (NameCallingUsedLast && eventNumber != 6 && eventNumber != 7)
			NameCallingUsedLast = False
		EndIf

        if(eventChild)
            ;Trigger the selected event.
            eventChild.SetActorValue("Variable07", eventNumber)
            eventChild.EvaluatePackage()
        endif

	EndIf
EndFunction

Function PlayerBeggarAbility()
    FavorJobsBeggarsAbility.Cast(Game.GetPlayer(), Game.GetPlayer())
    FavorJobsBeggarMessage.Show()
EndFunction




;Create the Armor corresponding to the child's clothes, equip it, then discard the token in their inventory.
Function EquipNewClothes(Actor child)
; 	;Debug.Trace("Verify!")
	if (clothesToRemove != None)
		child.UnequipItem(clothesToRemove)
		clothesToRemove = None
	EndIf
	if (clothesToEquip == ClothesChild01Male || clothesToEquip == ClothesChild01Female)
		clothesToRemove = ClothesChild01
		child.SetOutfit(ChildOutfit01, True)
		child.EquipItem(ClothesChild01, True)
	ElseIf (clothesToEquip == ClothesChild02Male || clothesToEquip == ClothesChild02Female)
		clothesToRemove = ClothesChild02
		child.SetOutfit(ChildOutfit02, True)
		child.EquipItem(ClothesChild02, True)
	ElseIf (clothesToEquip == ClothesChild03Male || clothesToEquip == ClothesChild03Female)
		clothesToRemove = ClothesChild03
		child.SetOutfit(ChildOutfit03, True)
		child.EquipItem(ClothesChild03, True)
	ElseIf (clothesToEquip == ClothesChild04Male || clothesToEquip == ClothesChild04Female)
		clothesToRemove = ClothesChild04
		child.SetOutfit(ChildOutfit04, True)
		child.EquipItem(ClothesChild04, True)
	ElseIf (clothesToEquip == ClothesChild05Male || clothesToEquip == ClothesChild05Female)
		clothesToRemove = ClothesChild05
		child.SetOutfit(ChildOutfit05, True)
		child.EquipItem(ClothesChild05, True)
	Else
; 		Debug.Trace("ERROR: Trying to equip clothes not in the list. " + clothesToEquip)
	EndIf
	child.RemoveItem(clothesToEquip, 1)
; 	;Debug.Trace("New clothes equipped.")
EndFunction

;Unequip the child's current clothes, create a token representing them, and move that token to the chest.
;For cleanliness, we'll do this with a helper function.
Function RemoveOldClothes(Actor child)
	RemoveOldClothesIfPresent(child, ClothesChild01, ClothesChild01Male, ClothesChild01Female)
	RemoveOldClothesIfPresent(child, ClothesChild02, ClothesChild02Male, ClothesChild02Female)
	RemoveOldClothesIfPresent(child, ClothesChild03, ClothesChild03Male, ClothesChild03Female)
	RemoveOldClothesIfPresent(child, ClothesChild04, ClothesChild04Male, ClothesChild04Female)
	RemoveOldClothesIfPresent(child, ClothesChild05, ClothesChild05Male, ClothesChild05Female)
EndFunction

;In game, children's clothing exists as three objects:
; - The actual clothes the children wear are non-playable Armor.
; - 'Boy' and 'Girl' MiscObject clothes tokens.
;   - Because non-playable items don't show up in the player's inventory, but we want the player to be able to give children clothes,
;     we represent the clothes with MiscObjects that are manipulated by this function and the following one.
Function RemoveOldClothesIfPresent(Actor child, Armor clothes, MiscObject maleToken, MiscObject femaleToken)
	if ((child.GetItemCount(clothes) > 0 && !child.IsEquipped(clothes)) || child.GetItemCount(clothes) > 1)
		child.RemoveItem(clothes)
		if (child.GetActorBase().GetSex() == 0)
; 			;Debug.Trace("Unequipped " + clothes + " and moved " + maleToken + " to chest.")
			; CurrentChildsChest.GetReference().AddItem(maleToken)
		Else
; 			;Debug.Trace("Unequipped " + clothes + " and moved " + femaleToken + " to chest.")
			; CurrentChildsChest.GetReference().AddItem(femaleToken)
		EndIf
	EndIf
EndFunction



;Give Player Gift Event: Roll up a poor gift if appropriate.
Function FGReceivePoorGift(Actor child)
	Formlist temp = BYOHRelationshipAdoptionGifts_Poor
	int roll = Utility.RandomInt(0, temp.GetSize() - 1)
	Game.GetPlayer().AddItem(temp.GetAt(roll), 1)
	ChangePoorCount(2)
EndFunction

;Give Player Gift Event: Roll up a gift, give it, and deduct the cost.
Function FGReceiveGift(Actor child)
	Formlist list = PickGiftList(child.GetItemCount(Gold001))			;Decide which list of gifts to use.
	int roll = Utility.RandomInt(0, list.GetSize())						;Roll for a gift on the list.
	int luck = 1																;Small chance of giving two of an item.
	if (Utility.RandomInt() > 90)
		luck = 2
	EndIf
	Game.GetPlayer().AddItem(list.GetAt(roll), luck)						;Give the gift
	int deduction = PickDeduction(child.GetItemCount(Gold001))			;Determine how much to 'charge' the child for the gift.
	deduction = (deduction / 2) + Utility.RandomInt(0, deduction / 2)
; 	;Debug.Trace("Deducting: " + deduction)
	child.RemoveItem(Gold001, deduction * luck)
EndFunction

;Name-Calling Events (6 & 7)
;Player cuts off the Name-calling event.
Function BreakNameCalling(bool orderToRoom, Actor childToOrder)
    ForcegreetEventReady = False
    Actor child1 = SceneChild1.GetActorRef()
	Actor child2 = SceneChild2.GetActorRef()
    TTM_Debug.debug("BreakNameCalling() orderToRoom(" + orderToRoom + ") childToOrder(" + childToOrder + ") child1(" + child1 + ") child2(" + child2 + ")")
    TTM_DialoguesChildrenScenes_SceneNameCalling01.Stop()
    TTM_DialoguesChildrenScenes_SceneNameCalling02.Stop()
    if(child1)
        ResetForceGreeting(child1)
        UnblockGames(child1)
        child1.EvaluatePackage()
    endif

    if(child2)
        ResetForceGreeting(child2)
        UnblockGames(child2)
        child2.EvaluatePackage()
    endif

    if (orderToRoom && childToOrder)
		MARAS.IssueChildOrder(childToOrder, 1.3, 3)
        childToOrder.EvaluatePackage()
	EndIf
EndFunction

;----------------------------------------------------------------------------------------------------
;LOCATION CHANGED EVENTS
;-----------------------

;Respond to Player LocationChanged events. This includes things like:
; - Starting or stopping the Civil War Siege handler quest.
; - Moving the player's family to a new home.
; - Triggering Return Home events.
Function PlayerLocationChanged(Location newLoc, Location oldLoc, bool onGameLoad = false)
    ForcegreetEventReady = false
    TTM_Debug.debug("TTM_ServiceChildrenLogic:PlayerLocationChanged:newLoc:" + newLoc + ":oldLoc:" + oldLoc + ":currLoc:"+ Game.GetPlayer().GetCurrentLocation())
    ;Determine whether to trigger a Welcome Home event.
    bool isNewLocPlayerHouse = newLoc.HasKeywordString("LocTypePlayerHouse")
    bool isOldLocPlayerHouse = oldLoc.HasKeywordString("LocTypePlayerHouse")
    bool enteredPlayerHouse = (!isOldLocPlayerHouse && isNewLocPlayerHouse)
    bool leftPlayerHouse = (isOldLocPlayerHouse && !isNewLocPlayerHouse)
    bool needQstStart = false

    TTM_Debug.debug("TTM_ServiceChildrenLogic:PlayerLocationChanged:enteredPlayerHouse:" + enteredPlayerHouse)
    TTM_Debug.debug("TTM_ServiceChildrenLogic:PlayerLocationChanged:leftPlayerHouse:" + leftPlayerHouse)
    Quest TTM_ChildreSandboxQst = TTM_Data.GetChildreSandboxQuest()

    TTM_Debug.debug("TTM_ServiceChildrenLogic:PlayerLocationChanged:onGameLoad:" + onGameLoad)
    TTM_Debug.debug("TTM_ServiceChildrenLogic:PlayerLocationChanged:TTM_ChildreSandboxQst.IsRunning():" + TTM_ChildreSandboxQst.IsRunning())

    if(onGameLoad && isNewLocPlayerHouse)
        if(TTM_ChildreSandboxQst.IsRunning())
            LocationAlias qstHomeLocAL = TTM_ChildreSandboxQst.GetAliasByName("CurrentHomeLocation") as LocationAlias
            Location qstHomeLoc = qstHomeLocAL.GetLocation()
            if(qstHomeLoc != newLoc)
                TTM_Debug.debug("TTM_ServiceChildrenLogic:PlayerLocationChanged:qstHomeLoc:" + qstHomeLoc)
                TTM_ChildreSandboxQst.Stop()
                needQstStart = true
            endif
        else
            needQstStart = true
        endif
    endif

    if(onGameLoad && !isNewLocPlayerHouse && TTM_ChildreSandboxQst.IsRunning())
        TTM_ChildreSandboxQst.Stop()
    endif

    TTM_Debug.debug("TTM_ServiceChildrenLogic:PlayerLocationChanged:enteredPlayerHouse:" + enteredPlayerHouse + ":needQstStart:" + needQstStart)

    if(enteredPlayerHouse || needQstStart)
        Actor[] children = MARAS.GetChildrenAssignedToHouse(newLoc)

        TTM_Debug.debug("TTM_ServiceChildrenLogic:PlayerLocationChanged:children.Length:" + children.Length)

        if(children.Length > 0)
            bool started = TTM_ChildreSandboxQst.Start()
            TTM_Debug.debug("TTM_ServiceChildrenLogic:PlayerLocationChanged:started:" + started)
            int tries = 4
            int j = 0
            while (j < tries && !TTM_ChildreSandboxQst.IsRunning())
                Utility.Wait(0.5)
                j += 1
            endwhile
            int i = 0
            while(i < numOfChildrenPerLocation && i < children.Length)
                Actor child = children[i]
                int childAliasIndex = i + 1
                string normalizedIndex = ""
                if(childAliasIndex < 10)
                    normalizedIndex = "0" + childAliasIndex
                else
                    normalizedIndex = "" + childAliasIndex
                endif
                TTM_Utils.SetActorAlias(TTM_ChildreSandboxQst, "Child0" + normalizedIndex, child)
                i += 1
            endwhile
            ReadyForcegreetEvent(newLoc)
        endif
    endif

    if(!isNewLocPlayerHouse && TTM_ChildreSandboxQst.IsRunning())
        TTM_ChildreSandboxQst.Stop()
    endif

    if(enteredPlayerHouse || leftPlayerHouse)
        Actor[] children = MARAS.GetChildrenAssignedToHouse(newLoc)
        if(children.Length > 0)
            int i = 0
            while(i < numOfChildrenPerLocation && i < children.Length)
                Actor child = children[i]
                child.EvaluatePackage()
                i += 1
            endwhile
        endif
    endif
EndFunction

;Respond to Child LocationChanged events. This includes things like:
; - Triggering a 'dirty' move for newly-adopted children when they reach their new home.
Function ChildLocationChanged(Actor child, Location newLoc, Location oldLoc)
	;If the child has moved to a new location, we can safely unblock games on them if they had been blocked.
	UnblockGames(child)
EndFunction

Function MigrateAllChildren()
    TTM_Debug.debug("TTM_ServiceChildrenLogic: MigrateAllChildren")
    if (migrationDone)
        TTM_Debug.debug("TTM_ServiceChildrenLogic: MigrateAllChildren: Migration already done.")
        return
    endif

    Quest childrenSandboxQst = TTM_Data.GetOriginalChildrenSandboxQuest()
    Quest childrenAdoptionQst = TTM_Data.GetOriginalChildrenAdoptionQuest()
    Quest childrenNewAdoptionQst = TTM_Data.GetOriginalChildrenNewAdoptionQuest()
    bool multipleAdoptionsInstalled = (Game.GetModByName("HearthfireMultiKid.esp") != 255)

    ;------------------------------------------------------------------------------------------------
    ; PASS 1: Migrate 3rd-party fertility mod records (FMR / BF)
    ;------------------------------------------------------------------------------------------------
    int fertilityEngine = TTM_ServiceFertility.GetActiveEngine()
    if (fertilityEngine == 1)
        TTM_Debug.debug("TTM_ServiceChildrenLogic: MigrateAllChildren: Pass 1 - Migrating FMR records")
        TTM_ServiceFertilityFMR.Migrate()
    elseif (fertilityEngine == 2)
        TTM_Debug.debug("TTM_ServiceChildrenLogic: MigrateAllChildren: Pass 1 - Migrating BF records")
        TTM_ServiceFertilityBF.Migrate()
    endif

    ;------------------------------------------------------------------------------------------------
    ; PASS 2: Migrate Vanilla & Hearthfire Adopted Children from running adoption quest aliases
    ;------------------------------------------------------------------------------------------------
    if (childrenAdoptionQst.IsRunning() || childrenSandboxQst.IsRunning() || childrenNewAdoptionQst.IsRunning())
        Actor spouseToAdoptWith = TTM_Utils.GetActorAlias(childrenAdoptionQst, "Spouse")
        Location houseToMove = TTM_Utils.GetChildMigrationPlayerHouse()
        TTM_Debug.debug("TTM_ServiceChildrenLogic: MigrateAllChildren: Pass 2 Adoption - spouse: " + spouseToAdoptWith + ", house: " + houseToMove)

        int maxChildren = 2
        if (multipleAdoptionsInstalled)
            maxChildren = 6
        endif

        Location playerHouse = TTM_Utils.GetChildMigrationPlayerHouse()

        int i = 1
        while (i <= maxChildren)
            Actor child = TTM_Utils.GetActorAlias(childrenAdoptionQst, "Child" + i)
            if (child != none)
                TTM_Debug.debug("TTM_ServiceChildrenLogic: MigrateAllChildren: Migrating adopted child " + i + ": " + child)
                ; multipleAdoptions = true prevents AdoptChild from stopping the adoption quest mid-loop
                AdoptChild(child, spouseToAdoptWith, houseToMove, multipleAdoptions = true, isMigrating = true)
            endif
            i += 1
        endwhile
    endif

    ;------------------------------------------------------------------------------------------------
    ; PASS 3: Teardown, Cleanup & Re-enable Adoptable Children
    ;------------------------------------------------------------------------------------------------
    migrationDone = true

    BYOHRelationshipAdoptableScript childrenAdoptableQst = TTM_Data.GetOriginalChildrenAdoptableQuest() as BYOHRelationshipAdoptableScript

    TTM_Debug.debug("TTM_ServiceChildrenLogic: MigrateAllChildren: Pass 3 Teardown - Stopping vanilla adoption quests")
    if (childrenSandboxQst != none && childrenSandboxQst.IsRunning())
        childrenSandboxQst.Reset()
        childrenSandboxQst.Stop()
    endif

    if (childrenAdoptionQst != none && childrenAdoptionQst.IsRunning())
        childrenAdoptionQst.Reset()
        childrenAdoptionQst.Stop()
    endif

    if (childrenNewAdoptionQst != none && childrenNewAdoptionQst.IsRunning())
        childrenNewAdoptionQst.Reset()
        childrenNewAdoptionQst.Stop()
    endif

    if (childrenAdoptableQst != none)
        childrenAdoptableQst.Reset()
    endif

    if (multipleAdoptionsInstalled)
        Spell hmkSpell = TTM_Data.GetHMKSpell()
        if (hmkSpell != none)
            TTM_Data.GetPlayer().RemoveSpell(hmkSpell)
        endif

        Quest hmkManager = TTM_Data.GetHMKCustomHomeManager()
        if (hmkManager != none)
            hmkManager.Stop()
        endif

        Quest hmkValidator = TTM_Data.GetHMKCustomHomeValidator()
        if (hmkValidator != none)
            hmkValidator.Stop()
        endif
    endif

    ReenableAdoptableChildren()
    TTM_Debug.info("TTM_ServiceChildrenLogic: MigrateAllChildren completed successfully.")
EndFunction

Function ReenableAdoptableChildren()
    ;Re-enable orphans from orphanage for adoption
	if (BYOHRelationshipAdoptableOrphanage.IsRunning())
		(BYOHRelationshipAdoptableOrphanage as BYOHRelationshipAdoptableOrphanageSc).EnableOrphanedOrphanageAdoptions()
	EndIf

	;Re-enable street urchins for adoption
	if (BYOHRelationshipAdoptableUrchins.IsRunning())
        BYOHRelationshipAdoptableUrchinScript urchinQst = BYOHRelationshipAdoptableUrchins as BYOHRelationshipAdoptableUrchinScript
        if (urchinQst.Sofie.GetActorRef() != None)
            urchinQst.Sofie.GetActorRef().SetFactionRank(BYOHRelationshipAdoptableFaction, 0)
            urchinQst.Sofie.GetActorRef().SetRelationshipRank(Game.GetPlayer(), 0)
        EndIf
        if (urchinQst.Alesan.GetActorRef() != None)
            urchinQst.Alesan.GetActorRef().SetFactionRank(BYOHRelationshipAdoptableFaction, 0)
            urchinQst.Alesan.GetActorRef().SetRelationshipRank(Game.GetPlayer(), 0)
        EndIf
        if (urchinQst.Lucia.GetActorRef() != None)
            urchinQst.Lucia.GetActorRef().SetFactionRank(BYOHRelationshipAdoptableFaction, 0)
            urchinQst.Lucia.GetActorRef().SetRelationshipRank(Game.GetPlayer(), 0)
        EndIf
        if (urchinQst.Blaise.GetActorRef() != None)
            urchinQst.Blaise.GetActorRef().SetFactionRank(BYOHRelationshipAdoptableFaction, 0)
            urchinQst.Blaise.GetActorRef().SetRelationshipRank(Game.GetPlayer(), 0)
        EndIf
	EndIf
EndFunction

;/
  SpawnSyntheticChild: Spawns a physical child actor for a synthetic young child record (Stage 2)
  using PlaceAtMe with persistent flag = true. Re-keys synthetic FormID to physical actor reference.
/;
Function SpawnSyntheticChild(int syntheticID)
    if (syntheticID == 0)
        TTM_Debug.warn("TTM_ServiceChildrenLogic: SpawnSyntheticChild called with 0 ID.")
        return
    endif

    int stage = MARAS.GetChildStage(syntheticID)
    if (stage != 2)
        TTM_Debug.warn("TTM_ServiceChildrenLogic: SpawnSyntheticChild: Child " + syntheticID + " is not in stage 2 (stage=" + stage + "). Only young children can be spawned.")
        return
    endif

    TTM_Debug.info("TTM_ServiceChildrenLogic: SpawnSyntheticChild: Spawning child " + syntheticID + ".")

    string childName = MARAS.GetChildName(syntheticID)
    int gender = MARAS.GetChildGender(syntheticID)
    if (gender != 0 && gender != 1)
        gender = Utility.RandomInt(0, 1)
        MARAS.SetChildGender(syntheticID, gender)
    endif

    Actor player = TTM_Data.GetPlayer()
    Actor bioMother = MARAS.GetBioMother(syntheticID)
    Actor bioFather = MARAS.GetBioFather(syntheticID)
    Actor guardianA = MARAS.GetGuardianA(syntheticID)
    Actor guardianB = MARAS.GetGuardianB(syntheticID)

    Race childRace = None
    if (bioMother != None)
        childRace = bioMother.GetRace()
    elseif (bioFather != None)
        childRace = bioFather.GetRace()
    elseif (guardianA != None)
        childRace = guardianA.GetRace()
    elseif (guardianB != None)
        childRace = guardianB.GetRace()
    else
        childRace = player.GetRace()
    endif

    ActorBase childBase = None

    ; 1. Try to resolve child base from Fertility Mode Reloaded (FMR) if loaded
    if (Game.GetModByName("Fertility Mode.esm") != 255)
        Quest storageQuest = Game.GetFormFromFile(0xd62, "Fertility Mode.esm") as Quest
        if (storageQuest != None)
            _JSW_BB_Storage fmrStorage = storageQuest as _JSW_BB_Storage
            _JSW_BB_Utility fmrUtility = storageQuest as _JSW_BB_Utility
            if (fmrStorage != None && fmrUtility != None && fmrStorage.Children.Length > 0)
                int raceIdx = fmrUtility.GetRaceIndex(childRace)
                if (raceIdx >= 0 && raceIdx < 10)
                    int baseIdx = 2 * raceIdx + gender
                    if (baseIdx >= 0 && baseIdx < fmrStorage.Children.Length)
                        childBase = fmrStorage.Children[baseIdx]
                        TTM_Debug.debug("TTM_ServiceChildrenLogic: Resolved childBase for child " + syntheticID + ", gender " + gender + ", race " + raceIdx + ", from FMR: " + childBase)
                    endif
                endif
            endif
        endif
    endif

    ; 2. Resolve the child base from Beeing Female's per-race baby actor lists (filled by its AddOn system)
    if (childBase == None && Game.GetModByName("BeeingFemale.esm") != 255 && childRace != None)
        string babyKey = "FW.AddOn.BabyActor_Female"
        if (gender == 0)
            babyKey = "FW.AddOn.BabyActor_Male"
        endif
        if (player == bioMother || player == bioFather)
            childBase = StorageUtil.FormListGet(childRace, babyKey + "Player", 0) as ActorBase
        endif
        if (childBase == None)
            childBase = StorageUtil.FormListGet(childRace, babyKey, 0) as ActorBase
        endif
        if (childBase != None)
            TTM_Debug.debug("TTM_ServiceChildrenLogic: Resolved childBase for child " + syntheticID + ", gender " + gender + ", from Beeing Female: " + childBase)
        endif
    endif

    ; Without a supported fertility mod there are no synthetic children to spawn, so there is no fallback base.

    if (childBase == None)
        TTM_Debug.err("TTM_ServiceChildrenLogic: Failed to resolve childBase for child " + syntheticID)
        return
    endif

    ; Spawn child actor using PlaceAtMe with persistent flag = true
    Actor spawnedChild = player.PlaceAtMe(childBase, 1, true) as Actor
    if (spawnedChild == None)
        TTM_Debug.err("TTM_ServiceChildrenLogic: PlaceAtMe failed to spawn child for " + syntheticID)
        return
    endif

    TTM_Debug.debug("TTM_ServiceChildrenLogic: child name to set: " + childName)
    if (childName != "")
        spawnedChild.SetDisplayName(childName, true)
    endif

    Actor donor = none
    ; Match hair color to mother/father if available
    if(bioMother != none && bioFather != none)
        if(Utility.RandomInt(0,1) == 1)
            donor = bioMother
        else
            donor = bioFather
        endif
    elseif (bioMother != None)
        donor = bioMother
    elseif (bioFather != None)
        donor = bioFather
    endif

    if(donor != none)
        spawnedChild.GetActorBase().SetHairColor(donor.GetActorBase().GetHairColor())
    endif

    ; Ingest / Adopt spawned child into MARAS
    bool isPlayerChild = (player == bioMother || player == bioFather)
    if (!isPlayerChild && ((bioMother != none && MARAS.IsNPCStatus(bioMother, "married")) || (bioFather != none && MARAS.IsNPCStatus(bioFather, "married"))))
        isPlayerChild = true
    endif

    TTM_Debug.debug("TTM_ServiceChildrenLogic: isPlayerChild: " + isPlayerChild)

    if (isPlayerChild)
        AdoptChild(spawnedChild, bioMother = bioMother, bioFather = bioFather, syntheticID = syntheticID, childStage = 2)
    else
        Actor gA = guardianA
        Actor gB = guardianB
        if (gA == None && gB == None)
            gA = bioMother
            gB = MARAS.GetMotherPartner(syntheticID)
            if (gB == None)
                gB = bioFather
            endif
        endif
        MARAS.RegisterChildActor(spawnedChild, gA, gB, bioMother = bioMother, bioFather = bioFather, syntheticID = syntheticID, stage = 2)
    endif

    spawnedChild.EvaluatePackage()

    Debug.Notification(spawnedChild.GetDisplayName() + " has arrived in Skyrim.")
    TTM_Debug.info("TTM_ServiceChildrenLogic: Successfully spawned child actor " + spawnedChild.GetDisplayName() + " (FormID " + spawnedChild.GetFormID() + ") from synthetic ID " + syntheticID)
EndFunction


