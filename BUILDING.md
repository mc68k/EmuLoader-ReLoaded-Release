# EmuLoader ReLoaded 1.0.0 — C++/Qt port (MAME family)

*A community continuation of EmuLoader, originally created by Ciro Alfredo Consentino (1974–2021). Ported from his Emuloader v8.9.4.*

Written in C++17 and Qt 6, scoped to MAME and HBMAME. It runs on Windows, Linux and macOS, and it reads and
writes the same files as the Delphi EmuLoader, so an existing installation
can be used directly:

```
EmuLoader-ReLoaded.exe --data "C:\Games\EmuLoader"
```

## Windows quick start (prebuilt zip)

1. Unzip `EmuLoader-ReLoaded-1.0.0-windows.zip` anywhere (not under Program Files).
2. To use your existing EmuLoader setup, either copy the *contents* of the
   `EmuLoader-ReLoaded` folder into your EmuLoader folder (it will not overwrite
   `EmuLoader.exe`; the new program is `EmuLoader-ReLoaded.exe`), or run it with
   `--data "C:\path\to\your\EmuLoader"`.
3. To start fresh, just run `EmuLoader-ReLoaded.exe`, then Menu Options (the orb) >
   MAME/Arcade > Emulators Setup, Select `mame.exe`, Ok, and let it create the list.
4. Coming from an earlier beta: the program is now `EmuLoader-ReLoaded.exe` (it was
   `EmuLoaderRL.exe`, and before that `EmuLoaderQt.exe`). Delete the old `.exe` and its
   `-Guide.html`, and point any shortcut to the new name; your settings carry over (they stay
   in the `EmuLoaderRL` folder).

It keeps its own files in the `EmuLoaderRL` folder of your EmuLoader folder and
never writes to the original's. The first time it starts, it copies your
settings, favorites, play history, custom parameters and image and video
settings from the original, then makes the games list again from MAME. MAME's
own files (`mame.ini`, `ui.ini`, the `ini` folder) are shared, as they belong to
MAME. In a folder of its own, the first start asks once for the
folder of the original or of an earlier copy of EmuLoader ReLoaded to copy from
(its whole `EmuLoaderRL` folder, then), or Skip. Make a backup copy of your EmuLoader folder anyway, since this is an early
version.

This is a work in progress. See **Porting status** below. The user guide,
`EmuLoader-ReLoaded-Guide.html` (also Help > EmuLoader ReLoaded User Guide, or F1), covers
features, operation and the differences from the original.

## How the port works

The original is about 256,000 lines of Pascal and 119 forms. Retyping the
windows by hand would take months and drift from the original, so the port
converts what can be converted mechanically and hand-ports the logic.

1. **Forms are converted, not redrawn.** `tools/dfm2ui.py` turns every Delphi
   `.dfm` into a Qt Designer `.ui` (`forms/`). Every control keeps its exact
   position, size, caption, hint, font and color. Delphi-only behavior
   (Align, Anchors, gradient panel styles, initial visibility, event handler
   names) travels along as `vcl*` properties. Menus are written to
   `<Form>.menus.json` with their original captions, order and names.

2. **A small runtime makes them behave like VCL** (`src/ui/`).
   `vcl::apply()` runs after `setupUi()` and reproduces Align and Anchors on
   resize, pixel font sizes, colors, grouped speed buttons and float sliders.
   It also formats "value labels" from their hints, as the Delphi OnChange
   handlers do. `ElWidgets` draws TPanelEx's gradient styles (ported from
   `PanelEx.pas`), TShadowLabel's shadow, splitters, shapes and bevels.

3. **MAME option handling is data, not code.** `tools/extract_mame_options.py`
   reads `uMAMESettings.pas` and generates `src/forms/MameOptionMap.inc`: 257
   option groups mapped to the control that edits them. Each entry records
   MAME's aliases, the control kind and the value list or precision. One
   generic reader/writer replaces about 3,600 lines of if/else. The roughly 40
   options with custom logic (screens, BIOS, language, plugins, ...) are
   hand-ported in `MameSettingsExtras.cpp`. The script also generates
   `src/forms/MameOptionEnable.inc` from `EnableSettingsIniMAME`: controls that
   start grayed out and are enabled only when the user's mame.ini has the option,
   so the window follows the MAME version in use. `settingsview` (a development
   tool) opens the window on a data folder, changes controls and saves, for testing.

4. **Core logic is plain C++/Qt Core** (`src/core/`):
   - `ElDatabase` reads and writes `arcade\games\mame.el` and `.elstatus`, the
     format in `docs/arcade/el-listformat.txt`. It builds them from
     `mame -listxml` plus a quick ROM scan.
   - `MameIni` keeps the layout of MAME `.ini` files, and `MameIniChain` ports
     the 13-level per-machine chain (`GetCustomIniFileMAME`).
   - `WinIni` handles `EmuLoader.ini` (like TMemIniFile).
   - `Frontend` handles the data folder, emulator path and version, ini
     folder and ROM folders.

## Building

You need Qt 6 (Widgets; UiTools is optional and only used by `formview`) and
CMake 3.19 or newer.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
build/EmuLoader-ReLoaded  # the program (EmuLoader-ReLoaded.exe on Windows)
```

* **Windows:** install Qt 6 with the online installer (MSVC or MinGW kit).
  Open `CMakeLists.txt` in Qt Creator, or run the commands above from a Qt
  command prompt. Run `windeployqt build\EmuLoader-ReLoaded.exe` to collect the Qt
  DLLs.
* **Cross-compiling from Linux:** `tools/package-windows.sh /path/to/qt6-for-mingw`
  builds the Windows version and produces `dist/EmuLoader-ReLoaded-1.0.0-windows.zip`.
* **Linux:** `apt install qt6-base-dev qt6-tools-dev cmake g++`.

On first run, the program copies `data_template/` (EmuLoader's icons, images
and filter files) into its data folder.

## Data folder

Pass `--data <folder>`, or put the program in the folder. The data folder (an
EmuLoader folder) is only read; this version's own files are in `EmuLoaderRL\`
in it, with the Delphi version's layout (`src/core/Frontend.h`):

| File | Contents |
|---|---|
| `EmuLoaderRL\EmuLoaderRL.ini` | settings: the Delphi version's `EmuLoader.ini`, copied the first time; this version's own in `[EmuLoaderRL]` |
| `EmuLoaderRL\arcade\emulators.ini` | `[MAME] emu_FileName`, `emu_Build`, … |
| `EmuLoaderRL\arcade\games\mame.el`, `mame.elstatus` | games list and have/miss status, made from MAME (never copied) |
| `EmuLoaderRL\favorites\`, `ini_files\`, `el_extras.ini`, `arcade\played_games\`, `arcade\custom_params\` … | copied from the Delphi version the first time |
| `resources\…`, `docs\…`, `arcade\filters\…` | icons and images, documents, catver.ini … (the data folder's, only read) |
| `lang\<code>.ini` | language files (also next to the program) |

## Porting status

**Working**

- Main window, converted from `uMain.dfm`. It has the original toolbar pictures and dropdown menus, the search box, the games list, the status bar and the snapshot panel.
- Tool bar: the captions the original draws into the pictures (in English) are shown as text under them instead, translated and sharp at any display scaling (pictures from the 128×128 frames at 150–200%). Buttons widen to fit their caption (two lines past one and a half pictures); buttons that don't fit go into a » menu. Icon Size (Extra Large 68×68, Large 48×48, Small 30×24) works from Menu Options > Games List > Tool Bar Buttons; Large is the default, as in the original.
- Games list with EmuLoader's icons (parent, clone, BIOS and device, colored for available and gray for missing) and red text for sets with missing ROMs. Columns can be sorted.
- Filters: Have/Miss (all 6 modes), Parent/Clone, Driver Status (the Driver Status Filter window: driver, emulation, color, sound or graphics status; `[ListFilter] DriverStatusCategory`), Favorites, title search, Games Filters (the "All Games" button: `arcade\filters\mame_filters.ini`, by driver and BIOS, without the driver's folder so they work with MAME 0.2xx) and the Miscellaneous Filters (Misc button: ROMs, BIOS, devices, audio, controls, orientation, save state, cocktail, artwork, CHD, no dump, merged, CRC32 collisions, multi-slot machines, catver.ini categories; `ini_files\games_filters.ini` [Miscellaneous]; the ROM flags are worked out from `-listxml` when the list is made and kept as `<romflags>` in the .el) (`src/core/GameFilters.cpp`, `src/app/MiscFiltersDialog.cpp`, `src/app/GamesFiltersDialog.cpp`). Color, sound, graphics, protection and local network come from MAME's `<feature>` lines (MAME 0.190 and later) or the older `<driver>` attributes, and the games list saves them with the cocktail status as the original does (`clrstat`, `sndstat`, `grpstat`, `protectionstat`, `cocktailstat`, `lanstat`). The toolbar pictures and captions change to show the active filter.
- Run Game (double-click, Enter or the menu). MAME starts from its own folder, and its exit code is reported.
- Run Game (Extra) (`FormArcadeRunGameExtraMAME`): play back or record an input file, load a save state (autosave), insert memory cards (the machines with slots are written to `arcade\games\mame_memcard_support.ini` with the games list; last used cards in `arcade\memcard_lastused\mame\`) and record a movie (AVI/MNG/WAV), with the file lists (F2 rename, Delete to the Recycle Bin). Folders come from `mame.ini`; options are compared with the game's whole ini chain (`src/app/RunGameExtraDialog.cpp`).
- Custom Parameters (games list menu): Set / Delete Game Parameters and Parent Game Parameters, in the original's `arcade\custom_params\mame\<name>.txt` (`custom_params=`), used when the game (or a clone without its own) runs; the window shows samples and MAME's `-listslots` / `-listmedia` for the machine (`src/core/RunExtras.cpp`, `src/app/CustomParametersDialog.cpp`).
- Create Arcade Games List (`-listxml`, with a progress window: step, "Found N machines", elapsed time, Cancel keeps the current list; a quick scan of the ROM folders), Reload ROM folders.
- Scan Games (Audit ROMs)... (`src/core/RomAudit.cpp`, `src/app/MainWindowAudit.cpp`, `ScanGamesDialog.cpp`, `ScanResultsDialog.cpp`): one window, made from `FormArcadeScanGamesMode`, in place of the original's Select Scan Mode and Scan Games submenu: which games (all, available, missing, incomplete, selected, with their counts) and how (Full Scan with CHDs and Audio Samples, Quick Scan, Mark Games Available; Scan MAME Sets for software list games), and every scan says what it did. View Missing ROMs/CHDs (`FormArcadeScanGamesResults`, all games or the selected one). The Full Scan has MAME check the sets (`-verifyroms <names...>` in batches, `-verifysoftlist <list>`, `-verifysamples`) instead of the original's own CRC check (`.elrom`, `.miss`), so merged sets are checked too; it asks before updating the games list, and keeps the results in `arcade\games\mame_audit.txt`.
- Emulator Settings dialog, converted from `uMAMESettings.dfm`. All 8 pages work: every option the original reads is read and written, including per-screen settings, the BIOS list (`mame_biossets.ini`, written when the list is created), languages, LUA plugins (`plugin.ini` / `plugin`), controller definitions, snapshot size, debugger font, audio latency (frames from MAME 0.278) and `skip_warnings` (`ui.ini`). The browse, reset and help buttons and the BGFX shader chain list work too. Controls for options your MAME doesn't have stay grayed out, as in the original.
- Custom settings for every chain level: game, driver, horizontal, vertical, debug, raster, vector, LCD, SVG, arcade, console, computer and othersys. You can set or delete each one. Files hold only the values that differ, with an optional check against all previous files ("Validate All Custom Files").
- Image panel: the single image and the 26 multi-image layouts (dual, triple, quad) with resizable splitters, the layout selector and Layout Settings (categories per panel, panels 2/3 on or off, hidden layouts, console/computer categories), all in `screenshot_layouts.ini`; all 17 categories (prev/next, category selector), several images per game (EmuLoader's `name0000.png` and MAME's `name\0000.png` naming, Ctrl + mouse wheel; the wheel alone changes category), folders from MAME's `ui.ini` / `mame.ini`, images in `<category>.zip`, the parent's image for clones, the artwork-to-snapshot fallback, the "not available" pictures, per-category background colors, the hint box (Preferences page 2) and the image menu (Stretch, Stretch Larger Only, Display Parent, Disable Aspect Ratio, Single Background Color, Go To First Image, Show Tool Bar).
- Game Details (games list menu): the game's information and its set files (set, parent, BIOS, devices, CHDs) found or missing, and every ROM and disk with CRC32, SHA-1, size and status. The ROMs come from `mame -listxml <name>` when the window opens (the original reads its `.elrom` file); ROMs are checked by CRC in `.zip` files (`src/core/MachineInfo.cpp`, `src/app/GameDetailsDialog.cpp`).
- Arcade Emulators Setup (`FormArcadeEmulatorsSetup`): the emulator for MAME and HBMAME with its version read from `-version` and written as the original does ("MAME v0.289 (Aug 03 2026)", `emu_Build`, `emu_DateTime` in `arcade\emulators.ini`), Reset System, Clear System, Default Settings (opens Emulator Settings), the MAME site links, and `mame.ini` / `ui.ini` created with `-createconfig` when missing. AlterMAME 1/2 is not carried over: an older MAME build's ROM sets no longer match today's games list (its keys in `emulators.ini` are left as they are).
- Play statistics, in the original's `arcade\played_games\mame.txt` (`hbmame.txt`), so both programs keep the same totals: every play is counted when MAME exits (times played, last played, total playtime), shown in the Played / Last Played / Playtime columns and Game Details. The Last Played tool bar button opens the Top 25 window (Select Game, Select Game and Exit); the games list menu's Reset Selected Games Info clears the playtime, the date, the count or the whole line; Preferences can hide the seconds (`src/core/PlayedGames.cpp`, `src/app/LastPlayedDialog.cpp`).
- Game documents (F3, or Show MAME Game Documents in the games list menu): mameinfo.dat / messinfo.dat, history.xml (or history.dat), story.dat, scores3.htm (or marp.dat) and gameinit.dat from ui.ini's historypath, in the order and display mode of Preferences (single panel above / below / beside the pictures with a splitter, or Images / Docs tabs), with the right-click menu (font, position, Go To, Disable) and the colors of Preferences page 2. Settings in `el_extras.ini` and `lightmode.ini`, as in the original (`src/core/GameDocs.cpp`, `src/app/MainWindowDocs.cpp`).
- Image tools: Rename / Delete Image (image menu; deleted files go to the Recycle Bin), Show Layout Dimensions, Category Settings (folders per system written to MAME's `ui.ini` / `mame.ini` or `sysimagefolders_arcade.ini`, categories shown or hidden, background colors, in `image_category.ini`), Delete Images of Clone Games (with Rename to Parent) and the MAME Images Manager (missing images, images of missing games, invalid images, Scan All Categories; rename, move, delete, also inside zip files: changes written to a new zip that replaces the old one once checked), settings in `el_extras.ini` as the original (`src/core/ImageFinder.cpp`, `src/app/ImageDeleteRenameDialog.cpp`, `ImagesDeleteClonesDialog.cpp`, `CategorySettingsDialog.cpp`, `ImagesManagerDialog.cpp`, `src/core/ImageIndex.cpp`, `ZipArchive.cpp`).
- Video previews: the selected game's video played with your media player (Shift+V, the image tool bar button, the games list menu, or AutoPlay), found in the folder of Video Preview Settings, `videosnaps` folders or the snapshot folders, with the parent's video and the dummy video that stops playback, in `ini_files\videopreview.ini` (`src/core/VideoPreview.cpp`, `src/app/VideoPreviewSettingsDialog.cpp`).
- Help menu: web links, the original's readme/FAQ/quick setup guide and this port's user guide.
- Find a Setting (Menu Options, Ctrl+Shift+F; also a box in Preferences): searches Preferences, MAME Settings and the menus by name, tooltip and description, and "Take me there" opens the page with the setting highlighted, or the menu with the item selected. The entries are collected from the windows' controls and the menus at run time (`src/app/SettingsSearch.cpp`), so new options are found without a list to keep up.
- Languages: every text can be translated (`lang\<code>.ini`, chosen on first start or in Preferences > General); a French example and British English (`en_GB.ini`) are included. See **Languages** below.
- Favorites use the original's `favorites\favorites.txt` (or the `[Preferences] FavoriteFile` profile), so both programs share them. The Favorites Manager (Favorites button menu > More...) lists the profiles: New, Clear Games, Cleanse, Replicate, Delete, edit title (F2) or file name (F3), set active (`src/core/Favorites.cpp`, `src/app/FavoritesManagerDialog.cpp`). Last selected game, filters and window size are remembered.
- The splash screen (TFormStatus): at startup and for long jobs (creating the games list, scanning files), with the original's artwork, texts, timer, count and progress bar color schemes, and Customize Splash Screen (`ini_files\splash.ini` [Splash], the original's keys). New: startup effects (vector grid, marquee light-up, card flip, starfield warp, high scores, signal glitch, or Random) with an optional blinking INSERT COIN, drawn with Qt while the games list is read on a second thread (`src/app/SplashView.cpp`, `SplashEffects.cpp`, `SplashScreen.cpp`, `SplashCustomizeDialog.cpp`). The arcade lettering is Press Start 2P (SIL Open Font License, `resources/fonts/`), built into the program.
- Update check (`src/core/UpdateCheck.cpp`, `src/app/UpdateFetch.cpp`, `src/app/MainWindowUpdates.cpp`): Help > Check for Updates, and once a week a few seconds after the start (Settings > General > Check for Updates Once a Week, `[EmuLoaderRL] CheckForUpdates`, `LastUpdateCheck`, `SkipUpdateVersion`), asks GitHub for the latest release of mc68k/EmuLoader-ReLoaded-Release and offers its page when it's newer (View Release, Skip This Version, Remind Me Later, Turn Off); nothing is downloaded. WinHTTP on Windows (no Qt Network to ship), Qt Network elsewhere; `EMULOADER_UPDATE_URL` points it at a file.
- Arcade ROMs Folders (each system's `rompath`), Arcade Files Versions (the emulator's version and the games list's, in red when they differ) and Export MAME and Arcade Games List (Full Format, also as Excel CSV; Short Format `gamename=system`; MAME Content Manager Plus; all, visible or selected games; `el_extras.ini` [ExportArcadeGames]) (`src/app/RomsFoldersDialog.cpp`, `FileVersionsDialog.cpp`, `ExportGamesDialog.cpp`).
- Scan MAME/HBMAME Missing Audio Samples (the samples sets from `-listxml` kept in `arcade\games\mame.elsamples`; `src/app/ScanSamplesDialog.cpp`), View Game Manual (PDF) (`manuals\<set>.pdf` in MAME's folder, `manuals_directory` in `sysimagefolders_arcade.ini`, set in Preferences), Number of Players from `nplayers.ini` (with Show Players Info From nplayers.ini Only) and Language from `languages.ini` (column, search, Game Details), the search box's Use Small Icons, Show Filter Labels in Small Tool Bar and Help, and Use Alternate Frontend Icons (`resources\main_icons\alternate`).
- Customize Columns and the columns header font (`src/app/ColumnsEditorDialog.cpp`); the view modes: Grouped (parents with their clones under a title band), Small Icons, Large Icons, Tiles and Thumbnails (`src/app/GamesIconView.cpp`, sharing the details list's model and selection), and Thumbnail Settings (`ThumbnailSettingsDialog.cpp`, `games_filters.ini` [Thumbnails]).
- Night Mode (`src/app/NightMode.cpp`: the night profile `nightmode\<profile>.ini`, the original's keys and defaults; the program drawn dark through the Fusion style's palette and the forms' colors turned around in `VclRuntime`) and Customize Night Mode Colors (`NightModeDialog.cpp`, with profiles).
- MAMu_ icons (`src/app/MamuIcons.cpp`): loose `.ico` files and `icons.zip` (`src/core/ZipArchive.cpp`), the parent/clone/device/BIOS/driver options, the folder in Preferences (with the progetto-SNAPS link). The MAMu_ Icons Manager is the Images Manager's Icons category: games with and without an icon, unused and invalid icons (driver icons and `deviceset.ico` aren't), the excluded list (`arcade\el_mamu_exclude.ini`), Create / Edit Icon.
- Smaller menu items: the Special List (`ini_files\special.txt`, with the Misc filter), Select Game at Startup (`[SelectGameStartup]`), Alternate Title Format and Game Title Extra Info, Incremental Search by title or set name, Show Tool Bar / Bound to Games Panel / Show Games Status Bar, the Restore Default and Reset Columns items, the Run Game Confirmation Dialog, Show Favorite Icon in Games, the image Resampler (Off, Linear, Cosine, Hermite; `src/app/ImageResample.cpp`) and the splitter bar's own menu.
- Delete Selected Games Files, Copy/Move Selected Games Files to Folder: one window for one or many games, with the ROM set, CHDs and config/NVRAM files of each, checked by type; deleted files go to the Recycle Bin; CHDs are copied into a folder named after their game (`src/core/GameFiles.cpp`, `src/app/GameFilesDialog.cpp`).
- Customize Tool Bar (`FormToolBarEditor`, `src/app/CustomizeToolBarDialog.cpp`): each button shown or not, the search box too (`[ToolBar] GamesFilters_Buttons`), Bound To Games Panel, Show Tool Bar, the icon size, All Visible; also a right-click on the tool bar.
- The search bar's Controls filter: the control types of `arcade\filters\mame_control_type.ini`; one picked shows the games with it (Include Tool Bar Filters: `[ListFilter] SearchBarControlsIncludeMainFilters`).
- Internet Game Info (`src/core/GameInfoPage.cpp`): the images tool bar's globe and the games list menu open the game's page in the browser: by default a page made from your files (`EmuLoaderRL\gameinfo\<name>.html`: pictures, details, clones, game documents; made again each time), else the website in Preferences (ProgettoEMMA / Arcade Italia, `%s` the set and the list). The original's built-in browser panel isn't ported.
- Preferences: Disable Clone Indent, Multiline Captions (games list and Machines List panel, `GamesListDelegate::twoLineCaption`), Disable "Delete/Copy/Move Games Files" (the three items), Left Align Emulators Info and Games (the Run Game Confirmation Dialog), Enable Diagnostic Log (`EmuLoaderRL\diagnostic.log`, `--debug-log`, 5 MB then `.1`; `src/core/DiagnosticLog.cpp`). Fix RetroArch Image File Names is hidden: it was for the original's non-MAME console games.
- MAME Settings > RAM Size: `arcade\games\mame_ramsize.ini` written with the games list from `<ramoption>` (the original's format), the machine's choices in its custom settings.
- Multi-slot machines (`src/core/MultiSlot.cpp`, `src/app/MultiSlotDialog.cpp`): with several Neo-Geo MVS, ST-V, Mega-Tech or PlayChoice-10 games selected, Run Game opens Multi-Slot Machines (the slots' order, the Neo-Geo board from `arcade\mame_multislot_machines.txt`, the last one kept) and runs `<machine> -cart1 <game> -cart2 <game> ...`; the Neo-Geo and ST-V cartridge lists (`arcade\games\mame_multicart_neogeo.ini`, `_stv.ini`) are made from MAME's hash folder with the games list, and the Show Only Neo-Geo MVS / ST-V Multi-Slot filters use them.
- Console and computer machines (MAME): off by default. Preferences > General > "Enable Console and Computer Support" ([EmuLoaderRL] ConsoleComputerSupport) lists them and shows the Machines tool bar button (arcade / machines with software lists / without, saved in `ini_files\games_filters.ini` [MachineType]) and the console.ini / computer.ini / othersys.ini custom settings. Machines are recognized as in the original: by their software lists (`arcade\games\mame.elsoftlist`, written when the list is created) and AntoPISA's `arcade\filters\mess.ini`.
- MAME software lists (`src/core/SoftwareLists.cpp`, `src/app/MainWindowSoftware.cpp`): Create MAME Software Games List (Update / Overwrite, also run by Create Arcade Games List unless turned off) reads MAME's hash folder into the original's `arcade\mame_softwarelist_games\<list>.el` / `.elstatus` and `arcade\games\mame.elsoftlistreq`; the games are listed with the machines (console/computer support, Machines button > MAME Software List Games), scanned in `roms\<list>\`, run as `mame <machine> [requirement] -<media> <game>` with the machine kept per list (`arcade\mame_machines_usercustom.ini`) and chosen in Select a Machine to Run the Game With (`SelectMachineDialog.cpp`) when there are several; the software list and machine custom parameters; Game Details from the hash file; favorites and play statistics as `nes\smb`; pictures in `snap\<list>\`; history.xml's software entries; the vgmplay filter and the export option; their pictures (`snap\<list>\<name>.png`, `<list>.zip`, `<list>\<list>.zip`, `snap_sl.zip`) in the main window, the Images Manager (Scan Software List Games) and Delete Images of Clone Games. The Machines List panel (`MainWindowMachines.cpp`: Enable MAME Machines Filter Panel; a machine's software list games listed and run with it; its filters, disabled machines, icon size and columns in `[MAMEMachinesFilterPanel]`), Customize MAME Software List and Customize MAME Machines List (`CustomizeListDialog.cpp`, `mame_softlist_exclude.txt` / `mame_machines_exclude.txt`).
- Preferences window, page 1: Don't Minimize, Allow Only One Instance, Ignore Exit Code 1, Hide Command Prompt Box (create list / run game), natural sorting, grayscale icons for sets with missing ROMs, hide seconds in Last Played / Playtime, Show Driver Path (new: `sony/zn1.cpp` or `zn1.cpp` in the Driver Name column; driver custom settings always go to `ini\source\zn1.ini`, as MAME reads them), the status column options (icons, texts, first letter, short titles) the console/computer setting and the game documents' display mode and order. Page 2 (light mode, `ini_files\lightmode.ini`): tool bar background (Windows theme / gradient / solid), search panel colors, the games list selection bar (gradient, alpha blending, round corners; missing-ROMs and inactive colors), status bar, list background color and image (tiled or not), the games list splitter style (the 17 TMS styles, single color, grip), the image border color, the image layout splitters, the image hint box and the game documents' colors and font, previewed live.

- Main CPU filter (Misc Filters > Filter MAME Games by Main CPU): a row under the tool bar, the CPU kept in the games list (`<maincpu>`).

**Left out on purpose.** The original Scan Games Mode window's Add MAME Device Sets With No ROMs (a hidden developer switch: the device sets without ROMs are never shown and aren't needed to check ROMs).

Consoles and computers run by emulators other than MAME, and the other arcade
emulators (Demul, Supermodel, Model 2, ZiNc, Daphne, DICE), are out of scope.
(Console and computer machines in MAME are supported; see above.) Their windows are converted in
`forms/` in case they are wanted later.

### Porting the next window

1. Its `.ui` is already in `forms/`. Preview it with
   `build/formview forms/FormX.ui out.png`.
2. Add it to the `emuloader` target, then call `setupUi()` and `vcl::apply()`.
3. Port the Pascal event handlers (their names are in the `vclOnClick` and
   related properties). Hook up menu items by name with `Menus::on()`.

## Languages

Every text can be translated with a plain UTF-8 file named by language code,
`lang/<code>.ini` (`fr.ini`, `de.ini`, `pt_BR.ini`; "English text=translation"
per line, one section per window, named in `src/app/LanguageSections.inc`: a window's
code (`eld::FavoritesManagerDialog`) and form (`FormFavoritesManager`) share `[Favorites Manager]`;
the program's names still work as sections). `src/app/Language.cpp` loads it as a
`QTranslator`, so it covers the converted forms (uic), `tr()` in the code and
the menus (`[Menus]`). The code also loads Qt's `translations/qtbase_<code>.qm`;
`Locale=` and `Name=` in the file are optional. A clean install asks for the
language, with the system language preselected; Preferences > General changes
it. A setting by shown or English name (`Français`, `French`) still finds the
file and is rewritten as the code. `lang/fr.ini` is a complete example.

The program's texts are in US English. `lang/en_GB.ini` (English (UK)) gives
every text with a word spelt differently in British spelling; it's generated by
`tools/make_british.py` (run it after `make_language_template.py`). A clean
install picks it only on an en_GB system; other English systems keep US English.

* `tools/make_language_template.py` writes `lang/English.ini` (the template)
  from `lupdate` and `forms/FormMain.menus.json`; with a file argument it adds
  new texts to an existing translation.
* New code: wrap user-visible strings in `tr()` (or
  `QCoreApplication::translate` / `QT_TRANSLATE_NOOP` outside a QObject), never
  ini keys, file names or MAME option values.
* Long translations: `vcl::relayout` widens AutoSize labels, check boxes and
  radio buttons into free space (never over a neighbor), and
  `vcl::elideLongTexts` cuts what still doesn't fit with "…", the full text in
  the tooltip. `langcheck <file> [--grab <folder>]` (also shipped as
  `langcheck.exe`) lists texts that don't fit and can save a picture of each
  window. It checks the windows that still have fixed places (the main window
  and MAME Settings); the windows in layouts and the Settings tabs make room for
  their texts.
* Flexible layouts from the original windows: `vcl::autoLayout(container)`
  (`src/ui/VclRuntime.cpp`) cuts a converted form's controls into rows and
  columns along the gaps of the design (columns first for panels side by side,
  rows first for controls), keeping the design's gaps, indents and sizes as
  minimums. In English the window looks as designed; a longer text or a larger
  font makes the window grow instead of cutting the text. Controls that overlap
  keep their design places; right-anchored ones stay at the right edge. MAME
  Settings uses it for all its pages (`MameSettingsDialog::setupPages`), and
  `langcheck` measures that window converted. Drop-down lists open as wide as
  their longest entry in every window.
* Fonts for other scripts: `src/ui/ScriptFonts.cpp` gives every installed font
  the system's font for each script as a substitute (`QFont::insertSubstitutions`,
  the language's script first) when the language is installed. A language's
  `[Language] Font=` replaces the usual interface fonts (forms and the
  application font), and `FontSize=+N` is added where fonts are shown
  (`ScriptFonts::shown`, `shownPixels`), never to a font that's saved.
* Tool bar captions: the original draws them into the pictures, in English.
  `src/app/ToolBarIcons.cpp` cuts that strip off, takes the best frame of each
  icon for the display scaling, and gives the caption as text (`[ToolBar]` in
  the language file), which `ElButton::setCaption` draws under the picture.
* `tools/dfm2ui.py` marks the `vcl*` properties and the MAME settings combo
  values `notr`, so they can't be translated by accident.

## Updating for new MAME versions

* A new option that the dialog already has a control for: add a line to
  `src/forms/MameOptionMap.inc`, e.g.
  `{ {"newoption"}, "bool", "SomeCheckBox", "", false },`.
* A new option without a control: add the control to the `.ui` (Qt Designer
  can edit converted forms), then add the line.
* Options the dialog doesn't know are never removed from `mame.ini`, and
  options missing from the user's `mame.ini` are never added (an older MAME
  would reject them).

## License

EmuLoader ReLoaded keeps the original EmuLoader's terms: free of charge and open source, it may be copied,
shared and changed, but not sold or used for commercial purposes; changed versions keep the credits and these
terms, say they're changed and share their source. See [LICENSE](LICENSE). There is no warranty.

It uses, under their own licenses: Qt 6 (LGPL v3), miniz (`third_party/miniz`, MIT), the LZMA SDK's 7z decoder
by Igor Pavlov (`third_party/lzma`, public domain), the Press Start 2P font (SIL
Open Font License), and, for the built-in video player, libmpv and FFmpeg (an LGPL build: see `third_party/mpv`).

## Tools

| Tool | Purpose |
|---|---|
| `tools/dfm2ui.py` | `.dfm` → `.ui` + menus JSON (`python3 tools/dfm2ui.py source/*.dfm -o forms`) |
| `tools/extract_mame_options.py` | regenerates `MameOptionMap.inc` from `uMAMESettings.pas` and lists what needs hand porting |
| `tools/formview.cpp` | renders a converted form to PNG |
| `tools/settingsview.cpp` | opens the Emulator Settings window on a data folder, for testing |
| `tools/make_language_template.py` | writes `lang/English.ini`; with a file argument, adds new texts to a translation |
| `tools/langcheck.cpp` | lists translated texts that don't fit their windows (`--grab` saves pictures); shipped as `langcheck.exe` |
| `tools/package-windows.sh` | cross-builds the Windows version and makes `dist/EmuLoader-ReLoaded-1.0.0-windows.zip` |

A community continuation of EmuLoader, originally created by Ciro Alfredo Consentino (1974–2021). Ported from his Emuloader v8.9.4.
