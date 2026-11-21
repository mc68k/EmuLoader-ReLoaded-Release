// MultiSlot - several arcade games in one multi-slot machine (the original's "Multi-Slot
// Machines", ValidateMultiSlot, uArcadeMultiSlotGames.pas).
//
// Some arcade machines take several cartridges at once, each game in a slot:
//   Neo-Geo MVS (neogeo, 6 slots; MAME's other boards: ng_mv4f 4, ng_mv2f 2 ... see below),
//   Sega ST-V (stvbios, 4), Sega Mega-Tech (megatech, 8), Nintendo PlayChoice-10 (playch10, 10).
// With several games of one of these selected, Run Game asks for the order of the slots (and,
// for the Neo-Geo, the board) and runs: neogeo -cart1 mslug -cart2 kof98 ...
//
// The games a Neo-Geo or an ST-V takes are those of MAME's own software lists for it
// (hash\neogeo.xml, hash\stv.xml, the <software> not supported="no"), made with the games list:
//   arcade\games\mame_multicart_neogeo.ini, mame_multicart_stv.ini   (one name per line)
// The Neo-Geo boards (and the last one used):
//   arcade\mame_multislot_machines.txt
//     [neogeo]  <machine>=<title>Neo-Geo MV-6F/><year>1990/><slots>6/><videochipset>.../><notes>.../>
//     [lastused] neogeo=<machine>
// HBMAME's are the same with "hbmame".
#pragma once

#include "ElDatabase.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace eld {

class MultiSlot {
public:
    // The multi-slot machine a game goes in: "neogeo" (BIOS neogeo, or the neopcb driver),
    // "stv", "megatech", "playch10" (their drivers); "" for the others.
    static QString machineOf(const MameGame& g);
    static int maxSlots(const QString& machine);  // 6, 4, 8, 10 (0: not a multi-slot machine)
    static QString title(const QString& machine); // "Neo-Geo MVS (Multi Video System)" ...
    // The cartridge lists of the Neo-Geo and the ST-V (the only ones that have them)
    static bool hasCartList(const QString& machine) { return machine == "neogeo" || machine == "stv"; }
    static QString cartFile(const QString& gamesFolder, const QString& systemBase, const QString& machine);
    // Reads MAME's <hash>\<machine>.xml and writes the cart file (false: the XML can't be read)
    static bool createCartList(const QString& xmlFile, const QString& file);
    static QSet<QString> loadCartList(const QString& file);

    // The games of `selected` (in order) that can go in `machine`'s slots: the machine's own games
    // you have, in its cartridge list when it has one; not the machine itself, nor software list
    // games. At most maxSlots(machine).
    static QVector<MameGame> slotGames(const QString& machine, const QVector<MameGame>& selected, const QSet<QString>& carts);

    // The Neo-Geo boards (mame_multislot_machines.txt [neogeo]), the default one first
    struct Board {
        QString name, title, year, chipset, notes;
        int slotCount = 0;
    };
    static QVector<Board> boards(const QString& machinesFile);
    static QString lastBoard(const QString& machinesFile);   // [lastused] neogeo= ("neogeo" if none)
    // Writes [lastused] neogeo=<board>, the rest of the file as it was (copied from `from` when
    // the file isn't there yet).
    static bool setLastBoard(const QString& machinesFile, const QString& board, const QString& from = {});

    // What runs the games: the board for the Neo-Geo, stvbios for the ST-V, else the machine
    static QString machineToRun(const QString& machine, const QString& board);
    // The command line after the emulator: {machine, "-cart1", game1, "-cart2", game2 ...}
    static QStringList commandLine(const QString& machineToRun, const QStringList& games);
};

} // namespace eld
