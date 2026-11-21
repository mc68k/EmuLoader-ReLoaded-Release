// SoftwareLists - MAME's software list games (the original's "MAME Software List Games").
//
// MAME's hash folder (hashpath in mame.ini, "hash" by default) holds one <list>.xml per software
// list: the cartridges, floppies, CDs ... that MAME's consoles and computers run. The machines
// that run each list come from -listxml (<softwarelist name="nes"/>, ElDatabase::softwareLists).
// The games' files go in a folder named after the list in a ROM folder: roms\nes\smb.zip.
//
// The files, in the original's formats (CreateSoftwareListXML, GetSoftListRequirementFile ...):
//   arcade\mame_softwarelist_games\<list>.el        the list's games:
//       [info_file] description=<the list's description> xmlfile_datetime= xmlfile_crc32=
//       then a section per parent with it and its clones, "name=MDE<title>..../><media>cart1/>"
//       (M media type 0 ROM/1 CHD, D driver status, E emulation status: 0 good, 1 imperfect
//       from supported="partial", 2 preliminary from supported="no").
//   arcade\mame_softwarelist_games\<list>.elstatus  name=IISMm, as the machines' .elstatus
//   arcade\games\mame.elsoftlistreq                 [<list>] game=<list>:<software> (requirement)
//   arcade\mame_softlist_exclude.txt                the lists left out (one name per line)
//   arcade\mame_machines_usercustom.ini             <list>=<machine> last used to run a list
// HBMAME's are the same with "hbmame".
#pragma once

#include "ElDatabase.h"

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace eld {

class SoftwareLists {
public:
    // ---- where things are (systemBase: "mame" or "hbmame")
    static QString gamesFolder(const QString& userDir, const QString& systemBase);   // <user>/arcade/mame_softwarelist_games/
    static QString requirementFile(const QString& machinesFolder, const QString& systemBase); // <arcade/games>/mame.elsoftlistreq
    static QString excludeFile(const QString& userDir, const QString& systemBase);    // <user>/arcade/mame_softlist_exclude.txt
    static QString machinesFile(const QString& userDir, const QString& systemBase);   // <user>/arcade/mame_machines_usercustom.ini
    // ReadMAMEHashFolder: hashpath from mame.ini (the first folder when there are several), else
    // "hash", relative to MAME's folder.
    static QString hashFolder(const QString& mameIni, const QString& mameFolder);

    // ---- making the lists
    // Requirements: [<list>] <game>=<list>:<software> (or just <software>).
    struct Requirements {
        QMap<QString, QMap<QString, QString>> lists;
        bool load(const QString& file);
        bool save(const QString& file) const;
        QString value(const QString& list, const QString& game) const { return lists.value(list).value(game); }
    };
    // Reads <hash>\<list>.xml and writes <folder>\<list>.el and .elstatus (every game missing
    // until a scan). The list's requirements replace its old ones in `req`. False when the XML
    // file can't be read (error says why).
    static bool create(const QString& xmlFile, const QString& list, const QString& folder, bool hbmame,
                       Requirements& req, QString* error = nullptr);
    // SoftListValidateListsXML (update mode): the list's .el was made from this very XML file
    // (same CRC-32), so it needn't be made again.
    static bool upToDate(const QString& xmlFile, const QString& folder, const QString& list);
    static QString fileCrc32(const QString& file);  // "1A2B3C4D", "" if it can't be read
    static QStringList made(const QString& folder);  // the lists with a .el in the folder, sorted
    static QStringList excluded(const QString& file);
    static bool saveExcluded(const QString& file, const QStringList& lists);
    // SoftListDeleteGamesFiles: a list's files (.el, .elrom, .elstatus, .miss).
    static void remove(const QString& folder, const QString& list);

    // ---- the games
    // The list's games (MameGame::softwareList set), with their status from <list>.elstatus.
    static QVector<MameGame> load(const QString& folder, const QString& list);
    // All the lists in the folder but the excluded ones, one after the other.
    static QVector<MameGame> loadAll(const QString& folder, const QStringList& excluded);
    // The .elstatus of each list that has games in `games` (software list games only).
    static bool saveStatus(const QString& folder, const QVector<MameGame>& games, bool hbmame);
    // Quick scan: a game is there when <rom folder>\<list>\<name>.zip / .7z or the folder
    // <list>\<name> (its CHDs) is. Games "forced available" stay as they are.
    static void quickScan(QVector<MameGame>& games, const QStringList& romFolders, const QSet<QString>* only = nullptr); // only: those keys

    // ---- running a game
    // The machine last chosen for a list (GetLastUsedMachineSoftwareList), "" if none.
    static QString lastMachine(const QString& machinesFile, const QString& list);
    static bool setLastMachine(const QString& machinesFile, const QString& list, const QString& machine);
    // GetSoftListGameRequiredParam: what goes before the game for its requirement, as MAME
    // arguments: {"-cart1", "scharger"}; empty when it has none.
    static QStringList requirementArgs(const Requirements& req, const QString& folder, const QString& list,
                                       const QString& game);
    // The command line after the emulator (ExecuteGame for software list games): the machine,
    // the custom parameters, the requirement, then "-<media> <game>". Custom parameters that
    // don't start with "-" start with the machine to use instead (it's returned in machineUsed).
    // A <game> tag in them is replaced by the game, which then isn't added at the end.
    static QStringList commandLine(const QString& machine, const QString& customParams, const QStringList& requirement,
                                   const QString& media, const QString& game, QString* machineUsed = nullptr);
    // The machine named at the start of custom parameters ("" when they start with "-").
    static QString customMachine(const QString& customParams);

    // ---- one game's files (Game Details)
    struct File {
        QString name, crc, sha1, status, part; // status: "", "baddump", "nodump"; part: "cart", "flop1" ...
        qint64 size = -1;
        bool disk = false;
    };
    static QVector<File> files(const QString& xmlFile, const QString& game);

    // Customize MAME Software List (GetSoftListFileTitle): a list's description and media type
    // (1 cartridge, 2 disc, 3 floppy disk, 4 cassette tape, 5 hard disk, 6 video game music; 0 =
    // a list with no software in it, which isn't shown).
    struct ListInfo { QString name, description; int media = 0; };
    static ListInfo listInfo(const QString& xmlFile);
    static QVector<ListInfo> hashLists(const QString& hashFolder); // every list in the folder, by name

    // FixMediaParameterStr: MAME's option for a <part name="...">: "cart" -> "cart1", "flop" ->
    // "flop1", "disk..." -> "flop1", "cdrom..." -> "cdrom" ...
    static QString mediaOption(const QString& part);
};

} // namespace eld
