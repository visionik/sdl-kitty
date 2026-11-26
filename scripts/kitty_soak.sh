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
      # Rough split: ≤1.3.1 vs tip (the ghostty-1.3 row in terminals.toml).
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

# id|label|kind|payload|hint
#   kind=bin   → build/<payload>
#   kind=ask   → scripts/sdl-kitty-ask.sh (8-option card; exit = choice)
DEMOS=(
  "solid|solid_present|bin|solid_present|full-frame color smoke"
  "static|static_present|bin|static_present|identical frames / upload_skip"
  "dirty|dirty_rects_demo|bin|dirty_rects_demo|sparse dirty + periodic tint"
  "dvd|dvd_sprite|bin|dvd_sprite|logo motion + dirty"
  "bounce|dvd_bounce|bin|dvd_bounce|bounce variant"
  "alpha|alpha_blit|bin|alpha_blit|RGBA translucent discs"
  "fw|fireworks|bin|fireworks|rockets over lake; q quits"
  "paint|mouse_paint|bin|mouse_paint|left-drag paint; q quits"
  "keys|key_probe|bin|key_probe|keyboard + TEXT input harness"
  "ask|kitty-ask|ask|soak8|ImGui ask card (8 options → exit 0..7 / 255)"
)
ASK_SH="${ROOT}/scripts/sdl-kitty-ask.sh"

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

run_bin() {
  local bin="$1" log="$2"
  export SDL_KITTY_DEBUG="${SDL_KITTY_DEBUG:-1}"
  set +e
  if [[ "$SECONDS_LIMIT" -gt 0 ]]; then
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
  return "$rc"
}

run_ask() {
  local log="$1"
  if [[ ! -x "$ASK_SH" ]]; then
    echo "missing $ASK_SH" >&2
    return 1
  fi
  if [[ ! -x "${BUILD}/sdl_kitty_ask" ]]; then
    echo "binary not built: ${BUILD}/sdl_kitty_ask" >&2
    echo "  cmake --build build --target sdl_kitty_ask" >&2
    return 1
  fi
  set +e
  "$ASK_SH" \
    "Pick a soak demo id (exit code = 0-based index):" \
    "solid" "static" "dirty" "dvd" "bounce" "alpha" "fireworks" "paint" \
    2>>"$log"
  local rc=$?
  set -e
  return "$rc"
}

run_one() {
  local id="$1"
  local row label kind payload hint log bin rc=0
  row="$(find_demo "$id")" || {
    echo "unknown demo id: $id (try --list)" >&2
    return 1
  }
  label="$(demo_field "$row" 2)"
  kind="$(demo_field "$row" 3)"
  payload="$(demo_field "$row" 4)"
  hint="$(demo_field "$row" 5)"

  mkdir -p "$LOG_DIR"
  log="${LOG_DIR}/${id}.log"
  {
    echo "=== soak host=${HOST} demo=${id} (${label}) kind=${kind} $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="
    echo "hint: ${hint}"
    echo "payload: ${payload}"
    echo "TERM_PROGRAM=${TERM_PROGRAM:-} TERM=${TERM:-} VER=${TERM_PROGRAM_VERSION:-}"
    echo "SDL_KITTY_PROFILES=${SDL_KITTY_PROFILES:-}"
  } | tee "$log"

  echo ""
  echo "▶ ${label}  [${id}]  — ${hint}"
  echo "  log: ${log}"
  case "$kind" in
    ask)  echo "  OK → exit 0..7 (choice); Esc → 255; then back to menu" ;;
    *)    echo "  quit the demo (q / Esc) to return to the menu" ;;
  esac
  echo ""

  set +e
  case "$kind" in
    bin)
      bin="${BUILD}/${payload}"
      if [[ ! -x "$bin" ]]; then
        echo "binary not built: $bin" >&2
        echo "  cmake --build build --target ${payload}" >&2
        set -e
        return 1
      fi
      run_bin "$bin" "$log"
      rc=$?
      ;;
    ask)
      run_ask "$log"
      rc=$?
      ;;
    *)
      echo "unknown demo kind: $kind" >&2
      set -e
      return 1
      ;;
  esac
  set -e

  {
    echo "=== exit ${rc} $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="
    if [[ "$kind" == "ask" ]]; then
      echo "ask exit=${rc} (0..7 = choice index, 255 = cancel)"
    fi
    if command -v rg >/dev/null 2>&1; then
      echo "--- snippets ---"
      rg -n 'init term=|transmit_probe|frame_edit|keyboard_protocol|WARN|FAIL|quit|ask exit' "$log" | tail -40 || true
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
    printf "  %2d) %-18s  %s\n" "$i" "$(demo_field "$row" 2)" "$(demo_field "$row" 5)"
    i=$((i + 1))
  done
  echo "   a) all      run graphics demos in order (skip ask)"
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
          # Ladder is for FB/graphics smoke; skip interactive ask.
          if [[ "$(demo_field "$row" 3)" == "bin" ]]; then
            run_one "$(demo_field "$row" 1)" || true
          fi
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
export SDL_KITTY_PROFILES="${SDL_KITTY_PROFILES:-${ROOT}/config/sdl-kitty.toml}"

case "$MODE" in
  list)
    echo "host=${HOST}"
    for row in "${DEMOS[@]}"; do
      printf "  %s\t%s\t%s\t%s\n" "$(demo_field "$row" 1)" "$(demo_field "$row" 2)" "$(demo_field "$row" 3)" "$(demo_field "$row" 5)"
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
      if [[ "$(demo_field "$row" 3)" == "bin" ]]; then
        run_one "$(demo_field "$row" 1)" || true
      fi
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
