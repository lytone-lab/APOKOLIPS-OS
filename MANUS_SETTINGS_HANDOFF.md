# Manus — Settings Window Chrome & Traffic Lights Handoff

**Workflow:** You do NOT edit the repo. You provide paste-ready Python heredoc patches. The user runs them, tests visually, and gives you feedback. Iterate until it works.

**Branch:** main (already pushed, handoff file lives here)

## The Core Problem
`SettingsWindow` (in `settings.h`) is a standalone `QWidget`. It does **not** inherit from `ShellApp` (unlike Finder, Notes, etc.).
Because of this, it has its own hardcoded `paintEvent`, its own `makeLight` lambda for traffic lights, and its own `updateWindowChrome()` method.

The sliders currently write to `visual.json` successfully (proven via logs: `[chrome] updateWindowChrome. Header: 49`), but:
1. **Sliders auto-reset** — the value snaps back after release instead of persisting.
2. **Settings window does not repaint** — changing Header Height / Radius / Traffic Light Size does not visibly update the Settings window itself.
3. **Other apps do not update live** — the user wants to drag a slider in Settings and see the change happen in Finder/Notes in real-time, side-by-side.
4. **UI Style thumbnails render as black boxes** — the 4 style buttons (Glass, Neumorphism, Skeuomorphism, Claymorphism) show no visuals.

## The Desired End State
While dragging ANY slider in Settings → Appearance:
- The Settings window itself updates live (header height, corner radius, traffic light size/spacing all redraw immediately).
- **Another open app (e.g. Finder) simultaneously updates live** — because it watches `visual.json` and reloads on change.
- The slider value **persists** after release (does not snap back to old value).
- The 4 UI Style buttons show colorful, distinct thumbnails of what each style looks like.

## Task 1: Fix Slider Auto-Reset
The slider writes via `VisualConfigManager::instance().setAndSave(...)` on every `valueChanged` tick. Something in `setAndSave()` or `save()` is re-reading `visual.json` from disk and overwriting the in-memory value with the stale on-disk value. This is a known save-race documented in PROJECT_STATE.txt (Widget toggle persistence bug, fix option A/B/C).

**Likely fix:** In `visualconfig.h`, either make `save()` write `m_cfg` directly without re-reading, OR add a `saveNoReload()` variant used by sliders.

## Task 2: Fix Settings Live Repaint
- In `settings.h::paintEvent`, the header separator is drawn at hardcoded `42` and the rounded body uses hardcoded `14`. Replace both with `cfg.windowHeaderHeight` and `cfg.windowRadius`.
- In `updateWindowChrome()`, update root layout's bottom margin to match `cfg.windowRadius` and call `update()`.
- In `updateTrafficLights()`, call `setFixedSize()` on `m_closeBtn`/`m_minBtn`/`m_maxBtn`, update their stylesheets, and `m_header->layout()->activate()`.

## Task 3: Cross-App Live Sync
`ShellApp::setupShellApp()` (in `shellapp.h`) already has a `QFileSystemWatcher` on `visual.json`. Verify it fires `VisualConfigManager::instance().load()` and `onConfigChanged()` on every write. If the app doesn't repaint after `load()`, add `update()` and `updateGeometry()` explicitly.

## Task 4: UI Style Thumbnails
- Generate a `QPixmap` per style with a gradient wallpaper behind a mock window, showing the effect. Glass = translucent blur; Neumorphism = soft raised shadow; Skeuomorphism = beige chrome with border; Claymorphism = puffy rounded corners in pink/purple.
- The button stylesheet must be `background: transparent;` so the icon shows. Selected state = accent border.

## How the User Tests
1. Open Settings → Appearance.
2. Open Finder side-by-side.
3. Drag "Header height" slider.
4. Both Settings AND Finder headers should grow/shrink simultaneously.
5. Release the slider — the value should stay (not snap back).
6. Repeat for Corner Radius, Traffic Light Size, Traffic Light Spacing.
7. UI Style buttons should show colorful thumbnails (not black boxes).

## Known Constraints
- Build is `cmake --build build -j1` (VM has 3GB RAM, -j2 OOMs).
- Settings is a separate process — `qDebug()` won't show in shell journal. Use `QFile("/tmp/settings-debug.log")` if logging is needed.
- Wayland + sway: do not use `QPropertyAnimation` on geometry (causes oscillation). Direct `setGeometry` only.
