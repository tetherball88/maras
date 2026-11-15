# `AffectionRules` — Architecture & Behavioral Guide

**Layer**: Domain Relationships (`SKSE_Source/modules/domain_relationships`) & Application Coordinator (`SKSE_Source/modules/app`)  
**Sources**:
- [AffectionRules.h](../../../SKSE_Source/modules/domain_relationships/include/maras/domain/relationships/AffectionRules.h)
- [AffectionRules.cpp](../../../SKSE_Source/modules/domain_relationships/src/AffectionRules.cpp)
- [AffectionEventRules.h](../../../SKSE_Source/modules/domain_relationships/include/maras/domain/relationships/AffectionEventRules.h)
- [AffectionEventRules.cpp](../../../SKSE_Source/modules/domain_relationships/src/AffectionEventRules.cpp)
- [AffectionEventCoordinator.h](../../../SKSE_Source/modules/app/include/maras/app/AffectionEventCoordinator.h)
- [AffectionEventCoordinator.cpp](../../../SKSE_Source/modules/app/src/AffectionEventCoordinator.cpp)
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
- **Default Daily `[min, max]` Caps**: Initialized natively on startup and reset inside `AffectionTable::ResetDefaultMinMax` (`WorldTables.cpp`) and persisted in `'AFCT'`:
  - `"intimacy"`: `[-22, 22]`
  - `"gift"`: `[0, 15]`
  - `"dialoguestarted"`: `[0, 5]`
  - `"slept"`: `[0, 8]`
  - `"promotion"`: `[-24, 24]`
  - `"loneliness"`: `[-16, 6]`
  - `"family"`: `[0, 6]`
- At midnight / day-change tick (`CalculateTotalDailyDelta`), each category's accumulated float total is rounded (`std::lround`), clamped independently to its registered `[min, max]` range, and summed into a net integer delta applied to permanent `[0, 100]` affection.

---

## 4. Daily Event Calculators (`AffectionEventRules`)

Interaction delta calculators live in pure engine-free `AffectionEventRules` and are dispatched through `AffectionEventCoordinator`:

| Daily Event Type | Formula & Temperament / Class Scaling |
| :--- | :--- |
| **`Intimacy`** | `baseAffection * mlt`, where `mlt` is `1.5x` for `Romantic`/`Jealous`, `0.5x` for `Independent`, and `1.0x` otherwise. Dispatched natively on `IntimateThreadService::ClearIntimateThread` (`8.0` foreplay, `15.0` sexual) via `EvaluateIntimateThreadCompletion`. |
| **`Gift`** | `goldValue / ((0.667 + static_cast<int>(socialClass) / 7.0) * 100.0)` (~15 pts per 1000g for `Outcast` down to ~6 pts for `Ruler`), scaled by `1.5x` for `Romantic`. Dispatched natively by `GiftMenuEventSink` (`GiftAndIntimacyRules::EvaluateGiftSession`) + `SkyrimNetEventService::EmitAffectionGift` on `"GiftMenu"` close. |
| **`DialogueStarted`** | Flat `+1.0` when opening dialogue with any NPC having a non-`Unknown` relationship status with the Player (`OnDialogueOpened` engine hook). |
| **`Promotion`** | `(proud: 12.0, humble: 4.0, other: 8.0) * rankDiff` triggered automatically on spouse hierarchy slot reconciliation. |
| **`Slept`** | `Romantic = +5.0`, `Jealous = +4.0`, `Independent = +1.0`, `Proud`/`Humble = +3.0`. Dispatched natively for same-cell partners by `SleepEventSink` (`EvaluateSleepPaidAffectionPartners`). |

---

## 5. Permanent Life-Event Scaling, Eligibility & Secrecy Guards

Life-event affection adjustments (`PlanPlayerPartnerAffectionAdjustment`) operate strictly from NPC partners toward the Player and obey these invariants:

1. **Partner Eligibility**: Excluded if null, Player (`0x14`), `kUnknownFatherRawId` (`0xFFFFFFFE`), synthetic child IDs, unregistered adults, or former-partner statuses (`Divorced`, `Jilted`, `ExDating`, `ExLover`). Eligible statuses are `Married`, `Engaged`, `Dating`, `Lover`, and `Candidate` relative to the Player.
2. **Temperament Multiplier & Clamping**:
   - Positive `baseDelta`: `Romantic = 1.35x`, `Proud = 1.20x`, `Independent = 0.80x`, others `1.00x`.
   - Non-positive `baseDelta`: `Jealous = 1.50x`, `Romantic = 1.25x`, `Independent = 0.70x`, `Humble = 0.85x`, others `1.00x`.
   - Scaled float product is truncated toward zero (`static_cast<int>(baseDelta * multiplier)`) and added to permanent score clamped to `[0, 100]`.
3. **Pregnancy & Paternity Secrecy Guards**:
   - `PlanPregnancyConceivedAffection` suppresses `+20` (`pregnancy_conceived` / `player_pregnant`) if `IsMotherPregnancySecretForPartner` determines the mother's unborn child has an undiscovered partner pregnancy secret.
   - `PlanChildMilestoneForParents` prioritizes `presumedFather` (`MARAS` mother partner) over `bioFather`, and suppresses `bioFather` rewards (`+10` for `"child"`/`"training"`, `+15` for `"adult"`) whenever `IsChildSecretForBioFather` is true.
   - `PlanPaternitySecretAffection` normalizes `*_unannounced` outcome suffixes and maps reveals (`mother_confessed_bio` / `bio_father_learnt` `+15`, `mother_confessed_presumed` `-30`, `presumed_father_learnt` `-40`, `mother_shared_pregnancy_presumed` / `pregnancy_visible_presumed` delegates to conception logic, `mother_confessed_both` splits `-30` to presumed father and `+15` to bio father when Mother is Player, fallback `-35`).
4. **Relationship & Infidelity Life-Event Base Deltas**:
   - **Fertility & Family**: Conception `+20`, pregnancy loss `-15`, childbirth `+25 + 10 * max(0, childCount - 1)`, child adoption `+20` (applied once in `LineageUseCases::RegisterChildActor`), child milestone `+10` (`"adult"` `+15`).
   - **Open Relationship Toggle**: Opening gives `Independent +15`, `Jealous -35`, `Romantic -20`, others `-10`; closing gives `Jealous +20`, `Romantic +15`, `Independent -10`, others `+10`. Walk-ins on Player open sex penalize `Jealous` witnesses `-10`.
   - **Cheating & Affairs**: Caught cheating applies `-30` (`victim == Player`) or `-35` (`cheater == Player`); affair confession applies `-30` (`cheater == Player`) or `-20` (`victim == Player`); affair exposure applies `-50` (`cheater == Player`), or `-50` to cheater + `-30` to affair partner (`victim == Player`); affair resolution applies `+10` on `"confronted"` and `-20` on `"breakup"` (`"allowed"` makes no permanent delta).
5. **Legacy Migration Guard (`SessionEpoch`)**: Engine/domain transition hooks inside `AffectionEventCoordinator` suppress automatic affection changes whenever `State::IsLegacyMigrationActive()` (`MARAS.IsMigrating()`) is set during legacy save migration; `BumpSessionEpoch` automatically resets this transient flag on load/revert/new game.

---

## 6. Daily Loneliness & Fidelity Decay Formula

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
