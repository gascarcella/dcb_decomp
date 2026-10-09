/* LIBC2's bzero and bcopy for the Windows build. The shim leaves LIBC2's string functions to the host's libc
 * (psxstack's psyq/README.md "LIBC2, and LIBAPI's names the host has too"); glibc keeps the BSD bzero and bcopy, the
 * Windows C runtime (UCRT) has neither, so the Windows build defines them here, with include/game.h's prototypes,
 * until the stack does (psxstack#61). bzero zeros n bytes (no caller uses its value); bcopy copies n bytes from its
 * first argument to its second, overlapping ranges as libc's does. Compiled with -fno-builtin, as state.c and tasks.c
 * are (port/CMakeLists.txt): game.h declares these with the PS1's types, which clash with the compiler's builtins. */
#ifdef _WIN32
#include "common.h"
#include "game.h"

void *bzero(void *p, s32 n) {
    u8 *d = p;
    s32 i;
    for (i = 0; i < n; i++) {
        d[i] = 0;
    }
    return p;
}

void bcopy(void *src, void *dst, s32 n) {
    const u8 *s = src;
    u8 *d = dst;
    s32 i;
    if (d < s) {
        for (i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (i = n - 1; i >= 0; i--) {
            d[i] = s[i];
        }
    }
}
#else
typedef int dcb_libc_win32_unused; /* ISO C wants a declaration in every translation unit */
#endif
