#!/bin/sh
# Full-pane stretch + FB cap + 1016 — the config that matched pointer to crosshair.
set -e
DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
ROOT="$(CDPATH= cd -- "$DIR/../.." && pwd)"
PAINT="$DIR/mouse_paint"

if [ ! -x "$PAINT" ]; then
  echo "missing $PAINT — rebuild and copy from build/mouse_paint" >&2
  exit 1
fi

export SDL_VIDEO_DRIVER=kitty
export SDL_KITTY_TRANSMIT="${SDL_KITTY_TRANSMIT:-direct}"
export SDL_KITTY_PLACE_CELLS=1
export SDL_KITTY_MOUSE_PIXELS=1
# Optional override; unset uses Rex default 1280x720 in the driver.
# export SDL_KITTY_FB_MAX=1280x720

echo "mouse_paint_fullpane: PLACE_CELLS=1 (stretch) + FB cap + 1016"
echo "Expect: filled dark pane, yellow crosshair under pointer. q quits."
echo "---"
cd "$DIR"
exec "$PAINT"
