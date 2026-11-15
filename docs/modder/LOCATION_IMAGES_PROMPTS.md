# Child Training Location Image Prompts

This document contains the self-contained image generation prompts used to create the 22 training location thumbnails for [`SKSE/Plugins/MARAS/childTraining.yaml`](../../SKSE/Plugins/MARAS/childTraining.yaml), as well as a reusable template for modders adding new training locations via `Data/SKSE/Plugins/MARAS/childTraining/*.yaml`.

---

## Target Image Specifications

- **Directory**: [`SKSE/Plugins/MARAS/childTraining/images/`](../../SKSE/Plugins/MARAS/childTraining/images) (`Data/SKSE/Plugins/MARAS/childTraining/images/` in-game)
- **Target Resolution**: **`688 × 384`** (`16:9` / `43:24` landscape, rendered in a `9rem × 5rem` frame in [`ChildTrainingModal.tsx`](../../ui-src/src/components/npcDetail/ChildTrainingModal.tsx))
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

### 0. `College of Winterhold.webp` — **Arcane Evoker (`mage`, College of Winterhold)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the high stone bridge and central tower of the College of Winterhold framed by an arcane astrolabe and open grimoire. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing fireballs or neon effects, NO full color, NO text or letters, NO outer border frame.
```

### 1. `Jorrvaskr.webp` — **Shield-Brother Vanguard (`warrior`, Jorrvaskr)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the overturned Nordic longship roof of Jorrvaskr mead hall behind a crossed Skyforge steel greatsword, round shield, and roaring wolf crest. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 2. `The Cistern - Thieves Guild.webp` — **Shadowfoot Infiltrator (`thief`, The Cistern)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the arched sewer vaults of the Riften Cistern with a skeleton key, crossed lockpicks, a coin purse, and a hooded rogue silhouette in profile. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 3. `The Sanctuary - Dark Brotherhood.webp` — **Dark Brotherhood Silencer (`assassin`, The Sanctuary)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: a carved stone skull doorway of the Dark Brotherhood Sanctuary with a curved ritual dagger, poison vial, and a stylized handprint emblem. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 4. `The Bards College.webp` — **Spellsword Skald (`bard`, The Bards College)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: a wooden lute, rolled parchment scrolls, quill, and rapier resting before the arched stone windows of the Bards College in Solitude. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 5. `Palace of the Kings - Stormcloak.webp` — **Stormcloak Shock-Trooper (`stormcloak`, Palace of the Kings)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the ancient stone facade of the Palace of the Kings in Windhelm with the roaring Windhelm bear heraldry and crossed two-handed Nordic battleaxes. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 6. `Castle Dour - Legionnaire.webp` — **Imperial Praetorian (`legionnaire`, Castle Dour)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the crenellated stone towers of Castle Dour in Solitude behind a plumed Imperial Legion helmet, gladius sword, and diamond-shaped Imperial dragon shield. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 7. `Sky Haven Temple.webp` — **Akaviri Dragonguard (`dragon_slayer`, Sky Haven Temple)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the carved stone relief of Alduin's Wall inside Sky Haven Temple with an Akaviri katana, Blades shield, and coiled dragon motif. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 8. `Fort Dawnguard.webp` — **Dawnguard Inquisitor (`dawnguard`, Fort Dawnguard)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the high stone keep of Fort Dawnguard framed by a heavy steel crossbow, war axe, Dawnguard full-helm, and a stylized sunburst emblem. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 9. `College of Whispers.webp` — **Whispers Necromancer (`necromancer`, College of Whispers)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: a horned skull staff, faceted grand soul gem, candelabras, and geometric ritual circle inside the arched stone vault of the College of Whispers. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 10. `Stendarr's Beacon.webp` — **Paladin of Stendarr (`vigilant`, Stendarr's Beacon)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the mountain watchtower of Stendarr's Beacon with a flanged steel mace, chained tome, and the pouring chalice amulet of Stendarr. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 11. `Temple of Kynareth.webp` — **Cleric of Kynareth (`healer`, Temple of Kynareth)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the spreading branches of the Gildergreen tree inside the Temple of Kynareth in Whiterun framed by a healer's staff, doves, and botanical herbs. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 12. `House Telvanni.webp` — **Telvanni Spellwright (`conjurer`, House Telvanni)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: a giant twisted Telvanni mushroom wizard tower in Morrowind framed by Daedric summoning sigils, a bound bow, and a high-collared Dunmer mage silhouette. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 13. `School of Julianos.webp` — **Julianos Mindweaver (`illusionist`, School of Julianos)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the triangular sacred geometry symbol of Julianos above classical Imperial colonnades, an Elven dagger, and symmetric mind-eye motifs. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 14. `The Synod Conclave.webp` — **Synod Warmage (`battlemage`, The Synod Conclave)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the spired Imperial council hall of the Synod Conclave with a heavy armored gauntlet gripping both an Elven longsword and a geometric arcane burst symbol. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 15. `Fighters Guild.webp` — **Guild Wardenscout (`ranger`, Fighters Guild)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: a recurve Elven bow, quiver of arrows, and crossed Elven longsword in front of a fortified Fighters Guild hall and pine forest silhouette. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO geometric neon, NO full color, NO text or letters, NO outer border frame.
```

### 16. `Shrine of Meridia.webp` — **Meridian Dawnbringer (`dawnbringer`, Shrine of Meridia)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the winged angelic statue of Meridia atop Mount Kilkreath with geometric sunbeam linework, a radiant longsword, and an Elven shield. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 17. `Sacellum of Boethiah.webp` — **Boethian Shadowblade (`shadowblade`, Sacellum of Boethiah)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the jagged mountain shrine statue of Boethiah with a horned Ebony plate helmet, serrated Ebony blade, and coiled serpent engraving. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 18. `Nightingale Hall.webp` — **Nightingale Sentinel (`nightingale`, Nightingale Hall)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the circular Nightingale bird-in-moon emblem framed by a masked Nightingale hood, the Nightingale Blade, and the Nightingale Bow. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 19. `Shrine of Molag Bal.webp` — **Coldharbour Dreadknight (`dread_scion`, Shrine of Molag Bal)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the horned demonic altar of Molag Bal with the spiked Mace of Molag Bal, iron chains, and gothic vampire plate armor motifs. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 20. `Frostmoon Crag.webp` — **Primal Pack Hunter (`pack_hunter`, Frostmoon Crag)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the snow-swept rocky cliffs and pine timber camp of Frostmoon Crag in Solstheim beneath a crescent moon, framed by a howling wolf silhouette, a carved Nordic bow, and Hircine's horned stag-and-wolf totem. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```

### 21. `Under Forge.webp` — **Primal Pack Berserker (`pack_berserker`, The Underforge)**
```text
Flat 2D medieval woodcut and etched brass line-art illustration on a solid matte warm-black background (#100c07), 16:9 aspect ratio. Two-tone color palette ONLY: muted antique gold (#c8a84b) and warm cream parchment (#f0e6d2) ink lines with crosshatching. Subject: the stone ritual font of the Underforge cavern beneath the Skyforge in Whiterun, flanked by a snarling werewolf silhouette, a two-handed Skyforge steel battleaxe, and the Companions wolf crest. Strictly flat 2D printmaking engraving style, NO 3D rendering, NO photorealism, NO glowing effects, NO full color, NO text or letters, NO outer border frame.
```
