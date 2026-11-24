cmake_minimum_required(VERSION 3.19)
# EmuLoader ReLoaded: the version is set here only. EL_VERSION_LABEL is what people see
# ("1.0.0"); the numbers go into the Windows file properties.
project(EmuLoaderRL VERSION 1.1.1 LANGUAGES C CXX)
set(EL_PRODUCT_NAME "EmuLoader ReLoaded")
set(EL_VERSION_LABEL "1.1.1")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# All the usual warnings for EmuLoader's own code (C++), with GCC and Clang (Apple's too): the code is
# kept free of them, so a new one stands out. The third-party code (C) keeps its own.
if(NOT MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall> $<$<COMPILE_LANGUAGE:CXX>:-Wextra>)
endif()
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_SOURCE_DIR}/forms)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
# The update check: on Windows with WinHTTP (part of Windows: no Qt6Network.dll to ship), elsewhere
# with Qt Network (src/app/UpdateFetch.cpp)
if(NOT WIN32)
  find_package(Qt6 REQUIRED COMPONENTS Network)
endif()
find_package(Qt6 QUIET COMPONENTS UiTools)

# ------------------------------------------------------------- core (no GUI)
add_library(el_core STATIC
  src/core/TextFile.cpp
  src/core/WinIni.cpp
  src/core/MameIni.cpp
  src/core/ElDatabase.cpp
  src/core/Frontend.cpp
  src/core/ImageFinder.cpp
  src/core/ImageIndex.cpp
  src/core/Trash.cpp
  src/core/VideoPreview.cpp
  src/core/GameFilters.cpp
  src/core/Favorites.cpp
  src/core/GameDocs.cpp
  src/core/MachineInfo.cpp
  src/core/PlayedGames.cpp
  src/core/GameFiles.cpp
  src/core/RunExtras.cpp
  src/core/ZipArchive.cpp
  src/core/SoftwareLists.cpp
  src/core/MultiSlot.cpp
  src/core/DiagnosticLog.cpp
  src/core/GameInfoPage.cpp
  src/core/RomAudit.cpp
  src/core/ChdCheck.cpp
  src/core/SevenZip.cpp
  src/core/UpdateCheck.cpp
  src/core/UpdateInstall.cpp
  third_party/miniz/miniz.c)
# The 7z decoder of the LZMA SDK (public domain): .7z picture and icon archives (SevenZip.cpp)
add_library(el_lzma STATIC
  third_party/lzma/7zAlloc.c third_party/lzma/7zArcIn.c third_party/lzma/7zBuf.c third_party/lzma/7zBuf2.c
  third_party/lzma/7zCrc.c third_party/lzma/7zCrcOpt.c third_party/lzma/7zDec.c third_party/lzma/7zStream.c
  third_party/lzma/Bcj2.c third_party/lzma/Bra.c third_party/lzma/Bra86.c third_party/lzma/BraIA64.c
  third_party/lzma/CpuArch.c third_party/lzma/Delta.c third_party/lzma/Lzma2Dec.c third_party/lzma/LzmaDec.c
  third_party/lzma/Ppmd7.c third_party/lzma/Ppmd7Dec.c)
target_compile_definitions(el_lzma PUBLIC Z7_PPMD_SUPPORT Z7_EXTRACT_ONLY)
target_include_directories(el_lzma PUBLIC third_party/lzma)
target_link_libraries(el_core PRIVATE el_lzma)
target_include_directories(el_core PUBLIC src/core PRIVATE third_party/miniz)
if(NOT MSVC) # (miniz says, on purpose, which file functions it uses: not a warning)
  set_source_files_properties(third_party/miniz/miniz.c PROPERTIES COMPILE_OPTIONS -w)
endif()
target_link_libraries(el_core PUBLIC Qt6::Core)
if(WIN32)
  target_link_libraries(el_core PUBLIC shell32) # SHFileOperationW: the Recycle Bin (Trash.cpp)
endif()
set_target_properties(el_core PROPERTIES AUTOMOC OFF AUTOUIC OFF)

# ------------------------------------------------- VCL look-alike Qt widgets
add_library(el_ui STATIC
  src/ui/ElWidgets.cpp
  src/ui/ScriptFonts.cpp
  src/ui/VclRuntime.cpp)
target_include_directories(el_ui PUBLIC src/ui)
target_link_libraries(el_ui PUBLIC Qt6::Widgets)

# ------------------------------------------------------------- application
# Everything but main().
add_library(el_app STATIC
  src/app/MainWindow.cpp
  src/app/MainWindowDocs.cpp
  src/app/FirstStart.cpp
  src/app/ImageResample.cpp
  src/app/SplashSettings.cpp
  src/app/SplashView.cpp
  src/app/SplashEffects.cpp
  src/app/SplashScreen.cpp
  src/app/SplashCustomizeDialog.cpp
  src/app/RomsFoldersDialog.cpp
  src/app/FileVersionsDialog.cpp
  src/app/AboutDialog.cpp
  src/app/ExportGamesDialog.cpp
  src/app/ScanSamplesDialog.cpp
  src/app/ColumnsEditorDialog.cpp
  src/app/GamesIconView.cpp
  src/app/ThumbnailSettingsDialog.cpp
  src/app/NightMode.cpp
  src/app/NightModeDialog.cpp
  src/app/MamuIcons.cpp
  src/app/GameDetailsDialog.cpp
  src/app/LastPlayedDialog.cpp
  src/app/EmulatorsSetupDialog.cpp
  src/app/DriverStatusDialog.cpp
  src/app/ProgressWindow.cpp
  src/app/FavoritesManagerDialog.cpp
  src/app/FileProblem.cpp
  src/app/GameFilesDialog.cpp
  src/app/ImageDeleteRenameDialog.cpp
  src/app/ImagesManagerDialog.cpp
  src/app/RecycleBin.cpp
  src/app/CrashLog.cpp
  src/app/CategorySettingsDialog.cpp
  src/app/VideoPreviewSettingsDialog.cpp
  src/app/MiscFiltersDialog.cpp
  src/app/GamesFiltersDialog.cpp
  src/app/ImagesDeleteClonesDialog.cpp
  src/app/CustomParametersDialog.cpp
  src/app/RunGameExtraDialog.cpp
  src/app/ToolBarIcons.cpp
  src/app/SettingsSearch.cpp
  src/app/Language.cpp
  src/app/MameSettingsDialog.cpp
  src/app/MameSettingsExtras.cpp
  src/app/GamesModel.cpp
  src/app/Menus.cpp
  src/app/GameFonts.cpp
  src/app/GameFontsDialog.cpp
  src/app/MachineFilterDialog.cpp
  src/app/PreferencesDialog.cpp
  src/app/LightMode.cpp
  src/app/GamesListDelegate.cpp
  src/app/ImageView.cpp
  src/app/ImageLayouts.cpp
  src/app/LayoutSettingsDialog.cpp
  src/app/SelectMachineDialog.cpp
  src/app/MultiSlotDialog.cpp
  src/app/MainWindowSoftware.cpp
  src/app/MainWindowMachines.cpp
  src/app/CustomizeListDialog.cpp
  src/app/ScanGamesDialog.cpp
  src/app/ScanResultsDialog.cpp
  src/app/MainWindowAudit.cpp
  src/app/MainWindowToolBar.cpp
  src/app/MainWindowGameFailed.cpp
  src/app/MainWindowEmptyList.cpp
  src/app/MainWindowVideo.cpp
  src/app/VideoPanel.cpp
  src/app/MainWindowSetupCheck.cpp
  src/app/MainWindowSevenZip.cpp
  src/app/MainWindowUpdates.cpp
  src/app/UpdateFetch.cpp
  src/app/UpdateDownload.cpp
  src/app/ToolBarCustomizer.cpp
  src/app/MainWindowSettings.cpp
  src/app/SettingsDialog.cpp
  src/app/SettingsLayout.cpp
  src/app/PreferencesPages.cpp
  forms/FormMain.ui
  forms/FormImageCategorySelector.ui
  forms/FormImageLayoutSelector.ui
  forms/FormImageLayoutSettings.ui
  forms/FormSelectFilterSystemMega.ui
  forms/FormPreferences.ui
  forms/FormGamesListFontSettings.ui
  forms/FormMAMESettings.ui
  forms/FormGameDetails.ui
  forms/FormLastPlayedGames.ui
  forms/FormArcadeEmulatorsSetup.ui
  forms/FormArcadeFiltersDriverStatus.ui
  forms/FormFavoritesManager.ui
  forms/FormCustomParameters.ui
  forms/FormImageDeleteRename.ui
  forms/FormImagesManager.ui
  forms/FormImageCategorySettings.ui
  forms/FormVideoPreviewSettings.ui
  forms/FormArcadeFiltersExtra.ui
  forms/FormArcadeGamesFilter.ui
  forms/FormImagesDeleteClones.ui
  forms/FormImageLayoutDimensions.ui
  forms/FormArcadeRunGameExtraMAME.ui
  forms/FormArcadeROMsFolders.ui
  forms/FormArcadeFileVersionsLarge.ui
  forms/FormArcadeExportGamesList.ui
  forms/FormArcadeScanAudioSamples.ui
  forms/FormColumnsEditor.ui
  forms/FormArcadeSoftwareListMachineToRunGame.ui
  forms/FormArcadeMultiSlotGames.ui
  forms/FormArcadeSoftwareListCustomize.ui
  forms/FormArcadeMAMEMachinesCustomize.ui
  forms/FormArcadeScanGamesMode.ui
  forms/FormArcadeScanGamesResults.ui)
target_include_directories(el_app PUBLIC src/app src/forms)
target_link_libraries(el_app PUBLIC el_core el_ui)
if(NOT WIN32)
  target_link_libraries(el_app PUBLIC Qt6::Network)
endif()
# Press Start 2P (SIL Open Font License, resources/fonts/PressStart2P-OFL.txt): the splash
# screen's arcade lettering (high scores, INSERT COIN)
qt_add_resources(el_app "fonts" PREFIX "/fonts" BASE resources/fonts FILES resources/fonts/PressStart2P-Regular.ttf)
# The build (the git revision; tools/package-windows.sh passes it) is shown wherever the version
# is: the splash screen, About, the main window's title (while EL_TITLE_BUILD is on, as it is for
# releases too: "EmuLoader ReLoaded 1.0.0 (build 7e50476)"), the file properties, the logs and the
# game info pages.
set(EL_BUILD_REV "dev" CACHE STRING "Build identifier shown with the version")
target_compile_definitions(el_app PUBLIC EL_PRODUCT_NAME="${EL_PRODUCT_NAME}" EL_VERSION_LABEL="${EL_VERSION_LABEL}"
                           EL_BUILD_REV="${EL_BUILD_REV}")
option(EL_TITLE_BUILD "Show the version and build in the main window's title" ON)
set_source_files_properties(src/app/MainWindow.cpp PROPERTIES COMPILE_DEFINITIONS "EL_TITLE_BUILD=$<BOOL:${EL_TITLE_BUILD}>")
if(WIN32)
  target_link_libraries(el_app PUBLIC shlwapi shell32 winhttp) # StrCmpLogicalW (natural sorting, as the original); SHFileOperationW (Recycle Bin); WinHTTP (the update check)
endif()

add_executable(emuloader WIN32 src/app/main.cpp)
target_link_libraries(emuloader PRIVATE el_app)
# Not "EmuLoader.exe": Windows file names ignore case, and this program can share a
# folder with the original Delphi EmuLoader.exe.
set_target_properties(emuloader PROPERTIES OUTPUT_NAME EmuLoader-ReLoaded)
qt_add_resources(emuloader "menus" PREFIX "/forms" BASE forms FILES forms/FormMain.menus.json)
if(WIN32)
  configure_file(src/app/emuloader.rc.in ${CMAKE_BINARY_DIR}/emuloader.rc @ONLY)
  target_sources(emuloader PRIVATE ${CMAKE_BINARY_DIR}/emuloader.rc)
endif()

# EmuLoader's icons, images and filter files, copied next to the program and
# installed into the data folder on first run.
add_custom_command(TARGET emuloader POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data_template $<TARGET_FILE_DIR:emuloader>/data_template
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/docs/EmuLoader-ReLoaded-Guide.html $<TARGET_FILE_DIR:emuloader>/EmuLoader-ReLoaded-Guide.html
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/docs/guide-images $<TARGET_FILE_DIR:emuloader>/guide-images
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/lang $<TARGET_FILE_DIR:emuloader>/lang)

# ------------------------------------------- form preview tool (development)
if(TARGET Qt6::UiTools)
  add_executable(formview tools/formview.cpp)
  target_link_libraries(formview PRIVATE el_ui Qt6::UiTools)
endif()

# --------------------------------- settings window preview (development)
add_executable(settingsview tools/settingsview.cpp src/app/MameSettingsDialog.cpp src/app/MameSettingsExtras.cpp
  src/app/PreferencesDialog.cpp src/app/PreferencesPages.cpp src/app/SettingsLayout.cpp src/app/SettingsSearch.cpp src/app/MachineFilterDialog.cpp src/app/GamesModel.cpp src/app/GameFonts.cpp
  src/app/LightMode.cpp src/app/Language.cpp src/app/FileProblem.cpp
  forms/FormMAMESettings.ui forms/FormPreferences.ui forms/FormSelectFilterSystemMega.ui)
target_include_directories(settingsview PRIVATE src/app src/forms)
target_link_libraries(settingsview PRIVATE el_core el_ui)
if(WIN32)
  target_link_libraries(settingsview PRIVATE shlwapi)
endif()

# Development tool: checks a language file against the windows' fixed layouts.
add_executable(langcheck tools/langcheck.cpp src/app/Language.cpp
  forms/FormMain.ui forms/FormPreferences.ui forms/FormMAMESettings.ui forms/FormImageLayoutSettings.ui
  forms/FormImageLayoutSelector.ui forms/FormImageCategorySelector.ui forms/FormSelectFilterSystemMega.ui
  forms/FormGamesListFontSettings.ui forms/FormGameDetails.ui forms/FormLastPlayedGames.ui
  forms/FormArcadeEmulatorsSetup.ui forms/FormArcadeFiltersDriverStatus.ui)
target_include_directories(langcheck PRIVATE src/app)
target_link_libraries(langcheck PRIVATE el_core el_ui)

# A source that uses a window's generated code (ui_<Form>.h) is compiled again whenever that
# window's .ui file changes. (The generated code's own dependency tracking missed a change once in
# the Windows build: one file kept the old window layout, and Settings crashed.)
file(GLOB_RECURSE EL_UI_USERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/tools/*.cpp)
foreach(src IN LISTS EL_UI_USERS)
  file(STRINGS ${src} uses REGEX "#include [\"<]ui_[A-Za-z0-9_]+\\.h[\">]")
  set(deps "")
  foreach(line IN LISTS uses)
    string(REGEX REPLACE ".*ui_([A-Za-z0-9_]+)\\.h.*" "\\1" form "${line}")
    if(EXISTS ${CMAKE_SOURCE_DIR}/forms/${form}.ui)
      list(APPEND deps ${CMAKE_SOURCE_DIR}/forms/${form}.ui)
    endif()
  endforeach()
  if(deps)
    set_property(SOURCE ${src} APPEND PROPERTY OBJECT_DEPENDS ${deps})
  endif()
endforeach()

