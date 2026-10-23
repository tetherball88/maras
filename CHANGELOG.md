# Changelog

## 2.3.0.alpha - 2026-10-27

### Added
- **Player Partner Agency & Infidelity Engine**: Added a dedicated background agency system for your active partners (**Dating**, **Engaged**, and **Married**), operating independently from generic NPC world matchmaking:
  - **Two-Stage Secret Affairs (`Talking Stage` -> `Physical Affair`)**: Closed-relationship partners who give in to temptation now start in a **Talking Stage** (secret letters, lingering conversations, flirtatious closeness) before escalating to a **Physical Affair** if left unresolved. Affair stages are tracked and badged in the Dashboard.
  - **Dynamic Temptation & Loyalty Drivers**: Partner temptation is calculated from affection tier (**Happy**, **Content**, **Troubled**, **Estranged**), days of player neglect, **Revenge** (if they caught you cheating), **Spouse Hierarchy** rank in polygamous households, **Shared Children** (which anchor loyalty, plus full immunity during pregnancy), and personality temperament.
  - **Realistic Local Paramours**: Tempted or open-relationship partners choose from people in their actual daily life and history—old flames, housemates under the same roof, guildmates, town neighbors, or people in the same hold—with anti-repeat rules so they don't keep getting back together with past failed flings or lovers you already confronted.
  - **Open-Relationship Side Lovers & Autonomous Breakups**: Partners in an **Open Relationship** can independently pursue consensual side lovers, and both cheating and open partners can break off their side relationships on their own when guilt sets in, when you reconnect and spend time with them, or when the novelty wears off.
- **Partner Force-Greet Conversations**: Your partners can now approach you when you return home to:
  - **Propose Opening the Relationship** (with options to agree, promise to spend more time together, scold, persuade, or intimidate).
  - **Ask to Close an Open Relationship** and become exclusive again.
  - **Confess Guilt** after breaking off a secret Talking or Physical affair on their own.
  - Ignoring a partner's pending conversation for too long carries consequences (an ignored request to open the relationship can push them into a secret Talking affair, an ignored request to close lowers affection, and an unconfessed Talking affair escalates to Physical).
- **Home-Arrival Encounters, Handwritten Notes & Tavern Rendezvous**: Returning to a partner's home can now trigger live arrival events—walking in on a Talking-Stage conversation, catching a Physical affair in the act, walking in on an open-relationship partner with their lover, or discovering a **handwritten note** left behind (a note from an open partner having dinner at a local inn, or a suspicious unsigned letter from a secret lover) that starts a quest leading you to the nearby tavern.
- **New "Partner Agency" Settings Tab & Loneliness Threshold Slider**:
  - Added a dedicated **Partner Agency** sub-page in **Settings** with toggles and sliders for closed-relationship cheating agency, open-relationship agency, SkyrimNet AI evaluation, result review modals, force-greets, home-arrival events, temptation/breakup multipliers, loyalty anchors, and day-threshold multipliers.
  - Added a master **Loneliness Threshold (Days)** slider (`1`–`14` days, default `2`) in **Settings -> General** and **Partner Agency** that controls both how many days you can be away before daily affection decay begins and the base time unit for all Partner Agency timers.
- **SkyrimNet Partner Agency Integration**: Added AI evaluation for partner affairs, open-relationship lovers, side breakups, guilt confessions, and open/close relationship proposals, along with dual first-person memory generation and expanded character prompt context (affection tier, days since last player interaction, affair stage, and pending conversation intents).

### Changed
- **Co-Spouse Intimacy in Polygamous Households**: When two or more of your active partners become intimate or form a `Lover` bond with each other, it is now treated as a consensual co-spouse bond rather than cheating on you.
- **Unconditional Partner Separation from World Matchmaking**: Your active partners (**Dating**, **Engaged**, **Married**) are now always excluded from generic NPC-to-NPC world matchmaking and ambient location banter so their relationships are governed exclusively by the Partner Agency system.
- **OStim Bed Detection Radius**: Increased the bed search radius for OStim intimate scenes so scenes reliably find beds in larger player homes and taverns.

### Fixed
- **Secrets Mode Masking**:
  - Consensual open-relationship side lovers (`Allowed`) are no longer hidden when **Secrets** mode is enabled.
  - NPC pairs who were **Divorced** or **Jilted** before starting a secret affair now display their prior Divorced/Jilted status while secrets are hidden instead of disappearing from the relationship list.
- **Walk-In Cheating Discovery**: Confronting a cheating partner or persuading/joining them when walking in on an affair now properly marks the secret affair as discovered.
- **Parent & Guardian Auto-Registration**: Biological parents, presumed fathers, and guardians assigned to a child or pregnancy are now automatically registered as tracked NPCs so their profiles always appear in the Dashboard.

## 2.2.0.alpha - 2026-10-26

### Added
- **Secrets / Intrigue Mode**: Added a **Secrets** toggle in the dashboard header and General Settings (hidden by default). When secrets are hidden, undiscovered affairs and paternity secrets are masked across the UI so you aren't spoiled on secrets your character hasn't uncovered yet (for example, showing the presumed father as the biological father until the truth comes out).
- **UI Scale & Full Screen Controls**: Added quick **UI & Font Scale** (`75%`–`150%`) and **Full Screen Mode** buttons in the dashboard header (and General Settings) to resize text and UI elements or expand the dashboard to fill the screen.
- **Pause Game While Browsing**: Added a **Pause** button in the dashboard header and an **Auto-Pause on Open** setting to pause the game world and AI while using the dashboard.
- **Interactive Parents & Guardians Editor**: You can now click any parent or guardian slot (**Guardian A**, **Guardian B**, **Biological Mother**, **Biological Father**, or **Presumed Father**) on a child's detail page to search and assign, swap, or clear parents from any adult NPC in Skyrim.
- **Dedicated Presumed Father Tracking**: Added a separate **Presumed Father** slot for children with paternity secrets, allowing unmarried partners (dating or engaged) to be tracked as the presumed father without automatically making them a household guardian.
- **Header Quick Actions**: Added a **Reload** button to the dashboard header, updated the child-flee button with a clear **Send Kids** label, and added a **Remove Child Record** button directly on a child's detail page.

### Changed
- **Unmarried Birth Custody**: Children born to unmarried parents no longer automatically assign both parents as shared guardians. Custody is now awarded to a single parent at birth based on your Child Custody settings, while married couples continue to share custody.
- **Child Custody on Marriage & Breakups**:
  - Marrying an NPC who has a child (or marrying the other biological parent of a child in your household) now properly brings the child into your shared household and resolves any paternity secret between the biological parents.
  - Breaking up a dating relationship or calling off an engagement now evaluates child custody and resolves pregnancy/paternity secrets involving the former partner, matching divorce behavior.
- **Unhoused Child Behavior**: Children in the player's household who have not yet been assigned to a home now follow and stay near their parent instead of wandering off, and newly spawned NPC children now appear beside their mother or guardian instead of at the player's location.
- **Beeing Female NG Parity**: Updated Beeing Female NG birth and child-spawning support to match Fertility Mode Reloaded's custody and co-spouse rules.
- **SkyrimNet Prompts**:
  - Added a note to character bio prompts instructing the AI that M.A.R.A.S relationship, family, pregnancy, and infidelity sections take priority over older or conflicting relationship descriptions in the character bio.
  - Improved pregnancy and paternity secret prompts so a pregnant mother hiding an affair baby no longer assumes her partner already knows about the pregnancy or already believes the baby is his.

### Fixed
- Fixed the player's name appearing with a `"(Player)"` suffix in UI views, notifications, and prompts instead of just the character's name.
- Fixed the dashboard not refreshing automatically after background or AI matchmaking updates finish applying.
- Fixed child adoption home selection opening a legacy message box instead of the new Prisma UI home selector modal.
- Fixed existing saves where an unmarried NPC's biological child with the player had their custody overwritten by the player, automatically restoring custody to the appropriate parent on load.

## 2.1.1.alpha - 2026-10-24
- Fixed Fertility Mode Reloaded (FMR 1.0.7+) newborn children remaining stuck at the `Infant` ("Newborn") stage in Prisma UI when growing up into a young child and being registered as an unrelated adopted orphan when spawned (`TTM_MainController` now subscribes to `FMR_ChildGrewUpWithMother` so `akMother` is received and matched to the mother's synthetic child record).
- Removed leftover hardcoded `sceneIdx = 4` override in `TTM_NpcsScenes` (`SetupSceneOnLocationChange`) so NPC-NPC ambient banter scenes play all randomized dialogue variants instead of repeating index 4.

## 2.1.0.alpha - 2026-10-22
- Added standalone Prisma UI modal system (`PrismaModalManager` and `<PrismaModalHost />`) replacing all native `RE::MessageBoxMenu`, `BGSMessage.Show()`, and `UIExtensions` (`UIListMenu`) popups across C++ and Papyrus — supports thread-safe FIFO modal queuing, checklists with per-row status dropdowns, checkboxes, selects, info boxes, and full keyboard/mouse navigation (`WASD`, Arrow keys, `Tab`, `Enter`/`Space`/`E`) while pausing the game independently of the main dashboard.
- Migrated all relationship and open-relationship confirmation popups (`ConfirmationModal`), wedding skip prompts (`ShowSkipWeddingModal` with "remember my choice" option), itemized divorce settlement notices (`ShowDivorceSettlementModal` with direct gold payment vs. hold bounty choice), single-step spouse/child residence & schedule selectors (`ShowChooseHomeModal`), co-parent adoption selectors (`ShowChooseAdoptionSpouseModal`), and the debug status spell menu (`ShowDebugStatusModal`) to Prisma UI modals.
- Added interactive first-time Player-NPC Relationship Audit modal (`ScanExistingRelationships`) — when plugin-baked or existing Player-NPC romantic `BGSRelationship` records (`Dating`, `Lover`, `Married`) are detected on first load (both new games and existing saves, including unspawned base NPCs), presents a checklist with source `.esp` names and per-row status selectors so players choose which relationships to import while NPC-NPC relationships import automatically in the background.
- Added interactive Matchmaking Suggestions review modal (`matchmakingConfirmResults` setting) — presents proposed scheduled, dice-roll, and global auto-apply matchmaking transitions in a checklist with per-row target status overrides prior to LLM memory generation and status application.
- Expanded SkyrimNet LLM failure retry modals (`ShowLLMRetryModal` and `ShowInGameFailedLLMModal`) to cover paternity secret evaluations, bio-father step-in evaluations (with retry, dice-roll fallback, and skip options), manual/global matchmaking scans, and single-pair memory generation — added plain-English **"What Does This Mean?"** guidance boxes explaining broken/unreadable JSON responses (and which SkyrimNet model slot to upgrade if frequent) vs. HTTP/connection and local server failures.
- Wired up the `confirmDivorceDuringLowAffectionQuest` setting (`low_affection_divorce` confirmation modal) before finalizing Stage 200 of the estranged spouse divorce quest (`TTM_QuestAffectionEstrangedDivorce`), allowing declined divorces to recover at Stage 150.
- Added destructive action confirmation modals in the Prisma UI dashboard before removing relationship records or child records.
- Removed the technical `Candidates` counter card from the Prisma UI Overview stats bar.
- Fixed `RegisterTenantInPlayerHouse` ignoring the `stayAtHome` schedule parameter when relocating a spouse from Papyrus, and guarded synthetic child FormIDs against engine faction/package calls during child removal.

## 2.0.2.alpha - 2026-10-18
- Added ability to completely remove/clean relationship records in the Prisma UI dashboard (available in My Household -> Partners, Overview -> All Pairs, NPC Detail -> Tracked Relationships / Partner Panel, and Matchmaking -> Manual Match) — wipes the pair's relationship record, status history, open-relationship state, affair/cheating records, and associated marriage/courting factions while keeping NPCs registered as Candidates (Rank 0) and leaving vanilla Skyrim engine relationship ranks untouched. Added automatic child custody and lineage cleanup when removing a relationship record — resolves shared children and unborn pregnancies via the Divorce Custody rules and strips auto-inferred co-parent links for vanilla single-parent children.
- Removed the "Bypass Status Rules" toggle from the Prisma UI Manual Match panel.

## 2.0.1 - 2026-10-17
- Fixed new game initialization importing plugin-baked Player-NPC `BGSRelationship` records (e.g., custom followers with pre-set Lover rank 4) — on a fresh new game, the player now starts with a clean relationship slate while NPC-NPC relationships are still scanned normally.
- Fixed floating-point numbers displaying excessive decimal places (e.g., `1.7000000476837158x` for Affection Decay Multiplier) across Prisma UI sliders, number inputs, and settings descriptions.

## 2.0 - 2026-10-16
Major release. See [CHANGELOG_v2.0.md](CHANGELOG_v2.0.md) for the full detailed changelog.
- Added Prisma UI dashboard — in-game web UI replacing MCM as the primary interface for relationship management, matchmaking, adoption, NPC detail views, and all mod settings. MCM is deprecated.
- Added Dating and Lover relationship statuses between Candidate and Engaged, with corresponding Ex-Dating and Ex-Lover statuses for ended relationships.
- Added full NPC-NPC relationship support across all statuses. Polygamy remains player-exclusive; NPC-NPC pairs are tracked on paper and integrated into SkyrimNet biography context.
- Added cheating and affairs system with affair types (Casual, Dating, Lover), affair statuses (Ongoing, Ended breakup/confronted/victim split, Allowed), secrecy tracking, and open relationship flags with auto-propagation.
- Added unlimited adoption system mirroring vanilla Hearthfire: polygamy-aware child-to-spouse assignment, vanilla NPC children adoption on marriage, force greeting, home sandbox, pets and critters, child orders, gift system, and configurable child custody policy on divorce.
- Added relationship status history tracking per pair (initiator, previous status, timestamp), viewable in Prisma UI.
- Added confirmation modals for relationship actions with configurable cooldowns; SkyrimNet adds a "Decline (Explain)" AI narration option.
- Added vanilla relationship scanning on first load — existing marriages and parent-child bonds are automatically imported into MARAS tracking.
- Added matchmaking tools in Prisma UI with LLM-assisted evaluation via SkyrimNet.
- Reworked SkyrimNet integration: migrated from Papyrus to native C++ decorators and event registration. Added relationship context decorators (household, co-spouses, infidelity, ex-partners, polygamy), structured event dispatching, and LLM-based relationship status evaluation with retry/ignore modals.
- Improved home sandbox: reworked package override logic to avoid follower framework conflicts, added quest whitelist, improved merchant spouse schedule handling.
- Fixed PlayerMarriedFaction being re-added by third-party mods when polygamy is enabled; added periodic faction sanitizer.
- Migrated major subsystems (relationship tracking, affection, children, package overrides, SkyrimNet dispatching) from Papyrus to native SKSE C++ for improved performance.
- Migrated SkyrimNet assets to Beta 25+ external plugin bundle format (`tetherball88.maras`) with `manifest.json`, standardized action filenames matching in-file names, and modular prompt directory structure.

## 0.0.4.dev - 2025-12-22
- Added MCM setting to adjust daily affection decay multiplier (0.0 = disabled, 1.0 = default, 2.0 = double decay). Set to 0 to disable decay entirely, or increase to make relationship maintenance more challenging.
- Added follower-aware home sandboxing: spouses assigned to player houses who become followers will temporarily drop their house sandboxing package and join you. When released from following, they return to their assigned house (15-second polling cycle for performance).
- Added merchant services for housed spouses: spouses assigned to player houses now become merchants like in vanilla, with support for specialized merchant types (Apothecary, Blacksmith, Fletcher, Jeweler, Hunter, Innkeeper, Misc, Spells, Tailor). Each merchant type shares the same merchant chest with other spouses of that type.
- Fixed PO3 Papyrus Extender linked reference persistence bug: replaced PO3's SetLinkedRef with native SKSE ExtraLinkedRef management to ensure home sandbox markers persist correctly across game loads.

## 0.0.3.dev - 2025-10-12
- Rebuilt proposal acceptance logic with the new `marriageComplexityVariables.json` tuning file, expanded `TTM_ServiceMarriageDifficulty` scoring, and extra MCM switches so candidates must finish social/skill tagging before engagements proceed.
- Broadened SkyrimNet integration by renaming proposal decorators, tightening action eligibility checks, surfacing co-spouse/ex-partner context, and emitting spouse promotion/demotion events for AI awareness.
- Introduced spouse hierarchy tracking that syncs faction ranks, exposes rank menus in the MCM, and applies demotion cooldown spells plus SkyrimNet alerts when positions shift.
- Added a documentation pass across README and new guides (`docs/JSON-CONFIG.md`, `docs/MCM.md`, `docs/SKYRIMNET.md`, `docs/USER-GUIDE.md`) to explain configuration knobs and AI workflows.
- Consolidated cooldown handling into `TTM_CooldownSpell` and refreshed data (initialData.json, prompts, quests) to support the new hierarchy and marriage flow hooks.

## 0.0.2.dev - 2025-10-09
- Added JSON-driven spouse type overrides and maintenance routines so social class/skill assignments can be patched (includes Volkihar family defaults and clothing-based wealth detection).
- Expanded the spouse MCM page with status-aware displays, configurable social/skill classes, home sharing toggles, and marriage chance readouts for candidates.
- Hooked additional mod events for SkyrimNet and OStim to keep sandbox packages in sync after AI-driven scenes and ensure housing packages are restored.
- Authored a comprehensive README covering requirements, features, and optional integrations.
- Recompiled Papyrus artifacts to align with the updated sources and new quest script rename.

## 0.0.1.dev - 2025-10-07
- Initial development release providing the core marry-anyone quest flow, JSON-backed data services, MCM configuration menus, and baseline SkyrimNet integration scaffolding.
