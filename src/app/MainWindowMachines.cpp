// MainWindow: the Machines List panel (PanelMachinesList, MachinesListSidePanel) - the MAME
// machines with software lists beside the games list. Double click a machine (or press Enter)
// and the games list shows the games of its software lists; they run with it. Its filters, icon
// size, columns and width are kept in EmuLoaderRL.ini [MAMEMachinesFilterPanel], the original's
// keys. Customize MAME Software List and Customize MAME Machines List are here too.
#include "MainWindow.h"
#include "ScriptFonts.h"

#include "GamesListDelegate.h"

#include "CustomizeListDialog.h"
#include "ElWidgets.h"
#include "PreferencesDialog.h"
#include "SoftwareLists.h"
#include "VclRuntime.h"
#include "ui_FormMain.h"

#include <QApplication>
#include <QDir>
#include <QHeaderView>
#include <QMessageBox>
#include <QKeyEvent>
#include <QMenu>
#include <QStandardItemModel>
#include <QTreeView>

namespace eld {

namespace {
const char* kPanel = "MAMEMachinesFilterPanel";
// aColumnsMachinesList / aColumnsWidthMachinesList
const char* const kColumnKeys[] = {"Machine", "Year", "Manufacturer", "Name", "Clone", "Driver", "SaveState"};
const int kColumnWidths[] = {250, 45, 120, 85, 85, 95, 90};
const int kIconSizes[] = {68, 48, 32, 24, 16};
enum { NameRole = Qt::UserRole + 1 };
} // namespace

QString MainWindow::machinesExcludeFile() const {
    return fe_.userFile("arcade/" + Frontend::fileBase(sys_) + "_machines_exclude.txt");
}

void MainWindow::setupMachinesPanel() {
    QTreeView* v = ui_->MachinesListSidePanel;
    machinesModel_ = new QStandardItemModel(0, 7, this);
    v->setModel(machinesModel_);
    v->setUniformRowHeights(true);
    v->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->setSelectionBehavior(QAbstractItemView::SelectRows);
    v->setSelectionMode(QAbstractItemView::SingleSelection);
    v->setRootIsDecorated(false);
    v->setIndentation(14); // a parent's clones under it
    v->setItemsExpandable(false);
    v->setAllColumnsShowFocus(true);
    v->setContextMenuPolicy(Qt::CustomContextMenu);
    v->installEventFilter(this); // Enter
    WinIni& ini = fe_.ini();
    for (int c = 0; c < 7; ++c) v->setColumnWidth(c, ini.readInteger(kPanel, kColumnKeys[c], kColumnWidths[c]));
    connect(v, &QTreeView::doubleClicked, this, [this](const QModelIndex& i) { useMachine(i.siblingAtColumn(0).data(NameRole).toString()); });
    connect(v->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { updateMachinesStatus(); });
    connect(v, &QWidget::customContextMenuRequested, this, [this, v](const QPoint& p) {
        QMenu* m = menus_.menu("PopupMachinesList");
        if (!m) return;
        // PopupMachinesListPopup: Add to / Remove From the disabled list, for the selected machine
        const QString name = v->currentIndex().siblingAtColumn(0).data(NameRole).toString();
        const bool excluded = SoftwareLists::excluded(machinesExcludeFile()).contains(name);
        if (QAction* a = menus_.action("PopupMachinesListSidePanelFiltersMachinesList_AddExcluded")) a->setVisible(!name.isEmpty() && !excluded);
        if (QAction* a = menus_.action("PopupMachinesListSidePanelFiltersMachinesList_RemoveExcluded")) a->setVisible(!name.isEmpty() && excluded);
        m->exec(v->viewport()->mapToGlobal(p));
    });
    connect(ui_->SplitterMachines, &ElSplitter::moved, this,
            [this] { fe_.ini().writeInteger(kPanel, "PanelSize", ui_->PanelMachinesList->width()); });

    // The filters: radio items whose tag is the filter's value (Filter_HaveMiss ...)
    struct Group { const char* key; const char* items[5]; };
    static const Group groups[] = {
        {"Filter_HaveMiss", {"PopupMachinesListSidePanelFiltersHaveMiss_ListAll", "PopupMachinesListSidePanelFiltersHaveMiss_Available",
                             "PopupMachinesListSidePanelFiltersHaveMiss_Missing", "PopupMachinesListSidePanelFiltersHaveMiss_MissingROMsCHDs", nullptr}},
        {"Filter_DriverStatus", {"PopupMachinesListSidePanelFiltersDriver_ListAll", "PopupMachinesListSidePanelFiltersDriver_WorkingGoodImperfect",
                                 "PopupMachinesListSidePanelFiltersDriver_WorkingGood", "PopupMachinesListSidePanelFiltersDriver_WorkingImperfect",
                                 "PopupMachinesListSidePanelFiltersDriver_NonWorkingPreliminary"}},
        {"Filter_SaveState", {"PopupMachinesListSidePanelFiltersSaveState_ListAll", "PopupMachinesListSidePanelFiltersSaveState_Supported",
                              "PopupMachinesListSidePanelFiltersSaveState_Unsupported", "PopupMachinesListSidePanelFiltersSaveState_Unknown", nullptr}},
        {"Filter_MachinesList", {"PopupMachinesListSidePanelFiltersMachinesList_ShowAll", "PopupMachinesListSidePanelFiltersMachinesList_ShowEnabledOnly",
                                 "PopupMachinesListSidePanelFiltersMachinesList_ShowDisabledOnly", nullptr, nullptr}},
    };
    for (const Group& g : groups) {
        const int value = ini.readInteger(kPanel, g.key, 0);
        for (int i = 0; i < 5 && g.items[i]; ++i) {
            if (QAction* a = menus_.action(g.items[i])) a->setChecked(i == value);
            const char* key = g.key;
            menus_.on(g.items[i], [this, key, i] {
                fe_.ini().writeInteger(kPanel, key, i);
                fillMachinesPanel();
            });
        }
    }
    menus_.on("PopupMachinesListSidePanelFiltersMachinesList_AddExcluded", [this] { excludeMachine(true); });
    menus_.on("PopupMachinesListSidePanelFiltersMachinesList_RemoveExcluded", [this] { excludeMachine(false); });
    menus_.on("PopupMachinesListSidePanelFiltersMachinesList_Customize", [this] { customizeMachines(); });
    menus_.on("MenuCustomizeMAMEMachinesList", [this] { customizeMachines(); });
    menus_.on("MenuCustomizeMAMESoftwareList", [this] { customizeSoftwareLists(); });
    menus_.on("PopupMachinesListSidePanelResetColumnsWidth", [this] {
        for (int c = 0; c < 7; ++c) {
            ui_->MachinesListSidePanel->setColumnWidth(c, kColumnWidths[c]);
            fe_.ini().deleteKey(kPanel, kColumnKeys[c]);
        }
    });
    static const char* sizes[] = {"PopupMachinesListSidePanelMegaLarge68x68", "PopupMachinesListSidePanelExtraLarge48x48",
                                  "PopupMachinesListSidePanelLarge32x32", "PopupMachinesListSidePanelStandard242x4",
                                  "PopupMachinesListSidePanelSmall16x16"};
    if (QAction* a = menus_.action("PopupMachinesListSidePanelStandard242x4")) a->setText(tr("Standard (24x24)")); // the original's typo
    const int size = std::clamp(ini.readInteger(kPanel, "GameIconSize", 4), 0, 4);
    for (int i = 0; i < 5; ++i) {
        if (QAction* a = menus_.action(sizes[i])) a->setChecked(i == size);
        menus_.on(sizes[i], [this, i] {
            fe_.ini().writeInteger(kPanel, "GameIconSize", i);
            ui_->MachinesListSidePanel->setIconSize(QSize(kIconSizes[i], kIconSizes[i]));
        });
    }
    v->setIconSize(QSize(kIconSizes[size], kIconSizes[size]));
    machinesDelegate_ = new MultilineDelegate(v); // Game Multiline Captions (Preferences)
    machinesDelegate_->on = ini.readBool("Preferences", "GameMultilineCaptions", false);
    v->setItemDelegate(machinesDelegate_);
    const bool status = ini.readInteger("Preferences", "ShowMAMEMachinesStatusBar", 1) != 0;
    if (QAction* a = menus_.action("PopupShowMAMEMachinesStatusBar")) {
        a->setCheckable(true);
        a->setChecked(status);
    }
    menus_.on("PopupShowMAMEMachinesStatusBar", [this] {
        QAction* a = menus_.action("PopupShowMAMEMachinesStatusBar");
        const bool on = a && a->isChecked();
        fe_.ini().writeInteger("Preferences", "ShowMAMEMachinesStatusBar", on ? 1 : 0);
        ui_->StatusBarPanelMachines->setVisible(on);
        vcl::relayout(ui_->PanelMachinesList);
        updateMachinesStatus();
    });
    ui_->StatusBarPanelMachines->setVisible(status);
    ui_->PanelMachinesList->hide();
    ui_->SplitterMachines->hide();
}

// ToggleMachinesListSidePanel: shown with console/computer support and Enable MAME Machines
// Filter Panel; the games list then shows the selected machine's software list games.
void MainWindow::showMachinesPanel() {
    const bool on = PreferencesDialog::consoleComputerSupport(fe_) && filter_->machineTypes().machinesPanel;
    QWidget* panel = ui_->PanelMachinesList;
    if (on) {
        if (panel->isHidden()) {
            panel->resize(std::max(120, fe_.ini().readInteger(kPanel, "PanelSize", 230)), panel->height());
            panel->show();
            ui_->SplitterMachines->show();
            vcl::relayout(ui_->PanelGamesArea);
        }
        fillMachinesPanel();
        // the machine used last, else the first one
        QString name = fe_.ini().readString(kPanel, "LastSelectedMachine");
        if (findMachineRow(name) < 0) name = machinesModel_->rowCount() ? machinesModel_->item(0, 0)->data(NameRole).toString() : QString();
        selectMachineRow(name);
        useMachine(name);
    } else if (!panel->isHidden()) {
        panel->hide();
        ui_->SplitterMachines->hide();
        vcl::relayout(ui_->PanelGamesArea);
        machinesModel_->removeRows(0, machinesModel_->rowCount());
        panelMachine_.clear();
        rememberSelection();
        filter_->setMachineLists(false, {});
        afterFilterChange();
    }
    // The All Games and Misc filters don't apply to the panel's games (the original grays them out).
    ui_->ButtonArcadeGamesFilters->setEnabled(!on);
    ui_->ButtonExtraFilters->setEnabled(!on);
}

// LoadMachinesListToSidePanel + ApplyMachinesListFilter: the machines with software lists, each
// parent with its clones under it; a hidden parent hides its clones.
void MainWindow::fillMachinesPanel() {
    if (!machinesModel_) return;
    QTreeView* v = ui_->MachinesListSidePanel;
    const QString keep = v->currentIndex().isValid() ? v->currentIndex().siblingAtColumn(0).data(NameRole).toString() : panelMachine_;
    WinIni& ini = fe_.ini();
    const int haveMiss = ini.readInteger(kPanel, "Filter_HaveMiss", 0), driver = ini.readInteger(kPanel, "Filter_DriverStatus", 0);
    const int saveState = ini.readInteger(kPanel, "Filter_SaveState", 0), listFilter = ini.readInteger(kPanel, "Filter_MachinesList", 0);
    const QStringList excludedList = SoftwareLists::excluded(machinesExcludeFile());
    const QSet<QString> excluded(excludedList.cbegin(), excludedList.cend());
    auto visible = [&](const MameGame& g) { // ValidateMAMEMachineVisible
        switch (haveMiss) {
        case 1: if (g.setStatus == 2) return false; break;
        case 2: if (g.setStatus != 2) return false; break;
        case 3: if (g.setStatus != 1) return false; break;
        }
        switch (driver) {
        case 1: if (g.driverStatus != 0 && g.driverStatus != 1) return false; break;
        case 2: if (g.driverStatus != 0) return false; break;
        case 3: if (g.driverStatus != 1) return false; break;
        case 4: if (g.driverStatus != 2) return false; break;
        }
        switch (saveState) {
        case 1: if (g.saveState != 1) return false; break;
        case 2: if (g.saveState != 0) return false; break;
        case 3: if (g.saveState != -1) return false; break;
        }
        if (listFilter == 1 && excluded.contains(g.name)) return false;
        if (listFilter == 2 && !excluded.contains(g.name)) return false;
        return true;
    };
    QVector<const MameGame*> machines;
    QSet<QString> names;
    for (const MameGame& g : db_.games())
        if (!g.isSoftware() && !g.softwareLists.isEmpty()) {
            machines << &g;
            names.insert(g.name);
        }
    std::sort(machines.begin(), machines.end(), [](const MameGame* a, const MameGame* b) {
        return a->title.compare(b->title, Qt::CaseInsensitive) < 0;
    });
    auto row = [&](const MameGame& g) {
        QList<QStandardItem*> items;
        const QString ss = g.saveState == 1 ? tr("Supported") : g.saveState == 0 ? tr("Unsupported") : QString();
        for (const QString& t : {g.title, g.year, g.manufacturer, g.name, g.cloneOf, g.driverText(false), ss}) items << new QStandardItem(t);
        items[0]->setIcon(model_->icon(g));
        items[0]->setData(g.name, NameRole);
        const GameFonts::Kind k = GameFonts::kindOf(g);
        for (QStandardItem* it : items) {
            it->setFont(ScriptFonts::shown(model_->fonts().font[k]));
            it->setForeground(model_->fonts().color[k]);
        }
        return items;
    };
    machinesModel_->removeRows(0, machinesModel_->rowCount());
    machinesModel_->setHorizontalHeaderLabels({QString(), tr("Year"), tr("Manufacturer"), tr("Name"), tr("Clone"), tr("Driver"), tr("Save State")});
    QHash<QString, QStandardItem*> parents;
    int shown = 0;
    for (const MameGame* g : machines) { // the parents first
        if (!g->cloneOf.isEmpty() && names.contains(g->cloneOf)) continue;
        if (!visible(*g)) {
            parents.insert(g->name, nullptr); // its clones are hidden too
            continue;
        }
        QList<QStandardItem*> r = row(*g);
        parents.insert(g->name, r[0]);
        machinesModel_->appendRow(r);
        ++shown;
    }
    for (const MameGame* g : machines) { // then the clones, under their parent
        if (g->cloneOf.isEmpty() || !names.contains(g->cloneOf) || !visible(*g)) continue;
        QStandardItem* p = parents.value(g->cloneOf);
        if (!p) continue;
        p->appendRow(row(*g));
        ++shown;
    }
    machinesModel_->setHeaderData(0, Qt::Horizontal, tr("%1 MAME Machines").arg(shown)); // UpdateTotalMachinesLabel
    v->expandAll();
    selectMachineRow(keep);
    updateMachinesStatus();
}

int MainWindow::findMachineRow(const QString& name) const {
    if (name.isEmpty() || !machinesModel_) return -1;
    const QModelIndexList hits = machinesModel_->match(machinesModel_->index(0, 0), NameRole, name, 1,
                                                       Qt::MatchExactly | Qt::MatchRecursive);
    return hits.isEmpty() ? -1 : 0;
}

void MainWindow::selectMachineRow(const QString& name) {
    QTreeView* v = ui_->MachinesListSidePanel;
    QModelIndex sel;
    if (!name.isEmpty()) {
        const QModelIndexList hits = machinesModel_->match(machinesModel_->index(0, 0), NameRole, name, 1,
                                                           Qt::MatchExactly | Qt::MatchRecursive);
        if (!hits.isEmpty()) sel = hits.first();
    }
    if (!sel.isValid() && machinesModel_->rowCount()) sel = machinesModel_->index(0, 0);
    if (!sel.isValid()) return;
    v->setCurrentIndex(sel);
    v->scrollTo(sel);
}

// SetSelectedMachineList: the games list shows the machine's software list games.
void MainWindow::useMachine(const QString& name) {
    const MameGame* m = name.isEmpty() ? nullptr : db_.find(name);
    panelMachine_ = m ? m->name : QString();
    if (m) fe_.ini().writeString(kPanel, "LastSelectedMachine", m->name);
    rememberSelection();
    filter_->setMachineLists(true, m ? m->softwareLists : QStringList());
    afterFilterChange();
}

// UpdateStatusBarMachine: the selected machine's name and its driver status.
void MainWindow::updateMachinesStatus() {
    const QModelIndex i = ui_->MachinesListSidePanel->currentIndex();
    const MameGame* m = i.isValid() ? db_.find(i.siblingAtColumn(0).data(NameRole).toString()) : nullptr;
    if (!m) {
        ui_->StatusBar_MachinesGameName->clear();
        ui_->IconMachineDriverStatus->clear();
        return;
    }
    ui_->StatusBar_MachinesGameName->setText(m->cloneOf.isEmpty() ? m->name : QString("%1 [%2]").arg(m->name, m->cloneOf));
    ui_->StatusBar_MachinesGameName->adjustSize();
    static const char* icons[] = {"scanresult_ok.ico", "scanresult_missroms.ico", "scanresult_badcrc.ico"};
    if (m->driverStatus >= 0 && m->driverStatus <= 2)
        ui_->IconMachineDriverStatus->setPixmap(QIcon(fe_.mainIcon(icons[int(m->driverStatus)])).pixmap(16, 16));
    else
        ui_->IconMachineDriverStatus->clear();
}

// AddRemoveMachineToExcludedList: Add Machine to / Remove Machine From Disabled List
void MainWindow::excludeMachine(bool add) {
    const QString name = ui_->MachinesListSidePanel->currentIndex().siblingAtColumn(0).data(NameRole).toString();
    if (name.isEmpty()) return;
    QStringList list = SoftwareLists::excluded(machinesExcludeFile());
    if (add == list.contains(name)) return;
    if (add) list << name;
    else list.removeAll(name);
    SoftwareLists::saveExcluded(machinesExcludeFile(), list);
    fillMachinesPanel();
}

// Customize MAME Machines List (Machines List panel menu, Menu Options > MAME/Arcade)
void MainWindow::customizeMachines() {
    CustomizeListDialog::Input in;
    in.kind = CustomizeListDialog::Machines;
    for (const MameGame& g : db_.games()) {
        if (g.isSoftware() || g.softwareLists.isEmpty()) continue;
        CustomizeListDialog::Row r;
        r.name = g.name;
        const QString ss = g.saveState == 1 ? tr("Supported") : g.saveState == 0 ? tr("Unsupported") : QString();
        r.cells = {g.title, g.year, g.manufacturer, g.name, g.cloneOf, g.driverText(false), ss};
        r.icon = model_->icon(g);
        r.parent = g.cloneOf.isEmpty();
        r.gray = !g.have();
        in.rows << r;
    }
    if (in.rows.isEmpty()) {
        QMessageBox::information(this, tr("Customize MAME Machines List"),
                                 tr("The games list has no machines that run software lists."));
        return;
    }
    in.excluded = SoftwareLists::excluded(machinesExcludeFile());
    in.excludeFile = machinesExcludeFile();
    in.emulator = fe_.emulatorVersion(sys_) + "\n" + QDir::toNativeSeparators(fe_.emulatorFile(sys_));
    in.systemIcon = QIcon(fe_.mainIcon(sys_ == SystemId::HBMAME ? "emu_hbmame.ico" : "emu_mame.ico"));
    in.emulatorIcon = QIcon(fe_.mainIcon("play_standard.ico"));
    CustomizeListDialog dlg(in, this);
    if (dlg.exec() == QDialog::Accepted) fillMachinesPanel();
}

// Customize MAME Software List (Menu Options > MAME/Arcade): the lists in MAME's hash folder; the
// unchecked ones aren't made or listed. The games list follows at once.
void MainWindow::customizeSoftwareLists() {
    const QString hash = SoftwareLists::hashFolder(fe_.mameIniPath(sys_), fe_.mameFolder(sys_));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QVector<SoftwareLists::ListInfo> lists = SoftwareLists::hashLists(hash);
    QApplication::restoreOverrideCursor();
    if (lists.isEmpty()) {
        QMessageBox::information(this, tr("Customize MAME Software List"),
                                 tr("No software lists were found in MAME's hash folder:\n%1").arg(QDir::toNativeSeparators(hash)));
        return;
    }
    static const char* media[] = {"", QT_TR_NOOP("Cartridge"), QT_TR_NOOP("Disc Image"), QT_TR_NOOP("Floppy Disk"),
                                  QT_TR_NOOP("Cassette Tape"), QT_TR_NOOP("Hard Disk Drive"), QT_TR_NOOP("Video Game Music")};
    static const char* icons[] = {"", "media_cartridge.ico", "media_disc.ico", "media_floppydisk.ico",
                                  "media_cassettetape.ico", "media_harddisk.ico", "media_videogamemusic.ico"};
    CustomizeListDialog::Input in;
    in.kind = CustomizeListDialog::SoftwareLists;
    for (const SoftwareLists::ListInfo& l : lists) {
        CustomizeListDialog::Row r;
        r.name = l.name;
        r.cells = {l.description, l.name, tr(media[l.media])};
        r.icon = QIcon(fe_.mainIcon(icons[l.media]));
        in.rows << r;
    }
    const QStringList before = SoftwareLists::excluded(softwareExcludeFile());
    in.excluded = before;
    in.excludeFile = softwareExcludeFile();
    in.emulator = fe_.emulatorVersion(sys_) + "\n" + QDir::toNativeSeparators(fe_.emulatorFile(sys_));
    in.systemIcon = QIcon(fe_.mainIcon(sys_ == SystemId::HBMAME ? "emu_hbmame.ico" : "emu_mame.ico"));
    in.emulatorIcon = QIcon(fe_.mainIcon("play_standard.ico"));
    CustomizeListDialog dlg(in, this);
    if (dlg.exec() != QDialog::Accepted || dlg.excluded() == before) return;
    // The lists left out disappear now; those put back are listed if they were made before
    // (else Create MAME Software Games List makes them).
    QApplication::setOverrideCursor(Qt::WaitCursor);
    rememberSelection();
    db_.setSoftware(SoftwareLists::loadAll(softwareFolder(), dlg.excluded()));
    loadPlayStats();
    model_->reload();
    applyColumns();
    afterFilterChange();
    QApplication::restoreOverrideCursor();
}

} // namespace eld
