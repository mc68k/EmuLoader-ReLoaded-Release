// LightMode - Preferences page 2 ("Light Mode"), ini_files\lightmode.ini
// (TFormMain.Read/WriteLightModeSettings): colors of the tool bar, search panel, games
// list selection bar, status bar, list background and splitter. Colors are Delphi TColor
// values. Keys this port doesn't use are kept in the file.
#pragma once

#include <QColor>
#include <QFont>
#include <QString>
#include <vector>

namespace eld {

struct LightMode {
    // [ToolBar]
    bool toolBarWindowsTheme = true, toolBarGradient = true;
    int toolBarTop = 0xC0C0C0, toolBarBottom = 0xFFFFFF;
    // [SearchGamesPanel]
    int searchFilterFont = 0x000000, searchFieldFont = 0x000080, searchTextFont = 0x000000, searchTextBack = 0xFFFFFF;
    // [GamesListBackground]
    int listBack = 0xFFFFFF;
    bool listTile = true, listImageEnabled = false;
    QString listImage = "resources\\images\\el_bk.png";
    // [GamesListSelectionBar]
    bool selGradient = true, selAlphaBlend = true, selRoundCorners = true;
    int selTop = 0xFCDBC1, selBottom = 0xFBF5F1, selFrame = 0xB98C64, selFont = 0x000000;
    int selMissTop = 0xC1DBFC, selMissBottom = 0xDCEBFC, selMissFrame = 0x648CB9, selMissFont = 0x000080;
    int selInactiveTop = 0xD2D2C8, selInactiveBottom = 0xDCDCDC, selInactiveFrame = 0x8E8E8E, selInactiveFont = 0x000000;
    // [GamesListStatusBar]
    bool statusGradient = true;
    int statusTop = 0xEAD9CC, statusBottom = 0xFBF5F1, statusFont = 0x000000;
    // [GamesListSplitter]
    bool splitterGrip = true;
    int splitterStyle = 6, splitterSingle = 0x000000, splitterSingleHot = 0x808080;
    // [Images]
    int imageBorder = 0x000000;
    // [Images] HintBox_* - the image hint box
    int hintColor = 0xFF9933, hintFrameColor = 0xFF9933, hintText = 0xFFFFFF, hintShadow = 0x000000;
    bool hintFrame = true, hintShadowOn = true, hintItalic = false;
    int hintOpacity = 200, hintPosition = 4;
    // [Images] Splitter* - the image layout splitters
    int imgSplitterStyle = 1, imgSplitterSingle = 0x000000, imgSplitterSingleHot = 0x808080;
    bool imgSplitterGrip = true;
    // [GameDocuments] - the game documents text (FontType: Delphi TFontStyles bits, 1 bold,
    // 2 italic, 4 underline, 8 strike out; FontSize in points)
    int docsBack = 0xFFFFFF, docsBorder = 0x000000, docsFontColor = 0x000000, docsFontSize = 9, docsFontStyle = 0;
    QString docsFontName = "Calibri";
    bool docsStatusBar = false;
    // [GamesListHeader] ColumnsHeaderFont* - the games list's column titles (Customize Columns
    // Header Font)
    QString headerFontName = "Segoe UI";
    int headerFontSize = 9, headerFontStyle = 0, headerFontColor = 0x000000;
    QFont headerFont() const;
    void setHeaderFont(const QFont& f);
    void defaultHeaderFont();

    bool load(const QString& file);
    bool save(const QString& file) const;
    static QColor qc(int tcolor);
    QFont docsFont() const;
    void setDocsFont(const QFont& f);

    // name -> member, for the Preferences window (ElColorBox / check box object names)
    struct IntField { const char* section; const char* key; const char* control; int LightMode::*member; };
    struct BoolField { const char* section; const char* key; const char* control; bool LightMode::*member; };
    static const std::vector<IntField>& intFields();
    static const std::vector<BoolField>& boolFields();
};

} // namespace eld
