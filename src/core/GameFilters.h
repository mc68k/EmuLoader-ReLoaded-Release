// GameFilters - the games list's Miscellaneous Filters (Misc button; TFormArcadeFiltersExtra,
// TFormMain.ApplyMiscFilters) and Games Filters - MAME and Arcade (the "All Games" button;
// TFormArcadeGamesFilter, TFormMain.FilterGamesList).
//
// Miscellaneous Filters are saved in ini_files\games_filters.ini [Miscellaneous], with the
// original's keys. The Games Filters are listed in arcade\filters\mame_filters.ini ([main], then
// a section per maker); the one in use is [ListFilter] ArcadeGames, as in the original.
#pragma once

#include "ElDatabase.h"

#include <QSet>
#include <QString>
#include <QStringList>

namespace eld {

struct MiscFilters {
    // Lists: 0 = list all (both), then the choices in the window's order.
    int bios = 0;            // BiosNoBios: 1 games with BIOS ROMs, 2 without
    int deviceRoms = 0;      // DevicesNoDevices: 1 games with device ROMs, 2 without
    int gameRoms = 0;        // GamesROMs: 1 with ROMs (any), 2 with no ROMs/CHDs, 3 with no main set
    int audio = 0;           // AudioType: 1 standard, 2 discrete, 3 samples
    int interface = 0;       // InterfaceType: 1 classic (stick/buttons), 2 mechanical
    int orientation = 0;     // ScreenOrientation: 1 horizontal, 2 vertical, 3 unknown
    int saveState = 0;       // SaveState: 1 supported, 2 unsupported, 3 unknown
    int specialList = 0;     // SpecialList: 1 Special List games only, 2 hide them (GamesModel checks it)
    int cocktail = 0;        // Cocktail: 1 with cocktail mode, 2 without
    int artwork = 0;         // ArtworkRequired: 1 required, 2 not required
    QString control;         // the MAME Control Type ("joy8way", "none"; "" = list all)
    bool hideBiosSets = false, hideDeviceSets = false, hideChd = false, hideNoDump = false;
    bool neoGeoMvs = false, stvMultiSlot = false, mergedOnly = false, crcCollisionOnly = false;
    bool hideVgmplay = false, mainCpu = false; // Hide MAME Software List vgmplay Games; Filter MAME Games by Main CPU (its row)
    // Category: unchecked = hidden (catver.ini or category.ini needed).
    enum Category { Casino, FruitMachines, SlotMachines, Rhythm, Mature, Mahjong, Tabletop, Mechanical, BoardGame,
                    Quiz, Utilities, Calculator, Educational, Electronic, Printers, Phones, Music, ConsoleMachines,
                    ComputerMachines, HandheldMachines, CategoryCount };
    bool show[CategoryCount];

    MiscFilters();
    bool isDefault() const;
    bool operator==(const MiscFilters& o) const;
    bool operator!=(const MiscFilters& o) const { return !(*this == o); }

    static MiscFilters load(const QString& gamesFiltersIni);
    bool save(const QString& gamesFiltersIni) const;
    static const char* categoryKey(int c);

    // The games a multi-slot Neo-Geo MVS / ST-V takes (arcade\games\mame_multicart_neogeo.ini,
    // mame_multicart_stv.ini, see MultiSlot.h): the Neo-Geo MVS and ST-V filters keep only these
    // of the Neo-Geo / ST-V machines (a filter does nothing while its list isn't made).
    struct MultiSlot { QSet<QString> neoGeo, stv; };
    static MultiSlot loadMultiSlot(const QString& neoGeoFile, const QString& stvFile);

    bool accept(const MameGame& g, const MultiSlot& multiSlot, bool hbmame) const;
};

namespace ArcadeFilter {
// One of mame_filters.ini's filters ("allgames", "cps1", "sega", ...): does the game belong to it?
// An unknown name keeps every game, as in the original.
bool accept(const QString& filter, const MameGame& g, bool hbmame);
// The driver's file name without its folder and extension ("capcom/cps1.cpp" -> "cps1").
QString driverBase(const QString& driver);
}

} // namespace eld
