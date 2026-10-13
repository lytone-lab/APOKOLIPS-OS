# Manus — Genie animation handoff

**You are producing one patch file.** You do not edit the repo. You
output a Python heredoc that `shellapp.h`-style applies to
`overlays.h`. Read everything below, then produce the patch.

---

## 1. Context

**Project:** APOKOLIPS-OS — a custom desktop shell (macOS + Windows
hybrid) running on sway/Wayland + Qt6 on Ubuntu. Repo:
`github.com/lytone-lab/APOKOLIPS-OS`.

**Relevant file:** `overlays.h` — contains `GenieOverlay`,
`ScaleOverlay`, `SuckOverlay` classes and the animator daemon.

**What's being fixed:** `GenieOverlay::paintEvent` — the code that
draws the shrink-to-dock animation when a window is minimized or
restored.

---

## 2. What the animation must do

When a user minimizes a window (yellow traffic light or Super+M):

1. Window screenshot is captured at its current rect.
2. Overlay window opens fullscreen (same size as screen, at 0,0).
3. Overlay draws a warped version of the screenshot that:
   - Collapses toward a **single sharp point** — the corner nearest
     the destination dock tile.
   - That point travels from its current position to the **center of
     the target dock tile**.
   - At the last frame, the collapsed point must sit **exactly on
     the tile center**, no overshoot, no undershoot.
4. Overlay closes; real window hides.

Restore is the same path in reverse (`m_reverse = true`).

---

## 3. The two known bugs (must fix)

### Bug A — overshoot on Finder and Node

**Symptom:** For `Finder` and `Node`, the collapsed point visibly
overshoots past the dock tile, sometimes going off-screen, before
disappearing. Other apps land correctly.

**Root cause suspicion:** the tip's path is `tipSrc → dock` with a
perpendicular S/C wiggle on top. The wiggle amplitude is
`qMin(deltaLen * 0.45, 180)`. For apps whose window rect is far from
the dock tile (Finder ~1000px, Node ~900px), the amplitude is large
enough that the perpendicular displacement overrides the direct
line, and near `u=1` the tip is pushed away from `dock` before the
`smoothStep` brings it back. Combined with `m_sCount` and envelope
`sin(π·u)`, the tip ends up outside the tile on some frames.

**What we want:** at frame `m_frame == m_frames - 1`, the tip must
be at exactly `m_dock`. No overshoot on any frame past the tile
center.

### Bug B — corners don't converge cleanly at the very start

**Symptom:** during the first few frames of the animation, the
window's four corners haven't visibly begun moving toward the tip
yet. The corners "hang" and then snap when the collapse kicks in.

**Root cause suspicion:** `TOP_DELAY = 0.5` means the top edge
starts collapsing at t=0.5. During t ∈ [0, 0.5], the top corners
are stationary while the bottom edge is already moving. That's
correct math but visually reads as "corners delayed". We want the
top corners to begin moving *immediately* — even a tiny amount —
so the whole shape starts converging from frame 1.

**What we want:** every corner's collapse progress at `t=0` should
be 0 (anchored), and the derivative at `t=0` should be positive
(already moving). At `t=1` all corners at the tip. No dead frames.

---

## 4. Constraints — do NOT change

- **No S/C shapes.** We tried S-curve and C-curve travel and both
  broke the landing. Straight-line travel from tip to dock. The
  wiggle amplitude should be **0**.
- **No fade.** We removed `p.setOpacity(1.0 - ...)` and it fixed a
  separate "tip vanishes before landing" bug. Keep opacity at 1.0.
- **No changes to constructor, tick(), handshake, or daemon.**
  Only `paintEvent` body.
- **No changes to visualconfig.h.** Ignore `genieShape`,
  `genieSCount` — they're unused now.
- **Do NOT change `m_strips` (the strip count) or the `SEGS = 24`
  constant.** Those are UI-tunable.
- **Do NOT change `genieTimelineProgress`.** It's shared with
  Scale and Suck.
- **Do NOT change the tip-corner selection** (nearest bottom corner
  to dock). That logic is correct.

---

## 5. Current `GenieOverlay::paintEvent` — exact code on disk

This is the state after commit `236a7b3` — "Genie: no fade, linear
tip path — funnel lands visibly on dock". Use this as your baseline.

Paste the exact current paintEvent:

    # In the VM, run:
    grep -n 'void paintEvent' overlays.h
    # Then extract the body of the GenieOverlay one.

**Copy the exact current text of `GenieOverlay::paintEvent` here
before editing.** Do not paste from memory.

---

## 6. What the fix should look like

Two structural changes to `paintEvent`:

**A) Tip path is straight-line, no wiggle:**

    // Before (with S/C wiggle) — replace:
    return tipSrc + delta * u
        + perp * (sAmp * env * osc);

    // After — straight line, guaranteed landing:
    return tipSrc + delta * u;

    // And set sAmp = 0.0, or delete the sAmp/perp/sN/shape
    // machinery entirely.

**B) Corners start moving at t=0:**

Change the collapse math so every strip has a nonzero velocity
at `t=0`. One approach: give every strip a small "head start"
in `localStart`. Instead of `localStart = (1-v) * TOP_DELAY`
(so the top strip starts at t=0.5), use something like
`localStart = (1-v) * TOP_DELAY * 0.3` — the top strip starts
at t=0.15 instead of 0.5. All strips are moving within 15% of
the animation.

Alternatively, use a single shared `u = smoothStep(t)` for all
strips, with the `v` (vertical position) applied only to the
*distance* each strip travels, not to *when* it starts. That
gives a uniform cascade — every strip begins moving at frame 1,
the bottom one arrives first at the tip, the top one arrives
last.

Pick whichever produces a clean visual. The requirement is:
**no dead frames at the start.**

---

## 7. Deliverable format

Output **one Python heredoc** that:

1. Runs from the project root (`~/apokolips-shell`).
2. Backs up `overlays.h` to `overlays.h.pre-manus.<timestamp>`.
3. Replaces `GenieOverlay::paintEvent` with the fixed version.
4. Aborts on any anchor mismatch (do not partially apply).
5. Builds with `cmake --build build -j2`.
6. Restores the backup on build failure.
7. Restarts the service only after a clean build.

Format:

    #!/usr/bin/env python3
    from pathlib import Path
    from datetime import datetime
    import shutil, subprocess, sys

    p = Path("overlays.h")
    original = p.read_text()

    OLD = """<exact current paintEvent — must match 1:1>"""
    NEW = """<fixed paintEvent>"""

    if original.count(OLD) != 1:
        print(f"ABORT: anchor count = {original.count(OLD)}")
        sys.exit(1)

    backup = p.with_name(
        "overlays.h.pre-manus." + datetime.now().strftime("%Y%m%d-%H%M%S"))
    shutil.copy2(p, backup)
    p.write_text(original.replace(OLD, NEW, 1))

    build = subprocess.run(["cmake", "--build", "build", "-j2"])
    if build.returncode:
        shutil.copy2(backup, p)
        print("BUILD FAILED — restored")
        sys.exit(1)

    subprocess.run(["systemctl", "--user", "restart",
                    "apokolips-shell.service"])
    print("OK — build succeeded, service restarted")

---

## 8. How to test your patch (for your own confidence, not required)

After the patch, minimize `Finder`, `Notes`, `Node` in turn. Each
should:
- Collapse to a sharp point at the bottom-corner nearest the dock.
- The point travels straight to the dock tile center.
- **No overshoot past the tile at any frame.**
- **No dead frames at the start** — corners move from frame 1.

The user is on Ubuntu/sway in a VM. Test results come back as
screenshots or a short video. You don't need to run anything.

---

## 9. What the user has asked for, verbatim

> "no S/C shapes, just a good patch that makes sure the converged
> point goes directly to intended tile, no overshoots at all for
> finder and node and a clean converge of corners at the start of
> the whole movement"

That's the entire brief. No other feature changes.
