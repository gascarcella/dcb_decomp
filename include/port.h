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

#else

#include <stdint.h>

#include "psxstack/hooks.h"

typedef intptr_t s32p;
typedef uintptr_t u32p;

#endif

#endif /* PORT_H */
