/* The game adapter (psxstack's include/psxstack/game.h; GAME_CONTRACT.md "4. The adapter units"): the port's
 * game-specific C, compiled with the runtime. At M0 it is empty: every adapter function has a weak default in
 * psxstack (runtime/game_defaults.c) that returns 0 or does nothing. M1 adds here what the port needs from this game:
 * game_apply_rate (nothing: the game has one rate), the state probes the replay scripts read, the overlay name to
 * file-ID map the loader hook asks for ("P:\\kawseg.bin" -> port/tools/port_inputs.py's index), the scratchpad
 * buffer, and later the mods. */
#include "port_runtime.h"

#include <stdint.h>

/* The scratchpad: what the game's SCRATCHPAD(type, ofs) points into (include/port.h, which declares it and sets
 * PORT_SCRATCHPAD_SIZE, 0x800). The PS1's is 1 KB at 0x1F800000; the host's is larger because the structs the game
 * keeps there are wider (SortWork, include/dcb/tmd_sort.h). Scratch data: nothing saves it or reads it from the disc. */
_Alignas(16) uint8_t port_scratchpad[0x800];

/* ISO C wants a declaration in every translation unit; this one goes away with the first real function. */
typedef int dcb_adapter_placeholder;

/* ===================================== The overlay loader's hook (M1) ============================================= *
 * game_overlay_id (include/port.h): src/main/system/loader.c's loadFileToAddress asks it which overlay the path it
 * has just read is, and hands the answer to the overlay manager (OVERLAY_COPY). overlay_ids.h is
 * port/tools/port_inputs.py's, from mk/version/us.mk's OVERLAYS: the same file IDs as overlays.txt. The probes and
 * the checkpoint image are in state.c. */
#include "overlay_ids.h"

_Static_assert(GAME_OVERLAY_TIER == 1 && PORT_SLOT_COUNT == 1, "the overlays' slot is game.json's memory.slots[0]");
_Static_assert(PS1_OVERLAY_AREA == PORT_SLOT1_BASE, "config/us OVERLAY_AREA is game.json's slot base");

static const struct {
    const char *name; /* in P.DRV */
    int32_t id;
} game_overlay_files[GAME_OVERLAY_COUNT] = GAME_OVERLAY_FILES;

static int game_same_name(const char *a, const char *b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char)(*a - 'A' + 'a') : *a;
        char y = *b >= 'A' && *b <= 'Z' ? (char)(*b - 'A' + 'a') : *b;
        if (x != y) {
            return 0;
        }
    }
    return *a == *b;
}

/* "P:\\kawseg.bin" -> the overlay's file ID; 0 for another drive (not an overlay); fatal for an overlay this version
 * does not have (jp's NISSEG and INTSEG). */
int32_t game_overlay_id(const char *path) {
    int i;
    if (path == NULL || (path[0] != 'P' && path[0] != 'p') || path[1] != ':' || path[2] != '\\') {
        return 0;
    }
    for (i = 0; i < GAME_OVERLAY_COUNT; i++) {
        if (game_same_name(path + 3, game_overlay_files[i].name)) {
            return game_overlay_files[i].id;
        }
    }
    port_fatal("overlay: %s is not one of this version's overlays (mk/version/us.mk OVERLAYS)", path);
}
