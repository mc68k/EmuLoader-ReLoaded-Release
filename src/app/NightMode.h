// NightMode - Menu Options > Night Mode (IsNightMode, [Preferences] NightModeEnabled) and its
// colors, nightmode\<profile>.ini ([Preferences] NightModeProfile, "Default"):
// TFormMain.Read/WriteNightModeSettings.
//
// The games list, its selection bar, status bar, splitters, search panel, image hint box and game
// documents take the same keys as the light mode's (lightmode.ini), so they're a LightMode with
// the night defaults; the rest of the program is drawn dark from the panels' and buttons' colors
// (ApplyNightMode in MainWindow).
#pragma once

#include "LightMode.h"

#include <QColor>
#include <QString>
#include <QStringList>
#include <vector>

namespace eld {

struct NightMode {
    LightMode look;   // [ToolBar] [GamesListSelectionBar] [GamesListStatusBar] [GamesListSplitter]
                      // [GamesListBackground] [Images] [SearchGamesPanel] [GameDocuments]
    // [GamesListHeader]: the column titles' background (normal, hover, pressed); its font is
    // FontColor / FontName / FontSize / FontType here (look.header*)
    int headerBack = 2367259, headerHover = 5263440, headerDown = 2565927;
    // [SearchGamesPanel] the text box's frame
    int searchFrame = 1513239, searchFocusedFrame = 11836807;
    // [Panels]
    bool panelsGradient = true;
    int panelsTop = 4734774, panelsBottom = 1578258, panelsInnerFrame = 1578258, panelsOuterFrame = 1578258;
    int titleFont = 0xFFFFFF, titleShadow = 1578258, title2Font = 11836807, title2Shadow = 1578258;
    bool titleShadowOn = false, title2ShadowOn = false;
    int imageCategoryText = 20985, imageZipText = 20985;
    // [Buttons]
    int buttonFont = 0xF0FBFF, buttonTop = 5131854, buttonBottom = 3289650, buttonFrame = 2236962, buttonFocusedFrame = 12615756;
    int buttonHoverTop = 12153143, buttonHoverBottom = 10181674, buttonHoverFrame = 1513239;
    int buttonSelectedTop = 20985, buttonSelectedBottom = 8290, buttonSelectedFrame = 2236962;
    int buttonDisabledTop = 3158064, buttonDisabledBottom = 1973790, buttonDisabledFrame = 2236962;
    int buttonDisabledFont = 0x808080, buttonDisabledShadow = 3158064;
    bool buttonDisabledShadowOn = false;
    QString checkTheme = "Night Mode 1 Black"; // CheckBoxRadioButtonProfile
    // [MenuPopup]
    int menuFrame = 1513239;
    // [ToolBar]
    QString overlayIconFolder = "Default";

    NightMode();                       // the original's night defaults
    bool load(const QString& file);
    bool save(const QString& file) const;

    // The Customize Night Mode Colors window's controls (NightMode<name>) and their values
    struct IntField { const char* section; const char* key; const char* control; int NightMode::*member; };
    struct BoolField { const char* section; const char* key; const char* control; bool NightMode::*member; };
    static const std::vector<IntField>& intFields();
    static const std::vector<BoolField>& boolFields();
    int* intValue(const QString& control);    // by the control's name, look's too; nullptr if none
    bool* boolValue(const QString& control);

    // nightmode\*.ini: the profiles
    static QStringList profiles(const QString& folder);
    // The game fonts' file: ini_files\sysgamecustomfont_nightmode.ini
    static QString fontsFile(const QString& iniFilesFolder) { return iniFilesFolder + "sysgamecustomfont_nightmode.ini"; }
};

} // namespace eld
