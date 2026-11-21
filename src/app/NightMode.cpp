#include "NightMode.h"

#include "WinIni.h"

#include <QDir>
#include <QFileInfo>

namespace eld {

NightMode::NightMode() {
    // DefaultColorColor of uNightMode.dfm's color boxes, and ReadNightModeSettings' defaults
    LightMode& l = look;
    l.toolBarWindowsTheme = false;
    l.toolBarGradient = true;
    l.toolBarTop = 4734774;
    l.toolBarBottom = 1513239;
    l.searchFilterFont = 6576720;
    l.searchFieldFont = 9469548;
    l.searchTextFont = 11836807;
    l.searchTextBack = 4734774;
    l.listBack = 1513239;
    l.listTile = true;
    l.listImageEnabled = true;
    l.listImage = "resources\\images\\bkgrd_dark.png";
    l.selGradient = l.selAlphaBlend = l.selRoundCorners = true;
    l.selTop = 20985;
    l.selBottom = 8290;
    l.selFrame = 1513239;
    l.selFont = 44287;
    l.selMissTop = 5636091;
    l.selMissBottom = 44287;
    l.selMissFrame = 1513239;
    l.selMissFont = 1513239;
    l.selInactiveTop = 9141097;
    l.selInactiveBottom = 4734774;
    l.selInactiveFrame = 1513239;
    l.selInactiveFont = 0xC0C0C0; // clSilver
    l.statusGradient = true;
    l.statusTop = 1513239;
    l.statusBottom = 1052429;
    l.statusFont = 11836807;
    l.splitterGrip = false;
    l.splitterStyle = 1;    // the night splitters are one color (and the hot one under the mouse)
    l.imgSplitterStyle = 1;
    l.splitterSingle = 1513239;
    l.splitterSingleHot = 20985;
    l.imageBorder = 1513239;
    l.hintColor = 1513239;
    l.hintFrameColor = 4734774;
    l.hintText = 20985;
    l.hintShadow = 1513239;
    l.hintFrame = false;
    l.hintShadowOn = false;
    l.hintItalic = false;
    l.hintOpacity = 190;
    l.hintPosition = 8;
    l.imgSplitterGrip = true;
    l.imgSplitterSingle = 1513239;
    l.imgSplitterSingleHot = 20985;
    l.docsBack = 1513239;
    l.docsBorder = 1513239;
    l.docsFontColor = 11836807;  // clrGamesListHeaderFontColor
    l.docsStatusBar = true;
    l.headerFontColor = 0xF0FBFF; // clCream
}

const std::vector<NightMode::IntField>& NightMode::intFields() {
    static const std::vector<IntField> f = {
        {"GamesListHeader", "GamesListHeaderBackgroundColor", "GamesListHeaderBackgroundColor", &NightMode::headerBack},
        {"GamesListHeader", "GamesListHeaderBackgroundColorHover", "GamesListHeaderBackgroundColorHover", &NightMode::headerHover},
        {"GamesListHeader", "GamesListHeaderBackgroundColorDown", "GamesListHeaderBackgroundColorDown", &NightMode::headerDown},
        {"SearchGamesPanel", "TextFrameColor", "SearchGamesPanelEditBoxCustomFrameColor", &NightMode::searchFrame},
        {"SearchGamesPanel", "TextFocusedFrameColor", "SearchGamesPanelEditBoxCustomFocusedFrameColor", &NightMode::searchFocusedFrame},
        {"Panels", "BackgroundTopColor", "SearchGamesPanelTopColor", &NightMode::panelsTop},
        {"Panels", "BackgroundBottomColor", "SearchGamesPanelBottomColor", &NightMode::panelsBottom},
        {"Panels", "InnerFrameColor", "SearchGamesPanelInnerFrameColor", &NightMode::panelsInnerFrame},
        {"Panels", "OuterFrameColor", "SearchGamesPanelOuterFrameColor", &NightMode::panelsOuterFrame},
        {"Panels", "TitleFontColor", "PanelColorsTitleFontColor", &NightMode::titleFont},
        {"Panels", "TitleShadowColor", "PanelColorsTitleShadowFontColor", &NightMode::titleShadow},
        {"Panels", "Title2FontColor", "PanelColorsTitle2FontColor", &NightMode::title2Font},
        {"Panels", "Title2ShadowColor", "PanelColorsTitle2ShadowFontColor", &NightMode::title2Shadow},
        {"Panels", "ImageCategoryTextFontColor", "PanelColorsImageCategoryTextFontColor", &NightMode::imageCategoryText},
        {"Panels", "ImageZipTextFontColor", "PanelColorsImageZipTextFontColor", &NightMode::imageZipText},
        {"Buttons", "FontColor", "ButtonColorFontColor", &NightMode::buttonFont},
        {"Buttons", "GradientTop", "ButtonColorGradientTop", &NightMode::buttonTop},
        {"Buttons", "GradientBottom", "ButtonColorGradientBottom", &NightMode::buttonBottom},
        {"Buttons", "FrameColor", "ButtonColorFrameColor", &NightMode::buttonFrame},
        {"Buttons", "FrameColorFocused", "ButtonColorFrameColorFocused", &NightMode::buttonFocusedFrame},
        {"Buttons", "GradientTopHover", "ButtonColorGradientTopHover", &NightMode::buttonHoverTop},
        {"Buttons", "GradientBottomHover", "ButtonColorGradientBottomHover", &NightMode::buttonHoverBottom},
        {"Buttons", "FrameColorHover", "ButtonColorFrameColorHover", &NightMode::buttonHoverFrame},
        {"Buttons", "GradientTopSelected", "ButtonColorGradientTopSelected", &NightMode::buttonSelectedTop},
        {"Buttons", "GradientBottomSelected", "ButtonColorGradientBottomSelected", &NightMode::buttonSelectedBottom},
        {"Buttons", "FrameColorSelected", "ButtonColorFrameColorSelected", &NightMode::buttonSelectedFrame},
        {"Buttons", "GradientTopDisabled", "ButtonColorGradientTopDisabled", &NightMode::buttonDisabledTop},
        {"Buttons", "GradientBottomDisabled", "ButtonColorGradientBottomDisabled", &NightMode::buttonDisabledBottom},
        {"Buttons", "FrameColorDisabled", "ButtonColorFrameColorDisabled", &NightMode::buttonDisabledFrame},
        {"Buttons", "FontColorDisabled", "ButtonColorFontColorDisabled", &NightMode::buttonDisabledFont},
        {"Buttons", "FontShadowColorDisabled", "ButtonColorFontShadowColorDisabled", &NightMode::buttonDisabledShadow},
        {"MenuPopup", "FrameColor", "MenuPopupMenuFrameColor", &NightMode::menuFrame},
    };
    return f;
}

const std::vector<NightMode::BoolField>& NightMode::boolFields() {
    static const std::vector<BoolField> f = {
        {"Panels", "GradientBar", "SearchGamesPanelGradientBar", &NightMode::panelsGradient},
        {"Panels", "TitleShadowEnabled", "PanelColorsTitleShadowEnabled", &NightMode::titleShadowOn},
        {"Panels", "Title2ShadowEnabled", "PanelColorsTitle2ShadowEnabled", &NightMode::title2ShadowOn},
        {"Buttons", "FontShadowUseDisabled", "LabelButtonColorFontShadowColorDisabled", &NightMode::buttonDisabledShadowOn},
    };
    return f;
}

int* NightMode::intValue(const QString& control) {
    QString c = control;
    if (c.startsWith("NightMode")) c = c.mid(9);
    for (const IntField& f : intFields())
        if (c == f.control) return &(this->*(f.member));
    for (const LightMode::IntField& f : LightMode::intFields())
        if (c == f.control) return &(look.*(f.member));
    if (c == "HintBox_Opacity") return &look.hintOpacity;
    if (c == "HintBox_Position") return &look.hintPosition;
    return nullptr;
}

bool* NightMode::boolValue(const QString& control) {
    QString c = control;
    if (c.startsWith("NightMode")) c = c.mid(9);
    for (const BoolField& f : boolFields())
        if (c == f.control) return &(this->*(f.member));
    for (const LightMode::BoolField& f : LightMode::boolFields())
        if (c == f.control) return &(look.*(f.member));
    return nullptr;
}

bool NightMode::load(const QString& file) {
    if (!QFileInfo::exists(file)) return false;
    look.load(file); // the keys it shares with lightmode.ini
    WinIni ini;
    ini.load(file);
    for (const IntField& f : intFields()) this->*(f.member) = ini.readInteger(f.section, f.key, this->*(f.member));
    for (const BoolField& f : boolFields()) this->*(f.member) = ini.readBool(f.section, f.key, this->*(f.member));
    look.headerFontColor = ini.readInteger("GamesListHeader", "FontColor", look.headerFontColor);
    look.headerFontName = ini.readString("GamesListHeader", "FontName", look.headerFontName);
    look.headerFontSize = ini.readInteger("GamesListHeader", "FontSize", look.headerFontSize);
    look.headerFontStyle = ini.readInteger("GamesListHeader", "FontType", look.headerFontStyle);
    checkTheme = ini.readString("Buttons", "CheckBoxRadioButtonProfile", checkTheme);
    overlayIconFolder = ini.readString("ToolBar", "OverlayIconFolder", overlayIconFolder);
    return true;
}

bool NightMode::save(const QString& file) const {
    QDir().mkpath(QFileInfo(file).absolutePath());
    if (!look.save(file)) return false;
    WinIni ini;
    ini.load(file);
    for (const IntField& f : intFields()) ini.writeInteger(f.section, f.key, this->*(f.member));
    for (const BoolField& f : boolFields()) ini.writeInteger(f.section, f.key, (this->*(f.member)) ? 1 : 0);
    // the night file has its own header font keys
    for (const char* k : {"ColumnsHeaderFontColor", "ColumnsHeaderFontName", "ColumnsHeaderFontSize", "ColumnsHeaderFontStyle"})
        ini.deleteKey("GamesListHeader", k);
    ini.writeInteger("GamesListHeader", "FontColor", look.headerFontColor);
    ini.writeString("GamesListHeader", "FontName", look.headerFontName);
    ini.writeInteger("GamesListHeader", "FontSize", look.headerFontSize);
    ini.writeInteger("GamesListHeader", "FontType", look.headerFontStyle);
    ini.writeString("Buttons", "CheckBoxRadioButtonProfile", checkTheme);
    ini.writeString("ToolBar", "OverlayIconFolder", overlayIconFolder);
    return ini.save(file);
}

QStringList NightMode::profiles(const QString& folder) {
    QStringList out;
    for (const QFileInfo& f : QDir(folder).entryInfoList({"*.ini"}, QDir::Files, QDir::Name | QDir::IgnoreCase))
        out << f.completeBaseName();
    if (!out.contains("Default", Qt::CaseInsensitive)) out.prepend("Default");
    return out;
}

} // namespace eld
