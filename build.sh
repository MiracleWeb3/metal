#!/bin/sh
# metal builds itself under the floor it asks of everyone else. Rule 3 applies here first:
# a plugin that tells you to turn -Werror on has no business compiling itself without it.
#
#   ./build.sh [output]        release build
#   METAL_SELFTEST=1 ./build.sh /tmp/split && /tmp/split --selftest
set -e

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out=${1:-${XDG_CACHE_HOME:-$HOME/.cache}/metal/split}
mkdir -p "$(dirname -- "$out")"

# The floor, and the debug mode the floor asks for.
set -- -std=c++20 -Wall -Wextra -Werror
if [ -n "$METAL_SELFTEST" ]; then
    set -- "$@" -DMETAL_SELFTEST -O1 -g -fsanitize=address,undefined -D_GLIBCXX_ASSERTIONS
else
    set -- "$@" -O2
fi

exec c++ "$@" -o "$out" "$here"/hooks/*.cpp
