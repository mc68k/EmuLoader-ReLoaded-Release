// GameFonts - the games list fonts, port of TFormMain.Read/WriteCustomGameFontFile.
// Stored in <data>\ini_files\sysgamecustomfont.ini, one section per kind of set:
//   [Parent Game] [Clone Game] [Preliminary Game] [Missing ROMs Game] [Missing ROMs Preliminary Game]
// with FontName, FontSize (points), FontColor (Delphi TColor) and FontStyle (TFontStyles byte).
#pragma once

#include "ElDatabase.h"

#include <QColor>
#include <QFont>
#include <QString>

namespace eld {

class GameFonts {
public:
    enum Kind { Parent, Clone, Preliminary, MissingRoms, MissingRomsPreliminary, Count };

    GameFonts();
    static const char* section(Kind k);
    static QString title(Kind k); // as shown in the font settings window

    bool load(const QString& file);
    bool save(const QString& file) const; // keeps other sections (console/computer systems)
    void setDefault(Kind k);
    // Night Mode's defaults (ReadCustomGameFontFile(True)): white sets, silver preliminary ones,
    // light red for missing ROMs
    void setNight(bool on);
    bool night = false;

    // Which font a game is drawn with (TFormMain.GetCanvasFont logic).
    static Kind kindOf(const MameGame& g);

    QFont font[Count];
    QFont allSystems;          // [All Systems]: the list's base font (GamesListView.Font)
    QColor allSystemsColor;
    bool hasAllSystems = false;
    QColor color[Count];

    static QColor fromTColor(int v, const QColor& fallback);
    static int toTColor(const QColor& c);
    static QFont fromDelphi(const QString& name, int size, int style);
    static int styleOf(const QFont& f);
};

} // namespace eld
