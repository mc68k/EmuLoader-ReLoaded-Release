#!/bin/sh
# Build the Windows version from Linux and collect everything into one folder + zip.
#   tools/package-windows.sh [qt-for-windows-prefix]   (default /opt/qt6-win)
set -e
QT=${1:-/opt/qt6-win}
SRC=$(cd "$(dirname "$0")/.." && pwd)
B=$SRC/build-win
VERSION=$(sed -n 's/^set(EL_VERSION_LABEL "\(.*\)")/\1/p' "$SRC/CMakeLists.txt" | tr ' ' '-')
ZIP=EmuLoader-ReLoaded-$VERSION-windows.zip
OUT=$SRC/dist/EmuLoader-ReLoaded
# The build's name in crash.log: the git revision (+ "-modified" with changes not committed)
# (EL_REV=<name> gives it: tools/make-release.sh builds from a copy without git)
if [ -n "$EL_REV" ]; then REV=$EL_REV; else
  REV=$(cd "$SRC" && git rev-parse --short HEAD 2>/dev/null || echo unknown)
  [ -n "$(cd "$SRC" && git status --porcelain 2>/dev/null | grep -v '^??')" ] && REV="$REV-modified"
fi
# (-ffile-prefix-map: the program's line numbers name its files from "emuloader", not from this
# computer's folders)
cmake -S "$SRC" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG -g1 -ffile-prefix-map=$SRC=emuloader" -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG -ffile-prefix-map=$SRC=emuloader" -DEL_BUILD_REV="$REV" \
  -DCMAKE_TOOLCHAIN_FILE="$SRC/tools/mingw-toolchain.cmake" -DCMAKE_PREFIX_PATH="$QT" \
  -DQT_HOST_PATH=/usr -DQT_HOST_PATH_CMAKE_DIR=/usr/lib/x86_64-linux-gnu/cmake
cmake --build "$B" --target emuloader langcheck
rm -rf "$OUT"; mkdir -p "$OUT/platforms" "$OUT/styles" "$OUT/imageformats"
cp "$B/EmuLoader-ReLoaded.exe" "$B/langcheck.exe" "$OUT/"
# The debug copy (not shipped): tools/crash-lookup.sh reads a crash.log against it
mkdir -p "$SRC/dist/symbols/$REV"
cp "$B/EmuLoader-ReLoaded.exe" "$SRC/dist/symbols/$REV/"
# (the last five builds' only: each is about 300 MB)
(cd "$SRC/dist/symbols" && ls -t | tail -n +6 | xargs -r rm -rf)
# EmuLoader-ReLoaded.exe keeps its line numbers (-g1: 32 MB, 7 MB more in the zip), so any copy of
# it can explain its own crash.log (tools/crash-lookup.sh); the tools are stripped
x86_64-w64-mingw32-strip "$OUT/langcheck.exe"
for d in Qt6Core Qt6Gui Qt6Widgets; do cp "$QT/bin/$d.dll" "$OUT/"; done
cp "$QT/plugins/platforms/qwindows.dll" "$QT/plugins/platforms/qoffscreen.dll" "$OUT/platforms/"
cp "$QT"/plugins/styles/*.dll "$OUT/styles/" 2>/dev/null || true
cp "$QT"/plugins/imageformats/*.dll "$OUT/imageformats/" 2>/dev/null || true
# MinGW runtime
GCCDIR=$(dirname "$(x86_64-w64-mingw32-g++-posix -print-libgcc-file-name)")
for f in libstdc++-6.dll libgcc_s_seh-1.dll; do cp "$GCCDIR/$f" "$OUT/"; done
cp /usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll "$OUT/"
cp -r "$B/data_template" "$OUT/"
cp "$SRC/README.md" "$OUT/"
# the guide, with this build under its title
sed "s|<p class=\"build\">Build: dev</p>|<p class=\"build\">Build: $REV</p>|" "$SRC/docs/EmuLoader-ReLoaded-Guide.html" > "$OUT/EmuLoader-ReLoaded-Guide.html"
# Press Start 2P is built into EmuLoader-ReLoaded.exe (the splash screen's arcade lettering); its
# license (SIL Open Font License) goes with it
mkdir -p "$OUT/licenses"
cp "$SRC/resources/fonts/PressStart2P-OFL.txt" "$OUT/licenses/"
cp "$SRC/third_party/lzma/README.md" "$OUT/licenses/LZMA-SDK-7z-decoder.md" # (public domain)
# EmuLoader's own license, and those of Qt (LGPL 3, which adds to the GPL 3) and miniz (MIT)
cp "$SRC/LICENSE" "$OUT/licenses/EmuLoader-ReLoaded-License.txt"
cp "$SRC/third_party/qt/README.md" "$OUT/licenses/Qt.md"
cp "$SRC/third_party/qt/LGPL-3.0.txt" "$OUT/licenses/Qt-LGPL-3.0.txt"
cp "$SRC/third_party/qt/GPL-3.0.txt" "$OUT/licenses/Qt-GPL-3.0.txt"
cp "$SRC/third_party/miniz/LICENSE" "$OUT/licenses/miniz-MIT.txt"
# EmuLoader's own video player: mpv's library, an LGPL build made by tools/build-libmpv-lgpl.sh (not in
# git: 23 MB), with its parts' licenses beside it. MPV_DLL=<file>, else ../mpvbuild/out/mpv-2.dll.
MPV=${MPV_DLL:-$SRC/../mpvbuild/out/mpv-2.dll}
if [ -f "$MPV" ]; then
  cp "$MPV" "$OUT/mpv-2.dll"
  cp "$SRC/third_party/mpv/README.md" "$OUT/licenses/Video-player-mpv-and-FFmpeg.md"
  cp "$(dirname "$MPV")"/licenses/*.txt "$OUT/licenses/"
else
  echo "Note: no mpv-2.dll ($MPV): the package's own video player won't play"
fi
# Language files, and Qt's own translations (standard buttons, file and color dialogs)
cp -r "$SRC/lang" "$OUT/"
mkdir -p "$OUT/translations"
QTTR=${QT_TRANSLATIONS:-/usr/share/qt6/translations}
cp "$QTTR"/qtbase_*.qm "$OUT/translations/" 2>/dev/null || true
find "$OUT" -name '*.dll' -exec x86_64-w64-mingw32-strip {} \;
# Python's zipfile marks non-ASCII file names as UTF-8, which Windows reads correctly.
(cd "$SRC/dist" && rm -f "$ZIP" && python3 -m zipfile -c "$ZIP" EmuLoader-ReLoaded)
echo "Packaged: $SRC/dist/$ZIP (build $REV)"
