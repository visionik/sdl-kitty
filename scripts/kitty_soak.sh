#!/usr/bin/env bash
# Host-agnostic sdl-kitty soak: menu or one-shot demos on whatever Kitty-capable
# terminal you are in (Rex, Kitty.app, Ghostty, WezTerm, …).
#
#   ./scripts/kitty_soak.sh              # interactive menu (returns after each run)
#   ./scripts/kitty_soak.sh --list
#   ./scripts/kitty_soak.sh --once solid
#   ./scripts/kitty_soak.sh --all         # ladder; quit each demo to advance
#   ./scripts/kitty_soak.sh --host wezterm --once dirty
#
# Logs: /tmp/sdl-kitty-soak/<host>-<stamp>/
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build"
RUNNER="${ROOT}/scripts/run-with-kitty-sdl.sh"
STAMP="$(date +%Y%m%d-%H%M%S)"
HOST_OVERRIDE=""
MODE="menu"          # menu | once | all | list
ONCE_ID=""
SECONDS_LIMIT=0      # 0 = until process exits (user quit)

usage() {
  cat <<'EOF'
Host-agnostic sdl-kitty soak (Rex / Kitty / Ghostty / WezTerm / …).

  ./scripts/kitty_soak.sh              interactive menu (returns after each demo)
  ./scripts/kitty_soak.sh --list
  ./scripts/kitty_soak.sh --once solid
  ./scripts/kitty_soak.sh --all         ladder; quit each demo to advance
  ./scripts/kitty_soak.sh --host wezterm --once dirty
  ./scripts/kitty_soak.sh --seconds 20 --once fw

Logs: /tmp/sdl-kitty-soak/<host>-<stamp>/
Options: --host NAME  --once ID  --all  --list  --seconds N  -h
EOF
}

detect_host() {
  local tp="${TERM_PROGRAM:-}"
  local term="${TERM:-}"
  local ver="${TERM_PROGRAM_VERSION:-}"
  if [[ -n "$HOST_OVERRIDE" ]]; then
    echo "$HOST_OVERRIDE"
    return
  fi
  case "$tp" in
    rex|Rex) echo "rex"; return ;;
    WezTerm|wezterm) echo "wezterm"; return ;;
    kitty) echo "kitty"; return ;;
    ghostty|Ghostty)
      # Rough split: ≤1.3.1 vs tip (same idea as terminals.toml first-match).
      if [[ -n "$ver" ]] && printf '%s\n' "$ver" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' \
        && [[ "$(printf '%s\n' "$ver" | awk -F. '{print ($1*10000+$2*100+$3)}')" -le 10301 ]]; then
        echo "ghostty-1.3"
      else
        echo "ghostty"
      fi
      return
      ;;
  esac
  case "$term" in
    *kitty*) echo "kitty"; return ;;
    *ghostty*) echo "ghostty"; return ;;
    *wezterm*) echo "wezterm"; return ;;
  esac
  if [[ -n "${KITTY_WINDOW_ID:-}" ]]; then
    echo "kitty"
    return
  fi
  echo "unknown"
}

# id|label|binary_relative_to_build|hint
DEMOS=(
  "solid|solid_present|solid_present|full-frame color smoke"
  "static|static_present|static_present|identical frames / upload_skip"
  "dirty|dirty_rects_demo|dirty_rects_demo|sparse dirty + periodic tint"
  "dvd|dvd_sprite|dvd_sprite|logo motion + dirty"
  "bounce|dvd_bounce|dvd_bounce|bounce variant"
  "alpha|alpha_blit|alpha_blit|RGBA translucent discs"
  "fw|fireworks|fireworks|rockets over lake; q quits"
  "paint|mouse_paint|mouse_paint|left-drag paint; q quits"
  "keys|key_probe|key_probe|keyboard + TEXT input harness"
)

demo_field() {
  local row="$1" n="$2"
  echo "$row" | cut -d'|' -f"$n"
}

find_demo() {
  local want="$1" row
  for row in "${DEMOS[@]}"; do
    if [[ "$(demo_field "$row" 1)" == "$want" ]]; then
      echo "$row"
      return 0
    fi
  done
  return 1
}

need_build() {
  if [[ ! -x "$RUNNER" ]]; then
    echo "missing $RUNNER" >&2
    exit 1
  fi
  if [[ ! -d "$BUILD" ]]; then
    echo "build/ missing — cmake -S . -B build && cmake --build build -j" >&2
    exit 1
  fi
}

run_one() {
  local id="$1"
  local row bin label hint log
  row="$(find_demo "$id")" || {
    echo "unknown demo id: $id (try --list)" >&2
    return 1
  }
  label="$(demo_field "$row" 2)"
  bin="${BUILD}/$(demo_field "$row" 3)"
  hint="$(demo_field "$row" 4)"
  if [[ ! -x "$bin" ]]; then
    echo "binary not built: $bin" >&2
    echo "  cmake --build build --target $(demo_field "$row" 3)" >&2
    return 1
  fi

  mkdir -p "$LOG_DIR"
  log="${LOG_DIR}/${id}.log"
  {
    echo "=== soak host=${HOST} demo=${id} (${label}) $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="
    echo "hint: ${hint}"
    echo "bin: ${bin}"
    echo "TERM_PROGRAM=${TERM_PROGRAM:-} TERM=${TERM:-} VER=${TERM_PROGRAM_VERSION:-}"
    echo "SDL_KITTY_PROFILES=${SDL_KITTY_PROFILES:-}"
  } | tee "$log"

  echo ""
  echo "▶ ${label}  [${id}]  — ${hint}"
  echo "  log: ${log}"
  echo "  quit the demo (q / Esc) to return to the menu"
  echo ""

  # DEBUG on stderr → log file. Keep stdout on the tty (Kitty APC graphics).
  export SDL_KITTY_DEBUG="${SDL_KITTY_DEBUG:-1}"
  set +e
  if [[ "$SECONDS_LIMIT" -gt 0 ]]; then
    # Portable soft timeout: background + sleep + kill (macOS has no timeout(1)).
    "$RUNNER" "$bin" 2>>"$log" &
    local pid=$!
    ( sleep "$SECONDS_LIMIT"; kill "$pid" 2>/dev/null; ) &
    local watchdog=$!
    wait "$pid" 2>/dev/null
    local rc=$?
    kill "$watchdog" 2>/dev/null || true
    wait "$watchdog" 2>/dev/null || true
  else
    "$RUNNER" "$bin" 2>>"$log"
    local rc=$?
  fi
  set -e

  {
    echo "=== exit ${rc} $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="
    if command -v rg >/dev/null 2>&1; then
      echo "--- snippets ---"
      rg -n 'init term=|transmit_probe|frame_edit|keyboard_protocol|WARN|FAIL|quit' "$log" | tail -40 || true
    fi
  } | tee -a "$log"

  echo ""
  echo "■ ${label} exited ${rc}. summary tail in ${log}"
  return 0
}

print_menu() {
  local i=1 row
  echo ""
  echo "sdl-kitty soak  host=${HOST}  log_dir=${LOG_DIR}"
  echo "────────────────────────────────────────"
  for row in "${DEMOS[@]}"; do
    printf "  %2d) %-8s  %s\n" "$i" "$(demo_field "$row" 1)" "$(demo_field "$row" 4)"
    i=$((i + 1))
  done
  echo "   a) all      run every demo in order (quit each to advance)"
  echo "   l) list     print ids"
  echo "   q) quit"
  echo "────────────────────────────────────────"
}

menu_loop() {
  local choice n row id
  while true; do
    print_menu
    printf "select> "
    if ! read -r choice; then
      echo
      break
    fi
    case "$choice" in
      q|Q|quit|exit) break ;;
      l|L|list)
        for row in "${DEMOS[@]}"; do
          printf "  %s\t%s\n" "$(demo_field "$row" 1)" "$(demo_field "$row" 4)"
        done
        ;;
      a|A|all)
        for row in "${DEMOS[@]}"; do
          run_one "$(demo_field "$row" 1)" || true
        done
        ;;
      '' ) continue ;;
      *)
        if [[ "$choice" =~ ^[0-9]+$ ]]; then
          n=$((choice))
          if (( n >= 1 && n <= ${#DEMOS[@]} )); then
            row="${DEMOS[$((n - 1))]}"
            run_one "$(demo_field "$row" 1)" || true
          else
            echo "out of range"
          fi
        else
          # allow typing the id
          if find_demo "$choice" >/dev/null; then
            run_one "$choice" || true
          else
            echo "unknown selection: $choice"
          fi
        fi
        ;;
    esac
  done
}

# --- argv ---
while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --list) MODE=list; shift ;;
    --all) MODE=all; shift ;;
    --once)
      MODE=once
      ONCE_ID="${2:-}"
      if [[ -z "$ONCE_ID" ]]; then echo "--once needs an id" >&2; exit 1; fi
      shift 2
      ;;
    --host)
      HOST_OVERRIDE="${2:-}"
      if [[ -z "$HOST_OVERRIDE" ]]; then echo "--host needs a name" >&2; exit 1; fi
      shift 2
      ;;
    --seconds)
      SECONDS_LIMIT="${2:-0}"
      shift 2
      ;;
    *)
      echo "unknown arg: $1" >&2
      usage
      exit 1
      ;;
  esac
done

need_build
HOST="$(detect_host)"
LOG_DIR="/tmp/sdl-kitty-soak/${HOST}-${STAMP}"
export SDL_KITTY_PROFILES="${SDL_KITTY_PROFILES:-${ROOT}/config/terminals.toml}"

case "$MODE" in
  list)
    echo "host=${HOST}"
    for row in "${DEMOS[@]}"; do
      printf "  %s\t%s\t%s\n" "$(demo_field "$row" 1)" "$(demo_field "$row" 2)" "$(demo_field "$row" 4)"
    done
    ;;
  once)
    mkdir -p "$LOG_DIR"
    echo "logs → ${LOG_DIR}"
    run_one "$ONCE_ID"
    ;;
  all)
    mkdir -p "$LOG_DIR"
    echo "logs → ${LOG_DIR}"
    for row in "${DEMOS[@]}"; do
      run_one "$(demo_field "$row" 1)" || true
    done
    echo "all done. logs → ${LOG_DIR}"
    ;;
  menu)
    mkdir -p "$LOG_DIR"
    echo "logs → ${LOG_DIR}"
    menu_loop
    echo "bye. logs → ${LOG_DIR}"
    ;;
esac
