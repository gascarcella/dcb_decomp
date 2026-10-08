#!/usr/bin/env bash
# The host task scheduler's test (docs/PORT.md "The task scheduler", "On the host"): builds tests/port/tasks_host_test.c
# with the game's src/main/system/task.c, the port's port/game/tasks.c (startup.s's glue) and psxstack's
# runtime/fiber.c (the submodule's), and runs it. No disc, no game data; gcc and Python 3 only.
#
#   scripts/tasks_test.sh            # PSXSTACK_DIR=/path/to/a/psxstack/checkout overrides the submodule
#   SANITIZE=0 scripts/tasks_test.sh # without AddressSanitizer and UBSan (on by default)
#   M32=1 scripts/tasks_test.sh      # at -m32 (gcc-multilib)
#
# Exit 0: every check passed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STACK="${PSXSTACK_DIR:-$ROOT/psxstack}"
OUT="$ROOT/build/tasks_test"
CC=${CC:-gcc}
PY="$ROOT/.venv/bin/python"
[[ -x "$PY" ]] || PY=python3
[[ -f "$STACK/runtime/fiber.c" ]] || { echo "tasks_test: $STACK/runtime/fiber.c is missing (scripts/worktree_init.sh)" >&2; exit 2; }
mkdir -p "$OUT/include"

ARCH=-m64
[[ "${M32:-0}" != 0 ]] && ARCH=-m32
FLAGS=("$ARCH" -std=gnu99 -O1 -g -fsigned-char -fwrapv -fno-strict-aliasing -DPC_PORT)
[[ "${SANITIZE:-1}" != 0 ]] && FLAGS+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
STACK_INC=(-I"$OUT/include" -I"$STACK/include" -I"$STACK/include/psxstack")
# The game's include path as the port gives it, for quoted includes only (the game's include/ has its own stdarg.h,
# which must not shadow the host's); -fno-builtin as the units have it (game.h declares libc's functions its own way).
GAME_INC=(-DVERSION_US -DSKIP_ASM -fno-builtin -iquote "$ROOT/port/include" -iquote "$ROOT/include" -iquote "$ROOT")

# fiber.c as the runtime compiles it; hooks.h needs the game's generated description.
"$PY" "$STACK/tools/game_gen.py" "$ROOT/port/game/game.json" --out "$OUT/include/psxstack_game_gen.h" > /dev/null
"$CC" "${FLAGS[@]}" -Wall -Wextra -Werror "${STACK_INC[@]}" -I"$STACK/runtime" -c "$STACK/runtime/fiber.c" -o "$OUT/fiber.o"
# The adapter unit as the port compiles it (-Wall -Wextra -Werror), the game's unit as the units are (-Wall; what the
# PS1 C does that -Wall dislikes stays a warning), the test strictly.
"$CC" "${FLAGS[@]}" -Wall -Wextra -Werror "${GAME_INC[@]}" "${STACK_INC[@]}" -c "$ROOT/port/game/tasks.c" -o "$OUT/tasks.o"
"$CC" "${FLAGS[@]}" -Wall -Wno-unused-variable -Wno-return-type "${GAME_INC[@]}" "${STACK_INC[@]}" \
      -c "$ROOT/src/main/system/task.c" -o "$OUT/task.o"
"$CC" "${FLAGS[@]}" -Wall -Wextra -Werror "${GAME_INC[@]}" "${STACK_INC[@]}" \
      -c "$ROOT/tests/port/tasks_host_test.c" -o "$OUT/tasks_host_test.o"
"$CC" "${FLAGS[@]}" "$OUT/tasks_host_test.o" "$OUT/tasks.o" "$OUT/task.o" "$OUT/fiber.o" -o "$OUT/tasks_host_test"
"$OUT/tasks_host_test"
