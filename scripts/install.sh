#!/bin/sh
# One-line installer for sdl-kitty (installs the `sdlk` CLI, private libSDL3,
# terminal profiles, and showcase demos).
#
#   curl -fsSL https://raw.githubusercontent.com/visionik/sdl-kitty/main/scripts/install.sh | sh
#
# Options (pass via `sh -s -- ...`):
#   --version TAG    Install a specific release tag (e.g. v0.1.0)
#   --prefix DIR     Install directory (default: ~/.local/share/sdl-kitty/install)
#   --bin-dir DIR    Directory for `sdlk` symlink (default: ~/.local/bin)
#   --from-source    Build from git instead of downloading a prebuilt release
#   --yes, -y        Non-interactive defaults
#   --no-mame        Skip MAME installation prompt
#   --no-roms        Skip free ROM download prompt
#   --no-setup       Install files only; do not run `sdlk setup`
#   --uninstall      Remove a local install
set -eu

REPO="${SDLK_REPO:-visionik/sdl-kitty}"
VERSION="${SDLK_VERSION:-}"
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
INSTALL_DIR="${SDLK_PREFIX:-$DATA_HOME/sdl-kitty/install}"
BIN_DIR="${SDLK_BIN_DIR:-$HOME/.local/bin}"
FROM_SOURCE=0
AUTO_YES=0
NO_MAME=0
NO_ROMS=0
NO_SETUP=0
DO_UNINSTALL=0

# Formatting (only when stdout is a tty)
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
  BOLD="$(printf '\033[1m')"
  GREEN="$(printf '\033[32m')"
  YELLOW="$(printf '\033[33m')"
  RED="$(printf '\033[31m')"
  CYAN="$(printf '\033[36m')"
  RESET="$(printf '\033[0m')"
else
  BOLD="" GREEN="" YELLOW="" RED="" CYAN="" RESET=""
fi

ok()   { printf '%s✓%s %s\n' "$GREEN" "$RESET" "$*"; }
warn() { printf '%s!%s %s\n' "$YELLOW" "$RESET" "$*"; }
err()  { printf '%s✗%s %s\n' "$RED" "$RESET" "$*" >&2; }
info() { printf '%s→%s %s\n' "$CYAN" "$RESET" "$*"; }

usage() {
  cat <<'EOF'
Usage: install.sh [options]

Options:
  --version TAG    Install a specific GitHub release tag (default: latest)
  --prefix DIR     Installation directory (default: ~/.local/share/sdl-kitty/install)
  --bin-dir DIR    Symlink directory for `sdlk` (default: ~/.local/bin)
  --from-source    Build from git with CMake instead of downloading a tarball
  --yes, -y        Run setup non-interactively
  --no-mame        Skip MAME installation prompt
  --no-roms        Skip free mamedev.org ROMs prompt
  --no-setup       Skip post-install setup wizard
  --uninstall      Remove local sdl-kitty installation
  -h, --help       Show this help
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --version)     shift; VERSION="${1:-}" ;;
    --prefix)      shift; INSTALL_DIR="${1:-}" ;;
    --bin-dir)     shift; BIN_DIR="${1:-}" ;;
    --from-source) FROM_SOURCE=1 ;;
    --yes|-y)      AUTO_YES=1 ;;
    --no-mame)     NO_MAME=1 ;;
    --no-roms)     NO_ROMS=1 ;;
    --no-setup)    NO_SETUP=1 ;;
    --uninstall)   DO_UNINSTALL=1 ;;
    -h|--help)     usage; exit 0 ;;
    *)             err "Unknown option: $1"; usage >&2; exit 2 ;;
  esac
  shift
done

if [ "$DO_UNINSTALL" -eq 1 ]; then
  rm -f "$BIN_DIR/sdlk"
  rm -rf "$INSTALL_DIR"
  ok "Removed $INSTALL_DIR and $BIN_DIR/sdlk"
  exit 0
fi

fetch() {
  url="$1"
  out="$2"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL --retry 2 -o "$out" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -q -O "$out" "$url"
  else
    err "Need curl or wget to download files."
    return 1
  fi
}

fetch_stdout() {
  url="$1"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL --retry 2 "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -q -O - "$url"
  else
    return 1
  fi
}

verify_sha256() {
  file="$1"
  expected="$2"
  actual=""
  if command -v shasum >/dev/null 2>&1; then
    actual="$(shasum -a 256 "$file" | awk '{print $1}')"
  elif command -v sha256sum >/dev/null 2>&1; then
    actual="$(sha256sum "$file" | awk '{print $1}')"
  else
    warn "Neither shasum nor sha256sum found; skipping checksum verification."
    return 0
  fi
  if [ "$actual" != "$expected" ]; then
    err "Checksum mismatch for $(basename "$file")"
    err "  expected: $expected"
    err "  actual:   $actual"
    return 1
  fi
  ok "SHA256 verified"
}

detect_platform() {
  os="$(uname -s)"
  arch="$(uname -m)"
  case "$os" in
    Darwin) OS_NAME="darwin" ;;
    Linux)  OS_NAME="linux" ;;
    *)      err "Unsupported OS: $os"; exit 1 ;;
  esac
  case "$arch" in
    arm64|aarch64) ARCH_NAME="arm64" ;;
    x86_64|amd64)  ARCH_NAME="x86_64" ;;
    *)             ARCH_NAME="$arch" ;;
  esac
}

resolve_latest_tag() {
  json="$(fetch_stdout "https://api.github.com/repos/${REPO}/releases/latest" 2>/dev/null || true)"
  if [ -z "$json" ]; then
    return 1
  fi
  tag="$(printf '%s\n' "$json" | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)"
  [ -n "$tag" ] || return 1
  printf '%s\n' "$tag"
}

TMP_DIR="$(mktemp -d 2>/dev/null || mktemp -d -t sdlk-install)"
cleanup() { rm -rf "$TMP_DIR"; }
trap cleanup EXIT INT TERM

install_from_release() {
  tag="$1"
  ver="${tag#v}"
  archive="sdl-kitty-${ver}-${OS_NAME}-${ARCH_NAME}.tar.gz"
  base_url="https://github.com/${REPO}/releases/download/${tag}"

  info "Downloading ${archive} (${tag})..."
  if ! fetch "${base_url}/${archive}" "${TMP_DIR}/${archive}"; then
    return 1
  fi

  if fetch "${base_url}/SHA256SUMS" "${TMP_DIR}/SHA256SUMS" 2>/dev/null; then
    expected="$(awk -v f="$archive" '$2 == f || $2 == ("*" f) {print $1; exit}' "${TMP_DIR}/SHA256SUMS")"
    if [ -n "$expected" ]; then
      verify_sha256 "${TMP_DIR}/${archive}" "$expected" || return 1
    fi
  fi

  mkdir -p "${TMP_DIR}/extract"
  tar -xzf "${TMP_DIR}/${archive}" -C "${TMP_DIR}/extract"

  # Tarball contains a single top-level directory (sdl-kitty-<ver>-<os>-<arch>/)
  staged="$(find "${TMP_DIR}/extract" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
  if [ -z "$staged" ] || [ ! -x "${staged}/bin/sdlk" ]; then
    err "Release archive did not contain bin/sdlk"
    return 1
  fi

  rm -rf "$INSTALL_DIR"
  mkdir -p "$(dirname "$INSTALL_DIR")"
  mv "$staged" "$INSTALL_DIR"
  ok "Installed ${archive} to ${INSTALL_DIR}"
  return 0
}

install_from_source() {
  for tool in git cmake; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      err "'$tool' is required to build from source."
      exit 1
    fi
  done

  info "Cloning https://github.com/${REPO}.git (with submodules)..."
  git clone --depth 1 --recurse-submodules --shallow-submodules \
    "https://github.com/${REPO}.git" "${TMP_DIR}/src"

  info "Configuring & building sdl-kitty (Release)..."
  gen_args=""
  if command -v ninja >/dev/null 2>&1; then
    gen_args="-G Ninja"
  fi
  # shellcheck disable=SC2086
  cmake -S "${TMP_DIR}/src" -B "${TMP_DIR}/build" $gen_args \
    -DCMAKE_BUILD_TYPE=Release \
    -DSDL_KITTY_RELOCATABLE=ON \
    -DSDL_KITTY_BUILD_SDL_TESTS=OFF

  jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
  cmake --build "${TMP_DIR}/build" -j "$jobs"

  rm -rf "$INSTALL_DIR"
  cmake --install "${TMP_DIR}/build" --prefix "$INSTALL_DIR"
  ok "Built and installed sdl-kitty into ${INSTALL_DIR}"
}

printf '\n%ssdl-kitty installer%s (%s)\n' "$BOLD" "$RESET" "$REPO"
detect_platform
info "Detected platform: ${OS_NAME}-${ARCH_NAME}"

installed=0
if [ "$FROM_SOURCE" -eq 0 ]; then
  if [ -z "$VERSION" ]; then
    VERSION="$(resolve_latest_tag || true)"
  fi
  if [ -n "$VERSION" ]; then
    if install_from_release "$VERSION"; then
      installed=1
    else
      warn "Prebuilt archive unavailable for ${OS_NAME}-${ARCH_NAME} (${VERSION}); falling back to source build."
    fi
  else
    warn "No published GitHub release found yet; building from source."
  fi
fi

if [ "$installed" -eq 0 ]; then
  install_from_source
fi

# Symlink sdlk into BIN_DIR
mkdir -p "$BIN_DIR"
ln -sf "${INSTALL_DIR}/bin/sdlk" "${BIN_DIR}/sdlk"
chmod +x "${INSTALL_DIR}/bin/sdlk"
ok "Linked ${BIN_DIR}/sdlk"

# PATH check
case ":${PATH}:" in
  *":${BIN_DIR}:"*) ;;
  *)
    warn "${BIN_DIR} is not in your PATH."
    printf '  Add this to your shell profile (~/.zshrc or ~/.bashrc):\n'
    printf '    export PATH="%s:$PATH"\n\n' "$BIN_DIR"
    ;;
esac

if [ "$NO_SETUP" -eq 0 ]; then
  setup_args=""
  [ "$AUTO_YES" -eq 1 ] && setup_args="$setup_args --yes"
  [ "$NO_MAME" -eq 1 ]  && setup_args="$setup_args --no-mame"
  [ "$NO_ROMS" -eq 1 ]  && setup_args="$setup_args --no-roms"
  # Re-attach stdin to /dev/tty if available (handles `curl ... | sh`)
  if [ -r /dev/tty ] && [ -t 1 ]; then
    # shellcheck disable=SC2086
    "${INSTALL_DIR}/bin/sdlk" setup $setup_args </dev/tty
  else
    # shellcheck disable=SC2086
    "${INSTALL_DIR}/bin/sdlk" setup --no-launch $setup_args
  fi
fi
