#!/usr/bin/env bash
# Run Nuklear SDL3+SDL_Renderer demo under the kitty video driver.
# stdout must stay on the tty (Kitty APC). stderr goes to a file so logs
# do not corrupt the graphics stream (same pattern as run-imgui-kitty.sh).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NUKLEAR_BIN:-$ROOT/build/nuklear_sdl3_renderer}"
ERR="${NUKLEAR_KITTY_ERR:-/tmp/nuklear-kitty.err}"

if [[ ! -x "$BIN" ]]; then
  echo "missing $BIN — build with: cmake --build build --target nuklear_sdl3_renderer" >&2
  exit 1
fi

export SDL_VIDEO_DRIVER="${SDL_VIDEO_DRIVER:-kitty}"
export SDL_RENDER_DRIVER="${SDL_RENDER_DRIVER:-software}"
export SDL_KITTY_PROFILES="${SDL_KITTY_PROFILES:-$ROOT/config/terminals.toml}"
export DYLD_LIBRARY_PATH="$ROOT/build/third_party/SDL${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
export SDL3_DYNAMIC_API="${SDL3_DYNAMIC_API:-$ROOT/build/third_party/SDL/libSDL3.dylib}"

exec "$BIN" "$@" 2>"$ERR"
