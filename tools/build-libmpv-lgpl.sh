#!/bin/sh
# Builds the video player's library for Windows, mpv-2.dll, from source, under the LGPL: mpv
# built with -Dgpl=false and FFmpeg without --enable-gpl (its default), with the libraries they
# need linked into the one DLL. Cross-compiled from Linux with MinGW-w64.
#
#   tools/build-libmpv-lgpl.sh [work folder]      (default: mpvbuild beside the source folder)
# The work folder's path is written into the DLL (FFmpeg keeps its configure line), so for a
# release build use a neutral one, such as /build/mpvbuild; the files' paths in the DLL are
# rewritten to start with "mpvbuild" (-ffile-prefix-map).
#
# The sources (git tags; downloaded into <work folder>/src when they aren't there):
#   mpv       v0.35.1      https://github.com/mpv-player/mpv          LGPL 2.1 or later (-Dgpl=false)
#   FFmpeg    n5.1.4       https://github.com/FFmpeg/FFmpeg           LGPL 2.1 or later
#   libass    0.17.1       https://github.com/libass/libass           ISC
#   FreeType  VER-2-13-2   https://github.com/freetype/freetype       FreeType License (FTL)
#   FriBidi   v1.0.13      https://github.com/fribidi/fribidi         LGPL 2.1 or later
#   HarfBuzz  8.3.0        https://github.com/harfbuzz/harfbuzz       MIT ("Old MIT")
#   zlib      v1.3.1       https://github.com/madler/zlib             zlib License
#   shaderc   v2023.7      https://github.com/google/shaderc          Apache 2.0  } Direct3D 11 video
#     with glslang 48f9ed8, SPIRV-Tools 360d469, SPIRV-Headers 4183b26 }   output (gpu-api=d3d11)
#                          https://github.com/KhronosGroup            Apache 2.0 / BSD / MIT
#   SPIRV-Cross vulkan-sdk-1.3.268.0 https://github.com/KhronosGroup/SPIRV-Cross  Apache 2.0
# Needs: MinGW-w64 (x86_64-w64-mingw32-gcc, g++), meson, ninja, cmake, nasm, pkg-config, autotools, git.
# The result: <work folder>/out/mpv-2.dll, and the licenses in <work folder>/out/licenses.
set -e
W=${1:-$(cd "$(dirname "$0")/../.." && pwd)/mpvbuild}
SRC=$W/src
P=$W/prefix
B=$W/build
OUT=$W/out
HOST=x86_64-w64-mingw32
JOBS=$(nproc)
mkdir -p "$SRC" "$P" "$B" "$OUT/licenses"

fetch() { # repository tag folder
  [ -d "$SRC/$3" ] || git clone -q --depth 1 --branch "$2" "https://github.com/$1" "$SRC/$3"
}
fetch mpv-player/mpv v0.35.1 mpv
fetch FFmpeg/FFmpeg n5.1.4 FFmpeg
fetch libass/libass 0.17.1 libass
fetch freetype/freetype VER-2-13-2 freetype
fetch fribidi/fribidi v1.0.13 fribidi
fetch harfbuzz/harfbuzz 8.3.0 harfbuzz
fetch madler/zlib v1.3.1 zlib
fetch google/shaderc v2023.7 shaderc
fetch KhronosGroup/SPIRV-Cross vulkan-sdk-1.3.268.0 SPIRV-Cross
fetchcommit() { # repository commit folder (shaderc's parts, at the commits its DEPS file names)
  [ -d "$3/.git" ] && return
  mkdir -p "$3" && (cd "$3" && git init -q && git remote add origin "https://github.com/$1" &&
    git fetch -q --depth 1 origin "$2" && git checkout -q FETCH_HEAD)
}
fetchcommit KhronosGroup/glslang 48f9ed8b08be974f4e463ef38136c8f23513b2cf "$SRC/shaderc/third_party/glslang"
fetchcommit KhronosGroup/SPIRV-Tools 360d469b9eac54d6c6e20f609f9ec35e3a5380ad "$SRC/shaderc/third_party/spirv-tools"
fetchcommit KhronosGroup/SPIRV-Headers 4183b260f4cccae52a89efdfcdd43c4897989f42 "$SRC/shaderc/third_party/spirv-headers"

MAP="-ffile-prefix-map=$W=mpvbuild"
export PKG_CONFIG_LIBDIR="$P/lib/pkgconfig"
export PKG_CONFIG_PATH=
CROSS=$W/mingw.cross
cat > "$CROSS" <<EOF
[binaries]
c = '$HOST-gcc'
cpp = '$HOST-g++'
ar = '$HOST-ar'
strip = '$HOST-strip'
windres = '$HOST-windres'
pkg-config = 'pkg-config'
[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
[built-in options]
prefix = '$P'
libdir = 'lib'
default_library = 'static'
buildtype = 'release'
c_args = ['$MAP']
cpp_args = ['$MAP']
EOF
mesonbuild() { # source folder, options...
  name=$1; shift
  meson setup --reconfigure --cross-file "$CROSS" "$B/$name" "$SRC/$name" "$@" >/dev/null 2>&1 ||
    meson setup --cross-file "$CROSS" "$B/$name" "$SRC/$name" "$@"
  ninja -C "$B/$name" -j "$JOBS" install >/dev/null
}

# zlib (static)
if [ ! -f "$P/lib/libz.a" ]; then
  mkdir -p "$B/zlib" && cd "$B/zlib"
  cp -r "$SRC/zlib/." .
  make -f win32/Makefile.gcc PREFIX=$HOST- LOC="$MAP" BINARY_PATH="$P/bin" INCLUDE_PATH="$P/include" LIBRARY_PATH="$P/lib" -j "$JOBS" install >/dev/null
  mkdir -p "$P/lib/pkgconfig"
  printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\nName: zlib\nDescription: zlib\nVersion: 1.3.1\nLibs: -L${libdir} -lz\nCflags: -I${includedir}\n' "$P" > "$P/lib/pkgconfig/zlib.pc"
fi

[ -f "$P/lib/libfreetype.a" ] || mesonbuild freetype -Dbrotli=disabled -Dbzip2=disabled -Dharfbuzz=disabled -Dpng=disabled -Dzlib=system -Dtests=disabled
[ -f "$P/lib/libfribidi.a" ] || mesonbuild fribidi -Ddocs=false -Dbin=false -Dtests=false
[ -f "$P/lib/libharfbuzz.a" ] || mesonbuild harfbuzz -Dfreetype=enabled -Dglib=disabled -Dgobject=disabled -Dcairo=disabled -Dicu=disabled \
  -Dgraphite2=disabled -Dtests=disabled -Ddocs=disabled -Dutilities=disabled -Dintrospection=disabled

# libass (autotools; DirectWrite for the fonts on Windows)
if [ ! -f "$P/lib/libass.a" ]; then
  cd "$SRC/libass" && [ -f configure ] || ./autogen.sh >/dev/null
  mkdir -p "$B/libass" && cd "$B/libass"
  CFLAGS="-O2 $MAP" "$SRC/libass/configure" --host=$HOST --prefix="$P" --enable-static --disable-shared --disable-fontconfig --disable-require-system-font-provider >/dev/null
  make -j "$JOBS" install >/dev/null
fi

# shaderc and SPIRV-Cross (static), for mpv's Direct3D 11 output: it works on every Windows 10 and 11
# PC, with or without a graphics driver (OpenGL needs one)
TOOLCHAIN=$(cd "$(dirname "$0")" && pwd)/mingw-toolchain.cmake
# the C++ runtime they need, linked into the DLL (by its full path: Meson would take the DLL's for "-lstdc++")
STDCXX="$($HOST-g++ -print-file-name=libstdc++.a) $($HOST-g++ -print-file-name=libwinpthread.a)"
if [ ! -f "$P/lib/libshaderc_combined.a" ]; then
  cmake -S "$SRC/shaderc" -B "$B/shaderc" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="$MAP" -DCMAKE_CXX_FLAGS="$MAP" -DCMAKE_INSTALL_PREFIX="$P" -DSHADERC_SKIP_TESTS=ON -DSHADERC_SKIP_EXAMPLES=ON -DSHADERC_SKIP_COPYRIGHT_CHECK=ON \
    -DSPIRV_SKIP_EXECUTABLES=ON -DSPIRV_SKIP_TESTS=ON -DENABLE_GLSLANG_BINARIES=OFF -DSPIRV_TOOLS_BUILD_STATIC=ON \
    -DBUILD_SHARED_LIBS=OFF >/dev/null
  ninja -C "$B/shaderc" -j "$JOBS" install >/dev/null
fi
if [ ! -f "$P/lib/libspirv-cross-c.a" ]; then
  cmake -S "$SRC/SPIRV-Cross" -B "$B/SPIRV-Cross" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="$MAP" -DCMAKE_CXX_FLAGS="$MAP" -DCMAKE_INSTALL_PREFIX="$P" -DSPIRV_CROSS_SHARED=OFF -DSPIRV_CROSS_STATIC=ON -DSPIRV_CROSS_CLI=OFF \
    -DSPIRV_CROSS_ENABLE_TESTS=OFF -DSPIRV_CROSS_ENABLE_MSL=OFF -DSPIRV_CROSS_ENABLE_CPP=OFF -DSPIRV_CROSS_ENABLE_REFLECT=OFF \
    -DSPIRV_CROSS_ENABLE_UTIL=OFF >/dev/null
  ninja -C "$B/SPIRV-Cross" -j "$JOBS" install >/dev/null
fi
# what mpv asks pkg-config for: the static libraries (and for SPIRV-Cross, under the shared library's name)
printf 'prefix=%s\nName: shaderc\nDescription: shaderc (combined, static)\nVersion: 2023.7\nLibs: -L${prefix}/lib -lshaderc_combined %s\nCflags: -I${prefix}/include\n' \
  "$P" "$STDCXX" > "$P/lib/pkgconfig/shaderc.pc"
printf 'prefix=%s\nName: spirv-cross-c-shared\nDescription: SPIRV-Cross C API (static)\nVersion: 0.58.0\nLibs: -L${prefix}/lib -lspirv-cross-c -lspirv-cross-hlsl -lspirv-cross-glsl -lspirv-cross-core %s\nCflags: -I${prefix}/include/spirv_cross\n' \
  "$P" "$STDCXX" > "$P/lib/pkgconfig/spirv-cross-c-shared.pc"

# FFmpeg: LGPL (no --enable-gpl, no --enable-nonfree); decoding only: no programs, encoders but
# the picture ones mpv's screenshots use, no devices, no network
if [ ! -f "$P/lib/libavcodec.a" ]; then
  mkdir -p "$B/FFmpeg" && cd "$B/FFmpeg"
  "$SRC/FFmpeg/configure" --prefix="$P" --target-os=mingw32 --arch=x86_64 --cross-prefix=$HOST- --pkg-config=pkg-config --extra-cflags="$MAP" \
    --enable-static --disable-shared --disable-programs --disable-doc --disable-debug --disable-autodetect \
    --enable-w32threads --enable-zlib --disable-network --disable-indevs --disable-outdevs --disable-postproc \
    --disable-encoders --enable-encoder=png,mjpeg --disable-muxers --disable-hwaccels >/dev/null
  make -j "$JOBS" install >/dev/null
fi

# mpv: the library only, LGPL; the gpu video output with Direct3D 11 and OpenGL, WASAPI sound
mesonbuild mpv -Ddefault_library=shared -Dlibmpv=true -Dcplayer=false -Dgpl=false -Dbuild-date=false \
  -Dlua=disabled -Djavascript=disabled -Dlibarchive=disabled -Dlibbluray=disabled -Ddvdnav=disabled -Dcdda=disabled \
  -Duchardet=disabled -Drubberband=disabled -Dlcms2=disabled -Dlibplacebo=disabled -Dvulkan=disabled -Dshaderc=enabled \
  -Dspirv-cross=enabled -Dd3d11=enabled -Diconv=disabled -Dzimg=disabled -Djpeg=disabled -Dmanpage-build=disabled \
  -Dc_link_args='-static -static-libgcc -static-libstdc++' -Dcpp_link_args='-static -static-libgcc -static-libstdc++'
cp "$P/bin/libmpv-2.dll" "$OUT/mpv-2.dll"
$HOST-strip "$OUT/mpv-2.dll"

# their licenses, with the DLL
cp "$SRC/mpv/LICENSE.LGPL" "$OUT/licenses/mpv-LGPL-2.1.txt"
cp "$SRC/FFmpeg/COPYING.LGPLv2.1" "$OUT/licenses/FFmpeg-LGPL-2.1.txt"
cp "$SRC/libass/COPYING" "$OUT/licenses/libass-ISC.txt"
cp "$SRC/freetype/docs/FTL.TXT" "$OUT/licenses/FreeType-FTL.txt"
cp "$SRC/fribidi/COPYING" "$OUT/licenses/FriBidi-LGPL-2.1.txt"
cp "$SRC/harfbuzz/COPYING" "$OUT/licenses/HarfBuzz-MIT.txt"
cp "$SRC/zlib/LICENSE" "$OUT/licenses/zlib.txt"
cp "$SRC/shaderc/LICENSE" "$OUT/licenses/shaderc-Apache-2.0.txt"
cp "$SRC/shaderc/third_party/glslang/LICENSE.txt" "$OUT/licenses/glslang.txt"
cp "$SRC/shaderc/third_party/spirv-tools/LICENSE" "$OUT/licenses/SPIRV-Tools-Apache-2.0.txt"
cp "$SRC/SPIRV-Cross/LICENSE" "$OUT/licenses/SPIRV-Cross-Apache-2.0.txt"
echo "Made $OUT/mpv-2.dll"
