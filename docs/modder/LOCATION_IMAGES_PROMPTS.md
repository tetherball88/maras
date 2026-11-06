# Child Training Location Image Prompts

This document contains the self-contained image generation prompts used to create the 20 training location thumbnails for [`SKSE/Plugins/MARAS/childTraining.yaml`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE/Plugins/MARAS/childTraining.yaml), as well as a reusable template for modders adding new training locations via `Data/SKSE/Plugins/MARAS/childTraining/*.yaml`.

---

## Target Image Specifications

- **Directory**: [`SKSE/Plugins/MARAS/childTraining/images/`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE/Plugins/MARAS/childTraining/images) (`Data/SKSE/Plugins/MARAS/childTraining/images/` in-game)
- **Target Resolution**: **`688 × 384`** (`16:9` / `43:24` landscape, rendered in a `9rem × 5rem` frame in [`ChildTrainingModal.tsx`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/prisma-src/src/components/npcDetail/ChildTrainingModal.tsx))
- **Format**: `.webp` (`85–88%` quality, Lanczos downscaling from native `2752 × 1536` Gemini output)
- **Visual Style**: Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (`#100c07`), using only muted antique gold (`#c8a84b`) and warm cream parchment (`#f0e6d2`) ink lines with crosshatching to match the M.A.R.A.S **"Candlelit Amber"** dashboard theme.

---

## Reusable Prompt Template (For Modders Adding New Locations)

Paste this standalone prompt into Gemini (replacing `<SUBJECT DESCRIPTION>`):

```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: <SUBJECT DESCRIPTION>. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

---

## Built-In Training Location Prompts (22 Locations)

### 0. `mage.webp` — **Mage (College of Winterhold)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the high stone bridge and central tower of the College of Winterhold framed by an arcane astrolabe and open grimoire. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing fireballs or neon effects, NO full color, NO text or letters, NO outer border frame.
```

### 1. `warrior.webp` — **Warrior (Jorrvaskr)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the overturned Nordic longship roof of Jorrvaskr mead hall behind a crossed Skyforge steel greatsword, round shield, and roaring wolf crest. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 2. `thief.webp` — **Thief (The Cistern — Thieves Guild)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the arched sewer vaults of the Riften Cistern with a skeleton key, crossed lockpicks, a coin purse, and a hooded rogue silhouette in profile. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 3. `assassin.webp` — **Assassin (The Sanctuary — Dark Brotherhood)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: a carved stone skull doorway of the Dark Brotherhood Sanctuary with a curved ritual dagger, poison vial, and a stylized handprint emblem. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 4. `bard.webp` — **Bard (The Bards College)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: a wooden lute, rolled parchment scrolls, quill, and rapier resting before the arched stone windows of the Bards College in Solitude. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 5. `stormcloak.webp` — **Stormcloak (Palace of the Kings)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the ancient stone facade of the Palace of the Kings in Windhelm with the roaring Windhelm bear heraldry and crossed two-handed Nordic battleaxes. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 6. `legionnaire.webp` — **Legionnaire (Castle Dour)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the crenellated stone towers of Castle Dour in Solitude behind a plumed Imperial Legion helmet, gladius sword, and diamond-shaped Imperial dragon shield. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 7. `dragon_slayer.webp` — **Dragon Slayer (Sky Haven Temple)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the carved stone relief of Alduin's Wall inside Sky Haven Temple with an Akaviri katana, Blades shield, and coiled dragon motif. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 8. `dawnguard.webp` — **Dawnguard (Fort Dawnguard)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the high stone keep of Fort Dawnguard framed by a heavy steel crossbow, war axe, Dawnguard full-helm, and a stylized sunburst emblem. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 9. `necromancer.webp` — **Necromancer (College of Whispers)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: a horned skull staff, faceted grand soul gem, candelabras, and geometric ritual circle inside the arched stone vault of the College of Whispers. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 10. `vigilant.webp` — **Vigilant (Stendarr's Beacon)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the mountain watchtower of Stendarr's Beacon with a flanged steel mace, chained tome, and the pouring chalice amulet of Stendarr. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 11. `healer.webp` — **Healer (Temple of Kynareth)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the spreading branches of the Gildergreen tree inside the Temple of Kynareth in Whiterun framed by a healer's staff, doves, and botanical herbs. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 12. `conjurer.webp` — **Conjurer (House Telvanni)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: a giant twisted Telvanni mushroom wizard tower in Morrowind framed by Daedric summoning sigils, a bound bow, and a high-collared Dunmer mage silhouette. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 13. `illusionist.webp` — **Illusionist (School of Julianos)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the triangular sacred geometry symbol of Julianos above classical Imperial colonnades, an Elven dagger, and symmetric mind-eye motifs. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 14. `battlemage.webp` — **Battlemage (The Synod Conclave)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the spired Imperial council hall of the Synod Conclave with a heavy armored gauntlet gripping both an Elven longsword and a geometric arcane burst symbol. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 15. `ranger.webp` — **Ranger (Fighters Guild)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: a recurve Elven bow, quiver of arrows, and crossed Elven longsword in front of a fortified Fighters Guild hall and pine forest silhouette. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO geometric neon, NO full color, NO text or letters, NO outer border frame.
```

### 16. `dawnbringer.webp` — **Dawnbringer (Shrine of Meridia)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the winged angelic statue of Meridia atop Mount Kilkreath with geometric sunbeam linework, a radiant longsword, and an Elven shield. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 17. `shadowblade.webp` — **Shadowblade (Sacellum of Boethiah)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the jagged mountain shrine statue of Boethiah with a horned Ebony plate helmet, serrated Ebony blade, and coiled serpent engraving. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 18. `nightingale.webp` — **Nightingale (Nightingale Hall)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the circular Nightingale bird-in-moon emblem framed by a masked Nightingale hood, the Nightingale Blade, and the Nightingale Bow. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 19. `dread_scion.webp` — **Dread Scion (Shrine of Molag Bal)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the horned demonic altar of Molag Bal with the spiked Mace of Molag Bal, iron chains, and gothic vampire plate armor motifs. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 20. `pack_hunter.webp` — **Primal Pack Hunter (Frostmoon Crag)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the snow-swept rocky cliffs and pine timber camp of Frostmoon Crag in Solstheim beneath a crescent moon, framed by a howling wolf silhouette, a carved Nordic bow, and Hircine's horned stag-and-wolf totem. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 21. `pack_berserker.webp` — **Primal Pack Berserker (The Underforge)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the stone ritual font of the Underforge cavern beneath the Skyforge in Whiterun, flanked by a snarling werewolf silhouette, a two-handed Skyforge steel battleaxe, and the Companions wolf crest. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```
