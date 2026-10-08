# The PC port (docs/PORT.md; DECISIONS "A fork of ReGame-Labs/dcb_decomp, for a PC port on psxstack"): the game's C
# units, its adapter (port/game/: game.json, the probes and mods to come) and psxstack (the runtime, the Psy-Q shim,
# the build) linked into one host binary, build/port/dcb. Adapted from dw2003recomp's port/CMakeLists.txt
# (docs/THIRD_PARTY.md).
#
#   cmake -S port -B build/port -G Ninja && cmake --build build/port && build/port/dcb --max-frames 60
#
# psxstack is the submodule at ../psxstack (PSXSTACK_DIR; scripts/worktree_init.sh checks it out at the pin; a
# checkout of the stack under development is -DPSXSTACK_DIR=$PWD/../psxstack). port/tools/port_inputs.py writes the
# build inputs psxstack_add_game() takes (build/port/gen/inputs/: the units and the overlays, from mk/version/us.mk
# and config/us/) at configure time. The PS1 build (the Makefile) is untouched: its C sees none of this.
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

# ---- The build inputs (port/tools/port_inputs.py), regenerated when their sources change.
set(DCB_VERSION "us" CACHE STRING "The game version to port (mk/version/<version>.mk, config/<version>/)")
set(DCB_INPUTS "${CMAKE_BINARY_DIR}/gen/inputs")
execute_process(COMMAND "${PSXSTACK_PYTHON}" "${DCB_ROOT}/port/tools/port_inputs.py" --out "${DCB_INPUTS}"
                        --version "${DCB_VERSION}"
                RESULT_VARIABLE r OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT r EQUAL 0)
    message(FATAL_ERROR "port/tools/port_inputs.py failed:\n${out}${err}")
endif()
string(STRIP "${out}" out)
message(STATUS "${out}")
string(TOUPPER "${DCB_VERSION}" DCB_VERSION_UPPER)
file(GLOB DCB_CONFIG_DEPS "${DCB_ROOT}/config/${DCB_VERSION}/symbols*.txt")
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
    INCLUDE_ASM_GUARD INCLUDE_ASM_H
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include" "${DCB_ROOT}/include" "${DCB_ROOT}"
    ADAPTER ${DCB_ADAPTER_SRCS}
    DEFINES VERSION_${DCB_VERSION_UPPER} SKIP_ASM
    CONFIGURE_DEPENDS "${DCB_ROOT}/port/tools/port_inputs.py" "${DCB_ROOT}/mk/version/${DCB_VERSION}.mk"
                      ${DCB_CONFIG_DEPS})
