#include "GameDetailsDialog.h"
#include "SoftwareLists.h"
#include "ElWidgets.h"
#include "MameIni.h"
#include "TextFile.h"
#include "VclRuntime.h"
#include "WinIni.h"
#include "ui_FormGameDetails.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLocale>
#include <QPainter>
#include <QProcess>
#include <QScreen>
#include <QStandardItemModel>
#include <QTreeView>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <cmath>

namespace eld {

namespace {

// SetLabelColor (light mode)
QColor stateColor(int state) {
    switch (state) {
    case 0: return QColor(0xFF0000);  // missing
    case 1: return QColor(0x008000);  // found / good
    case 2: return QColor(0x808000);  // missing ROMs/CHDs, imperfect
    case 3: return QColor(0xC0DCC0);  // merged clone set (clMoneyGreen)
    case -5: return QColor(0x0053A6); // "Game Files" (MsgTxtColors.colorFileName)
    case 21: return QColor(0xC0C0C0); // unknown driver status
    default: return {};
    }
}

// The state images of the ROMs list: a small round mark in the state's color.
QIcon stateIcon(int state) {
    static QHash<int, QIcon> cache;
    auto it = cache.constFind(state);
    if (it != cache.cend()) return *it;
    QPixmap px(16, 16);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    QColor c = state == 0 ? QColor(0xD32F2F) : state == 2 ? QColor(0xC9A400) : QColor(0x2E9E3E);
    p.setBrush(c);
    p.setPen(c.darker(130));
    p.drawEllipse(QRectF(3.5, 3.5, 9, 9));
    return *cache.insert(state, QIcon(px));
}

// GetSizeType: "32 Bytes", "4 KB", "1.5 MB" (up to two decimals, without trailing zeros)
QString sizeText(qint64 bytes) {
    if (bytes <= 0) return {};
    if (bytes < 1024) return GameDetailsDialog::tr("%1 Bytes").arg(bytes);
    static const char* units[] = {"KB", "MB", "GB"};
    double v = bytes / 1024.0;
    int u = 0;
    while (v >= 1024 && u < 2) {
        v /= 1024;
        ++u;
    }
    QLocale loc;
    QString n = loc.toString(v, 'f', 2);
    if (n.contains(loc.decimalPoint())) {
        while (n.endsWith('0')) n.chop(1);
        if (n.endsWith(loc.decimalPoint())) n.chop(loc.decimalPoint().size());
    }
    return QString("%1 %2").arg(n, units[u]);
}

// CHD header: "MComprHD", header length, version (big endian) at byte 12.
int chdVersion(const QString& file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return 0;
    QByteArray h = f.read(16);
    if (h.size() < 16 || !h.startsWith("MComprHD")) return 0;
    return (uchar(h[12]) << 24) | (uchar(h[13]) << 16) | (uchar(h[14]) << 8) | uchar(h[15]);
}

} // namespace

GameDetailsDialog::GameDetailsDialog(Frontend& fe, SystemId sys, const Input& in, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormGameDetails), fe_(fe), sys_(sys), in_(in) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Game Details"));
    files_ = std::make_unique<SetFiles>(fe_.romFolders(sys_));
    const MameGame& g = in_.game;

    // Top bar (FormShow): icons, title, name line and emulator version, scan mode; the color
    // follows the set's status (SetLightColorsGameTopBar).
    static const int topColors[] = {0xF0FAE5, 0xE5F0FA, 0xEEEEEE};
    ui_->TopBar->setProperty("vclColor1", ElColorBox::toQColor(topColors[std::clamp<int>(g.setStatus, 0, 2)]).name());
    ui_->TopBar->setProperty("vclColor2", QColor(0xF1F1F1).name());
    ui_->SystemIcon->setPixmap(in_.gameIcon.pixmap(68, 68));
    ui_->SystemIcon->setAlignment(Qt::AlignCenter);
    ui_->GameIcon->setPixmap(QIcon(in_.emuIcon).pixmap(24, 24));
    ui_->LabelGameTitle->setText(g.title);
    QString version = fe_.emulatorVersion(sys_);
    ui_->LabelEmulatorVersion->setText(tr("name: %1").arg(in_.nameLine) + (version.isEmpty() ? QString() : "\n" + version));
    static const char* scan[] = {QT_TR_NOOP("Full Scan"), QT_TR_NOOP("Quick Scan"), QT_TR_NOOP("Force Available")};
    ui_->LabelScanMode->setText(tr("Scan Mode") + "\n" + tr(scan[std::clamp<int>(g.scanMode, 0, 2)]));

    QApplication::setOverrideCursor(Qt::WaitCursor);
    set_ = g.imageName();       // a software list game's files: <rom folder>\<list>\<name>.zip
    parentSet_ = g.imageParent();
    if (g.isSoftware()) readSoftware();
    else runMame();
    fillLeft();
    fillRoms();
    QApplication::restoreOverrideCursor();
    layoutWindow();
}

GameDetailsDialog::~GameDetailsDialog() = default;

// mame -listxml <name>: the machine and the devices it uses.
void GameDetailsDialog::runMame() {
    QString exe = fe_.emulatorFile(sys_);
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        mameError_ = tr("MAME is not set up, so the ROM files can't be listed.");
        return;
    }
    QProcess p;
    p.setWorkingDirectory(fe_.mameFolder(sys_));
    p.start(exe, {"-listxml", in_.game.name});
    if (!p.waitForFinished(30000)) {
        p.kill();
        mameError_ = tr("MAME didn't answer, so the ROM files can't be listed.");
        return;
    }
    QHash<QString, MachineInfo> all = MachineInfo::parse(p.readAllStandardOutput());
    if (!all.contains(in_.game.name)) {
        mameError_ = tr("MAME doesn't know %1, so the ROM files can't be listed.").arg(in_.game.name);
        return;
    }
    m_ = all.take(in_.game.name);
    devices_ = all;
}

// A software list game's files, from its list in MAME's hash folder (the original reads them from
// the list's .elrom).
void GameDetailsDialog::readSoftware() {
    const MameGame& g = in_.game;
    const QString xml = QDir(SoftwareLists::hashFolder(fe_.mameIniPath(sys_), fe_.mameFolder(sys_))).filePath(g.softwareList + ".xml");
    if (!QFileInfo::exists(xml)) {
        mameError_ = tr("%1 was not found, so the files can't be listed.").arg(QDir::toNativeSeparators(xml));
        return;
    }
    m_.name = g.name;
    for (const SoftwareLists::File& f : SoftwareLists::files(xml, g.name)) {
        MachineInfo::Rom r;
        r.name = f.name;
        r.crc = f.crc;
        r.sha1 = f.sha1;
        r.status = f.status;
        r.size = f.size < 0 ? 0 : f.size;
        r.disk = f.disk;
        if (!g.cloneOf.isEmpty()) r.merge = f.name; // a clone's file may be in its parent's set (merged sets)
        m_.roms.push_back(r);
    }
}

QLabel* GameDetailsDialog::valueLabel(const QString& text, int state) {
    auto* l = new ElLabel(this);
    QLabel* tmpl = ui_->LabelYearValue;
    for (const QByteArray& n : tmpl->dynamicPropertyNames())
        if (n.startsWith("vcl")) l->setProperty(n, tmpl->property(n));
    l->setFont(tmpl->font());
    l->setPalette(tmpl->palette());
    QColor c = stateColor(state);
    if (c.isValid()) {
        QPalette pal = l->palette();
        pal.setColor(QPalette::WindowText, c);
        l->setPalette(pal);
    }
    l->setTextFormat(Qt::PlainText);
    l->setText(text);
    vcl::loose(l);
    return l;
}

// AddEntry2: a value wider than the left column takes two lines, with the whole text as tooltip.
void GameDetailsDialog::addEntry(const QString& title, const QString& value, int state, const QString& tip) {
    if (value.isEmpty()) return;
    const int maxWidth = 198; // LeftTextMaxSize
    const int row = grid_->rowCount();
    if (!title.isEmpty()) {
        auto* t = new ElLabel(this);
        QLabel* tmpl = ui_->LabelYear;
        for (const QByteArray& n : tmpl->dynamicPropertyNames())
            if (n.startsWith("vcl")) t->setProperty(n, tmpl->property(n));
        t->setFont(tmpl->font());
        t->setPalette(tmpl->palette());
        t->setTextFormat(Qt::PlainText);
        // A sub-item ("   Game Set" under Game Files) is shown in regular weight instead of being
        // indented with spaces, since the titles are right-aligned.
        if (title.startsWith(' ')) {
            QFont f = t->font();
            f.setBold(false);
            t->setFont(f);
            t->setProperty("vclFontStyle", QString());
        }
        t->setText(titleText(title.trimmed()));
        vcl::loose(t);
        grid_->addWidget(t, row, 0, Qt::AlignRight | Qt::AlignTop); // (against its value)
    }
    QLabel* v = valueLabel(value, state);
    if (v->sizeHint().width() > maxWidth) {
        v->setWordWrap(true);
        v->setFixedWidth(maxWidth);
        v->setToolTip(tip.isEmpty() ? value : tip);
    }
    grid_->addWidget(v, row, 1, Qt::AlignLeft | Qt::AlignTop);
}

int GameDetailsDialog::statusState(int status) {
    switch (status) {
    case 0: return Found;
    case 1: return Partly;
    case 2: return Missing;
    default: return Unknown;
    }
}

// "<set>.zip" when found (the file's name), else "<set>.zip" / "<set> (.zip; .7z)" in red.
QString GameDetailsDialog::setFileText(const QString& set, const QString& ext) {
    SetFiles::Found f = files_->find(set);
    return f.kind == SetFiles::Found::None ? set + ext : QFileInfo(f.path).fileName();
}

// "Year:" - a title with its colon; a translation can change the form ("%1 :" in French).
QString GameDetailsDialog::titleText(const QString& title) {
    return tr("%1:", "Game Details: a title before its value").arg(title);
}

// FillGameTree
void GameDetailsDialog::fillLeft() {
    const MameGame& g = in_.game;
    bool haveM = !m_.name.isEmpty();
    // the titles and their values in a grid (the titles right-aligned against the values)
    left_ = new QWidget(this);
    grid_ = new QGridLayout(left_);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setHorizontalSpacing(8);
    grid_->setVerticalSpacing(0);
    ui_->LabelYear->setText(titleText(ui_->LabelYear->text()));
    ui_->LabelYearValue->setText(g.year);
    for (QLabel* l : {(QLabel*)ui_->LabelYear, (QLabel*)ui_->LabelYearValue}) vcl::loose(l);
    if (!g.year.isEmpty()) {
        grid_->addWidget(ui_->LabelYear, 0, 0, Qt::AlignRight | Qt::AlignTop);
        grid_->addWidget(ui_->LabelYearValue, 0, 1, Qt::AlignLeft | Qt::AlignTop);
    } else {
        ui_->LabelYear->hide();
        ui_->LabelYearValue->hide();
    }

    addEntry(tr("Manufacturer"), g.manufacturer);
    // Audio: samples (found in MAME's samplepath, or missing), discrete or standard
    QString sampleSet = haveM && !m_.samples.isEmpty() ? (m_.sampleOf.isEmpty() ? m_.name : m_.sampleOf) : QString();
    if (!sampleSet.isEmpty() || g.audioType == 2) {
        if (sampleSet.isEmpty()) sampleSet = g.name;
        MameIniFile mi;
        mi.load(fe_.mameIniPath(sys_));
        QString found;
        for (const QString& d : splitPathList(mi.get("samplepath", "samples"))) {
            QDir dir(resolvePath(fe_.mameFolder(sys_), d));
            for (const char* ext : {".zip", ".7z"})
                if (found.isEmpty() && dir.exists(sampleSet + ext)) found = dir.filePath(sampleSet + ext);
        }
        if (!found.isEmpty())
            addEntry(tr("Audio Samples"), QString("%1 (%2)").arg(QFileInfo(found).fileName(), sizeText(QFileInfo(found).size())), Found);
        else
            addEntry(tr("Audio Samples"), sampleSet + " (.zip; .7z)", Missing);
    } else {
        addEntry(tr("Audio"), g.audioType == 1 ? tr("Discrete Audio") : tr("Standard Audio (chip)"));
    }
    // Video and resolution
    static const char* screens[] = {QT_TR_NOOP("Raster"), QT_TR_NOOP("Vector"), QT_TR_NOOP("LCD"), QT_TR_NOOP("SVG")};
    QString video = g.screenType >= 0 && g.screenType <= 3 ? tr(screens[int(g.screenType)]) : QString();
    if (g.orientation >= 0) {
        QString o = g.orientation == 1 ? tr("Vertical") : tr("Horizontal");
        video = video.isEmpty() ? o : QString("%1 (%2)").arg(video, o);
    }
    addEntry(tr("Video"), video);
    QString res = g.resolution;
    if (!g.refresh.isEmpty()) res += (res.isEmpty() ? QString() : " @ ") + g.refresh + " Hz";
    addEntry(g.resolution.isEmpty() ? tr("   Refresh Rate") : tr("   Resolution"), res);
    if (g.requiresArtwork) addEntry(tr("Artwork"), tr("External Artwork Required"));
    bool isBiosGame = g.bios.isEmpty() || g.bios == g.name;
    if (!isBiosGame) addEntry(tr("Bios Name"), g.bios);
    addEntry(tr("Driver Name"), g.driver);
    static const char* status[] = {QT_TR_NOOP("Good"), QT_TR_NOOP("Imperfect"), QT_TR_NOOP("Preliminary")};
    if (g.driverStatus >= 0 && g.driverStatus <= 2)
        addEntry(tr("Driver Status"), tr(status[int(g.driverStatus)]), statusState(g.driverStatus));
    if (g.driverStatus >= 1) { // AddDriverStatus: what isn't good, in two rows
        auto feature = [this](const QString& type) {
            QString s = m_.features.value(type);
            return s == "unemulated" ? 2 : s == "imperfect" ? 1 : s.isEmpty() ? -1 : 0;
        };
        int emulation = g.emuStatus;
        int protection = haveM ? feature("protection") : g.protectionStatus; // from the games list without MAME
        auto line = [this](const QList<QLabel*>& labels) { // side by side, under Driver Status
            auto* h = vcl::boxed(new QHBoxLayout, 0, 0, 0, 0, 10);
            for (QLabel* l : labels) h->addWidget(l);
            h->addStretch();
            grid_->addLayout(h, grid_->rowCount(), 1);
        };
        line({valueLabel(tr("Emulation"), statusState(emulation)),
              valueLabel(tr("Protection"), protection < 0 ? Unknown : statusState(protection))});
        line({valueLabel(tr("Graphic"), statusState(g.graphicStatus)), valueLabel(tr("Color"), statusState(g.colorStatus)),
              valueLabel(tr("Sound"), statusState(g.soundStatus))});
    }
    if (haveM && m_.driver.contains("cocktail")) {
        QString c = m_.driver.value("cocktail");
        int s = c == "good" ? 0 : c == "imperfect" ? 1 : 2;
        addEntry(tr("Cocktail"), tr(status[s]), statusState(s));
    } else if (!haveM && g.cocktailStatus >= 0 && g.cocktailStatus <= 2) {
        addEntry(tr("Cocktail"), tr(status[int(g.cocktailStatus)]), statusState(g.cocktailStatus));
    }
    if (haveM && m_.features.contains("lan")) {
        QString lan = m_.features.value("lan");
        int s = lan == "unemulated" ? 2 : lan == "imperfect" ? 1 : 0;
        addEntry(tr("Local Network"), tr(status[s]), statusState(s));
    } else if (!haveM && g.lanStatus >= 0 && g.lanStatus <= 2) {
        addEntry(tr("Local Network"), tr(status[int(g.lanStatus)]), statusState(g.lanStatus));
    }
    if (g.saveState >= 0) addEntry(tr("Save State"), g.saveState == 1 ? tr("Supported") : tr("Unsupported"));
    if (g.isSoftware()) { // what its software list says about it
        addEntry(tr("Software List"), QString("%1 [%2]").arg(g.softwareTitle, g.softwareList));
        addEntry(tr("Alternate Title"), g.altTitle);
        addEntry(tr("Usage"), g.usage);
        addEntry(tr("Compatibility"), g.compat);
        addEntry(tr("Notes"), g.notes);
    } else {
        addEntry(tr("Category"), g.category);
    }
    addEntry(tr("Version Added"), g.versionAdded);
    if (haveM) addEntry(tr("Main CPU Chip"), m_.mainCpu());
    if (!g.language.isEmpty()) addEntry(tr("Language"), g.language);
    if (!in_.players.isEmpty()) addEntry(tr("# of Players"), in_.players);
    // Controls (AddControlsList): the names from arcade\filters\mame_control_type.ini
    {
        WinIni names;
        names.load(QDir(fe_.dataDir()).filePath("arcade/filters/mame_control_type.ini"));
        QString title = tr("Controls");
        for (const QString& c : g.controls.split(',', Qt::SkipEmptyParts)) {
            if (c.compare("buttons", Qt::CaseInsensitive) == 0) continue;
            QString n = names.readString({}, c.trimmed());
            addEntry(title, n.isEmpty() ? c.trimmed() : n);
            title.clear();
        }
        if (g.buttons > 0) addEntry(title, g.buttons == 1 ? tr("1 Button") : tr("%1 Buttons").arg(g.buttons));
    }
    if (g.mechanical) addEntry(tr("Mechanical"), tr("Yes"));
    addEntry(tr("Last Played"), in_.lastPlayed);
    addEntry(tr("Playtime"), in_.playtime);
    if (g.size > 0) addEntry(tr("Game Size"), sizeText(g.size));
    QString title = tr("Software List");
    for (const QString& s : g.softwareLists) {
        addEntry(title, s);
        title.clear();
    }

    // Game Files: the set, its parent, the BIOS, devices and disks, found or missing.
    QString kind = g.kind == SetKind::Bios ? tr("Bios") : g.kind == SetKind::Device ? tr("Device")
                 : g.isSoftware() ? tr("Software") : g.systemType == 1 ? tr("Machine") : tr("Game");
    addEntry(tr("Game Files"), kind, FileKind);
    const QString ext = ".zip";
    bool ownRoms = false;
    for (const MachineInfo::Rom& r : m_.roms)
        if (!r.disk && r.merge.isEmpty()) ownRoms = true;
    if (haveM && m_.roms.isEmpty() && m_.deviceRefs.isEmpty()) {
        addEntry(tr("   Game Set"), tr("Set with no Game ROMs"));
    } else if (!haveM || ownRoms || !g.cloneOf.isEmpty() || files_->find(set_).kind != SetFiles::Found::None) {
        bool found = files_->find(set_).kind != SetFiles::Found::None;
        if (!found && g.merged) addEntry(tr("   Game Set"), tr("%1 (Merged)").arg(g.name), Merged);
        else if (found || ownRoms || !haveM) addEntry(tr("   Game Set"), setFileText(set_, ext), found ? Found : Missing);
    }
    if (!g.cloneOf.isEmpty()) {
        bool found = files_->find(parentSet_).kind != SetFiles::Found::None;
        addEntry(tr("   Parent Set"), setFileText(parentSet_, ext), found ? Found : Missing);
    }
    if (!isBiosGame) {
        bool found = files_->find(g.bios).kind != SetFiles::Found::None;
        addEntry(tr("   Bios Set"), setFileText(g.bios, ext), found ? Found : Missing);
    }
    int n = 0;
    for (const QString& d : m_.deviceRefs) {
        auto it = devices_.constFind(d);
        if (it == devices_.cend() || it->roms.isEmpty()) continue;
        bool found = files_->find(d).kind != SetFiles::Found::None;
        addEntry(tr("   Device Set %1").arg(++n), setFileText(d, ext), found ? Found : Missing);
    }
    for (const MachineInfo::Rom& r : m_.roms) {
        if (!r.disk) continue;
        QString file = files_->chd(set_, r.name);
        if (file.isEmpty() && !r.merge.isEmpty()) file = files_->chd(parentSet_, r.merge);
        addEntry(tr("   CHD"), r.name + ".chd", file.isEmpty() ? (r.status == "nodump" ? Normal : int(Missing)) : int(Found));
    }
}

// FillROMsTree: the game's ROMs and disks, then the ROMs of its devices.
void GameDetailsDialog::fillRoms() {
    const MameGame& g = in_.game;
    model_ = new QStandardItemModel(this);
    QTreeView* view = ui_->ROMsListView;
    view->setModel(model_);
    view->setRootIsDecorated(false);
    view->setUniformRowHeights(true);
    view->setAlternatingRowColors(true);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setSortingEnabled(true);
    struct Row {
        MachineInfo::Rom rom;
        QString device;
    };
    QList<Row> rows;
    for (const MachineInfo::Rom& r : m_.roms) rows.append({r, {}});
    int devicesWithRoms = 0;
    for (const QString& d : m_.deviceRefs) {
        auto it = devices_.constFind(d);
        if (it == devices_.cend() || it->roms.isEmpty()) continue;
        ++devicesWithRoms;
        for (const MachineInfo::Rom& r : it->roms) rows.append({r, d});
    }
    const bool deviceColumn = devicesWithRoms > 0;
    QStringList headers{tr("Name"), tr("CRC32"), tr("SHA-1"), tr("Size"), tr("Device"), tr("Status")};
    model_->setHorizontalHeaderLabels(headers);
    // Where a ROM can be: its own set, then for merged ROMs the parent and the BIOS.
    for (const Row& row : rows) {
        const MachineInfo::Rom& r = row.rom;
        QString where, prefix;
        int state = Found;
        bool noDump = r.status == "nodump" || (r.crc.isEmpty() && r.sha1.isEmpty());
        if (!row.device.isEmpty()) {
            prefix = tr("Device");
            if (!noDump && !files_->has(row.device, r.crc, r.name)) state = Missing;
        } else if (r.disk) {
            QString file = files_->chd(set_, r.name);
            if (file.isEmpty() && !r.merge.isEmpty()) {
                file = files_->chd(parentSet_, r.merge);
                if (!file.isEmpty()) prefix = tr("Parent");
            }
            if (file.isEmpty()) {
                if (!noDump) state = Missing;
            } else {
                int v = chdVersion(file);
                prefix = QString("%1v%2").arg(prefix.isEmpty() ? QString() : prefix + " ", v ? QString::number(v) : "?");
            }
        } else {
            bool found = files_->has(set_, r.crc, r.name);
            if (!found && !r.merge.isEmpty()) {
                // in the parent set (clones) or the BIOS set
                if (!g.cloneOf.isEmpty() && files_->has(parentSet_, r.crc, r.merge)) {
                    found = true;
                    prefix = tr("Parent");
                } else if (!g.bios.isEmpty() && g.bios != g.name && files_->has(g.bios, r.crc, r.merge)) {
                    found = true;
                    prefix = tr("Bios");
                } else {
                    prefix = !r.bios.isEmpty() || (g.cloneOf.isEmpty() && !g.bios.isEmpty()) ? tr("Bios") : tr("Parent");
                }
            }
            if (!found && !noDump) state = Missing;
        }
        QString text = noDump ? tr("No Dump") : state == Missing ? tr("Missing") : tr("Ok");
        if (r.status == "baddump") {
            text += ", " + tr("Bad Dump");
            if (state == Found) state = Partly;
        }
        if (!prefix.isEmpty()) text = prefix + ", " + text;
        QList<QStandardItem*> items;
        auto* name = new QStandardItem(stateIcon(state), r.disk ? r.name + ".chd" : r.name);
        items << name << new QStandardItem(r.crc) << new QStandardItem(r.sha1);
        auto* size = new QStandardItem(sizeText(r.size));
        size->setData(r.size, Qt::UserRole);
        size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        items << size << new QStandardItem(row.device);
        auto* st = new QStandardItem(text);
        QColor c = stateColor(state == Found ? 1 : state);
        st->setForeground(c);
        items << st;
        model_->appendRow(items);
    }
    view->setColumnHidden(4, !deviceColumn);
    if (!mameError_.isEmpty()) {
        auto* it = new QStandardItem(mameError_);
        it->setForeground(QColor(0x808080));
        model_->appendRow(it);
        view->setFirstColumnSpanned(0, QModelIndex(), true);
    } else if (rows.isEmpty()) {
        auto* it = new QStandardItem(tr("This set has no ROM files"));
        it->setForeground(QColor(0x808080));
        model_->appendRow(it);
        view->setFirstColumnSpanned(0, QModelIndex(), true);
    } else {
        model_->setHorizontalHeaderItem(0, new QStandardItem(tr("Name") + "    " + (rows.size() == 1 ? tr("1 file") : tr("%1 files").arg(rows.size()))));
    }
    for (int c = 0; c < model_->columnCount(); ++c) view->resizeColumnToContents(c);
    view->header()->setStretchLastSection(true);
}

// FormShow, in layouts: the top bar; the titles and values, with the ROMs list beside them as wide
// as its columns; the window as big as its contents (the list takes a bigger window's room).
void GameDetailsDialog::layoutWindow() {
    using vcl::boxed;
    QTreeView* view = ui_->ROMsListView;
    vcl::autoLayout(ui_->TopBar);
    {   // (the bar's design is wider than its texts: no margin of that size past them)
        QMargins m = ui_->TopBar->layout()->contentsMargins();
        m.setRight(7);
        ui_->TopBar->layout()->setContentsMargins(m);
    }
    ui_->TopBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    int listWidth = 4;
    for (int c = 0; c < model_->columnCount(); ++c)
        if (!view->isColumnHidden(c)) listWidth += view->columnWidth(c);
    listWidth = std::clamp(listWidth + 4, 360, 1100);
    const int rowH = std::max(view->sizeHintForRow(0), 18);
    const int listHeight = view->header()->sizeHint().height() + rowH * std::max(1, model_->rowCount()) + 6;
    view->setMinimumSize(360, 120);
    auto* leftColumn = boxed(new QVBoxLayout, 0, 0, 0, 0, 0);
    leftColumn->addWidget(left_);
    leftColumn->addStretch();
    auto* middle = boxed(new QHBoxLayout, 6, 9, 7, 7, 12);
    middle->addLayout(leftColumn);
    middle->addWidget(view, 1);
    auto* root = boxed(new QVBoxLayout(this), 0, 0, 0, 0, 0);
    root->addWidget(ui_->TopBar);
    root->addLayout(middle, 1);
    setProperty("vclLaidOut", true);
    layout()->activate();
    setMinimumSize(layout()->minimumSize());
    // as wide as the list's columns, as high as the longer of the two sides (within the screen)
    const QSize left = left_->sizeHint();
    QSize want(6 + left.width() + 12 + listWidth + 7,
               ui_->TopBar->sizeHint().height() + 9 + std::max(left.height(), listHeight) + 7);
    if (QScreen* s = screen()) want = want.boundedTo(s->availableGeometry().size() - QSize(40, 100));
    resize(want.expandedTo(minimumSize()));
}

} // namespace eld
