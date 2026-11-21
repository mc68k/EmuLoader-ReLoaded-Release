# 7z decoder (LZMA SDK)

The C decoder of 7-Zip's LZMA SDK by Igor Pavlov, version 26.06 (2026-11-06), taken
unchanged from https://github.com/ip7z/7zip (folder `C`). Only the files needed to read
.7z archives are here (no encoders); every one of them states in its header that it is
in the **public domain**.

EmuLoader ReLoaded uses it in `src/core/SevenZip.cpp` to read picture and icon archives
(`icons.7z`, `snap.7z`, ...).

Built with `Z7_PPMD_SUPPORT` and `Z7_EXTRACT_ONLY`, as the SDK's own 7zdec.
