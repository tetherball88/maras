# 👶🍼 Kids, Adoption & Fertility Guide — Raising a Dynasty (Without Losing Your Mind)

Vanilla Skyrim treats family life like a strict apartment lease: *"Maximum 2 children, maximum 1 dog, maximum 1 pet mudcrab, and nobody ever grows up or has biological babies."* 🚫🏠

**M.A.R.A.S** rips up that lease and throws it into the fires of Red Mountain! Whether you want to adopt every street urchin in Skyrim, raise a biological dynasty with **Fertility Mode Reloaded (FMR)** or **Beeing Female NG**, or watch messy **Secret Paternity** scandals blow up across Whiterun, here is how children and family life work.

---

> [!TIP]
> **Related Player Guides**
> - [🖥️ UI Dashboard Guide](UI_DASHBOARD_GUIDE.md) — How to manage your children, pets, adoptions, and paternity secrets in the Dashboard (`Alt + B`).
> - [💞 Relationship Overview](RELATIONSHIP_OVERVIEW.md) — Marriage, polygamy, cheating, and matchmaking.
> - [❤️ Affection System Guide](AFFECTION_SYSTEM.md) — How pregnancy, childbirth, and adoption boost your partner's love.

---

## 1. The Short & Sweet Version ⚡

- **Unlimited Kids**: No cap of 2 (vanilla) or 6 (Hearthfire Multiple Adoptions). Adopt or give birth to as many kids as your houses (and your PC's CPU!) can handle.
- **Two Ways to Grow Your Family**:
  1. 🧸 **Adoption (Works Out of the Box!)**: Adopt street urchins (Lucia, Sofie, Blaise, Alesan), Honorhall orphans, or modded adoptable kids—either through dialogue or with one click in the **Dashboard $\rightarrow$ Matchmaking $\rightarrow$ Child Adoption** tab!
  2. 🤰 **Biological Pregnancy & Birth**: Install **Fertility Mode Reloaded (FMR v1.0.7+)** or **Beeing Female NG (v3.6.4+)** and both you *and* NPCs across Skyrim can conceive, carry babies (including twins and triplets!), and raise them from newborns to adult followers.
- **Every Kid Gets Their Own Pets**: No more siblings fighting over a single fox—every child can have their own **Dog** and their own **Woodland Critter**.
- **Secret Dads & Custody Battles**: If a baby's real father isn't the mother's spouse, M.A.R.A.S tracks the **Paternity Secret**—and if the marriage ends in divorce, custody rules decide who gets the kids!

> [!WARNING]
> **Critical Version Requirements:**
> When using an optional fertility engine, the following minimum versions are **strictly required**:
> - **Fertility Mode Reloaded (FMR)**: **v1.0.7 or higher**
> - **Beeing Female NG**: **v3.6.4 or higher**
> 
> Older versions lack essential lifecycle ModEvents and parameter payloads required for M.A.R.A.S.'s pregnancy and child lifecycle integration.

---

## 2. "Paper Kids" vs. "Real Walking Kids" 📝🏃‍♂️

Most fertility mods don't spawn a 3D toddler the second a baby is born—they track babies as data records or inventory items first. M.A.R.A.S bridges both worlds seamlessly:

| Kid Type | What It Means | Where You'll See Them |
| :--- | :--- | :--- |
| 📝 **"Paper Kids"** *(Unspawned Records)* | They exist in M.A.R.A.S with a name, age, race, biological parents, and guardians, but don't have a 3D body walking around yet. | Inside the **M.A.R.A.S Dashboard** (`My Household -> Children`), and **SkyrimNet AI** NPCs know all about them and talk about them! |
| 🏃‍♂️ **"Real Kids"** *(Spawned Actors)* | A physical child (or grown-up adult follower!) walking around Skyrim. | Living in your homes, playing tag, doing chores, or fighting beside you as an adult follower! |

> [!TIP]
> **The One-Click `Spawn Child` Button!**
> Have a biological child who reached the `🧸 Child (Unspawned)` stage? Open their profile in the **M.A.R.A.S Dashboard (`Alt + B`)** and click **`Spawn Child`** in the top-right corner! M.A.R.A.S will spawn their physical 3D child actor right into the world and link all of their family history automatically. ✨

---

## 3. Life Under Your Roof: Guardians, Chores & Pets 🏡🐕

### 👨‍👩‍👧 Biological Parents vs. Official Guardians
M.A.R.A.S separates **Biological Parents** *(whose DNA made the kid)* from **Official Guardians** *(who actually raises the kid)*:
- **Single Spouse**: Any child you adopt or bring home is automatically co-parented by your spouse.
- **Multiple Spouses (Polygamy)**: You choose which spouse is assigned to co-parent and look after the child!
- **Biological Mother Priority**: If one of your spouses gives birth to a baby, that child is automatically assigned to their biological mother.
- **Single Parent (Unmarried Dragonborn)**: Not married? No problem! You can adopt and assign kids directly to any of your Player Houses on your own.

### 🎮 What Your Kids Do at Home
Your children behave just like lively Hearthfire kids—and a little smarter:
- **Attention-Hungry Greetings**: When you come home from an adventure, M.A.R.A.S checks which child you haven't talked to in the longest time and sends *them* running up to greet you first!
- **Classic Hearthfire Kid Life**: Your kids ask for allowance, accept gifts (sweetrolls, wooden swords, dolls, clothes), play tag and hide-and-seek, do household chores, and chat with their siblings when 2+ kids live under the same roof!
- **Spouse Housing & The "Stay at Home" Toggle**:
  - A spouse who is looking after a child **must** stay assigned to a Player House (you can't leave the kids home alone with no address!).
  - *Don't want your spouse stuck indoors 24/7?* Uncheck **"Stay at Home"** on the spouse's Dashboard profile! They'll still be the child's guardian, but they'll go back to running their town shop or daily routine during the day.

### 🦊 Pets & Woodland Critters (One Per Kid!)
In vanilla Skyrim, your entire household had to share **1 dog** and **1 wild critter**. In M.A.R.A.S, **every single child can own 1 Follower Pet AND 1 Woodland Critter**!
- **Saying "No!" Has a 15-Day Cooldown**: If you tell a child they can't keep a stray dog or mudcrab (or if their pet Tragically Meets a Sabre Cat), that child will wait **15 in-game days** before asking for another one.
- **Evicting & Banning Critters**: Did your kid bring home a giant **Frostbite Spider** and you hate looking at it? 🕷️ Open your child's profile in the **Dashboard**, click **Dismiss** to banish the critter immediately, and toggle **Ban Critters** (or **Ban Pets**) for that child—or turn pets/critters off globally in **Settings $\rightarrow$ Children & Pets**!

### ⚠️ The 20-Kid Sanity Rule (Don't Melt Breezehome!)
While M.A.R.A.S has **no limit** on how many children you can have across Skyrim, Skyrim's 2011 game engine still has feelings:
- Up to **20 children per house** get full interactive quest scenes (force-greetings, chores, gift animations).
- Any children beyond **#20 in the same house** simply hang out and sandbox peacefully around the house so your game doesn't lag. (Spread your big family across multiple castles and manors!)

---

## 4. Kids in NPC Households & The Orphanage Safety Net 🏘️⛪

NPC couples across Skyrim can also have biological babies or adopt children (you can even assign orphans to NPC couples in **Dashboard $\rightarrow$ Matchmaking $\rightarrow$ Child Adoption**!):
- **Who They Live With**: NPC kids live with their **mother** by default, or with their **father** if the mother isn't around.
- **The Honorhall Orphanage Safety Net**: Skyrim is a dangerous place. If **both parents of an NPC child die** (from dragon attacks, vampires, or an "accidental" stray Fireball), the orphaned child automatically packs their bags and moves to **Honorhall Orphanage in Riften**, where you—or another NPC couple—can adopt them! 🥺💖

---

## 5. Divorce & Child Custody Battles ⚖️💔

When a marriage ends in divorce, who gets the kids? M.A.R.A.S resolves custody step-by-step using rules you can customize in **Dashboard $\rightarrow$ Settings $\rightarrow$ Children & Pets**:

1. 🧬 **Step 1: Biological Parent First** *(Toggleable)*
   - If one partner is the child's **biological parent** and the other is an adoptive step-parent, the biological parent automatically wins custody.
2. 👑 **Step 2: Your Personal Custody Rule** *(When YOU Divorce a Spouse)*
   - **Always with Player**: The kids stay in your household.
   - **Never with Player**: Your ex-spouse takes the kids with them when they move out *(and M.A.R.A.S cleanly restores the child's original town factions!)*.
   - **Use Gender Policy**: Passes the decision down to Step 3.
3. 🚻 **Step 3: Gender Custody Policy** *(For Male + Female Couples)*
   - Choose whether children default to **Stay with Mother** *(default)* or **Stay with Father**.
4. 🪙 **Step 4: The 50/50 Coin Flip** *(For Same-Sex Couples)*
   - If none of the rules above break the tie, M.A.R.A.S flips a fair 50/50 coin!

---

## 6. Secret Dads & Paternity Bombshells 🕵️‍♂️💣

What happens when a married (or dating/engaged) woman gets pregnant by someone who is **not** her committed partner?
M.A.R.A.S automatically creates a **Paternity Secret**! At first, neither the deceived partner nor the real biological father knows the truth.

### 🔍 How the Secret Comes Out
- **The Ticking Clock**: Every day that passes as the child grows older increases the chance that someone connects the dots.
- **Wait... Why Does Our Baby Have Cat Ears?! (`Phenotype Mismatch`)**: If the child's race/appearance obviously doesn't match the presumed father, the daily chance of discovery **skyrockets**! 🐱👀
- **With SkyrimNet AI**: The mother can confess the truth in conversation, either father can start asking pointed questions to investigate, or you can click **`Evaluate Secret`** right on the child's Dashboard profile!
- **Without SkyrimNet AI**: M.A.R.A.S rolls behind-the-scenes dice each day using your **Settings $\rightarrow$ Fertility** probabilities.

### 💥 The Fallout & "Real Dad Steps Up"
When a paternity secret blows open:
1. **Emotional Earthquake**: The deceived partner suffers a massive **`-30` to `-40` Affection** drop (while a biological father learning he has a secret child gains **`+15` Affection**!).
2. **Forgiveness or Divorce**: The betrayed partner decides whether to forgive and raise the child anyway, or **divorce the mother on the spot**.
3. **Does the Real Father Step In?**: If the deceived partner walks out, M.A.R.A.S checks whether the **true biological father steps up**! If he does (and isn't already her ex-husband!), he marries the mother and welcomes her and the child into his household.
4. **Divorce Exposes Everything**: If a couple divorces for *any* reason, **all hidden paternity secrets between them are revealed immediately**!

---

## 7. Fertility Mode Reloaded (FMR) & Beeing Female NG Integration 🤰🧬

M.A.R.A.S doesn't replace your fertility mod's pregnancy cycles—instead, it listens to **Fertility Mode Reloaded (FMR)** and **Beeing Female NG** events and turns every pregnancy into a full family story:

| Life Stage Badge | What's Happening in Your Game |
| :--- | :--- |
| 🤰 **Expecting / Unborn** | Conception! M.A.R.A.S records the mother, bio-father (supporting **twins & triplets**—even multi-father twins in Beeing Female NG!), and checks if it's a Paternity Secret. Gives **`+20` Affection**! *(If a pregnancy is terminated early, the record is cleanly removed.)* |
| 🍼 **Newborn Infant** | Birth! The baby arrives (`+25` Affection, plus `+10` per extra twin/triplet!) and is tracked as a newborn infant. |
| 🧸 **Child** | The baby grows into a child (`+10` Affection)! Your kids get the name you chose; NPC couples' kids automatically get a **lore-friendly racial name**. |
| 🎓 **In Training** | *(FMR)* When you send a child off to train with a guild or mentor, M.A.R.A.S tracks their training class and location! |
| ⚔️ **Adult Offspring** | Your child is all grown up (`+15` Affection)! They become a recruitable adult follower in Skyrim, keep their full family tree in the Dashboard, and **SkyrimNet AI** recognizes them as your grown son or daughter! |

---

## 8. Installing Mid-Playthrough? How Migration Works 🔄💾

Already have adopted kids or FMR/Beeing Female babies on your current save? M.A.R.A.S automatically imports them when you load your game:
1. **Vanilla & Hearthfire Adopted Kids**: Automatically imports your existing adopted children, your spouse, and your home, then cleanly shuts down the old capped Hearthfire adoption quests so you can keep adopting more!
2. **Beeing Female NG & FMR Babies**: Automatically imports active pregnancies (every baby with its own father), carried baby items, existing child actors and recent births.
   - **Beeing Female NG children**: M.A.R.A.S. looks after your Beeing Female children's daily behavior (home routines, staying out of follower mode) and Beeing Female's own child conversation menu is switched off in favor of M.A.R.A.S.'s family dialogue. Beeing Female still grows them up and can turn them into adults.
   - *(One tiny FMR quirk to know: Because FMR itself only saves the most recent birth per mother and doesn't save which mother gave birth to which older named child in its list, if you had lots of kids across multiple wives *before* installing M.A.R.A.S, double-check their assigned mothers in the Dashboard!)* 😉
