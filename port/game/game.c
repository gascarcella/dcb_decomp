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
