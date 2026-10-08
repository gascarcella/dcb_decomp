# Status

_Last updated: 2026-10-08_

The fork is set up, and the PC port links and boots to the title screen headless (M1 in progress). Upstream's decompilation is complete: every function and all
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
v0.3.1 (v0.3.0: the second game's stack work, optional heap, the stack's Psy-Q declarations, fibers, the replay
runners; v0.3.1: the shim's functions this game calls, psxstack #37, #38, #41, #45).
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
  layout kept: `PLAYER_PROFILES` at 0x800C8964), `StCdIntrFlag`'s type. `tests/port/run.py boot` and `title` reach
  every checkpoint with the emulator's stages and overlay sequence, deterministic, but the profile hashes cannot
  match at boot (#24, a decision); `new_game` stops at the registration's first dialog (#23). Also open: UBSan's
  alignment reports (#25), the movie's length (#26), the TMD readers (#22 item 4).
- Next: #23 (the dialogs), the decision on #24, then `new_game` and `first_duel` (#4).

## Upstream
In sync with ReGame-Labs/dcb_decomp `main` at `be6a1dc` (2026-10-08).
