# APOKOLIPS-OS

A custom desktop environment — macOS polish, Windows compatibility, Linux
foundation.

Not a Linux distribution. Not a kernel. **A desktop shell** — the layer
that turns a plain sway/Wayland session into a coherent, opinionated
user experience with its own design language, apps, and behaviour.

If you want the short version: this is what GNOME Shell, KDE Plasma,
and Deepin DE are — implemented in Qt6 C++ on top of sway.

---

## What it is

- **A compositor-driven shell.** sway runs Wayland. APOKOLIPS paints the
  top bar, dock, launcher, spotlight, control center, notification
  center, and desktop on top of it.
- **A suite of apps.** Finder, Notes, Calculator, Aperture (image viewer),
  BoomTube (media player), myMach (system monitor), Node (terminal),
  Settings — every one frameless, every one obeying the same design
  system.
- **A design system.** Sigil. One palette, every surface. Change the
  wallpaper, the whole shell recolours.
- **A window manager's client.** All apps have macOS-style traffic
  lights, minimise with genie animation, restore with reverse genie.

## What it is not

- Not a kernel. Linux does that.
- Not a distribution. No package manager, no installer ISO.
- Not a window manager. sway does that.
- Not done. Ever. But it works today.

---

## Architecture

    sway (Wayland compositor)
      └── apokolips-shell       (the whole shell + every app)
            ├── top bar, dock, launcher, spotlight, control center
            ├── apps (Finder, Notes, Calculator, BoomTube, …)
            ├── settings window + ~20 pages
            └── animations (genie / scale / suck) + resident daemon

**One binary, many modes.** `apokolips-shell` dispatches on `argv[1]`:

| Flag | Role |
|---|---|
| (none) | The shell — top bar, dock, launcher, desktop |
| `--settings` | Settings app |
| `--files` | Finder |
| `--notes` | Notes |
| `--calc` | Calculator |
| `--image <path>` | Aperture (image viewer) |
| `--boomtube` | BoomTube |
| `--mymach` | myMach |
| `--node` | Node (terminal) |
| `--genie` / `--scale` / `--suck <png> …` | Minimise overlay |
| `--animator-daemon` | Resident animation server |

## Language

C++17 with Qt6 Widgets. The same stack KDE Plasma uses. C++ for
everything — kernel work, drivers, and the desktop layer, exactly what
GNOME, KDE, and Deepin do.

**No Python, no C#, no Electron in the shipped binary.** Python is only
used during development as a scripting tool for patching the source.

## Build

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j2
    systemctl --user restart apokolips-shell.service

## Run

    systemctl --user start apokolips-shell.service

or, for development:

    ./build/apokolips-shell                # the shell
    ./build/apokolips-shell --notes        # just Notes
    ./build/apokolips-shell --settings     # just Settings

## File layout

    main.cpp        — dispatch + everything not yet split
    shell.h         — shared include block
    theme.h         — Theme, ThemeManager, SysStats
    visualconfig.h  — VisualConfig, VisualConfigManager
    wallpaper.h     — Wallpaper base + all subclasses
    style.h         — VisualStyle, StyleManager, StyleRenderer, Sigil
    overlays.h      — Genie/Scale/Suck overlays + animator daemon
    notes.h         — NotesApp

    system/         — sway config, session scripts
    run.sh, run-session.sh, bootstrap.sh

_(main.cpp is being incrementally split. Each header above is a
working step in that process.)_

## Design system

**Sigil** is the palette. Every window, panel, and surface pulls its
colours from `Sigil::*`. Underneath, Sigil reads from `ThemeManager`,
which is driven by the active wallpaper. One theme change → every
surface follows.

Four visual styles are available:
Glass, Neumorphism, Skeuomorphism, Claymorphism.

## Configuration

    ~/.config/apokolips/visual.json   — every visual knob
    ~/.config/apokolips/style.json    — UI style

Runtime state (not settings):

    /tmp/apokolips-genie-state.json   — minimised windows
    /tmp/apokolips-animator           — daemon socket

## Philosophy

    macOS look. Windows compatibility. Linux freedom.
    Everything in Settings. Nothing hidden.

## Status

Under active development. Every feature is checked visually after it
lands. Everything can roll back.

See `AI_HANDOFF.md` for the current state and next steps.
See `TEAM_PROTOCOL.md` for how the project is worked on.
See `ARCHITECTURE_NOTES.md` for the design critique and roadmap.

## License

GPL v3 — see LICENSE.

Built by **lytone**. If you fork this or ship it, keep the credit.
