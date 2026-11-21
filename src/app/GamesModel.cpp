#include "GamesModel.h"
#include "ScriptFonts.h"
#include "PlayedGames.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlwapi.h>
#endif

#include <QColor>
#include <QSize>
#include <QFont>
#include <QLocale>
#include <QDir>
#include <QFileInfo>

namespace eld {

GamesModel::GamesModel(ElDatabase& db, const QString& iconFolder, QObject* parent)
    : QAbstractTableModel(parent), db_(db), iconFolder_(iconFolder) {}

void GamesModel::reload() {
    beginResetModel();
    endResetModel();
}

void GamesModel::setFonts(const GameFonts& f) {
    fonts_ = f;
    reload();
}

int GamesModel::rowCount(const QModelIndex& p) const { return p.isValid() ? 0 : db_.games().size(); }

static QString statusText(int s, const GamesModel::Options& o) {
    QString t;
    switch (s) {
    case 0: t = GamesModel::tr("Good"); break;
    case 1: t = GamesModel::tr("Imperfect"); break;
    case 2: t = GamesModel::tr("Preliminary"); break;
    default: return {};
    }
    // AbbreviateStatusText
    if (o.hideStatusTexts) return {};
    return o.statusFirstLetter ? t.left(1) : t;
}

// FixVersionInfoColumn: catver.ini writes ".123"; shown as "0.123" or ".123".
static QString versionText(const QString& v, bool leadingZero) {
    if (v.isEmpty()) return v;
    if (leadingZero) return v.startsWith('.') ? "0" + v : v;
    return (v.startsWith('0') && v.size() > 1 && v[1] == '.') ? v.mid(1) : v;
}

QIcon GamesModel::icon(const MameGame& g) const {
    if (customIcon)
        if (QIcon c = customIcon(g); !c.isNull()) return c;
    // resources\gameslist_icons\[Un]MAME{Parent,Clone,Bios,Device}.ico - "Un" = missing (gray)
    static const char* kinds[] = {"Parent", "Clone", "Bios", "Device"};
    // Sets with missing ROMs/CHDs (status 1) keep the colored icon unless
    // ShowGrayscaleIconGamesMissROMsCHDs (GetMAMEImageIndex).
    bool color = g.setStatus == 0 || (g.setStatus == 1 && !options_.grayscaleMissRoms);
    QString key = QString(color ? "" : "Un") + "MAME" + kinds[int(g.kind)];
    if (g.isSoftware()) // MAMESoftwarelist{Parent,Clone}.ico
        key = QString(color ? "" : "Un") + "MAMESoftwarelist" + (g.kind == SetKind::Clone ? "Clone" : "Parent");
    auto it = icons_.find(key);
    if (it != icons_.end()) return *it;
    QIcon ic(QDir(iconFolder_).filePath(key + ".ico"));
    icons_.insert(key, ic);
    return ic;
}

void GamesModel::setOptions(const Options& o) {
    beginResetModel();
    options_ = o;
    endResetModel();
}

void GamesModel::setRowHeight(int h) {
    if (h == rowHeight_) return;
    beginResetModel();
    rowHeight_ = h;
    endResetModel();
}

QString GamesModel::title(const MameGame& g) const {
    if (!options_.altTitleFormat) return g.title;
    if (g.title.startsWith("The ", Qt::CaseInsensitive)) return g.title.mid(4) + ", The";
    if (g.title.startsWith("Le ", Qt::CaseInsensitive)) return g.title.mid(3) + ", Le";
    return g.title;
}

QString GamesModel::shownTitle(const MameGame& g) const {
    QString t = title(g);
    const bool clone = !g.cloneOf.isEmpty();
    const int mode = options_.titleExtra;
    if (mode == 3 || (mode == 1 && !clone) || (mode == 2 && clone)) {
        int p = t.indexOf('(');
        if (p > 0) t = t.left(p).trimmed();
    }
    return t;
}

QVariant GamesModel::data(const QModelIndex& index, int role) const {
    const MameGame& g = db_.games()[index.row()];
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case Title: return shownTitle(g);
        case Year: return g.year;
        case Manufacturer: return g.manufacturer;
        case Orientation: return g.orientation == 1 ? tr("Vertical") : g.orientation == 0 ? tr("Horizontal") : QString();
        case Resolution: return g.resolution;
        case RefreshRate: return g.refresh.isEmpty() ? QString() : QString::number(g.refresh.toDouble(), 'f', 2) + " Hz";
        case Category: return g.category;
        case VersionAdded: return versionText(g.versionAdded, options_.leadingZero);
        case Name: return g.name;
        case Clone: return g.cloneOf;
        case DriverName: return g.driverText(options_.driverPath);
        case Players: return playersText(g);
        case Language: return g.language;
        case GameSize: return g.size > 0 ? QLocale().formattedDataSize(g.size, 1, QLocale::DataSizeTraditionalFormat) : QString();
        case DriverStatus: return statusText(g.driverStatus, options_);
        case EmulationStatus: return statusText(g.emuStatus, options_);
        case ColorStatus: return statusText(g.colorStatus, options_);
        case SoundStatus: return statusText(g.soundStatus, options_);
        case GraphicStatus: return statusText(g.graphicStatus, options_);
        case Played: return g.played > 0 ? QVariant(g.played) : QVariant();
        case LastPlayed: return PlayedGames::dateText(g.lastPlayed, options_.hideSecondsPlayed);
        case Playtime: return PlayedGames::playtimeText(g.playtime, options_.hideSecondsPlaytime);
        case SoftwareName: return g.softwareList;
        }
    } else if (role == Qt::DecorationRole && index.column() == Title) {
        return icon(g);
    } else if (role == Qt::DecorationRole && index.column() >= DriverStatus && index.column() <= GraphicStatus) {
        // GetStateImageIndexes: scanresult_ok / _missroms / _badcrc for good / imperfect / preliminary
        if (!options_.statusIcons) return {};
        const qint8 st[] = {g.driverStatus, g.emuStatus, g.colorStatus, g.soundStatus, g.graphicStatus};
        int v = st[index.column() - DriverStatus];
        static const char* files[] = {"scanresult_ok.ico", "scanresult_missroms.ico", "scanresult_badcrc.ico"};
        if (v < 0 || v > 2) return {};
        QString key = files[v];
        auto it = icons_.find(key);
        if (it == icons_.end()) it = icons_.insert(key, QIcon(QDir(statusFolder_).filePath(key)));
        return *it;
    } else if (role == Qt::ForegroundRole) {
        return fonts_.color[GameFonts::kindOf(g)]; // GetCanvasFont: parent/clone/preliminary/missing ROMs
    } else if (role == Qt::FontRole) {
        return ScriptFonts::shown(fonts_.font[GameFonts::kindOf(g)]); // (+ the language's FontSize=)
    } else if (role == Qt::SizeHintRole) {
        return QSize(-1, rowHeight_);
    } else if (role == Qt::ToolTipRole && index.column() == Title) {
        return QString("%1 [%2]").arg(g.title, g.key());
    }
    return {};
}

static const char* kColumns[GamesModel::ColumnCount][2] = {
    {"Title", QT_TRANSLATE_NOOP("eld::GamesModel", "Title")}, {"Year", QT_TRANSLATE_NOOP("eld::GamesModel", "Year")}, {"Manufacturer", QT_TRANSLATE_NOOP("eld::GamesModel", "Manufacturer")}, {"Orientation", QT_TRANSLATE_NOOP("eld::GamesModel", "Orientation")},
    {"Resolution", QT_TRANSLATE_NOOP("eld::GamesModel", "Resolution")}, {"RefreshRate", QT_TRANSLATE_NOOP("eld::GamesModel", "Refresh Rate")}, {"Category", QT_TRANSLATE_NOOP("eld::GamesModel", "Category")},
    {"VersionAdded", QT_TRANSLATE_NOOP("eld::GamesModel", "Version Added")}, {"Name", QT_TRANSLATE_NOOP("eld::GamesModel", "Game Name")}, {"Clone", QT_TRANSLATE_NOOP("eld::GamesModel", "Clone of")}, {"DriverName", QT_TRANSLATE_NOOP("eld::GamesModel", "Driver Name")},
    {"NumPlayers", QT_TRANSLATE_NOOP("eld::GamesModel", "Players")}, {"DriverStatus", QT_TRANSLATE_NOOP("eld::GamesModel", "Driver Status")}, {"EmulationStatus", QT_TRANSLATE_NOOP("eld::GamesModel", "Emulation Status")},
    {"ColorStatus", QT_TRANSLATE_NOOP("eld::GamesModel", "Color Status")}, {"SoundStatus", QT_TRANSLATE_NOOP("eld::GamesModel", "Sound Status")}, {"GraphicStatus", QT_TRANSLATE_NOOP("eld::GamesModel", "Graphic Status")},
    {"Played", QT_TRANSLATE_NOOP("eld::GamesModel", "Played")}, {"Language", QT_TRANSLATE_NOOP("eld::GamesModel", "Language")}, {"GameSize", QT_TRANSLATE_NOOP("eld::GamesModel", "Game Size")}, {"LastPlayed", QT_TRANSLATE_NOOP("eld::GamesModel", "Last Played")},
    {"Playtime", QT_TRANSLATE_NOOP("eld::GamesModel", "Playtime")}, {"SoftwareName", QT_TRANSLATE_NOOP("eld::GamesModel", "Software Name")}, {"Special", QT_TRANSLATE_NOOP("eld::GamesModel", "Special")}};
static const int kWidths[GamesModel::ColumnCount] = {400, 65, 180, 100, 90, 100, 180, 100, 100, 100, 105, 80,
                                                      90, 90, 90, 90, 90, 60, 100, 115, 130, 110, 130, 65};

const char* GamesModel::columnKey(int c) { return kColumns[c][0]; }
QString GamesModel::columnTitle(int c) { return tr(kColumns[c][1]); }
int GamesModel::defaultWidth(int c) { return kWidths[c]; }

QVariant GamesModel::headerData(int section, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    if (section >= DriverStatus && section <= GraphicStatus) { // ShortDriverColumnTitle
        static const char* longT[] = {QT_TR_NOOP("Driver"), QT_TR_NOOP("Emulation"), QT_TR_NOOP("Color"),
                                      QT_TR_NOOP("Sound"), QT_TR_NOOP("Graphic")};
        static const char* shortT[] = {QT_TR_NOOP("Drv"), QT_TR_NOOP("Emu"), QT_TR_NOOP("Clr"), QT_TR_NOOP("Snd"), QT_TR_NOOP("Grp")};
        return tr((options_.shortStatusTitles ? shortT : longT)[section - DriverStatus]);
    }
    return columnTitle(section);
}

// --------------------------------------------------------------------- filter

GamesFilter::GamesFilter(GamesModel* m, QObject* parent) : QSortFilterProxyModel(parent), model_(m) {
    setSourceModel(m);
    setSortCaseSensitivity(Qt::CaseInsensitive);
}

// The original sorts with Windows' own text comparison: AnsiCompareText (CompareString,
// ignoring case) or, for natural sorting, StrCmpLogicalW. Both use Windows "word sort":
// apostrophes and hyphens are ignored ("'88 Games" sorts as "88 Games"), and punctuation
// comes before digits, digits before letters. On Windows those functions are called
// directly; elsewhere the same rules are applied here.
#ifndef Q_OS_WIN
static int charClass(QChar c) { return c.isLetter() ? 2 : c.isDigit() ? 1 : 0; }

static int wordSortCompare(const QString& a, const QString& b, bool natural) {
    int i = 0, j = 0;
    auto skip = [](const QString& s, int& k) {
        while (k < s.size() && (s[k] == '\'' || s[k] == '-' || s[k] == QChar(0x2019))) ++k;
    };
    while (true) {
        skip(a, i);
        skip(b, j);
        if (i >= a.size() || j >= b.size()) break;
        if (natural && a[i].isDigit() && b[j].isDigit()) {
            int si = i, sj = j;
            while (si < a.size() && a[si] == '0') ++si;
            while (sj < b.size() && b[sj] == '0') ++sj;
            int ei = si, ej = sj;
            while (ei < a.size() && a[ei].isDigit()) ++ei;
            while (ej < b.size() && b[ej].isDigit()) ++ej;
            if (ei - si != ej - sj) return (ei - si) < (ej - sj) ? -1 : 1;
            for (int k = 0; k < ei - si; ++k)
                if (a[si + k] != b[sj + k]) return a[si + k] < b[sj + k] ? -1 : 1;
            i = ei;
            j = ej;
            continue;
        }
        QChar x = a[i].toCaseFolded(), y = b[j].toCaseFolded();
        if (x != y) {
            int cx = charClass(x), cy = charClass(y);
            if (cx != cy) return cx < cy ? -1 : 1;
            QString dx = QString(x).normalized(QString::NormalizationForm_D).left(1);
            QString dy = QString(y).normalized(QString::NormalizationForm_D).left(1);
            if (dx != dy) return dx < dy ? -1 : 1; // accented letters next to their base letter
            return x < y ? -1 : 1;
        }
        ++i;
        ++j;
    }
    bool ea = i >= a.size(), eb = j >= b.size();
    if (ea && eb) return 0;
    return ea ? -1 : 1;
}
#endif

static int textCompare(const QString& a, const QString& b, bool natural) {
    if (a.isEmpty() || b.isEmpty()) return a.isEmpty() == b.isEmpty() ? 0 : (a.isEmpty() ? -1 : 1); // empty first
#ifdef Q_OS_WIN
    const wchar_t* x = reinterpret_cast<const wchar_t*>(a.utf16());
    const wchar_t* y = reinterpret_cast<const wchar_t*>(b.utf16());
    if (natural) return StrCmpLogicalW(x, y);
    return CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, x, int(a.size()), y, int(b.size())) - CSTR_EQUAL;
#else
    return wordSortCompare(a, b, natural);
#endif
}

void GamesFilter::setNaturalSorting(bool on) {
    if (natural_ == on) return;
    natural_ = on;
    invalidate(); // sorts again
}

void GamesFilter::setSearch(const QString& text, int field, bool withToolBarFilters) {
    search_ = text;
    searchControl_.clear();
    searchField_ = field;
    withToolBarFilters_ = withToolBarFilters;
    invalidateFilter();
}

void GamesFilter::setControlSearch(const QString& control, bool withToolBarFilters) {
    search_.clear();
    searchControl_ = control;
    withToolBarFilters_ = withToolBarFilters;
    invalidateFilter();
}

QString GamesModel::playersText(const MameGame& g) const {
    if (!g.playersIni.isEmpty() || options_.playersIniOnly) return g.playersIni;
    return g.players > 0 ? QString::number(g.players) : QString();
}

static QString searchText(const GamesModel* m, const MameGame& g, int field) {
    switch (field) {
    case GamesFilter::SearchTitle: return g.title;
    case GamesFilter::SearchYear: return g.year;
    case GamesFilter::SearchManufacturer: return g.manufacturer;
    case GamesFilter::SearchCategory: return g.category;
    case GamesFilter::SearchVersionAdded: return g.versionAdded;
    case GamesFilter::SearchDriverName: return g.driver; // with the folder: "sony" finds its drivers too
    case GamesFilter::SearchBiosName: return g.bios;
    case GamesFilter::SearchResolution: return g.resolution;
    case GamesFilter::SearchGameName: return g.name;
    case GamesFilter::SearchCloneOf: return g.cloneOf;
    case GamesFilter::SearchPlayers: return m->playersText(g);
    case GamesFilter::SearchSoftwareList: return g.softwareList + ' ' + g.softwareTitle; // "nes" or its description
    case GamesFilter::SearchLanguage: return g.language;
    default: return {};
    }
}

bool GamesFilter::filterAcceptsRow(int row, const QModelIndex&) const {
    const MameGame& g = model_->game(row);
    // Device sets (with ROMs) are listed, as in the original; Misc > Hide Device Sets hides them.
    if (g.systemType == 1 && !consoleComputer_) return false; // arcade only
    if (!cpu_.isEmpty()) {
        if (g.isSoftware()) return false; // MAME's machines only
        if (cpuExact_ ? g.mainCpu.compare(cpu_, Qt::CaseInsensitive) != 0 : !g.mainCpu.contains(cpu_, Qt::CaseInsensitive))
            return false;
    }
    if (!search_.isEmpty()) {
        if (!searchText(model_, g, searchField_).contains(search_, Qt::CaseInsensitive)) return false;
        if (!withToolBarFilters_) return true; // "Include Tool Bar Filters" unticked
    }
    if (!searchControl_.isEmpty()) { // CheckGameControlType: MAME's machines only
        if (searchControl_.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0) {
            if (!g.controls.isEmpty() || g.isSoftware() || g.buttons > 0) return false;
        } else if (!g.controls.contains(searchControl_)) {
            return false;
        }
        if (!withToolBarFilters_) return true;
    }
    return toolBarFiltersAccept(g);
}

bool GamesFilter::basicFiltersAccept(const MameGame& g) const {
    switch (haveMiss_) {
    case Have: if (g.setStatus != 0) return false; break;
    case Miss: if (g.setStatus != 2) return false; break;
    case MissRoms: if (g.setStatus != 1) return false; break;
    case HaveAndMissRoms: if (g.setStatus == 2) return false; break;
    case MissAndMissRoms: if (g.setStatus == 0) return false; break;
    default: break;
    }
    if (parentClone_ == ParentsOnly && g.kind == SetKind::Clone) return false;
    if (parentClone_ == ClonesOnly && g.kind != SetKind::Clone) return false;
    if (driver_ != AllStatus) { // ValidateStatusID
        qint8 st = categoryStatus(g, driverCategory_);
        switch (driver_) {
        case GoodImperfect: if (st != 0 && st != 1) return false; break;
        case Good: if (st != 0) return false; break;
        case Imperfect: if (st != 1) return false; break;
        case Preliminary: if (st != 2) return false; break;
        default: break;
        }
    }
    if (favoritesOnly_ && !model_->favorites().contains(g.key())) return false;
    return true;
}

bool GamesFilter::toolBarFiltersAccept(const MameGame& g) const {
    // the Machines List panel: its machine's software list games only
    if (panel_) return g.isSoftware() && panelLists_.contains(g.softwareList) && basicFiltersAccept(g);
    if (consoleComputer_) {
        if (g.systemType == 0 && !machines_.arcade) return false;
        if (g.isSoftware()) {
            if (!machines_.softwareGames) return false;
        } else if (g.systemType == 1 && !(g.softwareLists.isEmpty() ? machines_.withoutSoftwareLists : machines_.withSoftwareLists)) {
            return false;
        }
    }
    if (!basicFiltersAccept(g)) return false;
    if (!favoritesOnly_ && !ArcadeFilter::accept(arcade_, g, hbmame_)) return false;
    if (misc_.specialList && model_->special().contains(g.key()) != (misc_.specialList == 1)) return false;
    if (!misc_.accept(g, multiSlot_, hbmame_)) return false;
    return true;
}

// The status the Driver Status filter looks at. MAME lists color, sound and graphics only when
// they aren't good, so for a machine with a known driver status, "not listed" counts as good.
qint8 GamesFilter::categoryStatus(const MameGame& g, int category) {
    qint8 st = g.driverStatus;
    switch (category) {
    case CategoryEmulation: st = g.emuStatus; break;
    case CategoryColor: st = g.colorStatus; break;
    case CategorySound: st = g.soundStatus; break;
    case CategoryGraphic: st = g.graphicStatus; break;
    default: return g.driverStatus;
    }
    return st < 0 && g.driverStatus >= 0 ? qint8(0) : st;
}

void GamesFilter::setGrouped(bool on) {
    if (grouped_ == on) return;
    grouped_ = on;
    invalidate();
}

bool GamesFilter::groupStart(int row) const {
    if (!grouped_ || row < 0 || row >= rowCount()) return false;
    if (row == 0) return true;
    const MameGame& g = model_->game(mapToSource(index(row, 0)).row());
    const MameGame& prev = model_->game(mapToSource(index(row - 1, 0)).row());
    return GamesModel::groupKey(g) != GamesModel::groupKey(prev);
}

bool GamesFilter::lessThan(const QModelIndex& a, const QModelIndex& b) const {
    const MameGame& ga = model_->game(a.row());
    const MameGame& gb = model_->game(b.row());
    if (grouped_) {
        // by the parent's title (then its name, should two share a title), the parent first
        const QString& ka = GamesModel::groupKey(ga);
        const QString& kb = GamesModel::groupKey(gb);
        if (ka != kb) {
            const int ra = model_->rowOf(ka), rb = model_->rowOf(kb);
            const QString ta = model_->title(ra >= 0 ? model_->game(ra) : ga);
            const QString tb = model_->title(rb >= 0 ? model_->game(rb) : gb);
            const int c = textCompare(ta, tb, natural_);
            return c != 0 ? c < 0 : ka < kb;
        }
        const bool ca = !ga.cloneOf.isEmpty(), cb = !gb.cloneOf.isEmpty();
        if (ca != cb) return !ca;
        return textCompare(model_->title(ga), model_->title(gb), natural_) < 0;
    }
    if (a.column() == GamesModel::GameSize) return ga.size < gb.size;
    if (a.column() == GamesModel::Played) return ga.played < gb.played;
    if (a.column() == GamesModel::Playtime) return ga.playtime < gb.playtime;
    if (a.column() == GamesModel::LastPlayed) // by date (never played first)
        return PlayedGames::fromDosDate(ga.lastPlayed) < PlayedGames::fromDosDate(gb.lastPlayed);
    // iCompare: NaturalCompareText unless DisableNaturalSorting, else AnsiCompareText.
    auto cmp = [this](const QString& x, const QString& y) {
        return textCompare(x, y, natural_);
    };
    if (a.column() == GamesModel::Title) return cmp(model_->title(ga), model_->title(gb)) < 0;
    int c = cmp(a.data().toString(), b.data().toString());
    if (c == 0) return cmp(model_->title(ga), model_->title(gb)) < 0;
    return c < 0;
}

} // namespace eld
