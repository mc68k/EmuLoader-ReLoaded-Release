// GamesModel - the games list (TEasyListview "GamesListView" in the original),
// with EmuLoader's columns, icons and colors, plus the toolbar filters.
#pragma once

#include <functional>

#include "ElDatabase.h"
#include "GameFilters.h"
#include "GameFonts.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QIcon>
#include <QSet>
#include <QSortFilterProxyModel>

namespace eld {

class GamesModel : public QAbstractTableModel {
    Q_OBJECT
public:
    // Columns are EmuLoader's aColumns (uCommon.pas), same order, ini names and widths, so
    // ini_files\columns_profile.ini applies unchanged.
    enum Column { Title, Year, Manufacturer, Orientation, Resolution, RefreshRate, Category, VersionAdded, Name, Clone,
                  DriverName, Players, DriverStatus, EmulationStatus, ColorStatus, SoundStatus, GraphicStatus, Played,
                  Language, GameSize, LastPlayed, Playtime, SoftwareName, Special, ColumnCount };
    static const char* columnKey(int c);   // "Title", "RefreshRate", ... (ini key)
    static QString columnTitle(int c); // "Title", "Refresh Rate", ...
    static int defaultWidth(int c);

    GamesModel(ElDatabase& db, const QString& iconFolder, QObject* parent = nullptr);
    void reload();                       // after the database changed
    void refreshIcons() { if (rowCount()) emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {Qt::DecorationRole}); }
    void refreshRow(int row) { emit dataChanged(index(row, 0), index(row, ColumnCount - 1)); }
    const MameGame& game(int row) const { return db_.games()[row]; }
    // MAMu_ icons: the game's own icon when there's one (a null icon: the list's own)
    std::function<QIcon(const MameGame&)> customIcon;
    int rowOf(const QString& name) const { return db_.indexOf(name); }
    // Grouped view: a parent and its clones share a group, under the parent's name
    // (a software list game's: "<list>\\<parent>", as two lists can hold the same names)
    static QString groupKey(const MameGame& g) {
        const QString& p = g.cloneOf.isEmpty() ? g.name : g.cloneOf;
        return g.isSoftware() ? g.softwareList + '\\' + p : p;
    }
    QSet<QString>& favorites() { return favorites_; }
    QSet<QString>& special() { return special_; } // the Special List (ini_files\special.txt)
    GameFonts& fonts() { return fonts_; }
    void setFonts(const GameFonts& f);
    void setStatusIconFolder(const QString& f) { statusFolder_ = f; }
    void setRowHeight(int h);            // CellSizes.Report.Height (ELV_CellSize)

    // Preferences that change how the list is drawn (EmuLoader.ini [Preferences]).
    struct Options {
        bool statusIcons = true;       // !DisableStatusIcons: icon in the status columns
        bool hideStatusTexts = false;  // ShowHideStatusTexts
        bool statusFirstLetter = false;// ShowStatusFirstLetterOnly
        bool shortStatusTitles = false;// ShortDriverStatusColumnTitles
        bool leadingZero = true;       // GameAddLeadingZeroVersionInfo: ".123" -> "0.123"
        bool grayscaleMissRoms = false;// ShowGrayscaleIconGamesMissROMsCHDs
        bool hideSecondsPlayed = false;   // HideSecondsLastPlayed
        bool hideSecondsPlaytime = false; // HideSecondsTotalPlaytime
        bool driverPath = true;         // [EmuLoaderRL] ShowDriverPath: "sony/zn1.cpp" or "zn1.cpp"
        bool favoriteIcon = false;      // [Preferences] ShowFavIconInGame: a star before favorites' titles
        bool altTitleFormat = false;    // [Preferences] NewTitleFormat: "Simpsons, The"
        bool playersIniOnly = false;    // [Preferences] PlayersInfoFromIniOnly (GetNumberPlayersInfo)
        int titleExtra = 0;             // [Preferences] TitleRemoveExtraText: the "(...)" after the title
                                        // left out of parents (1), clones (2) or all (3)
    };
    // The title as listed, searched and sorted: "The Simpsons" -> "Simpsons, The" with the
    // alternate format (SetDescriptionFormat).
    QString title(const MameGame& g) const;
    // The title as shown in the Title column: without the extra info when that's chosen
    // (SetCustomGameTitle; sorting still uses title()).
    QString shownTitle(const MameGame& g) const;
    // Number of Players (GetNumberPlayersInfo): nplayers.ini's, else MAME's count, unless
    // "Show Players Info From nplayers.ini Only".
    QString playersText(const MameGame& g) const;
    void setOptions(const Options& o);
    const Options& options() const { return options_; }

    int rowCount(const QModelIndex& = {}) const override;
    int columnCount(const QModelIndex& = {}) const override { return ColumnCount; }
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

    // The games list icon of a game (resources\gameslist_icons)
    QIcon icon(const MameGame& g) const;

private:
    ElDatabase& db_;
    QString iconFolder_;
    mutable QHash<QString, QIcon> icons_;
    QSet<QString> favorites_, special_;
    GameFonts fonts_;
    int rowHeight_ = 28;
    Options options_;
    QString statusFolder_;
};

class GamesFilter : public QSortFilterProxyModel {
    Q_OBJECT
public:
    // Same choices, in the same order, as the original toolbar menus.
    enum HaveMiss { ListAll, Have, Miss, MissRoms, HaveAndMissRoms, MissAndMissRoms };
    enum ParentClone { Both, ParentsOnly, ClonesOnly };
    enum Driver { AllStatus, GoodImperfect, Good, Imperfect, Preliminary };

    explicit GamesFilter(GamesModel* m, QObject* parent = nullptr);
    void setHaveMiss(int v) { haveMiss_ = v; invalidateFilter(); }
    void setParentClone(int v) { parentClone_ = v; invalidateFilter(); }
    void setDriver(int v) { driver_ = v; invalidateFilter(); }
    // FormArcadeFiltersDriverStatus > Select a Category: which status the Driver Status filter
    // looks at ([ListFilter] DriverStatusCategory).
    enum DriverCategory { CategoryDriver, CategoryEmulation, CategoryColor, CategorySound, CategoryGraphic };
    void setDriverCategory(int v) { driverCategory_ = v; invalidateFilter(); }
    int driverCategory() const { return driverCategory_; }
    static qint8 categoryStatus(const MameGame& g, int category);
    void setFavoritesOnly(bool v) { favoritesOnly_ = v; invalidateFilter(); }
    // Search bar (ApplyFilterSearchBar): text searched in one field, chosen in its settings menu,
    // with or without the tool bar filters.
    enum SearchField { SearchTitle, SearchYear, SearchManufacturer, SearchCategory, SearchVersionAdded, SearchLanguage,
                       SearchDriverName, SearchBiosName, SearchResolution, SearchGameName, SearchCloneOf,
                       SearchSoftwareList, SearchPlayers, SearchFieldCount };
    void setSearch(const QString& text, int field, bool withToolBarFilters);
    void clearSearch() { setSearch({}, searchField_, withToolBarFilters_); }
    // The search bar's Controls button (PopupSearchBarControlsFilterSelectionClick): the games
    // with this control (mame_control_type.ini's name; "none": none at all), in place of a text
    // search; cleared by any text search.
    void setControlSearch(const QString& control, bool withToolBarFilters);
    QString controlSearch() const { return searchControl_; }
    // The main CPU filter (ApplyFilterCPU; Misc Filters > Filter MAME Games by Main CPU): MAME's
    // machines whose main CPU is this one (exact) or has this in its name; with the other filters.
    void setCpuFilter(const QString& cpu, bool exact) { cpu_ = cpu; cpuExact_ = exact; invalidateFilter(); }
    QString cpuFilter() const { return cpu_; }
    void setNaturalSorting(bool on);
    // Grouped view (ButtonViewModeGrouped): each parent followed by its clones, the groups by the
    // parent's title, whatever column was sorted.
    void setGrouped(bool on);
    bool grouped() const { return grouped_; }
    bool groupStart(int row) const;       // the first row of its group (the group's title above it)      // !DisableNaturalSorting: 1, 2, 10 instead of 1, 10, 2
    bool searching() const { return !search_.isEmpty() || !searchControl_.isEmpty(); }

    // Machine types (Machines button, [MachineType] in ini_files\games_filters.ini). Console and
    // computer machines are only listed when that support is enabled in Preferences.
    // machinesPanel: Enable MAME Machines Filter Panel (the Machines List beside the games list)
    struct MachineTypes { bool arcade = true, withSoftwareLists = true, withoutSoftwareLists = true, softwareGames = true,
                          machinesPanel = false; };
    void setMachineTypes(const MachineTypes& m) { machines_ = m; invalidateFilter(); }
    const MachineTypes& machineTypes() const { return machines_; }
    void setConsoleComputer(bool on) { consoleComputer_ = on; invalidateFilter(); }
    // Miscellaneous Filters (Misc button) and Games Filters - MAME and Arcade (the "All Games"
    // button: "allgames", "cps1", ...; not used while the favorites list is shown, as in the original).
    void setMiscFilters(const MiscFilters& m, const MiscFilters::MultiSlot& multiSlot) { misc_ = m; multiSlot_ = multiSlot; invalidateFilter(); }
    const MiscFilters& miscFilters() const { return misc_; }
    void setArcadeFilter(const QString& name) { arcade_ = name.isEmpty() ? QStringLiteral("allgames") : name; invalidateFilter(); }
    const QString& arcadeFilter() const { return arcade_; }
    void setHbmame(bool on) { hbmame_ = on; invalidateFilter(); }
    // The Machines List panel's machine (ApplyFiltersMachinesList): only the software list games
    // of its lists are listed, with the Have/Miss, Parent/Clone, Driver Status and favorites
    // filters; off when `on` is false.
    void setMachineLists(bool on, const QStringList& lists) {
        panel_ = on;
        panelLists_ = QSet<QString>(lists.cbegin(), lists.cend());
        invalidateFilter();
    }
    bool machinePanel() const { return panel_; }
    int haveMiss() const { return haveMiss_; }
    int parentClone() const { return parentClone_; }
    int driver() const { return driver_; }
    bool favoritesOnly() const { return favoritesOnly_; }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
    bool lessThan(const QModelIndex& a, const QModelIndex& b) const override;

private:
    GamesModel* model_;
    int haveMiss_ = ListAll, parentClone_ = Both, driver_ = AllStatus, driverCategory_ = CategoryDriver;
    bool favoritesOnly_ = false;
    QString search_;
    QString searchControl_;
    QString cpu_;
    bool cpuExact_ = false;
    int searchField_ = SearchTitle;
    bool withToolBarFilters_ = true;
    MachineTypes machines_;
    bool consoleComputer_ = false;
    bool natural_ = true;
    bool grouped_ = false;
    MiscFilters misc_;
    MiscFilters::MultiSlot multiSlot_;
    QString arcade_ = QStringLiteral("allgames");
    bool hbmame_ = false;
    bool panel_ = false;
    QSet<QString> panelLists_;
    bool toolBarFiltersAccept(const MameGame& g) const;
    bool basicFiltersAccept(const MameGame& g) const; // Have/Miss, Parent/Clone, Driver Status, favorites
};

} // namespace eld
