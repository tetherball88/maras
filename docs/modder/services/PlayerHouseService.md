# PlayerHouseService — Feature & Mechanics Guide

**Implementation**: [PlayerHouseService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerHouseService.cpp)

`PlayerHouseService` manages all player-owned residences (vanilla Hearthfire/city homes as well as custom mod-added houses registered by the player), tracks which spouses and children live in which property, synchronizes home sandbox markers and AI package overrides, and equips housed spouses with home-store merchant factions.

---

## 1. Player House Registration & Marker Synchronization

A residence is identified by its **Location FormID** paired with an interior **Center/Sandbox Marker reference**.

- **Initial Registration & Deferred Marker Updates**:
  - A house can be registered before its interior marker is known (or updated later when the player visits/marks the interior).
  - Whenever a house's interior marker is registered or updated, the service automatically iterates through every tenant currently assigned to that house:
    - **Child Tenants**: Re-triggers child house assignment so the child's home marker moves to the updated location marker.
    - **Adult Spouse Tenants**: Teleports each spouse's dedicated personal home marker (`GetOrCreateHomeMarker`) directly to the house's marker reference so their sandbox package immediately anchors to the new room/interior.
- **Global House Counter Synchronization**:
  - Every time a new player residence is registered, the service updates the `maras_player_houses_count` global variable in `TT_MARAS.esp` so Creation Kit dialogue conditions know whether the player owns at least one home (`> 0`).
- **First-Time Relocation Tracking**:
  - Each registered house tracks an `initialMoveDone` flag so dialogue and quest scripts can distinguish between a newly acquired property and one that has already received its initial household assignment.

---

## 2. Tenant Registration & Home Store Merchant Factions

When an NPC is moved into a player house, `PlayerHouseService` handles both their physical AI routine and their household vendor role:

### Adult Spouse Tenants
1. **Housed Faction Assignment**:
   - Adds the spouse to the `MARAS_SpouseHousedFaction` (`0x112` in `TT_MARAS.esp`), unlocking housed-spouse dialogue and AI package conditions.
2. **Automatic Home Store Merchant Mapping**:
   - Inspects the spouse's vanilla/mod vendor factions to determine what kind of shop they should run at home:
     - **Specialist Merchants**: Checks 8 specific professions (**Apothecary, Blacksmith, Fletcher, Hunter, Innkeeper, Jeweler, Spell Vendor, Tailor**). If the spouse belongs to one or more of those vanilla job factions, they are added to the corresponding MARAS housed-merchant faction(s).
     - **General Store Fallback**: If the spouse does not belong to any of the 8 specialist vendor factions, they are automatically granted the default general merchant factions (`JobMerchantFaction`, `JobMiscFaction`, and `MARAS_MiscMerchantHousedFaction`) so any spouse can run a general goods shop from home.
3. **Personal Home Marker Teleportation**:
   - Moves the spouse's persistent personal home marker (`XMarker`) to the target house's sandbox marker.
4. **Stay-at-Home vs. Default Routine Toggle**:
   - Each adult tenant has a configurable **Stay at Home** setting (`stayAtHome`):
     - **Enabled (`true`)**: Registers the spouse with [PackageOverrideService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PackageOverrideService.cpp), forcing them to sandbox inside the player home via the `TT_MARAS.esp` override quest.
     - **Disabled (`false`)**: Unregisters the spouse from `PackageOverrideService`, allowing them to resume their vanilla daily schedule around town while still remaining registered as a resident of that house.
   - Re-enabling `stayAtHome` refreshes the spouse's home marker position to ensure they return to the correct house.

### Child Tenants
- **Mandatory Home Sandbox**: Children assigned to a player house always have `stayAtHome` forced to `true` and are never given adult housed-spouse or merchant factions.

### Moving Between Houses & Eviction Cleanup
- Moving a tenant from House A to House B automatically detaches them from House A's tenant list, adds them to House B, and updates their home marker without stripping and re-adding their merchant factions.
- Removing a tenant completely from player housing unregisters their AI package override and strips both `MARAS_SpouseHousedFaction` and all 11 housed-merchant factions.

---

## 3. Tenant Eviction Protections

To prevent broken family custody states, the UI and dialogue systems query `PlayerHouseService` before allowing a tenant to be evicted from player housing:

- **Spouses with Children Cannot Be Evicted Standalone**: If an adult tenant shares one or more children with the player, they cannot be removed from player housing into homelessness; they can only be relocated to another house.
- **Children with a Married Co-Guardian Cannot Be Evicted Standalone**: If a child in the player's household has a co-guardian who is currently **Married** to the player, the child cannot be removed from housing independently of that marriage branch.

---

## 4. Branch-Aware Family Unit Relocation

When the player relocates a spouse or a child via the Prisma UI Dashboard or dialogue, `PlayerHouseService` relocates the **entire nuclear family branch** together while strictly isolating other spouses' branches in polygamous households:

### Relocating via an Adult Spouse/Partner
- Verifies that the adult is either a romantic partner of the player (`Dating`, `Lover`, `Engaged`, `Married`) or shares a child with the player.
- Collects **only** the children whose custodial or biological records (`guardianA`, `guardianB`, `bioMother`, `bioFather`) link to **that specific spouse**.
- Moves the target spouse (preserving their existing `stayAtHome` preference) and all of their branch's children to the destination house.
- **Polygamy Isolation Guarantee**: Children belonging to the player's *other* spouses are never touched when relocating one specific spouse.

### Relocating via a Child
- Verifies that the child belongs to the player's household.
- Identifies which specific player spouse shares custody of that child:
  - **Co-Parented Child**: Gathers all sibling children belonging to that same `(Player, Spouse)` branch. If `moveParents` is enabled, moves the co-parent spouse alongside the children to the destination house; otherwise moves only the children of that branch.
  - **Single-Parent Player Child (No Co-Guardian)**: If the player is the sole guardian (`coGuardian == 0`), gathers all other single-parent children of the player who also lack a co-guardian and relocates them together.

---

## 5. Persistence & Co-Save Recovery

- Persists all registered player houses (Location FormID, Marker FormID, `initialMoveDone` flag) and their ordered tenant lists (including each tenant's `stayAtHome` toggle added in co-save version 4, defaulting to `true` when upgrading older saves).
- On save load, resolves all FormIDs against the active load order, skips any uninstalled house/NPC mods cleanly, and triggers `PackageOverrideService::RebuildFromTenants()` so all `stayAtHome` spouses immediately regain their home AI package overrides.

