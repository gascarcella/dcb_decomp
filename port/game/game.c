/* The game adapter (psxstack's include/psxstack/game.h; GAME_CONTRACT.md "4. The adapter units"): the port's
 * game-specific C, compiled with the runtime. At M0 it is empty: every adapter function has a weak default in
 * psxstack (runtime/game_defaults.c) that returns 0 or does nothing. M1 adds here what the port needs from this game:
 * game_apply_rate (nothing: the game has one rate), the state probes the replay scripts read, the overlay name to
 * file-ID map the loader hook asks for ("P:\\kawseg.bin" -> port/tools/port_inputs.py's index), the scratchpad
 * buffer, and later the mods. */
#include "port_runtime.h"

/* ISO C wants a declaration in every translation unit; this one goes away with the first real function. */
typedef int dcb_adapter_placeholder;
