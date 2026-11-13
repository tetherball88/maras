# AGENTS.md — Skyrim Prisma UI Dashboard (`prisma-src/`)

In-game Skyrim dashboard & modal UI rendered via **Prisma UI** (React 18 + Vite + Zustand + shadcn/ui + Tailwind CSS). Supplements root `AGENTS.md`.

```powershell
# Browser dev (mocked Prisma bridge) / Production build (relative './', editable variables.css)
Set-Location prisma-src; npm run dev
Set-Location prisma-src; npm run build
```

---

## 1. VRAM, DOM & Idle Budget

- **VRAM & GPU**: Shares frame budget with Skyrim. Prefer SVG, CSS gradients, borders, and Unicode glyphs; if raster is unavoidable, use small compressed WebP.
  - **Forbidden**: `backdrop-filter`, large `filter: blur()`, videos, WebGL, canvas particles, infinite animations, root/body opaque backgrounds (outside the window must be 100% transparent; backdrop dim = single `rgba()` layer).
  - Animate **only `transform` and `opacity`** (150–250ms discrete open/close; zero idle animations).
- **DOM & Idle**: Unmount when closed (`{isOpen && <Dashboard />}`) and unmount inactive tabs (never `display: none`). Virtualize lists `> 50` rows (`@tanstack/react-virtual`); target `< 1500` DOM nodes. Zero `setInterval` polling while closed (~0% idle CPU).

---

## 2. Theming & Proportional Scaling (`src/styles/variables.css`)

- `variables.css` is the user-editable public contract: keep it small, plain-English commented, and stable (never rename tokens). Emit it as an **un-minified, un-hashed file** via `public/` + `<link rel="stylesheet" href="./variables.css">` in `index.html` (never bundle into JS).
- **No `px` in components**: Use `rem` (layout) and `em` (component-local/icons, default `1em`), except optional `1px` (`0.0625rem`) hairlines and media queries.
- `html { font-size: var(--ui-font-size); }` is the **only** place `--ui-font-size` is applied; changing it must scale the entire UI proportionally.
- Map Tailwind theme & shadcn CSS variables (`--background`, `--primary`, etc.) to `variables.css` tokens—no hardcoded hex colors, sizes, or inline `style` (except dynamic `transform`/`opacity`). Max 2 subsetted `woff2` fonts.

---

## 3. Prisma Bridge, Zustand & Intrigue Mode

- **Single Bridge (`src/store/prismaBridge.ts`)**: Components never touch `window.*` game APIs directly (`Game <-> prismaBridge <-> Zustand <-> Components`), with automatic browser mock fallback when outside Skyrim.
- **Authoritative Snapshot Replacement & Intrigue Mode (`!settings.revealSecretsInUI`)**:
  - On `receiveMARASOverview`, rebuild `rawChildren`, `rawRelationships`, and `rawCheatingRecords` fresh from the payload (never merge/clone old maps, so hidden `UNBORN_*` or removed entries never linger after toggling `revealSecretsInUI`).
  - Client-side masking in `src/utils/childUtils.ts` (`maskChildrenMap`, `maskChildRecord`, `maskCheatingAndRelationships`) must match C++ world-wide Intrigue Mode (no `0x14` Player exemptions; omit unannounced NPC `unborn`; map `presumedFather` $\to$ `bioFather`; mask `hasBeastblood` & secret flags).
- **Zustand**: Small domain stores (`useMarasStore`, `useModalStore`), **always select slices** (`useModalStore((s) => s.activeModal)` or `useShallow`, never whole-store `useModalStore()`), keep mutations inside store actions, and compute derived values in selectors.

---

## 4. Standalone Modal System (`useModalStore` & `PrismaModalHost`)

- **Independent Root**: `<DashboardLayout>` (`useMarasStore.isOpen`) and `<PrismaModalHost>` (`useModalStore.activeModal !== null`) mount independently in `App.tsx`. Opening a modal must never open `<DashboardLayout>`.
- **Non-Dismissible & Keyboard-Accessible**: No `X` button, no backdrop-click close, no `Escape` close (suppressed in capture phase unless a `<Select>` dropdown is open). Auto-focus primary button on open; support `WASD`, Arrows, `Tab`/`Shift+Tab`, and `Enter`/`Space`/`E`. Resolve solely via `submitModal(buttonId, buttonIndex, customValues)` $\to$ `sendMarasAction('modalResponse', ...)`.
- **Declarative First**: Default `modalType: 'generic'` controls (`'infobox'`, `'checkbox'`, `'select'`, `'text'`, `'checklist'` with badges, Select All, auto-search `> 8` rows, inline row `<SelectInput />`). Register custom components in `modalRegistry.ts` only when declarative controls cannot express the layout.
- **Z-Index**: Window `--z-window: 20` $<$ Modal `--z-modal: 50` $<$ Dropdown `--z-dropdown: 60` $<$ Tooltip `--z-tooltip: 70`.

---

## 5. Components & Vite Build Budget

- **Components**: Reuse primitives (`className` via `cn()`, `React.forwardRef`, variants via **CVA** rather than boolean props, composition over mega-props). Use `React.memo` on list rows, `useCallback` on handlers, and `React.lazy` + `Suspense` on heavy tabs.
- **Vite**: `base: './'`, no source maps in prod, **~250KB gzipped** main chunk cap, no heavy libs (`moment`, full `lodash`, chart/animation libs).
