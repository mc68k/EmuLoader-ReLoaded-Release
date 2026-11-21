#!/bin/sh
# crash.log -> functions, source files and lines. EmuLoader-ReLoaded.exe carries its own line
# numbers, so the folder of the program that crashed (the one the crash.log came with) is enough;
# without it, the build's copy that tools/package-windows.sh keeps in dist/symbols/<build>.
#   tools/crash-lookup.sh crash.log [folder of the program that crashed]
set -e
LOG=$1
SRC=$(cd "$(dirname "$0")/.." && pwd)
REV=$(sed -n 's/.*(build \([^)]*\)).*/\1/p' "$LOG" | head -1)
DIR=${2:-$SRC/dist/symbols/$REV}
[ -d "$DIR" ] || { echo "No program for build $REV in $DIR: give the folder of the program that crashed"; exit 1; }
head -3 "$LOG"
grep -E '^ +#' "$LOG" | while read -r n frame; do
  mod=${frame%%+*}; off=${frame##*+}
  if [ -f "$DIR/$mod" ]; then
    base=$(x86_64-w64-mingw32-objdump -p "$DIR/$mod" | awk '/^ImageBase/ {print $2}')
    addr=$(printf '0x%x' $((0x$base + off - 1))) # the call, not the return address
    echo "$n $mod $(x86_64-w64-mingw32-addr2line -e "$DIR/$mod" -f -C -p "$addr" | sed "s|$SRC/||")"
  else
    echo "$n $frame"
  fi
done
