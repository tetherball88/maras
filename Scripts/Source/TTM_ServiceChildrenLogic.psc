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
    int waitTicks = 0
    while (adoptionInProgress && waitTicks < 300)
        Utility.WaitMenuMode(0.2)
        waitTicks += 1
    endwhile
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
                spouseToAdopWith = TTM_Utils.ChooseSpouseToAdoptWithMsg(child)
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
        houseToMove = MARAS.ShowChooseHomeModal(child, true)
    endif
    TTM_Debug.debug("TTM_ServiceChildrenLogic: AdoptChild: spouseToAdopWith = " + spouseToAdopWith + ", houseToMove = " + houseToMove)

    ; Register child with MARAS using the single authoritative ingestion pipeline
    ; C++ RegisterChildActor snapshots native factions, clears external factions, and assigns household factions
    MARAS.RegisterChildActor(child, player, spouseToAdopWith, houseToMove, bioMother, bioFather, syntheticChildID, childStage, sendEvent = !isMigrating)

    if (!isMigrating)
        lastChildAdoptedTimestamp = Utility.GetCurrentGameTime() + 3
        playerLastSeen = Utility.GetCurrentGameTime()
        if (houseToMove != none && player.GetCurrentLocation() == houseToMove)
            PlayerLocationChanged(houseToMove, none, true)
        endif
        child.EvaluatePackage()
        if (syntheticChildID == 0)
            Game.AddAchievement(61)
        endif
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
	MARAS.RecordChildInteraction(childToPlay)
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
    if (orderId < 10.0)
        MARAS.RecordChildInteraction(child)
    endif
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
	MARAS.RecordChildInteraction(child)
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
    Location houseLoc = CurrentHomeHouse.GetLocation()
    if (houseLoc == None && childChest != None)
        houseLoc = childChest.GetCurrentLocation()
    endif
    MARAS.RefillChildChestFromLists(childChest, houseLoc, BYOHRelationshipAdoptionGifts_Junk, BYOHRelationshipAdoptionGifts_0000, BYOHRelationshipAdoptionGifts_0025, BYOHRelationshipAdoptionGifts_0050, BYOHRelationshipAdoptionGifts_0100, BYOHRelationshipAdoptionGifts_0250, BYOHRelationshipAdoptionGifts_0500, BYOHRelationshipAdoptionGifts_1000)
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
	MARAS.RecordChildInteraction(child)
EndFunction

;Reject the pet under consideration for a child, recording retry timestamp (15d cooldown).
Function RejectPetForChild(Actor child)
	if (child)
		MARAS.RecordChildPetRetry(child)
	endif
EndFunction

;Ban pet events for a specific child or globally.
Function BanPetForChild(Actor child)
	MARAS.SetChildPetBanned(child, true)
EndFunction

Function MakePetFollower(Actor dog)
    DialogueFollower.SetAnimal(dog)
EndFunction


Function GiveAllowance(Actor child, int AllowanceAmount)
    PoorCounter = MARAS.ProcessChildAllowance(child, AllowanceAmount)
    if (AllowanceAmount > 0)
        PlayerBeggarAbility()
    endif
EndFunction

;Evaluate the pet in the TransientPet alias to determine whether it's suitable for adoption, and whether it's a dog.
;Returns the int (Faction Rank) representing the suitability. Stores the 'dog' status as AV06=1.
int Function EvaluatePetSuitability()
	return MARAS.EvaluateChildPetSuitability(TransientPet.GetActorRef(), BYOHRelationshipAdoption_PetDogsList, BYOHRelationshipAdoption_PetAllowedRacesList, BYOHRelationshipPotentialPetFaction)
EndFunction

;----------------------------------------------------------------------------------------------------
;CRITTER EVENTS
;--------------
;Reminder: Pets are player follower animals (dogs), Critters are kid animals (hares, foxes, etc.).


;Spawn a new critter the child will ask to keep.
Function SetupCritter(Actor child)
	Actor newCritter = MARAS.SpawnChildTransientCritter(child, BYOHRelationshipAdoption_CrittersMale, BYOHRelationshipAdoption_CrittersFemale)
	if (child)
		child.EvaluatePackage()
		child.MoveToPackageLocation()
	endif
	if (newCritter)
		newCritter.EvaluatePackage()
		newCritter.MoveToPackageLocation()
	endif
EndFunction

;Adopt the critter under consideration into the family.
Function AdoptCritter(Actor child)
	MARAS.AdoptChildTransientCritter(child)
EndFunction

;Reject the critter under consideration for a child, recording retry timestamp (15d cooldown).
Function RejectCritterForChild(Actor child)
	if (!child)
		return
	endif
	MARAS.RecordChildCritterRetry(child)
	MARAS.DismissChildTransientCritter(child)
EndFunction

;Ban critter events for a specific child or globally.
Function BanCritterForChild(Actor child, bool globalBan = false)
	if (globalBan)
		MARAS.SetGlobalCritterBanned(true)
	elseif (child)
		MARAS.SetChildCritterBanned(child, true)
	endif
	MARAS.DismissChildTransientCritter(child)
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
	if (!ForcegreetEventReady)
		ForcegreetEventReady = True
		StopGames()

		string planJson = MARAS.SelectChildForcegreetEvent(currentLoc, AnimalCompanion.GetActorRef() != None, critterChance, nameCallingChance)
		int eventNumber = MARAS.GetJsonInt(planJson, "eventNumber", 0)
		Actor eventChild = MARAS.GetJsonActor(planJson, "eventChild")

		if (eventNumber == 4)
			TransientPet.ForceRefTo(AnimalCompanion.GetActorRef())
			EvaluatePetSuitability()
		elseif (eventNumber == 5)
			SetupCritter(eventChild)
		elseif (eventNumber == 6 || eventNumber == 7)
			Actor sc1 = MARAS.GetJsonActor(planJson, "sceneChild1")
			Actor sc2 = MARAS.GetJsonActor(planJson, "sceneChild2")
			Quest sceneQst = SceneChild1.GetOwningQuest()
			bool started = (sc1 != None && sc2 != None && sceneQst != None && sceneQst.Start())
			if (started)
				MARAS.CommitNameCallingForcegreet(eventNumber)
				BlockGames(sc1)
				BlockGames(sc2)
				SceneChild1.ForceRefTo(sc1)
				SceneChild2.ForceRefTo(sc2)
				sc1.SetActorValue("Variable07", eventNumber)
				sc2.SetActorValue("Variable07", eventNumber)
				sc1.MoveTo(SchedulerSceneMarker1.GetReference())
				sc2.MoveTo(SchedulerSceneMarker2.GetReference())
				sc1.EvaluatePackage()
				sc2.EvaluatePackage()
				if (eventNumber == 6)
					TTM_DialoguesChildrenScenes_SceneNameCalling01.Start()
				else
					TTM_DialoguesChildrenScenes_SceneNameCalling02.Start()
				endif
				return
			else
				string fbJson = MARAS.SelectGenericChildForcegreetFallback(currentLoc)
				eventNumber = MARAS.GetJsonInt(fbJson, "eventNumber", 0)
				eventChild = MARAS.GetJsonActor(fbJson, "eventChild")
			endif
		endif

		if (eventNumber > 0 && eventNumber < 10 && eventChild)
			BlockGames(eventChild)
		endif

		if (eventChild && eventNumber > 0)
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
	PoorCounter = MARAS.ProcessChildPoorGift(child, BYOHRelationshipAdoptionGifts_Poor)
EndFunction

;Give Player Gift Event: Roll up a gift, give it, and deduct the cost.
Function FGReceiveGift(Actor child)
	MARAS.ProcessChildRegularGift(child, BYOHRelationshipAdoptionGifts_0000, BYOHRelationshipAdoptionGifts_0025, BYOHRelationshipAdoptionGifts_0050, BYOHRelationshipAdoptionGifts_0100, BYOHRelationshipAdoptionGifts_0250, BYOHRelationshipAdoptionGifts_0500, BYOHRelationshipAdoptionGifts_1000)
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
    if (onGameLoad)
        PoorCounter = MARAS.GetChildPoorCounter()
    endif
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

    int keptCount = MARAS.ShowChildMigrationAuditModal()
    TTM_Debug.info("TTM_ServiceChildrenLogic: Child migration audit completed (kept=" + keptCount + ").")

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
  Helper: Resolves the child's genetic Race strictly from biological parents.
/;
Race Function ResolveChildRaceFromLineage(Actor bioMother, Actor bioFather)
    if (bioMother != None)
        return bioMother.GetRace()
    elseif (bioFather != None)
        return bioFather.GetRace()
    endif
    return None
EndFunction

;/
  Helper: Chooses the best actor anchor to PlaceAtMe the newly spawned child/adult actor.
/;
Actor Function GetChildSpawnAnchor(Actor guardianA, Actor guardianB, Actor bioMother, Actor player)
    if (guardianA != None && guardianA != player)
        return guardianA
    elseif (guardianB != None && guardianB != player)
        return guardianB
    elseif (bioMother != None && bioMother != player)
        return bioMother
    endif
    return player
EndFunction

;/
  Helper: Picks a random valid ActorBase from a single Beeing Female StorageUtil FormList holder
  (mirrors FWAddOnManager.PickRandomActorBase in Beeing-Female-NG).
/;
ActorBase Function PickRandomBFActorBase(Form holder, string listKey)
    int c = StorageUtil.FormListCount(holder, listKey)
    if (c <= 0)
        return None
    endif
    ActorBase b = StorageUtil.FormListGet(holder, listKey, Utility.RandomInt(0, c - 1)) as ActorBase
    if (b != None)
        return b
    endif
    int i = 0
    while (i < c)
        b = StorageUtil.FormListGet(holder, listKey, i) as ActorBase
        if (b != None)
            return b
        endif
        i += 1
    endWhile
    return None
EndFunction

;/
  Helper: Resolves an ActorBase from Beeing Female's 3-tier AddOn hierarchy:
  ParentActor (mother/father) -> ParentRace (childRace) -> Global (none, "FW.AddOn.Global_*"),
  checking the "...Player" list first for player-lineage children before falling back to the main list
  (mirrors FWAddOnManager.GetAdultActor / GetBabyActorNew in Beeing-Female-NG).
/;
ActorBase Function ResolveBFActorBaseFromList(Actor bioMother, Actor bioFather, Race childRace, string listName)
    ActorBase b = None
    if (bioMother != None)
        b = PickRandomBFActorBase(bioMother, "FW.AddOn." + listName)
        if (b != None)
            return b
        endif
    endif
    if (bioFather != None)
        b = PickRandomBFActorBase(bioFather, "FW.AddOn." + listName)
        if (b != None)
            return b
        endif
    endif
    if (childRace != None)
        b = PickRandomBFActorBase(childRace, "FW.AddOn." + listName)
        if (b != None)
            return b
        endif
    endif
    return PickRandomBFActorBase(None, "FW.AddOn.Global_" + listName)
EndFunction

ActorBase Function ResolveBFActorBase(Actor bioMother, Actor bioFather, Race childRace, string listName, bool isPlayerLineage)
    if (listName == "")
        return None
    endif
    ActorBase b = None
    if (isPlayerLineage)
        b = ResolveBFActorBaseFromList(bioMother, bioFather, childRace, listName + "Player")
        if (b != None)
            return b
        endif
    endif
    return ResolveBFActorBaseFromList(bioMother, bioFather, childRace, listName)
EndFunction

;/
  SpawnSyntheticChild: Spawns a physical young child actor (Stage 2)
  using PlaceAtMe with persistent flag = true. Re-keys synthetic FormID to physical actor reference.
/;
Function SpawnSyntheticChild(int syntheticID)
    if (syntheticID == 0)
        TTM_Debug.warn("TTM_ServiceChildrenLogic: SpawnSyntheticChild called with 0 ID.")
        return
    endif

    int stage = MARAS.GetChildStage(syntheticID)
    if (stage != 2)
        TTM_Debug.warn("TTM_ServiceChildrenLogic: SpawnSyntheticChild: Child " + syntheticID + " is not in stage 2 (stage=" + stage + "). Use SpawnAdultChild for stage 4.")
        return
    endif

    TTM_Debug.info("TTM_ServiceChildrenLogic: SpawnSyntheticChild: Spawning young child " + syntheticID + ".")

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
    Race childRace = ResolveChildRaceFromLineage(bioMother, bioFather)

    ActorBase childBase = None
    bool usedBFBase = false

    ; 1. Resolve young child base from Fertility Mode Reloaded (FMR) if loaded
    if (Game.GetModByName("Fertility Mode.esm") != 255)
        Quest storageQuest = Game.GetFormFromFile(0xd62, "Fertility Mode.esm") as Quest
        if (storageQuest != None)
            _JSW_BB_Storage fmrStorage = storageQuest as _JSW_BB_Storage
            _JSW_BB_Utility fmrUtility = storageQuest as _JSW_BB_Utility
            if (fmrStorage != None && fmrUtility != None && fmrStorage.Children.Length > 0)
                int raceIdx = -1
                if (childRace != None)
                    raceIdx = fmrUtility.GetRaceIndex(childRace)
                endif
                if (raceIdx < 0 || raceIdx >= 10)
                    raceIdx = MARAS.GetChildSavedRaceIndex(syntheticID)
                endif
                if (raceIdx < 0 || raceIdx >= 10)
                    raceIdx = 6 ; Nord fallback
                endif
                int baseIdx = 2 * raceIdx + gender
                if (baseIdx >= 0 && baseIdx < fmrStorage.Children.Length)
                    childBase = fmrStorage.Children[baseIdx]
                    TTM_Debug.debug("TTM_ServiceChildrenLogic: Resolved young childBase for child " + syntheticID + ", gender " + gender + ", race " + raceIdx + ", from FMR: " + childBase)
                endif
            endif
        endif
    endif

    ; 2. Resolve young child base from Beeing Female's AddOn BabyActor hierarchy + built-in fallback
    if (childBase == None && Game.GetModByName("BeeingFemale.esm") != 255)
        bool isPlayerLineage = (player == bioMother || player == bioFather)
        string babyListName = "BabyActor_Female"
        if (gender == 0)
            babyListName = "BabyActor_Male"
        endif
        childBase = ResolveBFActorBase(bioMother, bioFather, childRace, babyListName, isPlayerLineage)
        if (childBase == None)
            ; Fallback to BeeingFemale.esm's built-in young child ActorBases (mirrors FWBabyItemList.ResolveConfiguredFallbackChildBase)
            if (isPlayerLineage)
                if (gender == 0)
                    childBase = Game.GetFormFromFile(0x02AB16, "BeeingFemale.esm") as ActorBase
                else
                    childBase = Game.GetFormFromFile(0x02AB17, "BeeingFemale.esm") as ActorBase
                endif
            endif
            if (childBase == None)
                if (gender == 0)
                    childBase = Game.GetFormFromFile(0x001D98, "BeeingFemale.esm") as ActorBase
                else
                    childBase = Game.GetFormFromFile(0x001D99, "BeeingFemale.esm") as ActorBase
                endif
            endif
        endif
        if (childBase != None)
            usedBFBase = true
            TTM_Debug.debug("TTM_ServiceChildrenLogic: Resolved young childBase for child " + syntheticID + ", gender " + gender + ", from Beeing Female: " + childBase)
        endif
    endif

    if (childBase == None)
        TTM_Debug.err("TTM_ServiceChildrenLogic: Failed to resolve young childBase for child " + syntheticID)
        return
    endif

    Actor spawnAnchor = GetChildSpawnAnchor(guardianA, guardianB, bioMother, player)
    Actor spawnedChild = spawnAnchor.PlaceAtMe(childBase, 1, true) as Actor
    if (spawnedChild == None)
        TTM_Debug.err("TTM_ServiceChildrenLogic: PlaceAtMe failed to spawn young child for " + syntheticID)
        return
    endif

    if (usedBFBase)
        TTM_ServiceFertilityBF.TakeOverChild(spawnedChild)
        StorageUtil.SetFloatValue(spawnedChild, "FW.Child.DOB", Utility.GetCurrentGameTime())
        StorageUtil.SetIntValue(spawnedChild, "FW.AddOn.StartGrowing", 1)
    endif

    if (childName != "")
        spawnedChild.SetDisplayName(childName, true)
    endif

    if (MARAS.RegisterNpcFertilityChildSpawn(spawnedChild, syntheticID, bioMother, bioFather, 2, false))
        spawnedChild.EvaluatePackage()
        spawnedChild.MoveToPackageLocation()
    else
        Actor coSpouse = MARAS.GetFertilitySpawnCoSpouse(syntheticID, bioMother, bioFather)
        AdoptChild(spawnedChild, spouseToAdopWith = coSpouse, bioMother = bioMother, bioFather = bioFather, syntheticID = syntheticID, childStage = 2)
    endif

    spawnedChild.EvaluatePackage()

    Debug.Notification(spawnedChild.GetDisplayName() + " has arrived in Skyrim.")
    TTM_Debug.info("TTM_ServiceChildrenLogic: Successfully spawned young child actor " + spawnedChild.GetDisplayName() + " (FormID " + spawnedChild.GetFormID() + ") from synthetic ID " + syntheticID)
EndFunction

;/
  SpawnAdultChild: Spawns a physical adult child actor (Stage 4) using trained class/race templates
  and registers them as an independent traveling adventurer available for hire.
/;
Function SpawnAdultChild(int syntheticID)
    if (syntheticID == 0)
        TTM_Debug.warn("TTM_ServiceChildrenLogic: SpawnAdultChild called with 0 ID.")
        return
    endif

    int stage = MARAS.GetChildStage(syntheticID)
    if (stage != 4)
        TTM_Debug.warn("TTM_ServiceChildrenLogic: SpawnAdultChild: Child " + syntheticID + " is not in stage 4 (stage=" + stage + ").")
        return
    endif

    TTM_Debug.info("TTM_ServiceChildrenLogic: SpawnAdultChild: Spawning adult child " + syntheticID + ".")

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
    Race childRace = ResolveChildRaceFromLineage(bioMother, bioFather)

    ActorBase adultBase = None
    bool usedBFBase = false
    ActorBase fmrBase = None
    ActorBase bfBase = None

    ; 1. Resolve trained adult child base from Fertility Mode Reloaded (FMR) if loaded
    if (Game.GetModByName("Fertility Mode.esm") != 255)
        Quest storageQuest = Game.GetFormFromFile(0xd62, "Fertility Mode.esm") as Quest
        if (storageQuest != None)
            _JSW_BB_Storage fmrStorage = storageQuest as _JSW_BB_Storage
            _JSW_BB_Utility fmrUtility = storageQuest as _JSW_BB_Utility
            if (fmrStorage != None && fmrUtility != None && fmrStorage.AdultChildren.Length > 0)
                int raceIdx = -1
                if (childRace != None)
                    raceIdx = fmrUtility.GetRaceIndex(childRace)
                endif
                if (raceIdx < 0 || raceIdx >= 10)
                    raceIdx = MARAS.GetChildSavedRaceIndex(syntheticID)
                endif
                if (raceIdx < 0 || raceIdx >= 10)
                    raceIdx = 6 ; Nord fallback
                endif
                int trainClass = MARAS.GetChildTrainingClass(syntheticID)
                int fmrSlot = Utility.RandomInt(0, 10)
                int adultIdx = (fmrSlot * 20) + (raceIdx * 2) + gender
                if (adultIdx >= 0 && adultIdx < fmrStorage.AdultChildren.Length)
                    fmrBase = fmrStorage.AdultChildren[adultIdx]
                    TTM_Debug.debug("TTM_ServiceChildrenLogic: Resolved fmrBase for child " + syntheticID + ", class " + trainClass + " (fmrSlot " + fmrSlot + "), gender " + gender + ", race " + raceIdx + ", from FMR: " + fmrBase)
                endif
            endif
        endif
    endif

    ; 2. Resolve adult child base from Beeing Female's AddOn AdultActor hierarchy
    int bfVariantCount = 0
    if (Game.GetModByName("BeeingFemale.esm") != 255)
        string adultListName = "AdultActor_Female"
        if (gender == 0)
            adultListName = "AdultActor_Male"
        endif
        bfBase = ResolveBFActorBase(bioMother, bioFather, childRace, adultListName, (player == bioMother || player == bioFather))
        if (bfBase != None && childRace != None)
            bfVariantCount = StorageUtil.FormListCount(childRace, "FW.AddOn." + adultListName)
            if (bfVariantCount < 1)
                bfVariantCount = 1
            endif
        endif
    endif

    if (fmrBase != None && bfBase != None)
        ; Pool all 11 FMR adult face slots (weight 11) with Beeing Female variants (weight bfVariantCount) for maximum face diversity
        if (Utility.RandomInt(1, bfVariantCount + 11) <= 11)
            adultBase = fmrBase
        else
            adultBase = bfBase
            usedBFBase = true
        endif
    elseif (fmrBase != None)
        adultBase = fmrBase
    elseif (bfBase != None)
        adultBase = bfBase
        usedBFBase = true
    endif

    if (adultBase == None)
        TTM_Debug.err("TTM_ServiceChildrenLogic: Failed to resolve adultBase for child " + syntheticID)
        return
    endif

    Actor spawnAnchor = GetChildSpawnAnchor(guardianA, guardianB, bioMother, player)
    Actor spawnedAdult = spawnAnchor.PlaceAtMe(adultBase, 1, true) as Actor
    if (spawnedAdult == None)
        TTM_Debug.err("TTM_ServiceChildrenLogic: PlaceAtMe failed to spawn adult child for " + syntheticID)
        return
    endif

    if (usedBFBase)
        TTM_ServiceFertilityBF.TakeOverChild(spawnedAdult)
    endif

    if (childName != "")
        spawnedAdult.SetDisplayName(childName, true)
    endif

    if (!MARAS.RegisterNpcFertilityChildSpawn(spawnedAdult, syntheticID, bioMother, bioFather, 4, false))
        Actor coSpouse = MARAS.GetFertilitySpawnCoSpouse(syntheticID, bioMother, bioFather)
        MARAS.RegisterChildActor(spawnedAdult, player, coSpouse, None, bioMother, bioFather, syntheticID, 4)
    endif

    spawnedAdult.EvaluatePackage()

    Debug.Notification(spawnedAdult.GetDisplayName() + " has arrived in Skyrim.")
    TTM_Debug.info("TTM_ServiceChildrenLogic: Successfully spawned adult child actor " + spawnedAdult.GetDisplayName() + " (FormID " + spawnedAdult.GetFormID() + ") from synthetic ID " + syntheticID)
EndFunction


