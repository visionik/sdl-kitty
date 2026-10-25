# Kitty keyboard (sdl-kitty)

Plan: great keyboard for games **and** text apps, verified on **Rex + Ghostty**.

## Ghostty soak (2026-10-23)

`key_probe` on Ghostty.app: digits `5`/`1` (`sc=34`/`30`), arrows, Space, Enter, `z`/`x`/`c`, `q` — all CSI-u press+release. Quit via `q` works. Focus gained/lost chatter at startup is harmless noise (same class as Rex).

## Kitty.app soak (2026-10-23)

Caps: `a=q` / `a=f` OK; sdl-kitty chose `shm` + `dirty_bw=on`, fb≈3360×2100.  
`key_probe`: `5`/`1`, arrows, `z`/`x`/`c`, `q` — CSI-u press+release (same as Ghostty/Rex).

## Rex capture (2026-10-23)

With `CSI > 11 u`, physical keys on Rex arrive on stdin as:

| Key | Press | Release |
|---|---|---|
| `5` | `CSI 53 u` (no `:1`) | `CSI 53;1:3u` |
| `1` | `CSI 49 u` | `CSI 49;1:3u` |
| arrows | legacy `CSI 1;1:1 A\|B\|C\|D` | `CSI 1;1:3 …` |
| `q` / Space / Enter / Esc | `CSI <code> u` | `…;1:3u` |
| Left Shift | `CSI 57441;2u` | `CSI 57441;1:3u` |

Omitted event-type subfield means **press**, not “no releases”. The driver must not arm min-hold auto-up for CSI-u.

**CSI-u length bug (fixed):** `KITTY_HandleCSI` copied the final byte into `seq` then called `ParseKittyKey(seq, seq_len + 1)`, so the trailing-`u` check saw `'\0'` and dropped every CSI-u event (digits, letters, Esc). Legacy arrows (`A`/`B`/`C`/`D`) were unaffected.

## Progressive enhancement

On enter, the driver sends:

```text
CSI > 27 u          # flags 1|2|8|16 — disambiguate, event types, all keys, associated text
CSI ? u             # probe: expect CSI ? <flags> u → kitty_keyboard_ok
```

On leave:

```text
CSI < u             # pop mode stack
```

See [Kitty keyboard protocol](https://sw.kovidgoyal.net/kitty/keyboard-protocol/).

If `CSI ? u` is unanswered, `kitty_keyboard_ok` stays false: legacy CSI arrows use min-hold auto-up (not sticky waits for `:3`). WezTerm also logs a one-time warning (see below).

| Flag | Bit | Meaning |
|---|---|---|
| 1 | disambiguate | Esc / ctrl+letter as CSI-u |
| 2 | event types | press / repeat / release |
| 8 | all keys as CSI | digits/letters get press+release (needed for games) |
| 4 | alternate keys | optional later |
| 16 | associated text | Phase 3 — codepoints in CSI-u → `SDL_SendKeyboardText` when text input is active |

## tty_in / tty_out

On Rex, keyboard bytes arrive on **stdin** even when stdin and stdout are the same tty device node. The driver keeps:

- `tty_in` — `STDIN_FILENO` when it is a tty (else stdout)
- `tty_out` — `STDOUT_FILENO` for APC/CSI writes
- `tty_fd` — alias of `tty_out` for write helpers

PumpEvents, termios, and query replies read `tty_in`.

## Hold policy

MAME (and similar) drain the SDL event queue then sample `SDL_GetKeyboardState`. Legacy keys with no release would vanish in one pump.

- When `kitty_keyboard_ok` (or a CSI with an `:event` subfield): real press / repeat / release (`:3`)
- Legacy / ASCII with no release expected: min-hold auto-up — profile `key_hold_ms` (default **80 ms**); **arrows / home / end / page** use at least **400 ms** so OS key-repeat delay does not feel like a dead stick

## Focus

`CreateWindow` may emit focus before some apps register the window id. The pump re-announces focus once (`focus_announced`) so late listeners still see `INPUT_FOCUS`.

## Phase 3 — associated text / `SDL_EVENT_TEXT_INPUT`

With flag **16** enabled (`CSI > 27 u`), hosts that support it embed text as Unicode codepoints in the third CSI-u field (e.g. Shift+A → `CSI 97;2;65u`). On press/repeat the driver calls `SDL_SendKeyboardText` (no-op unless the app called `SDL_StartTextInput`).

Legacy ASCII bytes (Kitty keyboard off) also feed text input for printable `0x20–0x7e`.

Verify with `key_probe` (already calls `SDL_StartTextInput`): type letters and look for `TEXT '…'` on stderr / the green accum bar.

## WezTerm: `enable_kitty_keyboard`

WezTerm defaults **`enable_kitty_keyboard = false`**. Until that is true, it **ignores** our `CSI > 11 u` push and stays on Xterm encoding (key-down + OS autorepeat only; **no key-ups** on the PTY). This looked like “arrows die after 1–2 s” under MAME when the client waited forever for `:3`.

At setup the driver probes with **`CSI ? u`**. A `CSI ? <flags> u` reply sets `kitty_keyboard_ok`. On WezTerm with no reply:

- One **`SDL_LogWarn`**: set `enable_kitty_keyboard = true` in `wezterm.lua`
- Legacy arrows use **min-hold auto-up** (≥400 ms) so games stay playable (some OS-repeat lag)

```lua
-- ~/.config/wezterm/wezterm.lua
local wezterm = require 'wezterm'
local config = wezterm.config_builder()
config.enable_kitty_keyboard = true
return config
```

**Soak (2026-10-25):** with the flag on → `keyboard_protocol ok`, Galaga hold-left smooth, KEY_DEBUG shows `event=1` / `event=3`. With the flag off → warn + `keyboard_protocol off`, playable with OS-repeat lag.

## Debug

```sh
export SDL_KITTY_KEY_DEBUG=1
```

Logs `kitty key sc=… event=… via=csi|legacy` (and harness lines). Needs `-DSDL_KITTY_RUNTIME_DEBUG=ON`.

## Phase 0 probes (run on Rex and Ghostty)

### Raw tty (no SDL)

```sh
python3 scripts/kitty_key_raw_probe.py --flags 11 --seconds 30
```

Press: `5` `1` arrows `q` `Esc` `Shift+A` `Ctrl+C` Space Enter.  
Quit: Esc Esc. Compare hex dumps across hosts.

Raw mode: probe logs use **CRLF** (`\r\n`). Plain LF alone staircases on the tty.

### SDL harness

If a prior `kill -9` left the pane wedged (raw/alt-screen), run `reset` first.

```sh
# stderr must leave the graphics tty; line-buffering is on in key_probe
scripts/run-with-kitty-sdl.sh build/key_probe 2>/tmp/key_probe.err
```

Soft surface in the pane; events in the file (`tail -f /tmp/key_probe.err`).  
Quit: `q` or `Esc` (Ctrl+C works too).  
Expect DOWN/UP for digits and arrows before trusting MAME.

### MAME (after harness is green)

```sh
# from ~/Projects/mame
MAME_KITTY_ERR=/tmp/mame-kitty.err SDL_KITTY_KEY_DEBUG=1 ./mame-kitty.sh pacman
```

## Checklist

See session plan “Great keyboard support for sdl-kitty” — games + text + robustness gates on Rex and Ghostty.
