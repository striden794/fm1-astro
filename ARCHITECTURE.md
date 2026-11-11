# ARCHITECTURE: AMB-1 as TIDE inherits it

Written in Phase 1 (2026-11-07) from the source and docs of AMB-1 v1.3.2 (charlesvestal/fm1-amb e642bf1, one
docs-only commit after the v1.3.2 tag), fm1-base 3ae2a65 and felucca-bsp f878800. Line numbers are those files'
as imported here. Nothing in `tide/` has been changed yet.

## 1. The tree

| Path | What | Licence |
|---|---|---|
| `app/dsp/amb.c`, `amb.h` | **The instrument.** One compilation unit at -O2: the four layers, the chord, the loops, the echo chain and the plate, the parameters and the eight pieces. Runs in the audio interrupt. | MPL-2.0 (Charles Vestal) |
| `app/ui.c` | The panel and the screen: pages, knobs, keys, help cards, autosave, the screen reader. Main loop. | MPL-2.0 |
| `app/project.c`, `app/app.h` | What is saved: the project record (every piece as it was left), its flash object, load/save, format migration. | MPL-2.0 |
| `app/app.c`, `app/host.c` | The glue to fm1-base's contract (`app_init`, `app_render`, `app_frame` ...), and the simulator's checks and commands. | MPL-2.0 |
| `app/fm1.toml`, `app/words.tsv` | Build settings (name, USB names, no radio, USB audio in, `amb.c` at -O2) and the screen reader's words. | |
| `base/` | **fm1-base**: the platform above the hardware (boot, audio, USB and USB audio, MIDI, flash store, screen, panel, crash handling, safe mode, update), the host simulator (`base/host/plat_host.c`), the browser emulator (`base/web/emu`), and the tools (`base/tools/build.py`, `fm1_install.py`, `fm1_backup.py` ...). A git submodule upstream, a plain directory here. | MPL-2.0 |
| `base/bsp/` | **felucca-bsp**: the chip and board layer (JieLi AC79, the FM-1 board), the update loader, `tools/fm1_rescue.*`. | MPL-2.0 |
| `web/app/` | The web app: the engine alone (`amb.c` + `amb_api.c`) in WebAssembly with its own page, for phones and desktops. | MPL-2.0 |
| `tests/` | `host/amb_test.c` (the engine), `scenarios/*.amb` (the whole app in the simulator), Node tests for the browser builds. | |

Note for CLAUDE.md: AMB-1 is **MPL-2.0 since 1.0**, not GPL-3.0 (0.9 and earlier were GPL), and it is built on
**fm1-base**, not directly on Felucca. MPL is file-by-file: a changed file that is shared must have its source shared.

## 2. Where things live

### Pieces (the presets)
- `app/dsp/amb.c:41-53`: `PIECES[8]`, one row of all 28 parameters per piece (Garden, Departures, Voices,
  Afternoon, Night Bus, Shoreline, Glass, Tides), plus the mode whose pool it loads (`en_piece`, 56-63).
- The parameter list and ranges: `PARAMS[]` at `amb.c:25-34` (`P_*` in `amb.h:49-59`).
- Choosing a piece: PRESETS encoder → `ui.c:441-444` → `set_piece` (`ui.c:283-292`) → `project_piece`
  (`project.c:140-145`, copies the slot's values into play) → `project_apply` → `en_set` per parameter →
  `en_regrow` (fresh loops, home chord: `amb.c:1244-1261`). Each piece keeps its own settings as left
  (`slot[8]`, `app.h:39`); Tune and MIDI out are global (`project.c:12`).

### The pad (and the "swell")
- Voice state `pad_t` and the four voices: `amb.c:385-402`. Four voices (`EN_NPAD 4`, `amb.h:37`), each one
  **breath** at a time: a note of the chord, a level envelope with a fixed shape (`breath()`, 404-413: raised-cosine
  rise over 40% of the cycle, a hold of 15%, a cosine fall of 35%, a rest of 10%).
- The breath's length is **the Swell knob**: `pad_breathe` (425-437): `(4 + 0.12·Swell) × BREATH[i] × (0.85..1.15)` s
  with `BREATH = {1, 1.21, 1.43, 1.67}` (399). At the default Swell 60: 11.2, 13.6, 16.0 and 18.7 s, ±15%.
  Its peak is random 0.6..1.0 (429). The next breath starts the instant one ends (491-495).
- Two colours per voice, set by the piece (`P_PADTONE`): **Rhodes**, a 2-operator FM pair at ratio 1:1 plus a
  second-harmonic sine (`render_pad`, 1497-1507), whose index rises with the breath (1495); **Voices**, a
  triangle + saw + sine through two state-variable formant filters (1510-1528) whose vowel is set by Bright, the arc
  and a slow wobble (`pad_coefs`, 502-508; vowel table 401).
- The pad bus: a 12 dB low-pass at `700 × 2^(Bright·0.03 + 0.6·arc)` Hz (`pad_bus`, 1444-1464), then 0.8 to the
  mix, 0.6 of (L+R) to the reverb and 0.2 to the echo.
- Chord changes and the pad: `pad_retarget` (585-598, voice leading in MIDI 48..77), `pad_leave` (457-470: a voice
  whose note the new chord lacks fades out in 1 s and breathes again), `pad_handoff` (443-453: on a new key, root,
  scale or colour, every sounding voice does so).

### Note generation
- **Harmony**: one pool (scale, 12-bit mask relative to the root) and one chord degree `deg`. `chord_change`
  (621-645) picks another degree by fixed weights (`W7`), retargets the pad, moves the bass, and sometimes rests the
  bass. It is scheduled by **Drift**: `drift_secs(v) = 20 × 2^(v·0.039)` s (86: 20 s at 0, 39 s at the default 25,
  5 min at 100), × a random 0.7..1.3 (616), counted down in `control()` (1735-1744), executed on the next bar
  (`tick`, 916-919). Halfway through, the bass may lean on the fifth (1737-1741).
- **Bells**: four tape loops of 13, 17, 23 and 29 quarter notes (`EN_LOOP_LEN`, 22), each slot a scale offset
  above the chord's degree (`loop_note[][]`, 540; the note resolved against the current chord at ring time,
  `bell_note` 573-583). Stepped in quarter notes by `tick` (904-915). At the end of each pass `loop_pass`
  (711-732) grows or prunes to the Density target (`loop_target` 665-669: 0..5 notes × (1 + 0.8·arc)), may rest
  the loop for a pass, and with **Evolve** moves or changes one note. Six bell voices (`NBELL 6`, 532), each a
  2-operator FM pair (1595-1597), the quietest stolen (738-749); ratio from Bell tone (758-759), decay 1.6..5 s by
  pitch (761-763), up to 60 ms of human lateness (780), now and then an octave doubling (782-783).
- **Bass**: one voice, a sine with its sub-octave and second harmonic (1561), on the chord's root in C2..B2
  (`bass_to`, 600-609), gliding (1558), resting some chords (643).
- **Pulse**: three drums (thump, closed hat, open hat) on Euclidean patterns of 16, 12 and 7 steps
  (`drums_pattern` 796-804, `en_euclid` 138-180), evolving by one hit or one rotation (`drums_evolve` 806-819),
  resting 2..4 bars at a time (888-892), swung (851-860), faded by the arc down to 0.3 (887).
- **The grid**: `tick()` once a sixteenth (884-922), driven by the tempo (`clock_pulse`, 1332-1340) or by MIDI
  clock in (`drain`, 1412-1426).
- **Slow motion**: nine random-target smoothers `mod[]` (355-376), a new target every 7..40 s, for pad pitch
  wobble (±8 cents × Motion), vowel, pan, tape wow and the echo heads; and **the arc** (1713-1731): one global
  value in −1..+1 × Motion with a new random target every 50..150 s, approached with a 35 s time constant.
- **Randomness**: one xorshift32 (`rnd`, 324-331), seeded by `en_seed` from `plat_ms()` when PLAY or a piece is
  pressed (`ui.c:287, 321`). Everything else is sample-exact, so a run is reproducible from the seed and the
  command log. This is what Phase 2's render harness needs.

### The echo and the space
- Sends: every layer adds to `rsend[]` (reverb) and `esend[]` (echo) per sample (pad 1459-1460, bells 1600-1601,
  bass 1569, drums 1649-1650, 1673). Send levels: Echo² and Reverb² (1189, 1191).
- **Echoplex**: a 160 ms int16 line with 0.55 feedback through a one-pole low-pass, a second tap at ~100 ms for
  the right (`echoes`, 1110-1120; 1041-1045).
- **Tape loop**: up to 5.6 s at a quarter rate (11.025 kHz, int16, 62000 samples: 1031-1033), synced to the tempo
  by Echo time (`echo_time`, 1067-1082: 1.5 to 16 beats), two heads (the second at 3/4, 2/3, 5/8, 1/2 or 3/8 of
  the loop, sliding by hand with Motion: 1085-1107), feedback `0.5 + 0.32·Size/100` (1194: 0.756 at Size 80)
  through a low-pass and a DC high-pass, each repeat darker (1130-1137).
- **Plate**: Dattorro's (`reverb`, 970-1003; lengths 929-936), decay `0.6 + 0.375·Size/100` (1193: 0.9 at Size 80),
  tank damping 0.45 one-pole (988, 994), 0.9995 while frozen (1732). Its input is the reverb sends plus 0.35 of the
  echo outputs (1704).
- **Output** (`render_chunk`, 1691-1710): mix + 0.75 tape + 0.3 Echoplex + 1.3 plate, × 0.36 × MASTER, then
  `tanh` soft clipping to 24 bits (1707-1708). There is no meter, compressor or limiter anywhere.

## 3. Voice and operator counts

| Layer | Voices | Per voice | Notes |
|---|---|---|---|
| Pad | 4, always sounding while playing | Rhodes: 2 FM operators + 1 sine (3 sine lookups). Voices: 3 oscillators (triangle, PolyBLEP saw, sine) + 2 SVFs | 2 low-pass poles on the bus per channel |
| Bells | 6 (stolen quietest) | 2 FM operators | Polyphony across 4 loops plus octave doublings |
| Bass | 1 | 3 sines | Glide |
| Pulse | 3 (one each) | Thump: 1 sine. Hats: 4 squares + noise + 2-pole HP | |
| Echo | Echoplex 7100 × int16 (RAM), tape 62000 × int16 (pool) | | |
| Plate | 34,706 floats (pool) = 135.6 KB | | Computed from `DL_LEN`, 933-936 |

Pool use by the engine: about 257 KB of the FM-1's 336 KB pool (plate 135.6 KB + tape 121.1 KB, `EN_POOL`
section, 10). The project record is 550 bytes (computed from `project_t`, `app.h:32-41`).

## 4. CPU budget and the guard

- The render runs per I2S half buffer of 256 frames = 5.8 ms at 44.1 kHz (`APP_BLOCK`, `app.h:11`; `HALF_FRAMES`,
  `base/device/main_fm1.c:13-16`). `plat_cpu_pct()` is the render's time as a percentage of that budget, smoothed
  over about 16 blocks (`main_fm1.c:110-113`); `plat_xruns()` counts overruns.
- **Guard: none in AMB-1.** fm1-base offers `APP_OVERLOAD` (`app_overload()` called when a block took over 90% of its
  budget: `main_fm1.c:105-108`, `base/include/plat.h:599`), and AMB-1's `app.h` does not define it. The only use of
  the CPU figure is display: the Setup page header shows it (`ui.c:669, 703`) and the screen reader speaks it
  (`ui.c:1148`). The manual does not mention it.
- The engine's cost is fixed per voice: there is no voice shedding. TIDE's rule 6 (peak under 70%, thin before
  glitches) needs `APP_OVERLOAD` plus its own continuous measure (NACHTFAHRT's 75% thinning is the model to study).
- Single core: `fm1.toml` has no `cpu2 = true`. The AC79's second core is available through fm1-base
  (`plat_cpu2_*`, `plat.h:518-540`), with a safe-mode fallback, if TIDE ever needs it.
- Flash: the app slot is 581 KB; AMB-1 1.3.2's app binary is 120,920 bytes (the release's `amb-1.3.2-app.bin`).

## 5. Save location and format

- One flash object, `OBJ_PROJ`, at offset **0xFC000** (its A/B sector pair 0xFC000 / 0xFD000), plus the panel
  calibration `OBJ_PANEL` at 0xDC000 (`APP_STORE_MAP`, `app.h:18-21`, "where AMB-1 0.9 kept them"). fm1-base's store
  (`base/device/storage.c`) writes each object alternately to its two 4 KiB sectors with a 32-byte CRC header, so a
  torn write keeps the previous copy; payloads up to 3840 bytes.
- The record (`project_t`, `app.h:32-41`): magic `"ENO1"` (0x314F4E45), format 4, the playing piece's 28 values and
  pool, lights, the piece number, then all eight pieces' slots (28 × int16 + pool each), 16 reserve bytes: 550 bytes.
  A format-3 record (0.9) is migrated (`project.c:79-113`); anything else gives the recipes (`project_load`, 115-132).
- Saving: SAVE saves now (`ui.c:338-345`); autosave a few seconds after a change **only while resting** (`autosave`,
  `ui.c:1007`), because a flash write stops the audio for a moment. SAVE held 1.5 s puts the piece back to its recipe
  without saving (`ui.c:445-454`).
- **Sharing with AMB-1's descendants**: DUB-1, NACHTFAHRT and AISLING inherit this map, so they all read and write
  0xFC000. The flash object carries the object number and the app checks its own magic, so a foreign record is simply
  rejected and replaced by defaults on the next save: that is how pieces get lost.
- **Giving TIDE its own slot** (rule 4): move `OBJ_PROJ` in `APP_STORE_MAP` to an unused pair in the data area
  (0x97000..0xDFFFF, `plat.h:346, 476`), e.g. 0xD8000 / 0xD9000, and change the magic (e.g. `"TID1"`). AMB-1's pieces
  at 0xFC000 then stay untouched by TIDE. Keep `OBJ_PANEL` at 0xDC000 so the panel calibration carries over. Back
  up anyway before the first flash: `python base/tools/fm1_backup.py` needs `backup = true` in the firmware that is
  running, which AMB-1 1.3.2 does not set, so the backup is "note the pieces' settings by hand or accept the loss".
  Phase 7 decides.

## 6. Knob and key mapping (AMB-1 as built)

Panel: 27 keys (F3..G5), 14 buttons (`B_*`, `plat.h:49-50`), 7 encoders (`EN_*`, `plat.h:51`: SELECT, ALGORITHM,
PRESETS, KNOB 1-4), the MASTER pot.

| Control | AMB-1 | Where |
|---|---|---|
| Keys | Toggle the key's pitch class in the scale (root stays, at least 3 notes) | `ui.c:412-414, 254-270` |
| PLAY | Grow / rest (acts on the press) | `ui.c:314-325` |
| REC | Freeze: chord held, plate rings on | `ui.c:326-329` |
| EDIT | Next chord on the bar | `ui.c:310-313` |
| HOME ENV ARP LFO SEQ FX GLO | Pages: Garden, Pad, Bells, Bass, Pulse, Space, Setup | `ui.c:18-20, 304-308` |
| SELECT | Previous / next page; with HOME held, Tempo | `ui.c:418-436` |
| ALGORITHM | Mode (loads its pool) | `ui.c:437-440, 272-281` |
| PRESETS | Piece | `ui.c:441-444` |
| KNOB 1-4 | The page's four values (table below) | `ui.c:455-461` |
| OCT− / OCT+ | Bells' octave ±2; both held 5 s: update mode (platform) | `ui.c:330-337, 866-867` |
| SAVE | Save; held 1.5 s: the piece back to its recipe | `ui.c:338-345, 445-454` |
| SEL | Nothing | `ui.c:366` |
| MASTER | Volume (gain = position², fm1-base's default `APP_GAIN`) | `plat.h` |
| MIDI in | Channel-1 notes toggle scale notes; clock, start, continue, stop drive the grid | `ui.c:352-362` |

Knobs per page (`VIEW_KNOB` / `KNOB_LABEL`, `ui.c:23-40`):

| Page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| Garden (HOME) | Tempo | Evolve | Motion | Drift |
| Pad (ENV) | Level | Swell | Bright | Colour (Rhodes/Voices) |
| Bells (ARP) | Level | Density | Tone | Octave |
| Bass (LFO) | Level | Octave | Glide | Fifth |
| Pulse (SEQ) | Level | Hits | Swing | Hats |
| Space (FX) | Echo | Echo time | Reverb | Size |
| Setup (GLO) | Root | Tune | MIDI out | Lights |

Power-on combinations (platform, manual §10): REC held = update mode (with m-bootloader, the installer reaches it;
PLAY held = m-bootloader's recovery menu); OCT− + OCT+ held = panel calibration.

**For TIDE's four knobs** (DESIGN §9): the natural fit is one main page where KNOB 1-4 are Mood, Density, Drift,
Space, with PRESETS (or the keys) choosing the landscape. The page system can stay for the quiet status display and a
setup page. Phase 6 decides.

## 7. Real-time clock

**No.** fm1-base's platform API has no time-of-day call: `plat_ms()` is milliseconds since boot (a 32-bit count,
wrapping every 49.7 days: `plat.h:60`), and `plat_ticks` a microsecond counter. The chip layer only switches the
AC79's "RTC domain" power bit on (`base/bsp/chip/fm1_sys.h:55`, the P33 always-on block); there is no RTC driver,
no 32 kHz crystal in the board files, no backup supply, and nothing (no network, no host clock) to set a time from
while the unit is offline. Seasons therefore follow **elapsed play time** (DESIGN §3). If wanted later, a time could
be set over USB MIDI (SysEx) from a computer and kept while powered, but it is lost at power-off.

## 8. Building on Windows

What this machine had on 2026-11-07: Python 3.14 with Pillow 12.3, Node 24, CMake, Visual Studio 18 (no
command-line compiler on PATH), **no** Docker, **no** WSL distribution, **no** gcc/clang/cc, **no** Emscripten.

### Done today, with nothing installed system-wide
Tooling lives in the project folder, outside `tide/`:
- `fm1-ambient/.venv` with the `ziglang` pip package (Zig 0.17.1, clang 22 inside): a C compiler.
- `fm1-ambient/tools/ccshim/cc.exe`: a 40-line stand-in for Unix `cc` that forwards to `zig cc`, because
  `base/tools/build.py` runs `subprocess.run(["cc", ...])`, which on Windows needs a real `.exe`. Rebuild with
  `sh tools/ccshim/build.sh`.
- Pillow's text layout (libraqm) needs a FriBidi DLL on PATH for `base/tools/gen_font.py`; GIMP 3's
  `libfribidi-0.dll` serves, appended to PATH for the build process only.

```sh
# Git Bash, from fm1-ambient/
export PATH="$PATH:/c/Users/tomorrowsman/AppData/Local/Programs/GIMP 3/bin"
export CC="$(pwd -W)/tools/ccshim/cc.exe" PYTHON=python
export PYTHONUTF8=1      # build.py rewrites firmware.json; without this, Windows' default code page mangles its "ü"
cd tide
python base/tools/build.py app --host        # app/build/host/amb_host (the whole app, scripted)
sh tests/run_tests.sh                        # engine test + 8 scenarios (emulator tests skipped: no emcc)
app/build/host/amb_host tests/scenarios/first.amb out/   # a WAV and screenshots from a script
```

The engine alone (what Phase 2's harness compiles) needs only `zig cc ... -DEN_HOST -Iapp/dsp -Ibase/include
app/dsp/amb.c` plus a driver, as `tests/host/amb_test.c` and `web/app/amb_api.c` do.

### The firmware package (.fwsc)
The JieLi pi32v2 toolchain (clang 4.0.1) is **Linux x86-64 only**. `build.py` runs it natively on Linux x86-64
and in a `linux/amd64` Debian container everywhere else (`use_docker`, `build.py:329-331`). On Windows that means
one of:
1. **WSL2 with Ubuntu** (recommended: everything runs natively, no container): `wsl --install -d Ubuntu`, then inside
   it `sudo apt install build-essential python3-pil python3-venv git`, `base/tools/get_toolchain.sh` (installs
   `~/.jieli/toolchain`), the AC79 SDK
   (`git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git
   ~/fw-AC79_AIoT_SDK`, Apache-2.0: three of its files go into the package), then `./build.sh`. The tree can stay on
   E: (`/mnt/e/...`), slower, or be cloned inside WSL.
2. Docker Desktop (the container route, as on macOS).
Both are system installs: **Tomorrow's Man's call** (WORKING_RULES). Needed by Phase 6 at the latest; the release workflow
(`.github/workflows/release.yml`, zvenson/fm1-build) is the reference build if byte-identity with a release is ever
needed.

### The browser emulator (`build.py --web`) and the web app
Need Emscripten (`emcc`). Options: inside WSL (`apt install emscripten`, or emsdk); or emsdk in
`fm1-ambient/toolchains/` on Windows (this session's attempt to run its installer was blocked by the session's
permission policy, so it is also Tomorrow's Man's call), where `build.py`'s bare `emcc` call would additionally need an
`emcc.exe` shim like `tools/ccshim`. Until then: the published emulator
<https://charlesvestal.github.io/fm1-amb/emu/> runs v1.3.2, whose code is identical to this tree's (the only
difference from the tag is `docs/MANUAL.md` and `tools/make_pages.py`).

### Files kept locally (rule 3)
`fm1-ambient/downloads/amb-1.3.2/`: `amb-1.3.2.fwsc`, `amb-1.3.2-app.bin`, `firmware.json`, `SHA256SUMS`, all
matching the release's sums. `fm1-ambient/downloads/stock/`: M-VAVE `FM-1-V15.fwsc` and `FM-1-V16.fwsc`, matching
the SHA-256s in `base/bsp/tools/fm1_rescue.sh`.

## 9. Rescue and back-to-stock

In order of how deep the trouble is (manual §10, BUILDING.md, `base/bsp/tools/fm1_rescue.py`):

1. **Back to stock, normally.** The web installer <https://charlesvestal.github.io/fm1-amb/install/> (Chrome or Edge,
   USB data cable), "Back to the stock firmware" with the V15 file; or M-VAVE's M-UPGRADE updater. Installing
   another firmware over USB the same way: the installer page or `python base/tools/fm1_install.py FILE.fwsc`
   (`pip install mido python-rtmidi`).
2. **Update mode from a running AMB-1 / TIDE**: hold OCT− and OCT+ for 5 s (the screen counts "UPDATE MODE IN 3..").
3. **Update mode at power-on**: hold REC while switching on. With m-bootloader installed (the installer offers it),
   the installer reaches the FM-1 there, and PLAY held at power-on opens m-bootloader's recovery menu. Without
   m-bootloader, REC enters the chip's own update mode (step 5).
4. **Safe mode**: if the firmware crashes twice in a row while starting, fm1-base starts in SAFE MODE: no sound, USB
   works, so the installer can reinstall or put stock back. PLAY tries a normal start again. A single crash shows
   a red CRASH screen, restarts, and then "RESTARTED AFTER A CRASH (PC ...)": note the address.
5. **Last resort, the chip's own update mode** (blank screen, USB `4C4A:8057` "WL80UBOOT1.00"): neither the
   installer nor M-UPGRADE sees it. `base/bsp/tools/fm1_rescue.sh` (macOS or Linux, `sudo`, pyusb) loads JieLi's
   flash loader, backs up the whole flash, and rewrites only the app area of the stock version whose boot head the
   FM-1 already has (V15's area ends at 0x93000, V16's at 0xA9000); the bootloader below 0x4000 and the device data
   above are never touched. **There is no Windows version of this script.** Before the first flash (Phase 7) Tomorrow's Man
   should know which Mac or Linux machine would run it, or accept WSL2 with `usbipd-win` USB passthrough (untested),
   or Felucca's FM-1-transporter board. Rehearsing steps 1-3 is the rule-3 rehearsal; step 5 is read, not rehearsed.

Rescue key summary: **OCT− + OCT+ 5 s** (running) and **REC at power-on** (update mode), **OCT− + OCT+ at
power-on** (panel calibration), **PLAY at power-on** (m-bootloader menu, if installed), **PLAY in safe mode** (try
again).

## 10. The four problems: diagnosis with code evidence

DESIGN §1 lists what Tomorrow's Man hears and a hypothesis for each. Here is what the code says.

### Problem 1: a piece is one of a handful of presets, and its tone never changes character. **Confirmed.**
- A piece is a row of 28 constants (`amb.c:41-53`); selecting one copies them all into play
  (`project.c:16-23, 140-145`).
- **The engine never moves its own parameters.** `par[]` is written only by `apply()` (`amb.c:1158-1165`), which
  only the UI's command queue reaches (`en_set` → `drain`, 1389-1392). No process in the engine changes a value or
  a center over time.
- What does move by itself is small, fast and zero-mean around the recipe: the arc (`1713-1731`; ±0.25 of the
  vowel range at 502, ±40% of the Rhodes index at 1495, ±0.6 octave of the pad filter at 1446, −25% reverb send at
  1694, ±80% bell count at 667, pulse 0.3..1.15 at 887) with a new random target every 50..150 s; and the nine
  `mod[]` wobbles (355-376) retargeted every 7..40 s (pitch ±8 cents, vowel ±0.2, pan ±0.35, all × Motion).
- **Discrete timbre never moves:** the bells' FM ratio index comes from the Bell tone knob (758-759), the Rhodes
  modulator is fixed at 1:1 (1497-1499), the formant table is a constant (401). Rhodes vs Voices is a per-piece
  switch crossfaded in about a second (442-453, 1197), not a morph.
- Cause: there is no notion of a timbre position; the recipe *is* the sound. (The notes do keep evolving, 711-732,
  so a piece is never a loop; its statistics are stationary.)

### Problem 2: over time the pad overwhelms the piece. **Confirmed in mechanism**, with a correction to the hypothesis.
The hypothesis blames stacking feedback. The pad's absolute level does not grow unboundedly: the echo and plate
are stable (feedback 0.756 and decay 0.9 at Size 80). What grows is the pad's **share**, because:
- **Nothing measures level.** The only level-dependent element in the engine is the `tanh` soft clip at the output
  (1707-1708). No meter, compressor, governor or per-layer budget exists; `en_pad_level` (1543) is for the screen.
- **The pad's gain is a constant**: `(Pad/100)²` (1167) × 0.5 (1473) × 0.8 (1457).
- **The pad never rests.** A breath ends and the next starts at once (491-495); the rest is 10% of the cycle (412);
  all four voices sing from `play_start` on (1356-1361). The engine's rests are for the pulse (888-892), the bass
  (643) and a bell loop (718): the header lists exactly those three (`amb.h:20-21`). The pad is a bed, not a layer.
- **Everything else thins and the pad does not.** When the arc goes sparse the bells lose up to 80% of their notes
  (667) and rest more (718), the bass rests more (643), the pulse drops to 0.3 (887); the pad's amplitude has no arc
  term (1493). When the arc goes full, the pad gets brighter instead (1495, 1446). Either way its share rises.
- **The space is low-mid heavy, where the pad lives.** The pad sends 0.6 of its sum to the plate and 0.2 to the
  echo (1459-1460, the largest sends of any layer), the echo outputs feed the plate again (1704), and the tank's
  damping (0.45 one-pole, 988/994) decays highs faster than lows, so the tail accumulates below ~1 kHz; the pad bus
  is low-passed at ~1.6 kHz at Bright 40 (1446) and the bass adds a sub-octave (1561). Freeze makes the plate
  nearly lossless (1732).
- **Voicing has no low-register spacing**: four voices in MIDI 48..77 (591), and the voice leading only penalises
  unisons (236-239), so close intervals around C3 happen.
- Cause: a constant-gain, never-resting pad with the biggest sends, in an engine with no level awareness, next to
  layers that do thin. DESIGN §5's governor and "the pad is a layer" address exactly this.

### Problem 3: the swell becomes jarring instead of spontaneous. **Partly confirmed.**
The hypothesis: "a scheduled ramp with a fixed shape and period that hits every layer at once". The code has two
things that could be heard as the swell:
- **The pad breath (the Swell knob)** is a fixed shape (404-413) with a near-fixed period per voice: at Swell 60,
  11.2, 13.6, 16.0 and 18.7 s, each ±15% (427, 399), and level and brightness rise together on every breath (1493,
  1495). The random peak (429) changes the height, never the shape or the rise time (40% of the cycle: 4.5..7.5 s).
  **Fixed shape and period: confirmed.** But it is per voice, four voices on unrelated cycles: **"every layer at
  once": refuted** for the breath.
- **The arc** (1716-1731) *does* move every layer at once (pad vowel, index and filter; bell count; pulse level;
  reverb send), with a 35 s time constant and a new target every 50..150 s that is pushed away from the middle
  (1723: a ±0.25 offset), so it sweeps between "sparse" and "full" rather than resting anywhere. Global, yes;
  a ramp, yes; fixed period, no (50..150 s).
- **Steps that could read as jarring** rather than swells: at a chord change a pad voice whose note is not in the
  new chord fades out in 1 s (`HAND_STEP` 442, 476-483) and restarts a breath already 15% risen (481), a 1 s drop
  against breaths of 11..19 s; on a piece, root, scale or colour change every sounding voice does this (443-453);
  consecutive breaths may jump in peak from 0.6 to 1.0 (429); voice leading may move a voice's next note by up to a
  sixth (585-598).
- Which of these Tomorrow's Man hears is his to say (rule 8); Phase 2's report will show the breath and the arc as periodic
  structure in the loudness and centroid traces, and the 1 s handoffs as steep level steps.
- Cause: swells come from two fixed-shape processes (a cosine breath, a one-pole slide to a random target), not
  from the sum of unrelated slow movements. DESIGN §6's summed walkers and asymmetric shapes address it.

### Problem 4: above the phrase level nothing changes. **Confirmed.**
- The slowest autonomous processes, all of them: chord drift 20 s..5 min (86, × 0.7..1.3 at 616; 39 s at the
  default), the arc's 50..150 s between targets (1724), the echo heads' 20..60 s (1097), `mod[]`'s 7..40 s (367),
  loop passes of 13..29 quarter notes (10.8..24 s at 72 bpm). **Nothing operates on a scale of tens of minutes or
  hours**, and nothing has memory beyond its next random target.
- **Harmony stays in one pool.** `chord_change` (621-645) moves between degrees of the current pool by fixed
  weights. The root (`P_ROOT`), the mode (`P_MODE`) and the pool only change from the panel (`ui.c:254-281`, Setup
  K1). No pitch class ever pivots by itself; no register moves (bells `LOOP_BASE` 542, pad 48..77 at 591, bass
  C2..B2 at 601).
- **Tone stays put** (problem 1), tempo stays put, layer levels stay put.
- Cause: there is no process slower than the loops, and no state that would let one exist. DESIGN §3's weather and
  season layers and §7's pivot modulation are the missing tiers.

### What this means for TIDE
The DSP (voices, echo, plate), the grid, the loops and the command queue are sound and reusable as they are. The
control layer to replace is small and well delimited: `control()` (1716-1750), the arc, `mod[]`, `pad_breathe`'s
scheduling, `chord_change` and `loop_target`. Everything DESIGN adds (walkers, governor, timbre cursor, harmonic
fields) sits where these are, feeding the same `apply()` targets or new engine-internal ones, and the seeded,
sample-exact structure means desktop renders will match the device.

## 11. TIDE Phase 3: the conductor (2026-11-08)

AMB-1's control layer is gone from `app/dsp/amb.c`: the arc (`arc`, `arcm`, `control()`'s retargeting), the pad's
scheduled breath (`breath()`, `BREATH[]`, `START_T[]`, `pad_breathe`), the 1 s `pad_leave` fades at chord changes,
the Drift timer (`drift_secs`) and the pulse's calls (its code is kept under `#if EN_PULSE`, default 0). The DSP
(`render_pad`'s voices, `render_bells`, `render_bass`, `echoes`, `reverb`), the grid (`tick`), the loops, the
command queue and the `en_*` state are AMB-1's. What stands in the control layer's place, all in `amb.c`:

- **Walkers** (`walker_t`, `walker_set`, `walk`): Ornstein-Uhlenbeck processes (center, tau in seconds at Drift 1x,
  sd, bounds), stepped every `CTRL_N` = 2048 samples (46 ms) by `conductor_step()`; `speed` multiplies time, 0
  holds. `slew()` is the dB rate limiter everything audible goes through.
- **The conductor's state** `cd`: seasons (`s_speed` 2.3 h, the weather's pace as log2; `s_density` 3.1 h;
  `s_bright` 1.7 h), weather (`bright` 11 min, `density` 17, `reg` 23 (semitones), `space` 13, `chord` 8 (chord
  dwell as log2), `pres[4]` 9/14/19/29 min: pad, bells, bass, Ground). `cd.dens` and `cd.br` are the combined
  density and brightness deviations the rest of the engine reads (`loop_target`, `pad_coefs`, `pad_bus`,
  `bell_ring`). `conductor_reset()` (from `regrow` and `en_init`) draws them afresh near their centers.
- **Drift** (`P_DRIFT`): `drift_speed(v)` = 2^((v−50)/25), 0 = Hold (text "Hold", "0.50x"; the screen reader says
  "0.50 times"). The weather runs at `speed × 2^s_speed`; seasons and the note-level walkers at `speed`. At Hold the
  walkers, the chord dwell, the cells' mutation (`loop_pass` returns), the pad's entrances and leaves and the
  governor's trim and biases all stop. Freeze holds the same things (and the plate, as before).
- **The pad as a layer** (`pad_step`, `pad_enter`, `pad_note_on`, `pad_leave`, `pad_handoff`): a voice is
  `PV_OFF`, `PV_ON` or `PV_OUT`. The conductor wants 0..3 voices from the pad's presence (`want` crosses 0.3, 0.6,
  0.85; `cd.dens` and the Density knob shift it; `pad_thin` takes one away when the pad has led). Entrances are at
  least 10 s apart; a voice enters from −45 dB at 0.4 dB/s on its note of the chord, sized by the governor's headroom
  (`base_db` −14..0), breathes on three level walkers (`lw[3]`, tau 25/47/90 s × `VOICE_K[i]`, sd from Swell:
  0.6 + 0.02·Swell dB each), stays 60–300 s, dissolves (`PV_OUT`) at 0.2–0.3 dB/s to −50 dB, rests 20–60 s. A chord
  change sends a voice on a note the new chord lacks to `PV_OUT` (never bent); a voice on a shared note holds. When
  every voice is dissolving, the faintest below −30 dB may take the new note from where it is. The player's key
  changes (piece, root, pool, colour) still cross over in 1 s (`pad_handoff`, `hand`/`hf`). `crest` (how far above
  its base a voice is) feeds the vowel and the Rhodes index, both rate-limited (`vowel`, `idx` in `pad_coefs`).
  `en_pad_amp[]` is the voice's amplitude as before; `en_cond[CD_PAD_VOICES]` the count singing.
- **The bass** (`bass_step`): presence < 0.3 rests it, > 0.4 brings it back; its level is two walkers (tau 40/95 s)
  through `slew` at 0.4 dB/s up, 0.25 down (6 dB/s after Stop). **Ground** (`ground_step`, `render_ground`): the
  chord's root at MIDI 24+pc (C1..B1), two sines 4 cents apart plus its octave and a little of its twelfth; enters
  when presence > 0.65 and the gap (180–420 s) has passed, 0.3 dB/s up, 0.2 down, stays 120–300 s or until presence
  < 0.5 (its presence walker centers on 0.3, so it is above 0.65 about 12% of the time; at 0.25/0.72 it never came in
  four hours of Garden, nor at 0.3/0.65 with that seed, so when it is overdue, 40-70 weather minutes since it left,
  its presence is called up at 0.01/s until it enters: rare, never absent). It uses the pulse's slot: `P_PULSE` is its level, `EN_TAP_PULSE` its tap, MIDI channel 3 (0x93).
- **The governor** (`governor_step`): per step, each layer's dry energy (from `layer_done`, every layer renders into
  `lyl/lyr` first) plus the plate's and the echoes' returns shared out in proportion to the sends (`SEND_R`,
  `SEND_E`), smoothed 20 s (`e20[]`, `m20`; `m60` for the CSV). Loudness: `target` = −18 + 0.15·(Density−40) dB on
  the engine's scale (per-channel mean square before the output gain); outside ±1 dB the `trim` (−12..+9) moves at
  0.1 dB/s and multiplies every layer's gain, thins the loops (`loop_target`), the pad (`cd.dens`) and the echo
  feedback (`echo_fb_eff`). Shares: among three or more sounding layers a layer over 45% gets a `bias` down to −8 dB
  (proportional, 0.06 dB/s); a layer that has led (> 50%) for three minutes calls the resting layers' presence up;
  `pad_thin`. `gdb[]`/`g[]` are each layer's trim + bias, slewed at 0.16 dB/s, applied in the renders.
- **The rate budget** (DESIGN §6): voice 0.4 + trim 0.1 + bias 0.06 dB/s, under the 0.75 dB/s that 1.5 dB in 2 s
  allows, leaving room for the plate's own ripple (~0.2 dB/s on a held note). To get there AMB-1's tape wobbles on
  the pad (`mod[0..5]`) run four times slower (28–160 s), the pad's pitch drift is ±2 cents (was ±4), the pad's
  reverb and echo sends come from the unpanned sum (`padm`, ×1.35) and its echo send is 0.1 (was 0.2).
- **Chords**: `drift_total` = 90 s × 2^chord × (0.7..1.3) in weather seconds (`drift_left -= dt × wspeed`); the
  bass's fifth lean halfway through stays. Garden (Drift 25 = half speed) dwells 2–5 min; AMB-1 dwelt 20 s–5 min
  (39 s at Garden's setting).
- **Layer gains**: pad 0.36 (AMB-1 0.5), bells 0.8 (0.62), bass 0.6 (0.42), Ground 0.5, so the recipes start nearer
  parity and the governor has room.
- **Exports**: `en_cond[EN_NCOND]` with `EN_COND_NAME[]` (the walkers, presence, speed, the governor's levels,
  target, trim, shares, gains, the pad's wanted and singing counts, Ground's level, the opening); `en_ground_level`;
  `en_arc` now carries `cd.dens`; `en_drum_glow[0]` carries Ground's level for the screen's fourth meter. Host only:
  `en_host_mute(layer, on)` mutes a layer after the governor has heard it (solo renders), `en_tap_pad_state(i)`.
- **The opening**: from silence, the first voice starts at −12 dB and the slews run at 2 dB/s for 30 s
  (`cd.opening`), so power-on sounds within seconds; the Definition of done leaves the first 2 minutes out.
- **Tests** (`tests/host/amb_test.c`): the pure parts as before; Drift texts; the chord moves at Drift 100; a voice
  enters only on a note of the chord and never more than three sing; Drift at Hold holds the weather, the chord and
  the cells; the pad alone for 15 min stays under the swell limit (its per-second dump goes to
  `app/build/tests/padalone.txt`); the whole garden for 12 min sits within 3 dB of the governor's target. The
  reader scenario expects the new Drift wording.
- **CPU**: not yet measured on the device (Phase 7). Added per block: four layer buffers summed into the mix and
  squared (about 6 operations a sample a layer), the returns squared, and the conductor's step every 2048 samples
  (a few hundred operations). The pad's mono send bus adds two one-poles a sample.

### Phase 3b (2026-11-08): the bells and the bass as layers, the governor rebuilt for layers that leave
After Tomorrow's Man's first listening (FINDINGS "Phase 3b"). Where the numbers above differ, these win:
- **The bells** (`bells`, `bells_step`): the presence walker (center 0.5, was 0.6; 14 min) feeds `loop_target` as
  `presf` = (pres − 0.25) / 0.35 clamped 0..1.3 (empty under 0.25), with Density × 0.028 notes a loop (was 0.05),
  ceiling 4 (was 5), and the trim only thins (`fm_minf(cd.trim, 0)`). Under 0.3, once `stay_t` ≥ `min_stay`
  (240–600 weather s), the layer rests: `tick` rings nothing, the level target is −60 dB; `rest_t` counts weather
  seconds once `ldb` ≤ −40 and the rest ends at `gap` (150–360 s), the presence lifted to 0.42, a new `min_stay`
  drawn. Level: two walkers (55 / 130 s, sd 0.65 × the Swell sd, ±8) + 6 dB × clamp((pres − 0.6) / 0.3, −1, 0),
  slewed 0.25 dB/s both ways (the opening 2.0), applied in `render_bells` with the gain 0.5 (was 0.8). `loop_add`
  draws velocities 30..94 (was 50..99); `loop_pass` sits a loop out with probability 0.1 + 0.25 × Evolve (was
  0.15 × Evolve); `rand_offset` sends a chord tone an octave down 20% of the time (encoded as `OFF_DOWN` + o, decoded
  in `bell_note`, which also adds `reg_now`: the register walker moves the bells with the pad). `en_cond[CD_BELLS]`
  is the layer's level in dB.
- **The bass** (`bass_step`): the same rest shape (`min_stay` 300–720 weather s, `gap` 120–300 after −40 dB, back
  with presence ≥ 0.4), walkers 80 / 190 s (was 40 / 95) with sd 0.56 × the Swell sd (was 0.5 ×), ±8 (was ±5), a
  4 dB presence tie, 0.25 dB/s up (was 0.4). `en_cond[CD_BASS]`.
- **The pad**: level walkers 39 / 73 / 140 s (was 25 / 47 / 90), sd 0.7 + 0.023 × Swell (was 0.6 + 0.02), ±8 (was
  ±6): deeper at the same dB/s. `pad_enter`: `base_db` = −6 + 4r + 0.4 × clamp(target − level20, ±4) (was 0.8 ×,
  unclamped); the 3 dB trim knock at an entrance is gone. `pad_step`: `want_n` ≥ 1 while the bells and the bass are
  resting or under −20 dB (the ensemble floor's pad half).
- **The governor** (`governor_step`): `K_W` = {1.0, 1.6, 0.45, 0.3} (bass was 0.8, Ground 0.5); `target` = −22 +
  0.15 × (Density − 40) (was −18); the trim moves on the 20 s meter outside ±1.5 dB (was ±1) at 0.06 dB/s up and
  0.1 down (was 0.1 both); the presence calls (a leading layer, a thin garden) are gone; `wshare[]` are the layers'
  K-weighted shares; the **make-up** `ff`: at a rest's start `ff_e0[k]` = e20[k] / g[k]² and `ff_db0[k]` = the level,
  each step `miss` = Σ K_W[k] × ff_e0[k] × g[k]² × (1 − 10^((ldb − db0)/10)) over the resting bells and bass,
  `want` = 10 log10((m20 + miss) / m20) capped at 6 dB, slewed 0.15 dB/s, added to every layer's gain (`gdb` =
  trim + ff + bias, still slewed 0.16: the rate budget's guard); `ff_e0` clears when the layer is back within 3 dB
  of where it left. The **ensemble floor**: `under` counts seconds with the meter 6 dB or more under the target,
  `silent` seconds with no layer (pad, bells, bass) above −30 dB; at 60 s or 10 s one resting layer is recalled (the
  bass, then the bells, each with presence lifted over its threshold; else the pad's presence to 0.65), then 180 s of
  grace. `en_cond[CD_FF]`.
- **Tests** (`tests/host/amb_test.c`): the bells rest at least once and return in 2 h at Drift 50 and their level
  never moves over 1 dB/s; the garden's 60 s level is within 3 dB of the target for at least three quarters of
  20 minutes; the loops test tolerates empty loops; the bell-tone test pre-rolls 40 s and counts a minute.
- **Rate budget**: unchanged in kind. The gains' slew (0.16) now carries trim + make-up + bias; the layers' own
  levels move at 0.25 (bells, bass) or 0.4 (a pad voice).

## 12. TIDE Phase 4: the timbre landscape and cursor, and the retune after Tomorrow's Man's second listening (2026-11-09)

- **The retune** (TESTING #14, #15, in `amb.c`): `render_bells` gain 0.32 (was 0.5; Tomorrow's Man: "at least 35% quieter",
  the hits he timed were all bells at velocities 0.92–1.03 of 1.10, two or three loops on one beat); `loop_add`
  velocities 25..80 (was 30..94). `bells_step` and `bass_step` slew 0.4 dB/s up and 0.18 down (was 0.25 both: "a
  return as slow as a leave"; Tomorrow's Man asked for the opposite, up quicker than down). A rest may not begin while the
  other of the two is resting or under −20 dB (`bell_vol`/`bass_vol` > 0 only): both away at once left the pad
  alone for six minutes at 2:30 in the Phase 3b Garden render, the stretch Tomorrow's Man called static.
- **The landscape** (`anchor_t`, `LAND[NLAND][NANCHOR]`, `land_now`, `tcur`, `cursor_blend`, `en_anchor_name`):
  two landscapes of six anchors on a unit square, selected by `P_PADTONE` (0 "Rhodes", 1 "Voices"; the texts are
  unchanged). An anchor: colour (0 FM, 1 formants), FM ratio, place, and the continuous set (FM: `idx` × AMB-1's
  index, `bloom` × the crest's 0.1, `h2` the second harmonic (AMB-1 0.12); Voices: `vowel` offset, `q` × the
  formants' k (0.45/0.3), `saw` (AMB-1 0.45); both: `fc` in octaves on the pad bus's low-pass). The cursor
  `tcur.x/y` = 0.5 + a season walker (`sx`/`sy`: tau 2.5/2.9 h, sd 0.15, ±0.25) + a weather walker (`wx`/`wy`: tau
  20/25 min, sd 0.25, ±0.45), the seasons at Drift's speed and the weather at the weather's, clamped 0..1. (The
  first 8 h render had the season at sd 0.22 within ±0.4 and the weather at 0.18 within ±0.35: the season parked in
  a corner for hours and the cursor visited 3 anchors in 8 h, Wurli 72% of the time; with the season small and the
  weather wide it visited all 6 in 4 h of either piece and in 8 h.)
  `cursor_blend` weights each anchor 1/(d² + 0.004) (an anchor's own weight ~250 against a neighbour's ~6), blends
  the FM set over the FM anchors and the Voices set over the Voices anchors, `fc` over all, and slews each value
  at 0.01/s (`init` sets them at once: a piece change, `P_PADTONE`); the nearest anchor changes only when another is
  nearer by d² < 0.7× (hysteresis: 178 changes in a fast hour without it, 45 with). `pad_note_on` takes the
  nearest anchor's colour and ratio into the voice (`v->voices`, `v->ratio`, `v->land`) and puts the modulator in
  phase with the carrier; `pad_handoff` crosses the voices when `v->land` differs from the landscape. `pad_coefs`:
  `inc[1]` = `inc[0]` × ratio (the modulator; AMB-1's second saw was never played), the vowel target + `tcur.vowel`,
  the index target × `tcur.idx` with the crest × `tcur.bloom`, the formants' k × `tcur.q`; `render_pad` reads
  `tcur.h2` and `tcur.saw` every 32 samples and the FM branch runs the modulator on `ph[1]`; `pad_bus` adds
  `tcur.fc` to the low-pass's exponent. Exports: `en_cond[CD_CURSOR_X/Y]`, `en_cond[CD_ANCHOR]` (the index),
  `en_anchor_name()` (the harness's `anchor` column; `render.c` no longer maps Pad tone to a name).
- **The governor** (second round, after the first Phase 4 Garden render): the bells' gain target is capped at +3 dB
  (`gdb[L_BELLS]`; the trim and make-up had lifted them +9 dB at the 95th percentile and +15 at most in thin
  stretches, undoing the 0.32 and making Tomorrow's Man's loud lone bells); `bells.quiet` counts real seconds since a bell
  rang and over 20 s counts the bells as out for the pad's ensemble floor (22 s with nothing sounding at 2:06 in
  that render, the pad at no voice, the bass resting, the bells present but their loops sparse); the trim comes
  down at 0.15 dB/s (was 0.1; the mix overshot +4 to +6 dB on the engine's meter as the layers returned to a
  raised trim). Third round, after the second 8 h render: the make-up's target is reduced by whatever the mix is
  already over its target (`want -= max(err, 0)`; trim +8 and make-up +5 had stacked as the bass returned, +7 dB over
  for minutes, the pad thinned to nothing by the governor's headroom term and the bass alone for an hour). A rule
  "the pad sings a voice while the bass has led five minutes" was tried in that round and removed: the bass's energy
  share is over half whenever the pad thins, so the pad never rested (0% of 8 h), a bed again. Fourth round, after
  the in-step solo renders showed the pad's fastest moves at a pause: the ensemble floor's ask is a 60 s latch
  (`cd.floor_hold`; a single bell reset `bells.quiet` and the floor's voice was sent out a second after it entered,
  in and out every 20 s), and a pad voice enters at −45 dB *after* the gains (`pad_enter`: `from` = −45 −
  max(gdb[L_PAD], 0); with the trim at +9 and the make-up at +6 an appearance at −45 before them was a step).
- **The plate's ripple** (found measuring the swell schedule): the tank's two modulated allpasses (`EXC` = 24
  samples at 0.70 Hz, Dattorro's figures) put a level ripple of several dB at 0.7 Hz on a held pad note (the pad's
  solo render; the plate is most of the pad's level). Sampled once a second it aliases to 3.3 s and 9.4 s: the
  "9–10 s peak" of Phases 3–3b. `pl_exc_k` scales the excursion; host only, `en_host_plate_exc(k)` and the
  harness command `plate_exc k`, for the A/B in `out/phase4/ab-plate/` (TESTING #18). The device's plate is unchanged.
- **The solo renders kept in step** (`amb.c` host only: `en_host_meter_hook`, called in `governor_step` with the
  step's plate and echo energies; `tools/render.c` `--meter-out FILE` / `--meter-in FILE`, `meter_hook`;
  `tools/render.py` runs the mix pass first, then the four solos in parallel with `--meter-in run/meter.bin`): a solo
  pass still mutes the other layers' dry sound and sends (`layer_done`, `tap_end`: the solo is the layer with its own
  echo and reverb, and the solos sum to the mix within ±0.2 dB), and its governor hears the mix pass's plate and
  echo returns instead of its own thinner ones, so every decision is the same (the dry energies are the same by
  construction) and the five passes' CSV columns are identical. Found on 2026-11-09: until then the governor's trim
  differed from the mix's within 33–46 s and the passes diverged (the pad's voice count differing in two thirds of
  all seconds, the solos' energy sum −32 to +10 dB against the mix), so the solo-based figures of Phases 3 and 3b
  were taken on other performances than the WAV. A first fix that kept the sends in and scaled the returns by each
  layer's share (by the block's dry energies, then by `e20`) was dropped the same day: a resting layer's first strike
  was attributed to the pad and read as a 4–9 dB/s swell. 8 floats a conductor step: `meter.bin` is 5 MB for 8 h.
  Nothing of this is compiled for the device.
- **The harness** (`tools/render.c`): `--env FILE` writes the output's mean square every 100 ms (`ENV_N` 4410
  frames, the block cut to the boundary), one value a line; `tools/render.py` asks for `mix.env` and `solo-*.env`;
  `tools/evolve_report.py` takes the pad's envelope from `solo-pad.env` at 10 Hz when it is there (the same Welch
  bands by period) and reports the fast ripple (1–4 s) for information; the loudness check is the one-sided band
  (`loudness_inside_pct` 85, `loudness_over_db` 3, `loudness_under_db` 9; CLAUDE.md, TESTING #17); a figure shows
  the nearest anchor and the cursor over time.
- **Tests** (`tests/host/amb_test.c`): the cursor in an hour at Drift 100 changes its nearest anchor at least twice
  and visits three or more, never leaves the square, the render stays finite and bounded through the anchors and in
  the Voices landscape; the Hold test covers the cursor's walkers. Two tests made robust to the generator's path:
  the pad-entrances test runs at Drift 50 (it inherited 100 from the chord test, where a chord moves every 20–45 s
  and a voice entering at 0.4 dB/s is sent out before it reaches the −20 dB the test counts) and prints the voices
  per 30 s when it fails; the bell-tone test counts zero crossings per bell struck over two minutes (the loops'
  random fill had differed 12 to 2 notes between its runs). All pass.
- **CPU**: per pad voice the FM branch gains one phase accumulator and one table read for the second harmonic's
  scale (it was a constant); the blend is a few dozen operations every 46 ms. Still unmeasured on the device.

## 13. TIDE Phase 5: the harmonic field, and the retune after Tomorrow's Man's third listening (2026-11-09)

Where the numbers of §11 and §12 differ, these win. All in `app/dsp/amb.c` unless said.

- **The bells** (Tomorrow's Man, TESTING #20: "constant as well as sharp"; he counted 35 and more in three minutes at 3:27 of the
  Phase 4 take, the events file has 58). `tick`: the loops step every `LOOP_STEP` = 12 sixteenths (three quarter notes;
  AMB-1 and Phases 3–4: every quarter), each loop on its own sixteenth of the twelve (`LOOP_OFS` = 0, 3, 7, 10), so at
  Garden's tempo a slot is 2.5 s, the cells are 33, 43, 58 and 73 s long (11–24 s before) and no two loops strike on
  one beat. `loop_add`: velocities 10..63 (Tomorrow's Man's numbers; Phase 4 25..80). `loop_target`: Density × 0.028 × (1 + 0.5 ×
  the weather's and the season's density) × `presf` (0..1.0, was 1.3), the trim only thinning; the governor's part of
  `cd.dens` is not read (a raised trim had added a fifth to the count when the garden was thin). `bell_voice`: the
  strike's attack is per note, `arate` = Bell tone's rate / (1 + 12 × (1 − velocity)), never slower than 80 ms: at
  Garden's tone 20 ms at velocity 0.63 and 45 ms at 0.10 (4 ms for every note before). `render_bells`: gain 0.45 and
  no lift (Phase 4: 0.32 with a +3 dB lift that sat at its ceiling: the same level in a full garden).
- **The bells' company** (`governor_step`, `company`): the other layers' K-weighted 20 s energy against the target;
  within 4 dB of it nothing, below that the bells' gain follows 0.6 dB a dB, to −9 dB. `gdb[L_BELLS]` = min(trim, 0)
  + bias + company: the governor never lifts the bells.
- **The governor is one-sided** (Tomorrow's Man, TESTING #17). The trim comes down when the 20 s level is 1.5 dB over the target
  (0.15 dB/s, at its full rate from 3 dB over), lifts only when it is more than 6 dB under (0.06 dB/s, to +6 at the
  most; Phase 4: ±1.5 dB, to +9) and relaxes to 0 at 0.02 dB/s inside. `cd.dens` takes the trim's thinning only
  (clamp −0.4..0). The make-up for a resting layer is capped at 3 dB (was 6). The ensemble floor's `under` counts
  12 dB under (was 6). The pad's `want` takes the governor's headroom as clamp(target − level, −4, +2) × 0.05 (was ±4).
- **The foundation** (`pad_step`, `bass_step`, `pad_sings`): one sustained layer always sounds. While the bass is
  resting or under −20 dB the pad sings a voice at least (`cd.floor_hold`; in a piece without a bass the Phase 4 rule
  with the bells stands); the bass may begin a rest only when a pad voice is up (`pad_sings`: singing, within 15 dB
  of full), and asks for one otherwise. The bells no longer count as company for this.
- **The pad's rest is bounded** (`cd.pad_rest`, `cd.pad_rest_max`): with no voice singing for 240–600 s of Drift's
  time (8–20 min at Garden's half speed) the pad's presence is lifted to 0.45 and a voice is asked for. (A first
  final render had no pad voice through its whole eighth hour: the presence walker low, the bass present.)
- **Pad entrances** (`cd.entry_gap`, drawn in `pad_enter`): the next no sooner than 6 + 40 r² s (r uniform; median
  16 s; Phase 4: 10 s flat).
- **The timbre cursor**: the season walkers 0.11 within ±0.18 (was 0.15, ±0.25), the weather's 0.27 within ±0.5 (was
  0.25, ±0.45).
- **The harmonic field** (DESIGN §7; `fld`, `froot`, `home_pool`, `rot12`, `field_home`, `field_step`,
  `field_publish`, `en_field_name`). `pool` is now the field's scale relative to the field's root `froot`;
  `home_pool` and `par[P_ROOT]` are the player's (home). `bell_note` and `pad_retarget` read `froot`. Two season
  walkers (stepped at Drift's speed in `conductor_step`): `fld.mode` (center 0, tau `FLD_MODE_TAU` 60 min, sd 1.2,
  ±3: the set's place in fifths from home, tilted by (Bright − 50) × 0.02) and `fld.root` (tau 100 min, sd 0.8, ±2:
  the root's). When a walker is three quarters of a step from where the field is (`fld.shift`, `fld.move`) and the
  field has lasted `fld.dwell` (`FLD_DWELL`, 17–30 season minutes: 34–60 min at Garden's half speed), a step is
  asked for (`fld.pend`: ±1 the set, ±2 the root) and done at the next chord change that allows it
  (`chord_change` → `field_step`), never at Hold:
  - the set: the absolute pitch-class set is turned a fifth (`rot12` by 7 or 5) with the root held; allowed if
    exactly one note leaves and one a semitone from it comes, and the root and its fifth stay in the scale (no
    Locrian; a pentatonic can only go where its root stays); postponed while a pad voice above −40 dB still sings
    the note that leaves (never bent). The bells' cells hold scale steps, so their note on that step plays the new
    pitch from the next strike on.
  - the root: a fifth up or down inside the same set (allowed if the new root and its fifth are in it); the
    sounding chord's degree is renumbered so its notes stay, and the next chord is the new tonic.
  After a step the walker is set on the new place (a way back is a full three quarters of a step again) and a new
  dwell is drawn; a step that can never be done from here sets the walker back. The player's Root or scale
  (`apply(P_ROOT)`, `pool_set`) and a new piece (`conductor_reset`) bring the field home. Exports:
  `en_field_root`, `en_field_pool`, `en_field_name()` ("D Dorian"; a set without a name as "D 0x6AD"),
  `en_cond[CD_S_MODE, CD_S_ROOT, CD_FIELD_SHIFT, CD_FIELD_MOVE]`.
- **The plate, host only** (TESTING #18): `pl_w` is the tank's modulation rate (0.0001 rad a sample, 0.70 Hz, as it
  was); `en_host_plate_mod(rate_hz, wander)` and the harness commands `plate_rate HZ` and `plate_wander` set the rate,
  or put each of the two modulated allpasses on its own oscillator whose rate slides between 0.25 and 1.3 Hz (a new
  target every 2–7 s, its own random numbers, so the music is untouched). The device's plate is unchanged.
- **The harness** (`tools/render.c`): the CSV's `field` column is the engine's `en_field_name()` (it was the player's
  root and mode); `root` and `mode` stay the player's. Note: the engine's music depends on how the host cuts its
  blocks (`--env` cuts them at 100 ms), so a CSV-only run of a seed is another performance than the same seed with
  `--env`; `tools/render.py` always renders the same way. `../tools/evolve_report.py`: the one-sided loudness line
  (`loudness_over_db` 3 for `loudness_over_pct` 99, `loudness_under_db` 20, `silence_db` 30 for `silence_s` 20), the
  bells per three minutes; `../tools/compare.py`: the rows to match.
- **Tests** (`tests/host/amb_test.c`): the field (3 h at Drift 100: at least three changes and three fields, every
  change one step, the root and its fifth always in the scale, the chord's tones always in the field; a new Root or
  scale brings it home); Hold holds the field and its walkers, a chord asked for by hand included; the garden's 60 s
  level never more than 3 dB over its target nor 15 under in 20 min (it was: within 3 dB three quarters of the time);
  the pad's rest ends (no stretch without a voice over 720 s in 2 h at Drift 50); the pad-alone swell test takes the
  99th percentile against the limits and caps the fastest second at twice them (its "p99.9" of 780 seconds was the
  fastest second itself); the bell-tone test counts per bell struck (it had counted per MIDI message, nearly all
  clock) over six minutes. All pass.
- **CPU**: the field is a few dozen operations a conductor step and a chord change; the bells ring a third as often;
  a bell voice carries its own attack rate. Still unmeasured on the device.

## 14. TIDE Phase 5b: the bells in the scale and between Phases 4 and 5, the wandering plate, the hand-over (2026-11-09)

After Tomorrow's Man's fourth listening (TESTING #21, #23). Where the numbers of §11–§13 differ, these win. All in
`app/dsp/amb.c` unless said.

- **The bells' register, a bug since Phase 3b** (`bell_note`). The register walker was added to the note itself in
  plain semitones (`n = base + s + ... + reg_now`), a chromatic transposition: at any register but 0 the bells played
  another key, and 38–46% of all bell strikes in the Phase 3b, 4 and 5 renders are outside the scale (Phase 3: none).
  Found from Tomorrow's Man's "sharper bell tones" at 3:27 of the Phase 4 take, where the bells were a major third down (55%
  outside); his "gold" at 3:25 was a fourth down, which is nearly the same scale (12% outside). Now the walker moves
  each loop's register and the root is found at or below it: `base = LOOP_BASE[l] + BELL_REG + reg_now`, then down to
  the root. `BELL_REG` = −3: the four registers sit a minor third under AMB-1's (C5, G4, G5, D4).
- **The bells' activity** (`cd.bact`, `BELL_ACT`, `BELL_BRIDGE`, `loop_target`, `bells_step`). A weather walker of
  their own (center 0, tau 6 weather minutes, sd 0.8, −1.6..+1.2, in log2) multiplies the loops' count:
  `d *= 2^(BELL_ACT + bact + BELL_BRIDGE × alone) × bell_amt`, `BELL_ACT` 0.9 (the count's center, 1.9 × Phase 5's),
  `BELL_BRIDGE` 0.5, the ceiling 3 notes a loop (was 4). **A passage lets up**: once the walker has been over +0.2 for
  `bells.busy_max` (150–390 real seconds, drawn each time) it is set to −0.5..−1.2 and finds its way back (without
  the timer: 10 a minute and more for 28 minutes at a stretch). Garden 8 h: 4.6 strikes a minute; in any three
  minutes 12 at the median, 31 at the 90th percentile, 50 at the most, none 21% of the time; 10 a minute and more 13%
  of the time, 8.7 minutes at a stretch at the most (Phase 4: 62%, 94 minutes; Phase 5: never).
- **The Bells knob is their amount** (`apply(P_BELLS)`, `bell_amt`): the level as before (v²) and the count with it,
  (v / 65)^1.5 (1 at Garden's 65). Clips at 40 / 65 / 90: 1.6 / 4.7 / 6.1 strikes a minute, the dry level −17 / 0 /
  +7 dB. Phase 6 puts the default at the knob's middle.
- **Their tone** (`bell_voice`): the tone a strike takes is the knob's + 0.2 × the weather's brightness (`cd.br`,
  ±0.12) − 0.12 × `cd.alone`, and it picks the ratio, the index, the index's decay and the attack; the index falls
  with the note's height above G4 (half an octave of index an octave, to three octaves); the attack's softening by
  velocity is `1 + 5 × (1 − vel)` (was 12): 11 ms at velocity 0.63 and 22 ms at 0.10 at Garden's tone (Phase 5:
  20–45 ms; Phase 4: 4 ms). Velocities stay 0.10–0.63 (Tomorrow's Man's).
- **Their company** (`governor_step`, `cd.alone`): `cd.alone` (0..1, 20 s smoothed) is how far the other layers sit
  under the target (0 within 4 dB, 1 at 19 dB under); the bells' gain follows it down by 4 dB at the most (was 9),
  and it rounds their tone and draws a few more of them out.
- **The plate wanders** (Tomorrow's Man's choice, TESTING #23, clip F): what was the host-only A/B is the engine's.
  `pl_wander` = 1; each modulated allpass has its own oscillator (`lfo_s/c`, `lfo2_s/c`) whose rate slides to a new
  target in `pl_wlo` .. `pl_wlo + pl_wspan` (0.25–1.3 Hz) every 2–7 s (`plate_wander_step`, once a chunk, its own
  generator `pl_rng`). Host: `en_host_plate_mod(hz, 0)` sets a steady rate (0.7: AMB-1's), `en_host_plate_range`.
  Cost: one more two-multiply oscillator a sample.
- **The plate's breathing and the swell limit.** With the wandering plate the pad's 3 s level (its solo render)
  moves 1.8–2.0 dB/s and 3.1–3.4 dB in 2 s at the 99.9th percentile (steady plate: 0.89 and 1.27; the median moment
  0.26 dB/s); narrowing the range (0.5–1.3, 0.7–1.6 Hz) does not change it (1.85), so it is the two halves beating,
  not the slow end. `tools/render.py` therefore makes a sixth pass, `solo-pad-steady` (the knob log with
  `plate_rate 0.7` first, `--meter-in`, `--env` so its blocks are cut as the others'): the same performance through
  AMB-1's steady plate; `tools/evolve_report.py` judges the pad's swell limit on it (after checking that its trim,
  note and voice columns equal the mix's) and reports the level as heard. A wording for Tomorrow's Man: TESTING #26.
- **A pad voice enters from −52 dB** (`pad_enter`; was −45) and, **in another voice's shadow** (20 dB or more under
  the loudest pad voice), rises at 2 dB/s (`pad_step`, `top`). In a take where the pad sat 5 dB lower than usual a
  lone voice appearing at −45 came up through the report's floor with the plate filling behind it (3 dB/s, 57 such
  seconds in 8 h, all 25–30 dB under the pad's median).
- **The foundation's voice hands over** (`pad_step`, `relay`): while the pad is the one sustained layer
  (`cd.floor_hold` > 0) a voice at the end of its stay stays until another voice is up (singing, above −15 dB), and
  one is asked for (`want_n` ≥ 2). It used to leave at once and cross its successor 18 dB down: every quiet extreme
  of the Phase 5 and first 5b takes (16–22 dB under the target). Garden 8 h: 13.3 dB under at the most (LUFS), and
  7.7 on the engine's meter; twelve 20-minute runs from other seeds: 3–7 dB.
- **`en_init` clears `en_playing` and `en_frozen`.** A host that calls it again (the tests, once a scenario) was still
  playing, so its `en_play(1)` did nothing and the opening never ran. The device calls it once, at boot.
- **Exports**: `en_cond[CD_BELL_ACT]` (the count's log2 factor), `[CD_ALONE]`, `[CD_FIELD_PCS]` (the field's pitch
  classes as a 12-bit set, C = bit 0), so the harness's CSV has `c_bell_act`, `c_alone`, `c_field_pcs`.
- **The report** (`../tools/evolve_report.py`): bell strikes outside the field, % (`events.txt` against
  `c_field_pcs`, or the field's name in older runs); the bells' busy stretches (10 a minute and more: share of the
  time, longest); the pad's swell limit through the steady plate, with the as-heard figures; the modulation peak above
  a fitted parabola, for information (the straight line's largest miss sits on the first bins inside its fit, 67–75 s,
  in most hours of most takes: TESTING #27). `../tools/compare.py`: rows to match.
- **Tests** (`tests/host/amb_test.c`): every bell is a note of the field while the register walker crosses its
  whole range (3 h at Drift 100: 600–1000 bells, none outside; the hook checks each strike against
  `en_field_root/pool`); the bell-tone test counts zero crossings a second of ringing bell (per bell struck over six
  minutes failed once the count rode a walker: 13 bells against 77); the pad-alone swell test runs through the steady
  plate. All pass.
- **CPU**: one oscillator more in the plate; a walker; nothing else of note. Still unmeasured on the device.

## 15. TIDE Phase 5c: standing tones and the ebb (2026-11-10)

After Tomorrow's Man's fifth listening (TESTING #25, on the Phase 5b Garden take). Where the numbers of §11–§14 differ, these
win. All in `app/dsp/amb.c` unless said.

**What he heard, measured.** "Tones at ~530 Hz and its harmonic at ~1060 Hz that overwhelm when they become static",
"almost a standing wave" at 0:48:30–0:49:00, "the very specific tone that can sound like feedback" at 3:39–3:40; and
"the excerpt on the whole stays the same loudness ... this never relents". Both tones are one pad voice:
- 0:48: voice 3 on C5 (522.5 Hz) at amplitude 1.72 (+4.7 dB; its usual 0.1–0.5), its three level walkers cresting
  together at +8.6 dB over its resting level, the FM bloom at the crest putting its octave (C6, 1045 Hz) 4 dB over
  its fundamental, the other voices and the bass fading, the gains lifted 2.5 dB for the thin garden, the plate at
  1.4 times the dry pad.
- 3:39: two voices on the same C#5 (MIDI 73; `pad_step`'s "not the same pitch class" was a preference, not a rule),
  1.29 and 0.47, the plate ringing at 3.5 times the dry pad, the 60 s loudness 5.5 dB over the target (the take's
  loudest moment).
A fixed EQ would not have answered it: the tone is whatever note the voice holds (C5 once, C#5 the other time, a
dozen quieter cases between C#4 and D5). And a plate at this decay answers a held sine with whichever of its modes
lie on it, ±6 dB from note to note, so the remedy is that no single held voice gets that large.

- **A voice's ceiling** (`PAD_CREST` 3 dB, `PAD_TOP` −3 dB; `pad_step`). The walkers' sum is compressed 4:1 above
  +1.5 dB and stops at +3 over the voice's resting level; and no voice's level passes −3 dB whatever its resting
  level (amplitude 0.71; a lone voice there weighs about the Density target by itself, at 0 dB 4–5 dB more).
  Downward the walkers are as deep as before.
- **A voice's height** (`pad_height`, `PAD_TILT` 0.35 dB a semitone above G4, `PAD_ROUND` 0.35). A voice is softer
  the higher its note (C5 −1.75 dB, F5 −3.5, B5 −5.6: about the ear's curve from 400 Hz to 1 kHz), and above E4 its
  overtones halve an octave (the FM index with its bloom, and the second harmonic; `v->hk`, at least 0.35). `v->crest`
  is taken without the tilt.
- **No unison** (`pad_step`, `same`). A voice does not enter on the very note another voice sings or is still
  dissolving on above −30 dB; it waits, or another voice goes. The pitch-class preference stays a preference.
- **The pad's gain 0.40** (`render_pad`; 0.36): the ceiling and the tilt took 3.5 dB off the pad's median level in a
  first take (its share 45% → 33%); 0.9 dB given back.
- **The ebb** (`EBB_*`, `governor_step`, `cd.ebb`). Every `EBB_GAP` (5–16 Drift minutes: 10–32 real minutes at
  Garden's half speed), if the 20 s meter is within 5 dB of the target, an ebb begins: its bottom is drawn
  (`EBB_DEPTH`, 11–16 dB under the Density target) and its stay (`EBB_HOLD`, 45–165 real seconds); the bells'
  activity is set to a lull (−1.0..−1.4). `cd.ebb` (dB, on every layer's gain, outside the gains' own 0.16 dB/s
  slew) then follows a servo: the garden's own level (the 3 s meter less the ebb as that meter hears it, `cd.ebb3`)
  is taken down to the bottom, `want = clamp(target − depth − own level, −EBB_MOST, 0)`, falling at `EBB_FALL`
  0.1 dB/s and rising at `EBB_RISE` 0.25. The bottom is a place, not a drop: if the garden thins by itself under
  an ebb (a lone voice's walkers can take 10 dB off in a minute) the ebb gives the difference back. (As a drop of
  9–16 dB with a floor on the 20 s meter, three takes read 19.1, 19.3 and 20.9 dB under the target at the deepest;
  the Definition of done's line is 20.) The stay counts down while the ebb is within 1 dB of where it is wanted;
  then the ebb returns to 0 and the next gap is drawn. The governor compares its meter with the target less the ebb
  as a 20 s meter hears it (`cd.ebb20`), so the trim neither chases an ebb down nor lifts into it; the pad's voice
  count, a new voice's size, the bells' company and the "nothing sounds" threshold use the same. At Hold nothing of
  it moves. What stays: the foundation (a pad voice or the bass always sounds), so an ebb is far-off music, not
  silence. `en_cond[CD_EBB]`, CSV `c_ebb`.
- **A lift is handed back** (`governor_step`, `back`). A raised trim comes down at 0.15 dB/s (not the relaxing
  0.02) once the mix is within 3 dB of its target again on the 20 s or the 3 s meter. In a take without it the trim
  (+6) and the make-up (+3) sat through a 14 dB lull, the bass and a voice returned together under them, and the
  60 s loudness went 5.8 dB over before the cut began.
- **The report** (`../tools/evolve_report.py`). *Standing tones*, for information: per 10 s the strongest single
  partial of the mono mix between 250 Hz and 6 kHz, A-weighted, as the loudness of that sine alone against the
  Density target; the minutes with one tone within 3 dB of the target's whole loudness, the loudest, the eight
  loudest moments with note and share (`features.npz` is now `-v2` and holds `tone`). On the 5b take it ranks
  Tomorrow's Man's two moments first and third of 2,880 (0:48:40 C6 +0.4 dB, 3:39:20 C#5 −0.7). *The tide*, for information:
  the ebbs from `c_ebb` (deeper than 3 dB; returns shorter than three minutes joined), their depth, length, spacing
  and the 60 s loudness at each bottom, with a figure. *The crest line is judged above the fitted parabola* (Tomorrow's Man
  ok, TESTING #27); the straight line's figure is printed for information. `../tools/compare.py`: rows to match.
- **Tests** (`tests/host/amb_test.c`): an hour at Drift 100: no two voices above −30 dB on one note; no voice over
  its ceiling (−3 dB less the tilt); three or more ebbs of 5 dB or more, none over 17, each back; at Hold an ebb
  does not move. All pass.
- **CPU**: nothing per sample; a handful of operations per conductor step. Still unmeasured on the device.

## 16. TIDE Phase 6: the controls, the screen, the save slot, the emulator (2026-11-10)

**The pages** (`app/ui.c` `VIEW_KNOB`, `KNOB_LABEL`; buttons as AMB-1's):

| Page (button) | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| **Tide** (HOME) | Mood | Density | Drift | Space |
| Pad (ENV) | Level | Swell | Bright | Colour |
| Bells (ARP) | Amount | Tone | Octave | Evolve |
| Bass (LFO) | Level | Octave | Glide | Fifth |
| Ground (SEQ) | Level | Motion | Tempo | (none) |
| Space (FX) | Echo | Echo time | Reverb | Size |
| Setup (GLO) | Root | Tune | MIDI out | Lights |

The old Garden page's Tempo, Evolve and Motion moved to Ground and Bells; Density moved from Bells to the first page;
the pulse's Hits, Swing and Hats are on no page (the pulse is not played). Keys, PLAY, REC, EDIT, SELECT, ALGORITHM,
PRESETS, OCT−/OCT+, SAVE, HOME + SELECT (tempo) and the help cards are AMB-1's.

**Mood and Space** (`P_MOOD`, `P_SPACE`, −50..+50, 0 in every recipe; `mood_set`, `space_set` in `app/dsp/amb.c`).
Each is an offset around the piece's own knobs, so at 0 the engine computes exactly what it did before: the 8 h
Garden take of seed 1107 is the same file as Phase 5c's, byte for byte (`out/phase6/batch.log`).
- Mood: the pad's Bright ±30 points (`bright`), the mode walker's lean ±1.5 mode steps (`mode_tilt`, in the two places
  that read Bright's tilt), and the center of the weather's brightness `cd.br` −0.40..+0.50 (`MOOD_BR_DOWN`,
  `MOOD_BR_UP`): the pad's filter, vowel and index, the bells' tone and their octave-ups all read `cd.br`.
- Space: toward near, Reverb and Echo −30 points and Size −20 (the plate's decay, the echo's feedback); toward far,
  Echo +30 but Reverb only +15 and Size +10 (`SPACE_FAR_REV`, `SPACE_FAR_SIZE`). With the far side as long as the near
  one (Garden's Reverb at 95, Size at 100) a held state rose 8.2 dB and one note rang in the plate within 3 dB of the
  whole target for 3.8 of 5 minutes; now +2.9 dB and 0.3 minutes at Hold, where the governor's hands are stopped,
  and inside every line when the piece runs (`out/phase6/`). The Space page's own knobs keep their values; the
  offset is applied where the sends are computed.
- Drift fully down is Hold (Phase 3); the screen says "hold" top right. The ebb has no knob (TESTING #32 had no
  preference: the engine decides).

**The screen** (`draw_header`, `draw_ring`, `draw_meters`, `draw_weather`, `draw_main`). The header's key is the
harmonic field as it is now (`en_field_name`), not the home scale; the ring and the keys' lights show the field too
(`field_abs`). Left: the four layers (the fourth is Ground). Right, in the pulse rings' place: the tide
(`en_cond[CD_EBB]`, full = in, empty at 16 dB out), then two marks for the weather's brightness (`CD_BRIGHT`) and
density (`en_arc`). Top left: the timbre cursor's nearest anchor (`en_anchor_name`). The screen reader says the field's
scale, "holding", and "The tide is out".

**The save slot** (`app/app.h`): `OBJ_PROJ` at 0xD8000 / 0xD9000, magic "TID1", format 1 (AMB-1's format 4 plus the two
new values; 30 values a piece, 582 bytes). AMB-1 and its descendants keep theirs at 0xFC000, so neither overwrites the
other; a record with AMB-1's magic in TIDE's slot is not read (`tests/scenarios/tide.amb`, `storeamb`). AMB-1's
format-3 migration is gone. The panel calibration stays at 0xDC000 and carries over.

**Tests.** `tests/host/amb_test.c`: Mood leans the weather and the field, Space the plate. `tests/scenarios/tide.amb`:
every knob of the Tide, Bells and Ground pages, Hold, the help card, save and restore per piece, the recipe reset, the
foreign record. `tests/scenarios/hour.amb`: an hour of the whole app at Drift 100 with a screenshot every ten minutes.
`migrate.amb` (AMB-1's format 3) was removed. `ui.c`'s `accel` now honours the simulator's `spin` (it never did: a
second spin within 60 ms of the first was accelerated).

**Building here.** `. tools/env.sh` (from `fm1-ambient/`) sets the compiler shim, Emscripten 6.0.10 in
`fm1-ambient/toolchains/emsdk` (installed in the project folder; the version AMB-1's release workflow pins) and the
FriBidi path. Then in `tide/`: `python base/tools/build.py app --host` (the simulator), `python base/tools/build.py app
--web` (the browser emulator: `app/build/emu/`, served with `python -m http.server`), `sh tests/run_tests.sh`.
**Not built here:** the `.fwsc` package (the JieLi toolchain is Linux x86-64 only). `.github/workflows/release.yml`
is AMB-1's and builds it on GitHub (`workflow_dispatch`, or a `v*` tag) once the tree is in a GitHub repository; the
build's file stem is still `amb` (`app/fm1.toml` `name`), left alone until that build can be run, since the stem runs
through the workflow, `tests/run_tests.sh` and the web scripts. The package identity (`id_prefix`, AMB-1's `FM-1_7`)
is also AMB-1's still: choose TIDE's own before the first flash (Phase 7).

## 17. SAMSARA Phase 6c: the wheel (2026-11-11)

Tomorrow's Man, on what should be in the middle of the screen (TESTING #37): "perhaps a rendition of the Wheel of Samsara".
`app/ui.c`, `draw_wheel` (called first in `draw_ring`): eight gold spokes and a hub with an eye, inside the ring of
twelve notes, which is the wheel's rim (a thin gold circle under the note discs, radius `WH_RIM` 24; the notes are
its jewels). The drawing is our own, from the traditional eight-spoked form: a spoke is widest two thirds of the way
out and comes to a point at the rim (`wh_spoke`).

- **It turns with the harmony:** its angle is `en_drift` (the way to the next chord, 0..1) times one spoke's step
  (45 degrees), clockwise. Eight spokes alike, so the next chord carries on from where the last ended. At Hold and on
  a frozen chord `en_drift` stands, and so does the wheel.
- **Its gold sinks with the tide:** `mix(K_BELL_T, K_ACC, 5 + 11 * tide_px() / 80)`.
- **The chord's name** moved from the middle (the hub's place) to the top line of the garden, between the timbre
  anchor and the way to the next chord, in `FONT_S`.
- **Cost:** 53 by 53 pixels, one square root each and no arctangent (the pixel is turned into the wheel's frame and
  folded into one wedge), on each redraw of the garden (at most 20 a second, and only when something on it changed;
  `en_drift` was already in the redraw's signature in 40 steps a chord). Unmeasured on the device: Phase 7's CPU check.
