#include "GameFonts.h"

#include <QCoreApplication>
#include "WinIni.h"

#include <QDir>
#include <QFileInfo>

namespace eld {

static const char* kSections[GameFonts::Count] = {"Parent Game", "Clone Game", "Preliminary Game", "Missing ROMs Game",
                                                   "Missing ROMs Preliminary Game"};

GameFonts::GameFonts() {
    for (int k = 0; k < Count; ++k) setDefault(Kind(k));
    allSystems = font[Parent];
    allSystemsColor = color[Parent];
}

const char* GameFonts::section(Kind k) { return kSections[k]; }

QString GameFonts::title(Kind k) {
    static const char* t[Count] = {QT_TRANSLATE_NOOP("GameFonts", "Parent Set"), QT_TRANSLATE_NOOP("GameFonts", "Clone Set"),
                                   QT_TRANSLATE_NOOP("GameFonts", "Preliminary Set"), QT_TRANSLATE_NOOP("GameFonts", "Missing ROMs/CHDs"),
                                   QT_TRANSLATE_NOOP("GameFonts", "Missing ROMs, Preliminary")};
    return QCoreApplication::translate("GameFonts", t[k]);
}

void GameFonts::setDefault(Kind k) {
    // Defaults from ReadCustomGameFontFile (light mode): Segoe UI 9, preliminary sets gray and
    // struck out (style 8), sets with missing ROMs red.
    int style = (k == Preliminary || k == MissingRomsPreliminary) ? 8 : 0;
    font[k] = fromDelphi("Segoe UI", 9, style);
    if (night)
        color[k] = k == Preliminary ? QColor(0xc0, 0xc0, 0xc0)
                 : (k == MissingRoms || k == MissingRomsPreliminary) ? QColor(250, 80, 80) : QColor(Qt::white);
    else
        color[k] = k == Preliminary ? QColor(0x80, 0x80, 0x80)
                 : (k == MissingRoms || k == MissingRomsPreliminary) ? QColor(Qt::red) : QColor(Qt::black);
}

void GameFonts::setNight(bool on) {
    night = on;
    for (int k = 0; k < Count; ++k) setDefault(Kind(k));
    allSystems = font[Parent];
    allSystemsColor = color[Parent];
}

QFont GameFonts::fromDelphi(const QString& name, int size, int style) {
    QFont f(name);
    f.setPointSize(size > 0 ? size : 9);
    // TFontStyles as a byte: fsBold=1, fsItalic=2, fsUnderline=4, fsStrikeOut=8
    f.setBold(style & 1);
    f.setItalic(style & 2);
    f.setUnderline(style & 4);
    f.setStrikeOut(style & 8);
    return f;
}

int GameFonts::styleOf(const QFont& f) {
    return (f.bold() ? 1 : 0) | (f.italic() ? 2 : 0) | (f.underline() ? 4 : 0) | (f.strikeOut() ? 8 : 0);
}

QColor GameFonts::fromTColor(int v, const QColor& fallback) {
    if (v < 0) { // system color ($FF0000xx): use the usual Windows values
        switch (v & 0xFF) {
        case 5: return Qt::white;                    // clWindow
        case 8: case 18: case 7: return Qt::black;   // clWindowText, clBtnText, clMenuText
        case 17: return QColor(0x6d, 0x6d, 0x6d);    // clGrayText
        case 13: return QColor(0x00, 0x78, 0xd7);    // clHighlight
        case 14: return Qt::white;                   // clHighlightText
        default: return fallback;
        }
    }
    return QColor(v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF); // TColor is $00BBGGRR
}

int GameFonts::toTColor(const QColor& c) { return c.red() | (c.green() << 8) | (c.blue() << 16); }

GameFonts::Kind GameFonts::kindOf(const MameGame& g) {
    bool prelim = g.driverStatus == 2;
    if (g.setStatus == 1) return prelim ? MissingRomsPreliminary : MissingRoms;
    if (prelim) return Preliminary;
    return g.cloneOf.isEmpty() ? Parent : Clone;
}

bool GameFonts::load(const QString& file) {
    WinIni ini;
    if (!ini.load(file)) return false;
    for (int k = 0; k < Count; ++k) {
        const char* s = kSections[k];
        if (!ini.sectionExists(s)) continue;
        QFont def = font[k];
        font[k] = fromDelphi(ini.readString(s, "FontName", def.family()), ini.readInteger(s, "FontSize", def.pointSize()),
                             ini.readInteger(s, "FontStyle", styleOf(def)));
        color[k] = fromTColor(ini.readInteger(s, "FontColor", toTColor(color[k])), color[k]);
    }
    hasAllSystems = ini.sectionExists("All Systems");
    allSystems = font[Parent];
    allSystemsColor = color[Parent];
    if (hasAllSystems) {
        allSystems = fromDelphi(ini.readString("All Systems", "FontName", allSystems.family()),
                                ini.readInteger("All Systems", "FontSize", allSystems.pointSize()),
                                ini.readInteger("All Systems", "FontStyle", styleOf(allSystems)));
        allSystemsColor = fromTColor(ini.readInteger("All Systems", "FontColor", 0), allSystemsColor);
    }
    return true;
}

bool GameFonts::save(const QString& file) const {
    WinIni ini;
    ini.load(file); // keep the console/computer system sections untouched
    for (int k = 0; k < Count; ++k) {
        const char* s = kSections[k];
        ini.writeInteger(s, "FontColor", toTColor(color[k]));
        ini.writeString(s, "FontName", font[k].family());
        ini.writeInteger(s, "FontSize", font[k].pointSize());
        ini.writeInteger(s, "FontStyle", styleOf(font[k]));
    }
    // "All Systems" (the list's base font) is a setting of its own: only created when missing,
    // as the parent font, which is what EmuLoader does by default.
    if (!ini.sectionExists("All Systems")) {
        ini.writeInteger("All Systems", "FontColor", toTColor(color[Parent]));
        ini.writeString("All Systems", "FontName", font[Parent].family());
        ini.writeInteger("All Systems", "FontSize", font[Parent].pointSize());
        ini.writeInteger("All Systems", "FontStyle", styleOf(font[Parent]));
    }
    QDir().mkpath(QFileInfo(file).absolutePath());
    return ini.save(file);
}

} // namespace eld
