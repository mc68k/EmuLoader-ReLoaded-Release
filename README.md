# EmuLoader ReLoaded

*A community continuation of EmuLoader, originally created by Ciro Alfredo Consentino (1974–2021). Ported from his Emuloader v8.9.4.*

EmuLoader ReLoaded is a frontend for MAME and HBMAME: a games list with filters, pictures, videos and game
documents, from which you start your games. It looks and works like the original EmuLoader, and it can use an
existing EmuLoader folder as it is, with your games list, favorites, play history and settings.

It is written in C++ and Qt 6. It covers the MAME family only; the other emulators the original supports
(Demul, Supermodel, Model 2, ZiNc, Daphne and DICE) are left out.

> Make a backup copy of your EmuLoader folder before you try it.

## Download

Download the latest `EmuLoader-ReLoaded-<version>-windows.zip` from the
[Releases page](https://github.com/mc68k/EmuLoader-ReLoaded-Release/releases/latest).

**You need:**

- Windows 10 or Windows 11, 64-bit
- MAME or HBMAME (any build, including custom ones such as `mamearcade.exe`)

## Installing

### Using your existing EmuLoader folder

1. Make a backup copy of your EmuLoader folder.
2. Unzip the download, then copy the contents of its `EmuLoader-ReLoaded` folder into your EmuLoader folder.
   Nothing of the original is overwritten: the new program is `EmuLoader-ReLoaded.exe`, next to `EmuLoader.exe`.
3. Start `EmuLoader-ReLoaded.exe`.

The first time it starts, it copies your settings, favorites, play history, custom parameters, and picture and
video settings from the original, then makes the games list again from MAME, so that it matches your MAME
version. It keeps its own files in the `EmuLoaderRL` folder and never changes the original's: you can go on
using `EmuLoader.exe` as before.

### Starting fresh

1. Unzip the download anywhere you can write to (not under `Program Files`).
2. Start `EmuLoader-ReLoaded.exe`.
3. When it says that no emulator is set up yet, click **Set Up MAME...**, choose your `mame.exe`, then click **OK**.
4. Click **Create the Games List**. EmuLoader asks MAME for its list of machines and looks for your ROMs, which
   takes a minute or two.

### Windows warns about the program

The program isn't signed, so Windows SmartScreen may warn about it the first time. Click **More info**, then
**Run anyway**.

## Updating

EmuLoader checks once a week whether a new version has been released, and **Help > Check for Updates** checks
at any time. It only asks GitHub for the latest version number, and it never downloads or installs anything
itself: **View Release** opens the release's page. To update, close EmuLoader, then unzip the new version over
the old one. Your settings stay in the `EmuLoaderRL` folder.

The weekly check can be turned off in **Settings > General**.

## What's new compared with the original

- **One Settings window**, with a tab for each subject and a search box: type part of a setting's name to find it.
- **Pictures and icons in `.7z` archives** as well as `.zip` files, read in the background so the games list
  never waits for them.
- **Its own video player** for video previews, with sound.
- **Startup effects** for the splash screen, from a vector grid to an arcade high-score table.
- **MAME's software lists** and **multi-slot machines**, such as the Neo-Geo's cartridges.
- **Scan Games** in one window, with MAME's own full check of every ROM and CHD.
- **When a game doesn't start**, a window explains why, with MAME's own message.
- **Translations**: every text can be translated with a language file. English (US), English (UK) and an
  example French translation are included.
- **A diagnostic log** to help track down problems.

The user guide, `EmuLoader-ReLoaded-Guide.html` (**Help > EmuLoader ReLoaded User Guide**, or **F1**), describes
every feature and lists every difference from the original.

## Problems and questions

Report problems and ask questions on the
[Issues page](https://github.com/mc68k/EmuLoader-ReLoaded-Release/issues). To help track a problem down:

- **The user guide** has a troubleshooting section.
- **Settings > General > Enable Diagnostic Log** records what EmuLoader does in
  `EmuLoaderRL\diagnostic.log`. Include it when you report a problem.
- **If EmuLoader closes unexpectedly**, it writes what happened to `EmuLoaderRL\crash.log`. Include that too.

## Going back to the original

Start `EmuLoader.exe`: its files are as it left them. Changes made in one program don't show in the other.

## Source code

The source code of every version is on GitHub, at https://github.com/mc68k/EmuLoader-ReLoaded-Release, next
to its download: each release is tagged with its version (v1.0.0, ...). `BUILDING.md` there explains how to
build it.

## License and credits

EmuLoader ReLoaded keeps the terms Ciro released EmuLoader on: it's free of charge and open source, and it can be
copied and shared freely, but it can't be sold or used for commercial purposes. You may change it and share your
changed version, as long as it keeps the credits and these terms, says it's changed, and shares its source code.
The full terms are in `licenses\EmuLoader-ReLoaded-License.txt` in the download. The `licenses` folder also has
the licenses of the parts below.

- **EmuLoader** by Ciro Alfredo Consentino (1974–2021), whose program and files this version continues.
- **Qt** by The Qt Company and contributors, used under the GNU Lesser General Public License, version 3.
- **LZMA SDK** (the `.7z` reader) by Igor Pavlov, in the public domain.
- **miniz** (the `.zip` reader and writer) by Rich Geldreich and contributors, under the MIT License.
- **mpv** and **FFmpeg** (the video player, `mpv-2.dll`), under the GNU Lesser General Public License, with
  libass, FreeType, FriBidi, HarfBuzz, zlib, shaderc and SPIRV-Cross, each under its own license.
- **Press Start 2P** (the splash screen's arcade lettering) by CodeMan38, under the SIL Open Font License 1.1.

MAME is a registered trademark of Gregory Ember. EmuLoader ReLoaded isn't affiliated with the MAME team.
