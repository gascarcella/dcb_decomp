#!/usr/bin/env bash
# The host GTE macros' test (port/include/gte.h; docs/PORT.md "The contract, for this game", the GTEMAC row): builds
# tests/port/gte_host_test.c against psxstack's software GTE (the submodule's psyq/gte.c, gte_shadow.c, libgte.c,
# compiled as psyq/check.sh compiles the shim) and runs it. No disc, no game data; gcc and Python 3 only.
#
#   scripts/gte_test.sh            # PSXSTACK_DIR=/path/to/a/psxstack/checkout overrides the submodule
#   SANITIZE=0 scripts/gte_test.sh # without AddressSanitizer and UBSan (on by default)
#
# Exit 0: every check passed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STACK="${PSXSTACK_DIR:-$ROOT/psxstack}"
OUT="$ROOT/build/gte_test"
CC=${CC:-gcc}
PY="$ROOT/.venv/bin/python"
[[ -x "$PY" ]] || PY=python3
[[ -f "$STACK/psyq/gte.c" ]] || { echo "gte_test: $STACK/psyq/gte.c is missing (scripts/worktree_init.sh)" >&2; exit 2; }
mkdir -p "$OUT/include"

FLAGS=(-m64 -std=gnu99 -O1 -g -fsigned-char -fwrapv -fno-strict-aliasing -DPC_PORT -Wall -Wextra -Werror)
[[ "${SANITIZE:-1}" != 0 ]] && FLAGS+=(-fsanitize=address,undefined -fno-sanitize-recover=all)

# The shim's GTE: psyq_internal.h reaches hooks.h, which includes the game's generated description.
"$PY" "$STACK/tools/game_gen.py" "$ROOT/port/game/game.json" --out "$OUT/include/psxstack_game_gen.h" > /dev/null
for f in gte gte_shadow libgte; do
    "$CC" "${FLAGS[@]}" -I"$OUT/include" -I"$STACK/psyq" -I"$STACK/include" -I"$STACK/include/psxstack" \
          -I"$STACK/runtime" -c "$STACK/psyq/$f.c" -o "$OUT/$f.o"
done
# The test sees the game's include path as the port gives it (port/include first), for its quoted includes only (the
# game's include/ has its own stdarg.h and string.h, which must not shadow the host's), the stack's LIBGTE types, and
# the hooks (common.h includes port.h, which includes psxstack/hooks.h and the generated description under PC_PORT).
"$CC" "${FLAGS[@]}" -DVERSION_US -DSKIP_ASM -iquote "$ROOT/port/include" -iquote "$ROOT/include" -I"$OUT/include" \
      -I"$STACK/include" -I"$STACK/include/psxstack" \
      -c "$ROOT/tests/port/gte_host_test.c" -o "$OUT/gte_host_test.o"
"$CC" "${FLAGS[@]}" "$OUT/gte_host_test.o" "$OUT/gte.o" "$OUT/gte_shadow.o" "$OUT/libgte.o" -o "$OUT/gte_host_test"
"$OUT/gte_host_test"
