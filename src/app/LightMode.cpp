#include "LightMode.h"

#include "GameFonts.h"
#include "ElWidgets.h"
#include "WinIni.h"

#include <QDir>
#include <QFileInfo>

namespace eld {

const std::vector<LightMode::IntField>& LightMode::intFields() {
    static const std::vector<IntField> f = {
        {"ToolBar", "BackgroundTopColor", "ToolBarBkTopColor", &LightMode::toolBarTop},
        {"ToolBar", "BackgroundBottomColor", "ToolBarBkBottomColor", &LightMode::toolBarBottom},
        {"SearchGamesPanel", "FilterFontColor", "SearchGamesPanelFilterFontColor", &LightMode::searchFilterFont},
        {"SearchGamesPanel", "FieldFontColor", "SearchGamesPanelFieldFontColor", &LightMode::searchFieldFont},
        {"SearchGamesPanel", "TextFontColor", "SearchGamesPanelEditBoxFontColor", &LightMode::searchTextFont},
        {"SearchGamesPanel", "TextBackgroundColor", "SearchGamesPanelEditBoxBackgroundColor", &LightMode::searchTextBack},
        {"GamesListBackground", "GamesListBackgroundColor", "GamesBackgroundColor", &LightMode::listBack},
        {"GamesListSelectionBar", "TopColor", "GamesSelectionTopColor", &LightMode::selTop},
        {"GamesListSelectionBar", "BottomColor", "GamesSelectionBottomColor", &LightMode::selBottom},
        {"GamesListSelectionBar", "FrameColor", "GamesSelectionFrameColor", &LightMode::selFrame},
        {"GamesListSelectionBar", "FontColor", "GamesSelectionFontColor", &LightMode::selFont},
        {"GamesListSelectionBar", "MissROMsTopColor", "GamesSelectionMissROMsTopColor", &LightMode::selMissTop},
        {"GamesListSelectionBar", "MissROMsBottomColor", "GamesSelectionMissROMsBottomColor", &LightMode::selMissBottom},
        {"GamesListSelectionBar", "MissROMsFrameColor", "GamesSelectionMissROMsFrameColor", &LightMode::selMissFrame},
        {"GamesListSelectionBar", "MissROMsFontColor", "GamesSelectionMissROMsFontColor", &LightMode::selMissFont},
        {"GamesListSelectionBar", "InactiveTopColor", "GamesSelectionInactiveTopColor", &LightMode::selInactiveTop},
        {"GamesListSelectionBar", "InactiveBottomColor", "GamesSelectionInactiveBottomColor", &LightMode::selInactiveBottom},
        {"GamesListSelectionBar", "InactiveFrameColor", "GamesSelectionInactiveFrameColor", &LightMode::selInactiveFrame},
        {"GamesListSelectionBar", "InactiveFontColor", "GamesSelectionInactiveFontColor", &LightMode::selInactiveFont},
        {"GamesListStatusBar", "TopColor", "GamesListStatusBarTopColor", &LightMode::statusTop},
        {"GamesListStatusBar", "BottomColor", "GamesListStatusBarBottomColor", &LightMode::statusBottom},
        {"GamesListStatusBar", "FontColor", "GamesListStatusBarFontColor", &LightMode::statusFont},
        {"GamesListSplitter", "GamesListSplitterSingleColor", "GamesListSplitterSingleColor", &LightMode::splitterSingle},
        {"GamesListSplitter", "GamesListSplitterSingleColorHot", "GamesListSplitterSingleColorHot", &LightMode::splitterSingleHot},
        {"Images", "ImageBorderColor", "ImageBorderColor", &LightMode::imageBorder},
        {"Images", "HintBox_Color", "HintBox_Color", &LightMode::hintColor},
        {"Images", "SplitterSingleColor", "ImageSplitterSingleColor", &LightMode::imgSplitterSingle},
        {"Images", "SplitterSingleColorHot", "ImageSplitterSingleColorHot", &LightMode::imgSplitterSingleHot},
        {"Images", "HintBox_FrameColor", "HintBox_FrameColor", &LightMode::hintFrameColor},
        {"Images", "HintBox_TextColor", "HintBox_TextColor", &LightMode::hintText},
        {"Images", "HintBox_TextShadowColor", "HintBox_TextShadowColor", &LightMode::hintShadow},
        {"GameDocuments", "BackgroundColor", "GameDocumentsBackgroundColor", &LightMode::docsBack},
        {"GameDocuments", "BorderColor", "GameDocsBorderColor", &LightMode::docsBorder},
    };
    return f;
}

const std::vector<LightMode::BoolField>& LightMode::boolFields() {
    static const std::vector<BoolField> f = {
        {"ToolBar", "WindowsTheme", "ToolBarWindowsTheme", &LightMode::toolBarWindowsTheme},
        {"ToolBar", "GradientBar", "ToolBarGradientBar", &LightMode::toolBarGradient},
        {"GamesListBackground", "TileBackground", "GamesTileBackground", &LightMode::listTile},
        {"GamesListBackground", "BackgroundImageEnabled", "GamesBackgroundImageEnable", &LightMode::listImageEnabled},
        {"GamesListSelectionBar", "Gradient", "GameSelectionGradientBar", &LightMode::selGradient},
        {"GamesListSelectionBar", "AlphaBlend", "GameSelectionAlphaBlend", &LightMode::selAlphaBlend},
        {"GamesListSelectionBar", "RoundCorners", "GameSelectionRoundCorners", &LightMode::selRoundCorners},
        {"GamesListStatusBar", "Gradient", "GamesListStatusBarGradientBar", &LightMode::statusGradient},
        {"GamesListSplitter", "GamesListSplitterShowGripIcon", "GamesListSplitterShowGripIcon", &LightMode::splitterGrip},
        {"Images", "HintBox_FrameEnabled", "HintBox_FrameEnabled", &LightMode::hintFrame},
        {"Images", "SplitterShowGrip", "ImageSplitterShowGripIcon", &LightMode::imgSplitterGrip},
        {"Images", "HintBox_TextShadowEnabled", "HintBox_TextShadowEnabled", &LightMode::hintShadowOn},
        {"Images", "HintBox_FontItalicStyle", "HintBox_FontItalicStyle", &LightMode::hintItalic},
        {"GameDocuments", "ShowStatusBar", "GameDocsShowStatusBar", &LightMode::docsStatusBar},
    };
    return f;
}

bool LightMode::load(const QString& file) {
    WinIni ini;
    if (!ini.load(file)) return false;
    for (const IntField& f : intFields()) this->*(f.member) = ini.readInteger(f.section, f.key, this->*(f.member));
    for (const BoolField& f : boolFields()) this->*(f.member) = ini.readBool(f.section, f.key, this->*(f.member));
    listImage = ini.readString("GamesListBackground", "BackgroundImage", listImage);
    splitterStyle = ini.readInteger("GamesListSplitter", "GamesListSplitterStyle", splitterStyle);
    hintOpacity = ini.readInteger("Images", "HintBox_Opacity", hintOpacity);
    imgSplitterStyle = ini.readInteger("Images", "SplitterStyle", imgSplitterStyle);
    hintPosition = ini.readInteger("Images", "HintBox_Position", hintPosition);
    docsFontColor = ini.readInteger("GameDocuments", "FontColor", docsFontColor);
    docsFontName = ini.readString("GameDocuments", "FontName", docsFontName);
    docsFontSize = ini.readInteger("GameDocuments", "FontSize", docsFontSize);
    docsFontStyle = ini.readInteger("GameDocuments", "FontType", docsFontStyle);
    headerFontColor = ini.readInteger("GamesListHeader", "ColumnsHeaderFontColor", headerFontColor);
    headerFontName = ini.readString("GamesListHeader", "ColumnsHeaderFontName", headerFontName);
    headerFontSize = ini.readInteger("GamesListHeader", "ColumnsHeaderFontSize", headerFontSize);
    headerFontStyle = ini.readInteger("GamesListHeader", "ColumnsHeaderFontStyle", headerFontStyle);
    return true;
}

bool LightMode::save(const QString& file) const {
    WinIni ini;
    ini.load(file); // keep the settings of parts not ported yet
    for (const IntField& f : intFields()) ini.writeInteger(f.section, f.key, this->*(f.member));
    for (const BoolField& f : boolFields()) ini.writeInteger(f.section, f.key, (this->*(f.member)) ? 1 : 0);
    ini.writeString("GamesListBackground", "BackgroundImage", listImage);
    ini.writeInteger("GamesListSplitter", "GamesListSplitterStyle", splitterStyle);
    ini.writeInteger("Images", "HintBox_Opacity", hintOpacity);
    ini.writeInteger("Images", "SplitterStyle", imgSplitterStyle);
    ini.writeInteger("Images", "HintBox_Position", hintPosition);
    ini.writeInteger("GameDocuments", "FontColor", docsFontColor);
    ini.writeString("GameDocuments", "FontName", docsFontName);
    ini.writeInteger("GameDocuments", "FontSize", docsFontSize);
    ini.writeInteger("GameDocuments", "FontType", docsFontStyle);
    ini.writeInteger("GamesListHeader", "ColumnsHeaderFontColor", headerFontColor);
    ini.writeString("GamesListHeader", "ColumnsHeaderFontName", headerFontName);
    ini.writeInteger("GamesListHeader", "ColumnsHeaderFontSize", headerFontSize);
    ini.writeInteger("GamesListHeader", "ColumnsHeaderFontStyle", headerFontStyle);
    QDir().mkpath(QFileInfo(file).absolutePath());
    return ini.save(file);
}

QColor LightMode::qc(int tcolor) { return ElColorBox::toQColor(tcolor); }

QFont LightMode::headerFont() const {
    return GameFonts::fromDelphi(headerFontName, std::max(4, headerFontSize), headerFontStyle);
}

void LightMode::setHeaderFont(const QFont& f) {
    headerFontName = f.family();
    if (f.pointSize() > 0) headerFontSize = f.pointSize();
    headerFontStyle = (f.bold() ? 1 : 0) | (f.italic() ? 2 : 0) | (f.underline() ? 4 : 0) | (f.strikeOut() ? 8 : 0);
}

void LightMode::defaultHeaderFont() { // ELV_GetDefaultHeaderFont
    const LightMode d;
    headerFontName = d.headerFontName;
    headerFontSize = d.headerFontSize;
    headerFontStyle = d.headerFontStyle;
    headerFontColor = d.headerFontColor;
}

QFont LightMode::docsFont() const {
    QFont f(docsFontName);
    f.setPointSize(std::max(4, docsFontSize));
    f.setBold(docsFontStyle & 1);
    f.setItalic(docsFontStyle & 2);
    f.setUnderline(docsFontStyle & 4);
    f.setStrikeOut(docsFontStyle & 8);
    return f;
}

void LightMode::setDocsFont(const QFont& f) {
    docsFontName = f.family();
    docsFontSize = f.pointSize() > 0 ? f.pointSize() : docsFontSize;
    docsFontStyle = (f.bold() ? 1 : 0) | (f.italic() ? 2 : 0) | (f.underline() ? 4 : 0) | (f.strikeOut() ? 8 : 0);
}

} // namespace eld
