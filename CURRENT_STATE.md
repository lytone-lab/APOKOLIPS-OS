# APOKOLIPS-OS — Current State

**Rewritten 2026-10-16.**

Read first: `AI_HANDOFF.md`, `TEAM_PROTOCOL.md`, `PROJECT_STATE.txt`.
This file is a fast snapshot — go to `PROJECT_STATE.txt` for the bug
list, `CUSTOMISATION_VISION.md` for the long-term plan.

---

## Repo

`github.com/lytone-lab/APOKOLIPS-OS` — main branch, HEAD at
`d3aaee5` (ApoNotify service + Glyph wiring + flash card style fix).

Ubuntu VM via SSH (host = Windows 11, port 2222). sway/Wayland +
Qt6. `apokolips-shell.service` user unit. Debug build (`-j2`),
3 GB VM RAM.

## Shipped recently

- Shared protocols: Open With, Recents, drag-and-drop.
- Clipboard panel: traffic lights, images/files, pin, persistence.
- Launcher theming: columns / blur / tint / 3x3 position anchor.
- Widget blip fix (no more 1 Hz raise war).
- Now Playing desktop widget with controls.

## Currently open (reconciled 2026-10-17)

- Per-app chrome rewrite — Settings UI + config shipped
  (settings.h:5140+, visualconfig.h:40,490,767). Application to apps
  at construct is what's left. V1 #4 next up.
- Weather widget — SHIPPED (WeatherCard in widgets.h:1277). Doc
  claim was stale.
- Genie family — RETIRED (Rule #2). "Option A + mesh-warp V2" above
  are stale; do not resume without user naming it.
- Restore via animator daemon — low pri (no felt delay).
- Clipboard rich drag-out polish — open.
- Keyboard bindings Settings page — open, reverted 2026-10-09,
  redesign needed.
- Accessibility High-contrast / Reduce-motion — labels honest today
  (GTK works, shell V2). V2 plan logged in PROJECT_STATE WIRING
  BACKLOG.

## Rules

- V1 only, by default. P2/P3 and V2/V3+ parked unless named.
  See TEAM_PROTOCOL.md Rule #2.
- Diagnostic before every feature — not just bugs.
- Read the whole function before patching.
- Verify every helper exists.
- Trace lambda lifetimes.
- Twin loops change together.
- Unfiltered build output (`tail -20`, `echo exit: $?`).

See `TEAM_PROTOCOL.md` for the full rule set.
