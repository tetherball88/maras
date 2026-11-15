Scriptname TTM_SanitySuite Hidden
{
    Console-driven in-game sanity suite for M.A.R.A.S.
    Run scenarios from console via `bat maras_all_pre` (or individual `bat maras_s1`..`bat maras_s4_post`),
    then validate generated JSON dumps + MARAS.log offline with tools/ingame_sanity/verify-maras-sanity.ps1.
    Zero .esp edits required.
}

Actor Function PrepareActor(int formId, string pluginName = "Skyrim.esm") global
    Actor akActor = Game.GetFormFromFile(formId, pluginName) as Actor
    if !akActor
        MARAS.Log("[SanitySuite] Could not resolve actor FormID " + formId + " from " + pluginName, 3)
        return None
    endif

    akActor.Enable()
    if akActor.IsDead()
        akActor.Resurrect()
    endif
    akActor.MoveTo(Game.GetPlayer(), 96.0, 96.0, 0.0)
    return akActor
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
        MARAS.Log("[SanitySuite] Failed to resolve WhiterunBreezehome location/marker", 3)
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
        MARAS.PromoteNPCToStatus(npc, "married", true)
    endif
EndFunction

Function EnsurePairMarried(Actor actorA, Actor actorB) global
    if !actorA || !actorB
        return
    endif
    MARAS.RegisterCandidate(actorA)
    MARAS.RegisterCandidate(actorB)
    if !MARAS.IsNPCStatusWith(actorA, actorB, "married")
        MARAS.PromoteNPCToStatusWith(actorA, actorB, "married", true)
    endif
EndFunction

Function PrepEnvironment() global
    MARAS.SetLogLevel(1) ; debug
    EnsureBreezehomeRegistered()

    PrepareActor(0x0001A69A) ; Ysolda
    PrepareActor(0x000A2C94) ; Lydia (HousecarlWhiterunRef)
    PrepareActor(0x0001A671) ; Mikael
    PrepareActor(0x0001A675) ; Carlotta Valentia
    PrepareActor(0x00013488) ; Camilla Valerius
    PrepareActor(0x00013491) ; Sven
    PrepareActor(0x0001348C) ; Faendal
    PrepareChildActor()

    MARAS.DumpStoredData("prep_baseline")
    Debug.Notification("[MARAS Sanity] Prep complete")
EndFunction

Function RunScenario1() global
    ; Scenario 1: Polygamy, Hierarchy, Affection, Housing & Adopted Child
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

    MARAS.SetPermanentAffection(ysolda, 85, true)
    MARAS.SetPermanentAffection(lydia, 72, true)
    MARAS.AddAffection(ysolda, 5.0, "gift")

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

    EnsurePlayerMarried(ysolda)
    EnsurePlayerMarried(lydia)
    EnsurePairMarried(camilla, sven)
    MARAS.RegisterCandidate(mikael)
    MARAS.RegisterCandidate(carlotta)
    MARAS.RegisterCandidate(faendal)

    ; Camilla cheats on Sven with Faendal (secret Physical: stage 1)
    MARAS.RecordCheating(camilla, sven, faendal, true)
    MARAS.SetAffairStage(camilla, sven, faendal, 1)

    ; Player cheats on Ysolda with Carlotta (secret Talking: stage 0) -> validates zero 0x14 UI exemption
    MARAS.RecordCheating(player, ysolda, carlotta, true)
    MARAS.SetAffairStage(player, ysolda, carlotta, 0)

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
    Actor camilla = PrepareActor(0x00013488)
    Actor sven = PrepareActor(0x00013491)
    Actor faendal = PrepareActor(0x0001348C)

    EnsurePairMarried(camilla, sven)
    MARAS.RegisterCandidate(faendal)

    MARAS.RegisterUnbornChild(camilla, faendal, 10.0, false)
    int infantId = MARAS.PromoteUnbornToInfant(camilla, 15.0, "SanityInfant", faendal)
    if infantId == 0
        MARAS.Log("[SanitySuite] ERROR: PromoteUnbornToInfant returned 0", 3)
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

    Debug.Notification("[MARAS Sanity] S3 Paternity & Secrecy dumps complete")
EndFunction

Function RunScenario4PreSave() global
    ; Scenario 4 (Pre-Save): Divorce Eviction & Death Cascade before Co-Save Round-Trip
    Actor player = Game.GetPlayer()
    Actor lydia = PrepareActor(0x000A2C94)
    Actor camilla = PrepareActor(0x00013488)
    Actor sven = PrepareActor(0x00013491)

    if !MARAS.IsNPCStatusWith(lydia, player, "divorced")
        EnsurePlayerMarried(lydia)
        MARAS.Breakup(player, lydia)
    endif
    EnsurePairMarried(camilla, sven)

    sven.KillSilent(None)
    Debug.Notification("[MARAS Sanity] Close console for 2s to process death...")
    Utility.Wait(2.0)

    if !MARAS.IsNPCDeceased(sven)
        MARAS.Log("[SanitySuite] WARN: Sven not yet marked deceased after Wait(2.0)", 2)
    endif

    MARAS.SetSettingBool("revealSecretsInUI", false)
    MARAS.DumpStoredData("s4_pre_save_masked")
    MARAS.SetSettingBool("revealSecretsInUI", true)
    MARAS.DumpStoredData("s4_pre_save_omniscient")
    MARAS.SetSettingBool("revealSecretsInUI", false)

    Debug.Notification("[MARAS Sanity] S4 pre-save ready - Save & Reload now!")
EndFunction

Function RunScenario4PostLoad() global
    ; Scenario 4 (Post-Load): Dump Reloaded 'MARS' v7 Co-Save State
    MARAS.SetSettingBool("revealSecretsInUI", false)
    MARAS.DumpStoredData("s4_post_load_masked")
    MARAS.SetSettingBool("revealSecretsInUI", true)
    MARAS.DumpStoredData("s4_post_load_omniscient")
    MARAS.SetSettingBool("revealSecretsInUI", false)

    Debug.Notification("[MARAS Sanity] S4 post-load dumped!")
EndFunction

Function RunAllPreSave() global
    PrepEnvironment()
    RunScenario1()
    RunScenario2()
    RunScenario3()
    RunScenario4PreSave()
EndFunction
