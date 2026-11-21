// MainWindow: Scan Games (Audit ROMs)... and View Missing ROMs/CHDs, the games list's right-click
// menu (PopupScanGames, PopupScanResults...).
//
// The Scan Games window (ScanGamesDialog) says which games and how: Full Scan has MAME check every
// set (RomAudit), Quick Scan looks for the sets' files, Mark Games Available marks them available
// unchecked. Creating the games list always does a Quick Scan (a Full Scan of every set takes
// minutes).
#include "MainWindow.h"
#include "FileProblem.h"

#include "ChdCheck.h"
#include "DiagnosticLog.h"
#include "GamesModel.h"
#include "PreferencesDialog.h"
#include "ProgressWindow.h"
#include "RomAudit.h"
#include "ScanGamesDialog.h"
#include "ScanResultsDialog.h"
#include "SoftwareLists.h"
#include "TextFile.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <functional>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QHeaderView>
#include <QLocale>
#include <QMap>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTreeView>

namespace eld {

void commandPromptBox(QProcess* p, bool hide, bool readOutput);

namespace {
QString dbKey(const QString& auditKey) { QString k = auditKey; return k.replace(':', '\\'); }       // nes:smb -> nes\smb
QString auditKey(const MameGame& g) { return g.isSoftware() ? g.softwareList + ':' + g.name : g.name; }
// A set with nothing to find: MAME doesn't mention it
bool noFiles(const MameGame& g) { return g.romFlag(MameGame::RomsKnown) && !g.romFlag(MameGame::AnyRoms) && g.mediaType == 0; }

// A set whose status in the games list differs from what a scan found
struct ListChange {
    QString key, title;
    int was = 0, now = 0;   // setStatus: 0 available/complete, 1 incomplete, 2 missing/not found
};

// View Changes (after a scan): which sets the games list shows differently from what the scan
// found. Double-click (or Enter) selects the game in the games list; Save List writes a text file.
void showListChanges(QWidget* parent, const QString& scanName, QVector<ListChange> changes,
                     const std::function<bool(const QString&)>& select) {
    const QString listText[] = {MainWindow::tr("Available"), MainWindow::tr("Incomplete"), MainWindow::tr("Missing")};
    const QString scanText[] = {MainWindow::tr("Complete"), MainWindow::tr("Incomplete"), MainWindow::tr("Not found")};
    std::sort(changes.begin(), changes.end(), [](const ListChange& a, const ListChange& b) {
        return QString::localeAwareCompare(a.title, b.title) < 0;
    });
    QDialog dlg(parent);
    dlg.setObjectName("ScanGamesChanges");
    dlg.setWindowTitle(MainWindow::tr("Scan Games: Changes"));
    dlg.setSizeGripEnabled(true);
    auto* lay = new QVBoxLayout(&dlg);
    auto* label = new QLabel(changes.size() == 1 ? MainWindow::tr("1 set the games list shows differently from what the %1 found:").arg(scanName)
                                                 : MainWindow::tr("%1 sets the games list shows differently from what the %2 found:")
                                                       .arg(QLocale().toString(int(changes.size()))).arg(scanName));
    label->setWordWrap(true);
    lay->addWidget(label);
    auto* tree = new QTreeWidget;
    tree->setObjectName("ChangesList");
    tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true);
    tree->setAlternatingRowColors(true);
    tree->setHeaderLabels({MainWindow::tr("Game"), MainWindow::tr("Set"), MainWindow::tr("Games list"), scanName});
    for (const ListChange& c : changes) {
        auto* it = new QTreeWidgetItem(tree, {c.title, c.key, listText[qBound(0, c.was, 2)], scanText[qBound(0, c.now, 2)]});
        it->setData(0, Qt::UserRole, c.key);
    }
    tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch); // the game's title gets the room
    tree->header()->setStretchLastSection(false);
    lay->addWidget(tree);
    auto selectCurrent = [&] {
        if (QTreeWidgetItem* it = tree->currentItem()) select(it->data(0, Qt::UserRole).toString());
    };
    QObject::connect(tree, &QTreeWidget::itemActivated, &dlg, [&] { selectCurrent(); }); // double-click, Enter
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close);
    QPushButton* save = bb->addButton(MainWindow::tr("Save List To File..."), QDialogButtonBox::ActionRole);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(save, &QPushButton::clicked, &dlg, [&] {
        const QString file = QFileDialog::getSaveFileName(&dlg, MainWindow::tr("Save List To File"), "scan-changes.txt", MainWindow::tr("Text files (*.txt)"));
        if (file.isEmpty()) return;
        QStringList lines;
        for (const ListChange& c : changes)
            lines << QString("%1\t%2\t%3 -> %4").arg(c.key, c.title, listText[qBound(0, c.was, 2)], scanText[qBound(0, c.now, 2)]);
        if (!writeTextFile(file, lines.join("\r\n") + "\r\n"))
            FileProblem::cantSave(&dlg, MainWindow::tr("Save List To File"), file);
    });
    lay->addWidget(bb);
    dlg.resize(760, 480);
    dlg.exec();
}
} // namespace

QString MainWindow::auditFile() const { return RomAudit::file(fe_.gamesFolder(), Frontend::fileBase(sys_)); }

// The guide, at one of its sections
void MainWindow::openGuide(const QString& anchor) {
    const QString path = QDir(programFolder()).filePath("EmuLoader-ReLoaded-Guide.html");
    if (!QFileInfo::exists(path)) return;
    QUrl url = QUrl::fromLocalFile(path);
    url.setFragment(anchor);
    QDesktopServices::openUrl(url);
}

// Scan Games (Audit ROMs)...: the window, then the scan it says
void MainWindow::openScanGames() {
    if (db_.games().isEmpty()) return;
    ScanGamesDialog::Input in;
    in.fe = &fe_;
    in.softwareLists = db_.softwareCount() > 0 && PreferencesDialog::consoleComputerSupport(fe_); // listed, so scanned
    for (int i = 0; i < 5; ++i) in.counts[i] = int(scanRows(i).size());
    ScanGamesDialog dlg(in, this);
    if (dlg.exec() != QDialog::Accepted) return;
    fe_.saveIni();
    scanGames(dlg.which());
}

// Runs MAME with args in its folder and gives back what it printed; false when it couldn't start
// or the scan was canceled
bool MainWindow::runVerify(const QStringList& args, QString& output, ProgressWindow& win, int done, const QString& count) {
    QProcess p;
    p.setWorkingDirectory(fe_.mameFolder(sys_));
    p.setProcessChannelMode(QProcess::MergedChannels);
    commandPromptBox(&p, fe_.ini().readBool("Configuration", "HideDOSBoxEmuVersionCreateGames", true), true);
    const QString exe = fe_.emulatorFile(sys_);
    p.start(exe, args);
    if (!p.waitForStarted()) {
        DiagnosticLog::write("error", "Could not start " + QDir::toNativeSeparators(exe) + ": " + p.errorString());
        return false;
    }
    QByteArray out;
    while (p.state() != QProcess::NotRunning) {
        p.waitForReadyRead(100);
        out += p.readAll();
        win.setProgress(done, count);
        if (win.canceled()) {
            p.kill();
            p.waitForFinished();
            return false;
        }
    }
    out += p.readAll();
    output = QString::fromLocal8Bit(out);
    return true;
}

// Full Scan of these games: their result (every one of them, the ones MAME didn't find too).
// False when MAME couldn't be started; canceled says whether Cancel stopped it (the result then
// has the games checked before).
bool MainWindow::runAudit(const QVector<int>& rows, bool samples, RomAudit::Results& out, ProgressWindow& win, bool& canceled) {
    canceled = false;
    const QLocale loc;
    QStringList arcade;
    QMap<QString, QStringList> software; // list -> its games
    for (int r : rows) {
        const MameGame& g = db_.games()[r];
        if (g.isSoftware()) software[g.softwareList] << g.name;
        else arcade << g.name;
    }
    const QHash<QString, QString> samplesOf = samples ? ElDatabase::loadSamples(fe_.samplesFile(sys_)) : QHash<QString, QString>();
    QStringList withSamples;
    if (samples)
        for (const QString& n : arcade)
            if (samplesOf.contains(n)) withSamples << n;
    const int total = int(arcade.size() + withSamples.size()) + [&] { int n = 0; for (const auto& l : software) n += int(l.size()); return n; }();
    int done = 0;
    auto count = [&] { return tr("Checked %1 / %2").arg(loc.toString(done), loc.toString(total)); };
    constexpr int batch = 200; // names per command line (well under Windows' limit)

    win.step(tr("%1 is checking the sets (-verifyroms)...").arg(Frontend::section(sys_)), total);
    for (int i = 0; i < arcade.size(); i += batch) {
        const QStringList names = arcade.mid(i, batch);
        QString text;
        if (!runVerify(RomAudit::verifyRomsArgs(names), text, win, done, count())) { canceled = win.canceled(); return canceled; }
        const RomAudit::Results got = RomAudit::parseRoms(text);
        for (const QString& n : names) {
            RomAudit::Set s = got.value(n);
            if (s.key.isEmpty()) { // not mentioned: no file of it at all
                s.key = n;
                const int r = db_.indexOf(n);
                s.roms = r >= 0 && noFiles(db_.games()[r]) ? RomAudit::Good : RomAudit::SetNotFound;
            }
            out.insert(n, s);
        }
        done += int(names.size());
        win.setProgress(done, count());
    }
    for (auto it = software.cbegin(); it != software.cend(); ++it) {
        win.step(tr("%1 is checking the software list %2 (-verifysoftlist)...").arg(Frontend::section(sys_), it.key()), total);
        QString text;
        if (!runVerify(RomAudit::verifySoftListArgs(it.key()), text, win, done, count())) { canceled = win.canceled(); return canceled; }
        const RomAudit::Results got = RomAudit::parseRoms(text);
        for (const QString& n : it.value()) {
            const QString key = it.key() + ':' + n;
            RomAudit::Set s = got.value(key);
            if (s.key.isEmpty()) {
                s.key = key;
                s.roms = RomAudit::SetNotFound;
            }
            out.insert(key, s);
        }
        done += int(it.value().size());
        win.setProgress(done, count());
    }
    if (!withSamples.isEmpty()) win.step(tr("%1 is checking the audio samples (-verifysamples)...").arg(Frontend::section(sys_)), total);
    RomAudit::Results sampleResults;
    for (int i = 0; i < withSamples.size(); i += batch) {
        const QStringList names = withSamples.mid(i, batch);
        QString text;
        if (!runVerify(RomAudit::verifySamplesArgs(names), text, win, done, count())) { canceled = win.canceled(); break; }
        const RomAudit::Results got = RomAudit::parseSamples(text);
        for (const QString& n : names) {
            RomAudit::Set s = got.value(n);
            if (s.key.isEmpty()) {
                s.key = n;
                s.samples = RomAudit::SetNotFound;
            }
            sampleResults.insert(n, s);
        }
        done += int(names.size());
        win.setProgress(done, count());
    }
    RomAudit::merge(out, sampleResults);
    return true;
}

// The disks MAME didn't find that are there all the same: why MAME can't use them (ChdCheck).
// A delta CHD's parent is looked for among every CHD in the ROM folders, read once.
void MainWindow::checkChds(RomAudit::Results& results) {
    const QStringList folders = fe_.romFolders(sys_);
    QHash<QString, QString> bySha1;
    bool indexed = false;
    for (auto it = results.begin(); it != results.end(); ++it) {
        if (it->roms != RomAudit::Bad) continue;
        const int r = db_.indexOf(dbKey(it.key()));
        const QString parent = r >= 0 ? db_.games()[r].cloneOf : it->parent;
        const QString set = it.key().contains(':') ? QString(it.key()).replace(':', '/') : it.key();
        for (RomAudit::File& f : it->files) {
            if (f.kind != RomAudit::Disk || f.problem != RomAudit::NotFound) continue;
            const QString file = ChdCheck::find(folders, set, parent, f.name);
            if (file.isEmpty()) continue;
            const ChdCheck::Header h = ChdCheck::read(file);
            f.chdFile = file;
            if (!h.isDelta()) {
                f.chdIssue = RomAudit::File::OtherChd;
                continue;
            }
            if (!indexed) {
                bySha1 = ChdCheck::index(folders);
                indexed = true;
            }
            f.chdParentSha1 = h.parentSha1;
            f.chdParent = bySha1.value(h.parentSha1);
            f.chdIssue = f.chdParent.isEmpty() ? RomAudit::File::DeltaParentMissing : RomAudit::File::DeltaParentElsewhere;
            DiagnosticLog::write("games", QString("Full Scan: %1 is a delta CHD (parent SHA-1 %2%3)")
                                              .arg(QDir::toNativeSeparators(file), h.parentSha1,
                                                   f.chdParent.isEmpty() ? QString(", not found") : ", " + QDir::toNativeSeparators(f.chdParent)));
        }
    }
}

// The games a Scan Games item is for: 0 all, 1 available, 2 missing, 3 missing ROMs/CHDs
// (incomplete), 4 selected; with Select Scan Mode's Scan MAME Sets
QVector<int> MainWindow::scanRows(int which) {
    QVector<int> rows;
    const int sets = scanSetsMode();
    const bool console = PreferencesDialog::consoleComputerSupport(fe_);
    QSet<QString> selected;
    if (which == 4)
        for (const MameGame* g : selectedGames()) selected.insert(g->key());
    for (int r = 0; r < db_.games().size(); ++r) {
        const MameGame& g = db_.games()[r];
        if (sets == 1 && g.isSoftware()) continue;
        if (sets == 2 && !g.isSoftware()) continue;
        if (g.isSoftware() && !console) continue; // not shown, not scanned
        switch (which) {
        case 1: if (g.setStatus != 0) continue; break;
        case 2: if (g.setStatus != 2) continue; break;
        case 3: if (g.setStatus != 1) continue; break;
        case 4: if (!selected.contains(g.key())) continue; break;
        default: break;
        }
        rows << r;
    }
    return rows;
}

int MainWindow::scanSetsMode() { return db_.softwareCount() ? ScanGamesDialog::setsMode(fe_) : 1; }

// The games list status saved and shown again after a scan
void MainWindow::scanDone() {
    db_.saveStatus(fe_.statusFile(sys_));
    if (db_.softwareCount()) SoftwareLists::saveStatus(softwareFolder(), db_.games(), sys_ == SystemId::HBMAME);
    const QList<const MameGame*> sel = selectedGames();
    const QString current = sel.isEmpty() ? QString() : sel.first()->key();
    QByteArray layout = list_->header()->saveState();
    model_->reload();
    list_->header()->restoreState(layout);
    if (!current.isEmpty()) selectGameByName(current); // the game stays selected
    updateStatus();
}

// The scan: all games, available, missing, missing ROMs/CHDs (incomplete) or the selected ones,
// with the Scan Games window's choices (ScanGames)
void MainWindow::scanGames(int which) {
    const QString title = tr("Scan Games (Audit ROMs)");
    if (db_.games().isEmpty()) return;
    const QVector<int> rows = scanRows(which);
    if (rows.isEmpty()) {
        QMessageBox::information(this, title, which == 4 ? tr("Select the games to scan first.") : tr("There are no games to scan."));
        return;
    }
    const QLocale loc;
    const int mode = ScanGamesDialog::mode(fe_);
    DiagnosticLog::write("games", QString("Scan Games: %1 sets, mode %2").arg(rows.size()).arg(mode));
    if (mode == 2) { // Force Games Available
        const QString games = rows.size() == 1 ? tr("1 game") : tr("%1 games").arg(loc.toString(int(rows.size())));
        if (QMessageBox::question(this, title, tr("Mark %1 as available without checking them?").arg(games)) != QMessageBox::Yes)
            return;
        for (int r : rows) {
            db_.games()[r].setStatus = 0;
            db_.games()[r].scanMode = 2;
        }
        scanDone();
        QMessageBox::information(this, title, tr("%1 marked as available.").arg(games));
        return;
    }
    // an explicit scan checks the forced games again too
    QSet<QString> names, keys;
    for (int r : rows) {
        MameGame& g = db_.games()[r];
        if (g.scanMode == 2) g.scanMode = 1;
        (g.isSoftware() ? keys : names).insert(g.isSoftware() ? g.key() : g.name);
    }
    if (mode == 1) { // Quick Scan, then what it found
        QVector<qint8> before;
        for (int r : rows) before << db_.games()[r].setStatus;
        QVector<ListChange> listChanges;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        db_.quickScan(fe_.romFolders(sys_), &names);
        if (!keys.isEmpty()) SoftwareLists::quickScan(db_.games(), fe_.romFolders(sys_), &keys);
        QApplication::restoreOverrideCursor();
        int have = 0, changed = 0;
        for (int i = 0; i < rows.size(); ++i) {
            const qint8 now = db_.games()[rows[i]].setStatus;
            have += now != 2;
            changed += now != before[i];
            if (now != before[i]) {
                const MameGame& g = db_.games()[rows[i]];
                listChanges << ListChange{auditKey(g), g.title.isEmpty() ? g.name : g.title, before[i], now};
            }
        }
        scanDone();
        // What it didn't find, for View Missing ROMs/CHDs: the sets not found; the ones it found
        // again stop being "not found" (a Full Scan's details of an incomplete set are kept)
        RomAudit::Results all = RomAudit::load(auditFile()), scan;
        for (int r : rows) {
            const MameGame& g = db_.games()[r];
            const QString key = auditKey(g);
            if (g.setStatus == 2) {
                RomAudit::Set s = all.value(key);
                s.key = key;
                s.roms = RomAudit::SetNotFound;
                s.files.erase(std::remove_if(s.files.begin(), s.files.end(), [](const RomAudit::File& f) { return f.kind != RomAudit::Sample; }),
                              s.files.end());
                all.insert(key, s);
                scan.insert(key, s);
            } else if (auto it = all.find(key); it != all.end()) {
                if (it->roms == RomAudit::SetNotFound) it->roms = RomAudit::Good;
                if (g.setStatus == 1) scan.insert(key, it.value()); // incomplete, as a Full Scan found it
            }
        }
        RomAudit::save(auditFile(), all);
        const QString games = rows.size() == 1 ? tr("1 game") : tr("%1 games").arg(loc.toString(int(rows.size())));
        QMessageBox box(QMessageBox::Information, title,
                        tr("The Quick Scan checked %1:").arg(games) + "\n\n" + tr("Found: %1").arg(loc.toString(have)) + "\n" +
                            tr("Not found: %1").arg(loc.toString(int(rows.size()) - have)) + "\n\n" +
                            (changed == 0 ? tr("The games list already showed them so.")
                                          : changed == 1 ? tr("1 game changed in the games list.")
                                                         : tr("%1 games changed in the games list.").arg(loc.toString(changed))),
                        QMessageBox::NoButton, this);
        box.setEscapeButton(box.addButton(QMessageBox::Ok));
        QPushButton* view = scan.isEmpty() ? nullptr : box.addButton(tr("View Results"), QMessageBox::ActionRole);
        QPushButton* viewChanges = listChanges.isEmpty() ? nullptr : box.addButton(tr("View Changes"), QMessageBox::ActionRole);
        if (viewChanges) viewChanges->setObjectName("ViewChanges");
        const auto select = [this](const QString& key) { return selectGameByName(dbKey(key)); };
        for (;;) {
            box.exec();
            if (view && box.clickedButton() == view) viewScanResults(false, &scan);
            else if (viewChanges && box.clickedButton() == viewChanges) showListChanges(this, tr("Quick Scan"), listChanges, select);
            else break;
        }
        return;
    }

    // Full Scan
    const QString exe = fe_.emulatorFile(sys_);
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        QMessageBox::warning(this, title, tr("Set up MAME first (Menu Options > MAME/Arcade > Emulators Setup)."));
        return;
    }
    const bool chds = ScanGamesDialog::chds(fe_), samples = ScanGamesDialog::samples(fe_);
    ProgressWindow win(title, this);
    win.setCancelable(true);
    RomAudit::Results results;
    bool canceled = false;
    if (!runAudit(rows, samples, results, win, canceled)) {
        win.close();
        FileProblem::cantStart(this, title, exe, QString());
        return;
    }
    if (chds) checkChds(results);
    win.close();
    DiagnosticLog::write("games", QString("Full Scan: %1 sets checked%2").arg(results.size()).arg(canceled ? " (canceled)" : ""));
    // kept for View Missing ROMs/CHDs, whatever the answer below
    RomAudit::Results all = RomAudit::load(auditFile());
    RomAudit::merge(all, results);
    RomAudit::save(auditFile(), all);

    // what the scan found, and how it differs from the games list
    int good = 0, incomplete = 0, notFound = 0, samplesBad = 0, worse = 0, better = 0;
    QVector<QPair<int, int>> checked; // row, its status now
    QVector<ListChange> listChanges;
    int changes = 0;
    for (auto it = results.cbegin(); it != results.cend(); ++it) {
        const int st = RomAudit::setStatus(it.value(), chds);
        if (it->samples == RomAudit::Bad || it->samples == RomAudit::SetNotFound) ++samplesBad;
        if (st < 0) continue;
        (st == 0 ? good : st == 1 ? incomplete : notFound)++;
        const int r = db_.indexOf(dbKey(it.key()));
        if (r < 0) continue;
        checked << qMakePair(r, st);
        const int was = db_.games()[r].setStatus;
        if (st != was) {
            ++changes;
            (st > was ? worse : better)++;
            const MameGame& g = db_.games()[r];
            listChanges << ListChange{it.key(), g.title.isEmpty() ? g.name : g.title, was, st};
        }
    }
    const QString sets = results.size() == 1 ? tr("1 set") : tr("%1 sets").arg(loc.toString(int(results.size())));
    QString said = (canceled ? tr("The Full Scan was canceled after %1:").arg(sets) : tr("The Full Scan checked %1:").arg(sets)) +
                   "\n\n" + tr("Complete: %1").arg(loc.toString(good)) + "\n" + tr("Incomplete: %1").arg(loc.toString(incomplete)) + "\n" +
                   tr("Not found: %1").arg(loc.toString(notFound));
    if (samples) said += "\n" + tr("Audio samples missing: %1").arg(loc.toString(samplesBad));
    const bool problems = incomplete > 0 || samplesBad > 0;
    QMessageBox box(QMessageBox::Information, title, said, QMessageBox::NoButton, this);
    QPushButton* update = nullptr;
    if (changes) {
        QStringList diff;
        if (worse)
            diff << (worse == 1 ? tr("1 set the games list shows as available is incomplete or not found.")
                                : tr("%1 sets the games list shows as available are incomplete or not found.").arg(loc.toString(worse)));
        if (better)
            diff << (better == 1 ? tr("1 set the games list shows as missing or incomplete is complete.")
                                 : tr("%1 sets the games list shows as missing or incomplete are complete.").arg(loc.toString(better)));
        box.setInformativeText(diff.join('\n') + "\n\n" + tr("Update the games list?"));
        box.setIcon(QMessageBox::Question);
        update = box.addButton(tr("Update"), QMessageBox::AcceptRole);
        box.setEscapeButton(box.addButton(tr("Don't Update"), QMessageBox::RejectRole));
        box.setDefaultButton(update);
    } else {
        box.setInformativeText(tr("The games list already shows them so."));
        box.setEscapeButton(box.addButton(QMessageBox::Ok)); // Esc: OK, never View Results
    }
    // View Results: the results window, then back to this message (its question still to answer)
    QPushButton* view = problems || notFound ? box.addButton(tr("View Results"), QMessageBox::ActionRole) : nullptr;
    if (view) view->setObjectName("ViewResults");
    // View Changes: which sets the games list shows differently (e.g. "missing" sets found complete)
    QPushButton* viewChanges = listChanges.isEmpty() ? nullptr : box.addButton(tr("View Changes"), QMessageBox::ActionRole);
    if (viewChanges) viewChanges->setObjectName("ViewChanges");
    box.setObjectName("ScanGamesResultBox");
    const auto select = [this](const QString& key) { return selectGameByName(dbKey(key)); };
    for (;;) { // the results or the changes, then back to this message (its question still to answer)
        box.exec();
        if (view && box.clickedButton() == view) viewScanResults(false, &results);
        else if (viewChanges && box.clickedButton() == viewChanges) showListChanges(this, tr("Full Scan"), listChanges, select);
        else break;
    }
    if (update && box.clickedButton() == update) {
        for (const auto& [r, st] : checked) { // every set checked: now by a Full Scan
            db_.games()[r].setStatus = qint8(st);
            db_.games()[r].scanMode = 0;
        }
        DiagnosticLog::write("games", QString("Full Scan: games list updated (%1 sets changed)").arg(changes));
        scanDone();
    }
}

// View Missing ROMs/CHDs > All Games / Selected Game (PopupScanResults...Click)
// scan: a scan's own results (View Results after it): the sets not found shown too
void MainWindow::viewScanResults(bool selectedOnly, const RomAudit::Results* scan) {
    const QString title = tr("View Missing ROMs/CHDs");
    const RomAudit::Results results = scan ? *scan : RomAudit::load(auditFile());
    if (results.isEmpty()) {
        QMessageBox::information(this, title, tr("Nothing to show yet: run a scan (Scan Games (Audit ROMs)...). "
                                                 "What it finds missing is listed here."));
        return;
    }
    QString only;
    if (selectedOnly) {
        const QList<const MameGame*> sel = selectedGames();
        if (sel.size() != 1) return;
        only = auditKey(*sel.first());
        const RomAudit::Set s = results.value(only);
        const QString name = sel.first()->title.isEmpty() ? only : sel.first()->title;
        if (s.key.isEmpty()) {
            QMessageBox::information(this, title, tr("%1 hasn't been scanned yet.").arg(name));
            return;
        }
        if (s.roms == RomAudit::Good && (s.samples == RomAudit::Good || s.samples == RomAudit::NotChecked || !ScanGamesDialog::samples(fe_))) {
            QMessageBox::information(this, title, tr("The last scan found nothing missing for %1.").arg(name));
            return;
        }
    }
    ScanResultsDialog::Input in;
    in.fe = &fe_;
    in.results = results;
    in.only = only;
    in.showNotFound = scan != nullptr;
    in.showSamples = ScanGamesDialog::samples(fe_);
    in.emulatorVersion = fe_.emulatorVersion(sys_);
    in.listVersion = db_.version;
    in.game = [this](const QString& key) -> const MameGame* {
        const int r = db_.indexOf(dbKey(key));
        return r >= 0 ? &db_.games()[r] : nullptr;
    };
    in.icon = [this](const MameGame& g) { return model_->icon(g); };
    in.select = [this](const QString& key) { return selectGameByName(dbKey(key)); };
    ScanResultsDialog dlg(in, this);
    dlg.exec();
}

} // namespace eld
