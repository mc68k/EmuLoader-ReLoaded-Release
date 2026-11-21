// MAME .ini files and MAME's per-machine .ini chain.
//
// MameIniFile keeps comments and layout, rewriting only edited lines in
// EmuLoader's format ("%-25s %s", the same as MAME's own -createconfig).
//
// MameIniChain names the files MAME merges when a machine starts, in order.
// It is a port of TFormMain.GetCustomIniFileMAME / LoadCustomSettingsFiles:
//   0 mame.ini, 1 debug, 2 vertical, 3 horizont, 4 arcade, 5 console,
//   6 computer, 7 othersys, 8 raster/vector/lcd/svg, 9 source/<driver>,
//   10 <bios>, 11 <parent>, 12 <machine>
#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace eld {

class MameIniFile {
public:
    bool load(const QString& path);
    void parse(const QString& text);
    bool save(const QString& path) const;
    QString toString() const;

    bool has(const QString& key) const { return find(key) >= 0; }
    QString get(const QString& key, const QString& def = {}) const;
    bool set(const QString& key, const QString& value);  // true if changed
    bool remove(const QString& key);
    QStringList keys() const;
    bool empty() const;
    void clear() { lines_.clear(); }
    void addHeader(const QString& comment);

    static QString formatLine(const QString& key, const QString& value);

private:
    struct Line { QString raw, key, value; bool dirty = false; };
    int find(const QString& key) const;
    QVector<Line> lines_;
    bool crlf_ = false; // written back with the line endings it was read with
};

struct ChainMachine {
    QString name, cloneOf, bios, driver; // driver = "pacman.cpp"
    int orientation = -1;   // 0 horizontal, 1 vertical
    int screenType = -1;    // 0 raster, 1 vector, 2 lcd, 3 svg
    int systemType = 0;     // 0 arcade, 1 console/computer
    QString softwareName;   // set for software-list items
};

class MameIniChain {
public:
    static constexpr int kLevels = 13;
    static QString levelLabel(int level);

    // Base name used at a level ("" = level does not apply to this machine).
    static QString baseName(int level, const ChainMachine& m);

    // Full path of the level's file. mameBuild is the minor version (e.g. 270 for 0.270).
    static QString path(int level, const ChainMachine& m, const QString& mameIni, const QString& iniDir, int mameBuild);

    // Values as MAME would see them after merging levels 0..uptoLevel (inclusive).
    static QMap<QString, QString> effective(const ChainMachine& m, const QString& mameIni, const QString& iniDir,
                                            int mameBuild, int uptoLevel);

    // Folder MAME reads per-machine .ini files from: the first inipath entry
    // ("." means "ini"), relative to the MAME folder. Port of EmuLoader's logic.
    static QString iniDir(const MameIniFile& mameIni, const QString& mameFolder);
};

QStringList splitPathList(const QString& s);

} // namespace eld
