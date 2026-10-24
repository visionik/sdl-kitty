#!/bin/sh
# Run in an already-sized Rex/Ghostty pane (not a fresh `rex run` stub 80x24).
# Usage: mouse_paint_compare.sh [1|0]   1=c/r stretch (default), 0=1:1 pixels
set -e
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
cd "$ROOT/build"
PAINT="./mouse_paint"
MODE="${1:-1}"
export SDL_VIDEO_DRIVER=kitty
if [ -z "${SDL_KITTY_PROFILES:-}" ] && [ -f "$ROOT/config/terminals.toml" ]; then
  export SDL_KITTY_PROFILES="$ROOT/config/terminals.toml"
fi
export SDL_KITTY_TRANSMIT="${SDL_KITTY_TRANSMIT:-direct}"
export SDL_KITTY_PLACE_CELLS="$MODE"
export SDL_KITTY_MOUSE_PIXELS=1
export SDL_KITTY_DEBUG="${SDL_KITTY_DEBUG:-1}"
# Rex: default cap is 1280x720; override with SDL_KITTY_FB_MAX=WxH|off.
echo "PLACE_CELLS=$MODE  (1=stretch+1016, 0=1:1 pixels)"
echo "TERM_PROGRAM=${TERM_PROGRAM:-?}  FB_MAX=${SDL_KITTY_FB_MAX:-(rex default 1280x720)}"
if [ "$MODE" = "0" ]; then
  echo "Expect: capped image at 1:1 (may not fill pane), yellow crosshair under pointer. q quits."
else
  echo "Expect: filled pane, yellow crosshair under pointer. q quits."
fi
echo "---"
exec "$PAINT"
