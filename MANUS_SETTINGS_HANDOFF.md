# Manus — Settings Window Chrome & Traffic Lights Handoff

**Status:** The sliders in Settings → Appearance for "Window Chrome" and "Traffic Lights" do not update the UI live. The UI Style boxes are rendering as blank/black squares.

## The Core Problem
`SettingsWindow` (in `settings.h`) is a standalone `QWidget`. It does **not** inherit from `ShellApp` (unlike Finder, Notes, etc.). 
Because of this, it has its own hardcoded `paintEvent`, its own `makeLight` lambda for traffic lights, and its own `updateWindowChrome()` method.

The sliders currently write to `visual.json` successfully, but the `SettingsWindow` fails to repaint its layout or update its child widgets (the traffic lights) in real-time.

## Task 1: Fix Traffic Lights Sliders (Live Update)
- **Where:** `settings.h` -> `buildUi()` (around line 330)
- **Issue:** The `makeLight` lambda hardcodes the size when the window is constructed. There is an `updateTrafficLights()` method, but when the slider moves, the buttons don't resize.
- **Fix:** Ensure `updateTrafficLights()` updates `setFixedSize()` on `m_closeBtn`, `m_minBtn`, and `m_maxBtn`, updates their stylesheets, and forces `m_header->layout()->activate()`.

## Task 2: Fix Window Chrome Sliders (Live Update)
- **Where:** `settings.h` -> `paintEvent()` and `updateWindowChrome()`
- **Issue:** `paintEvent` draws the header strip and separator lines using hardcoded values (`42` and `14`). Even when `updateWindowChrome()` is called, the layout margins for the rounded corners don't update.
- **Fix:** 
  1. In `paintEvent`, replace `42` with `VisualConfigManager::instance().cfg().windowHeaderHeight`.
  2. In `paintEvent`, replace the hardcoded radius `14` with `cfg.windowRadius`.
  3. In `updateWindowChrome()`, update the root layout's bottom margin to match the new radius, and call `update()`.

## Task 3: Fix UI Style Thumbnails
- **Where:** `settings.h` -> `makeStyleIcon()` and the UI Style card in `makeAppearancePage()`.
- **Issue:** The `QPixmap` generation either renders black, or the button stylesheet covers it with an opaque background.
- **Fix:** Generate distinct, colorful thumbnails for Glass, Neumorphism, Skeuomorphism, and Claymorphism. Ensure the button stylesheet uses `background: transparent;` so the icon is visible, and highlight the selected one with an accent border.

## How to test
1. Open Settings → Appearance.
2. Drag "Header height", "Corner radius", "Traffic light size", and "Traffic light spacing".
3. The Settings window itself must update instantly as the sliders move.
