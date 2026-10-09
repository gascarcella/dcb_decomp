# The PC port (docs/PORT.md; DECISIONS "A fork of ReGame-Labs/dcb_decomp, for a PC port on psxstack"): the game's C
# units, its adapter (port/game/: game.json, the probes and mods to come) and psxstack (the runtime, the Psy-Q shim,
# the build) linked into one host binary, build/port/dcb. Adapted from dw2003recomp's port/CMakeLists.txt
# (docs/THIRD_PARTY.md).
#
#   cmake -S port -B build/port -G Ninja && cmake --build build/port && build/port/dcb --max-frames 60
#
# psxstack is the submodule at ../psxstack (PSXSTACK_DIR; scripts/worktree_init.sh checks it out at the pin; a
# checkout of the stack under development is -DPSXSTACK_DIR=$PWD/../psxstack). port/tools/port_inputs.py writes the
# build inputs psxstack_add_game() takes (build/port/gen/inputs/: the units, the overlays and the volatile ranges, from
# mk/version/us.mk, config/us/ and tests/replay/replay.py) and the adapter's overlay_ids.h at configure time, and
# port/tools/port_bss.py the executable's game .bss as C (build/port/gen/bss_standins.c, a MAIN unit: docs/PORT.md).
# The PS1 build (the Makefile) is untouched: its C sees none of this.
cmake_minimum_required(VERSION 3.20)
project(dcbport C)

get_filename_component(DCB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(PSXSTACK_DIR "${DCB_ROOT}/psxstack" CACHE PATH "The psxstack checkout (the submodule; or another checkout of the stack)")
if(NOT EXISTS "${PSXSTACK_DIR}/cmake/psxstack.cmake")
    message(FATAL_ERROR "psxstack not found at ${PSXSTACK_DIR}: the submodule is not checked out (git submodule update "
                        "--init psxstack, or scripts/worktree_init.sh; or -DPSXSTACK_DIR=<path>)")
endif()
# The generators run in the project's Python (scripts/setup.sh makes .venv; a worktree links it).
if(EXISTS "${DCB_ROOT}/.venv/bin/python" AND NOT PSXSTACK_PYTHON)
    set(PSXSTACK_PYTHON "${DCB_ROOT}/.venv/bin/python" CACHE FILEPATH "" FORCE)
endif()
set(PSXSTACK_VERSION_ROOT "${DCB_ROOT}" CACHE PATH "" FORCE)   # the fork's tags name the build
include("${PSXSTACK_DIR}/cmake/psxstack.cmake")

# ---- The build inputs (port/tools/port_inputs.py) and the game .bss (port_bss.py through its --bss), regenerated
# when their sources change: the .bss's labels come from config/, the headers that declare them from include/.
set(DCB_VERSION "us" CACHE STRING "The game version to port (mk/version/<version>.mk, config/<version>/)")
set(DCB_INPUTS "${CMAKE_BINARY_DIR}/gen/inputs")
execute_process(COMMAND "${PSXSTACK_PYTHON}" "${DCB_ROOT}/port/tools/port_inputs.py" --out "${DCB_INPUTS}"
                        --bss "${CMAKE_BINARY_DIR}/gen/bss_standins.c" --version "${DCB_VERSION}"
                RESULT_VARIABLE r OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT r EQUAL 0)
    message(FATAL_ERROR "port/tools/port_inputs.py failed:\n${out}${err}")
endif()
string(STRIP "${out}" out)
string(REPLACE "\n" "\n-- " out "${out}")   # a status prefix on each line (port_bss.py's, port_inputs.py's)
message(STATUS "${out}")
string(TOUPPER "${DCB_VERSION}" DCB_VERSION_UPPER)
file(GLOB DCB_CONFIG_DEPS "${DCB_ROOT}/config/${DCB_VERSION}/symbols*.txt" "${DCB_ROOT}/config/${DCB_VERSION}/main.yaml"
                          "${DCB_ROOT}/config/${DCB_VERSION}/undefined_syms*.txt")
file(GLOB_RECURSE DCB_HEADER_DEPS "${DCB_ROOT}/include/*.h")
file(GLOB DCB_ADAPTER_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/game/*.c")

# The game's C compiles with its version switch (include/version.h) and without the splat assembly includes
# (include/include_asm.h: SKIP_ASM makes INCLUDE_ASM/INCLUDE_RODATA empty and drops the labels.inc asm). The GTE
# macros' host forms will be port/include/gte.h, first on the include path (docs/PORT.md), so no GTEMAC here.
psxstack_add_game(dcb
    GAME_JSON "${CMAKE_CURRENT_SOURCE_DIR}/game/game.json"
    UNITS "${DCB_INPUTS}/units.txt"
    MAIN_UNIT "${DCB_ROOT}/src/main/main.c"
    OVERLAYS "${DCB_INPUTS}/overlays.txt"
    EXE_SYMBOLS "${DCB_ROOT}/config/${DCB_VERSION}/symbols.txt"
    VOLATILE "${DCB_INPUTS}/volatile.txt"
    INCLUDE_ASM_GUARD INCLUDE_ASM_H
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include" "${DCB_ROOT}/include" "${DCB_ROOT}"
    ADAPTER ${DCB_ADAPTER_SRCS}
    DEFINES VERSION_${DCB_VERSION_UPPER} SKIP_ASM
    CONFIGURE_DEPENDS "${DCB_ROOT}/port/tools/port_inputs.py" "${DCB_ROOT}/port/tools/port_bss.py"
                      "${DCB_ROOT}/mk/version/${DCB_VERSION}.mk" "${DCB_ROOT}/tests/replay/replay.py" ${DCB_CONFIG_DEPS}
                      ${DCB_HEADER_DEPS})

# The adapter's port/game/tasks.c (startup.s's task glue) includes the game's headers for Task and the scheduler's
# prototypes, and game.h declares libc's functions with the PS1's types (strlen, memset, bzero, ...): under the
# adapter's -Werror those clash with the compiler's builtins. The units compile with -fno-builtin for the same reason.
# state.c (the probes) includes them too, and libc_win32.c (the Windows build's bzero and bcopy) defines two of them.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/game/tasks.c" "${CMAKE_CURRENT_SOURCE_DIR}/game/state.c"
                    "${CMAKE_CURRENT_SOURCE_DIR}/game/libc_win32.c"
             APPEND PROPERTY COMPILE_OPTIONS -fno-builtin)
# state.c includes the runtime's port_runtime.h (psxstack/types.h) and then the game's common.h, which defines the same
# s8..u64 again, outside psxstack's PSXSTACK_TYPES_H guard (upstream's header). Identical typedefs are valid C11 and
# GCC accepts them at -std=gnu99; clang (the Windows build's llvm-mingw) warns, which -Werror makes an error.
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/game/state.c" APPEND PROPERTY COMPILE_OPTIONS
                 -Wno-typedef-redefinition)
endif()
# The adapter's generated header (overlay_ids.h: the overlay IDs and the PS1 addresses it maps), quote-only as the
# game's include directories are for it.
set_property(SOURCE ${DCB_ADAPTER_SRCS} APPEND PROPERTY COMPILE_OPTIONS "-iquote" "${DCB_INPUTS}")
