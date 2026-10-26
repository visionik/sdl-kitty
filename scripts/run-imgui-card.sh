#!/usr/bin/env bash
# Run Dear ImGui (SDL3+SDL_Renderer) as a TUI card inside a text border.
# Uses SDL_KITTY_VIEWPORT + ALTSCREEN=0 (docs/tui-card.md).
# stdout stays on the tty for Kitty APC; stderr → file.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${IMGUI_BIN:-$ROOT/build/imgui_sdlrenderer3}"
ERR="${IMGUI_KITTY_ERR:-/tmp/imgui-kitty-card.err}"

if [[ ! -x "$BIN" ]]; then
  echo "missing $BIN — build with: cmake --build build --target imgui_sdlrenderer3" >&2
  exit 1
fi

# Terminal cell size (fallback 80x24).
COLS=80
ROWS=24
if [[ -r /dev/tty ]]; then
  # Prefer the graphics tty size when available.
  sz=$(stty size </dev/tty 2>/dev/null || true)
  if [[ -n "${sz:-}" ]]; then
    ROWS=${sz%% *}
    COLS=${sz##* }
  fi
fi
# Inset card: title row, border, status.
VP_COL="${SDL_KITTY_VIEWPORT_COL:-3}"
VP_ROW="${SDL_KITTY_VIEWPORT_ROW:-3}"
VP_W="${SDL_KITTY_VIEWPORT_W:-$((COLS - 4))}"
VP_H="${SDL_KITTY_VIEWPORT_H:-$((ROWS - 5))}"
if (( VP_W < 20 )); then VP_W=20; fi
if (( VP_H < 8 )); then VP_H=8; fi
if (( VP_COL + VP_W - 1 > COLS )); then VP_W=$((COLS - VP_COL + 1)); fi
if (( VP_ROW + VP_H - 1 > ROWS )); then VP_H=$((ROWS - VP_ROW + 1)); fi

# Modest FB stretched into the cell box (override with SDL_KITTY_FB_FIXED).
FB_W=$((VP_W * 8))
FB_H=$((VP_H * 16))
if (( FB_W < 320 )); then FB_W=320; fi
if (( FB_H < 200 )); then FB_H=200; fi

export SDL_VIDEO_DRIVER="${SDL_VIDEO_DRIVER:-kitty}"
export SDL_RENDER_DRIVER="${SDL_RENDER_DRIVER:-software}"
export SDL_KITTY_PROFILES="${SDL_KITTY_PROFILES:-$ROOT/config/terminals.toml}"
export SDL_KITTY_ALTSCREEN="${SDL_KITTY_ALTSCREEN:-0}"
export SDL_KITTY_PLACE_CELLS="${SDL_KITTY_PLACE_CELLS:-1}"
export SDL_KITTY_VIEWPORT="${SDL_KITTY_VIEWPORT:-$VP_COL,$VP_ROW,$VP_W,$VP_H}"
export SDL_KITTY_FB_FIXED="${SDL_KITTY_FB_FIXED:-${FB_W}x${FB_H}}"
export DYLD_LIBRARY_PATH="$ROOT/build/third_party/SDL${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
export SDL3_DYNAMIC_API="${SDL3_DYNAMIC_API:-$ROOT/build/third_party/SDL/libSDL3.dylib}"

# Primary-screen chrome (driver will not smcup/clear).
{
  printf '\033[2J\033[H\033[?25l'
  printf '\033[1;1H\033[1m Dear ImGui (kitty card) \033[0m  viewport=%s  Esc/q in demo to quit' \
    "$SDL_KITTY_VIEWPORT"
  # Border one cell outside the viewport when space allows.
  x0=$((VP_COL - 1))
  y0=$((VP_ROW - 1))
  x1=$((VP_COL + VP_W))
  y1=$((VP_ROW + VP_H))
  if (( x0 >= 1 && y0 >= 2 && x1 <= COLS && y1 < ROWS )); then
    printf '\033[%d;%dH+' "$y0" "$x0"
    printf '%*s' "$VP_W" '' | tr ' ' '-'
    printf '+'
    for ((r = 0; r < VP_H; r++)); do
      printf '\033[%d;%dH|' "$((VP_ROW + r))" "$x0"
      printf '\033[%d;%dH|' "$((VP_ROW + r))" "$x1"
    done
    printf '\033[%d;%dH+' "$y1" "$x0"
    printf '%*s' "$VP_W" '' | tr ' ' '-'
    printf '+'
  fi
  printf '\033[%d;1H status: mouse/keyboard → ImGui inside card · stderr → %s' "$ROWS" "$ERR"
  printf '\033[H'
} >/dev/tty 2>/dev/null || {
  # Fallback when /dev/tty is unavailable (still try stdout before SDL owns it).
  printf '\033[2J\033[H\033[?25l'
  printf 'Dear ImGui kitty card viewport=%s\n' "$SDL_KITTY_VIEWPORT"
}

: >"$ERR"
echo "imgui-card: viewport=$SDL_KITTY_VIEWPORT fb_fixed=$SDL_KITTY_FB_FIXED → $ERR" >>"$ERR"
exec "$BIN" "$@" 2>>"$ERR"
