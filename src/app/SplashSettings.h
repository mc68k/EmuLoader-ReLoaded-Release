// SplashSettings - the splash screen's look (TFormStatus.ReadSplashIni / WriteSplashSettings):
// ini_files\splash.ini [Splash], with the original's keys, plus this version's effect choice.
//
// Positions are in the artwork's own pixels (980 x 600), whatever size the splash screen is
// drawn at. Colors are Delphi TColor values, as the original writes them.
#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

namespace eld {

// The progress bar's color schemes (TXiProgressBar.ColorScheme): 0 = the user's own colors.
struct ProgressColors {
    int border = 0, backFace = 0, backGrad = 0, foreFace = 0, foreGrad = 0; // TColor
};

struct SplashSettings {
    // Texts: title (the job), message (the step), timer, count ("000 of 000"), version.
    enum Text { Title, Message, Timer, Count, Version, TextCount };
    int textColor[TextCount];
    int shadowColor[TextCount];
    bool shadows = true;          // ShowTextShadows
    bool oneShadowColor = true;   // TextShadow_OneColor
    int shadowColorAll = 0;       // TextShadowColorAll (clBlack)
    int shadowDistance = 1;       // TextShadowDistance
    // VersionInfoPositionX / Y. New default: the top right, clear of both pictures' artwork
    // (x = -1 puts it against the right edge, whatever the text's width).
    int versionX = 640, versionY = 20;
    bool alternateLogo = false;   // EnableAlternateLogoFile: logo2.png ("Night Mode Edition")
    bool alternateLayout = false; // EnableTextsAlternateLayout
    bool largeSize = false;       // UltraSize_4Kx2: the splash screen half as big again
    int scheme = 1;               // ProgressBarColorScheme (1 = Desert)
    ProgressColors custom;        // ProgressBarCustom*: the colors of scheme 0
    // New: the effect played at startup ("none", "random", or an effect's key) and the blinking
    // INSERT COIN after it.
    QString effect = "none";
    bool insertCoin = false;
    QString lastRandom;           // the effect Random played last (not chosen twice in a row)

    SplashSettings();
    static SplashSettings load(const QString& iniFile);
    bool save(const QString& iniFile) const;

    static int defaultTextColor(int text);
    static constexpr int defaultVersionX = 640, defaultVersionY = 20;
    QColor shadowFor(int text) const;  // the shadow color a text is drawn with
    ProgressColors progressColors() const; // the scheme's colors, or the custom ones
    static ProgressColors schemeColors(int scheme);
    static QStringList schemeNames();      // "Custom Colors", "Desert", ... (translated)

    // Effects, in the drop-down's order after None and Random.
    static QStringList effectKeys();       // "grid", "marquee", "card", "stars", "scores", "glitch"
    static QString effectTitle(const QString& key);
};

} // namespace eld
