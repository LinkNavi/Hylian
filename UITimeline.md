# DustUI Timeline

Target API: `Dust/DustUI-API.md`. Tracking status here since the full spec
(text, custom shader widgets, layer system) is bigger than one pass —
this file gets updated as phases land, not written once and forgotten.

## Status: Phase 1 in progress

---

## Phase 1 — Core widgets, layout, solid rendering (this pass)

Goal: every non-text widget in the spec works — `Widget`/`Row`/`Column`/
`Stack`, units, anchoring, size/background/border(rounded corners)/padding/
opacity, immediate-mode `beginUI()`/`endUI()`, drawn on top of the 3D pass.
Enough to build the Hotbar/Minimap/HP-bar examples from the spec, just
without the name labels on them yet.

- [ ] Units — `px()`, `pct()`, `vw()`, `vh()`
- [ ] `Anchor` enum + 9-point offset math (screen-relative for top-level
      widgets, parent-relative for children)
- [ ] `Color` struct + named constants + `Color::fromHex`
- [ ] `Widget` — value-tree (`std::vector<Widget> children`), fluent
      builder returning `*this`: `.size/.background/.border/.padding/
      .anchor/.opacity/.child`
- [ ] `.text()` — accepted and stored on the widget so call sites compile,
      but does not render yet (needs Phase 2). Logs once per widget if hit.
- [ ] `Row`/`Column`/`Stack` layout modes + `.gap()`
- [ ] Layout pass — resolves every widget's screen-space rect top-down
      (px/pct/vw/vh -> pixels, anchor offsets, flow layout inside Row/
      Column, Stack overlays with each child anchored independently)
- [ ] Render pass — one quad mesh reused for every widget, one SDF
      rounded-rect-with-border shader (embedded like default.vert/frag,
      not a file games have to ship), alpha blend on, depth test off,
      orthographic screen-space projection
- [ ] `DustEngine::beginUI()` / `endUI()` — rebuilds the tree every frame
      (no diffing yet, see Phase 4), draws after `endMode3D()` so UI is
      always on top per the spec
- [ ] Demo in Runtime — a couple of widgets (bars, hotbar-style slots) added
      to the feature showcase, screenshot-verified against the real GPU

Deliberately NOT in this phase (see later phases): `.text()` actually
rendering, `.shader()` custom widgets, sprites, dirty-rect diffing, the
HUD/overlay/world-space layer system (marked TBD in the spec itself).

---

## Phase 2 — Font System (blocks real `.text()`)

goals.txt lists this as its own unstarted section — multi-font support,
one font instance usable at multiple sizes. `.text()` on Widget goes from
"stored but inert" to actually rasterizing once this lands. Needed before
Name Tag, labeled stat bars, or anything with a title works per spec.

- [ ] Glyph atlas generation (stb_truetype or freetype — TBD)
- [ ] Font loading through AssetManager (packed like models/textures)
- [ ] Text layout (single-line first; wrapping later if needed)
- [ ] Wire into Widget's `.text(str, size, color)`

---

## Phase 3 — Custom shader widgets

`.shader("name.frag", [](ShaderParams&){...})` — spiral charge meters, bent
bars, liquid fills. Needs a per-widget pipeline cache keyed by shader path
(can't precompile these like the default UI shader, they're user-authored)
and a `ShaderParams` push-constant builder.

- [ ] `UI::ShaderParams` — typed `.set(name, value)` -> push constant bytes
- [ ] Pipeline cache: shader path -> built pipeline, built lazily on first use
- [ ] `.shader()` on Widget, wired through the render pass

---

## Phase 4 — Polish

- [ ] Immediate-mode diffing — skip re-drawing widgets whose resolved
      rect/params didn't change frame-to-frame (spec says this should
      happen; Phase 1 just redraws everything, correct but not optimized)
- [ ] Sprites (`.sprite()` or similar — not in the spec's Widget section
      explicitly, called out under Anchors/Units intro, needs an actual API)
- [ ] Layer system — HUD/overlay/world-space UI, explicitly TBD in the spec
