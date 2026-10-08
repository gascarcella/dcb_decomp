# The PC port

The port compiles upstream's matching C for a 64-bit host and links it with
[psxstack](https://github.com/gascarcella/psxstack): its runtime, its Psy-Q shim and its launcher. The stack's
`docs/GAME_CONTRACT.md` defines what this game supplies. [dw2003recomp](https://github.com/gascarcella/dw2003recomp) is
the worked example of every piece (its `docs/PORT.md`, `port/`, `tools/port_inputs.py`, `tests/`). This page holds
the plan, what this game needs from the stack, and the open questions. It will become the port's reference as the
answers land.

Nothing of the port exists yet. The facts below come from a survey of the `us` sources on 2026-10-08: the 155 C files
that `mk/version/us.mk` lists. Counts are from grep and approximate.

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
| `game.json` identity | `id` (the binary's and the settings' name: `dcb` is a candidate), the title, `launcher.about` crediting the decompilation (DECISIONS "Credit") |
| Disc | SLUS-01328, "Digimon Digital Card Battle (USA)": SHA-1 `b3945b3e76c1fcc554a7614e2b4211d974990105`, 215661936 bytes, one MODE2/2352 track |
| `video.rate` | 60 (NTSC) |
| `memory.slots` | **One slot**, the overlay area at `0x801DDF38` (`OVERLAY_AREA`, `config/us/symbols_overlay_calls.txt`). The largest overlay is KAWSEG, `0x1EAF8` bytes (to `0x801FCA30`; OPENSEG reaches `0x801F8188`). Each overlay's zeroed data is inside its file, so file size = memory size. KAWSEG and OPENSEG run into the 32 KB the start-up reserves for the stack (`0x801F8000`-`0x80200000`) |
| `memory.heap` | Open: see "Memory and pointers" |
| `UNITS`, `MAIN_UNIT` | `MAIN_C_SRC` and `<OVERLAY>_C_SRC` of `mk/version/us.mk`, without `src/main/psyq/` (the shim replaces the libraries), `startup.s` and `libmath.s` (host replacements, below). `main()` is `src/main/main.c:54`: it starts the scheduler with `runMainTask` (`src/main/system/boot.c:76`) and spins on `rand()` |
| `OVERLAYS` | ENDSEG, EVOSEG, KAWSEG, OPENSEG, SAISEG, SUBSEG, SUGSEG, all in slot 1. psxstack keys an overlay by a file ID; this game loads by name from `P.DRV` (below), so the adapter or the stack has to map names. Open |
| `EXE_SYMBOLS` | `config/us/`'s symbol files (the EXE's names, and `symbols_overlay_calls.txt` for the 84 overlay functions the EXE calls) |
| `GTEMAC` | Not the same shape as dw2003's `gtemac.h`: `include/gte.h` has 40 `gte_*` macros, 33 of them `__asm__` over `include/gte_macros.inc`, used 270 times in 6 files (`tmd_sort.c` 230). They need host forms over the software GTE. Open: psxstack's GTEMAC translation or a host `gte.h` under `PC_PORT` |
| `INCLUDE_DIRS` | `include/`, the root, `external/psyq_headers/psyq_lib47/include`. Game code doesn't include the Psy-Q headers: `include/game.h` redeclares the types (`:69-151`) and the prototypes (`~:1485-1660`). That bears on psxstack #7 |

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
  EXE's `.bss`).
  - `HeapBlock.addr` is an `s32` with the in-use flag in bit 31 and KSEG masking (`heap.c:24, 82, 151, 190`).
  - On the host the arena has to sit where `PTR_TO_S32` works (psxstack's arena), or the block table has to hold
    offsets.
  - psxstack's `memory.heap` must be contiguous with the slots, and this heap is not: the rest of `.bss` and Psy-Q's
    `.bss` lie between them. Open: a contract change, or a heap region stretched to the slot.
- **Psy-Q's libc heap:** `InitHeap` puts it over the overlay area, but game code never calls malloc or free.
- **Pointers held in integers:**
  - `addFrameCallback(s32)`: 56 sites, 55 with `(s32)fn`.
  - Globals declared `s32` and cast at use: `PLAYER_PROFILES`, `SPRITE_POOL_CURSOR`, …; 521 `((T *)GLOBAL)` casts.
  - Raw casts: `(s32|u32)&x` 231 in 20 files, `(s32|u32)ident` 433 in 66 files, `*(s32 *)(p + off)` 107 in 12 files.
  - `OVERLAY_LOAD_ADDR` is declared `s32` (`duel_util.c:149`).
  - psxstack's `PTR_TO_S32`/`S32_TO_PTR` cover the arena; the probe's `-Werror` on pointer/int casts will list each
    site.
- **Ordering tables:** `P_TAG.addr:24` with `addPrim`/`setaddr` (293 uses in 44 files), and `packet & 0xFFFFFF` in
  `tmd_sort.c:621, 640`. psxstack's tag window already handles 24-bit tags over the units' data and the arena
  (psxstack `docs/PORT.md` "Ordering tables on 64-bit").
- **Scratchpad:**
  - `0x1F800000` is used in 8 files. Most uses are the `SORT_WORK` macro (`include/dcb/tmd_sort.h:6`, about 119 uses
    in `tmd_sort.c`); others are in `camera.c`, `scene3d.c`, `render_loop.c` (clears 1 KB) and the cutscene and
    sprite code.
  - psxstack has no scratchpad region: its only scratchpad hook is `PORT_SCRATCHPAD_STACK_*`, for dw2003's stack
    switch, which does nothing on the host. Here the game stores data there, so the host needs a 1 KB buffer behind
    a macro: a small hook here, or a stack region.
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

## Open questions
1. Fibers in psxstack: the API (create, switch, destroy), Windows (fibers) and Linux (`ucontext` or hand-written
   switches), AddressSanitizer annotations, save states holding every stack. When may a vblank preempt? Only at the
   pump's points: are they enough for `memcard.c`'s spin?
2. The heap: in psxstack's arena, through the `HEAP_*` macros, with a contract change for a heap that isn't contiguous
   with the slot?
3. Overlay identity: psxstack keys overlays by file ID, and this game names them inside `P.DRV`.
4. GTE: a host `gte.h` under `PC_PORT`, or psxstack's `GTEMAC` translation extended to this macro set.
5. The stale KAWSEG addresses (0x801E6424, 0x801E651C): what happens on the PS1 when they run?
6. Psy-Q declarations: this game redeclares them in `include/game.h` and builds its libraries against
   `jype0/psyq_headers` (Psy-Q 4.7). psxstack #7 decides where the shim's declarations come from.
7. Would upstream take the hooks? They leave the PS1 build identical, and upstream's CONTRIBUTING.md forbids
   `NON_MATCHING`, not `PC_PORT`. Ask juandav once the first hooks exist, through the owner.
