// MainWindow: MAME's software list games (see SoftwareLists.h) - making their lists from MAME's
// hash folder, scanning their files and the machine each list runs with.
#include "MainWindow.h"

#include "ProgressWindow.h"
#include "RunExtras.h"
#include "SelectMachineDialog.h"
#include "SoftwareLists.h"
#include "ui_FormMain.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTreeView>

namespace eld {

QString MainWindow::softwareFolder() const { return SoftwareLists::gamesFolder(fe_.userDir(), Frontend::fileBase(sys_)); }

QString MainWindow::requirementFile() const {
    return SoftwareLists::requirementFile(fe_.gamesFolder(), Frontend::fileBase(sys_));
}

QString MainWindow::machinesUsedFile() const { return SoftwareLists::machinesFile(fe_.userDir(), Frontend::fileBase(sys_)); }

QString MainWindow::softwareExcludeFile() const {
    return SoftwareLists::excludeFile(fe_.userDir(), Frontend::fileBase(sys_));
}

// The software lists MAME's machines run (their <softwarelist> in -listxml), by name.
QStringList MainWindow::softwareListNames(const ElDatabase& db) {
    QSet<QString> names;
    for (const MameGame& g : db.games())
        if (!g.isSoftware())
            for (const QString& s : g.softwareLists) names.insert(s);
    QStringList out(names.cbegin(), names.cend());
    out.sort();
    return out;
}

QVector<MameGame> MainWindow::machinesFor(const QString& list) const {
    QVector<MameGame> out;
    for (const MameGame& g : db_.games())
        if (!g.isSoftware() && g.softwareLists.contains(list)) out.push_back(g);
    return out;
}

// CreateSoftwareListXML for each list the machines run, except the excluded ones and those with
// no file in the hash folder. Update mode (1) leaves the lists made from the same XML file as
// they are; overwrite mode (2) makes them all again. False when it was canceled.
bool MainWindow::makeSoftwareLists(ProgressWindow& win, int mode, const ElDatabase& machines) {
    const QString folder = softwareFolder();
    const QString hash = SoftwareLists::hashFolder(fe_.mameIniPath(sys_), fe_.mameFolder(sys_));
    const QStringList excluded = SoftwareLists::excluded(softwareExcludeFile());
    SoftwareLists::Requirements req;
    if (mode == 2) {
        for (const QString& l : SoftwareLists::made(folder)) SoftwareLists::remove(folder, l);
        QFile::remove(requirementFile());
    } else {
        req.load(requirementFile());
    }
    win.step(tr("Checking the software lists..."));
    QStringList todo;
    for (const QString& l : softwareListNames(machines)) {
        if (excluded.contains(l)) continue;
        const QString xml = QDir(hash).filePath(l + ".xml");
        if (!QFileInfo::exists(xml)) continue; // no list for it in MAME's hash folder
        if (mode == 1 && SoftwareLists::upToDate(xml, folder, l)) continue;
        todo << l;
        if (win.canceled()) return false;
    }
    const QLocale loc;
    win.step(tr("Creating the software lists..."), int(todo.size()));
    QStringList errors;
    bool done = true;
    for (int i = 0; i < todo.size(); ++i) {
        if (win.canceled()) {
            done = false;
            break;
        }
        win.setProgress(i, tr("Software list %1 / %2: %3").arg(loc.toString(i + 1), loc.toString(int(todo.size())), todo[i]));
        QString err;
        if (!SoftwareLists::create(QDir(hash).filePath(todo[i] + ".xml"), todo[i], folder, sys_ == SystemId::HBMAME, req, &err))
            errors << err;
    }
    req.save(requirementFile());
    if (!errors.isEmpty())
        QMessageBox::warning(&win, tr("Create %1 Software Games List").arg(Frontend::section(sys_)),
                             tr("These software lists could not be read:") + "\n\n" + errors.mid(0, 10).join("\n"));
    return done;
}

// The software list games' quick scan: roms\<list>\<name>.zip (.7z, or a folder for CHDs).
void MainWindow::scanSoftware(QVector<MameGame>& games) {
    SoftwareLists::quickScan(games, fe_.romFolders(sys_));
    SoftwareLists::saveStatus(softwareFolder(), games, sys_ == SystemId::HBMAME);
}

// Menu Options > MAME/Arcade > Create MAME Software List Games (MenuCreateMAMESoftwareListGamesClick).
void MainWindow::createSoftwareListGames() {
    const QString title = tr("Create %1 Software Games List").arg(Frontend::section(sys_));
    if (softwareListNames(db_).isEmpty()) {
        QMessageBox::information(this, title,
                                 tr("The games list has no machines that run software lists. Create the games list "
                                    "first (Menu Options > MAME/Arcade > Create Arcade Games List)."));
        return;
    }
    const QString hash = SoftwareLists::hashFolder(fe_.mameIniPath(sys_), fe_.mameFolder(sys_));
    if (!QFileInfo(hash).isDir()) {
        QMessageBox::warning(this, title,
                             tr("MAME's hash folder was not found:\n%1\n\nIt holds the software lists (hashpath in mame.ini).")
                                 .arg(QDir::toNativeSeparators(hash)));
        return;
    }
    // The original's "MAME Software List Games" choices (TFormArcadeSelectSystem, ActionMode 6).
    QMessageBox box(QMessageBox::Question, title,
                    tr("Create the games of MAME's software lists (the consoles' and computers' cartridges, disks, "
                       "tapes ...), from the lists in MAME's hash folder:\n%1\n\n"
                       "Update: makes the new lists and those that changed in MAME.\n"
                       "Overwrite: makes every list again.\n\n"
                       "A game's files go in a folder named after its list, in a ROM folder: roms\\nes\\smb.zip.")
                        .arg(QDir::toNativeSeparators(hash)),
                    QMessageBox::NoButton, this);
    QPushButton* update = box.addButton(tr("Update"), QMessageBox::AcceptRole);
    QPushButton* overwrite = box.addButton(tr("Overwrite"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(update);
    auto* also = new QCheckBox(tr("Also update them when the games list is created"), &box);
    also->setChecked(fe_.ini().readInteger("Preferences", "CreateMAMESoftwareListGames", 1) != 0);
    box.setCheckBox(also);
    box.exec();
    if (box.clickedButton() != update && box.clickedButton() != overwrite) return;
    const int mode = box.clickedButton() == overwrite ? 2 : 1;
    // on keeps Overwrite when Select Scan Mode chose it
    const int was = fe_.ini().readInteger("Preferences", "CreateMAMESoftwareListGames", 1);
    fe_.ini().writeInteger("Preferences", "CreateMAMESoftwareListGames", also->isChecked() ? (was == 2 ? 2 : 1) : 0);
    fe_.saveIni();

    ProgressWindow win(title + " " + (mode == 2 ? tr("(Overwrite Mode)") : tr("(Update Mode)")), this);
    makeSoftwareLists(win, mode, db_);
    win.setCancelable(false);
    win.step(tr("Checking the ROM folders..."));
    QVector<MameGame> games = SoftwareLists::loadAll(softwareFolder(), SoftwareLists::excluded(softwareExcludeFile()));
    scanSoftware(games);
    win.step(tr("Loading the new games list..."));
    loadGamesList();
    win.close();
}

// GetLastUsedMachineSoftwareList, then SelectMachineToRun when there's none: the machine a
// software list game runs with ("" when there's none or the window was closed).
QString MainWindow::selectMachine(const MameGame& g, bool showSingle) {
    const QString title = tr("Select Machine to Run Software List Game");
    const QVector<MameGame> machines = machinesFor(g.softwareList);
    if (machines.isEmpty()) {
        QMessageBox::information(this, title,
                                 tr("No machine sets were found. Make sure you have a MAME games list.") + "\n" +
                                     tr("Software List: %1 [%2]").arg(g.softwareTitle, g.softwareList));
        return {};
    }
    QString current = SoftwareLists::lastMachine(machinesUsedFile(), g.softwareList);
    if (machines.size() == 1) {
        // There's only one: it's used without asking.
        if (current.isEmpty()) {
            current = machines.first().name;
            SoftwareLists::setLastMachine(machinesUsedFile(), g.softwareList, current);
        }
        if (showSingle)
            QMessageBox::information(this, title,
                                     tr("There is only one machine compatible with this software list game and it's "
                                        "already selected.") +
                                         "\n\n" + tr("Software List: %1 [%2]").arg(g.softwareTitle, g.softwareList) +
                                         "\n" + tr("Machine: %1 [%2]").arg(machines.first().title, machines.first().name));
        return current;
    }
    SelectMachineDialog::Input in;
    in.game = g;
    in.gameIcon = model_->icon(g);
    in.nameLine = ui_->StatusBar_GamesGameName->text();
    in.machines = machines;
    in.current = current;
    in.extrasIni = extrasIni();
    in.icon = [this](const MameGame& m) { return model_->icon(m); };
    SelectMachineDialog dlg(in, this);
    if (dlg.exec() != QDialog::Accepted || dlg.machine().isEmpty()) return {};
    SoftwareLists::setLastMachine(machinesUsedFile(), g.softwareList, dlg.machine());
    model_->refreshIcons();
    return dlg.machine();
}

// CustomParam_ReadFile for a software list game: custom_params\mame\<list>\<game>.txt, its
// parent's, the list's own (<list>\<list>.txt), else the machine's (<machine>.txt).
QString MainWindow::softwareCustomFile(const MameGame& g, const QString& machine) const {
    const QDir d(CustomParams::folder(fe_.userDir(), Frontend::fileBase(sys_)));
    const QDir sub(d.filePath(g.softwareList));
    for (const QString& n : {g.name, g.cloneOf, g.softwareList})
        if (!n.isEmpty() && QFileInfo::exists(sub.filePath(n + ".txt"))) return sub.filePath(n + ".txt");
    if (!machine.isEmpty() && QFileInfo::exists(d.filePath(machine + ".txt"))) return d.filePath(machine + ".txt");
    return {};
}

// ExecuteGame for software list games: the machine (the last one used for the list, else the
// one at the start of the custom parameters, else chosen in Select a Machine), its requirement
// and the game. Empty when there's no machine to run it with.
QStringList MainWindow::softwareArgs(const MameGame& g) {
    // the Machines List panel's machine, else the one last used for the list
    QString machine = filter_->machinePanel() && !panelMachine_.isEmpty() ? panelMachine_
                                                                           : SoftwareLists::lastMachine(machinesUsedFile(), g.softwareList);
    QString params;
    if (sys_ == SystemId::MAME) {
        const QString file = softwareCustomFile(g, machine);
        if (!file.isEmpty()) params = CustomParams::read(file);
    }
    if (machine.isEmpty()) machine = SoftwareLists::customMachine(params);
    if (machine.isEmpty()) machine = selectMachine(g, false);
    if (machine.isEmpty()) return {};
    SoftwareLists::Requirements req;
    req.load(requirementFile());
    return SoftwareLists::commandLine(machine, params, SoftwareLists::requirementArgs(req, softwareFolder(), g.softwareList, g.name),
                                      g.media, g.name);
}

} // namespace eld
