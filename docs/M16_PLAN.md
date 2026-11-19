# M16 – UI and HUD: implementation plan

## Source requirements (docs/ROADMAP.md, lines 1301–1310)

- **Goal:** Flight HUD, targeting, navigation, communications, inventory,
  station interaction, menus, maps, interaction prompts.
  HUD styling follows the M9.5 toon art direction (outlined panels,
  flat colors, graphic rim accents).
- **Proof:** Full game loop playable with UI (no console commands needed).
- **Tech:** Dear ImGui (dev tools) + custom or RmlUi (final UI).
- **Depends on:** M10 (ECS), M15 (Gameplay), M15.0 (flight HUD is the
  first consumer; the full HUD expands from it).

The roadmap defines **no `16.x` steps**. The table below proposes them.

## Proposed tech decision

**Final UI: custom toon UI**, built on the existing `starforge::Hud` facade
and `hud.slang` (outlined panels, flat colours, rim accents). Rationale:

- The toon art direction is already implemented; RmlUi would need a full
  HTML/CSS renderer integrated through the public API plus custom decorators
  to match the look — large cost, no visual gain.
- A custom retained-mode canvas keeps rendering in one public-facade pass,
  stays within the ADR-004 UI budget slice, and is fully testable headlessly.
- Dear ImGui remains available for **dev/debug panels only** (not the shipped
  UI), as the roadmap specifies.

**Text**: built-in deterministic ASCII bitmap font (8×8, glyphs 32–126) —
no external assets, golden-testable, sufficient for labels/chat/maps.

## Architecture

```
apps/game/
  ui_state.hpp/.cpp        CPU UI state, screens, focus, hit-test (T1-testable)
  ui_layout.hpp/.cpp       pure layout/geometry helpers (T1-testable)
  ui_screens/*.cpp         screen builders (flight HUD, comms, map, ...)
include/starforge/
  ui.hpp                   public UiCanvas facade (pImpl, no Vulkan types)
src/public_api/ui.cpp      bridge to internal renderer
src/environment/ui_renderer.*  instanced rect + text + bar/outline rendering
src/gameplay/ui_text.hpp   bitmap font glyph table (pure CPU)
```

The game builds a `UiCanvas` each frame from `UiState`; one `record()` call
draws everything through the public API. Input flows: `Application` →
`UiState::handleInput()` → gameplay methods (no console commands).

---

## Task breakdown

Each task is independently committable and ends with tests. Dependencies
are listed; earlier tasks are prerequisites for later ones.

### 16.1 — Absolute mouse position + UI input primitives
- Add absolute cursor position to `Application`/`Platform::Window`
  (`mousePosition()`), independent of relative-motion steering.
- Add mouse-button edge (pressed/released) tracking; keep existing levels.
- `Space` flight↔UI toggle already exists; P/Esc pause already exists.
- **Verify:** T1 test for input edges; headless smoke for position tracking.
- **Done when:** UI mode can report an absolute cursor and click edges
  without affecting flight steering.

### 16.2 — Bitmap font + text rendering primitive
- Pure-CPU glyph table (8×8 ASCII 32–126) in `src/gameplay/ui_text.hpp`.
- Internal text renderer (one instanced draw per glyph atlas page).
- Public `UiCanvas::addText()` (position, colour, scale, string).
- **Verify:** T1 glyph metrics tests; T3 golden rendering a known string.
- **Done when:** a string renders deterministically through the public API
  with a golden image.

### 16.3 — Toon UI canvas (panels, outlines, bars, buttons)
- Extend `starforge::Hud`/new `starforge::UiCanvas` with `addRect`,
  `addBar`, `addButton`, `addText`, `clear`, `record`.
- Retained canvas → single instanced draw; toon outline + flat fill +
  rim accent from `hud.slang` styling.
- **Verify:** T3 golden of a composite panel; budget test under ADR-004 UI
  slice.
- **Done when:** a full panel with outline, bars and text renders in one
  public-facade call with zero validation errors.

### 16.4 — UI state machine + hit-testing/focus
- CPU `UiState`: screens (Flight, Pause, Inventory, Station, Comms, Map,
  Menu), focus stack, widget hover/press, click hit-test, keyboard nav.
- Pure geometry helpers (`ui_layout.hpp`) for hit rectangles.
- **Verify:** T1 tests for hit-testing, focus transitions, screen stack.
- **Done when:** click/hover/focus can be driven headlessly with assertions.

### 16.5 — Flight HUD + targeting
- Flight overlay: throttle/speed, shield/hull bars, credits/cargo,
  crosshair, current system, jump range (replaces M15 console logging).
- Targeting: cycle targets (Tab), target info panel, lock/lead reticle,
  hostile/neutral colour coding from faction.
- **Verify:** T3 golden of flight HUD; T1 tests for target cycle/lock state.
- **Done when:** all flight-critical info is on screen and target selection
  works without console commands.

### 16.6 — Navigation / radar
- Radar: nearby rocks, hostile, planet, station markers with bearing and
  distance; player at centre, range rings.
- Navigation panel: selected destination, bearing, ETA with boost.
- **Verify:** T1 tests for world→radar projection and contact filtering;
  T3 golden of the radar widget.
- **Done when:** navigation is readable on screen and tracks the moving
  ship/hostile correctly.

### 16.7 — Communications (chat)
- Message log (combat events, trade confirmations, mission updates) +
  player text input with keyboard capture in UI mode.
- Channels (local/system/combat) with colour coding; command-free sending.
- **Verify:** T1 tests for log ring buffer and text-entry state; T3 golden.
- **Done when:** messages appear in the UI and the player can type/send.

### 16.8 — Inventory + station interaction
- Cargo list with quantities; station market screen with buy/sell buttons
  driven by clicks (replaces M15's arrow-key keys).
- Docking prompt + docking state reflected in the UI.
- **Verify:** T1 tests for click→buy/sell transactions through UiState;
  T3 golden of the station screen.
- **Done when:** buy/sell/dock are fully mouse-driven in the demo.

### 16.9 — Menus, maps, interaction prompts, toasts
- Pause menu (resume/controls/quit), galaxy & sector maps (systems, current
  location, jump range), contextual interaction prompts, toast notification
  queue (replaces console logs for transient events).
- **Verify:** T1 tests for menu navigation, map projection, toast expiry;
  T3 goldens for map + toast overlay.
- **Done when:** menus/maps/prompts/toasts render and respond to input.

### 16.10 — Full-loop UI proof + budget gate
- End-to-end: launch → fly (flight HUD) → target/fight → dock → buy/sell →
  accept/complete mission → jump → repeat, **entirely through UI**.
- 5-minute run, p95 < ADR-004 interval, zero validation errors, screenshots
  reviewed for toon visual identity.
- **Verify:** T5 headless simulation through `UiState` + GPU goldens.
- **Done when:** the M16 Proof line is satisfied and ROADMAP marks all steps ✅.

---

## Decisions (confirmed with user)
1. **Final UI: custom toon UI** for now. **Fallback: RmlUi** if the custom
   solution cannot deliver the required UI functionality — revisit if any
   task below proves unachievable with the custom canvas.
2. **One commit per task** (16.1–16.10), each independently testable.
3. **Strict order**: implement 16.1 → 16.10 sequentially; no reprioritisation.
