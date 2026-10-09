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
/* a heap block's size the PS1 code writes as its byte count, of an object whose host type is larger: ps1 */
#define HOST_FITS(ps1, host) ps1
/* a function the C declares void whose caller keeps its result through a function pointer (the effect scripts'
   constructors): on the PS1 the result is v0 as the function's last call left it */
#define V0_RESULT void
#define V0_RETURN

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
   becomes the PS1 address of the same byte (its block's PS1 address plus its offset in the block: the host spaces the
   blocks wider), so the 32-bit word holds what it holds on the PS1; NULL is 0; anything else goes to psxstack's
   PTR_TO_S32 (the overlay slot; fatal elsewhere). Defined in src/main/system/heap.c. */
s32 game_ptr_to_s32(const void *p);
void *game_s32_to_ptr(s32 v);
s32 game_heap_owns(const void *p); /* whether p is in the heap's host bytes */
s32 game_heap_holds(s32 v);         /* whether v is a PS1 address in the heap */
#define GAME_PTR_TO_S32(p) game_ptr_to_s32(p)
#define GAME_S32_TO_PTR(type, v) ((type)game_s32_to_ptr(v))

/* A PlayerProfile (include/game.h) to and from its PS1 layout, 0x2774 bytes, its card pointers as PS1 heap addresses:
   what a save on the memory card holds (src/openseg/memcard/open_save.c) and the checkpoint image. Defined in
   port/game/state.c (void pointers: this header comes before game.h's types). */
void game_profile_to_ps1(u8 *out, const void *profile);
void game_profile_from_ps1(void *profile, const u8 *in);

/* A heap block's size the PS1 code writes as its byte count, of an object whose type (host, a sizeof) is larger on the
   host: the block keeps the PS1's size, so the heap's table and every later block keep the PS1's addresses, and the
   host's bytes fit because heap.c spaces each block HEAP_HOST_SCALE (sizeof(void *) / 4) times its size; checked at
   compile time (issue #31) */
#define HOST_FITS(ps1, host) (0 * sizeof(char[(host) <= (ps1) * (sizeof(void *) / 4) ? 1 : -1]) + (ps1))

/* A function the C declares void whose caller keeps its result through a function pointer (KAWSEG's and SUGSEG's
   effect-script constructors, *_create*FromParams): on the PS1 the result is v0 as the function's last call, the
   object's constructor, left it. The host returns that call's result: x86-64's rax survives the call only by chance
   (a stack protector's check clobbers it, and the entry kept a null object) */
#define V0_RESULT void *
#define V0_RETURN return

/* The arguments of a call to one of startup.s's stubs that the game declares without a prototype and calls with as
 * many arguments as it uses (spawnTask, resumeTask; include/game.h): each one an s32p, the missing ones 0. On the
 * PS1 a missing argument is whatever its register or stack slot held; on the host it is 0, so a run repeats. */
#define PORT_S32P_ARGS2_(a, b, ...) (s32p)(a), (s32p)(b)
#define PORT_S32P_ARGS2(...) PORT_S32P_ARGS2_(__VA_ARGS__, 0, 0)
#define PORT_S32P_ARGS9_(a, b, c, d, e, f, g, h, i, ...) \
    (s32p)(a), (s32p)(b), (s32p)(c), (s32p)(d), (s32p)(e), (s32p)(f), (s32p)(g), (s32p)(h), (s32p)(i)
#define PORT_S32P_ARGS9(...) PORT_S32P_ARGS9_(__VA_ARGS__, 0, 0, 0, 0, 0, 0, 0, 0, 0)

/*
 * The overlays (docs/PORT.md "Overlays"). The host links every overlay in, and
 * the executable's calls into them (include/dcb/overlay_calls.h) are calls by
 * name: each name is its own overlay's, prefixed, so the linker binds it to
 * the right host function and nothing needs a tag. loadFileToAddress, which
 * reads an overlay into the area, tells the overlay manager which one it was:
 * game_overlay_id (port/game/game.c) maps the loader's path ("P:\\kawseg.bin")
 * to the overlay's file ID (port/tools/port_inputs.py: its 1-based index in
 * mk/version/us.mk's OVERLAYS); 0 for a path that is not an overlay.
 */
int32_t game_overlay_id(const char *path);

#endif

#endif /* PORT_H */
