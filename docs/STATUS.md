# Status

_Last updated: 2026-10-08_

The fork is set up, and **M1 (headless boot) is complete**: the PC port replays the four emulator scripts, twice
identical, clean under ASan/UBSan; M2 (rendering) has started, and the game runs in a window with its launcher (docs/PORT.md "Running it"). Upstream's decompilation is complete: every function and all
data of the USA, Japanese and European releases match (upstream's README "Status").

## The PS1 build
- **`us` builds byte-identical here:** `scripts/build.sh` gives `OK` for `SLUS_013.28` and the seven overlays
  (`ENDSEG`, `EVOSEG`, `KAWSEG`, `OPENSEG`, `SAISEG`, `SUBSEG`, `SUGSEG`) in about 11 s. It used the project-local
  toolchain: binutils 2.42 in `bin/cross`, the host's cpp (GCC 16) as `mipsel-linux-gnu-cpp`, and Python 3.12 in
  `.venv`.
- `jp` and `eu` are not built here: their discs are not in the data checkout yet.
- **CI:**
  - The fork's `fork.yaml` builds `us` from the data checkout and compares every binary.
  - Upstream's `build.yaml` runs its `names` job (`check_names.py`, `hacks.py --check README.md`); its build job
    skips forks.

## The PC port
**M0 (issue #2) is done: the skeleton configures and the host-compile probe runs.** `psxstack/` is the stack at
v0.3.4 (the launcher self-test and `--fps` help read the game's rate, #57; the GPU's two known title differences against PCSX-Redux documented, #54; v0.3.3: LIBC2's `rand`/`srand`, the CD's rate following `video.rate`, `port_test`'s crash directory and ASan default; v0.3.2: a checkpoint without an image; v0.3.1: the shim's functions this game calls, psxstack #37, #38, #41, #45; v0.3.0: the second game's stack work, optional heap, the stack's Psy-Q declarations, fibers, the replay
runners).
- `port/` (`CMakeLists.txt`, `game/game.json`, the empty adapter, `tools/port_inputs.py`, `tools/port_inventory.py`)
  configures from the tracked sources alone: 155 units, 7 overlays in one slot. No upstream file changed.
- **The baseline** (`scripts/probe.sh`, CI's `probe` job): 35 of 155 units compile; 120 fail with 3,732 gating
  diagnostics (3,246 sites), 3,637 of them int/pointer casts; 4 duplicate overlay globals. The triage by kind, and
  what each needs, is `docs/PORT.md` "The host-compile probe". About 1,900 sites follow from some 40 declarations.
- **M1 step 1, the hook header and the declarations** (#4): `include/port.h` (the PS1 side of the hooks, `s32p`/`u32p`),
  `s32p` at the pointer-holding declarations, `setaddr` through `PTR_TO_U32`, the missing prototypes, `game.h`'s
  Psy-Q prototypes agreeing with the stack's (`decls`: 45 same, 9 compatible, 0 mismatching). The probe: **67 of 155
  units compile; 88 fail with 1,016 diagnostics**, 945 of them explicit `(s32)` casts of pointers (step 2), no
  implicit declaration left; 5 duplicate overlay globals (`D_801DDF38` shows up now that its units compile).
- **M1 step 2, the casts** (#10, #13-#16 and the final pass): the probe is **140 of 155 units compile; 15 fail with 49
  diagnostics** (was 128 / 271 before the final pass); what is left is step 3 (script VM registers, heap, `tmd_sort.c`).
- The executable's game `.bss` (143 labels the C only declares) is generated C, a MAIN unit (`port/tools/port_bss.py`,
  `docs/PORT.md` "Beyond the gate"): the probe counts 141 of 156 (the 49 unchanged), `link` 357 undefined
  (481 without it).
- **M1 step 3, the last diagnostics** (the heap's block table, the scratchpad, the OMD words, the script registers,
  the effects' parent, issue #11's labels, `decls` against psxstack main): **155 of 156 units compile**; the one left
  is `task.c`'s `<kernel.h>` (the scheduler glue). `decls`: 0 mismatching against v0.3.0 and psxstack main.
- `game.json` has no heap (the game's is its own data); the shim compiles against the stack's declarations.
- Decided (2026-10-08): the hooks use a pointer-width typedef, `s32p` (`docs/PORT.md` "Open questions" 2).
- The stack work is in (psxstack #7, #25, #26 with #28, #27, released as v0.3.0).
- **The emulator oracle (issue #3) is in:** `scripts/setup.sh redux`, `scripts/check_emulator.sh`, and four replay
  scripts (`boot`, `title`, `new_game`, `first_duel`) with their records, each deterministic over two runs, on
  PCSX-Redux's interpreter core (its dynarec cannot run this game: `docs/PORT.md` "Testing"). CI's `replay` job runs
  them. `tests/port/run.py` is written for M1 and cannot run before the port links.
- M1: `port/include/gte.h` has the 40 GTE macros on the shim's software GTE (the six units that use them compile
  with it); `scripts/gte_test.sh` checks them (CI's `probe` job runs it).
- M1 step 3a, the task scheduler on the host: `port/game/tasks.c` is `startup.s`'s glue over psxstack's fibers
  (`spawnTask`, `endTask`, `resumeTask`, `exitTask`, `yieldTask`, `waitFrames`, `launchTaskScheduler`), `task.c`'s
  `PC_PORT` blocks create, destroy and preempt the fibers, `main()`'s idle loop ticks (`PLATFORM_WAIT()`), and
  `spawnTask`/`resumeTask` calls pass every argument at pointer width (docs/PORT.md "The task scheduler", "On the
  host"). `scripts/tasks_test.sh` (CI's `probe` job) runs seven tasks through the game's scheduler against the order
  derived from the PS1's rules. The probe: **156 of 156 units compile, 0 diagnostics** (`-m64` and `-m32`); `link`:
  the 5 duplicate overlay globals, 328 undefined.
- **M1 step 3c, the overlays and the adapter:** `loadFileToAddress` tells the overlay manager which overlay it read
  (`game_overlay_id`, from `port_inputs.py`'s `overlay_ids.h`); the EXE's 211 calls into overlays need no tag (each
  name is its overlay's own and links directly); the stale KAWSEG addresses are never reached (no effect script
  creates kind 0) and halt on the host; no duplicate global left; the adapter's probes (`port/game/state.c`: stage,
  map, the profile image in its PS1 layout, the scripts' `wait_mem` targets, `VOLATILE`); `PLATFORM_WAIT` in the
  CD and memory card polls (`docs/PORT.md` "Overlays", "Busy-waits", "Testing").
- **M1 step 4, the link and the first boot:** the pin is v0.3.1. `scripts/probe.sh` is a gate now and passes: 156 of
  156 units, 0 diagnostics, no duplicate global, `decls` 0 mismatching (`game.h` and `evoseg.h`: the `PC_PORT` side of
  `OpenEvent`/`EnableEvent`, `CdSearchFile`, `CdSync` and the `void` returns takes the stack's form, `GsWorkBase` is a
  pointer on the host; #22 items 1-3), `psyq/check.sh` 0 missing. **The port links** (`scripts/port_build.sh`; CI's
  `probe` job): the 71 overlay `undefined_syms` names the C references are `#define`d onto their fields (#20), and
  `port_gen.py sections` passes. **It boots headless to the title** (`scripts/port_build.sh --boot`, CI's `replay`
  job): `main()`, `runMainTask`, the CD, OPENSEG at frame 163 (stage 8), the opening movie, OPENSEG again at 5583 and
  the title's PRESS START at 6013 (the emulator: 827 and 7866); the sanitizer build too, with the same log. The
  blockers on the way: the frame buffer read before the render loop runs, the card directories' size (the heap's PS1
  layout kept: `PLAYER_PROFILES` at 0x800C8964), `StCdIntrFlag`'s type. `tests/port/run.py boot` and `title` pass and
  are a CI gate (#24 decided: the checkpoints before the profile is defined are `"image": false`, the
  `rand()`-drawn `cardCopySerials` are volatile, the four records re-recorded). Also open: the movie's length (#26),
  the TMD readers (#22 item 4).
- **The dialogs (#23):** every dialog object is one `Dialog` on the host (`ChoiceDialog`, `Window`, `DialogK`,
  `EvoDialog`, `DUEL_DIALOG`, `SAI_DIALOG`, the `u8[0xB8]` stack buffers, `DeckScreen.dialog`), and `Model2220` is the
  `Model` (`docs/PORT.md` "Memory and pointers"); two reads through null pointers the PS1 survives (`initDialog`'s
  text 0, `unloadModelAnimations` after `unloadModel`) take OpenBIOS's zeros. **`tests/port/run.py new_game` and
  `first_duel` run to their end**: SAISEG at 9109 and KAWSEG at 11560 (the emulator: 11681, 14211), every stage, map
  and overlay load the emulator's, deterministic, the five image-less checkpoints pass; the sanitizer build too, with
  no report the base did not have. The `saiseg` and `first_duel` hashes match too, with the bytes the game never writes and the
  `rand()` starter card (`cardCollection` 28 and 137) in `VOLATILE_RANGES`: **all four scripts pass the port test and
  gate CI** (about 100 s).
- **M2 (rendering) started: the port's VRAM and pictures against the emulator's** (`tests/port/vram.py`, CI's `replay`
  job; `docs/PORT.md` "Testing"). At the four scripts' 7 checkpoints and 5 more moments (the name entry, the starter
  list, the title 120 frames on, SAISEG's first area and first message), keyed by name. **The dumps are aligned on the
  game's state** (#33): before each, the script's variant waits for the scrolling background's position, a page typed
  out or SAISEG's message arrow, so the frame counts the loads took no longer matter. The textures and CLUTs are equal
  at every dump; the whole VRAM and the picture at `openseg_loaded`, `title+120`, `saiseg` and `first_duel`; the
  registration and SAISEG screens differ in 51 to 165 pixels (one row or column of stretched textured quads), the
  title in 2 and 5: the software GPU against PCSX-Redux (psxstack#54, known in `tests/port/vram_known.json`).
  `starter_chosen` also shows the player's model in another pose (its idle animation runs from the page, so the wait
  for the background moves its phase). The emulator is not CPU-bound at any of them (a frame every vsync).
- **M1 closed (#25):** the pin is v0.3.3. The port's frames moved with the CD's 60 Hz rate (`openseg_loaded` 181,
  `title` 7057, `title_menu` 7059, `name_entered` 7634, `starter_chosen` 8148, `saiseg` 10203, `first_duel` 12686;
  the emulator: 827, 7866, 7868, 8541, 9102, 11681, 14211); with the PS1's `rand` the starter's five bonus cards fall
  differently, so all ten of the Veemon deck's are in `VOLATILE_RANGES` (new_game and first_duel re-recorded: only
  their stable hashes changed); `random_index` is `port_rand_seed()`, recorded, not compared. **The heap on the host**
  keeps the PS1's block table (PS1 addresses: `PLAYER_PROFILES` still 0x800C8964) and spaces the blocks twice as wide,
  so each starts 8-aligned, with the PS1's leftover bytes copied in (`docs/PORT.md` "Memory and pointers"). The
  sanitizer build has no report on any script: the alignment reports are gone, `HUFFMAN_LEFT`/`RIGHT` are arrays on
  the host, and `assignCardCopySerial`'s in-struct overrun is in `tests/port/ubsan.supp`. `tests/port/run.py
  --sanitize` gates CI's `replay` job with the four scripts.
- **The duel's views and sizes (#30, #31):** `EvoModel`, `ModelData` and the other views of the duel's objects by PS1
  offsets (the duel state, the profile, the players, the HUD panels, the camera, the effects' transforms) agree with
  the host's layout; the heap blocks of host-grown types keep their PS1 sizes, which the host's types fit
  (`HOST_FITS`; `docs/PORT.md` "Memory and pointers"). A scratch script past `first_duel` (presses through the
  tutorial) runs the first round of the tutorial duel on the port: the support card, SUGSEG's polygon battle
  (attacks, damage, SUGSEG at frame 19594) and back to KAWSEG (21262), then round 2 (the opponent digivolves into
  Frigimon, the Battle Phase), with no crash, to frame 27586, where the blind presses stall at a card select. The
  sanitizer build had no report the base did not have. The emulator cannot replay frame-timed presses there (its
  timing differs): a duel replay script needs waits on the duel's state (#37).
- **M2's second half, the desktop build** (docs/PORT.md "Running it"): `scripts/setup.sh sdl` builds SDL3, DXC and Dear
  ImGui at psxstack's pins (the stack's own setup, in a checkout of it under `bin/psxstack-tools/`; 27 s with 32 jobs);
  `scripts/port_build.sh --sdl` builds the window build (`build/port-sdl/dcb`) and passes the stack's input self-test;
  `--sdl --boot` boots in an offscreen window whose log and picture equal the headless build's.
  `scripts/launcher_build.sh` builds `dcb-launcher` from `game.json`; its self-test passes with the real disc and game
  fully (301 of 301; psxstack v0.3.4 reads the game's rate, #57). On this desktop (Wayland,
  NVIDIA) the `new_game` script ran in a real window with each renderer (`--renderer gpu`: Vulkan) to SAISEG, 60.00
  vsyncs a second, both logs equal to the headless run's; the pictures (the movie, the title, the registration) are
  right, the GPU renderer's equal to the software one's. The SPU's output is not silent (not yet compared: M3). CI's
  `desktop` job runs both scripts, its tools cached on the stack's pins. The game crashes without a disc (#39), so the
  input self-test needs it. Ready for a play-test by hand.
- **The release path** (docs/PORT.md "Running it" Windows, "Releases"): `scripts/build_windows.sh` cross-builds the game
  and the launcher for Windows with llvm-mingw (`scripts/setup.sh windows`: the stack's steps at its pins); under Wine
  (`--test`, CI's `windows` job) the launcher's self-test (301 of 301, with the disc and `dcb.exe`), the input
  self-test and the `boot` and `title` replays pass, and `boot`'s frame log, record and SPU trace are byte-identical to
  the Linux headless build's. Two game-side Windows fixes: `bzero`/`bcopy`, missing from the UCRT
  (`port/game/libc_win32.c`), and clang's duplicate-typedef error in `state.c`. `scripts/package_appimage.sh` and
  `scripts/package_windows.sh` make `dcb-<version>-linux-x86_64.AppImage` (5 MB, glibc 2.38+) and
  `dcb-<version>-windows-x86_64.zip` (4 MB; the PDBs in `-debug.zip`), each smoke-tested (303 of 303 inside the
  package, with the disc); `release.yml` drafts a release on a `vX.Y.Z` tag, which the owner publishes by hand;
  `scripts/release_local.sh` builds the same in Docker `ubuntu:24.04` (under 2 minutes warm). No release yet.
- Next: a replay script through the tutorial duel (#37) and the rest of the duel (#4); M2: the duel's VRAM, the
  stretched quads' row or column (psxstack#54).

## Upstream
In sync with ReGame-Labs/dcb_decomp `main` at `be6a1dc` (2026-10-08).
