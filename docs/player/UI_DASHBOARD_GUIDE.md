# 🖥️✨ The M.A.R.A.S Dashboard Guide — Your Romance & Household Command Center

Remember the dark ages of Skyrim modding, when checking on your spouses meant scrolling through 47 laggy SkyUI MCM pages while waiting three seconds for each slider to load? 🐢

Say goodbye to all of that! **M.A.R.A.S** features a fast, modern, interactive **In-Game Dashboard** powered by **Prisma UI**. Whether you want to check why your spouse is grumpy, move your family to a new manor, play Cupid with two townsfolk in Whiterun, or inspect the entire province's soap-opera gossip, it's all one keypress away.

---

> [!TIP]
> **Related Player Guides**
> - [💞 Relationship Overview](RELATIONSHIP_OVERVIEW.md) — Dating, marriage, open relationships, cheating drama, and matchmaking rules.
> - [❤️ Affection System Guide](AFFECTION_SYSTEM.md) — How daily moods, gifts, and loneliness decay work.
> - [🎭 Archetypes & Buffs](ARCHETYPES_AND_BUFFS.md) — Social Classes, Skill Types, Temperaments, and how buffs scale.
> - [👶 Fertility Mode Reloaded Guide](FERTILITY_MODE_RELOADED_INTEGRATION_USER_GUIDE.md) — Pregnancy, biological children, and paternity secrets.

---

## 1. Opening & Navigating the Dashboard ⌨️🖱️

- **Open / Close Hotkey**: Press **`Alt + B`** during gameplay (default) to pop open the M.A.R.A.S Dashboard right over your game!
  - *Want a different key?* You can toggle **`Ctrl`**, **`Shift`**, or **`Alt`** modifiers in **Settings $\rightarrow$ General** inside the Dashboard, or set any custom key combo in `Data/SKSE/Plugins/MARAS.ini` (`ToggleUI = Alt+B`) so it never clashes with your other mods!
- **Pause / Unpause Game (`⏸️` Button in Top-Right Header & Auto-Pause Setting)**: In the top-right header bar is a **Pause / Unpause Game** button. Click it anytime to pause or unpause Skyrim while browsing the Dashboard (`Paused` badge lights up in gold when active). Prefer the game to pause automatically every time you open the Dashboard? Enable **Auto-Pause Game on Dashboard Open** in **Settings $\rightarrow$ General**!
- **Full Screen Mode (`⛶` Button & Setting)**: Want the Dashboard to fill your entire monitor instead of floating as a centered window? Click the **Full Screen Mode** toggle button in the top-right header bar (or toggle **Full Screen Mode** in **Settings $\rightarrow$ General**). Your preference is saved automatically with your game!
- **Accessibility — UI & Font Scaling (`- 100% +` Header Control & Slider)**: Playing on a 4K TV, ultrawide monitor, or Steam Deck? Use the quick **`- / 100% / +`** scale buttons in the top-right header bar (or the **UI & Font Scale (Accessibility)** slider in **Settings $\rightarrow$ General**) to scale all fonts, icons, cards, and UI elements proportionally from **75% to 150%**! (Click the percentage readout in the header at any time to reset to `100%`.)
- **In-Game Confirmation Popups (Mouse & Keyboard Navigation)**: Whenever an in-game confirmation popup appears (such as a relationship proposal, breakup/divorce prompt, home selection, or matchmaking review), you can use either your **Mouse** or **Keyboard** (**`WASD`**, **Arrow Keys**, or **`Tab` / `Shift+Tab`** to move between buttons and options, and **`Enter`**, **`Space`**, or **`E`** to select).
- **Secret Visibility Toggle (`👁️` Button Next to M.A.R.A.S Title)**: By default, the Dashboard launches in **Intrigue Mode (Secrets Hidden)** (`EyeOff` icon, greyed out) so your playthrough stays full of mystery! While secrets are hidden, undiscovered affairs and paternity secrets are masked across the entire UI as if no secret exists (e.g., a child's Presumed Father is shown as their normal Biological Father, and secret affairs stay hidden until discovered or exposed in-game). Want to peek behind the curtain? Click the eye button next to the **M.A.R.A.S** title to switch to **All Secrets Revealed** (`Eye` icon, highlighted bright)—and your choice is saved automatically with your game!
- **Children Flee Button (`👶` Button Next to M.A.R.A.S Title)**: Next to the **M.A.R.A.S** title in the top-left header bar (visible on every page) is a little baby-icon button. Click it to make every child in your current cell run away from you. They flee through the nearest door, and any child still stuck next to you after about 12 seconds is moved out of the way. **It resets when you change cell**: as soon as you leave the cell you pressed it in, the children go back to normal (press it again in the new cell if needed). You don't always have to press it: M.A.R.A.S **automatically** makes children in the cell flee whenever an **OStim or SexLab** scene starts there, and releases them when the scene ends. Your own children also offer the same thing as a **dialogue option**, so you can trigger it by talking to them without opening the Dashboard. The button is greyed out when you have no children; hover it for a reminder of what it does.
- **Collapsible Sidebar**: On the left side of the window, you'll find the **4 Main Tabs** (**Overview**, **My Household**, **Matchmaking**, and **Settings**), each with its own sub-pages. Click the little sidebar icon at the top-left if you want to collapse the menu into sleek icons for extra screen space!

---

## 2. Tab 1: 📊 Overview — The Skyrim Gossip & Status Hub

The **Overview** tab gives you a bird's-eye view of both your personal love life and every relationship happening across Skyrim:

### 🏠 Dashboard (Home View)
- **Relationship Scoreboard**: Quick stat cards at the top showing how many **Married**, **Dating**, **Lovers**, **Engaged**, and **Divorced** NPCs you have.
- **Top 3 Spouse Hierarchy Podium**: Spotlights your **Lead (`1st`)**, **Second (`2nd`)**, and **Third (`3rd`)** spouses along with their current affection and buffs.
- **Affection Health Overview**: A quick-glance health monitor for all of your partners' mood bars—perfect for spotting who hasn't seen you in 5 days before they slide into *Estranged*!
- **Recent Activity Feed**: Shows the 5 newest relationship events across Skyrim.

### 📜 Activity Feed (The Gossip Column)
- Want the full tea? 🍵 This sub-page logs **every relationship change across Skyrim in chronological order**—who started dating whom, who got engaged, who broke up, who got divorced, and when it happened!

### 🔍 Browse NPCs & All Pairs
- **Browse NPCs**: A split-screen inspector! Search or filter any NPC on the left, and click them to see their entire relationship tree, secret affairs, exes, and children on the right.
- **All Pairs**: A master table of every couple in the game (both yours and NPC-to-NPC), complete with badges showing their status (`Married`, `Dating`, `Lover`, etc.), whether they're in an **Open Relationship**, or if someone is **Cheating**!

---

## 3. Tab 2: 🏡 My Household — Partners, Kids & Buffs

Everything under your roof lives here across three sub-pages:

### 💍 Partners
Manage all of your current and former partners in one place:
- Filter by **Married**, **Engaged**, **Dating**, **Lovers**, **Affair Partners**, or **Exes**.
- Promote or demote spouses in your **Spouse Hierarchy (`1st`, `2nd`, `3rd`, `4th+`)**.
- Toggle **Open Relationships** on or off for any committed partner.
- Assign spouses to live in any of your registered **Player Houses**.
- **Remove Relationship Record (`🗑️` Button)**: Accidentally imported a custom follower as a `Lover` on an existing save, or want to completely wipe a relationship or ex-partner record without leaving breakup history behind? Click the **Remove Relationship Record** trash icon on any partner row (also available in **Overview $\rightarrow$ All Pairs**, **NPC Detail $\rightarrow$ Partner Panel**, and **Matchmaking $\rightarrow$ Manual Match**) to cleanly erase the M.A.R.A.S relationship record, history, and affair logs while keeping the NPC registered as a **Candidate**!

### 👶 Children
Your family roster! Tracks both **Vanilla/Hearthfire adopted kids** and **biological children** (from *Fertility Mode Reloaded* or *BeeingFemale*):
- See every child's life stage badge: **🤰 Expecting / Unborn**, **🍼 Newborn Infant**, **🧸 Child**, **🎓 In Training**, or **⚔️ Adult Offspring**.
- View both their **Biological Parents** (genetic lineage) and their **Official Guardians** (who actually has custody and raises them).
- Spot **Paternity Secrets** (when a partner doesn't know who the real father is, or when a baby looks suspiciously nothing like the spouse!).

### ⚡ Household Buffs
No calculator required! This page breaks down all **14 Spouse Buffs**:
- **8 Permanent Social Class Buffs** (always active from your spouses, scaled by your Monogamy `2.0x` bonus or Polygamy Hierarchy rank + their Affection mood).
- **6 Follower Combat Skill Buffs** (active when a spouse is traveling with you).
- Shows your **exact combined bonus** for each buff, which spouses are actively contributing right now, and even which spouses sitting at home *could* give you a combat buff if you bring them along on your next dungeon run!

---

## 4. Tab 3: 💘 Matchmaking — Playing Cupid Across Skyrim

Feeling like Mara's personal assistant—or a chaotic soap-opera director? The **Matchmaking** tab has three sub-pages:

### ✨ Manual Match (Play God with Anyone's Love Life)
- Pick **NPC A** on the left and **NPC B** on the right (you can even pick **yourself** or search any NPC in Skyrim's entire database!).
- Instantly set or change their relationship status (**Lover**, **Dating**, **Engaged**, **Married**), break them up (**Ex-Lover**, **Ex-Dating**, **Jilted**, **Divorced**), toggle their **Open Relationship** status, or click **Remove Relationship Record** to wipe the pair's relationship record completely!

### 🤖 LLM Matchmaking *(Requires SkyrimNet)*
- Let the **SkyrimNet AI Game Master** scan Skyrim's singles and couples!
- Run a **Global World Scan** or pick a specific NPC to get tailored romantic suggestions based on their personalities and backgrounds.
- Read the AI's explanation for *why* two NPCs belong together (or why they should break up!), preview or **edit the custom first-person memories** the AI writes for both characters, and click **Apply** to make it canon in your game!

### 🍼 Child Adoption (For You *or* NPC Couples!)
- Want to adopt an eligible child into your own household without hunting down dialogue triggers? Do it right here!
- **Bonus Feature — NPC Couple Adoption**: You can also select any **NPC-to-NPC married couple** and assign an orphan to *their* family! M.A.R.A.S automatically registers them as the child's guardians and relocates the child into the couple's shared home. 🏠💖

---

## 5. Deep-Dive Profiles: Clicking on Any NPC (or Child!) 🕵️‍♀️🪪

Clicking on **any NPC's name or card** anywhere in the Dashboard opens their full **Character Detail Page**:

### 🚀 Top-Right Quick Action Bar
- 🧭 **Teleport Player To**: Instantly fast-travel yourself right to that NPC's exact spot in the world!
- 🙋 **Teleport NPC To Player**: Summon a lost spouse, follower, or child right to your feet!
- 💘 **Matchmaking**: Open a dedicated matchmaking pop-up specifically for that NPC.
- 👶 **Spawn Child** *(for grown biological children)*: Turns an abstract biological child record into a real, physical child actor walking around your home!
- ✨ **Evaluate Secret** *(for children with Paternity Secrets + SkyrimNet)*: Ask the SkyrimNet AI to evaluate whether a hidden paternity secret gets discovered!

### 🛠️ What You Can Customize Inside an NPC's Profile
- **Archetype Overrides**: Disagree with how M.A.R.A.S classified an NPC? Use the dropdowns to change their **Social Class**, **Skill Type**, or **Temperament** on the spot!
- **Affection Slider**: View their exact `0–100` Affection score, see how many days it's been since you last visited, or manually slide their affection up or down.
- **Spouse Rank & Housing**: Change a spouse's **Hierarchy Rank**, move them (and their kids!) to a different **Player House**, and toggle **Stay at Home** so they don't wander back to their old town.
- **AI Package Inspector**: See the exact Creation Engine AI package the NPC is running right now (great for checking if they're sandboxing at home, following you, or working).
- **Parent & Guardian Slot Swapping** *(on a Child's profile)*: Click any of the parent cards (**Guardian A**, **Guardian B**, **Biological Mother** [female NPCs], **Biological Father** [male NPCs], or **Presumed Father** [male NPCs, visible when secrets are revealed]) to open an inline searchable NPC picker and reassign or clear any guardian or lineage slot on the fly!
- 🐕 **Child Pet & Critter Manager** *(on your Child's profile)*:
  - See if your child has adopted a **Follower Pet** (like Meeko or a stray dog) or dragged home a **Wild Critter** (like a mudcrab, skeever, or frostbite spider! 🕷️).
  - Click **Dismiss** if you want the critter out of your house, or toggle **Ban Pets / Ban Critters** for that specific child so they never bring another one home!

---

## 6. Tab 4: ⚙️ Settings — Make It Your Own

The **Settings** tab replaces traditional MCM pages with **9 organized sub-pages**:

1. 🎛️ **General**: Configure **Interface & Accessibility** (**Auto-Pause Game on Dashboard Open**, **Full Screen Mode**, and **UI & Font Scale (`75%–150%`)**), change your **Dashboard Hotkey** (and `Ctrl`/`Shift`/`Alt` modifiers), toggle **Player Polygamy**, toggle **Disable Polygamy Quest** (stops/resets *"The Flame Divides Not"* and prevents it starting upon sleep), toggle **Skip Wedding Ceremony**, and adjust the **Affection Decay Multiplier** slider (`0.0x` for zero decay up to `2.0x` hardcore).
2. ☑️ **Confirmations**: Choose which relationship milestones (*Dating*, *Lover*, *Engaged*, *Married*, *Breakup/Divorce*, *Open/Close*) show an in-game confirmation prompt before happening.
3. 💘 **Matchmaking**: Toggle automatic background matchmaking, switch between **LLM Mode** and **Weighted Dice-Roll Mode**, set scan intervals, NPC cooldown days, **Exclude Player's Active Partners**, **Allow Cheating**, **7 Sexual Orientation filters**, and customize Dice-Roll probability weights.
4. 🎬 **NPC Scenes**: Toggle ambient couple banter in the world, set cooldown hours, choose which relationship/cheating scene types can play, and toggle the **15 Location Keyword rules** (so couples banter in taverns, temples, and towns—not in the middle of a bandit cave!).
5. 👶 **Children & Pets**: Choose your **Divorce Child Custody Policy** (*Stay with Mother*, *Stay with Father*, *Always with Player*, or *Never with Player*) and global child pet/critter permissions.
6. 🧬 **Fertility**: Configure **Fertility Mode Reloaded (FMR) / BeeingFemale** integration options.
7. 🤖 **AI Dialogues**: Configure autonomous AI relationship proposals, affair confessions/exposures, and paternity reveals.
8. 🛡️ **SkyrimNet**: View SkyrimNet connection status or toggle **Simulate No SkyrimNet** for testing fallback behavior.
9. 🔧 **Cheats & Debug**: Export full human-readable JSON diagnostic snapshots (`Dump MARAS Data` and `Dump NPC Index` to `Data/SKSE/Plugins/MARAS/dumps/`) for troubleshooting or modder inspection.

---

## 7. Pro Tip: Resizing & Customizing the Look (`variables.css`) 🎨📏

Playing on a massive 4K TV from the couch and want bigger text? Or playing on a compact laptop screen and want the Dashboard smaller?
- Every single panel, font, icon, and button in the M.A.R.A.S Dashboard scales proportionally from **one single setting**!
- Open `Data/PrismaUI/views/MARAS/variables.css` in any text editor (like Notepad) and change `--ui-font-size: 16px;` to `20px` (bigger) or `14px` (smaller)—the entire window and every icon inside it will scale up or down automatically! You can even tweak the accent colors and panel transparency right in that same file. ✨

