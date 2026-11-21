#!/bin/sh
# Records an edited copy of a decomp file as the port's patch for it.
# Usage: mkpatch.sh <path relative to native/decomp, e.g. src/x.cpp or include/y.h> <edited copy>
set -e
N=$(cd "$(dirname "$0")/.." && pwd)
case "$1" in
  include/*) P="$N/port/patches/include/${1#include/}.patch" ;;
  *) P="$N/port/patches/$1.patch" ;;
esac
mkdir -p "$(dirname "$P")"
diff -u --label "a/$1" --label "b/$1" "$N/decomp/$1" "$2" > "$P" || true
[ -s "$P" ] || { rm -f "$P"; echo "no change"; exit 0; }
echo "wrote $P"
