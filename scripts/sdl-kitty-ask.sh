#!/usr/bin/env bash
# Present a question + 2–8 choices as an sdl-kitty ImGui card.
# The choice is the process exit status (Unix-style).
#
#   ./scripts/sdl-kitty-ask.sh [--multi|-m] "Question?" "A" "B" ["C" ...]
#   status=$?
#
# Single-select (radio) by default → exit 0..n-1 (0-based index).
# --multi (checkboxes) → exit bitmask (bit i ⇒ option i selected).
# Esc / Cancel → exit 255.
# Usage / arg errors → exit 2.
#
# stdout is the Kitty graphics tty (APC); do not capture it for the answer.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${SDL_KITTY_ASK_BIN:-$ROOT/build/sdl_kitty_ask}"
ERR="${SDL_KITTY_ASK_ERR:-/tmp/sdl-kitty-ask.err}"

if [[ ! -x "$BIN" ]]; then
  echo "missing $BIN — build with: cmake --build build --target sdl_kitty_ask" >&2
  exit 2
fi

MULTI=0
FORWARD=()
POSITIONAL=0
SKIP_NEXT=0
for a in "$@"; do
  if (( SKIP_NEXT == 1 )); then
    FORWARD+=("$a")
    SKIP_NEXT=0
    continue
  fi
  case "$a" in
    --multi|-m) MULTI=1; FORWARD+=("$a") ;;
    --help|-h) FORWARD+=("$a") ;;
    --font-size|--theme) FORWARD+=("$a"); SKIP_NEXT=1 ;;
    --out)
      echo "sdl-kitty-ask: choice is the exit status; --out removed" >&2
      exit 2
      ;;
    -*)
      FORWARD+=("$a")
      ;;
    *)
      FORWARD+=("$a")
      POSITIONAL=$((POSITIONAL + 1))
      ;;
  esac
done

read_tty_size() {
  COLS=80
  ROWS=24
  if [[ -r /dev/tty ]]; then
    local sz
    sz=$(stty size </dev/tty 2>/dev/null || true)
    if [[ -n "${sz:-}" ]]; then
      ROWS=${sz%% *}
      COLS=${sz##* }
    fi
  fi
}
read_tty_size

if (( POSITIONAL > 1 )); then
  NOPT=$((POSITIONAL - 1))
else
  NOPT=2
fi
if (( NOPT < 2 )); then NOPT=2; fi
if (( NOPT > 8 )); then NOPT=8; fi

VP_COL="${SDL_KITTY_VIEWPORT_COL:-3}"
VP_ROW="${SDL_KITTY_VIEWPORT_ROW:-3}"
VP_W="${SDL_KITTY_VIEWPORT_W:-$((COLS - 4))}"
VP_H="${SDL_KITTY_VIEWPORT_H:-$((6 + NOPT + 3))}"
if [[ -z "${SDL_KITTY_VIEWPORT_W:-}" ]] && (( VP_W > 96 )); then VP_W=96; fi
if (( VP_W < 40 )); then VP_W=40; fi
if (( VP_H < 12 )); then VP_H=12; fi
if (( VP_COL + VP_W - 1 > COLS )) && (( COLS > VP_COL )); then
  VP_W=$((COLS - VP_COL + 1))
fi
need_rows=$((VP_ROW + VP_H + 1))
need_cols=$((VP_COL + VP_W + 1))
for _ in 1 2 3 4 5 6 7 8 9 10; do
  if (( ROWS >= need_rows && COLS >= need_cols )); then
    break
  fi
  sleep 0.3
  read_tty_size
done
if (( VP_ROW + VP_H - 1 > ROWS )) && (( ROWS > VP_ROW + 2 )); then
  VP_H=$((ROWS - VP_ROW - 1))
fi

FB_W=$((VP_W * 16))
FB_H=$((VP_H * 32))
if (( FB_W > 1600 )); then FB_H=$((FB_H * 1600 / FB_W)); FB_W=1600; fi
if (( FB_H > 960 )); then FB_W=$((FB_W * 960 / FB_H)); FB_H=960; fi
if (( FB_W < 640 )); then FB_W=640; fi
if (( FB_H < 448 )); then FB_H=448; fi

export SDL_VIDEO_DRIVER="${SDL_VIDEO_DRIVER:-kitty}"
export SDL_RENDER_DRIVER="${SDL_RENDER_DRIVER:-software}"
export SDL_KITTY_PROFILES="${SDL_KITTY_PROFILES:-$ROOT/config/sdl-kitty.toml}"
export SDL_KITTY_ALTSCREEN="${SDL_KITTY_ALTSCREEN:-0}"
export SDL_KITTY_PLACE_CELLS="${SDL_KITTY_PLACE_CELLS:-1}"
export SDL_KITTY_VIEWPORT="${SDL_KITTY_VIEWPORT:-$VP_COL,$VP_ROW,$VP_W,$VP_H}"
export SDL_KITTY_FB_FIXED="${SDL_KITTY_FB_FIXED:-${FB_W}x${FB_H}}"
# Keep the card image after OK/cancel unless the caller overrides.
export SDL_KITTY_LEAVE_IMAGES="${SDL_KITTY_LEAVE_IMAGES:-1}"
export DYLD_LIBRARY_PATH="$ROOT/build/third_party/SDL${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
export SDL3_DYNAMIC_API="${SDL3_DYNAMIC_API:-$ROOT/build/third_party/SDL/libSDL3.dylib}"

MODE_LABEL="single"
if (( MULTI )); then MODE_LABEL="multi"; fi
{
  printf '\033[2J\033[H\033[?25l'
  printf '\033[1;1H\033[1m sdl-kitty-ask \033[0m  %s-select  exit=index/mask  Esc=255' \
    "$MODE_LABEL"
  printf '\033[H'
} >/dev/tty 2>/dev/null || true

: >"$ERR"
set +e
"$BIN" "${FORWARD[@]}" 2>>"$ERR"
rc=$?
set -e
exit "$rc"
