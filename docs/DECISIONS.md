# Decisions

The decisions that shape how this fork works, a few lines each: what and why. Code and docs cite them as
`DECISIONS "<heading>"`; keep the headings stable. Upstream's own rules for the decompilation are in
[CONTRIBUTING.md](../CONTRIBUTING.md) and are not repeated here.

## A fork of ReGame-Labs/dcb_decomp, for a PC port on psxstack
_Decided: 2026-10-08_

This repository is a GitHub fork of [ReGame-Labs/dcb_decomp](https://github.com/ReGame-Labs/dcb_decomp), juandav's
matching decompilation of Digimon Digital Card Battle (100 % of the code and data of the USA, Japanese and European
releases). Its author agreed to the fork. The decompilation is theirs; what this fork adds is a PC port built from
the same C on [psxstack](https://github.com/gascarcella/psxstack), the stack first built for
[dw2003recomp](https://github.com/gascarcella/dw2003recomp). psxstack's contract plans for this case: a third-party
decomp is forked and carries the port's hooks as a small patch set (psxstack `docs/GAME_CONTRACT.md` "3. The hooks in
the game's C").

## Upstream stays mergeable: merge it in, keep our diff to their files small
_Decided: 2026-10-08_

`upstream` is ReGame-Labs/dcb_decomp. Its `main` is merged into ours on a branch, through a pull request; `main` is
never rebased, since the fork is public. Our additions live in files of our own (`scripts/`, `docs/`, `CLAUDE.md`,
`port/`, `psxstack/`, `.github/workflows/fork.yaml`). Changes to their files are the hooks the port needs and a credit
banner at the top of `README.md`, nothing else: no renames, no reformatting, no reorganisation, so that each upstream
merge stays easy. A fix that belongs to the decompilation itself goes to upstream as a pull request, when the owner
says so, rather than living only here.

## The PS1 build stays byte-identical
_Decided: 2026-10-08_

Every change to `src/`, `include/`, `config/`, `mk/` or the `Makefile` keeps `make compare` matching
(`scripts/build.sh`). The port's hooks follow psxstack's rule (`GAME_CONTRACT.md` "Layering", rule 5): each macro's PS1
side expands to exactly the original code, and the host side exists only under `#ifdef PC_PORT`. So the matching
build never depends on the stack, and the hooks could go upstream unchanged if upstream wants them. The fork's CI
builds `us`, the only disc in the data checkout so far. Code that only `jp` or `eu` compiles is checked with those
discs once they are available.

## USA first
_Decided: 2026-10-08_

The port starts with `VERSION=us` (SLUS-01328). It is upstream's default build and the most complete one: the Psy-Q
libraries are decompiled, and the start-up and soft-float code are written as source. Japan and Europe follow on the
same source; each version is a separate build (`#if VERSION_*`), so each would be its own port binary. The European
`INTSEG` and `NISSEG` are a Japanese debug build linked against another executable, and are left until last.

## The disc comes from the data checkout
_Decided: 2026-10-08_

The owner's private repository `gascarcella/dcb-gamedata`, cloned beside the main checkout as `../dcb-gamedata`
(`scripts/gamedata_dir.sh`; `$DCB_GAMEDATA` overrides), holds the disc image, the executable and `P.DRV` that the build
reads, a BIOS and the pinned emulator. `scripts/setup.sh gamedata` links `disks/us/` from it, and CI checks it out
through a read-only deploy key (`GAMEDATA_DEPLOY_KEY`). Nothing from it is ever copied into this repository. Without
it, a contributor dumps their own disc as upstream's README says.

## Project-local toolchain, no sudo
_Decided: 2026-10-08_

Upstream's build expects Ubuntu's `binutils-mipsel-linux-gnu` and `gcc-mipsel-linux-gnu`. `scripts/setup.sh` builds
binutils 2.42 into `bin/cross` (the version Ubuntu 24.04 ships, the same as upstream's CI) and wraps the host's `cpp` as
`mipsel-linux-gnu-cpp` (the build runs it with `-undef -nostdinc`). It also fetches a Python 3.12 through uv into
`bin/python`, makes `.venv`, and runs upstream's `tools/dl_deps.sh`. The toolchain is symlinked into `.venv/bin`, so
activating the venv, which upstream already asks for, is the only step. Worktrees link the main checkout's `bin/`,
`.venv` and `disks/` (`scripts/worktree_init.sh`). A system toolchain, where one exists, is used instead of building.

The window's and the launcher's tools (SDL3, DXC, Dear ImGui; 2026-10-08) are the stack's pins, built by the stack's
own `scripts/setup.sh` rather than a copy of its steps here (dw2003recomp keeps a copy and must hold its pins equal by
hand): `scripts/setup.sh sdl` checks the stack out at the submodule's commit in `bin/psxstack-tools/`, because the
stack's setup installs into its own checkout's `tools/`, which for a submodule would be inside `.git/`. A pin bump
in the stack rebuilds them; CI caches them on `psxstack/scripts/setup.sh --pins`. The Windows build's and the
release's (llvm-mingw, SDL3 for Windows, SDL3 with the desktop backends: `scripts/setup.sh windows`, `sdl3-desktop`)
come the same way; the AppImage tools, which the stack does not pin, are our own `appimage` step (the first game's pins).

## psxstack is a submodule pinned by tag; changes to it are psxstack pull requests
_Decided: 2026-10-08_

`psxstack/` is the stack at a release tag (v0.2.1 at first, the same pin as dw2003recomp). What the port needs from
the stack (fibers for the task scheduler, Psy-Q declarations of its own, shim functions) is built in psxstack, under
its rules: no game facts in the stack, the contract first. Each change is checked against both consumers and released
as a tag; this fork then bumps its pin in a pull request. dw2003recomp is the reference consumer: how each part of
the contract was done there, and why, is in its `port/`, `tools/port_inputs.py`, `tests/` and `docs/`.

## Credit
_Decided: 2026-10-08_

Upstream's `LICENSE` (MIT, juandav) stays as it is. The README opens with a banner naming the decompilation and its
author. `docs/THIRD_PARTY.md` lists what the fork builds on, and the launcher's About text (`game.json`
`launcher.about`) credits the decompilation.

## Pull requests to the fork's main; docs are lean
_Decided: 2026-10-08_

Work lands on this fork's `main` through a pull request opened against `gascarcella/dcb_decomp` (`gh pr create --repo
gascarcella/dcb_decomp --base main`: gh otherwise proposes the parent repository). CI must be green, and the owner
reviews and merges. `docs/` holds reference documents only: the current state is a short `docs/STATUS.md`, and plans
and problems are issues (here and in psxstack). There is no session log: the pull requests say what was done.

## Releases: tagged drafts, published by hand
_Decided: 2026-10-08_ (as the first game's, its DECISIONS of the same name)

Players get a Linux x86_64 AppImage and a Windows x86_64 zip (each: the launcher, the game, the mods' manifests; the
debug info in separate files) and supply their own disc. A pushed tag `vX.Y.Z` runs `release.yml`: both packages built
on `ubuntu-24.04` (the AppImage's glibc floor) and smoke-tested, then a **draft** GitHub release. Publishing binaries
built from the decompiled code (no game data) is the owner's explicit decision each time. Unlike the first game's, the
release job does not rerun the tests that need the disc: `fork.yaml` runs them on every push to `main`, so the tag is
cut on a commit whose CI is green, and the release needs no data checkout. `scripts/release_local.sh` builds the same
packages in a Docker `ubuntu:24.04` container.

## Windows: cross-built from Linux with llvm-mingw, tested under Wine
_Decided: 2026-10-08_ (as the first game's, its DECISIONS "Windows: cross-built from Linux with llvm-mingw, tested
under Wine and Proton")

The Windows build of the game and the launcher is cross-compiled here with psxstack's toolchain file and its pinned
llvm-mingw (clang + lld, UCRT; lld writes the PDBs), SDL3 cross-built the same way: the stack's own setup steps in
`bin/psxstack-tools/` (`scripts/setup.sh windows`), no system package. Static, x86_64, Windows 10 or newer. Tested on
Linux under Wine (`scripts/build_windows.sh --test`, CI's `windows` job): the launcher's and the input self-tests, the
replays, and `boot`'s log byte-identical to the Linux build's. Real Windows comes from testers.
