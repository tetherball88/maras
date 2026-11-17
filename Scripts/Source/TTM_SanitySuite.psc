Scriptname TTM_SanitySuite Hidden
{
    Console-driven in-game sanity suite for M.A.R.A.S.
    Run scenarios directly from console via `maras_sanity all_pre` / `maras_sanity s4_post` (alias `msanity`),
    then validate generated JSON dumps + MARAS.log offline with tools/ingame_sanity/verify-maras-sanity.ps1.
    Zero .esp edits required.
}

Actor Function PrepareActor(int formId, string pluginName = "Skyrim.esm") global
    Actor akActor = Game.GetFormFromFile(formId, pluginName) as Actor
    if !akActor
        MARAS.Log("[SanitySuite] ERROR: Could not resolve actor FormID " + formId + " from " + pluginName, 3)
        return None
    endif

    akActor.Enable()
    if akActor.IsDead()
        akActor.Resurrect()
    endif
    akActor.MoveTo(Game.GetPlayer(), 96.0, 96.0, 0.0)
    return akActor
EndFunction

Actor Function ResolveUnloadedActor(int formId, string pluginName = "Skyrim.esm") global
    ; Resolves a placed actor WITHOUT moving them to the player so their cell & 3D remain unloaded.
    Actor akActor = Game.GetFormFromFile(formId, pluginName) as Actor
    if !akActor
        MARAS.Log("[SanitySuite] ERROR: Could not resolve unloaded actor FormID " + formId + " from " + pluginName, 3)
        return None
    endif

    akActor.Enable()
    if akActor.IsDead()
        akActor.Resurrect()
    endif
    return akActor
EndFunction

Function ResetTestActor(Actor akActor) global
    if akActor
        MARAS.UnregisterNPC(akActor)
    endif
EndFunction

Actor Function PrepareChildActor() global
    Actor childActor = PrepareActor(0x00003F5E, "HearthFires.esm") ; Lucia
    if !childActor
        childActor = PrepareActor(0x0001A66B, "Skyrim.esm") ; Braith fallback
    endif
    return childActor
EndFunction

Location Function EnsureBreezehomeRegistered() global
    Location loc = Game.GetFormFromFile(0x0001F86F, "Skyrim.esm") as Location ; WhiterunBreezehomeLocation
    ObjectReference marker = Game.GetFormFromFile(0x0001F88E, "Skyrim.esm") as ObjectReference ; WhiterunBreezehomeCenterMarker
    if loc && marker
        MARAS.RegisterPlayerHouseCell(loc, marker)
    else
        MARAS.Log("[SanitySuite] ERROR: Failed to resolve WhiterunBreezehome location/marker", 3)
    endif
    return loc
EndFunction

Function EnsurePlayerMarried(Actor npc) global
    if !npc
        return
    endif
    Actor player = Game.GetPlayer()
    MARAS.RegisterCandidate(npc)
    if !MARAS.IsNPCStatusWith(npc, player, "married")
        if !MARAS.PromoteNPCToStatus(npc, "married", true)
            ; Clear any pre-scanned NPC partner (e.g. vanilla Mikael <-> Ysolda) and retry
            MARAS.UnregisterNPC(npc)
            MARAS.RegisterCandidate(npc)
            MARAS.PromoteNPCToStatus(npc, "married", true)
        endif
    endif
EndFunction

Function EnsurePairMarried(Actor actorA, Actor actorB) global
    if !actorA || !actorB
        return
    endif
    MARAS.RegisterCandidate(actorA)
    MARAS.RegisterCandidate(actorB)
    if !MARAS.IsNPCStatusWith(actorA, actorB, "married")
        if !MARAS.PromoteNPCToStatusWith(actorA, actorB, "married", true)
            MARAS.UnregisterNPC(actorA)
            MARAS.UnregisterNPC(actorB)
            MARAS.RegisterCandidate(actorA)
            MARAS.RegisterCandidate(actorB)
            MARAS.PromoteNPCToStatusWith(actorA, actorB, "married", true)
        endif
    endif
EndFunction

Function SetupDynamicChildrenPreSave(Actor bioMother, Actor bioFather) global
    ActorBase adultTemplate = None
    ActorBase childTemplate = None
    if Game.GetModByName("Fertility Mode.esm") != 255
        Quest storageQuest = Game.GetFormFromFile(0x00000D62, "Fertility Mode.esm") as Quest
        if storageQuest
            _JSW_BB_Storage fmrStorage = storageQuest as _JSW_BB_Storage
            if fmrStorage
                if fmrStorage.AdultChildren.Length > 12
                    adultTemplate = fmrStorage.AdultChildren[12] ; Nord Male Adult Child template
                endif
                if fmrStorage.Children.Length > 12
                    childTemplate = fmrStorage.Children[12] ; Nord Male Child template
                endif
            endif
        endif
    endif
    if !adultTemplate
        adultTemplate = Game.GetFormFromFile(0x001034E4, "Skyrim.esm") as ActorBase ; WEFarmerMale non-unique fallback
    endif
    if !childTemplate
        childTemplate = Game.GetFormFromFile(0x00013BA9, "Skyrim.esm") as ActorBase ; Braith child base fallback
    endif
    if !adultTemplate || !childTemplate || !bioMother || !bioFather
        MARAS.Log("[SanitySuite] ERROR: Missing template or parents for dynamic 0xFF child setup", 3)
        return
    endif

    MARAS.RegisterCandidate(bioMother)
    MARAS.RegisterCandidate(bioFather)

    ; 1. Persistent 0xFF Stage 4 Adult Child -> exercises runtime-cloned TESNPC visuals & RPG across reload
    MARAS.RegisterUnbornChild(bioMother, bioFather, 5.0, false)
    int adultSynthId = MARAS.PromoteUnbornToInfant(bioMother, 6.0, "SanityAdultFF", bioFather)
    if adultSynthId != 0
        MARAS.SetChildTrainingLocation(adultSynthId, "College of Winterhold")
        MARAS.SetChildStage(adultSynthId, 4)
        Actor spawnedAdult = Game.GetPlayer().PlaceAtMe(adultTemplate, 1, true) as Actor
        if spawnedAdult
            spawnedAdult.SetDisplayName("SanityAdultFF", true)
            MARAS.RegisterChildActor(spawnedAdult, bioMother, None, None, bioMother, bioFather, adultSynthId, 4, false)
        else
            MARAS.Log("[SanitySuite] ERROR: PlaceAtMe failed for SanityAdultFF", 3)
        endif
    else
        MARAS.Log("[SanitySuite] ERROR: PromoteUnbornToInfant failed for SanityAdultFF", 3)
    endif

    ; 2. Ephemeral 0xFF dynamic child -> marked deleted after pre-save dump to verify orphan pruning on reload
    Actor orphanActor = Game.GetPlayer().PlaceAtMe(childTemplate, 1, false) as Actor
    if orphanActor
        orphanActor.SetDisplayName("SanityOrphanFF", true)
        MARAS.RegisterChildActor(orphanActor, bioMother, None, None, bioMother, bioFather, 0, 2, false)
        MARAS.UpdateChildName(orphanActor.GetFormID(), "SanityOrphanFF")
    else
        MARAS.Log("[SanitySuite] ERROR: PlaceAtMe failed for SanityOrphanFF", 3)
    endif
EndFunction

Function PrepEnvironment() global
    MARAS.SetLogLevel(1) ; debug
    EnsureBreezehomeRegistered()

    Actor ysolda = PrepareActor(0x0001A69A) ; Ysolda
    Actor lydia = PrepareActor(0x000A2C94) ; Lydia (HousecarlWhiterunRef)
    Actor mikael = PrepareActor(0x0001A671) ; Mikael
    Actor carlotta = PrepareActor(0x0001A675) ; Carlotta Valentia
    Actor camilla = PrepareActor(0x00013488) ; Camilla Valerius
    Actor sven = PrepareActor(0x00013491) ; Sven
    Actor faendal = PrepareActor(0x0001348C) ; Faendal
    Actor braith = PrepareActor(0x0001A66B) ; Braith
    Actor niranye = ResolveUnloadedActor(0x0001B11C) ; Niranye in Windhelm (kept unloaded)
    PrepareChildActor()

    if ysolda
        MARAS.StopShareHouseWithPlayer(ysolda, "prep_reset")
    endif
    if braith
        MARAS.RemoveChild(braith)
    endif
    if carlotta
        MARAS.RemoveUnbornChild(carlotta)
    endif
    if camilla
        MARAS.RemoveUnbornChild(camilla)
    endif

    ; Clear any world-scanned relationships (e.g. vanilla Mikael <-> Ysolda) on test NPCs
    ResetTestActor(ysolda)
    ResetTestActor(lydia)
    ResetTestActor(mikael)
    ResetTestActor(carlotta)
    ResetTestActor(camilla)
    ResetTestActor(sven)
    ResetTestActor(faendal)
    ResetTestActor(niranye)

    MARAS.DumpStoredData("prep_baseline")
    Debug.Notification("[MARAS Sanity] Prep complete")
EndFunction

Function RunScenario1() global
    ; Scenario 1: Polygamy, Hierarchy, Affection, Housing, Adopted Child & Engine Mirrors/Hook_AddToFaction
    MARAS.SetEnablePolygamy(true)
    MARAS.SetDisablePolygamyQuest(true)

    Location breezehomeLoc = EnsureBreezehomeRegistered()
    Actor ysolda = PrepareActor(0x0001A69A)
    Actor lydia = PrepareActor(0x000A2C94)
    Actor childActor = PrepareChildActor()

    EnsurePlayerMarried(ysolda)
    EnsurePlayerMarried(lydia)

    MARAS.SetHierarchyRank(ysolda, 0)
    MARAS.SetHierarchyRank(lydia, 1)

    if breezehomeLoc
        MARAS.RegisterTenantInPlayerHouse(ysolda, breezehomeLoc, true)
        MARAS.RegisterTenantInPlayerHouse(lydia, breezehomeLoc, false)
    endif

    if childActor
        MARAS.RegisterChildActor(childActor, Game.GetPlayer(), ysolda, breezehomeLoc, None, None, 0, 2, true)
        if breezehomeLoc
            MARAS.AssignChildToHouse(childActor, breezehomeLoc, false)
        endif
    endif

    ; Set deterministic affection targets AFTER child adoption so child_adopted delta does not overwrite them
    MARAS.SetPermanentAffection(ysolda, 85, true)
    MARAS.SetPermanentAffection(lydia, 72, true)
    MARAS.AddAffection(ysolda, 5.0, "gift")

    MARAS.SetSettingBool("revealSecretsInUI", false)
    MARAS.DumpStoredData("s1_household")
    Debug.Notification("[MARAS Sanity] S1 Household dumped")
EndFunction

Function RunScenario2() global
    ; Scenario 2: Affairs, Stage Progression, Open/Close Cleanup & Intrigue Mode Masking
    MARAS.SetEnablePolygamy(true)

    Actor player = Game.GetPlayer()
    Actor ysolda = PrepareActor(0x0001A69A)
    Actor lydia = PrepareActor(0x000A2C94)
    Actor mikael = PrepareActor(0x0001A671)
    Actor carlotta = PrepareActor(0x0001A675)
    Actor camilla = PrepareActor(0x00013488)
    Actor sven = PrepareActor(0x00013491)
    Actor faendal = PrepareActor(0x0001348C)

    ; Ensure Mikael has no residual relationship from world scan so (Lydia, Mikael) is an open-pair lover, not Mikael's secret affair
    ResetTestActor(mikael)

    EnsurePlayerMarried(ysolda)
    EnsurePlayerMarried(lydia)
    EnsurePairMarried(camilla, sven)
    MARAS.RegisterCandidate(mikael)
    MARAS.RegisterCandidate(carlotta)
    MARAS.RegisterCandidate(faendal)

    ; Disable polygamy after marrying both spouses so Player cheating on Ysolda is recorded instead of polygamy-exempted
    MARAS.SetEnablePolygamy(false)

    ; Camilla cheats on Sven with Faendal (secret Physical: stage 1)
    MARAS.RecordCheating(camilla, sven, faendal, true)
    MARAS.SetAffairStage(camilla, sven, faendal, 1)

    ; Player cheats on Ysolda with Carlotta (secret Physical: Talking applies only when Player is victim) -> validates zero 0x14 UI exemption
    MARAS.RecordCheating(player, ysolda, carlotta, true)

    ; Open (Player, Lydia), allow Lydia + Mikael Lovers, then close -> auto ExLover
    MARAS.SetPairIsOpen(player, lydia, true)
    MARAS.PromoteNPCToStatusWith(lydia, mikael, "lover", true)
    MARAS.SetPairIsOpen(player, lydia, false)

    MARAS.SetSettingBool("revealSecretsInUI", false)
    MARAS.DumpStoredData("s2_masked")
    MARAS.SetSettingBool("revealSecretsInUI", true)
    MARAS.DumpStoredData("s2_omniscient")
    MARAS.SetSettingBool("revealSecretsInUI", false)

    Debug.Notification("[MARAS Sanity] S2 Affairs & Intrigue dumps complete")
EndFunction

Function RunScenario3() global
    ; Scenario 3: Unborn -> Infant Promotion & Two-Edge Paternity Secrecy
    bool prevSimNoSn = MARAS.GetSettingBool("simulateNoSkyrimNet")
    int prevBirthChance = MARAS.GetSettingInt("paternityPhenotypeBirthChance")
    MARAS.SetSettingBool("simulateNoSkyrimNet", true)
    MARAS.SetSettingInt("paternityPhenotypeBirthChance", 0)

    Actor camilla = PrepareActor(0x00013488)
    Actor sven = PrepareActor(0x00013491)
    Actor faendal = PrepareActor(0x0001348C)

    EnsurePairMarried(camilla, sven)
    MARAS.RegisterCandidate(faendal)

    int infantId = MARAS.FindSyntheticChild(camilla, "SanityInfant")
    if infantId == 0
        MARAS.RegisterUnbornChild(camilla, faendal, 10.0, false)
        infantId = MARAS.PromoteUnbornToInfant(camilla, 15.0, "SanityInfant", faendal)
    endif
    if infantId == 0
        MARAS.Log("[SanitySuite] ERROR: PromoteUnbornToInfant returned 0", 3)
        MARAS.SetSettingInt("paternityPhenotypeBirthChance", prevBirthChance)
        MARAS.SetSettingBool("simulateNoSkyrimNet", prevSimNoSn)
        return
    endif

    ; Enforce both edges secret: param2 = secretForBioFather, param3 = secretForPartner
    MARAS.SetPaternitySecretEdges(infantId, true, true)

    MARAS.SetSettingBool("revealSecretsInUI", false)
    MARAS.DumpStoredData("s3_masked")
    MARAS.SetSettingBool("revealSecretsInUI", true)
    MARAS.DumpStoredData("s3_omniscient")

    ; Partial reveal: biological father learns truth while mother's partner remains deceived
    MARAS.ResolvePaternitySecretForBioFather(infantId, true)

    MARAS.SetSettingBool("revealSecretsInUI", false)
    MARAS.DumpStoredData("s3_partial_masked")
    MARAS.SetSettingBool("revealSecretsInUI", true)
    MARAS.DumpStoredData("s3_partial_omniscient")
    MARAS.SetSettingBool("revealSecretsInUI", false)

    MARAS.SetSettingInt("paternityPhenotypeBirthChance", prevBirthChance)
    MARAS.SetSettingBool("simulateNoSkyrimNet", prevSimNoSn)

    Debug.Notification("[MARAS Sanity] S3 Paternity & Secrecy dumps complete")
EndFunction

Function RunScenario4PreSave() global
    ; Scenario 4 (Pre-Save): VTable Divorce Hook, Death Cascade, 0xFF Spawned Child + Cloned TESNPC,
    ; Unloaded Placed NPC, 0xFF Home XMarkers + Variable06 Order + 'CFSN' Snapshot, and 'SPAS' Shared Home
    bool prevSimNoSn = MARAS.GetSettingBool("simulateNoSkyrimNet")
    int prevStepUpChance = MARAS.GetSettingInt("paternityBioStepUpChance")
    MARAS.SetSettingBool("simulateNoSkyrimNet", true)
    MARAS.SetSettingInt("paternityBioStepUpChance", 0)

    Actor player = Game.GetPlayer()
    Location breezehomeLoc = EnsureBreezehomeRegistered()
    Actor ysolda = PrepareActor(0x0001A69A)
    Actor lydia = PrepareActor(0x000A2C94)
    Actor mikael = PrepareActor(0x0001A671)
    Actor carlotta = PrepareActor(0x0001A675)
    Actor camilla = PrepareActor(0x00013488)
    Actor sven = PrepareActor(0x00013491)
    Actor lucia = Game.GetFormFromFile(0x00003F5E, "HearthFires.esm") as Actor
    Actor braith = PrepareActor(0x0001A66B)
    Actor niranye = ResolveUnloadedActor(0x0001B11C) ; Unloaded in Windhelm ('Niranye's House')

    ; Ensure baseline Ysolda marriage & Breezehome housing exist even if s4_pre is invoked standalone
    EnsurePlayerMarried(ysolda)
    if ysolda && breezehomeLoc && MARAS.GetTenantHouse(ysolda) != breezehomeLoc
        MARAS.RegisterTenantInPlayerHouse(ysolda, breezehomeLoc, true)
    endif
    if lucia && ysolda && breezehomeLoc && MARAS.GetTenantHouse(lucia) != breezehomeLoc
        MARAS.RegisterChildActor(lucia, player, ysolda, breezehomeLoc, None, None, 0, 2, false)
        MARAS.AssignChildToHouse(lucia, breezehomeLoc, false)
    endif

    ; 1. Exercise FactionReconciler Hook_RemoveFromFaction detour: external PlayerMarriedFaction removal -> Divorced + Eviction
    if !MARAS.IsNPCStatusWith(lydia, player, "divorced")
        EnsurePlayerMarried(lydia)
        Faction vanillaMarriedFaction = Game.GetFormFromFile(0x000C6472, "Skyrim.esm") as Faction
        if vanillaMarriedFaction
            lydia.RemoveFromFaction(vanillaMarriedFaction)
        else
            MARAS.Breakup(player, lydia)
        endif
    endif

    ; 2. Dynamic 0xFF800000+ Stage 4 Adult Child (runtime-cloned TESNPC) + ephemeral 0xFF orphan child
    SetupDynamicChildrenPreSave(carlotta, mikael)

    ; 3. Unloaded placed NPC (0x00-0xFE) registered & paired without MoveTo (3D unloaded across save/reload)
    if niranye && mikael
        MARAS.RegisterCandidate(niranye)
        MARAS.RegisterCandidate(mikael)
        if !MARAS.IsNPCStatusWith(niranye, mikael, "dating")
            MARAS.PromoteNPCToStatusWith(niranye, mikael, "dating", true)
        endif
    endif

    ; 4. Dynamic 0xFF home XMarkers, Lucia Variable06 order, and Braith 'CFSN' child faction snapshot
    if ysolda && !MARAS.GetTrackedNpcHomeMarker(ysolda)
        MARAS.Log("[SanitySuite] ERROR: Pre-save GetTrackedNpcHomeMarker(ysolda) returned None", 3)
    endif
    if lucia
        MARAS.IssueChildOrder(lucia, 2.0, 12.0)
        if !MARAS.GetChildHomeMarker(lucia)
            MARAS.Log("[SanitySuite] ERROR: Pre-save GetChildHomeMarker(lucia) returned None", 3)
        endif
    endif
    if braith && lucia && braith != lucia && ysolda && breezehomeLoc
        MARAS.RegisterChildActor(braith, player, ysolda, breezehomeLoc, None, None, 0, 2, false)
        MARAS.AssignChildToHouse(braith, breezehomeLoc, false)
    endif

    ; 5. Share Ysolda's original home ('Ysolda's House' 000165AC) & bed ownership ('SPAS')
    if ysolda
        if !MARAS.ShareHouseWithPlayer(ysolda)
            MARAS.Log("[SanitySuite] ERROR: ShareHouseWithPlayer(ysolda) returned false", 3)
        endif
    endif

    ; 6. Death cascade on Sven
    EnsurePairMarried(camilla, sven)
    sven.KillSilent(None)
    Debug.Notification("[MARAS Sanity] Close console for 2s to process death...")
    Utility.Wait(2.0)

    if !MARAS.IsNPCDeceased(sven)
        MARAS.Log("[SanitySuite] ERROR: Sven not marked deceased after Wait(2.0)", 3)
    endif

    MARAS.SetSettingBool("revealSecretsInUI", false)
    MARAS.DumpStoredData("s4_pre_save_masked")
    MARAS.SetSettingBool("revealSecretsInUI", true)
    MARAS.DumpStoredData("s4_pre_save_omniscient")
    MARAS.SetSettingBool("revealSecretsInUI", false)

    MARAS.SetSettingInt("paternityBioStepUpChance", prevStepUpChance)
    MARAS.SetSettingBool("simulateNoSkyrimNet", prevSimNoSn)

    Debug.Notification("[MARAS Sanity] S4 pre-save ready - Save & Reload now!")
EndFunction

Function RunScenario4PostLoad() global
    ; Phase A: Immediately dump reloaded 'MARS' v7 co-save & live engine state before post-load mutations
    Actor ysolda = Game.GetFormFromFile(0x0001A69A, "Skyrim.esm") as Actor
    Actor lucia = Game.GetFormFromFile(0x00003F5E, "HearthFires.esm") as Actor
    if ysolda && !MARAS.GetTrackedNpcHomeMarker(ysolda)
        MARAS.Log("[SanitySuite] ERROR: Post-load GetTrackedNpcHomeMarker(ysolda) returned None", 3)
    endif
    if lucia && !MARAS.GetChildHomeMarker(lucia)
        MARAS.Log("[SanitySuite] ERROR: Post-load GetChildHomeMarker(lucia) returned None", 3)
    endif

    MARAS.SetSettingBool("revealSecretsInUI", false)
    MARAS.DumpStoredData("s4_post_load_masked")
    MARAS.SetSettingBool("revealSecretsInUI", true)
    MARAS.DumpStoredData("s4_post_load_omniscient")
    MARAS.SetSettingBool("revealSecretsInUI", false)

    ; Phase B: Post-load live engine convergence & restoration:
    ;   - Move unloaded Niranye into loaded cell -> 3D attach & OnActorLoaded convergence
    ;   - RemoveChild(Braith) -> restore original vanilla factions & crime faction from reloaded 'CFSN'
    ;   - StopShareHouseWithPlayer(Ysolda) -> restore original cell public flag & bed owner from 'SPAS'
    Actor niranye = Game.GetFormFromFile(0x0001B11C, "Skyrim.esm") as Actor
    if niranye
        niranye.MoveTo(Game.GetPlayer(), 120.0, -120.0, 0.0)
    endif

    Actor braith = Game.GetFormFromFile(0x0001A66B, "Skyrim.esm") as Actor
    if braith && lucia && braith != lucia
        MARAS.RemoveChild(braith)
    endif

    if ysolda
        MARAS.StopShareHouseWithPlayer(ysolda, "sanity_restore")
    endif

    Debug.Notification("[MARAS Sanity] Waiting 1s for 3D load & engine restore...")
    Utility.Wait(1.0)

    MARAS.SetSettingBool("revealSecretsInUI", false)
    MARAS.DumpStoredData("s4_post_restore")

    Debug.Notification("[MARAS Sanity] S4 post-load & restore dumps complete!")
EndFunction

Function RunAllPreSave() global
    PrepEnvironment()
    RunScenario1()
    RunScenario2()
    RunScenario3()
    RunScenario4PreSave()
EndFunction
