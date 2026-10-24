# sdl-kitty TODOs

Living list. Add new items as we go; mark done with date when closed.

---

## Rex SHM / Ghostty (2026-10-24)

Live `a=q,t=s` on Rex returns **`EINVAL: unsupported medium`** (direct `a=q` → OK). So SHM is **disabled in the host**, not a client race.

Ghostty source (`~/Projects/ghostty`):

- SHM loader: `src/terminal/kitty/graphics_image.zig` `readSharedMemory` — `shm_open` → mmap → copy → **terminal** `shm_unlink`
- Enabled via `Limits.shared_memory` (`allWithTempDir` = on; `Limits.direct` = **off**)
- Full Ghostty.app Termio sets `.allWithTempDir(...)` (SHM on)
- Embedders (Rex/libghostty) expose `GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_MEDIUM_SHARED_MEM`; default for screens is often direct-only unless the embedder opts in
- Protocol: **client must not unlink before the terminal opens**; Ghostty unlinks after open. Our `TransmitShm` still always-unlinks after send (bug for Kitty.app / Ghostty.app) — probe already leaves the name on success
- `a=f` in Ghostty is **animation-frame** transmit (incl. SHM medium when enabled), composed into an existing image

Ghostty.app soak (1.3.1, 2026-10-24): caps direct+SHM OK; `solid_present` SHM OK. **a=f is unimplemented in 1.3.1** (returns `ERROR: unimplemented action`); real animation is tip PR #13943 (merged 2026-08-21, after 1.3.1 cut). Profile: `prefer_direct=false`, `silent_af_ok=false`, `direct_af_when_shm=false` → dirty_bw off, full a=T (works). Revisit dirty_bw on tip / next release.

- [ ] Ask Rex to enable Kitty shared-memory medium (or document why it’s off)
- [ ] Fix `TransmitShm` / probe: never `shm_unlink` on success; only on create failure / abandoned object
- [ ] Keep Rex `prefer_direct` until host SHM is actually enabled
- [x] Ghostty 1.3.1: diagnose a=f (unimplemented; tip #13943)
- [x] Ghostty 1.3.1 soak: alpha, fireworks, galaga (full SHM a=T; smooth)
- [ ] Ghostty tip/next: re-soak dirty_bw once animation ships in a release

---

## Focus now: Rex (2026-10-23)

Park Kitty.app graphics polish until Rex soaks are solid. Use Rex as the day-to-day host; re-open Kitty with a deliberate checklist later.

### Rex soak checklist

Run from repo root: `SDL_VIDEO_DRIVER=kitty ./build/<demo>` (debug: `SDL_KITTY_DEBUG=1 … 2>/tmp/<demo>.err`).

| Demo | What “good” looks like |
|---|---|
| `solid_present` | Solid colors; `term=rex prefer_direct`; clean exit (no `Gi=…;OK` on next prompt) |
| `dirty_rects_demo` | Sparse `dirty_bw` after first full frame |
| `dvd_sprite` | Logo moves; no base64; `dirty_bw` or small uploads |
| `fireworks` | Rockets + Space bursts; high cov stays on `a=f` (Rex profile 0.95); little flash |
| `alpha_blit` | Translucent discs over grid (`FB_ALPHA=1`); prefer direct `a=f` / multi-rect |
| `key_probe` | Physical **5 / 1 / arrows** press+release (don’t trust rex send-key for digits) |
| soft MAME | Keyboard + soft present OK on Rex |

- [ ] Re-run the table after each driver change that touches present/transmit/keyboard
- [ ] Keep `docs/terminals.md` Rex soak rows current

---

## Kitty.app — findings (parked)

**Do not chase these until Rex checklist is green again.** Summary of 2026-10-23 `alpha_blit` / present experiments:

| Observation | Detail |
|---|---|
| Uncapped / large FB + **direct** | Multi-chunk APC → **base64 wall** in the pane (`dvd_sprite`, forced `TRANSMIT=direct`) |
| **SHM `a=T`** | Probe OK; full presents work; mid-run SHM→direct fallback was dangerous (now refused when profile prefers SHM) |
| **SHM `a=f` (`t=s`)** | Writes “succeed” but image goes **blank**; `frame_edit_probe` fails on Kitty |
| **Direct `a=f`** | Can look good briefly; large / many RGBA rects at ~60Hz → **base64 wall** again |
| **Union dirty bbox** | Far-apart discs → 400–800k px union → either skip `a=f` (full SHM every frame = **black flash**, alpha looks wrong) or giant direct `a=f` (soup) |
| **Multi-rect direct `a=f`** | Built for sparse discs; still too much RGBA base64 on ~1080p Kitty at 60Hz |
| **Policy now** | On SHM sessions, **skip direct `a=f`** → full SHM `a=T` (no soup; flash / weak alpha on `alpha_blit`) |
| **Banners on tty** | stderr under Kitty flashes through pane — fireworks uses `/tmp` log when DEBUG / silent when tty |

### Kitty reopen plan (later)

- [ ] Confirm whether Kitty documents / supports `a=f` + `t=s`; if not, never probe that path
- [ ] Fix or accept `frame_edit_probe` false negatives (`silent_af_ok` / timeout / reply parse)
- [ ] Safe sparse path on Kitty: SHM-only pixel transport (no direct base64), or much lower RGBA dirty rate / smaller FB for `alpha_blit`
- [ ] Optional: tile-diff + SHM full present at capped rate to cut flash
- [ ] Re-soak: `solid_present` → `alpha_blit` → `fireworks` with DEBUG logs; update `docs/terminals.md`

---

## Per-terminal profile (done baseline 2026-10-23)

- [x] **`docs/terminals.md`** features/fixes matrix
- [x] **`KITTY_TerminalProfile`** + detect (`SDL_kittyprofile.c`)
- [x] Wire FB default cap, transmit prefer, max direct bytes, `a=f` coverage/px, silent `a=f`, Rex ioctl ignore, keys_on_stdin
- [x] Drain APC OK on restore; skip redundant CSI home; honor forced SHM; no giant direct fallback on SHM hosts
- [x] **Phase 1 TOML** — `config/terminals.toml` + `docs/terminal-profiles.md` (defaults + sparse `[[terminal]]`; full key inventory)
- [x] **Phase 2** — tomlc17 under `src/video/kitty/tomlc17/`; `SDL_kittyprofile_load.c`; `SDL_KITTY_PROFILES` + pref/base/default paths; expanded `KITTY_TerminalProfile`; `kitty_profile_utils_test`; builtin fallback
- [x] **Phase 3** — `scripts/terminals_toml_to_md.py`; generated matrix in `docs/terminals.md`; wrappers set `SDL_KITTY_PROFILES`
- [x] **Phase 4** — wire §B/§C into driver (`ignore_ioctl_pixels`, `direct_af_when_shm`, `direct_fallback_max_bytes`, chunk/probe/drain/stub/key_hold/fb_diff/place_cells/pingpong/window_fallback/`shm_unlink_after_send`; ProbeSupport via loaded matchers)
- [x] **Phase 5** — document user pref-path overlay (`docs/terminal-profiles.md`); optional live soak when Rex enables SHM
- [ ] Soak-driven number tweaks (WezTerm row; Kitty reopen per section above)
- [ ] Optional unit test for `KITTY_DetectTerminalProfile` / TOML load

## Larger framebuffers

**Context (2026-10-23):** Default FB cap is **per host** via `KITTY_TerminalProfile` (Rex/unknown/WezTerm 1280×720; Kitty/Ghostty 1920×1080). Uncapped Kitty ~3k×2k caused SHM→direct base64 dumps. See `docs/terminals.md`.

**Goal:** Support larger FBs safely without APC/base64 floods.

- [ ] **Transport-aware caps** — Soft-max by mode (SHM vs direct), not only a single global
- [ ] **Probe max safe `a=T` size** — Per-host ladder; cache for the session
- [ ] **SHM failure policy** — Partially done (refuse giant direct on non-`prefer_direct` hosts); still: retry capped / zlib / hard-error UX
- [ ] **Raise default when SHM OK** — After proven soaks
- [ ] **Sparse upload at large FB** — Multi-rect `a=f` exists for direct hosts; Kitty needs a SHM-safe sparse story
- [ ] **Tune a=f thresholds** — Profile fields; Rex high (0.95 / 720p); Kitty tight (0.40 / 160k) + no direct `a=f` on SHM sessions
- [ ] **Soak ladder** — Prefer **Rex first**; Kitty later
- [ ] **Docs** — `SDL_KITTY_FB_MAX` / `FB_FIXED` / `FB_MAX=0` risk note

**Knobs today:** `SDL_KITTY_FB_MAX=WxH` (or `0` uncapped), `SDL_KITTY_FB_FIXED=WxH`, `SDL_KITTY_TRANSMIT=shm|direct|zlib`

---

## Better test plan (draft)

1. **One host at a time** — Rex until checklist green; then Ghostty; then Kitty.
2. **One variable at a time** — don’t combine FB_ALPHA + dirty_bw force + transmit force in one experiment.
3. **Always DEBUG to a file** — `SDL_KITTY_DEBUG=1 … 2>/tmp/<demo>-<host>.log`; never banners on the graphics tty.
4. **Log expectations** — `term=`, `transmit_probe`, `frame_edit_probe`, `mode=dirty_bw|dirty_cpu|full`, no `falling back to direct` on Kitty unless intentional.
5. **Pass/fail is visual + log** — base64 wall = fail; blank after `dirty_bw` = fail; flash-only may be known limitation (note it).

- [ ] Optional: `scripts/rex_soak.sh` wrapping the Rex checklist into one run + log dir

---

## Backlog (add below)

- [ ] Keyboard Phase 3 — text input (`SDL_SendKeyboardText`, flag 16 / associated text)
- [ ] WezTerm host soak (caps + `key_probe` + soft demo)
- [ ] FOCUS_GAINED/LOST chatter at startup — tighten if it bothers apps
- [ ] Optional: drop MAME `-background_input` once focus is proven solid without it
