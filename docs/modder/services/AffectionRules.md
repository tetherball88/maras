# `AffectionRules` — Architecture & Behavioral Guide

**Layer**: Domain Relationships (`SKSE_Source/modules/domain_relationships`)  
**Sources**:
- [AffectionRules.h](../../../SKSE_Source/modules/domain_relationships/include/maras/domain/relationships/AffectionRules.h)
- [AffectionRules.cpp](../../../SKSE_Source/modules/domain_relationships/src/AffectionRules.cpp)
- [EngineSyncPlan.cpp](../../../SKSE_Source/modules/state/src/EngineSyncPlan.cpp)
- [Architecture Overview](../ARCHITECTURE.md)

---

## 1. Purpose & Responsibility

`MARAS::Domain::Relationships::AffectionRules` encapsulates all pure, engine-free math for permanent affection tiers (`[0, 100]`), daily interaction delta accumulation, and daily loneliness/relational decay across active Player partners. Stateful application and persistence of affection scores reside in `AffectionTable` inside [`WorldStore`](WorldStoreAndCoSave.md) and are driven by [`RelationshipUseCases`](RelationshipUseCases.md).

---

## 2. Affection Thresholds & Gameplay Scaling

| Permanent Score | Threshold Tier (`GetAffectionThreshold`) | Spouse Buff Multiplier (`ComputeAffectionMultiplier`) |
| :--- | :--- | :--- |
| **`75 .. 100`** | `"happy"` | `1.25x` |
| **`50 .. 74`** | `"content"` | `1.00x` |
| **`25 .. 49`** | `"troubled"` | `0.25x` |
| **`0 .. 24`** | `"estranged"` | `0.00x` (grants zero household/follower buffs) |

---

## 3. Daily Interaction Caps & Delta Aggregation

Throughout an in-game day, dialogue, gifts, dates, and intimate events record typed values into `AffectionTable::dailyByType` (normalized to lowercase keys via `NormalizeAffectionType`):
- Each interaction type supports configurable `[min, max]` daily caps persisted in `'AFCT'`.
- At midnight / day-change tick (`CalculateTotalDailyDelta`), each category's accumulated float total is rounded (`std::lround`), clamped independently to its registered `[min, max]` range, and summed into a net integer delta applied to permanent `[0, 100]` affection.

---

## 4. Daily Loneliness & Fidelity Decay Formula

`CalculateLonelinessDecay(snap, npcId, isFollowing, activePartnerCount)` determines the automatic daily affection change when `SettingsData::affectionDecayMultiplier > 0`:

1. **Active Follower Bonus (`+6.0`)**: If the partner is currently traveling as an active follower (`isFollowing == true`), loneliness decay is replaced by a flat `+6.0` daily affection boost.
2. **Base Decay by Temperament** (for non-following active partners):
   - `Independent`: `-1.0` / day
   - `Humble`: `-2.0` / day
   - `Proud` / `Jealous` (default): `-3.0` / day
   - `Romantic`: `-4.0` / day
3. **Relationship Tier Scaling (`statusMult`)**: `Married = 1.00x`, `Engaged = 0.75x`, `Dating = 0.50x`, `Lover = 0.25x`.
4. **Polygamy Load Scaling (`polyMult`)**: Reduces per-spouse decay pressure in larger households (`>= 7` active partners $\rightarrow 0.50\text{x}$, `>= 5` $\rightarrow 0.60\text{x}$, `>= 3` $\rightarrow 0.75\text{x}$, otherwise `1.00x`).
5. **Open Relationship Modifier (`openRelMult`)**: When `IsMainTierStatus && edge->isOpen`, scales decay by temperament reaction: `Independent = 0.50x`, `Humble = 0.75x`, `Proud = 0.90x`, `Romantic = 1.15x`, `Jealous = 1.35x`.
6. **Known Ongoing Infidelity Multiplier (`cheatingMult`)**: If an ongoing **discovered** (`!isSecret`) affair exists between `(npc, Player)`, scales decay up by temperament: `Independent = 1.25x`, `Humble = 1.35x`, `Romantic = 1.50x`, `Proud = 1.75x`, `Jealous = 2.00x`.
7. **Global Setting Scalar**: Multiplied by `SettingsData::affectionDecayMultiplier`.
