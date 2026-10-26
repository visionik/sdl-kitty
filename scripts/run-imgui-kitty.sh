#!/usr/bin/env bash
# Run Dear ImGui SDL3+SDL_Renderer example under the kitty video driver.
# stdout must stay on the tty (Kitty APC). stderr goes to a file so logs
# do not corrupt the graphics stream (same pattern as mame-kitty.sh).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${IMGUI_BIN:-$ROOT/build/imgui_sdlrenderer3}"
ERR="${IMGUI_KITTY_ERR:-/tmp/imgui-kitty.err}"

if [[ ! -x "$BIN" ]]; then
  echo "missing $BIN — build with: cmake --build build --target imgui_sdlrenderer3" >&2
  exit 1
fi

export SDL_VIDEO_DRIVER="${SDL_VIDEO_DRIVER:-kitty}"
# Kitty has no GPU context; force the software renderer onto the window FB.
export SDL_RENDER_DRIVER="${SDL_RENDER_DRIVER:-software}"
export SDL_KITTY_PROFILES="${SDL_KITTY_PROFILES:-$ROOT/config/terminals.toml}"
export DYLD_LIBRARY_PATH="$ROOT/build/third_party/SDL${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
export SDL3_DYNAMIC_API="${SDL3_DYNAMIC_API:-$ROOT/build/third_party/SDL/libSDL3.dylib}"

exec "$BIN" "$@" 2>"$ERR"
