#!/usr/bin/env bash
# handoff.sh — one-shot onboarding packet for a fresh AI session.
# Run:  ./handoff.sh          → prints packet to stdout
#       ./handoff.sh > packet.txt   → saves to a file
#
# The packet is designed to be pasted into a fresh AI chat.
# It gives the AI: current state, rules, file tree, git status,
# and the canonical docs — with a standing instruction to ask
# for diagnostics rather than assume.

set -u
cd "$(dirname "$0")"

# ── Header ──
echo "════════════════════════════════════════════════════════════"
echo " APOKOLIPS OS — HANDOFF PACKET"
echo " Generated: $(date '+%Y-%m-%d %H:%M:%S %Z')"
echo "════════════════════════════════════════════════════════════"
echo ""
echo "READ THIS FIRST, NEW AI:"
echo "You are reading an auto-generated handoff packet. It contains"
echo "everything you need to onboard without assuming anything."
echo ""
echo "MANDATORY READING ORDER — do not skip, do not skim:"
echo "  1. TEAM_PROTOCOL.md       (rules of engagement — NON-NEGOTIABLE)"
echo "  2. AI_HANDOFF.md          (onboarding, environment, workflow)"
echo "  3. CURRENT_STATE.md       (what's happening right now)"
echo "  4. PROJECT_STATE.txt      (bug list + feature backlog)"
echo "  5. ARCHITECTURE_NOTES.md  (design roadmap — NOW/SOON/EVENTUAL)"
echo "  6. CUSTOMISATION_VISION.md (long-term customisation plan)"
echo ""
echo "STANDING INSTRUCTIONS (these override your defaults):"
echo "  • Never suggest the user take a break, rest, or stop."
echo "  • Read the WHOLE function before patching. Not a grep."
echo "  • Verify every helper actually exists before referencing it."
echo "  • Trace lambda captures — this has caused real crashes."
echo "  • If two loops must use the same value, patch them together."
echo "  • Show unfiltered build output — grep-only hides failures."
echo "  • A bug in PROJECT_STATE.txt is a CLAIM, not a FACT."
echo "    Reproduce first. Only patch what still reproduces."
echo "  • Adding a new app touches SEVEN places. See TEAM_PROTOCOL."
echo "  • If a listed feature appears missing, ASK before writing."
echo "    Do not assume. Do not fabricate. Check the code."
echo "  • If you cannot find an anchor, ask for a diagnostic."
echo "    Do not guess."
echo ""
echo "────────────────────────────────────────────────────────────"
echo " GIT STATE"
echo "────────────────────────────────────────────────────────────"
echo "HEAD:"
git log -1 --format='  %h  %s  (%cr)' 2>/dev/null || echo "  (not a git repo)"
echo ""
echo "Branch:"
git branch --show-current 2>/dev/null || echo "  (unknown)"
echo ""
echo "Recent commits (last 15):"
git log --oneline -15 2>/dev/null | sed 's/^/  /'
echo ""
echo "Working tree:"
if [ -z "$(git status --short 2>/dev/null)" ]; then
  echo "  (clean — nothing uncommitted)"
else
  git status --short 2>/dev/null | sed 's/^/  /'
fi
echo ""
echo "────────────────────────────────────────────────────────────"
echo " LIVE CONFIG SNAPSHOT"
echo "────────────────────────────────────────────────────────────"
CFG="$HOME/.config/apokolips/visual.json"
if [ -f "$CFG" ]; then
  python3 - "$CFG" <<'PYEOF' 2>/dev/null || cat "$CFG" | head -30
import json, sys
d = json.load(open(sys.argv[1]))
keys = ["minimizeStyle", "genieDurationMs", "dockPosition",
        "sigilAccent", "focusMode", "dndEnabled", "focusPolicy"]
for k in keys:
    if k in d:
        print(f"  {k}: {d[k]!r}")
PYEOF
else
  echo "  (no visual.json — first run)"
fi
echo ""
echo "────────────────────────────────────────────────────────────"
echo " FILE TREE (headers + top-level)"
echo "────────────────────────────────────────────────────────────"
find . -maxdepth 1 -name '*.h' -o -name '*.cpp' -o -name '*.sh' \
     -o -name '*.txt' -o -name '*.md' | sort | sed 's|^\./|  |'
echo ""
echo "  app/ (if split):"
[ -d app ] && ls app/ | sed 's/^/    /' || echo "    (no app/ dir)"
echo ""
echo "  assets/:"
[ -d assets ] && find assets -maxdepth 2 -type f | sed 's/^/    /'
echo ""
echo "────────────────────────────────────────────────────────────"
echo " FULL DOCS (do not skim — read carefully)"
echo "────────────────────────────────────────────────────────────"
for f in TEAM_PROTOCOL.md AI_HANDOFF.md CURRENT_STATE.md \
         ARCHITECTURE_NOTES.md CUSTOMISATION_VISION.md README.md; do
  if [ -f "$f" ]; then
    echo ""
    echo "════════════ $f ════════════"
    cat "$f"
    echo ""
  else
    echo ""
    echo "════════════ $f (MISSING) ════════════"
    echo ""
  fi
done
echo ""
echo "════════════ PROJECT_STATE.txt (first 400 lines — full bug list + session log) ════════════"
head -400 PROJECT_STATE.txt
echo ""
echo "════════════ END OF HANDOFF PACKET ════════════"
echo ""
echo "FINAL INSTRUCTION: After reading this packet, run ONE of:"
echo "  • a read-only diagnostic (to confirm a listed bug reproduces),"
echo "  • a git log to see the most recent commits,"
echo "  • or ask the user 'what are we doing today?'"
echo ""
echo "Do NOT immediately send a patch. First verify. Then patch."
