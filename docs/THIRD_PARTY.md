# Third-party work

What this fork builds on, and what it borrows. Add a line when borrowing anything.

| What | From | Licence | How it is used |
|---|---|---|---|
| The decompilation: every file upstream has (`src/`, `include/`, `config/`, `mk/`, `tools/`, `Makefile`, `Dockerfile`, `.github/workflows/build.yaml`, `docker.yaml`, `README.md`, `CONTRIBUTING.md`, `TODO.md`, `.recomp.json`) | [ReGame-Labs/dcb_decomp](https://github.com/ReGame-Labs/dcb_decomp), by juandav | MIT (`LICENSE`) | This repository is a fork of it; upstream's `main` is merged in (DECISIONS "Upstream stays mergeable") |
| The PC port's runtime, Psy-Q shim, build and launcher | [gascarcella/psxstack](https://github.com/gascarcella/psxstack) | MIT | The `psxstack` submodule, pinned by tag |
| The shape of the port's game side: the scripts' structure (`scripts/setup.sh`'s binutils step, `scripts/gamedata_dir.sh`, `scripts/worktree_init.sh`, `scripts/probe.sh`), `port/CMakeLists.txt`, `port/tools/port_inputs.py` and `port/tools/port_inventory.py` (its `tools/port_inputs.py` and `tools/port_inventory.py`), `scripts/check_emulator.sh`, `tests/replay/replay.py`, `tests/replay/probes.lua` and `tests/port/run.py` (its own, as they consume psxstack's runners) and, as the port grows, its adapter | [gascarcella/dw2003recomp](https://github.com/gascarcella/dw2003recomp) (same owner) | MIT | Adapted to this game; a file copied from there names it in its header comment |
| The upstream submodules: maspsx, m2c, decomp-permuter, psyq_headers | as `.gitmodules` lists | their own | Unchanged, as upstream uses them |
| GNU binutils 2.42 (built by `scripts/setup.sh` into `bin/cross`, not in the repository) | [gnu.org](https://www.gnu.org/software/binutils/) | GPL-3.0 | The assembler and linker of the PS1 build |
