# APOKOLIPS-OS — Current State

**Rewritten each session. Paste this file's raw URL into a fresh AI
chat to bring it up to speed instantly.**

Raw URL:
`https://raw.githubusercontent.com/lytone-lab/APOKOLIPS-OS/main/CURRENT_STATE.md`

Also read first:
- `AI_HANDOFF.md` — the general onboarding doc
- `TEAM_PROTOCOL.md` — how this project is worked on
- `ARCHITECTURE_NOTES.md` — design critiques + roadmap
- `PROJECT_STATE.txt` — the bug list + feature backlog

---

## Date of this snapshot

2026-10-12, ~05:33 EAT

---

## Repository

`github.com/lytone-lab/APOKOLIPS-OS` — main branch.

Ubuntu VM (`hostname apokolips`) accessed via SSH port 2222 from a
Windows 11 host. Sway/Wayland + Qt6. systemd user service
`apokolips-shell.service`.

**Build config:** currently **Debug** build. Release OOMs the 3 GB VM
even with swap. Must build with `-j1`:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build -j1

When the VM is next shut down: raise base memory from 3 GB to 5 GB in
VirtualBox Manager → Settings → System. Then Release builds stop
OOMing.

---

## Canonical patch format

Every code change is one bash block that:

1. `pkill -9 -f cc1plus; pkill -9 -f gmake; pkill -9 -f "cmake --build"; pkill -9 -f animator-daemon`
2. Python heredoc that:
   - reads the target file
   - does `src.count(old) == 1` check per replacement, aborts on
     mismatch
   - writes `<file>.pre-<topic>.<YYYYMMDD-HHMMSS>` backup first
   - writes the modified source
   - runs `cmake --build build -j1`
   - prints `error:` lines on failure, exits non-zero
   - on success: `systemctl --user restart apokolips-shell.service`
   - prints is-active

**Critical:** the animator daemon caches overlay code. Any patch
touching `overlays.h` must `pkill -f animator-daemon` before the shell
restart. The daemon respawns via a 5-second timer in the shell.

---

## File layout (post-split)

    shell.h            shared include block
    shellapp.h         ShellApp base class (all 7 apps inherit)
    theme.h            Theme, ThemeManager, SysStats
    visualconfig.h     VisualConfig + VisualConfigManager
    style.h            VisualStyle, StyleManager, StyleRenderer, Sigil
    wallpaper.h        Wallpaper base + subclasses
    overlays.h         Genie/Scale/Suck + animator daemon
    registry.h         Icons, AppEntry, appRegistry, ShellRegistry, BadgeRegistry
    weather.h          Weather:: namespace
    globals.h          signal toggles + sway globals
    shared_fwd.h       forward decls for helpers still in main.cpp
    helpers.h          WallpaperConfig, ApokolipsInputDialog, QuickLook,
                       FinderPlaceRow, SysControl, MouseSpy
    notes.h, calc.h, finder.h, aperture.h, node.h, mymach.h, boomtube.h
    settings.h
    chrome.h           TopBar, Dock, DockIcon, AppLauncher, Spotlight,
                       MissionControl, ControlCenter, NotificationCenter,
                       ClipboardPanel, DesktopIcon, DesktopCanvas,
                       DesktopWidgets, DesktopBackground, TrayIcon,
                       BatteryPercentLabel
    main.cpp           runShell + main() dispatch (~3.5k lines)

---

## Design system — Sigil

`namespace Sigil` in `style.h`. Every surface pulls from it. Palette:
graphite/bone with oxblood accent (`#a83250` family). Underlying theme
is driven by the wallpaper via ThemeManager.

App icon rendering: `Icons::drawFor(name, painter, rect, color)` in
`registry.h`. Each app has a `drawXxx` function that calls
`appTile(p, r, topColor, bottomColor)` — a colored gradient with
macOS-style **carve** (dark inner stroke) + **rim** (bright top band).
Every app now has its own color:

| App       | Color       |
|-----------|-------------|
| Finder    | Slate blue  |
| Notes     | Warm amber  |
| Settings  | Graphite    |
| Aperture  | Indigo      |
| BoomTube  | Deep red    |
| myMach    | Forest green|
| Node      | Near-black  |
| Launchpad | Dark grey   |

Chip at same bounds/radius as icon (0.22 radius), invisible at rest,
soft radial halo on hover (not a hard outline).

---

## Terminal unification

**Node** (our QTermWidget app) is the standard terminal. **Foot** is a
fallback recovery terminal.

- `Super+Return` → Node
- `Super+Shift+Return` → foot

Every "launch terminal" site in the codebase uses
`apokolips-shell --node --run "<cmd>"`. `--run` skips single-instance
detection and types the command into a fresh window after 500ms.

---

## Traffic lights — dev-only escape hatches

Currently in the shell's own top bar for dev convenience. Remove when
the OS ships.

- Red: `shellCloseViaGenie(this)` + `QTimer::singleShot(900, QCoreApplication::quit)` (added this session — quits the process after the animation, so the running dot goes off)
- Yellow: `shellMinimizeFocusedWindow()` (SIGRTMIN to shell → minimize pipeline)
- Green: toggle between preferred size and `shellSafeArea()` — **not animated, this is the G5 gap**

---

## Apokolips system menu

Top bar has an **omega (Ω)** button on the far left. Click opens a
custom `QMenu`:

- About This Apokolips OS
- System Settings
- Recent Items (stub — awaiting shared-protocols work)
- Quick Actions → Screenshot / DND / Wallpaper
- Window → Force Quit / Mission Control / Reload Shell
- Lock / Sleep / Restart / Shut Down
- Log Out

Menu uses `menuStyle(theme)` helper at `main.cpp:949`.

---

## Dock + desktop icons

Chip model same as app icons (macOS style). Desktop icons get a
slightly larger chip than dock tiles. Hover halo on both.

Dots under running apps are **meant** to indicate "has a window in
sway tree" (not "process is alive"). That logic was replaced in
`chrome.h` around line 1252 with a `swaymsg -t get_tree` query — but
the runtime check from outside the shell shows the tree is correct
while the shell-side result is unverified.

---

## Genie animation — current state

`GenieOverlay::paintEvent` in `overlays.h`. Current model:

- **Tip corner** = nearest bottom corner to dock tile
- **24 horizontal strips**, each warped via `QTransform::quadToQuad`
  (no visible seams because strips share boundaries)
- **Per-strip fold-then-fly:** fold from source position to `tipSrc`
  over first half of the local timeline, then fly `tipSrc → dock`
  along an S-curve over the second half
- **S-curve:** single primary harmonic with
  `sAmp = min(tipLen * 0.38, 140)`, plus a 25% second harmonic
- **`genieSCount`** (1..5) in `visualConfig` — user-configurable
  S-wiggle count
- **`genieDurationMs`** default around 480 ms
- Restore uses same path with `m_reverse = true`

---

## Open bugs

1. **Dots on reload** — last patch replaced `pollRunning` with a
   sway-tree query in `chrome.h:1252`+ but the failure is inside the
   shell's call. Next step: add a `qInfo()` of the tree length right
   after `readAllStandardOutput()` and see what the shell actually
   receives.

2. **Genie animation "goes into desktop"** — Manus's critical check:
   the animation endpoint should be the **specific app's dock icon**,
   not generic dock center. The path also crosses desktop widgets.
   Two separate issues:
   - endpoint wrong / not landing on the icon
   - path crosses widget area
   Manus priority #2 is right: **trace the full chain** — minimized
   window → app identity → dock icon label → icon center in global
   coordinates → screen origin → final overlay target. Log each with
   `qInfo()`. Add guards that **reject** missing/stale targets
   rather than falling back to a generic dock point.

3. **Dock position change → no desktop reflow** — Left dock overlaps
   clock/weather widgets; right dock crowds desktop icons. Manus
   priority #1: shared `dockRect()` helper every consumer reads,
   recompute on position change.

4. **Finder folder-nav flicker + dark "back" panel during opening**
   — Manus priority #4: keep old list visible while loading, swap
   atomically.

5. **Blur slider stutter** — Manus priority #5: don't recompute
   full-screen blur every tick; debounce + cache.

6. **Reload Shell black frame** — Manus priority #6: keep stable
   desktop surface or show a deliberate loading state during shell
   replace.

7. **Network "Querying…" 3 s block** — Manus priority #7: async +
   cached results + timeout feedback.

8. **Rounded corners go dark during animation frames** — Manus
   priority #3: investigate whether the captured pixmap bakes square
   corners; keep rounded alpha clip on every frame.

9. **Finder background is alpha, not blur** — correct observation.
   Wallpaper shows through but not frosted.

---

## What to do first

**Option A (recommended):** Add the trace to the minimize chain
(Manus priority #2). Highest-value change — gives us the data we've
been missing and converts silent wrong animations into logged
reasons. Small patch to `main.cpp` minimize handler.

**Option B:** Fix the dots-on-reload bug first. One log line to see
what the shell actually receives.

**Option C:** Fix dock position → desktop reflow (Manus priority #1).
Bigger patch, shared `dockRect()` helper.

Don't stack patches. One at a time. Build (Debug, -j1), test, commit.

---

## Commit message conventions

Recent commits:

- `Split main.cpp: 22k -> 3.5k lines across 21 headers`
- `ShellApp base class + Notes migration`
- `Terminal unification: Node replaces foot; --run flag; Sigil sway chrome`
- `Red light quits process; restore works; dot state from sway tree`

Commit + push after every successful fix:

    git add -A && git commit -m "..." && git push

---

## The user

Solo builder. Not a programmer. Wants paste-ready commands, no
theories without evidence. Gets frustrated when patches fail silently.
Wants one patch at a time. Wants visual verification after each. Does
**not** want long plans — wants code, then expectations, then result.

**How to deliver a patch:**

1. One bash block — copy-paste, no editing
2. Backup first
3. Abort on any anchor mismatch
4. Build with `-j1`, Debug
5. Report `OK:` per replacement
6. Restart the service, print is-active
7. List what to look for on screen
8. Wait for the user to test and paste results

Never send patch B while patch A is still building. The pkill header
prevents the OOM/eaten-compiler race.
