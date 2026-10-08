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
Not started. `psxstack/` is the stack at v0.2.1 (the same pin as dw2003recomp). `docs/PORT.md` has the plan, what
the game needs from the stack, and the open questions.

## Upstream
In sync with ReGame-Labs/dcb_decomp `main` at `be6a1dc` (2026-10-08).
