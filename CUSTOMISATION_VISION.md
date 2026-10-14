# APOKOLIPS-OS — Customisation Vision

**What this is:** the full plan for what will eventually be the
"Windhawk-class" customisation layer — every visual knob the user
can turn, every automation they can build, every mod they can
install and share. Nothing hidden behind config files. Every
customisation goes through Settings, is live, and reverts cleanly.

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
