# Digimon Digital Card Battle: the PC port (a fork of ReGame-Labs/dcb_decomp)

**Goal:** a native PC port (Linux and Windows, with a launcher) of Digimon Digital Card Battle, built from the matching
decompilation's C on [psxstack](https://github.com/gascarcella/psxstack). This repository is a **fork** of
[ReGame-Labs/dcb_decomp](https://github.com/ReGame-Labs/dcb_decomp), juandav's decompilation (100 % of the USA,
Japanese and European releases, byte-identical). The decompilation is theirs; the port is ours, and its author agreed
to the fork. Two things must always hold:
1. **The PS1 build stays byte-identical** (`scripts/build.sh` ends with every binary `OK`).
2. **Upstream stays mergeable:** our changes to their files are only the port's hooks and the README banner
   (DECISIONS "Upstream stays mergeable").

## The neighbours (beside this repo's main checkout, `~/Projects/Games/DW2003Recomp/`)
| Checkout | What it is to this work |
|---|---|
| `../psxstack` | The stack: the runtime, the Psy-Q shim, `psxstack_add_game()`, the launcher, the tools. Also the `psxstack/` submodule here, pinned by tag. It has its **own `CLAUDE.md`**; read it before changing anything there. Its `docs/GAME_CONTRACT.md` is the line between game and stack |
| `../dw2003recomp` | **The reference consumer:** the first game ported on psxstack (Digimon World 2003), same owner. Every contract piece this port needs exists there, with the reasons: `port/CMakeLists.txt` and `port/game/` (adapter, `game.json`, mods), `include/port.h` (the hook macros' PS1 side), `tools/port_inputs.py` (the unit and overlay lists), `tools/port_inventory.py` (the host-compile probe's configuration), `tests/` (the emulator oracle, the pad-script replays, the port's M1 test), `docs/PORT.md` and `docs/DECISIONS.md` (from "PC port architecture" on). Look there before designing; borrow with a line in `docs/THIRD_PARTY.md` |
| `../dcb-gamedata` | The owner's **private** data checkout: the disc, the executable and `P.DRV`, a BIOS, the pinned PCSX-Redux (`gamedata/README.md`). Never copy anything from it into this repo |
| `../dw2003-gamedata` | The first game's data checkout; not needed here |

## Session protocol
0. **Worktrees (Orca):** a worktree has only tracked files. Run `scripts/worktree_init.sh` first (~1 s: links the main
   checkout's `bin/`, `.venv`, `disks/`; checks out the submodules from the main checkout's objects). The main checkout
   is set up once with `scripts/setup.sh` (a few minutes; it builds binutils).
1. **Start:** read `docs/STATUS.md` (the state) and `docs/PORT.md` (the plan and what is known about the game). Work
   items are issues: `gh issue list` here, `gh issue list -R gascarcella/psxstack` for the stack's.
2. **Work in phases.** Stop at the end of each phase to summarize, and wait for the user's go-ahead.
3. **Pull requests go to this fork:** branch, push to `origin`, then
   `gh pr create --repo gascarcella/dcb_decomp --base main`. Without `--repo`, gh proposes the parent repository;
   this clone's `gh repo set-default` points at the fork, but pass the flag anyway. CI must be green (check the run
   for the PR's head SHA with `gh api`), and the user reviews and merges. Never open a pull request or issue on
   ReGame-Labs/dcb_decomp unless the user asks for that specific one.
4. **End:** update `docs/STATUS.md` when the state changed; record a decision in `docs/DECISIONS.md` only when it
   changes how the project works (a few lines: what and why); leftover work becomes issues, not docs; commit with a
   clear message. There is no session log: the pull request says what was done.

## Working on psxstack
The port will need stack work: fibers for the game's task scheduler, Psy-Q declarations owned by the stack
(psxstack #7), shim functions this game calls and dw2003 doesn't (#8). Do it in `../psxstack`, or in a worktree of it,
under **its** `CLAUDE.md`:
- No game facts in the stack: no address, file ID, name or hash. They come from `game.json` or the adapter.
- A change to what a game supplies starts in `docs/GAME_CONTRACT.md`.
- Psy-Q names stay Sony's, and the shim reimplements behavior; it never decompiles Sony's code.
- Test against the consumers. There are now two: dw2003recomp, which must keep passing its `scripts/test.sh`, and
  this fork.

While developing, build the port here against the checkout (`-DPSXSTACK_DIR=$PWD/../psxstack`). After the psxstack
pull request merges and is tagged, bump the pin here in a pull request (`git -C psxstack fetch --tags && git -C
psxstack checkout vX.Y.Z`, commit the submodule). dw2003recomp bumps its own pin in its own pull request.

## Rules
- **Never commit game data:** no disc, `disks/`, split `asm/`, `assets/`, `expected/`, BIOS or Psy-Q SDK files. Check
  `git status` before committing. The data checkout is the only place for them.
- **Hooks keep the PS1 build identical** (psxstack `GAME_CONTRACT.md` "Layering", rule 5). Each macro's PS1 side
  expands to the original code exactly. Host-only code sits under `#ifdef PC_PORT`, and `<psxstack/hooks.h>` is
  included only there. Rebuild (`scripts/build.sh`) after every change to `src/`, `include/`, `config/`, `mk/` or the
  `Makefile`. The port's tools and the replay tools read the stack from the submodule, or from `$PSXSTACK_DIR` when it
  names another checkout (stack work in progress). A file that `jp` or `eu` also compiles must not change their code either: until those discs are in the
  data checkout, keep such edits to `#ifdef PC_PORT` blocks and macros whose PS1 side is the original text.
- **Upstream's conventions for anything in the decompilation** (`CONTRIBUTING.md`: names, fake-match markers, file
  layout, versions). They are not dw2003recomp's: here functions are camelCase (`spawnTask`), globals SCREAMING_CASE
  (`CURRENT_TASK`), overlay symbols prefixed (`SAI_`, `KAW_`, …). Port code of our own (`port/`) follows psxstack's
  naming (`port_*`, `game_*`).
- **Upstream sync:** `git fetch upstream && git switch -c sync-upstream origin/main && git merge upstream/main`, rebuild,
  then a pull request. Never rebase or force-push `main`.
- Ask before anything that needs sudo or installs system-wide. Tools go into `bin/` (gitignored) through
  `scripts/setup.sh` steps, never by hand. Setup steps live in `scripts/` or `docs/`, never only in shell history.
- Don't claim something works until it has been run. When unsure, say so and test.
- Credit stays visible: upstream's `LICENSE` untouched, the README banner, `docs/THIRD_PARTY.md`.

## Layout
| Path | Whose | Contents |
|---|---|---|
| `src/<binary>/`, `include/` | upstream | The matched C: `src/main/` (the executable; `psyq/` = the Psy-Q libraries decompiled, `startup.s` and `libmath.s` hand-written asm), `src/<overlay>/` per overlay (`openseg`, `saiseg`, `subseg`, `evoseg`, `kawseg`, `sugseg`, `endseg`; `intseg`, `nisseg` are jp/eu). `include/dcb/` the game's headers, `include/version.h` the `VERSION_*` switches |
| `config/<version>/` | upstream | splat configs, symbols, checksums per version |
| `mk/version/<version>.mk` | upstream | Each version's executable, disc directory, overlays, compiler and source list |
| `tools/` | upstream | Their build helpers (`try_match.py`, `hacks.py`, `check_names.py`, `rename.py`, `extract_drv.py`, `objdiff_generate.py`, `dl_deps.sh`, …) |
| `external/` | upstream | Submodules: maspsx, m2c, decomp-permuter, psyq_headers |
| `Makefile`, `Dockerfile`, `.github/workflows/build.yaml`, `docker.yaml` | upstream | The build and their CI (its build job runs only in ReGame-Labs; its `names` job runs here too) |
| `scripts/` | ours | `setup.sh`, `worktree_init.sh`, `build.sh`, `gamedata_dir.sh`, `probe.sh` (the port's configure and host-compile probe), `gte_test.sh` (the host GTE macros' test), `check_emulator.sh` (the emulator boots the disc), `port_build.sh` (the port's build; `--boot`, `--sdl` the window build), `launcher_build.sh` (the launcher and its self-test) |
| `docs/` | ours | `STATUS.md`, `PORT.md`, `DECISIONS.md`, `THIRD_PARTY.md` |
| `tests/` | ours | `replay/` (the emulator oracle: `replay.py` configures psxstack's runner, `probes.lua` this game's state probes, `scripts/*.json` the pad scripts, `expected/*.json` their records), `port/run.py` (the port's replay test, from M1), `port/vram.py` + `vram.lua` + `vram_known.json` (M2: the VRAM and pictures against the emulator's), `port/gte_host_test.c` (the host GTE macros, `scripts/gte_test.sh`) |
| `psxstack/` | ours (submodule) | The stack at its pinned tag |
| `port/` | ours | The port's game side, as in dw2003recomp: `CMakeLists.txt` (`psxstack_add_game()`), `game/` (the adapter, `game.json`), `tools/` (`port_inputs.py`: the unit and overlay lists from `mk/version/us.mk`; `port_bss.py`: the executable's game `.bss` as C, a generated MAIN unit; `port_inventory.py`: psxstack's host-compile probe configured for this tree), `include/` (host-only headers first on the port's include path: `gte.h`, the 40 GTE macros on psxstack's software GTE), later `mods/`. Our tools live here, not in upstream's `tools/` |
| `.github/workflows/fork.yaml` | ours | The fork's CI: the `us` build from the data checkout (deploy key secret `GAMEDATA_DEPLOY_KEY`), the port's disc-free `probe` job (`scripts/probe.sh`, then `scripts/gte_test.sh`), the `replay` job (the disc and the emulator from the data checkout) and the `desktop` job (the window build and the launcher, their self-tests; the tools cached on psxstack's pins) |
| `bin/`, `.venv/`, `disks/`, `build/`, `asm/`, `expected/`, `assets/` | untracked | The toolchain (`bin/cross`: binutils + cpp wrapper, `bin/python`, upstream's downloads, `bin/redux` the emulator, `bin/psxstack-tools/` a checkout of the stack whose `tools/` holds SDL3, DXC and Dear ImGui: `scripts/setup.sh sdl`), the venv, the disc files, build outputs |

## Commands
```sh
scripts/setup.sh            # main checkout, once: submodules, binutils (bin/cross), Python 3.12, .venv, upstream's deps, disks/us from ../dcb-gamedata
scripts/setup.sh disc       # the whole disc image, disks/us/dcb_us.bin + .cue (for the emulator and the port), SHA-1 checked
scripts/setup.sh redux      # the pinned PCSX-Redux into bin/redux (the data checkout's zip, through psxstack's tools/replay/redux.sh)
scripts/setup.sh sdl        # SDL3, DXC, Dear ImGui at psxstack's pins into bin/psxstack-tools/tools (the stack's own setup; ~30 s-minutes; DCB_SDL_TOOLS_FROM=../dw2003recomp/tools links instead)
scripts/check_emulator.sh   # the disc boots headless in the emulator (OPENSEG loaded)
.venv/bin/python tests/replay/replay.py check [-j N]            # the emulator replays against tests/replay/records/ (CI's replay job)
.venv/bin/python tests/replay/replay.py run tests/replay/scripts/X.json [--record] [--repeat 2] [-v] [--prelude LUA]   # one script; --record writes its expected file
scripts/worktree_init.sh    # a fresh worktree: link bin/ .venv disks/, submodules at their pins (run first!)
scripts/build.sh [--clean]  # make generate + make + make compare for VERSION (default us); every binary must say OK
scripts/probe.sh            # the port: cmake configure (build/port), then the host-compile probe and link check over every us unit (no disc)
.venv/bin/python port/tools/port_inventory.py probe|link|counts [--sites KIND]   # the probe alone; build/port_inventory/
scripts/gte_test.sh         # the host GTE macros (port/include/gte.h) against psxstack's software GTE; no disc
scripts/tasks_test.sh       # the task scheduler (task.c + port/game/tasks.c) on psxstack's fibers; no disc
scripts/port_build.sh [--boot]   # the headless port, build/port/dcb (--boot: 600 frames from the disc to OPENSEG)
scripts/port_build.sh --sdl [--boot]   # the window build, build/port-sdl/dcb, and its --input-test (offscreen; needs the disc); --boot: its log equals the headless run's
build/port-sdl/dcb --disc disks/us/dcb_us.cue --window [--renderer gpu] [--memcard1 build/card1.mcd]   # play (docs/PORT.md "Running it": keys, where saves go)
scripts/launcher_build.sh   # build/launcher/dcb-launcher and its self-test (offscreen; with the disc and build/port-sdl/dcb, the real game too)
.venv/bin/python tests/port/vram.py [SCRIPT ...] [-j 4] [--keep]   # M2: VRAM, textures and picture vs the emulator per checkpoint (known: tests/port/vram_known.json; PNGs in build/port-vram/)
cmake -S port -B build/port -G Ninja [-DPSXSTACK_DIR=$PWD/../psxstack]           # the port's configure (M0: configures; the build links from M1)
. .venv/bin/activate        # then upstream's commands work as their README says (the toolchain is in .venv/bin):
make generate               #   splat: asm/us, build/us/generated (after config changes)
make -j$(nproc) && make compare   #   build, check SHA-1s of the executable and every overlay
make report                 #   objdiff report (build/us/report.json)
python3 tools/hacks.py --check README.md   # the README's fake-match counts (upstream CI's names job)
python3 tools/check_names.py               # names agree across versions (upstream CI's names job)
python3 tools/try_match.py ...             # upstream's single-function match helper (see its header)
bin/mkpsxiso-2.20-Linux/bin/dumpsxiso -x DIR -s DIR/x.xml IMAGE.bin   # dump a disc
gh pr create --repo gascarcella/dcb_decomp --base main   # always to the fork
```
The whole disc image (the emulator and the port read it) is opt-in: `scripts/setup.sh disc` rebuilds
`disks/us/dcb_us.bin` + `.cue` from the data checkout and checks its SHA-1 (`b3945b3e…`).

## Docs
`docs/STATUS.md` the state · `docs/PORT.md` the port: plan, what the game needs, the open questions ·
`docs/DECISIONS.md` how the fork works and why · `docs/THIRD_PARTY.md` credits and borrowings · upstream's
`README.md` (the decompilation, its build, the versions) and `CONTRIBUTING.md` (its conventions) · psxstack's
`docs/GAME_CONTRACT.md`, `docs/PORT.md`, `docs/RUNTIME.md` · dw2003recomp's `docs/PORT.md`, `docs/DECISIONS.md`,
`tests/README.md`.
