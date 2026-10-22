#!/usr/bin/env bash
# Run a binary against the sdl-kitty-built libSDL3 via DynAPI (or DYLD/LD path).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build"
LIB=""
if [[ -f "${BUILD}/third_party/SDL/libSDL3.dylib" ]]; then
  LIB="${BUILD}/third_party/SDL/libSDL3.dylib"
elif [[ -f "${BUILD}/third_party/SDL/libSDL3.so" ]]; then
  LIB="${BUILD}/third_party/SDL/libSDL3.so"
else
  echo "Built libSDL3 not found under ${BUILD}/third_party/SDL" >&2
  echo "Configure and build first, e.g.:" >&2
  echo "  cmake -S ${ROOT} -B ${BUILD} && cmake --build ${BUILD} -j" >&2
  exit 1
fi

export SDL_VIDEO_DRIVER=kitty
export SDL3_DYNAMIC_API="${LIB}"
# Also put our lib first on the loader path for binaries linked to it directly.
if [[ "$(uname)" == "Darwin" ]]; then
  export DYLD_LIBRARY_PATH="$(dirname "$LIB"):${DYLD_LIBRARY_PATH:-}"
else
  export LD_LIBRARY_PATH="$(dirname "$LIB"):${LD_LIBRARY_PATH:-}"
fi

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <command> [args...]" >&2
  echo "example: $0 ${BUILD}/solid_present" >&2
  exit 1
fi

exec "$@"
