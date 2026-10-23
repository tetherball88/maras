Scriptname TTM_QuestPartnerAway extends Quest

ReferenceAlias Property Alias_Partner Auto
ReferenceAlias Property Alias_Lover Auto
ReferenceAlias Property Alias_TavernMarker Auto
LocationAlias Property Alias_TavernLocation Auto

string Property ActiveScenario = "" Auto Hidden

Function StartRendezvous(Actor akPartner, Actor akLover, string asScenario, Location akTavernLoc, ObjectReference akTavernMarker)
    if (!akPartner || !akLover || !akTavernLoc || !akTavernMarker)
        TTM_Debug.warn("TTM_QuestPartnerAway: StartRendezvous missing required arguments: partner=" + akPartner + ", lover=" + akLover + ", loc=" + akTavernLoc + ", marker=" + akTavernMarker)
        return
    endif

    if (akPartner.GetCurrentScene() != none || akLover.GetCurrentScene() != none)
        TTM_Debug.debug("TTM_QuestPartnerAway: StartRendezvous: partner or lover is already in a scene (partnerScene=" + akPartner.GetCurrentScene() + ", loverScene=" + akLover.GetCurrentScene() + ")")
        return
    endif

    if (self.IsRunning())
        FinishQuest(false)
        Utility.Wait(0.1)
    endif

    self.Reset()
    self.Start()

    ActiveScenario = asScenario
    Alias_TavernLocation.ForceLocationTo(akTavernLoc)
    Alias_TavernMarker.ForceRefTo(akTavernMarker)
    Alias_Partner.ForceRefTo(akPartner)
    Alias_Lover.ForceRefTo(akLover)

    ; Move only the partner to the tavern immediately and snap to navmesh so they sandbox cleanly
    akPartner.MoveTo(akTavernMarker)
    PO3_SKSEFunctions.MoveToNearestNavmeshLocation(akPartner)
    akPartner.EvaluatePackage()

    if (asScenario == "open_date_away")
        self.SetStage(10)
        self.SetObjectiveDisplayed(10, true)
    else
        self.SetStage(20)
        self.SetObjectiveDisplayed(20, true)
    endif

    RegisterForSingleUpdateGameTime(12.0)

    TTM_Debug.info("TTM_QuestPartnerAway: Started '" + asScenario + "' for partner=" + TTM_Utils.GetActorName(akPartner) + " and lover=" + TTM_Utils.GetActorName(akLover) + " at " + akTavernLoc.GetName())
EndFunction

Event OnUpdateGameTime()
    if (self.IsRunning())
        TTM_Debug.info("TTM_QuestPartnerAway: Rendezvous timed out after 12 in-game hours; returning partner to normal schedule")
        FinishQuest(false)
    endif
EndEvent

bool Function CheckPlayerArrival(Location akNewLoc)
    if (!self.IsRunning())
        return false
    endif

    Actor player = TTM_Data.GetPlayer()
    Actor partner = Alias_Partner.GetActorRef()
    Actor lover = Alias_Lover.GetActorRef()
    Location tavernLoc = Alias_TavernLocation.GetLocation()
    ObjectReference tavernMarker = Alias_TavernMarker.GetReference()

    if (!partner || !lover)
        TTM_Debug.warn("TTM_QuestPartnerAway: CheckPlayerArrival aborting and stopping quest — partner=" + partner + ", lover=" + lover)
        FinishQuest(false)
        return false
    endif

    bool arrived = false
    if (akNewLoc != none && tavernLoc != none && akNewLoc == tavernLoc)
        arrived = true
    elseif (player.GetParentCell() != none && (player.GetParentCell() == partner.GetParentCell() || (tavernMarker != none && player.GetParentCell() == tavernMarker.GetParentCell())))
        arrived = true
    endif

    if (!arrived)
        TTM_Debug.debug("TTM_QuestPartnerAway: CheckPlayerArrival not at tavern yet (akNewLoc=" + akNewLoc + ", tavernLoc=" + tavernLoc + ", playerCell=" + player.GetParentCell() + ", partnerCell=" + partner.GetParentCell() + ")")
        return false
    endif

    if (partner.GetCurrentScene() != none || lover.GetCurrentScene() != none)
        TTM_Debug.debug("TTM_QuestPartnerAway: CheckPlayerArrival deferring — partner or lover is currently in a scene (partnerScene=" + partner.GetCurrentScene() + ", loverScene=" + lover.GetCurrentScene() + ")")
        return true
    endif

    string scen = ActiveScenario
    TTM_Debug.info("TTM_QuestPartnerAway: Player arrived at tavern for scenario '" + scen + "' with partner=" + TTM_Utils.GetActorName(partner) + ", lover=" + TTM_Utils.GetActorName(lover))

    FinishQuest(true)

    if (partner.GetSitState() != 0)
        partner.MoveTo(partner)
        PO3_SKSEFunctions.MoveToNearestNavmeshLocation(partner)
    endif
    partner.EvaluatePackage()

    TTM_NpcsScenes npcScenes = TTM_Data.GetMarasNpcsScenesQuest() as TTM_NpcsScenes
    if (npcScenes)
        npcScenes.SetupSceneOnLocationChange(partner, lover, scen)
    endif

    return true
EndFunction

Function FinishQuest(bool abComplete)
    UnregisterForUpdateGameTime()
    Actor partner = Alias_Partner.GetActorRef()
    TTM_Debug.info("TTM_QuestPartnerAway: FinishQuest(abComplete=" + abComplete + ") for scenario='" + ActiveScenario + "', partner=" + TTM_Utils.GetActorName(partner))

    if (abComplete)
        if (self.IsObjectiveDisplayed(10))
            self.SetObjectiveCompleted(10)
        endif
        if (self.IsObjectiveDisplayed(20))
            self.SetObjectiveCompleted(20)
        endif
    else
        self.FailAllObjectives()
    endif

    Alias_Partner.Clear()
    Alias_Lover.Clear()
    Alias_TavernMarker.Clear()
    Alias_TavernLocation.Clear()
    ActiveScenario = ""

    self.SetStage(100)
    self.Stop()

    if (partner)
        partner.EvaluatePackage()
    endif
EndFunction
