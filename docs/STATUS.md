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
v0.2.1 (the same pin as dw2003recomp).
- `port/` (`CMakeLists.txt`, `game/game.json`, the empty adapter, `tools/port_inputs.py`, `tools/port_inventory.py`)
  configures from the tracked sources alone: 155 units, 7 overlays in one slot. No upstream file changed.
- **The baseline** (`scripts/probe.sh`, CI's `probe` job): 35 of 155 units compile; 120 fail with 3,732 gating
  diagnostics (3,246 sites), 3,637 of them int/pointer casts; 4 duplicate overlay globals. The triage by kind, and
  what each needs, is `docs/PORT.md` "The host-compile probe". About 1,900 sites follow from some 40 declarations.
- `game.json`'s heap is a placeholder until psxstack makes `memory.heap` optional (psxstack #26); the shim cannot
  compile against this tree until psxstack owns its Psy-Q declarations (psxstack #7).
- Next: the hooking-strategy decision (`docs/PORT.md` "Open questions" 2) and the stack work (psxstack #7, #25, #26,
  #27), then issue #3 (the oracle) and #4 (M1).

## Upstream
In sync with ReGame-Labs/dcb_decomp `main` at `be6a1dc` (2026-10-08).
