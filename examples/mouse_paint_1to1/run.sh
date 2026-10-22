#!/bin/sh
# Native 1:1 placement + FB cap + raw 1016 — crosshair tracks the dark rectangle.
set -e
DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
PAINT="$DIR/mouse_paint"

if [ ! -x "$PAINT" ]; then
  echo "missing $PAINT — rebuild and copy from build/mouse_paint" >&2
  exit 1
fi

export SDL_VIDEO_DRIVER=kitty
export SDL_KITTY_TRANSMIT="${SDL_KITTY_TRANSMIT:-direct}"
export SDL_KITTY_PLACE_CELLS=0
export SDL_KITTY_MOUSE_PIXELS=1
# Optional override; unset uses Rex default 1280x720 in the driver.
# export SDL_KITTY_FB_MAX=1280x720

echo "mouse_paint_1to1: PLACE_CELLS=0 (1:1) + FB cap + raw 1016"
echo "Expect: capped dark rectangle, yellow crosshair under pointer on it. q quits."
echo "---"
cd "$DIR"
exec "$PAINT"
