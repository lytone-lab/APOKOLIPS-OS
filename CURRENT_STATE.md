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
Qt6. `apokolips-shell.service` user unit. Debug build (`-j1`),
3 GB VM RAM.

## Shipped recently

- Shared protocols: Open With, Recents, drag-and-drop.
- Clipboard panel: traffic lights, images/files, pin, persistence.
- Launcher theming: columns / blur / tint / 3x3 position anchor.
- Widget blip fix (no more 1 Hz raise war).
- Now Playing desktop widget with controls.

## Currently open

- Per-app chrome rewrite — the flagship, next up.
- Weather widget with forecast (V1 #5).
- Genie option A (band rewrite), mesh-warp genie (V2).
- Restore via animator daemon (user reports no delay; low pri).
- Clipboard rich drag-out polish.
- Keyboard bindings Settings page.

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
