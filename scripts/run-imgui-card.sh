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

# Read tty cell size. Rex often opens at a stub 80×24 then WINCH; wait so a
# preset tall viewport (e.g. 76×38) is not clamped away on first present.
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

VP_COL=3
VP_ROW=3
VP_W=$((COLS - 4))
# Default inset height; SDL_KITTY_CARD_HEIGHT_SCALE multiplies (e.g. 2 = 2× taller).
SCALE="${SDL_KITTY_CARD_HEIGHT_SCALE:-1}"
VP_H=$(( (ROWS - 5) * SCALE ))
PRESET_VP=0
if [[ -n "${SDL_KITTY_VIEWPORT:-}" ]]; then
  IFS=',' read -r VP_COL VP_ROW VP_W VP_H <<<"$SDL_KITTY_VIEWPORT" || true
  PRESET_VP=1
fi
VP_COL="${SDL_KITTY_VIEWPORT_COL:-$VP_COL}"
VP_ROW="${SDL_KITTY_VIEWPORT_ROW:-$VP_ROW}"
VP_W="${SDL_KITTY_VIEWPORT_W:-$VP_W}"
VP_H="${SDL_KITTY_VIEWPORT_H:-$VP_H}"
if (( VP_W < 20 )); then VP_W=20; fi
if (( VP_H < 8 )); then VP_H=8; fi

need_rows=$((VP_ROW + VP_H + 1))
need_cols=$((VP_COL + VP_W + 1))
if (( PRESET_VP == 1 )) || [[ -n "${SDL_KITTY_VIEWPORT_H:-}" ]]; then
  # Wait up to ~3s for the pane to grow enough for the requested card.
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    if (( ROWS >= need_rows && COLS >= need_cols )); then
      break
    fi
    sleep 0.3
    read_tty_size
  done
fi
# Only clamp to stty when the viewport was derived from it (not preset/env).
if (( PRESET_VP == 0 )) && [[ -z "${SDL_KITTY_VIEWPORT_H:-}" ]]; then
  if (( VP_COL + VP_W - 1 > COLS )); then VP_W=$((COLS - VP_COL + 1)); fi
  if (( VP_ROW + VP_H - 1 > ROWS )); then VP_H=$((ROWS - VP_ROW + 1)); fi
fi

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

# Title/status only — driver draws the ASCII box via KITTY_DrawViewportChrome
# (and redraws it on WINCH so the border matches the live viewport).
STATUS_ROW=$ROWS
if (( VP_ROW + VP_H + 1 > STATUS_ROW )); then
  STATUS_ROW=$((VP_ROW + VP_H + 1))
fi
{
  printf '\033[2J\033[H\033[?25l'
  printf '\033[1;1H\033[1m Dear ImGui (kitty card) \033[0m  viewport=%s  Esc/q in demo to quit' \
    "$SDL_KITTY_VIEWPORT"
  printf '\033[%d;1H status: mouse/keyboard → ImGui inside card · stderr → %s' "$STATUS_ROW" "$ERR"
  printf '\033[H'
} >/dev/tty 2>/dev/null || {
  printf '\033[2J\033[H\033[?25l'
  printf 'Dear ImGui kitty card viewport=%s\n' "$SDL_KITTY_VIEWPORT"
}

: >"$ERR"
echo "imgui-card: viewport=$SDL_KITTY_VIEWPORT fb_fixed=$SDL_KITTY_FB_FIXED → $ERR" >>"$ERR"
exec "$BIN" "$@" 2>>"$ERR"
