# The PC port

The port compiles upstream's matching C for a 64-bit host and links it with
[psxstack](https://github.com/gascarcella/psxstack): its runtime, its Psy-Q shim and its launcher. The stack's
`docs/GAME_CONTRACT.md` defines what this game supplies. [dw2003recomp](https://github.com/gascarcella/dw2003recomp) is
the worked example of every piece (its `docs/PORT.md`, `port/`, `tools/port_inputs.py`, `tests/`). This page holds
the plan, what this game needs from the stack, and the open questions. It will become the port's reference as the
answers land.

M0 (the skeleton) exists: `port/` configures and the host-compile probe runs ("The host-compile probe"). The facts
below come from a survey of the `us` sources on 2026-10-08: the 155 C files that `mk/version/us.mk` lists. Counts
are from grep and approximate.

## Plan
The milestones follow dw2003recomp's (its `docs/PORT.md`):

| Milestone | What | Done when |
|---|---|---|
| **M0: the skeleton** | `port/CMakeLists.txt` with `psxstack_add_game()`; `port/game/game.json` (the US disc); an empty adapter; a `tools/port_inputs.py` that writes the unit and overlay lists from `mk/version/us.mk` and `config/us/`; the host-compile probe (`psxstack/tools/port_inventory.py probe`) configured for this tree | The probe lists every unit's host errors: the real count of hooks to add |
| **M1: headless boot** | The hooks in the C; the stack work below (fibers, the shim's gaps); an emulator oracle (PCSX-Redux from the data checkout) and pad-script replays: boot, title, new game, first duel | The port replays the scripts with the emulator's checkpoints, twice byte-identical, clean under ASan/UBSan |
| **M2: rendering** | The software GPU against the emulator's VRAM; the SDL3 window, keyboard and gamepad | The checked frames and VRAM equal the emulator's |
| **M3: sound** | libsnd SEQ playback and the SPU against the emulator's SPU write traces | Traces equal on the emulator's timeline |
| **M4: saves** | libcard and the BIOS file calls over `.mcd` images | Saves move both ways between the port and the emulator |
| **M5: the whole game** | The opening movie (libcd streaming + MDEC), every overlay reached by a test, the launcher, packaging, mods | A release |

## Running it
The desktop build: the game in a window (psxstack's docs/RUNTIME.md "The window") and the launcher (psxstack's
`launcher/README.md`). It needs the disc image (the game cannot start without one: #39) and the window's tools.
```sh
scripts/setup.sh disc sdl            # the disc image; SDL3, DXC and Dear ImGui at psxstack's pins (bin/psxstack-tools/)
scripts/port_build.sh --sdl          # build/port-sdl/dcb, then its input self-test (offscreen)
build/port-sdl/dcb --disc disks/us/dcb_us.cue --window --memcard1 build/card1.mcd   # play; --renderer gpu: Vulkan
scripts/launcher_build.sh            # build/launcher/dcb-launcher, then its self-test (offscreen)
build/launcher/dcb-launcher          # choose the disc on its Disc screen, then Play
```
`scripts/setup.sh sdl` runs the stack's own `scripts/setup.sh sdl3 dxc imgui` in a checkout of the stack at the
submodule's commit, `bin/psxstack-tools/` (the stack's setup installs into its own checkout's `tools/`; a few minutes,
mostly SDL3). SDL3's backends follow the `-dev` headers present: with no X11/Wayland headers only the offscreen driver is
built (`psxstack/scripts/setup.sh --sdl3-desktop-apt` lists Ubuntu's packages). With the first game's checkout beside
this one, `DCB_SDL_TOOLS_FROM=../dw2003recomp/tools scripts/setup.sh sdl` links its built tools instead (the stack's
setup checks them against its pins). `PSXSTACK_TOOLS_DIR=<dir>` points both build scripts at other tools.

**Keys** (by position; the stack's defaults, rebindable on the launcher's Controls screen): arrows the D-pad, X cross
(confirm), C circle, Z square, S triangle, Enter START, Backspace or right Shift SELECT, Q L1, E R1, 1 L2, 3 R2; P
pauses, F11 toggles fullscreen. A gamepad in the PlayStation layout (south cross, east circle, ...; the left stick is
the D-pad too). Fast-forward (hold Tab; 4x) is a mod, off until switched on in the launcher's Mods screen: the way through
the two-minute opening movie.

**Where things go.** Started bare, the game reads no settings and its memory cards are fresh ones in memory, lost at exit
(`--memcard1 FILE` keeps one; created formatted when missing). Started from the launcher, everything is in the
settings directory, `~/.local/share/dcb/` (`$XDG_DATA_HOME/dcb/`; `--config-dir DIR` or `$DCB_CONFIG_DIR` overrides):
`settings.json`, `card1.mcd` and `card2.mcd`, `logs/last-run.log` (the game's output), `crashes/` (crash reports).
The launcher finds the game beside itself, else at `build/port-sdl/dcb` beside `build/launcher/` (the development tree).

**State at M2** (2026-10-08): the window and the launcher work on this desktop (Wayland, both renderers; the GPU
renderer on Vulkan gives the software renderer's picture at internal scale 1). A window run's log is the headless
run's byte for byte. Known unfinished: **sound** is M3: the SPU plays the game's sequences through the stack's LIBSND
and SPU (the output is not silent), but nothing has compared it with the emulator's; **saves** are M4: saving to a card
works within the port (the `new_game` script saves to a fresh card), but cards have not moved between the port and the
emulator; the opening movie (about two minutes, unskippable as on the PS1) ends some 1,200 frames early (#26); the duel
has been run only through its tutorial's first round (#37); the launcher's self-test has one known failure, a check
that hard-codes the first game's 50 Hz ([psxstack#57](https://github.com/gascarcella/psxstack/issues/57)).

## The contract, for this game
| Contract input | This game (`us`) |
|---|---|
| `game.json` identity | `port/game/game.json`: `id` `dcb`, title "Digimon Digital Card Battle", `env_prefix` `DCB`, `launcher.about` crediting the decompilation (DECISIONS "Credit") |
| Disc | SLUS-01328, "Digimon Digital Card Battle (USA)": SHA-1 `b3945b3e76c1fcc554a7614e2b4211d974990105`, 215661936 bytes, one MODE2/2352 track |
| `video.rate` | 60 (NTSC) |
| `memory.slots` | **One slot**, `overlay`, the overlay area at `0x801DDF38` (`OVERLAY_AREA`, `config/us/symbols_overlay_calls.txt`), `0x1EAF8` bytes: the largest overlay, KAWSEG (to `0x801FCA30`; OPENSEG reaches `0x801F8188`). Each overlay's zeroed data is inside its file, so file size = memory size. KAWSEG and OPENSEG run into the 32 KB the start-up reserves for the stack (`0x801F8000`-`0x80200000`) |
| `memory.heap` | **None** (psxstack 0.3.0 made it optional, [psxstack#26](https://github.com/gascarcella/psxstack/issues/26)): this game's heap is its own `.bss` array (`HEAP_ARENA`), game data the port saves, resets and snapshots like the rest; the arena holds the slot alone. See "Memory and pointers" |
| `UNITS`, `MAIN_UNIT` | `port/tools/port_inputs.py` writes them from `MAIN_C_SRC` and `<OVERLAY>_C_SRC` of `mk/version/us.mk`: 155 units and the generated game `.bss` (`port_bss.py`, "Beyond the gate"), without `src/main/psyq/` (the shim replaces the libraries), `startup.s` and `libmath.s` (host replacements, below). `main()` is `src/main/main.c:54`: it starts the scheduler with `runMainTask` (`src/main/system/boot.c:76`) and spins on `rand()` |
| `OVERLAYS` | ENDSEG, EVOSEG, KAWSEG, OPENSEG, SAISEG, SUBSEG, SUGSEG, all in slot 1. psxstack keys an overlay by a file ID; this game loads by name from `P.DRV` (below). Decided game-side (2026-10-08): the ID is the overlay's 1-based index in `us.mk`'s `OVERLAYS` list (`port_inputs.py`), and at M1 the adapter maps `"P:\\kawseg.bin"` to it in `loadFileToAddress`'s hook |
| `EXE_SYMBOLS` | `config/us/`'s symbol files (the EXE's names, and `symbols_overlay_calls.txt` for the 84 overlay functions the EXE calls) |
| `GTEMAC` | **Not passed.** `include/gte.h` has 40 `gte_*` macros, 33 of them `__asm__` over `include/gte_macros.inc`, used 270 times in 6 files (`tmd_sort.c` 230). psxstack's translator cannot take them: it rejects output operands (`gte_mfc2`'s `"=r"`), knows only `.word` commands (ours are the `.inc` mnemonics), keeps temporaries per macro (`gte_prefetchv3c` hands `$8`-`$13` to `gte_ldv3_prefetched`), and writes its override as `psyq/gtemac.h`, which never shadows `"gte.h"`. Decided (2026-10-08) and **done**: `port/include/gte.h`, a host-only header first on the include path with the real one's guard (`GTE_H`), has all 40 macros by hand over `psyq_gte_mtc2`/`mfc2`/`ctc2`/`cfc2`/`cmd`/`swc2_` (the mnemonics as `gte_macros.inc`'s command words; the prefetch pair's `$8`-`$13` a static six-word buffer). The six units that use them compile with it with no GTE diagnostic; `scripts/gte_test.sh` (`tests/port/gte_host_test.c`) checks every macro against the shim's software GTE (LIBGTE functions, rtpt = 3 x rtps, ncct = 3 x nccs, the formulas). The probe still stubs the macros with its own override (it does not need their bodies); a macro added upstream and missing from the host header shows as an implicit declaration in the port build |
| `INCLUDE_DIRS` | `port/include/`, `include/`, the root. Game code doesn't include the Psy-Q headers (`task.c` alone includes `<kernel.h>` for the BIOS TCB): `include/game.h` redeclares the types (`:69-151`) and the prototypes (`~:1485-1660`). There is no `include/psyq/`, so since psxstack 0.3.0 the shim compiles against the stack's own declarations (psxstack #7) and `port_inventory.py decls` compares `game.h`'s prototypes with them: 29 same, 9 compatible, 16 mismatching at M0 (`s32` returns where the stack returns `void` or a pointer, `ClearImage`'s colours); fixed game-side at M1, byte-identical: 45 same, 9 compatible, 0 mismatching (against psxstack main's 184 at step 3: 91 same, 12 compatible, 0 mismatching; `game.h` declares the stack's return types under `PC_PORT`, as for `LoadImage`). At v0.3.1 (286 functions; `decls` also reads `include/dcb/evoseg.h`): 142 same, 13 compatible, 0 mismatching, and `scripts/probe.sh` gates on it |

## The host-compile probe
### The baseline (M0, 2026-10-08, at 3802dee)
`scripts/probe.sh` configures the port from the tracked sources alone (`port/CMakeLists.txt`,
`port/tools/port_inputs.py`: 155 units, 7 overlays) and runs psxstack's inventory (`port/tools/port_inventory.py`:
gcc 16.2.1, `-m64`, psxstack v0.2.1's gate flags, `-DVERSION_US -DSKIP_ASM`, every `gte_*` macro a no-op). The
result is the baseline M1 drives to zero; CI's `probe` job runs it and uploads `build/port_inventory/summary.txt`.

**35 of 155 units compile; 120 fail with 3,732 gating diagnostics:** 2,328 int-to-pointer-cast (1,678 inside
`include/game.h`'s macros, 116 inside `include/dcb/frame_callback.h`'s), 1,309 pointer-to-int-cast (337 inside
`game.h`'s macros), 37 implicit-function-declaration, 29 incompatible-pointer-types, 19 int-conversion, 9 other
errors, 1 fatal. `link` over the 35 objects: 4 duplicate globals, 160 undefined (65 Psy-Q functions, 22 data globals,
62 defined in failing units, 11 other).

**Now (M1 step 2, casts final pass):** 140 of 155 units compile; 15 fail with 49 diagnostics: 23 pointer-to-int-cast,
22 int-to-pointer-cast, 2 incompatible-pointer-types, 1 int-conversion, 1 fatal (`kernel.h`). What is left is step 3: the
script VMs' `regs[]`/`vars[]`, `EffectTemplate`, `ScriptRunner.unk0`, the heap, `tmd_sort.c`, `scene3d.c`, the loader.

**After M1 step 3** (the heap, the scratchpad, the TMD words, the script registers, the effects' parent, the
`decls`, the aliased labels of issue #11; "Memory and pointers"): **155 of 156 units compile** (with the generated
`.bss` unit), at `-m64` and at `-m32`; the one left is `task.c`'s fatal `<kernel.h>` (the scheduler glue replaces
it). `decls`: 0 mismatching against v0.3.0 and against psxstack main (91 same, 12 compatible of 184). `link`: the
same 5 duplicate overlay globals, 323 undefined (357 before: the aliased labels are fields now).

**After M1 step 3a** (the task scheduler on the host, "The task scheduler"): **156 of 156 units compile, 0
diagnostics**, at `-m64` and at `-m32`. `link`: the 5 duplicate overlay globals, 328 undefined (the 5 more are
`task.c`'s references to `port/game/tasks.c` and psxstack's fibers, which the link check does not scan).

**After M1 step 1** (`include/port.h`, the declarations, the prototypes; 2026-10-08): **67 of 155 units compile; 88
fail with 1,016 diagnostics:** 945 pointer-to-int-cast (the explicit casts: `drawText` and its siblings 445,
`add`/`removeFrameCallback` 96, the GTE calls 138, `transformAndAdd*` 102, 111 assignments, 53 other calls), 29
incompatible-pointer-types, 23 int-to-pointer-cast, 18 int-conversion, 1 fatal (`task.c`'s `<kernel.h>`); none of
them in a header macro, no implicit declaration, no other error. Worst files: `sub_deck_screens.c` 142,
`evo_shatter.c` 89, `sug_sphere.c` 62, `battle_hud.c` 59, `endseg.c` 53. `link` over the 67 objects: 5 duplicate
globals (`D_801DDF38`, the overlay area's first word, is defined in `kaw_cpu.c`, `sai_data.c` and `sug_history.c`).

### The triage
The 3,732 are 3,246 distinct sites (a site inside a header macro counts once per C line that expands it). Sorted by
kind, with the fix each needs. "Typedef" = a pointer-width integer type (`s32` on the PS1, `intptr_t` under
`PC_PORT`) at a declaration; "cast edit" = the same type written at the cast site (`(s32)&x` becomes `(s32p)&x`,
byte-identical on the PS1); "macro" = psxstack's `PTR_TO_S32`/`PTR_TO_U32` or a `PC_PORT` block. Classified by
regexes over the diagnostics and the source lines: approximate to a few percent.

| Kind | Sites | Files | What fixes it | Examples |
|---|---|---|---|---|
| A1 `(T *)GLOBAL` inside a header macro: `CUR_SPRT` 894, `PLAYER_DATA` 391, `WP` 79, `DECK_CHOICE` 10 | 1,374 | 32 | **Typedef at 4 declarations** (`SPRITE_POOL_CURSOR`, `PLAYER_PROFILES`, `WINDOW_PRIM_CURSOR`, `DECK_CHOICE`: `extern s32` in `game.h`/`frame_callback.h`) | `card_render.c:261`, `card_db.c:265`, `window.c:294` |
| A2 `(T *)GLOBAL` written out in the C: `PLAYER_PROFILES` 283, `CURRENT_FRAME_BUFFER` 10, `KAW_DUEL` 3, `SESSION_DATA`, `SORT_WORK`, `STAGE_PAK`, … | 309 | 44 | **Typedef at the same declarations** (about 12 globals) | `card_db.c:254`, `card_db.c:378` |
| A3 pointer-to-int inside a header macro: `addPrim`/`setaddr` 241, `CARD_BYTE` 78 (an offsetof idiom), `SHATTER_QUAD`/`_TRI` 10 | 329 | 43 | **One macro edit each**: `setaddr` to `PTR_TO_U32` (the ordering-table tag, as dw2003), `CARD_BYTE` to an `offsetof` form under `PC_PORT`, the two SHATTER macros in `evo_cutscene.c` | `card_render.c:273`, `kaw_battle_sim.c:462`, `evo_cutscene.c:842` |
| B1 `(s32)&x` at a call: the GTE wrappers (`RotTransPers3/4` 70, `SetRotMatrix` 19, `RotAverageNclip4` 13, `transformAndAdd*` 70) and others | 254 | 20 | **Cast edit per site**; the prototypes in `game.h` (`s32 RotTransPers4(s32, …)`) get the typedef too | `card_render.c:829`, `card_render.c:843` |
| B2 `(T *)value`, an `s32` local or parameter: `waitFrames()`'s result 42, `freeHeapBlock()` 6, `loadFileTagged()` 5, `loadFile()` 4, `decompressToHeap()` 3, parameters `pak` 8, `text` 7, `tim` 4, `path` 4, … | 106 | 48 | **Typedef at ~8 return types and ~15 parameters** (`waitFrames` returns the resume value, a pointer; the loaders return buffers) | `card_db.c:231`, `card_render.c:38`, `card_render.c:104` |
| B3 `(s32)ident`, a pointer, function or array name as an integer: `drawText` and its siblings 290, `addFrameCallback`/`removeFrameCallback` 96, `LoadImage` 18, assignments 54, `AddPrim` 8 | 506 | 77 | **Cast edit per site**; the callees' parameters (`drawText(s32 x, s32 y, s32 text, …)`, `addFrameCallback(s32)`) get the typedef | `battle_hud.c:145`, `battle_hud.c:147`, `duel_session.c:162` |
| B4 `(s32)call()`, a pointer-returning call as an integer: `findPakChunk` 8, `allocHeapBlock` 2, `openDiscFile` | 13 | 7 | Cast edit per site | `player_data.c:176`, `player_data.c:210` |
| B5 `(s32)"literal"` into `drawText` and co. 120, assignments and tables 50, `mountDriveTask` 2 | 173 | 32 | Cast edit per site (the 3 in `open_save.c:58-60` are static initializers: the typedef also makes them constant) | `battle_hud.c:245`, `scene3d.c:210`, `open_save.c:58` |
| C2 `(T *)field`, a pointer read back from an `s32` struct field: `slots->slots` 14, `slot->value` 10, `Graphics.primSlots[]` 9, `obj->tmd[]` 4, `HeapBlock.addr` 2, `win->label` 2, `model->link`, `bg->tim`, … | 54 | 8 | **Per struct**: a field of the game's own in-memory structs gets the typedef, which widens the struct on the host (no raw-offset reader, see D); `HeapBlock.addr` keeps bit 31 as its flag, so `heap.c` gets `PC_PORT` macros for the flag and the address (one file, ~10 sites). Fields that mirror disc data (`obj->tmd`?) need a look each | `scroll_bg.c:73`, `scene3d.c:96`, `scene3d.c:136` |
| D raw-offset access `*(s32 *)((s8 *)p + 0x24)` | 1 | 1 | A `PC_PORT` field access (`file->size`) | `loader.c:100` |
| E implicit declaration, a game function without a prototype (36 sites, 31 names: `KAW_showBonusBanner`, `transformAndAddPolyG4/GT4`, `addFrameCallback`, …) | 36 | 14 | **A prototype each** in the right `include/dcb/*.h` (byte-identical; a candidate upstream PR) | `duel_session.c:162`, `sug_trail.c:318` |
| E implicit declaration, Psy-Q: `MoveImage2` | 1 | 1 | psxstack #7 | `card_zones.c:354` |
| F incompatible-pointer-types (`s32 *` paths into `loadFileTagged(s32 *path, …)`, `drawTextColored`'s arguments swapped against its prototype, …) and int-conversion (`PLAYER_PROFILES = allocHeapBlock(...)`) | 48 | 12 | The typedef at the parameter or global fixes most; a few are real prototype mismatches to check against the PS1 code | `player_data.c:213`, `battle_hud.c:300`, `player_data.c:23` |
| G other errors: `OVERLAY_LOAD_ADDR` (`const s32 … = (s32)OVERLAY_AREA`: a non-constant initializer and a `const` against `game.h`'s `extern s32`), `evo_trays.c:144`'s initializer, `task.c:11`'s `<kernel.h>` (`struct TCB`: the BIOS thread blocks the scheduler glue reads), `sug_sphere.c:22-23` and `open_movie.c:377` conflicting prototypes | 10 | 6 | The typedef (then `(s32p)OVERLAY_AREA` is an address constant); `task.c`: a `PC_PORT` block (the fibers replace the scheduler, psxstack #25); the 3 prototypes: byte-identical fixes, candidates for upstream | `duel_util.c:149`, `task.c:69`, `sug_sphere.c:22` |
| X unclassified: `DUEL->sprites = (void *)KAW_allocCardPolys()` 11; `(s32)((T *)TABLE + id * 0x13C))->name` 21 | 32 | 12 | Cast edits and typedefs as above | `duel_session.c:37`, `text.c:69` |
| **Total** | **3,246** | | | |

**What the typedef buys.** Kinds A1, A2, B2, C2, F and most of G follow from about 40 declaration lines in
`include/` (4 globals for 1,683 sites; ~12 more globals; ~8 return types; ~20 parameters and fields): **about 1,900
sites silenced by declarations**, in a diff upstream can read. Kinds B1, B3, B4, B5 and X are **about 950 explicit
casts** that need the type written at the site whichever strategy is chosen (a mechanical `(s32)` to `(s32p)` where
the operand is a pointer, reviewable file by file). Kind A3 is **3 macro edits for 329 sites**, and `setaddr` is the
one place where psxstack's own macro (`PTR_TO_U32`, the tag window) is the right tool. What needs psxstack's
`PTR_TO_S32` proper is small: `HeapBlock.addr` (the flag in bit 31) and whatever `Task.regs`/`Task.stack` keep once
the fibers replace the context switch. Per-site `PTR_TO_S32`/`S32_TO_PTR` everywhere would be ~3,200 edits, ~1,700
of them inside two header macros that one typedef covers.

### Beyond the gate (`counts` and `link`)
- **Busy-waits:** 9 empty-body loops, all in `cd_file.c` (`CdInit`, `CdControlB`, `CdRead` retries): `PLATFORM_WAIT()`
  each. `open_movie.c`'s spins and `memcard.c`'s counter wait have non-empty bodies ("Busy-waits" above).
- **Fixed addresses:** 13 scratchpad sites (`SORT_WORK` in `tmd_sort.h` and 12 casts of `0x1F800000` in `main/model`,
  `main/gfx`, `evoseg/cutscene`, `main/system`) and 2 overlay-area casts in `kaw_effect.c` (the stale addresses). No
  late-bound `func_8xxxxxxx` names, no `INCLUDE_ASM`.
- **GTE:** 271 uses of 38 of the 40 `gte_*` macros, 230 in `tmd_sort.c` (`gte_lwc2` 69, `gte_swc2` 47, `gte_stopz_reg`
  22, `gte_mfc2` 12). `port/include/gte.h` covers all 40 (`scripts/gte_test.sh`).
- **Psy-Q calls** (by library; the library of a name comes from the `src/main/psyq/<library>_<object>` file that
  defines it, written to `build/port_inventory/psyq_symbols.txt` in the form the inventory parses): LIBGPU 50
  functions (`DrawSync` 50 sites, `SetSemiTrans` 50, `AddPrim` 43, `GetTPage` 27, `SetDrawTPage` 25, `LoadImage` 22,
  …), LIBSND 26, LIBGS 20, LIBCD 18, the pad and BIOS file calls 13, LIBCARD 5, LIBSPU 3, LIBAPI 3, LIBETC 2.
- **The executable's game `.bss` is generated C** (`port/tools/port_bss.py`, at configure time through
  `port_inputs.py --bss`, into `build/port/gen/bss_standins.c`, a MAIN unit, so the compile launcher puts it in
  `dcb_bss_main`, which the reset and the save states snapshot; the probe writes its own copy and compiles it). The C
  only declares these labels. The labels are those of `config/us/symbols.txt` in the executable's `.bss`, outside
  the file's Psy-Q section: the 122 of splat's `game` segment (`0x80077A08` to `0x801D83D8`, `0x1609D0` bytes;
  splat's 18 other `D_` labels there are in no tracked file and no C names them), and 21 more in its `psyq`
  segment, after libspu's first `0x14` bytes (`HUD_PANELS` at `0x801D83EC` to `HACK_SCRIPT_CURSOR`, up to libspu's
  `D_801D8560`: psylink's object order). Each one is an alias of a union of its declared type (`__typeof__` of
  the `extern`, so `s32p` changes need no new run), the types the C casts it to (`(Graphics *)&GRAPHICS`,
  `(FileEntry *)&DRIVE_DIRECTORY`) and its PS1 bytes (`HEAP_ARENA`, declared `s32`, is `0x148000`). 139 typed (6
  from a unit's declaration, 2 of them `void *` because the type is the unit's own), 2 byte arrays (`SOFT_FLOAT_*`,
  `libmath.s`'s), 2 kept apart (below). Checks: each storage is at least its PS1 bytes and its host type
  (`_Static_assert`); with `-DPORT_BSS_CHECK_PS1` at `-m32`, each declared type fits its PS1 bytes, all but
  `DUEL_MSG_BAR`. `link` (after M1 step 2): its "asm-only data" undefined globals go from 144 to 20, none of them
  the executable's game `.bss` (Psy-Q's `Gs*` and `StCdIntrFlag`, overlay data, `OVERLAY_AREA`).
- **PS1 aliases the host does not keep** (#11): some bytes have two names in the C, and on the host each name is
  its own object. `SCREEN_COPY_MODE` is `SCREEN_COPY_EFFECT.mode` (`screen_copy.c` uses both) and `TASK_GP` sits in
  `TASKS` (both inside a `size:`); `GRAPHICS` is a whole `Graphics` whose fields are labels too (`FRAME_CALLBACKS`,
  `SCENE_3D_ENABLED`, `VBLANKS_PER_FRAME`, `CAMERA_SNAP`, `CAMERA_TARGET_MODEL`, `CAMERA_TARGET_PITCH`,
  `CLEAR_BG_ON_DRAW` = `DB(0).draw.isbg`); `DUEL_MSG_BAR` is a `0x1A`-byte `MsgBar` over 8 PS1 bytes that holds
  `MSG_BAR_PLAYER_LABEL`, `MSG_BAR_NEXT`, `MSG_BAR_NEXT2` and reaches into libspu's `D_801D83D8`. M1 maps such a name
  onto its field in a `PC_PORT` block of its header (`#define SCREEN_COPY_MODE (SCREEN_COPY_EFFECT.mode)`), which
  the generator then leaves to the C.
- **Duplicate globals** (`link`): `D_801F5254`, `D_801F535C`, `D_801F5454` in `evo_bss.c` and `sai_bss.c`,
  `D_801F540C` in `evo_bss.c` and `open_bss.c`: overlay data sharing an address and a splat name. psxstack links
  every overlay statically, so they need distinct names (an upstream rename with the overlay prefix, as
  CONTRIBUTING.md asks, or `PC_PORT` renames). Done at M1 with `PC_PORT` renames, with `D_801DDF38` ("Overlays",
  "On the host"): `link` finds none.

### What got in the way in psxstack v0.2.1
Fixed in v0.3.0 (psxstack #28, #7): the GTE override is written at the game's header's path, the inventory takes a
Psy-Q library map (`psyq_libraries`), `psyq/check.sh` takes its paths, and the runtime and the shim no longer see the
game's include directories (this tree's `include/stdarg.h` shadowed the host's). The wrapper here lost its own
override and derived symbol file with the pin bump.

## What the game needs

### The task scheduler (the largest new piece)
- **The tasks:** `TASKS[32]` (`include/dcb/main.h:20-38`, 0xC0 bytes each, a kernel-TCB-shaped `regs[40]`). TASKS[0] is
  the main task.
- **The C side:** `src/main/system/task.c` has `createTask` :210, `selectNextTask` :182, `handleVsyncPreemption` :141,
  `killTask` :318 and `wakeTask` :415.
- **The asm side (`src/main/startup.s`):** `spawnTask`, `endTask` and `resumeTask` are stubs that disable interrupts,
  call the C routine and return with `rfe`. `exitTask`, `yieldTask`, `waitFrames` and `waitFramesResume` share one
  body: it saves s0-s7/gp/sp/fp/ra into `CURRENT_TASK`, picks the next task and restores it.
- **Switching is cooperative and preemptive:**
  - Cooperative: `yieldTask` and `waitFrames` (607 calls; 120 are `waitFrames(0x7FFFFFFF)`, sleep until
    `resumeTask`).
  - Preemptive: a vblank root-counter event (`OpenEvent(RCnt3)` + `SetRCnt`/`StartRCnt`, task.c:130-133) runs
    `handleVsyncPreemption` in interrupt context. It ticks the vblank counters and, when the current task's priority
    isn't 0, switches to TASKS[0].
- **Stacks:** 0x100 to 0x2000 bytes, from the game heap (tag -3). They are far too small for x86-64 frames, so on the
  host each task needs a host stack of its own.
- **Pointers in ints:** `spawnTask` is declared without a prototype (`s32 spawnTask();`, `include/game.h`; 244
  calls in 50 files, with 5 to 9 arguments). It receives function pointers and string literals, and `createTask` takes
  the entry and its four arguments as `s32`. `Task.stack` and `Task.regs` are `s32`. On the host the entry and the
  arguments are pointer-sized.

**On the host** (M1 step 3a; `port/game/tasks.c`, `scripts/tasks_test.sh`): each task is a psxstack fiber
(`hooks.h` "Fibers"), and the scheduler's C is unchanged but for its `PC_PORT` blocks.
- **The glue:** `port/game/tasks.c` is `startup.s` on the host (an adapter unit; psxstack compiles no `.s`). Each stub
  has the asm's effect on the task records, and a switch is `port_fiber_switch` to the fiber of the task
  `selectNextTask` picked:
  - `spawnTask`, `endTask` and `resumeTask` call `createTask`, `killTask` and `wakeTask`. Nothing interrupts a task on
    the host except a vsync tick, and a tick runs only where the game waits, so the interrupt masking has no
    counterpart; neither has `disableInterrupts`/`restoreInterrupts` (defined empty, never called).
  - `yieldTask` and `waitFrames` mark the task yielded (the flags' high half `0x8000`) and switch. `waitFrames(n)`
    counts the task's turns, as the asm's loop through `waitFramesResume` does: it returns at the n-th turn after the
    call, or one turn after a `resumeTask`, with `resumeTask`'s result, which is a return value and not `v0`.
  - `exitTask` runs `exitCurrentTask` and then `port_fiber_exit` to the task it selected. A fiber starts in
    `game_task_fiber_entry`: the entry (an overlay tag resolves, as the PS1 would jump there at the first switch)
    with its four arguments, then `exitTask`, which is `regs[R_RA]` on the PS1.
  - `launchTaskScheduler` calls `startTaskScheduler`. There is no `gp` to record.
- **The main fiber is the list's end, not TASKS[0].** On the PS1 `main()`'s `for (;;) rand();` is the context that
  `TASK_LIST_END` (at `TASKS - 1`) saves: the idle loop, which runs when every task has yielded. The first vsync
  preempts it to TASKS[0] (`runMainTask`). On the host `TASK_LIST_END.fiber` is `port_fiber_main()`, TASKS[0] gets a
  fiber of its own, and the loop has a `PLATFORM_WAIT()`: one vsync tick per idle turn, so `rand()` is called once per
  idle vsync instead of as often as the PS1 has time for.
- **`task.c` under `PC_PORT`:** no `<kernel.h>`. The `KERNEL_TCB` copies go, and `asm.h`'s register indices stay for
  the `regs[]` writes, which are kept for the record. `CURRENT_TASK = &TASK_LIST_END` and `TASK_LIST_END.prev = TASKS`
  stand for `TASKS - 1` and `CURRENT_TASK + 1`. `createTask` and `startTaskScheduler` store the entry and the
  arguments in the host fields and create the fiber. `killTask` destroys the killed task's fiber.
  `handleVsyncPreemption` keeps its counters and choices; where the PS1 loads TASKS[0]'s registers into the TCB, it
  calls `port_fiber_preempt`, and the pump switches at the end of the tick, on whichever fiber ticked.
- **`Task`:** `wakeResult` and `stack` are `s32p`. Under `PC_PORT` the struct ends with `fiber`, `entry` and
  `args[4]` (pointer-width), after `regs[]`. `createTask`, `startTaskScheduler` and `wakeTask` take `s32p` where a
  pointer may pass. The PS1's bytes are unchanged in us, jp and eu.
- **The unprototyped stubs:** on the PS1 `spawnTask` and `resumeTask` stay `s32 f();`. Under `PC_PORT`, `game.h`
  declares them with every parameter `s32p`, and a variadic macro of the same name (`port.h`'s
  `PORT_S32P_ARGS9`/`ARGS2`) casts each argument to `s32p` and fills the missing ones with 0. A variadic or
  unprototyped definition would not do. An `int` passed where the definition reads 64 bits has undefined high bits:
  in a stack slot that is garbage, and an entry that takes a pointer would get it. The missing arguments (a 7-argument
  `spawnTask`'s `a2`/`a3`, a one-argument `resumeTask`'s result) would be whatever the register or slot held, as on
  the PS1, and not repeatable.
- **Tested** by `scripts/tasks_test.sh` (CI's `probe` job): `task.c`, `tasks.c` and psxstack's `runtime/fiber.c` with
  stubs for the heap, the kernel's events and the overlay resolver, under ASan and UBSan. Seven tasks spawn (5, 6 and
  9 arguments), yield, wait, wake (a pointer result, a missing one), kill, exit (explicitly and by returning) and
  reuse slots and fibers. A task busy-waits across a tick and is preempted to the main task, then resumed by
  `selectNextTask`'s PREEMPTED/DEFERRED rules, and the main task ticks itself in the non-vsync mode. The trace must
  equal the order derived by hand from `task.c` and `startup.s`. Not covered: the real RCnt3 event (the shim's), the
  game's own tasks, save states with tasks.

### Overlays
- **Loading:**
  - A task runs `loadFileToAddress("P:\\kawseg.bin", OVERLAY_LOAD_ADDR, …)` (`src/main/system/loader.c:87`), then
    the caller sleeps until the loader calls `resumeTask`.
  - `"X:\\name"` means the archive `\X.DRV;1` (found by `CdSearchFile`), then the name in its directory: `FileEntry`,
    `include/dcb/cd_file.h:6-12`, up to 0x200 entries.
  - The drives are A B C E F G M P; P holds the overlays.
  - There are 38 load sites, all in the EXE. No overlay loads another.
- **Calls into an overlay:** 211 references in 13 EXE files, 29 of them `spawnTask` entries. They use absolute
  addresses in the overlay area; there is no jump table. The EXE assumes the right overlay is resident.
- **Overlays calling the EXE:** they reference EXE symbols as absolute addresses (`Makefile:221-226`).
- **Stale addresses:**
  - KAWSEG calls `SUG_createFadeRect` (0x801E6424) and has a table entry at 0x801E651C (`kaw_effect.c:98, 511`).
    Both point into KAWSEG's own `KAW_chooseSupportCard`, so they look stale (leftovers from SUGSEG's layout).
  - The `undefined_syms_{sai,sub,open}seg.txt` files define 81 symbols in the middle of overlay structs.
- **On the host** (M1):
  - *The load.* `loadFileToAddress` keeps its disc read into the area (`OVERLAY_LOAD_ADDR` is the base of the
    port's slot, `port_slot1`), so `LOADED_FILE_SIZE`, the bytes in the slot and `resumeTask` are the PS1's; then,
    in a `PC_PORT` block, `OVERLAY_COPY(1, game_overlay_id(path), dst, dst, size)` makes the overlay the slot's
    current one and restores its `.data`/`.bss` as the file holds them. `game_overlay_id` (`port/game/game.c`)
    maps `"P:\\kawseg.bin"` to the file ID through `overlay_ids.h`, which `port/tools/port_inputs.py` writes from
    the same `OVERLAYS` list as `overlays.txt`; another drive is 0 (no overlay), an overlay this version lacks is
    fatal. psxstack's copy time (`size * 12 / 677376` vsyncs after the load, dw2003's `memcpy`) also runs here,
    where the PS1 has no copy: 1 or 2 frames per load ([psxstack#39](https://github.com/gascarcella/psxstack/issues/39)).
  - *Calls by name.* Every overlay function the EXE calls (`include/dcb/overlay_calls.h`, the 84 names of
    `symbols_overlay_calls.txt`) is its own overlay's global, under the overlay's prefix, and all 84 are defined in
    the overlays' C. The host links every overlay in, so the linker binds each call, each `spawnTask` entry and
    each table entry to the right host function: no tag, no `SLOT_FUNC`/`LATE_FUNC`, no edit at the 211 sites
    (unlike dw2003, whose overlays share unnamed addresses). The PS1's precondition, the right overlay loaded,
    is the game's. A function pointer the EXE stores is a host pointer, and psxstack's `OVERLAY_FN` passes those
    unchanged: the scheduler's trampoline may still call entries through `OVERLAY_FN(1, entry)`, which then also
    covers a tag if one ever appears.
  - *Data by name.* `KAW_RESULT_SCREEN_STATE` is KAWSEG's global; `OPEN_MEMCARD_CANCELLED` (inside
    `OPEN_MEMCARD`) is, in the EXE, `*game_open_memcard_cancelled()`, the adapter's pointer to the field
    (`overlay_calls.h`). OPENSEG's own use of the name (`open_memcard.c`) is among the `undefined_syms` aliases below.
  - *Names inside overlay objects* (#20, M1 step 4). `undefined_syms_{open,sai,sub}seg.txt` and `symbols_subseg.txt`
    name addresses inside `OPEN_MEMCARD`, `SAI_AREA`, `SAI_WORLD_MAP`, `SUB_WINDOWS`, the menus and others. The 71
    the compiled C references are `#define`d onto their fields under `PC_PORT`, in the header or unit that declares
    them (`#define SAI_MAP_STATE (SAI_WORLD_MAP.state)`; a cast keeps the declared type where the field's differs,
    `(*(u8 *)&SAI_WORLD_MAP.active)`), each offset checked against its container at -m32. The 10 no unit references
    keep their externs (a use would fail the link).
  - *The stale addresses are never reached* (US disc): only effect kind 0, the fade rect, uses them, and the 31
    scripts of `CBTL_EFF.ARC`, KAWSEG's only source of effect scripts, create 240 effects, all of constant kinds 1
    (201), 2 (6) and 3 (33). On the PS1 they would enter `KAW_chooseSupportCard` mid-body (0x801E6424 is a load
    delay slot, 0x801E651C a branch) and return through a frame it never built. Kind 0 is SUGSEG's (its skill
    scripts use it). The host stops there (`PLATFORM_HALT` in two `PC_PORT` stand-ins in `kaw_effect.c`) instead of
    calling SUGSEG's linked but unloaded functions.
  - *Duplicate names.* Each overlay's first word, `D_801DDF38` in all seven, and `D_801F5254`, `D_801F535C`,
    `D_801F540C`, `D_801F5454` (EVOSEG with SAISEG or OPENSEG) take their overlay's prefix under `PC_PORT`
    (`#define D_801DDF38 KAW_D_801DDF38`); `link` shows no duplicate. An upstream rename to the prefixed names
    (CONTRIBUTING.md "Overlay symbols") would make the defines unnecessary: the owner's call.

### Memory and pointers
- **The game heap:** `src/main/system/heap.c` manages `HEAP_ARENA` (0x8008C848, `HEAP_SIZE` 0x148000, inside the
  EXE's `.bss`; on the host a game `.bss` array, not psxstack's arena: `game.json` has no heap).
  - `HeapBlock.addr` keeps the in-use flag in bit 31 and strips the KSEG bits of free blocks. It is `s32p` (M1
    step 3), and the `HEAP_ADDR_*` macros of `heap.c` are the original expressions on the PS1.
  - **The host's layout** (issue #25, decided at the end of M1): the block table holds **the PS1's values** (PS1
    addresses, the flag in bit 31, sign-extended in the wider word), so the allocator makes the PS1's first-fit
    decisions on the same sizes and every block has the address the PS1 gives it. Only the bytes move: the block at
    PS1 offset o of the arena is at `HEAP_HOST_SCALE * o` of the host's arena (`sizeof(void *) / 4`: 2 at `-m64`,
    1 at `-m32`, the PS1's layout there), `heap.c`'s own `HEAP_ARENA` that much larger. Every block then starts on
    a multiple of 8 on a 64-bit host, the alignment of its pointers (at the PS1's 4, UBSan reported every access to
    the profile, `SessionData`, `SaisegSession`, `PlayerDeck`, `CardSlot`), and the spacing after a block's size is never handed
    out (a host struct allocated with a PS1 byte count, issue #31, runs into it instead of the next block).
    `HEAP_ADDR_PTR`/`HEAP_PTR_ADDR` convert between a table value and the host pointer; a pointer off the grid or
    outside the arena is no block's (0), as it never matches on the PS1.
  - **The PS1's leftovers:** a block handed out holds what the blocks before it left on the same PS1 bytes, and the
    game reads some it never writes (the profile's name after its terminator, the settings word's unused bits,
    `scriptOffset`, `deckChoice`: the checkpoint image compares them). The host keeps a freed block's bytes (and a
    shrunk block's tail) at their PS1 offsets in `HEAP_PS1_BYTES` and copies them into each new block, so its bytes
    are the PS1's too. Alternatives rejected: aligning the blocks in the table itself (the PS1 addresses drift after
    the first odd block: `PLAYER_PROFILES` would no longer be 0x800C8964, the scripts' `wait_mem` and the image's
    card pointers would need a table of their own, and first-fit could choose other blocks), and keeping the 4-byte
    alignment with `-fno-sanitize=alignment` (undefined behaviour left in, against psxstack's rule that suppressions
    are only for in-struct overruns).
  - A heap pointer kept in an `s32` (a script register, below) goes through `GAME_PTR_TO_S32`/`GAME_S32_TO_PTR`
    (`include/port.h`, defined in `heap.c`): on the host it becomes the PS1 address of the same byte (the block's
    PS1 address from the table plus the offset in the block), so the word holds what it holds on the PS1; a pointer
    elsewhere goes to psxstack's `PTR_TO_S32` (the slot, else fatal). The adapter writes the profile's card pointers
    the same way (`game_heap_owns`).
  - The PS1 addresses hold as long as the game makes the PS1's allocations with the PS1's sizes before a block:
    the replay scripts wait for `PLAYER_PROFILES` at 0x800C8964, and the checkpoint image writes heap pointers as
    PS1 addresses. So a permanent block that is larger on the host must not grow there: the memory card directories
    (`CardDir`, 0x260 bytes on the PS1, larger on the host because the shim's `DIRENTRY` holds a pointer) are host
    storage in `memcard.c`, and the PS1's two blocks are still allocated (M1 step 4).
- **Psy-Q's libc heap:** `InitHeap` puts it over the overlay area, but game code never calls malloc or free.
- **Pointers held in integers:**
  - `addFrameCallback(s32)`: 56 sites, 55 with `(s32)fn`.
  - Globals declared `s32` and cast at use: `PLAYER_PROFILES`, `SPRITE_POOL_CURSOR`, …; 521 `((T *)GLOBAL)` casts.
  - Raw casts: `(s32|u32)&x` 231 in 20 files, `(s32|u32)ident` 433 in 66 files, `*(s32 *)(p + off)` 107 in 12 files.
  - `OVERLAY_LOAD_ADDR` is declared `s32` (`duel_util.c:149`).
  - psxstack's `PTR_TO_S32`/`S32_TO_PTR` cover the arena; the probe's `-Werror` on pointer/int casts will list each
    site.
  - **Script registers** (M1 step 3): one interpreter (`script.c`'s `runScriptToNextEvent`) runs every VM: KAWSEG's
    tutorial and effects, EVOSEG's fusion and effects, SAISEG's area and labels, SUGSEG's effects. The registers stay
    `s32`: the effect VMs read the register file as structs of words (`EffectParams`, `EvoFxParams`, KAWSEG's
    `EFFECT_PARAMS` at byte 0x248), and SAISEG's registers 12.. become the saved `areaScriptFlags`. Only op 8 puts an
    address in a register (of an inline block of the script, which is loaded into the heap): it goes through
    `GAME_PTR_TO_S32`, and the readers (the text lines, the tutorial's names) through `GAME_S32_TO_PTR`. SUGSEG's
    `vramEntries` is not an address: it is `loadModel`'s VRAM slot, which the PS1 steps by an `Entry16` per model slot
    as a pointer; the host does the same arithmetic on the integer (issue #30). SAISEG's `ScriptRunner.unk0` (the script file) is `s32p`.
  - **Effect objects** (M1 step 3): `EffectObject.parent` is `s32p`, as the `EvoFx` and `EffectInit` views of the
    same object already had a pointer there; the fields after it are 4 bytes further on the host, so
    `EffectTemplate` and the effects that start with an EffectObject's bytes (`RingEffect`, SUGSEG's) take the host's
    size under `PC_PORT` (`scroll_bg.h` asserts it). The templates are built on the stack and copied: nothing reads
    them from the disc.
  - **The OMD words** (M1 step 3): `relocateOmdObjects` turns each object's offset into an address in place; the word
    is the file's 32 bits, so the host keeps the offset and `OMD_OBJ_DATA` (`anim_control.h`) adds the `Tmd18`'s
    address where `scene3d.c` reads it. TMD-format models go through the shim's `GsMapModelingData` (not in
    psxstack yet).
  - **Labels inside structs** (issue #11, M1 step 3): `FRAME_CALLBACKS`, `SCENE_3D_ENABLED`, `VBLANKS_PER_FRAME`,
    `CAMERA_SNAP`, `CAMERA_TARGET_MODEL`, `CAMERA_TARGET_PITCH`, `CLEAR_BG_ON_DRAW` (fields of `GRAPHICS`),
    `MSG_BAR_PLAYER_LABEL`/`NEXT`/`NEXT2` (`DUEL_MSG_BAR`) and `SCREEN_COPY_MODE` (`SCREEN_COPY_EFFECT`) are
    `#define`d onto their fields under `PC_PORT` (`game.h`, `vblank.h`), and `GRAPHICS` is declared a `Graphics`;
    `TASK_GP` goes with the scheduler glue. `port_bss.py` gives a `#define`d label's PS1 bytes to the label before
    it, so `GRAPHICS` spans its fields (0x8218 bytes) and fits at `-m32` with `-DPORT_BSS_CHECK_PS1`;
    `DUEL_MSG_BAR` still does not (its 8 bytes before libspu's data, issue #11).
  - **Views by PS1 offsets** (issue #23): callers name another struct's fields by their PS1 offsets in types of their
    own; on the host those offsets move. The dialogs: `ChoiceDialog` (OPENSEG), `Window` (`OPEN_DIALOG`, and
    `DUEL_DIALOG`, an `s32` the C reads as a `Window`), `DialogK` (KAWSEG), `EvoDialog`, and the `u8[0xB8]`/`u8[0xC0]`
    buffers (13 stack frames, `SAI_DIALOG`, `DeckScreen.dialog`) are the `Dialog` itself under `PC_PORT`: a typedef,
    the views' names as members of unions in `Dialog` (`options[2]` and `yes`/`no` over `yesLabel`/`noLabel` at 0x98,
    `draw` over `onFrame` at 0xA0, `result` over `choice` at 0xA5), the buffers through `DIALOG_BUFFER(name, size)`
    with `DIALOG_BUFFER_CHOICE`/`DIALOG_BUFFER_PAD` for their bytes 0xA5 and 0xA6 (`dialog.h`; on the PS1 each is the
    original text). `Model2220`, `EvoModel` (EVOSEG) and `ModelData` (SUGSEG) are the `Model`: their names are
    members of unions in `Model` (`partCount`, `x` = `pos.vz`, `rotX`/`rotY`, `parts` over `obj` with its `tmd`, `pose`
    over `bonepos`, `animClip`/`animKeyTimer`, `matrices`/`boneMatrices` over `lw`, `owner` over `link`, `clutRect`
    over `crect`; `clut` is 256 `u16` on the host).
  - **Views with the host's offsets** (issue #30): where a view names one region of another object, it keeps its type
    and its pads become the other type's host offsets (`__builtin_offsetof`), each pinned by a `_Static_assert` (the
    `EFFECT_OBJECT_HOST_EXTRA` pattern). The duel state (`DUEL_STATE`): the host `Duel` holds the pointers of KAWSEG's
    views at their PS1 places and the PS1 block's 0x86C bytes; `DuelK`, `DuelAi`, `DuelBanner`, `DuelRing` pad to it.
    `ProfileK` pads to the host `PlayerProfile`, `PlayerStats` to the host `Player`, `HudPanel` holds a pointer as
    `Panel` and `HudPanelK` do. Raw offsets become names on the host (macros whose PS1 side is the original text):
    `CARD_ANIM_SIZE`/`SPRITE_KIND` (`CardAnim`), a player's HUD panel state (`HUD_PANELS + p * 0xD8 + 0xD`), a
    `Player`'s bit-field word (0x178, `PLAYER_FLAGS_OFS`), `Graphics`' camera words (`camera.c`), `Scene3D`'s view
    matrix (0x78) and byte 0x130, `SessionData`'s `opponentDeck` bytes (0x70-0x74), `scrollWindowTo`'s `UiWindow`
    halfwords, a `Model`'s keys (0xD80), a chain of `Xform` pointers, and a saved deck copied over a `Player` (its
    0x110 bytes, up to `bonusFlags`).
  - **Transforms** (issue #30): an effect (`EffectObject`) is a `Transform` (parent at 0x48) whose target
    (`targetMatrix`, 0x4C) is another (parent at 0x94); on the host both parents are pointer-wide words in
    `EffectObject`, so its own `parent` is at 0xA0 and everything after it 0xC further (`EFFECT_OBJECT_HOST_EXTRA`,
    `EFFECT_OBJECT_PARENT_EXTRA`); the views (`EffectInit`, `RootEffect`, `EvoFx`, `ModelLink`, `RingEffect`) follow.
    A streak `Particle` holds its parent at 0x48 too, and the `u8[0x4C]` transforms (`EffectSlots.xform`,
    `TrailEffect.edges`/`xform`) are `TRANSFORM_HOST_SIZE`. SUGSEG's `TexAnim` holds four pointers, so `RingEffect`'s
    `texAnim`, `ModelLink` and `EvoModelFx` make room for it (`TEX_ANIM_HOST_EXTRA`).
  - **Stack locals as arrays**: `SUG_tickHudSlides` walks a function's separate `HudSlide` locals as an array (the
    PS1 frame has them adjacent); on the host they are one array.
  - **Heap sizes** (issue #31): of the 42 literal `alloc*HeapBlock` sizes in the `us` units, 8 are a host-grown type's
    PS1 size: `SESSION_DATA`, `CARD_ANIMS`, `DUEL_STATE`, `DUEL_PLAYERS` (`PLAYER_BLOCK_SIZE`: the name runs past the
    struct), `rollRewardCards`' pointers, `SUG_SPRITE_CACHE`, `EVO_SHARDS`, `SUB_EDITED_DECK`. They keep the PS1's size
    in the block table, so later blocks keep their PS1 addresses, and go through `HOST_FITS(ps1, host)` (`port.h`),
    which checks at compile time that the host's type fits the block's host bytes (`HEAP_HOST_SCALE` times its size,
    above). 3 were handled before (the card directories, `KAW_MATCH_SCREEN`'s `sizeof`); 31 are true byte counts,
    marked `/* PC_PORT: bytes */`. psxstack's `counts --sites size` knows only the first game's allocator names, so it
    lists none here (psxstack#55).
  - **Reads through null pointers** the PS1 survives (address 0 is the kernel's RAM, zeros under OpenBIOS):
    `initDialog` measures a null text, `unloadModelAnimations` reads the id of a slot `unloadModel` cleared. On the
    host each reads OpenBIOS's zeros (an empty string, id 0) under `PC_PORT`.
- **Ordering tables:** `P_TAG.addr:24` with `addPrim`/`setaddr` (293 uses in 44 files), and `packet & 0xFFFFFF` in
  `tmd_sort.c:621, 640`. psxstack's tag window already handles 24-bit tags over the units' data and the arena
  (psxstack `docs/PORT.md` "Ordering tables on 64-bit"). `tmd_sort.c`'s tags go through `PTR_TO_U32`, and
  `SortWork.packet` is `u32p`: the host keeps the pointer where the PS1 strips the KSEG bits.
- **Scratchpad:**
  - `0x1F800000` is used in 8 files. Most uses are the `SORT_WORK` macro (`include/dcb/tmd_sort.h:6`, about 119 uses
    in `tmd_sort.c`); others are in `camera.c`, `scene3d.c`, `render_loop.c` (clears 1 KB) and the cutscene and
    sprite code.
  - psxstack has no scratchpad region: its only scratchpad hook is `PORT_SCRATCHPAD_STACK_*`, for dw2003's stack
    switch, which does nothing on the host. Here the game stores data there. Decided game-side (2026-10-08): a
    `SCRATCHPAD(type, ofs)` macro in `port.h` over a buffer in the adapter; a stack region only if a third game
    needs one.
  - Done (M1 step 3): every `0x1F800000` site is `SCRATCHPAD(type, ofs)` (`0x1F800000 + ofs` on the PS1), over
    `port_scratchpad` in `port/game/game.c`: 0x800 bytes, not 1 KB, because `SortWork` is wider on the host
    (pointers), so the vertex buffer that follows it (`SORT_WORK_BUFFER`, 0x7C on the PS1) starts at 0x90 and still
    has the PS1's 0x384 bytes. `scene3d.c` and `evo_cutscene.c` write `SortWork`'s `tpage` and `clut` by name on the
    host (the PS1 writes scratchpad words 12 and 13). `camera.c`'s and `sug_sprite.c`'s temporaries keep their
    offsets.
  - Other fixed addresses: `task.c:68` reads the kernel's PCB pointer at `0x108`. There are no I/O register
    accesses in game code.

### Busy-waits (`PLATFORM_WAIT` sites)
These loops spin without yielding:
- `open_movie.c:403` (`isdone`, set by the `DecDCTout` callback).
- `open_movie.c:366, 383` (`StGetNext` retries, the ring filled from the CD interrupt).
- `memcard.c`'s polls for the card's events (`waitForMemoryCardEvent`, `waitForMemoryCardHwEvent`) with
  `pollInterval` 0: they spin until an event or until `MEMORY_CARD_WAIT_COUNTER`, which another task increments
  once a frame, reaches 0x259.
- Nine `while (CdInit()/CdControlB()/CdRead() == 0)` loops in `cd_file.c`.

About 140 more polling loops call `waitFrames`/`yieldTask` and are fine once the scheduler works. `VSync` is called
in 10 places.

**On the host** (M1): the nine `cd_file.c` loops have `PLATFORM_WAIT()` as their body (empty on the PS1). The
memory card polls run `PLATFORM_WAIT()` in a `PC_PORT` block when they do not wait frames (the us/eu and the jp
forms; jp's hardware poll never waits). Nothing on the PS1 yields there: the event comes from the card's
interrupt, and the counter moves only when the vblank interrupt preempts the polling task (`handleVsyncPreemption`
switches to the main task when the interrupted one's priority is not 0) so that the screen's task runs. The host's
tick stands for those interrupts: `port_wait` runs the pending ones (the card's, once the shim's LIBCARD exists) and
the game's vblank handler, whose `port_fiber_preempt` (the scheduler's fibers) switches at the tick's end, so the wait progresses as on the PS1 and
the timeout counts frames as there. The movie's spins get **no** hook: they are Sony's sample player, the same as
dw2003's title movie, and the shim already models them (`StGetNext` runs a vsync tick once per 5000 empty polls,
`DecDCTout` runs its callback before it returns, so `isdone` is set when the loop starts). A `PLATFORM_WAIT` there
would make their timeouts (2000 x 2000 polls, 0x800000 iterations) count frames instead of polls.

### Interrupt-context code
- The vblank preemption handler (above).
- `OPEN_uploadMovieSlice` via `DecDCToutCallback` (`open_movie.c:223, 314, 337`; it calls `StCdInterrupt`).
- libsnd's tick (`SsSetTickMode(1)`/`SsStart`, `sound.c:49-71`).
- No `VSyncCallback`/`DrawSyncCallback` in game code.

### Hand-written assembly
- `src/main/startup.s` (the scheduler glue): replaced by the fibers.
- `src/main/libmath.s` (soft float: `__adddf3`, `__muldf3`, `__divdf3`, `__cmpdf2`, `__floatsidf`, `__fixdfsi`,
  `__mulsf3`, …): on the host the compiler's own floating point. Only 6 files use float or double (`camera.c`,
  `kaw_hand.c` via `sqrtDouble`, `sug_hud.c`, `sug_screen_copy.c`, `sug_bss.c`, `libmath.c`), so it is worth a
  layer-1 check that results match bit for bit.
- `card_motion.c:594`'s `.rodata` padding `__asm__`, and the `labels.inc` include that `include/include_asm.h:35-38`
  adds to every unit (skipped with `SKIP_ASM`, as on the host).

### The Psy-Q shim: what this game calls that psxstack v0.2.1 doesn't implement
A first grep for definitions in psxstack's `psyq/` and `runtime/`. Confirm with psxstack's
`psyq/check.sh --game-root`, which may find some of these as macros or under other declaration styles:
- **libgpu:** StoreImage, LoadImage2/StoreImage2/MoveImage2, PutDrawEnv, GetDispEnv, SetDrawArea, AddPrim,
  SetShadeTex, the function forms of `SetPoly*`/`SetSprt*`/`SetTile*`/`SetLine*`, MargePrim, SetTexWindow,
  SetDrawStp, SetDrawMode, OpenTIM/ReadTIM.
- **libgte:**
  - Scalar maths: csqrt, catan, ratan2.
  - Vectors and matrices: VectorNormal, MatrixNormal, MulMatrix0, CompMatrix, PushMatrix/PopMatrix, TransMatrix,
    RotMatrix, RotMatrixYXZ, SetRotMatrix/SetLightMatrix/SetTransMatrix.
  - Transforms: RotTrans, RotTransPers/3/4, RotAverage4, RotNclip3/4, RotAverageNclip3/4.
  - Lighting: NormalColorCol/3.
- **libgs:**
  - Display and coordinates: GsSwapDispBuff, GsInitCoordinate2, GsSetLsMatrix, GsSetLightMatrix, GsMulCoord3,
    GsGetLs, GsGetLws.
  - Models and the work area: GsMapModelingData, GsSetAmbient, GsSetWorkBase/GsGetWorkBase, GsLinkObject4,
    GsSortObject4.
  - The `GsTMDfast*`/`GsTMDdivT*` functions the game writes into `GsFCALL4` (`evo_cutscene.c:1085+`).
- **libcd:** CdSearchFile, CdRead, CdReadSync, CdSync, CdMix, StClearRing, StGetBackloc, StRingStatus.
- **libsnd and libspu:**
  - libsnd: SsStart, SsSetMono/SsSetStereo, SEQ playback (SsSeqOpen/Close/Play/Stop/SetVol/GetVol),
    SsUtKeyOnV/SsUtKeyOffV, SsUtReverbOff.
  - libspu: SpuClearReverbWorkArea, SpuSetVoiceAttr, SpuSetCommonAttr.
- **libapi:**
  - Events and root counters: OpenEvent/EnableEvent/TestEvent, SetRCnt/StartRCnt.
  - Critical sections: Enter/ExitCriticalSection.
  - Pads: ChangeClearPad.
  - The BIOS file calls the memory card uses: open, read, write, lseek, close, firstfile, nextfile
    (`memcard.c:365+`).
- **libcard:** InitCARD, StartCARD, `_bu_init`, `_card_info/_load/_clear/_format`. dw2003 used libmcrd, which this
  game doesn't call.
- **Present already:** libpress (MDEC), libpad, libetc.

The game also uses libc's `sprintf` (345 calls), `rand` (87), `str*`, `memset`, `bzero` and `bcopy`. Their
Psy-Q-specific behaviour (`rand`'s sequence, `sprintf`'s formats) needs a check against the emulator.

## Testing
The oracle is the game in PCSX-Redux (dw2003recomp's DECISIONS "The port is checked against the emulator"); the
runners are psxstack's (`tools/replay/`, GAME_CONTRACT.md "6. Tests"), configured here. The stack is the submodule
(`$PSXSTACK_DIR` names another checkout of it, for stack work).

| Check | What it proves | Where |
|---|---|---|
| Byte-identical PS1 build | No hook or `#ifdef PC_PORT` changes a PS1 byte | `scripts/build.sh` (CI `build (us)`) |
| Host-compile probe | Every unit compiles at `-m64` with pointer/int casts and implicit declarations as errors; no duplicate global | `scripts/probe.sh` (CI `probe`; the count is the baseline until M1) |
| The emulator boots the disc | PCSX-Redux (`scripts/setup.sh redux`, the pin in the data checkout) loads OPENSEG for the opening movie | `scripts/check_emulator.sh` (`tests/replay/replay.py boot`) |
| Emulator replays | Four pad scripts replayed from boot, each twice byte-identical: `boot` (the title, frame 7866), `title` (the menu), `new_game` (the registration: name, starter, the save to a fresh card, SAISEG at 11681), `first_duel` (Beginner City's script into KAWSEG at 14211), with the player profile hashed at every checkpoint | `tests/replay/replay.py check` (CI `replay`), `tests/replay/scripts/`, `expected/`, `probes.lua` |
| The port builds and boots | `build/port/dcb` links (every writable section renamed: `port_gen.py sections`); booted headless from the disc for 600 frames it exits 0 at the frame cap with OPENSEG loaded (stage 8) | `scripts/port_build.sh` (CI `probe`), `scripts/port_build.sh --boot` (CI `replay`) |
| The port against the emulator | The port replays the same scripts and reaches the emulator's checkpoints (the cross-core view: names, stages, maps, stable profile hashes, the overlay sequence) twice byte-identical | `tests/port/run.py`: all four scripts pass and are the CI gate (`replay` job): every checkpoint at the emulator's stage and map, the overlay and map sequences, two runs identical (the four take about 100 s). Checkpoints before the profile is defined carry no image (`"image": false`: `openseg_loaded`, `title`, `title_menu`, `name_entered`, `starter_chosen`; the record has no hashes). `saiseg` and `first_duel` keep theirs, and the stable hash leaves out (`VOLATILE_RANGES`): `profileId`, `playTime` and `cardCopySerials` (`rand()`); the heap the game never writes before those checkpoints (the emulator has the allocator's leftovers, the port zeros: partner 0's last 3 padding bytes, partners 1-2, decks 1-2, the starter deck's name after its terminator, `unk104` and `unk10E`, `unk15DF`, `unk2435`, `unk2771` bytes 1-2, `rewardCards`, `rewardResults`); and the `cardCollection` bytes of the Veemon deck's ten bonus cards (0x0B, 0x74, 0x19, 0x83, 0x1C, 0x89, 0x1F, 0x8A, 0xF9, 0x102: the starter gets one of each pair by `rand() % 2`, and the sequence's position depends on the idle loop's calls per frame). Deck 0's cards, the rest of the collection and the registration's fields stay compared. The port's frames (psxstack v0.3.3, the CD at 60 Hz): `openseg_loaded` 181, `title` 7057, `title_menu` 7059, `name_entered` 7634, `starter_chosen` 8148, `saiseg` 10203, `first_duel` 12686 |
| The port under ASan and UBSan | The sanitizer build (`-DPSXSTACK_SANITIZE=ON`, `build/port-san`) replays each script once with no report, and its log, record and SPU trace equal the plain build's. UBSan's suppressions are `tests/port/ubsan.supp` (psxstack's rule: only in-struct overruns the game relies on, each named with its function): `assignCardCopySerial`'s 8 serials in a row of 6. Fixed for the host instead: the heap's 4-byte alignment ("Memory and pointers"), `HUFFMAN_LEFT`/`HUFFMAN_RIGHT` (declared `s32` and indexed as tables of 0x220: arrays under `PC_PORT`, `HUFFMAN_NODE` in `decompress.c`) | `tests/port/run.py --sanitize`: the four scripts, CI's `replay` job |
| The window build (M2) | `build/port-sdl/dcb` (`-DPSXSTACK_SDL=ON`, the tools of `scripts/setup.sh sdl`) builds; the stack's input self-test passes on SDL's offscreen driver (every default key and a virtual gamepad's buttons reach the pad, hotkeys never do, the pause round trip; with the disc: #39); booted 600 frames in an offscreen window, paced in real time, its log and its picture at frame 500 equal the headless build's | `scripts/port_build.sh --sdl --boot` (CI `desktop`) |
| The launcher | `build/launcher/dcb-launcher` builds from `game.json`; its self-test passes (the settings directory, `settings.json`'s round trips, every screen with injected keys and a virtual gamepad, the play path), with the real disc's SHA-1 and the SDL game run 300 frames from the launcher's command; one known failure accepted by name (psxstack#57) | `scripts/launcher_build.sh` (CI `desktop`) |
| The port's VRAM and pictures against the emulator's (M2) | At every checkpoint of the four scripts, and at `title+120`, `saiseg+120` and `saiseg+600` (frames after `boot`'s and `new_game`'s last checkpoint), a `vram` step dumps the whole VRAM on both sides (psxstack's `vram` step: `PCSX.GPU.getVRAM()` in the emulator, `DCB_PORT_CHECKPOINT_DIR` in the port), keyed by the checkpoint's name, never by a frame; the emulator's prelude `tests/port/vram.lua` adds the displayed picture (`PCSX.GPU.takeScreenShot()`) and the game's cadence (its frame-buffer index and `vblanksPerFrame` over the last 60 vsyncs); the port is run twice, the second time with `--screenshot` at the frames the first run's record gives. Three checks per dump, each a gate unless `tests/port/vram_known.json` lists it: the whole VRAM, the textures and CLUTs (the VRAM right of the display buffers, which start at x 0), the displayed picture. **At M2's start:** the emulator is never CPU-bound at these dumps (its frame-buffer index flips every vsync, `vblanksPerFrame` 1; the movie's `openseg_loaded` excepted, where the render loop is not running), so all three can be compared everywhere. The whole VRAM is equal at `openseg_loaded`, `title+120`, `saiseg` and `first_duel`; the textures at every dump but `saiseg+120`; the picture everywhere but `title_menu` (5 pixels) and the four dumps of #33. Known: `title` (2 pixels' mask bit) and `title_menu` (one column of a mode-2 modulated sprite), the software GPU against PCSX-Redux's ([psxstack#54](https://github.com/gascarcella/psxstack/issues/54)); `name_entered`, `starter_chosen`, `saiseg+120`, `saiseg+600`: the port reaches them after other frame counts, so the scrolling background and SAISEG's loading and animations are at another phase (#33) | `tests/port/vram.py` (CI `replay`, about 70 s at `-j 4`; the differences as PNGs in `build/port-vram/<script>/`, `--keep` keeps the dumps) |

**The probes** (`tests/replay/probes.lua`): `stage` is the overlay slot's first word (each overlay's own id: SUGSEG
4, KAWSEG 5, SAISEG 6, SUBSEG 7, OPENSEG 8, EVOSEG 9, ENDSEG 10; 0 before a load); `map` the profile's `areaId`;
`random_index` libc's `rand()` state (`D_801DDC10`); the checkpoint image the player's profile, `PlayerProfile`
(0x2774 bytes, what a save writes), at its fixed heap address 0x800C8964 (the scripts assert it). The
stable hash zeroes `profileId` (drawn from `rand()` at creation) and `playTime`. The port's adapter implements the
same at M1 (`port/game/state.c`): the stage from the current overlay's own first word, the map, the image and the
scripts' `wait_mem` targets (`PLAYER_PROFILES` as the PS1 address of its heap block, OPENSEG's objects while OPENSEG
is loaded). The profile is not pointer-free: each partner keeps two pointers into the card database and each saved
deck thirty (`Partner.baseCard`/`armorCard`, `CardSlot.card`, heap addresses on the PS1), so the host's struct is
0x2A78 bytes and the adapter writes the image field by field in the PS1 layout, the pointers as PS1 heap addresses.
On the host `random_index` is LIBC2's `rand` state, `port_rand_seed()` (the shim's `rand` is the PS1's generator since
psxstack v0.3.3): it is **recorded, not compared** (the cross-core view leaves it out), because `main()`'s idle loop
calls `rand()` as often as it spins between vsyncs on the PS1, so the sequence's position at a checkpoint depends on
the core's timing.

**The core.** PCSX-Redux's dynarec cannot run this game: the overlay loader's first CD read never completes
(`FILE_LOADER_BUSY` stays 1, the vblank event stops after about 165 frames; with OpenBIOS and the retail BIOS alike,
and the log shows an unanswered pad command 0x43), while the interpreter core plays it. Every emulator run here is
on the interpreter (`tests/replay/replay.py` adds `-interpreter` to every command and marks the records
`"core": "interpreter"`); psxstack's runner assumes the dynarec is the recording core, so the driver wraps it until
the stack lets a game choose (a psxstack issue). The port's test compares the cross-core view, which does not
depend on the core.

**What the scripts know about the game:** dialogs opened by `initDialog` start with their cursor on No unless the
caller sets `choice = 1`; the name entry's, the starter deck's and the save screen's "create a file" dialogs do not,
so the scripts press LEFT before confirming them; the registration's own Yes/No pages do, so a plain CROSS takes the
longer "tell me about the game" path. The opening movie cannot be skipped (about 7,000 frames). A fresh card from
the emulator is formatted (15 free blocks); the save goes to File 1.

## Open questions
Decided on 2026-10-08 (the choices are in "The contract, for this game" and above): the heap (`memory.heap` becomes
optional in psxstack, [psxstack#26](https://github.com/gascarcella/psxstack/issues/26)), overlay identity and the
scratchpad (game-side, no contract change), the GTE macros (a host `port/include/gte.h`), the probe's CI gate (it
must run; the counts are the baseline until M1 drives them to zero), and the test runners (lifted into psxstack
first, [psxstack#27](https://github.com/gascarcella/psxstack/issues/27), then consumed here, issue #3). Still open:
1. ~~Fibers in psxstack~~ Done in psxstack 0.3.0 ([psxstack#25](https://github.com/gascarcella/psxstack/issues/25);
   its `docs/PORT.md` "Fibers", `examples/tasks`): `port_fiber_create/switch/exit/destroy/preempt`, a vblank handler
   preempts at the end of the tick. M1's glue: `spawnTask` creates, `yieldTask`/`waitFrames` switch, `exitTask`
   exits, `handleVsyncPreemption` preempts to TASKS[0]; `main()`'s spin gets a `PLATFORM_WAIT()` (done: "The task
   scheduler", "On the host"). Whether the pump's points suffice for `memcard.c`'s spin is checked then.
2. ~~The hooking strategy~~ Decided (2026-10-08): **a pointer-width integer typedef**, `s32p`/`u32p` (`s32`/`u32` on
   the PS1, `intptr_t`/`uintptr_t` under `PC_PORT`, defined in `include/port.h`), at the declarations of the
   pointer-holding globals, parameters, return values and fields and at the `(s32)&x` casts ("The host-compile
   probe": ~40 declarations silence ~1,900 sites, ~950 casts are rewritten with it); psxstack's `PTR_TO_U32` for
   `setaddr`, `PTR_TO_S32` only where an integer lives in a PS1-sized layout (`HeapBlock.addr`, the task context).
   Both sides are byte-identical; the diff stays readable for upstream. (At step 3 `HeapBlock.addr` became `s32p`,
   and the PS1-sized words that hold heap pointers, the script registers, use the game's `GAME_PTR_TO_S32`.)
3. The stale KAWSEG addresses (0x801E6424, 0x801E651C): what happens on the PS1 when they run? With the emulator
   (issue #3), before the adapter resolves them at M1.
4. ~~Psy-Q declarations~~ Done in psxstack 0.3.0 ([psxstack#7](https://github.com/gascarcella/psxstack/issues/7)):
   the stack owns them; M1 fixes the 16 prototypes of `game.h` that `decls` reports and makes `decls` a gate.
5. Would upstream take the hooks? They leave the PS1 build identical, and upstream's CONTRIBUTING.md forbids
   `NON_MATCHING`, not `PC_PORT`. Not asked yet (the owner's call, 2026-10-08): the hooks stay in the fork for now;
   the 36 missing prototypes and the 4 duplicate overlay names are upstream PR candidates on their own.
6. ~~The heap's alignment on the host~~ Decided (2026-10-08, issue #25): the block table keeps the PS1's addresses
   and the host spaces the blocks twice as wide, with the PS1's leftover bytes copied in ("Memory and pointers"):
   the records keep their meaning (the same PS1 addresses and the same bytes), and the sanitizer runs with no
   alignment exception. Still open there: the allocations sized by PS1 byte counts (issue #31).
