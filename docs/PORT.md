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
| `INCLUDE_DIRS` | `port/include/`, `include/`, the root. Game code doesn't include the Psy-Q headers (`task.c` alone includes `<kernel.h>` for the BIOS TCB): `include/game.h` redeclares the types (`:69-151`) and the prototypes (`~:1485-1660`). There is no `include/psyq/`, so since psxstack 0.3.0 the shim compiles against the stack's own declarations (psxstack #7) and `port_inventory.py decls` compares `game.h`'s prototypes with them: 29 same, 9 compatible, 16 mismatching at M0 (`s32` returns where the stack returns `void` or a pointer, `ClearImage`'s colours); fixed game-side at M1, byte-identical: 45 same, 9 compatible, 0 mismatching (against psxstack main's 184 at step 3: 91 same, 12 compatible, 0 mismatching; `game.h` declares the stack's return types under `PC_PORT`, as for `LoadImage`) |

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
  CONTRIBUTING.md asks, or `PC_PORT` renames).

### What got in the way in psxstack v0.2.1
Fixed in v0.3.0 (psxstack #28, #7): the GTE override is written at the game's header's path, the inventory takes a
Psy-Q library map (`psyq_libraries`), `psyq/check.sh` takes its paths, and the runtime and the shim no longer see the
game's include directories (this tree's `include/stdarg.h` shadowed the host's). The wrapper here lost its own
override and derived symbol file with the pin bump.

## What the game needs

### The task scheduler (the largest new piece)
- **The tasks:** `TASKS[32]` (`include/dcb/main.h:20-30`, 0xC0 bytes each, a kernel-TCB-shaped `regs[40]`). TASKS[0] is
  the main task.
- **The C side:** `src/main/system/task.c` has `createTask` :167, `selectNextTask` :139, `handleVsyncPreemption` :106,
  `killTask` :263 and `wakeTask` :355.
- **The asm side (`src/main/startup.s`):** `spawnTask`, `endTask` and `resumeTask` are stubs that disable interrupts,
  call the C routine and return with `rfe`. `exitTask`, `yieldTask`, `waitFrames` and `waitFramesResume` share one
  body: it saves s0-s7/gp/sp/fp/ra into `CURRENT_TASK`, picks the next task and restores it.
- **Switching is cooperative and preemptive:**
  - Cooperative: `yieldTask` and `waitFrames` (607 calls; 120 are `waitFrames(0x7FFFFFFF)`, sleep until
    `resumeTask`).
  - Preemptive: a vblank root-counter event (`OpenEvent(RCnt3)` + `SetRCnt`/`StartRCnt`, task.c:95-98) runs
    `handleVsyncPreemption` in interrupt context. It ticks the vblank counters and, when the current task's priority
    isn't 0, switches to TASKS[0].
- **Stacks:** 0x100 to 0x2000 bytes, from the game heap (tag -3). They are far too small for x86-64 frames, so on the
  host each task needs a host stack of its own.
- **On the host:** one fiber per task. psxstack has no fibers yet; it runs the game on one stack of its own, and save
  states and AddressSanitizer already know about that stack. Preemption can only happen where the host gets control:
  at the pump's vblank points (`PLATFORM_WAIT` in the spins below, `VSync`, the waits). Save states must hold every
  live task's stack. This is generic stack work: any game that uses Psy-Q's own threads (`OpenTh`/`ChangeTh`) needs
  the same. It is a psxstack issue.
- **Pointers in ints:** `spawnTask` is declared without a prototype (`s32 spawnTask();`, `include/game.h:1522`; 244
  calls in 50 files). It receives function pointers and string literals, and `createTask` takes the entry and its
  four arguments as `s32`. `Task.stack` and `Task.regs` are `s32`. On the host the entry and the arguments are
  pointer-sized.

### Overlays
- **Loading:**
  - A task runs `loadFileToAddress("P:\\kawseg.bin", OVERLAY_LOAD_ADDR, …)` (`src/main/system/loader.c:87`), then
    the caller sleeps until the loader calls `resumeTask`.
  - `"X:\\name"` means the archive `\X.DRV;1` (found by `CdSearchFile`), then the name in its directory: `FileEntry`,
    `include/dcb/cd_file.h:6-12`, up to 0x200 entries.
  - The drives are A B C E F G M P; P holds the overlays.
  - There are 38 load sites, all in the EXE. No overlay loads another.
- **Calls into an overlay:** 211 references in 13 EXE files, 29 of them `spawnTask` entries. They use absolute
  addresses in the overlay area; there is no jump table. The EXE assumes the right overlay is resident. Each is a
  `SLOT_FUNC`/`LATE_FUNC` site on the host.
- **Overlays calling the EXE:** they reference EXE symbols as absolute addresses (`Makefile:221-226`).
- **Stale addresses:**
  - KAWSEG calls `SUG_createFadeRect` (0x801E6424) and has a table entry at 0x801E651C (`kaw_effect.c:98, 511`).
    Both point into KAWSEG's own `KAW_chooseSupportCard`, so they look stale (leftovers from SUGSEG's layout).
  - The adapter must know what those sites do at run time before it resolves them.
  - The `undefined_syms_{sai,sub,open}seg.txt` files define 81 symbols in the middle of overlay structs.

### Memory and pointers
- **The game heap:** `src/main/system/heap.c` manages `HEAP_ARENA` (0x8008C848, `HEAP_SIZE` 0x148000, inside the
  EXE's `.bss`; on the host a game `.bss` array, not psxstack's arena: `game.json` has no heap).
  - `HeapBlock.addr` keeps the in-use flag in bit 31 and strips the KSEG bits of free blocks. Done (M1 step 3): it is
    `s32p` (the table is in memory only), and the `HEAP_ADDR_*` macros of `heap.c` are the original expressions on
    the PS1; on the host the flag is the top bit of the pointer-wide word.
  - A heap pointer kept in an `s32` (a script register, below) goes through `GAME_PTR_TO_S32`/`GAME_S32_TO_PTR`
    (`include/port.h`, defined in `heap.c`): on the host it becomes the PS1 address of the same byte, so the word
    holds what it holds on the PS1; a pointer elsewhere goes to psxstack's `PTR_TO_S32` (the slot, else fatal).
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
    `GAME_PTR_TO_S32`, and the readers (the text lines, the tutorial's names, SUGSEG's `vramEntries`) through
    `GAME_S32_TO_PTR`. SAISEG's `ScriptRunner.unk0` (the script file) is `s32p`.
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
- `memcard.c:178-192` (a counter another task increments: progress needs preemption).
- Nine `while (CdInit()/CdControlB()/CdRead() == 0)` loops in `cd_file.c`.

About 140 more polling loops call `waitFrames`/`yieldTask` and are fine once the scheduler works. `VSync` is called
in 10 places.

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
| The port against the emulator | The port replays the same scripts and reaches the emulator's checkpoints (the cross-core view: names, stages, maps, stable profile hashes, the overlay sequence) twice byte-identical | `tests/port/run.py`: written, **not runnable until M1** (the port does not link) |

**The probes** (`tests/replay/probes.lua`): `stage` is the overlay slot's first word (each overlay's own id: SUGSEG
4, KAWSEG 5, SAISEG 6, SUBSEG 7, OPENSEG 8, EVOSEG 9, ENDSEG 10; 0 before a load); `map` the profile's `areaId`;
`random_index` libc's `rand()` state (`D_801DDC10`); the checkpoint image the player's profile, `PlayerProfile`
(0x2774 bytes, pointer-free, what a save writes), at its fixed heap address 0x800C8964 (the scripts assert it). The
stable hash zeroes `profileId` (drawn from `rand()` at creation) and `playTime`. The port's adapter implements the
same at M1 (`game_state_*`).

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
   exits, `handleVsyncPreemption` preempts to TASKS[0]; `main()`'s spin gets a `PLATFORM_WAIT()`. Whether the pump's
   points suffice for `memcard.c`'s spin is checked then.
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
