# Status

_Last updated: 2026-10-08_

The fork is set up and the PC port has not started. Upstream's decompilation is complete: every function and all
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
v0.3.0 (the second game's stack work: optional heap, the stack's Psy-Q declarations, fibers, the replay runners).
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
- `game.json` has no heap (the game's is its own data); the shim compiles against the stack's declarations.
- Decided (2026-10-08): the hooks use a pointer-width typedef, `s32p` (`docs/PORT.md` "Open questions" 2).
- The stack work is in (psxstack #7, #25, #26 with #28, #27, released as v0.3.0).
- **The emulator oracle (issue #3) is in:** `scripts/setup.sh redux`, `scripts/check_emulator.sh`, and four replay
  scripts (`boot`, `title`, `new_game`, `first_duel`) with their records, each deterministic over two runs, on
  PCSX-Redux's interpreter core (its dynarec cannot run this game: `docs/PORT.md` "Testing"). CI's `replay` job runs
  them. `tests/port/run.py` is written for M1 and cannot run before the port links.
- Next: #4 (M1).

## Upstream
In sync with ReGame-Labs/dcb_decomp `main` at `be6a1dc` (2026-10-08).
