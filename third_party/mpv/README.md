# The video player: mpv (libmpv) and FFmpeg

EmuLoader's own video player uses the mpv media player's library, `mpv-2.dll` in EmuLoader's folder. EmuLoader
loads it when it first plays a video; without it, EmuLoader works as before, and videos can be played with
another media player (Settings > Video > Advanced).

The included `mpv-2.dll` is an LGPL build, made from these sources without changes by
`tools/build-libmpv-lgpl.sh` in EmuLoader ReLoaded's source code:

| Part | Version | Source | License |
|------|---------|--------|---------|
| mpv (built with `-Dgpl=false`) | 0.35.1 | https://github.com/mpv-player/mpv/tree/v0.35.1 | LGPL 2.1 or later (`mpv-LGPL-2.1.txt`) |
| FFmpeg (built without `--enable-gpl`) | 5.1.4 | https://github.com/FFmpeg/FFmpeg/tree/n5.1.4 | LGPL 2.1 or later (`FFmpeg-LGPL-2.1.txt`) |
| libass | 0.17.1 | https://github.com/libass/libass/tree/0.17.1 | ISC (`libass-ISC.txt`) |
| FreeType | 2.13.2 | https://github.com/freetype/freetype/tree/VER-2-13-2 | FreeType License (`FreeType-FTL.txt`) |
| FriBidi | 1.0.13 | https://github.com/fribidi/fribidi/tree/v1.0.13 | LGPL 2.1 or later (`FriBidi-LGPL-2.1.txt`) |
| HarfBuzz | 8.3.0 | https://github.com/harfbuzz/harfbuzz/tree/8.3.0 | MIT (`HarfBuzz-MIT.txt`) |
| zlib | 1.3.1 | https://github.com/madler/zlib/tree/v1.3.1 | zlib License (`zlib.txt`) |
| shaderc (for the Direct3D 11 output) | 2023.7 | https://github.com/google/shaderc/tree/v2023.7 | Apache 2.0 (`shaderc-Apache-2.0.txt`) |
| glslang (part of shaderc) | 48f9ed8 | https://github.com/KhronosGroup/glslang/tree/48f9ed8b08be974f4e463ef38136c8f23513b2cf | several permissive licenses: BSD, MIT, Apache 2.0 and others (`glslang.txt`) |
| SPIRV-Tools (part of shaderc) | 360d469 | https://github.com/KhronosGroup/SPIRV-Tools/tree/360d469b9eac54d6c6e20f609f9ec35e3a5380ad | Apache 2.0 (`SPIRV-Tools-Apache-2.0.txt`) |
| SPIRV-Cross (for the Direct3D 11 output) | 1.3.268.0 | https://github.com/KhronosGroup/SPIRV-Cross/tree/vulkan-sdk-1.3.268.0 | Apache 2.0 (`SPIRV-Cross-Apache-2.0.txt`) |

Portions of this software are copyright © The FreeType Project (https://freetype.org). All rights reserved.

It draws the video with Direct3D 11 (every Windows 10 and 11 PC has it, with or without a graphics driver), or
OpenGL. When there's no sound device, the video plays without sound.

The library is a separate file, and it can be replaced: with a build of your own from the sources above, or with
another libmpv build (named `mpv-2.dll`, `libmpv-2.dll` or `mpv.dll`).
