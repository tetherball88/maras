# AGENTS.md — Skyrim Prisma UI Dashboard

## Project Overview

This is a **dashboard-style window/modal UI for Skyrim**, rendered in-game via **Prisma UI** (SKSE plugin embedding a web view). The UI appears as a **centered window over the game** — it is NOT a regular website and NOT a persistent HUD.

**Stack:** React 18 + Vite + Zustand + shadcn/ui + Tailwind CSS

**Golden rules:**
1. The web view shares **VRAM and GPU time with the game** — stay lightweight.
2. **Everything scales from the root font size.** All dimensions use `rem`/`em`. A user changing one variable in `variables.css` must scale the entire UI.

---

## Critical Constraints (Read First)

### 1. VRAM & GPU Budget
The dashboard is composited into the game's frame. Every GPU layer and repaint competes with Skyrim (often running 4K texture mods) for VRAM.

- **NEVER** use large raster images. Prefer **SVG, CSS gradients, borders, and Unicode glyphs** for all decoration (frames, dividers, ornaments).
- If a raster image is unavoidable: **WebP**, sized to actual display dimensions, aggressively compressed.
- **Forbidden:** `backdrop-filter`, large `filter: blur()`, videos, WebGL, canvas particle effects, infinite looping animations.
- Panel translucency = `rgba()` / gradient backgrounds, **never** blur.
- Animate **only `transform` and `opacity`**. Never animate `width`, `height`, `margin`, `box-shadow`, or `filter`.
- Keep animations short and discrete (150–250ms open/close transitions). No animation should run while the dashboard sits idle.
- Everything outside the dashboard window must be **fully transparent** — the root/body never paints a background. A dim overlay behind the modal, if desired, is a single semi-transparent `rgba()` element, nothing more.

### 2. DOM Size & Lifecycle
- When the dashboard is **closed, render nothing** — an empty transparent root. Unmount, don't hide (`{isOpen && <Dashboard />}`).
- Unmount inactive tabs/pages of the dashboard rather than keeping them in the DOM with `display: none`.
- Virtualize lists that can exceed ~50 rows (`@tanstack/react-virtual`).
- Target: **< 1500 mounted DOM nodes**.

### 3. Idle Cost
- No polling (`setInterval`) while the dashboard is closed. Updates are event-driven from the game via the Prisma bridge.
- Idle CPU when closed must be ~0%.

---

## Theming & Scaling — `variables.css` (MANDATORY)

All user-tweakable design tokens live in a single file: **`src/styles/variables.css`**. This file is the public contract with end users — mod users will open it and edit values by hand. Keep it **small, commented, and stable** (don't rename variables between versions).

### Rules

1. **Every size in the app is `rem` or `em`. `px` is forbidden** except for:
   - `1px` hairline borders (optional — prefer `0.0625rem`)
   - Media queries
2. The **entire UI scales from `--ui-font-size`** set on `:root`/host element.
   Changing it from `16px` to `20px` must proportionally scale the whole dashboard — text, spacing, the window itself, icons, everything.
3. Component-internal sizes that should scale with *that component's* text use `em`; global/layout sizes use `rem`.
4. Components **never hardcode** colors, font sizes, radii, or spacing — they consume CSS variables (directly or through Tailwind theme mapping).
5. New tokens require a comment explaining what they affect, in plain language a non-developer can understand.

- `html { font-size: var(--ui-font-size); }` is the ONLY place `--ui-font-size` is applied.
- Tailwind config must map to these variables (e.g. `colors.accent: 'var(--color-accent)'`, spacing/font-size scales pointing at the tokens) so utility classes stay tweakable through `variables.css`.
- shadcn's theme variables (`--background`, `--primary`, etc.) must be **aliased to these tokens**, not defined with independent values.

---

## Game Communication (Prisma Bridge)

- ALL game communication (SKSE/Papyrus) goes through one module: `lib/prisma.ts`. Components must NEVER touch `window.*` game APIs directly.
- Data flow:
  ```
  Game → prisma bridge → Zustand store → components
  Components → prisma bridge → Game
  ```
- The bridge must detect a plain browser (`npm run dev`) and fall back to **mock data**, so the dashboard is fully developable without launching Skyrim.
- Dashboard open/close is driven by game events (key press handled by the plugin); the bridge dispatches these to the UI store.

---

## Zustand Rules

- One store per domain, kept small (`useUiStore`, `usePlayerStore`, ...).
- **Always use selectors** — never subscribe to the whole store:
  ```ts
  // ✅ Re-renders only when this value changes
  const gold = usePlayerStore((s) => s.gold);

  // ❌ Re-renders on any change
  const store = usePlayerStore();
  ```
- Use `useShallow` when selecting multiple values.
- Actions live inside stores; components contain no mutation logic.
- Never store derived data — compute in selectors.

---

## Component Guidelines

### Reusability
- **Build primitives once, compose everywhere.** Check existing shared components before creating new ones.
- Every reusable component must:
  - Accept `className`, merged via `cn()`
  - Use `React.forwardRef` where DOM access matters
  - Define variants with **CVA** (`class-variance-authority`), not boolean props:
    ```tsx
    // ✅ <Panel variant="inset" size="sm" />
    // ❌ <Panel isInset isSmall />
    ```
- Prefer **composition over configuration** — children/slots instead of mega-components with 15 props.
- shadcn primitives stay unmodified where possible; Skyrim styling comes from the theme variables and thin wrapper components.
- Icon components take size in `em` (`1em` default) so icons scale with the text they sit next to.

### Performance
- `React.memo` for list rows and frequently re-rendered leaves.
- `useCallback` for handlers passed to memoized children.
- Lazy-load heavy dashboard tabs with `React.lazy` + `Suspense` so opening the window is instant.

### Styling
- Tailwind utilities backed by the CSS variables; no hardcoded hex values, no hardcoded `px` sizes in components.
- No inline `style` except dynamic `transform`/`opacity`.
- Max **2 font files**, `woff2` only, subset to used glyphs.

---

## Vite / Build Rules

- `base: './'` in `vite.config.ts` — output must work from local/`file://` serving.
- `variables.css` must be emitted as a **separate, un-minified, un-hashed file** in the build output so users can edit it after install:
  - Load it via `<link rel="stylesheet" href="./variables.css">` in `index.html` and copy it through `public/` (do NOT import it into the JS bundle).
- No source maps in shipped builds.
- Main chunk budget: **~250KB gzipped** — flag any PR exceeding it.
- No heavy dependencies without discussion (no moment.js, full lodash, chart or animation libraries; prefer CSS transitions).

---

## Do / Don't Summary

| ✅ Do | ❌ Don't |
|---|---|
| All sizes in `rem`/`em` | `px` sizes in components |
| Tokens from `variables.css` | Hardcoded colors/sizes |
| SVG + CSS gradients | Raster decoration images |
| Animate `transform`/`opacity` | Animate layout or `filter` |
| Unmount closed dashboard/tabs | `display:none` heavy trees |
| `rgba()` translucent panels | `backdrop-filter: blur()` |
| Zustand selectors | Whole-store subscriptions |
| Event-driven game updates | Polling with `setInterval` |
| Editable `variables.css` in dist | Bundling user tokens into JS |

---

## Commands

```bash
npm run dev        # Browser dev with mocked Prisma bridge
npm run build      # Production build (relative paths, editable variables.css)
```
