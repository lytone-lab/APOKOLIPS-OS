# ARCHITECTURE_NOTES.md — Design critiques + opportunistic roadmap

Not a bug list. A design review. Each item is either:
  [NOW]     — small, do it while you're already in that file
  [SOON]    — one focused session
  [EVENTUAL]— bigger structural work, needs the right moment
  [PHILO]   — a decision, not a patch. Only you can make it.

---

## [NOW] Bake the defaults

You tuned genieDurationMs, minimizeStyle, dock position, icon size
over many sessions — but a fresh install still gets the original
defaults (genieDurationMs = 480, style = Glass). Every value you
picked is now *your* taste. Bake it in so a new user gets the
finished feel, not the prototype feel.

Fields to update in `struct VisualConfig` defaults:
  - genieDurationMs -> what you landed on (240?)
  - minimizeStyle   -> what you landed on (suck?)
  - genieStrips     -> 80
  - dockPosition    -> keep Bottom unless you've switched
  - dockIconSize    -> keep unless tuned

One-line changes per field. Ship anytime.

---

## [NOW] A "What's new / Take the tour" panel

You have ~20 Settings pages and no way for a user to learn:
  - Right-click dock to add an app
  - Super+M minimize / Super+Shift+R reload
  - Yellow light minimizes, red light animated-closes
  - Three-finger swipe = whatever Mission Control is bound to
  - Drag window to screen edge to snap

macOS gets away with this because of 40 years of muscle memory.
You don't. A single modal with 6-8 lines of "did you know..."
shown once, dismissible, would fix 80% of discoverability.

Place it: first launch after install. Store "seen" flag in
visual.json. No bigger than a QDialog with a QLabel and a Close.

---

## [SOON] Restore path via animator daemon

Minimize goes through the daemon (~10-20 ms). Restore still
spawns a fresh process (~200 ms). Same pattern applies. Small
follow-up.

---

## [SOON] Terminal (foot) decorations

foot windows have no title bar. Every other app has Sigil
chrome. This is the one place the OS looks inconsistent on
first launch. Sway-side: `for_window [app_id="foot"]` rules
or launch foot with `--app-id` + sway's `client_decoration`.
No Qt code needed.

---

## [EVENTUAL] Split main.cpp

This is the biggest velocity drag. 22k lines, 900 KB. Every
patch is a search-and-replace against a huge file; rebuilds
are 2-5 min; anchors miss; the git history is unreadable.

Suggested split (each gets its own .cpp/.h):
  theme.{h,cpp}          — Theme, ThemeManager, Sigil, Wallpaper
  visualconfig.{h,cpp}   — VisualConfig, VisualConfigManager
  overlays.{h,cpp}       — Genie/Scale/Suck, animator daemon
  chrome.{h,cpp}         — TopBar, Dock, DockIcon, AppLauncher
  settings.{h,cpp}       — SettingsWindow + all pages
  apps/*.cpp             — one file per app
  main.cpp               — dispatch only

Rebuild time drops to <30 s per change. Patch anchors become
meaningful. Doable in 2-3 focused sessions with the backup +
incremental-compile pattern. Risky, so schedule it around a
natural pause in feature work, not mid-bug-hunt.

---

## [EVENTUAL] Shared behaviors across apps

Right now every app re-implements its own chrome, keyboard
handling, close/min/zoom. Ctrl+W works in some, not others.
Ctrl+Q maybe quits. Super+M is global (fine). No app has
consistent drag-anywhere-to-move.

Fix: one `ShellApp` base class that:
  - Sets up the frameless window + 16 px bottom margin
  - Installs the traffic lights with the standard behavior
  - Binds Ctrl+W / Ctrl+Q / Super+M
  - Provides `applySigilPaint()`, `restyleAll()` hooks
  - Auto-subscribes to ThemeManager + VisualConfigManager

Every app becomes ~200 lines shorter. And they *feel* like one
OS because they literally share behavior.

---

## [EVENTUAL] Shared app-to-app protocols

macOS feels like a system because every app inherits Services,
Share Sheet, Quick Look, Drag & Drop, Spotlight indexing,
Focus modes. You have none of these yet.

Smallest viable versions:
  - "Open with" registry (Core Matrix already exists — extend it
    to be callable from any app)
  - Drag-drop between apps (Finder -> Notes, Finder -> Aperture)
  - A single "recent files" store that all apps read/write
  - A shell-level clipboard history panel

Any one of these is a session's work but the *feeling* it adds
is disproportionate.

---

## [PHILO] The four UI styles

Glass / Neumorphism / Skeuomorphism / Claymorphism. Four styles
means no style. Every OS the user has ever loved has *one*
recognizable look. Pick the one you'd defend in a review, ship
it as default, and hide the other three under a "Developer"
toggle in Settings.

Not urgent. But at some point the project's identity has to
crystallize around one aesthetic.

---

## [PHILO] Windhawk-vision vs. Opinion

Original vision: Windhawk + Rainmeter class customisability,
one-click sliders, no plugin downloads.

The ambivalence: Windhawk and Rainmeter exist *because* Windows
doesn't ship these features. If you add them all as knobs, you
inherit the same problem Windows has — no opinion, just a
config panel.

Two paths:
  A. Ship every knob (Windhawk vision, fully)
  B. Ship the choices you'd defend, expose overrides underneath

Real answer is probably B for the primary surfaces (dock, top
bar, animations) and A for deeper stuff (cursor size, keyboard
repeat, fonts). But the project should *know* which it is.

---

## What I would fix first, if it were my project

1. [NOW] Bake the defaults               — 10 min
2. [NOW] What's new panel                — 1 session
3. [SOON] Restore via daemon             — 30 min
4. [SOON] Terminal decorations           — 30 min, sway-side
5. [EVENTUAL] Shared ShellApp base class — 2 sessions
6. [EVENTUAL] Split main.cpp             — 2-3 sessions, one time
7. [PHILO]  Pick the one style.          — a decision, not code

Everything else is a queue, not a deadline.

---

## How this file gets used

When you're about to work on something, glance at this file. If
a [NOW] item is adjacent to what you're already touching — do
it while you're there. [SOON] items are worth their own slot.
[EVENTUAL] items need a natural pause and a cleared schedule.
[PHILO] items need you to decide, then write the decision down
in AI_HANDOFF.md so future sessions inherit it.

The point isn't to clear this list. It's to keep the project
from collapsing into the same "just a lot of apps" that every
Linux desktop has been for 25 years.
