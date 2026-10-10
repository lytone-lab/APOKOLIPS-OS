# APOKOLIPS-OS — AI Handoff

**Read this file first. It replaces reading a long chat history.**
**Then read PROJECT_STATE.txt for the bug list.**
**Then `git log --oneline -20` for the recent commit trail.**

---

## 1. The user and the workflow

You are pair-programming with a solo builder who:

- Is **not a programmer**. Commands must be paste-ready shell blocks.
  No "edit this file in your editor" instructions. No multi-step
  interactive prompts. No "run A, then B, then C, checking each" —
  that's what burns him out.
- Is on **SSH from a host into an Ubuntu guest (VM)**. VS Code with
  Remote-SSH is open, but he mostly pastes into the terminal.
- Wants **facts, not theories**. Ask for a diagnostic command when
  uncertain. Do not speculatively refactor.
- Wants **one patch at a time**. Never send patch B while patch A is
  still building.
- Values **visual verification**. After each patch, list what to look
  for on screen. Screenshots are welcome.

### The canonical patch format

Every code change is **one bash command** that:

1. Kills stuck builds:
   `pkill -9 -f cc1plus; pkill -9 -f gmake; pkill -9 -f "cmake --build"`
2. Runs a Python heredoc that:
   - reads `main.cpp`
   - does one `src.count(old) == 1` check per replacement, aborts on
     mismatch (never silent partial apply)
   - writes `main.cpp.pre-<topic>.<YYYYMMDD-HHMMSS>` backup first
   - writes modified source
   - runs `cmake --build build`, prints `error:` lines on failure
   - on success: `systemctl --user restart apokolips-shell.service`
   - prints is-active
3. Prints a numbered test plan at the end.

### Rules learned the hard way

- **Never start a second build while the first is running.** Two
  builds on the same tree fight each other. The pkill header prevents
  this.
- **Always back up** to `main.cpp.pre-<topic>.<timestamp>` before
  writing. Roll back on failure.
- **`main.cpp` is one 22k-line file.** Rebuilds are 2–5 min in Release.
  Warn the user when a patch will take a while.
- **Release build is the default.** If you see Debug in
  `build/CMakeCache.txt`, reconfigure with
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`.
- **Backups are gitignored.** User can `rm main.cpp.pre-*` after a
  stable commit.

---

## 2. Project overview

**APOKOLIPS-OS** is a custom desktop environment — macOS UI + Windows
compatibility hybrid — built on sway/Wayland + Qt6. One big `main.cpp`
(no split yet) plus `system/` sway config and assets.

**Stack:**

- sway (Wayland compositor)
- Qt6 Widgets (all UI)
- QTermWidget (Node terminal)
- wl-clipboard, grim, swaymsg
- systemd user service `apokolips-shell.service`

**One binary, many modes.** Dispatches on `argv[1]`:

| Flag | Role |
|---|---|
| (none) | The shell — top bar, dock, launcher, spotlight, desktop |
| `--settings` | Settings |
| `--files` | Finder |
| `--notes` | Notes |
| `--calc` | Calculator |
| `--image <path>` | Aperture (image viewer) |
| `--boomtube` | BoomTube (media) |
| `--mymach` | myMach (system monitor) |
| `--node` | Node (terminal) |
| `--genie <png> x y w h dx dy` | Genie minimize overlay |
| `--scale <png> x y w h dx dy` | Scale minimize overlay |
| `--suck <png> x y w h dx dy` | Suck minimize overlay |
| `--glyph`/`--snap`/`--screensaver`/`--clipboard` | misc overlays |

**Config** in `~/.config/apokolips/`:

- `visual.json` — every visual knob (opacity, blur, dock position,
  minimize style, genie duration/strips/curve, fonts, keyboard, etc.)
- `style.json` — Glass / Neumorphism / Skeuomorphism / Clay

**State** in `/tmp`:

- `apokolips-genie-state.json` — `{"windows":[{conId, png, ...}]}`.
  Written on minimize, read on dock click to restore. Pruned every 5 s
  by `pruneStaleGenieState()`. Wiped on shell startup.

---

## 3. Build / deploy

    cd ~/apokolips-shell
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # once if Debug
    cmake --build build -j2                           # 2-5 min
    systemctl --user restart apokolips-shell.service
    systemctl --user is-active apokolips-shell.service

Watch a background build:

    tail -3 /tmp/build.log
    pgrep -c cc1plus         # 0 = done, 1 = still compiling

Rollback:

    ls -t main.cpp.pre-* | head -3
    cp main.cpp.pre-<topic>.<ts> main.cpp
    cmake --build build
    systemctl --user restart apokolips-shell.service

---

## 4. Current state (2026-10-11)

**Working:**

- Shell chrome: top bar, dock (Bottom/Left/Right), launcher,
  spotlight, control center, notification center
- Traffic lights on every app: red = close with genie, yellow =
  minimize with genie, green = zoom toggle
- Minimize animation: genie / scale / suck, selectable in
  Settings -> Appearance
- Reverse restore: dock click on a minimized app plays reverse
  animation back to original position (state file + scratchpad)
- Dock position changes are live and don't freeze
- Settings has ~20 pages, all wired to real config
- Wallpaper engine: animated wallpapers + image/GIF
- Single-instance: opening a running app focuses it
- Release build (2 MB binary, ~200 ms overlay spawn)
- Finder opacity slider updates live

**Known issues / next items:**

- **Strip seams visible** in genie animation. Interim fix: draw each
  strip 1 px wider (overlap). Real fix: mesh warp (queued as Genie
  option A).
- **Super+Shift+R sway bind** doesn't fire. Config line needs fixing.
- **Keyboard bindings** — Settings page for rebinding shortcuts.
  Reverted experiment needs a redesign.
- Genie option A rewrite: 4-6 bands, or full mesh for zero seams.
- Terminal (foot) decorations: no title bar.
- Customisation layer — Windhawk/Rainmeter-class knobs in Settings.

---

## 5. The genie family

1. Shell captures focused window via `grim -g "<x,y> <w> <h>"`.
2. Shell finds the dock tile for the app, computes its center.
3. Shell spawns `apokolips-shell --genie <png> x y w h dx dy
   --ready-sock <name>`.
4. Overlay boots Qt, paints warped pixmap shrinking toward (dx,dy).
5. On first paint, overlay pings the `--ready-sock` server. Shell
   then moves the real window to sway scratchpad.
6. State written to `/tmp/apokolips-genie-state.json`.

Reverse: dock click reads state, spawns overlay with `--reverse`,
`swaymsg [con_id=N] scratchpad show`.

Variants:

- `--scale` — uniform shrink
- `--suck` — perspective warp (QTransform::quadToQuad)
- `--genie` — strip-warp (default); queued for band rewrite

**Critical timing:** shell hides the real window ONLY on the overlay's
`--ready-sock` ping, which fires from the overlay's first `paintEvent`
+ 25 ms. Any earlier signal causes a visible flash.

---

## 6. Sway integration

- Socket: `$XDG_RUNTIME_DIR/sway-ipc.<uid>.<pid>.sock`.
  Helper `discoverSwaySock()` in main.cpp.
- Criteria: `[app_id=...]`, `[title=...]`, `[con_id=N]`, `[pid=N]`.
- Our apps set `setDesktopFileName("apokolips-<name>")` so sway sees
  `app_id=apokolips-<name>`. Never match on `apokolips-shell` for our
  own apps — the shell process uses that.
- Sway binds:
  - `$mod+m` -> `pkill -RTMIN apokolips-shell` (minimize focused)
  - `$mod+Shift+S` -> Glyph
  - `$mod+Z` -> snap
  - `$mod+V` -> clipboard
  - `$mod+Shift+R` -> (broken)
  - `Ctrl+Shift+R` in shell window -> reload (works)

---

## 7. Sigil — the design system

`namespace Sigil` inside main.cpp. Every visual surface pulls colors:
`Sigil::contentBg()`, `Sigil::headerBg()`, `Sigil::sidebarBg()`,
`Sigil::border()`, `Sigil::accent()`.

Underneath, Sigil reads from `ThemeManager::instance().current()` set
by the active Wallpaper. Change wallpaper -> theme changes -> every
Sigil-aware surface recolors.

**New window class checklist:** use `Sigil::*` for colors, subscribe
to ThemeManager, use
`QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5)` for the rounded body,
and add 16 px bottom margin to the root layout (that margin prevents
the rounded-corner fringe).

---

## 8. Settings pattern

Settings is a QStackedWidget with a sidebar. Every page:

- Lives in `SettingsWindow::buildUi()` or `ensurePageBuilt(idx)`.
- Reads `VisualConfigManager::instance().cfg()`.
- Writes via `setAndSave([&](VisualConfig &c){ c.field = value; })`.
- The shell polls `visual.json` mtime and reloads on change. Apps in
  separate processes need their own QFileSystemWatcher — see
  FileExplorer.

To add a setting:

1. Add field to `struct VisualConfig`.
2. Add load/save lines in `VisualConfigManager::load()` and `save()`.
3. Add a control to the Settings page.
4. Subscribe consumers (in-shell: subscribe(); out-of-process: file
   watcher).

---

## 9. Common diagnostics

    cd ~/apokolips-shell
    grep -n "<something>" main.cpp | head
    L=$(grep -n 'class X' main.cpp | cut -d: -f1); sed -n "${L},$((L+80))p" main.cpp
    awk '/class X/,/^};/' main.cpp | grep -n '<pattern>'
    journalctl --user -u apokolips-shell.service -n 100 --no-pager

Ask for **one** diagnostic at a time. Never send a patch with a
guessed anchor.

---

## 10. Immediate next steps

1. Keyboard bindings — Settings page that writes `swaymsg bindsym`.
2. Restore path via animator daemon — minimize already uses it,
   restore still spawns a fresh process (~200 ms).
3. Genie quadToQuad rewrite — borrow Suck's technique, keep the
   genie silhouette. Eliminates strips in genie entirely.
4. Terminal (foot) decorations — sway-side title bar.
5. Customisation layer — Windhawk/Rainmeter-class knobs in Settings.
6. Settings polish — wallpaper solid color + local file; display
   refresh/scale/Night Shift; sound input + per-app volume.

---

## 11. What the user wants

Not clean code. Not refactors. **A working macOS-plus-Windows desktop,
feature by feature, verified visually.**

If something breaks, roll back and try again. If a feature is done,
commit it. If a bug is fixed, tick it off in PROJECT_STATE.txt.

Never let him sit on a broken build. Never claim something works
without seeing the output. Be terse. Be exact. Ship patches.
