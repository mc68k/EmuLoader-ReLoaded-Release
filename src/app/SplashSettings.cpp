#include "SplashSettings.h"

#include "ElWidgets.h"
#include "WinIni.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace eld {

namespace {
const char* const kSection = "Splash";
// TColor values (BGR)
constexpr int clBlack = 0x000000, clWhite = 0xFFFFFF, clYellow = 0x00FFFF, clLime = 0x00FF00, clAqua = 0xFFFF00;
const char* const kTextKeys[] = {"TitleTextColor", "MessageTextColor", "TimerTextColor", "SoftwareTextColor",
                                 "VersionInfoTextColor"};
const char* const kShadowKeys[] = {"TitleShadowColor", "MessageShadowColor", "TimerShadowColor",
                                   "SoftwareShadowColor", "VersionInfoShadowColor"};
} // namespace

SplashSettings::SplashSettings() {
    for (int t = 0; t < TextCount; ++t) {
        textColor[t] = defaultTextColor(t);
        shadowColor[t] = clBlack;
    }
    custom = schemeColors(1); // custom colors start from Desert, as in the original
}

int SplashSettings::defaultTextColor(int text) {
    // the labels' colors in uStatus.dfm (DefaultColorColor of the color boxes)
    switch (text) {
    case Title: return clYellow;
    case Timer: return clLime;
    case Count: return clAqua;
    default: return clWhite;
    }
}

SplashSettings SplashSettings::load(const QString& iniFile) {
    SplashSettings s;
    WinIni ini;
    if (!ini.load(iniFile)) return s;
    for (int t = 0; t < TextCount; ++t) {
        s.textColor[t] = ini.readInteger(kSection, kTextKeys[t], s.textColor[t]);
        s.shadowColor[t] = ini.readInteger(kSection, kShadowKeys[t], s.shadowColor[t]);
    }
    s.shadows = ini.readBool(kSection, "ShowTextShadows", true);
    s.oneShadowColor = ini.readBool(kSection, "TextShadow_OneColor", true);
    s.shadowColorAll = ini.readInteger(kSection, "TextShadowColorAll", clBlack);
    s.shadowDistance = std::clamp(ini.readInteger(kSection, "TextShadowDistance", 1), 0, 10);
    s.versionX = ini.readInteger(kSection, "VersionInfoPositionX", defaultVersionX);
    s.versionY = ini.readInteger(kSection, "VersionInfoPositionY", defaultVersionY);
    // the original's defaults (6,228) put it over the alternate picture's
    // artwork: they become this version's default place
    if (s.versionX == 6 && (s.versionY == 228 || s.versionY == 260)) {
        s.versionX = defaultVersionX;
        s.versionY = defaultVersionY;
    }
    s.alternateLogo = ini.readBool(kSection, "EnableAlternateLogoFile", false);
    s.alternateLayout = ini.readBool(kSection, "EnableTextsAlternateLayout", false);
    s.largeSize = ini.readBool(kSection, "UltraSize_4Kx2", false);
    s.scheme = std::clamp(ini.readInteger(kSection, "ProgressBarColorScheme", 1), 0, 12);
    s.custom.backFace = ini.readInteger(kSection, "ProgressBarCustomBackTop", s.custom.backFace);
    s.custom.backGrad = ini.readInteger(kSection, "ProgressBarCustomBackDown", s.custom.backGrad);
    s.custom.border = ini.readInteger(kSection, "ProgressBarCustomBackBorder", s.custom.border);
    s.custom.foreFace = ini.readInteger(kSection, "ProgressBarCustomBarTop", s.custom.foreFace);
    s.custom.foreGrad = ini.readInteger(kSection, "ProgressBarCustomBarDown", s.custom.foreGrad);
    s.effect = ini.readString(kSection, "Effect", "none").toLower();
    if (s.effect != "none" && s.effect != "random" && !effectKeys().contains(s.effect)) s.effect = "none";
    s.insertCoin = ini.readBool(kSection, "InsertCoin", false);
    s.lastRandom = ini.readString(kSection, "LastRandomEffect");
    return s;
}

bool SplashSettings::save(const QString& iniFile) const {
    WinIni ini;
    ini.load(iniFile); // keeps the original's 4K keys and anything else
    for (int t = 0; t < TextCount; ++t) {
        ini.writeInteger(kSection, kTextKeys[t], textColor[t]);
        ini.writeInteger(kSection, kShadowKeys[t], shadowColor[t]);
    }
    ini.writeInteger(kSection, "EnableAlternateLogoFile", alternateLogo);
    ini.writeInteger(kSection, "EnableTextsAlternateLayout", alternateLayout);
    ini.writeInteger(kSection, "ShowTextShadows", shadows);
    ini.writeInteger(kSection, "TextShadow_OneColor", oneShadowColor);
    ini.writeInteger(kSection, "TextShadowColorAll", shadowColorAll);
    ini.writeInteger(kSection, "TextShadowDistance", shadowDistance);
    ini.writeInteger(kSection, "VersionInfoPositionX", versionX);
    ini.writeInteger(kSection, "VersionInfoPositionY", versionY);
    ini.writeInteger(kSection, "UltraSize_4Kx2", largeSize);
    ini.writeInteger(kSection, "ProgressBarColorScheme", scheme);
    ini.writeInteger(kSection, "ProgressBarCustomBackTop", custom.backFace);
    ini.writeInteger(kSection, "ProgressBarCustomBackDown", custom.backGrad);
    ini.writeInteger(kSection, "ProgressBarCustomBackBorder", custom.border);
    ini.writeInteger(kSection, "ProgressBarCustomBarTop", custom.foreFace);
    ini.writeInteger(kSection, "ProgressBarCustomBarDown", custom.foreGrad);
    ini.writeString(kSection, "Effect", effect);
    ini.writeInteger(kSection, "InsertCoin", insertCoin);
    ini.writeString(kSection, "LastRandomEffect", lastRandom);
    QDir().mkpath(QFileInfo(iniFile).absolutePath());
    return ini.save(iniFile);
}

QColor SplashSettings::shadowFor(int text) const {
    return ElColorBox::toQColor(oneShadowColor ? shadowColorAll : shadowColor[text]);
}

ProgressColors SplashSettings::schemeColors(int scheme) {
    // TXiProgressBar.SetColorScheme
    switch (scheme) {
    case 2: return {0x156F37, 0xCBF5DC, clWhite, 0xC9F5DB, 0x28C162};     // Grass
    case 3: return {0x6B6B6B, 0xE0E0E0, clWhite, 0xF0F0F0, 0xADADAD};     // Silver
    case 4: return {0xB35900, 0xFFEAD5, clWhite, 0xFFEEDD, 0xFFAE5E};     // Sky
    case 5: return {0x00009D, 0xD7D7FF, clWhite, 0xE8E8FF, 0x8080FF};     // Rose
    case 6: return {0x006464, 0xCEFFFF, clWhite, 0xDFFFFF, 0x05BCDC};     // Sun
    case 7: return {clBlack, 0x005500, 0x008000, 0x91ECAE, 0x00AA00};     // Hackers
    case 8: return {clBlack, 0x804000, 0xA85400, 0xFFCB97, 0xEC7600};     // Night
    case 9: return {clBlack, 0x000059, 0x46468C, 0x9F9FF2, 0x4848BB};     // Velvet
    case 10: return {clBlack, 0x464646, 0x6F6F6F, 0xCECECE, 0x737373};    // Metal
    case 11: return {clBlack, 0x800040, 0xCA0065, 0xFFB7DB, 0xFF3399};    // Violet
    case 12: return {clBlack, 0x2C5656, 0x3E7B7B, 0xBCDEDE, 0x468E8E};    // Toxic
    default: return {0x005680, 0xC6ECFF, clWhite, 0x9BDEFF, 0x007BB7};    // Desert
    }
}

ProgressColors SplashSettings::progressColors() const { return scheme == 0 ? custom : schemeColors(scheme); }

QStringList SplashSettings::schemeNames() {
    return {QCoreApplication::translate("SplashSettings", "Custom Colors"), QCoreApplication::translate("SplashSettings", "Desert"),
            QCoreApplication::translate("SplashSettings", "Grass"), QCoreApplication::translate("SplashSettings", "Silver"),
            QCoreApplication::translate("SplashSettings", "Sky"), QCoreApplication::translate("SplashSettings", "Rose"),
            QCoreApplication::translate("SplashSettings", "Sun"), QCoreApplication::translate("SplashSettings", "Hackers"),
            QCoreApplication::translate("SplashSettings", "Night"), QCoreApplication::translate("SplashSettings", "Velvet"),
            QCoreApplication::translate("SplashSettings", "Metal"), QCoreApplication::translate("SplashSettings", "Violet"),
            QCoreApplication::translate("SplashSettings", "Toxic")};
}

// (CRT Power-On was taken out: its curved, darkened picture made the splash screen's texts hard to read.)
QStringList SplashSettings::effectKeys() { return {"grid", "marquee", "card", "stars", "scores", "glitch"}; }

QString SplashSettings::effectTitle(const QString& key) {
    if (key == "none") return QCoreApplication::translate("SplashSettings", "None");
    if (key == "random") return QCoreApplication::translate("SplashSettings", "Random");
    if (key == "grid") return QCoreApplication::translate("SplashSettings", "Vector Grid");
    if (key == "marquee") return QCoreApplication::translate("SplashSettings", "Marquee Light-Up");
    if (key == "card") return QCoreApplication::translate("SplashSettings", "Card Flip");
    if (key == "stars") return QCoreApplication::translate("SplashSettings", "Starfield Warp");
    if (key == "scores") return QCoreApplication::translate("SplashSettings", "High Scores");
    if (key == "glitch") return QCoreApplication::translate("SplashSettings", "Signal Glitch");
    return key;
}

} // namespace eld
