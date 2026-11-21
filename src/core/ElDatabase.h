// ElDatabase - EmuLoader's MAME games database.
//
// Reads and writes the same files as the Delphi EmuLoader, so an existing
// installation's lists and scan results carry over (and stay usable by it):
//   <EmuLoader>\arcade\games\mame.el        machine list    (docs/arcade/el-listformat.txt)
//   <EmuLoader>\arcade\games\mame.elstatus  have/miss status per set
// It can also build those files from "mame -listxml" plus a quick ROM scan.
#pragma once

#include "RunExtras.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>

namespace eld {

enum class SetKind : quint8 { Parent, Clone, Bios, Device };

struct MameGame {
    QString name, title, year, manufacturer;
    QString controls;          // "joy8way,trackball"
    QString resolution, refresh;
    QString bios, cloneOf, driver; // driver = source file, "pacman/pacman.cpp" (or "pacman.cpp")
    // MAME's romof when it's neither the parent nor a BIOS: a device whose ROMs are another
    // device's (qsound_hle's are qsound's). This version's <romof> tag.
    QString romOf;
    // The chip MAME tags "maincpu" ("Z80", "MC68000"): the CPU filter. This version's <maincpu> tag
    // (the original keeps it in <system>_maincpu.ini).
    QString mainCpu;
    QString driverText(bool withFolder) const; // what the games list shows
    qint64 size = 0;
    qint16 buttons = -1, players = -1;
    QString language;          // AntoPISA's languages.ini (arcade\filters): the first language listed
    QString playersIni;        // nplayers.ini (arcade\filters): "2P alt", "4P sim", ...
    qint8 mediaType = 0;       // 0 ROM, 1 CHD
    qint8 screenType = -1;     // 0 raster, 1 vector, 2 LCD, 3 SVG/unknown
    qint8 orientation = -1;    // 0 horizontal, 1 vertical
    qint8 audioType = 0;       // 0 standard, 1 discrete, 2 samples
    qint8 driverStatus = -1;   // 0 good, 1 imperfect, 2 preliminary
    qint8 emuStatus = -1, colorStatus = -1, soundStatus = -1, graphicStatus = -1;
    // protectionstat, cocktailstat, lanstat in the .el (the original's eProtectionStatus,
    // eCocktailStatus, eNetwork): same 0/1/2 values, -1 = not given
    qint8 protectionStatus = -1, cocktailStatus = -1, lanStatus = -1;
    qint8 saveState = -1;      // 0 unsupported, 1 supported
    bool mechanical = false;
    bool requiresArtwork = false;  // <reqartwork>
    // What the machine's ROMs are (the Miscellaneous Filters' ROM filters), from -listxml when the
    // list is made: <romflags> in this version's .el, 0 = not known (a list made before).
    // OwnFilesKnown / OwnFiles: files of its own (ROMs or disks that aren't its parent's, so a
    // <name>.zip or <name>\ in split sets); a clone without any is all in its parent's set.
    enum RomFlag : quint8 { RomsKnown = 1, AnyRoms = 2, GameRoms = 4, DeviceRoms = 8, AllNoDump = 16, CrcCollision = 32,
                            OwnFilesKnown = 64, OwnFiles = 128 };
    quint8 romFlags = 0;
    bool romFlag(RomFlag f) const { return romFlags & f; }
    QString category, versionAdded; // from catver.ini (not stored in the .el)
    QString section;           // .el section it was read from ("devices", "bios", parent name)
    QString raw;               // the line as read; written back unchanged so unknown tags survive
    qint8 systemType = 0;      // 0 arcade, 1 console/computer (MAME "mess" machines)
    QStringList softwareLists; // from <system>.elsoftlist (machines that run software lists)
    // MAME software list games (arcade\mame_softwarelist_games\<list>.el, SoftwareLists.h): the
    // list a game is in ("" for MAME's machines) and what the list's file in MAME's hash folder
    // says about it. The list's description is also the game's category, as in the original.
    QString softwareList;      // eSoftwareName: "nes"
    QString softwareTitle;     // the list's description: "Nintendo Entertainment System cartridges"
    QString media;             // <media>: the option that loads it, "cart1" (eSoftwareExecParameter)
    QString usage;             // <info name="usage">: "Requires a Japanese system"
    QString compat;            // <sharedfeat name="compatibility">: "NTSC"
    QString notes, altTitle;   // <notes>, <info name="alt_title">
    bool isSoftware() const { return !softwareList.isEmpty(); }
    // Its name in favorites, played games and the Special List (GetPlayedGamesNameEntry): the
    // name, or "<list>\<name>" for a software list game, as two lists can hold the same name.
    QString key() const { return softwareList.isEmpty() ? name : softwareList + '\\' + name; }
    // Its pictures: <folder>\<list>\<name>.png for a software list game (as the original keeps them).
    QString imageName() const { return softwareList.isEmpty() ? name : softwareList + '/' + name; }
    QString imageParent() const { return cloneOf.isEmpty() || softwareList.isEmpty() ? cloneOf : softwareList + '/' + cloneOf; }
    // play statistics (arcade\played_games\<system>.txt; not stored in the .el)
    int played = 0;
    qint32 lastPlayed = 0;     // DOS file date
    qint64 playtime = 0;       // milliseconds
    SetKind kind = SetKind::Parent;
    // .elstatus
    qint8 setStatus = 2;       // 0 OK, 1 missing ROMs but set file found, 2 missing
    qint8 scanMode = 1;        // 0 full, 1 quick, 2 forced
    bool merged = false;

    bool have() const { return setStatus != 2; }
    int gameId() const;        // EmuLoader "Game ID" (00..07 for MAME)
};

class ElDatabase {
public:
    using Progress = std::function<void(int done)>;

    // Load mame.el (+ .elstatus if present). Returns false if the list is missing.
    bool load(const QString& elFile, const QString& statusFile, QString* error = nullptr);
    bool save(const QString& elFile, const QString& statusFile) const;
    bool saveStatus(const QString& statusFile) const;  // only the have/miss status (.elstatus)

    // Machines and their software lists: arcade\games\mame.elsoftlist, one "[machine]" section
    // per machine followed by its software list names. A machine with software lists is a
    // console/computer (eSystemType 1), as are the names in AntoPISA's arcade\filters\mess.ini.
    bool loadSoftwareLists(const QString& file);
    bool saveSoftwareLists(const QString& file) const;
    int loadMessIni(const QString& file);

    // Category / Version Added from catver.ini ([Category] and [VerAdded]).
    bool loadCatver(const QString& file);
    // Number of players from Nomax's nplayers.ini ("name=2P alt" lines; eNumberPlayersIni).
    int loadNPlayers(const QString& file);
    // Language from AntoPISA's languages.ini: [English] sections listing set names (LoadLanguagesList).
    int loadLanguages(const QString& file);

    // [info_file] header of the list: "MAME Games List" / emulator version.
    QString description, version;

    // Build the list from "mame -listxml" output (streamed line by line).
    bool fromListXml(const std::function<bool(QString&)>& nextLine, const Progress& progress = {},
                     const std::atomic<bool>* cancel = nullptr);

    // BIOS sets of the BIOS machines, from -listxml <biosset> (arcade\games\mame_biossets.ini:
    // [Descriptions] bios=title, then [bios] default_bios=<set> and <set>=<description>).
    struct BiosSets { QString bios, title, defaultSet; QVector<QPair<QString, QString>> sets; };
    QVector<BiosSets> biosSets;
    bool saveBiosSets(const QString& file) const;

    // Machines with memory card slots, from -listxml <device type="memcard"> (ParseMemCardDeviceTag;
    // arcade\games\mame_memcard_support.ini, see RunExtras.h).
    QList<MemoryCardSlots> memoryCards;

    // The RAM sizes a machine can have (computers), from -listxml <ramoption name="64K"
    // default="yes">65536</ramoption>: arcade\games\mame_ramsize.ini, as the original writes it
    // ([apple2e] default=64K, then 64K=65536 ...), for MAME Settings > RAM Size.
    struct RamSizes { QString machine, defaultName; QVector<QPair<QString, QString>> options; };
    QVector<RamSizes> ramSizes;
    bool saveRamSizes(const QString& file) const;
    static RamSizes loadRamSizes(const QString& file, const QString& machine); // options empty: none

    // Machines that play audio samples, and the samples set each uses (-listxml sampleof, or the
    // machine's own name): arcade\games\mame.elsamples, "name=samples" lines, as the original.
    QHash<QString, QString> samples;
    bool saveSamples(const QString& file) const;
    static QHash<QString, QString> loadSamples(const QString& file);

    // Quick scan: a set is available when <name>.zip/.7z or folder <name> is in a ROM folder.
    // MAME's machines only (SoftwareLists::quickScan scans the software list games).
    // only: those games (by name), else all.
    void quickScan(const QStringList& romFolders, const QSet<QString>* only = nullptr);

    // The software list games, listed after the machines (SoftwareLists::load). They are never
    // written to the machine list or its status file.
    void setSoftware(QVector<MameGame> games);
    int softwareCount() const { return softwareCount_; }

    QVector<MameGame>& games() { return games_; }
    const QVector<MameGame>& games() const { return games_; }
    // By key(): a machine's name, or "<list>\<name>" for a software list game.
    int indexOf(const QString& key) const { return index_.value(key, -1); }
    const MameGame* find(const QString& key) const;

private:
    void reindex();
    QVector<MameGame> games_;
    QHash<QString, int> index_;
    int softwareCount_ = 0;
    bool utf8_ = false; // written back in the encoding it was read in (new lists: ANSI)
};

} // namespace eld
