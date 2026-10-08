#ifndef PORT_H
#define PORT_H

/*
 * The PC port's hooks (docs/PORT.md; psxstack's docs/GAME_CONTRACT.md "2. The
 * hook header"). common.h includes this file after its typedefs: no unit
 * includes it directly.
 *
 * Without PC_PORT (the PS1 build) every macro expands to exactly the code the
 * unit had, so no byte of the executable or of an overlay changes, and
 * nothing of the port is needed. With -DPC_PORT (the 64-bit host build) the
 * macros come from psxstack's psxstack/hooks.h.
 *
 * s32p and u32p are an s32 and a u32 that hold a pointer: the globals,
 * parameters, return values and fields the game keeps addresses in. They are
 * s32 and u32 on the PS1 and pointer-sized on the host (docs/PORT.md "Open
 * questions").
 */

#ifndef PC_PORT

typedef s32 s32p;
typedef u32 u32p;

/* the body of a busy-wait that only an interrupt ends: nothing */
#define PLATFORM_WAIT()
/* a pointer kept in an integer, and back */
#define PTR_TO_S32(p) ((s32)(p))
#define S32_TO_PTR(type, v) ((type)(v))
/* the pointer's bits: the ordering-table tags (setaddr) */
#define PTR_TO_U32(p) ((u32)(p))
/* a function or data at a fixed address in the overlay area */
#define SLOT_FUNC(type, addr) ((type)(addr))
#define OVERLAY_FN(tier, fn) (fn)
#define SLOT_PTR(tier, type, addr) ((type)(addr))
/* the scratchpad, the 1 KB of fast RAM at 0x1F800000: a pointer `ofs` bytes in */
#define SCRATCHPAD(type, ofs) ((type)(0x1F800000 + (ofs)))
/* a pointer into the game's own memory kept in an s32 (a script register, an
   s32 field), and back: the address itself on the PS1 */
#define GAME_PTR_TO_S32(p) ((s32)(p))
#define GAME_S32_TO_PTR(type, v) ((type)(v))

#else

#include <stdint.h>

#include "psxstack/hooks.h"

typedef intptr_t s32p;
typedef uintptr_t u32p;

/* The scratchpad: a static buffer of the adapter's (port/game/game.c), larger than the PS1's 1 KB because the host's
   structs in it are wider (SortWork: include/dcb/tmd_sort.h). Nothing saves it or reads it from the disc. */
#define PORT_SCRATCHPAD_SIZE 0x800
extern u8 port_scratchpad[PORT_SCRATCHPAD_SIZE];
#define SCRATCHPAD(type, ofs) ((type)(port_scratchpad + (ofs)))

/* A pointer kept in an s32 on the PS1 (docs/PORT.md "Memory and pointers"): one into the game's heap (HEAP_ARENA)
   becomes the PS1 address of the same byte, so the 32-bit word holds what it holds on the PS1; NULL is 0; anything
   else goes to psxstack's PTR_TO_S32 (the overlay slot; fatal elsewhere). Defined in src/main/system/heap.c. */
s32 game_ptr_to_s32(const void *p);
void *game_s32_to_ptr(s32 v);
#define GAME_PTR_TO_S32(p) game_ptr_to_s32(p)
#define GAME_S32_TO_PTR(type, v) ((type)game_s32_to_ptr(v))

#endif

#endif /* PORT_H */
