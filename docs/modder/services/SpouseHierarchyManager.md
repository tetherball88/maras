# `SpouseHierarchyManager` — Features & Behavioral Guide

**Source**: [`SpouseHierarchyManager.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseHierarchyManager.cpp)

---

## 1. What This Service Does

In a polygamous household, `SpouseHierarchyManager` maintains the social pecking order among the player's spouses. It manages the **Top 3 Ranked Slots** (**Lead Spouse**, **Second Spouse**, and **Third Spouse**), while any additional spouses belong to the **Fourth+ (`4+`)** tier.

---

## 2. Core Features & Mechanics

### 2.1 Automatic Ranking When Marrying
- **First-Come, First-Served Top 3**: When the player marries a new spouse, the service automatically places them into the highest open slot among the Top 3 (`Lead` $\rightarrow$ `Second` $\rightarrow$ `Third`).
- **Overflow to Fourth+**: Once all three top slots are full, any new spouses automatically enter at the `4+` tier unless the player manually promotes them.

### 2.2 Promotions, Demotions & Rank Swapping
Players can reorganize their spouse hierarchy at any time via dialogue or the Prisma UI Dashboard:
- **Moving into an Empty Slot**: If a top-3 slot is vacant, the selected spouse moves directly into it and receives a promotion event.
- **Direct Rank Swap (Top-3 $\leftrightarrow$ Top-3)**: If the player assigns a ranked spouse (e.g., *Third Spouse*) to a slot already held by another ranked spouse (e.g., *Lead Spouse*), the two spouses **swap positions**. The promoted spouse gains affection while the demoted spouse reacts to losing status.
- **Evicting a Top-3 Spouse (`4+` $\rightarrow$ Top-3)**: If an unranked (`4+`) spouse is promoted into an occupied top-3 slot, the previous occupant is **bumped out of the Top 3** down to the `4+` tier.
- **Emotional Reactions**: Every promotion or demotion fires an event carrying the exact number of ranks gained or lost so Papyrus and SkyrimNet can trigger jealousy, pride, or gratitude based on each spouse's **Temperament**.

### 2.3 Automatic Succession on Divorce or Death
- **No Empty Gaps Above Ranked Spouses**: Whenever a top-3 spouse divorces the player, dies, or is manually removed from the top ranks, the service automatically **compacts the hierarchy**:
  - Remaining top-ranked spouses shift upward in order (e.g., if the *Lead Spouse* dies, the *Second Spouse* becomes the new *Lead Spouse*, and the *Third Spouse* becomes *Second*).
  - Any newly opened 3rd slot is automatically filled by the next married spouse from the `4+` pool.
- **Save Load Hygiene**: When loading a save, the service verifies that all ranked spouses are still alive and valid, automatically promoting surviving spouses if a ranked spouse died prior to the save.
