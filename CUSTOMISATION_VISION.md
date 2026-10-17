# APOKOLIPS-OS — Customisation Vision

------------------------------------------------------------------------
SESSION LOG — 2026-10-16
------------------------------------------------------------------------
V1 ship-order progress:

  [x] #1 — System monitors as desktop widgets (CPU/RAM/DISK gauges,
      StatsCard hybrid, clock, calendar, weather). Shipped earlier;
      framework in widgets.h (WidgetCard base + subclasses).
  [x] #2 — Launcher theming. Columns, blur, tint, 3x3 position
      anchor, live preview. Settings → Launcher & Spotlight.
  [x] #3 — Now Playing widget on desktop (BoomTube). State via
      /tmp/apokolips-boombube-state.json, controls via
      /tmp/apokolips-boombube-cmd.json. Prev/play/next buttons.
      Live visibility toggle.
  [ ] #4 — Per-app chrome rewrite. NEXT. The flagship — nobody
      else has this.
  [ ] #5 — Weather widget with forecast (data already in top bar).

P1 customisation breadth (40 knobs / 12 categories): not started.
Platform note: no audio output on the VM (sound work deferred to
ISO). Settings → Widgets page now hosts per-widget toggles.

------------------------------------------------------------------------


**What this is:** the full plan for what will eventually be the
"Windhawk-class" customisation layer — every visual knob the user
can turn, every automation they can build, every mod they can
install and share. Nothing hidden behind config files. Every
customisation goes through Settings, is live, and reverts cleanly.

> **V1 ONLY.** Default state is V1. All P2/P3 items below are
> parked — not mentioned, not diagnosed, not built — unless the user
> names one explicitly. See TEAM_PROTOCOL.md Rule #2.

> **DEFERRED INDEFINITELY (2026-10-16):** Auto-themes from
> wallpapers (Elaboration 3, P3). Reason logged in PROJECT_STATE.txt
> under "SUGGESTION — Auto-themes from wallpapers". Do not propose.

Real Windhawk = community-shared Windows UI mods. This is that
concept, native to our own OS, with a visual rules engine and
theme marketplace on top.

Phases are tagged P1 / P2 / P3.
  P1 = first shipped version of the customisation layer.
  P2 = second pass, adds depth and community features.
  P3 = long-term; may need new infrastructure.

------------------------------------------------------------------------
FEATURE CATALOGUE (phase-tagged)
------------------------------------------------------------------------

1. WINDOW CHROME
   [P1] Title bar height (px slider)
   [P1] Corner radius (0–24 px)
   [P1] Shadow depth (soft → dramatic)
   [P1] Traffic light size, spacing, hover glyphs on/off
   [P2] Blur behind chrome (amount slider)
   [P2] Frameless vs thin border toggle
   [P3] Per-app chrome rewrite (see Elaboration 4)

2. TOP BAR
   [P1] Height (24–40 px)
   [P1] Blur / opacity / solid
   [P1] Per-widget visibility (clock, CPU, RAM, weather, battery,
        network, seal icon)
   [P1] Icon size
   [P2] Click actions per widget (left / right)

3. DOCK
   [P1] Position (bottom / left / right)
   [P1] Icon size + padding
   [P1] Magnification curve strength
   [P1] Running-dot style (dot / line / glow)
   [P2] Auto-hide on/off + delay
   [P2] Hover preview on/off (window thumbnails)

4. LAUNCHER
   [P1] Grid columns (4–8)
   [P1] Icon size
   [P1] Featured row on/off
   [P2] Categories on/off
   [P2] Search bar on/off

5. DESKTOP WIDGETS
   [P1] Which widgets are shown (clock, calendar, CPU, RAM,
        weather, sticky notes, media, battery)
   [P1] Card style (glass / solid / outline)
   [P2] Position of each (drag-to-place)
   [P2] Card size per widget
   [P2] Auto-hide when a window is maximised

6. ANIMATIONS
   [P1] Window open/close duration
   [P1] Minimise style (genie / scale / suck / thanos)
   [P1] Minimise duration + curve
   [P1] Cursor glow style (glow / arrow / off)
   [P1] Menu fade duration
   [P2] Launcher spring strength
   [P1] Global "reduce motion" toggle

7. MENUS
   [P1] Corner radius
   [P1] Blur / opacity
   [P1] Padding / spacing
   [P1] Item height
   [P2] Highlight sweep on/off

8. NOTIFICATIONS
   [P1] Position (top-right / top-left / bottom-right)
   [P1] Duration default
   [P1] Card style (glass / solid)
   [P1] History depth
   [P2] Auto-dismiss on/off
   [P2] Sound per app

9. SOUNDS
   [P1] Startup chime
   [P1] Lock / unlock
   [P1] Error
   [P1] Notification
   [P1] Minimise / maximise
   [P2] Per-app sounds

10. ACCENT & THEME
    [P1] Seal colour (7 presets)
    [P1] Wallpaper → theme auto-follow on/off
    [P1] Light / dark / auto
    [P2] Custom seal (colour picker)
    [P2] Per-app theme overrides
    [P3] Theme marketplace (Elaboration 2)
    [P3] Auto-generated themes from wallpaper (Elaboration 3)

11. CURSOR
    [P1] Glow on/off, style, brightness, radius, trail, softness, fade
    [P1] Pointer size
    [P2] Pointer style (default / macOS-style)

12. FONTS
    [P1] System font
    [P1] Monospace font
    [P1] Base size
    [P2] Headline font
    [P2] Letter spacing (shell only)

13. KEYBOARD SHORTCUTS
    [P1] Visual keybinding editor
    [P1] Every shortcut rebindable
    [P2] Import / export

14. MODS (the Windhawk part)
    [P2] Mod system — a mod is a folder with a manifest + rules
    [P2] Install / enable / disable a mod with one click
    [P2] See what a mod does before installing (diff preview)
    [P3] Mod marketplace — browse, rate, install from the web
    [P3] Mod authoring tool — build a mod from your current setup

15. RULES ENGINE (see Elaboration 1)
    [P2] Basic rules: "when X, do Y" — a text list
    [P3] Drag-and-drop visual builder
    [P3] Conditions: app focused, battery, time, wallpaper, dock
         position, presence of external monitor
    [P3] Actions: switch theme, disable blur, hide top bar, mute
         notifications, run command, apply dock position

------------------------------------------------------------------------
DETAILED ELABORATIONS
------------------------------------------------------------------------

## Elaboration 1 — Rules engine with drag-and-drop builder

**What it looks like:**
A new Settings page called "Automation". Two columns:
left = a library of Conditions and Actions as draggable cards,
right = the workspace where you assemble them.

The top of the workspace is a big "WHEN" canvas with one slot.
Below it, an "AND" section that can hold N extra conditions.
Below that, a "THEN" section that can hold N actions.

You drag "App focused" onto the WHEN slot. It opens a mini-form:
"App = [dropdown of installed apps]". You drag "On battery" onto
the AND row. Then you drag "Switch to dark theme" onto the THEN
row. You name the rule "Late night coding". Click Save.

The rule runs instantly and forever.

**How it works (implementation):**
- Every rule = a JSON object: `{when: [{type, params}], then: [{type, params}]}`
- The shell already polls for events (focused window, config
  changes, etc). Each poll tick evaluates every active rule.
- When a rule's conditions become true, its actions run once.
  When they go false, the shell reverts any actions that were
  marked "revertible".
- No compilation. No mod authoring. Just a JSON list the user
  builds by hand in the UI.

**How it fits the vision:**
This is what turns a desktop into a personal OS. macOS never had
this. Windows never had this. Even Linux tiling users write shell
scripts to fake it. A native, visual, reversible rules engine is
a genuinely new capability. And it feeds back into the mod system
later — a mod is essentially a pre-packaged rule set + asset
bundle.

## Elaboration 2 — Theme marketplace

**What it looks like:**
A new tab in Settings called "Themes". A grid of theme cards.
Each card shows: name, author, small preview image, install count,
rating. Click a card → detailed view with a bigger preview and an
"Install" button.

Installing a theme:
1. Downloads a `.apokolips-theme` file (a zip with a manifest + assets)
2. Unpacks to ~/.local/share/apokolips/themes/<name>/
3. Shows a preview window of what your shell would look like
4. "Apply" button switches. "Uninstall" removes.

You can also publish your own theme: Settings → Themes → "Publish
current setup". It bundles your visual.json, wallpapers, custom
fonts, sounds, cursor, into a single file. You enter a name and
press export. That file goes to the marketplace (or just shared
with a friend as a file).

**How it works (implementation):**
- The theme file is a zip with a strict schema
- The manifest lists which settings it overrides
- On apply, the shell merges the theme over current config
- "Revert" restores the previous settings snapshot
- Marketplace backend is a simple GitHub repo of themes +
  a JSON index. No server needed.

**How it fits the vision:**
Community. Right now, your shell is one person's taste. A
marketplace lets other people add their own taste. And because
everything is declarative (no code execution), it's safe — a
theme can only set config values, not run arbitrary code.

## Elaboration 3 — Auto-generated themes from wallpapers

**What it looks like:**
You set a wallpaper. The shell samples its dominant colours and
proposes a theme:

> **Match your wallpaper?**
> I found 5 colours in this image. Want the shell to use them?
>
> [Preview] [Yes] [No]

Preview shows the whole shell recoloured — top bar, dock, cards,
buttons — using the extracted palette.

**How it works (implementation):**
- Read the wallpaper image, downscale to 64×64
- Run k-means or median-cut colour clustering → 5–8 dominant colours
- Sort by luminance: darkest → lightest
- Assign: darkest → contentBg, mid → cardBg, lightest → text,
  most-saturated → accent
- Ensure WCAG-ish contrast so text stays readable
- Feed the result into ThemeManager::setTheme() as a normal theme

The algorithm is not hard. The tuning is — extracting a palette
that reads well in practice, not just mathematically.

**How it fits the vision:**
The original Sigil concept was that the wallpaper drives the theme.
That's the current default (four built-in wallpapers each ship a
hand-tuned theme). This makes it automatic and universal: any
wallpaper, any user, gets a coherent theme. Removes the biggest
barrier for new users — they don't have to design a theme, the
shell does it for them.

## Elaboration 4 — Per-app chrome rewrite

**What it looks like:**
A new Settings page: "Per-App Appearance". A list of installed
apps on the left. Click an app → overrides on the right:

- Chrome style (system default / flat / glass / solid)
- Traffic lights on the left or right
- Accent colour (inherit / pick one)
- Font (system / pick one)
- Corner radius (system / 0–24)
- Sidebar colour
- Always opens at (preferred size override)

So you could say: "Finder always has a light sidebar, magenta
accent, and rounded corners. Notes is always flat, no traffic
lights, matching the note's colour."

**How it works (implementation):**
- Add `appOverrides` map to visual.json:
  `{ "apokolips-files": { chromeStyle: "flat", accent: "#a83250" }, ... }`
- Each ShellApp subclass reads its own overrides on construct
- Settings page is a simple form that writes to the map
- Per-app overrides always win over global config
- A "Reset this app to defaults" button per app

**How it fits the vision:**
The shell is a system, but apps don't have to look identical. On
macOS, Mail and Calendar have very different chrome — they
weren't forced into one template. Per-app overrides make the
shell respect each app's character while keeping one design
language. It's also the foundation for the theme marketplace —
a theme can bundle per-app overrides so installing a theme
reshapes the whole system coherently.

------------------------------------------------------------------------
WHAT'S IN P1 (first shipped version)
------------------------------------------------------------------------
Every [P1] item above. That's roughly 40 knobs across 12
categories — enough that almost anything a user wants to change
about the shell is visible in Settings. No rules, no marketplace,
no mods yet.

**Estimated: 4–6 sessions** once we start, spread over time.

## WHAT'S IN P2 (second pass)
Rules engine (basic list), mod system, per-app chrome, per-app
theme overrides, custom seal picker, hover previews, keyboard
shortcut import.

**Estimated: 4–6 sessions.**

## WHAT'S IN P3 (long term)
Drag-and-drop rules builder, theme marketplace, auto-themes from
wallpapers, mod marketplace, mod authoring tool.

**Estimated: 6–10 sessions.**

------------------------------------------------------------------------
END OF VISION
------------------------------------------------------------------------

------------------------------------------------------------------------
CUSTOMISATION PRIORITY LIST (added 2026-10-14)
------------------------------------------------------------------------

Extracted from user-count data of Windhawk, Rainmeter, and KDE Plasma.
Ranked by how much users actually love them, then phased by effort.

The strategy is NOT to build every customisation. It is to build the
ones users use most, love most, and that attract new users. Every
other item goes on the pile and waits.

## TIER 1 — I can't live without this

These are the features people install third-party tools specifically
to get.

  #  Feature                                    Source         Phase
  1  Start menu / launcher restyling            Windhawk #1    V1
  2  Taskbar restyling (transparency, etc)      Windhawk #1    V1
  3  Taskbar icon size independent of scale     Windhawk       V1
  4  System monitors as desktop widgets         Rainmeter #1   V1
  5  Taskbar on any edge                        Windhawk       V1 (shipped)
  6  Right-click context menu styling           Windhawk       V1
  7  Volume control per app                     Windhawk       V1
  8  Window rules — open apps how you want      KDE Plasma     V2
  9  Custom global shortcuts for anything       KDE Plasma     V1
  10 Now playing / media controls on desktop    Rainmeter      V1

## TIER 2 — Oh that's nice

  #  Feature                                    Source         Phase
  11 Music visualizer reacting to audio         Rainmeter      V2
  12 Weather widget with forecast               Rainmeter      V1
  13 Calendar widget with events                Rainmeter      V1
  14 Screenshot drag-to-target                  KDE Plasma     V2
  15 Night light / colour temperature           KDE Plasma     V1
  16 Window scroll-to-opacity                   KDE Plasma     V1
  17 Cursor magnifier                           KDE Plasma     V2
  18 QR code from clipboard                     KDE Plasma     V2
  19 Middle-click window to close on taskbar    KDE Plasma     V1
  20 Right-click taskbar entry -> recent files  KDE Plasma     V2

## TIER 3 — Power user flex

  #  Feature                                    Source         Phase
  21 Draw on screen with red lines              KDE Plasma     V2
  22 Wiggle mouse -> cursor gets bigger         KDE Plasma     V1
  23 Sticky note in the panel                   KDE Plasma     V1
  24 Colour picker widget                       KDE Plasma     V2
  25 Always-on-top toggle per window            KDE Plasma     V1
  26 Meta+click to move/resize any window       KDE Plasma     V1
  27 Screen corner peek at desktop              KDE Plasma     V1
  28 Live volume meter in sliders               KDE Plasma     V2
  29 Video playback speed from media ctrl       KDE Plasma     V2
  30 Vertical taskbar layout                    Windhawk       V1

## THE FOUR FLAGSHIP ITEMS (the ones you personally want)

  Item                             Phase
  Rules engine (drag-drop builder) V2 basic -> V3 visual
  Theme marketplace                V3
  Auto-themes from wallpapers      V2
  Per-app chrome rewrite           V2

## SHIP-ORDER FOR V1 (attract new users first)

The five with highest wow-per-hour:

  1. System monitors as widgets — CPU / RAM / GPU / network graphs
     Rainmeter's #1 use case. myMach's data already exists.
     Present as floating desktop cards.
  2. Launcher theming — grid size, blur, icon size
     Windhawk's #1 mod. Launchpad exists. One settings page.
  3. Now playing widget on desktop
     Rainmeter top-3. BoomTube exists. Just a floating widget.
  4. Per-app chrome rewrite — Finder light, Notes flat, etc
     Nobody else has this. macOS doesn't. Windows doesn't.
     Genuinely new category of customisation.
  5. Weather widget with forecast
     Rainmeter top-5. Weather data already in the top bar.

## SCOPE, HONEST NUMBER

  V1: ~15 features × 1-3 sessions = ~25 sessions
  V2: ~10 features × 1-2 sessions = ~20 sessions
  V3: ~5 features  × 2-4 sessions = ~15 sessions

  Total: ~60 sessions ≈ 6 months of evenings.

That is the honest size. Not a weekend, not a month.
The whole point is worth it because nobody has done it.

------------------------------------------------------------------------
END OF PRIORITY LIST
------------------------------------------------------------------------

------------------------------------------------------------------------
WIDGET FRAMEWORK (added 2026-10-15)
------------------------------------------------------------------------

## The system, not just the widgets

Desktop widgets are not a single feature — they are a system. The
system is the WidgetLayer + WidgetCard framework. Individual
widgets (clock, CPU, weather) are plugins on top.

## Architecture — Variant 2 (chosen)

  DesktopRoot
    └── WidgetLayer  (fullscreen, transparent, ignores empty mouse)
          ├── WidgetCard(Clock)     x=100  y=200
          ├── WidgetCard(CPU)       x=100  y=400
          ├── WidgetCard(Weather)   x=1400 y=200
          └── ...

Why this and not top-level windows:
  A previous attempt using per-widget top-level windows triggered
  a screen-wide zoom (top bar + dock + wallpaper all rescaled).
  Root cause unknown, but structural — top-level windows fight
  the shell's own layout. Variant 2 keeps every card inside the
  shell widget tree. The bug's cause is gone by construction.

## Card anatomy

  - Frosted dark background (blur of wallpaper behind)
  - Corner radius ~20px
  - Padding ~16px
  - Optional header with icon + title + gear button
  - Content area — widget decides layout
  - Right-click menu: Remove / Size S-M-L / Change / Configure…
  - Drag anywhere on the card moves it. Position saved to
    visual.json under `widgetPositions[<id>]`.
  - Snap-to-grid optional (Settings toggle).

## Visual language (per user's reference images)

  - Circular progress rings for stats — green fill for healthy,
    amber for warning, red for critical
  - Big number + small unit underneath
  - Icon inside the ring (top area)
  - Weather: big temp + condition + 5-day icon row
  - Calendar: month grid, today highlighted with accent dot
  - Screen time: bar chart + per-app breakdown list
  - Small gear badge bottom-right opens per-widget settings

## Settings access — two paths, not redundant

  - Right-click widget → fast per-widget actions
  - Settings → Desktop Widgets → catalogue: which widgets
    exist, add new, global style, snap-to-grid, default position
  - The right-click menu has "Configure…" that opens Settings
    scrolled to that widget's section

## Widget catalogue by phase

  P1 (first shipped set):
    - Clock (analog + digital toggle)
    - CPU / RAM rings
    - Weather (current + 5-day)
    - Calendar (month grid)

  P2:
    - Now Playing (album art + controls)
    - Battery / power
    - Disk usage
    - Network (up/down rates)

  P3:
    - Sticky notes
    - Screen time
    - GPU stats
    - Music visualizer

------------------------------------------------------------------------
END OF WIDGET FRAMEWORK
------------------------------------------------------------------------
