#include "GameFilesDialog.h"

#include "ProgressWindow.h"
#include "WinIni.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace eld {

namespace {
enum { FileRole = Qt::UserRole + 1, IndexRole };

QString kindText(const GameFile& f) {
    switch (f.kind) {
    case GameFile::Rom:
        return f.parentSet ? QCoreApplication::translate("eld::GameFilesDialog", "ROM (merged in the parent's set)")
                           : QCoreApplication::translate("eld::GameFilesDialog", "ROM");
    case GameFile::Chd: return QCoreApplication::translate("eld::GameFilesDialog", "CHD");
    default: return QCoreApplication::translate("eld::GameFilesDialog", "CFG/NVRAM");
    }
}

QString sizeText(qint64 b) { return QLocale().formattedDataSize(b, 2, QLocale::DataSizeTraditionalFormat); }
} // namespace

GameFilesDialog::GameFilesDialog(FileAction action, const QString& caption, const QList<Game>& games,
                                 const QString& extrasIni, const QString& systemFolder, QWidget* parent)
    : QDialog(parent), action_(action), games_(games), extrasIni_(extrasIni), systemFolder_(systemFolder) {
    setWindowTitle(caption);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setSizeGripEnabled(true);
    auto* lay = new QVBoxLayout(this);

    // The games and their files
    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(4);
    tree_->setHeaderLabels({tr("File"), tr("Type"), tr("Size"), tr("Date Modified")});
    tree_->setUniformRowHeights(true);
    tree_->setTextElideMode(Qt::ElideMiddle); // long paths keep their file name
    tree_->setAlternatingRowColors(true);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c) tree_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    filling_ = true;
    for (int gi = 0; gi < games_.size(); ++gi) {
        const Game& g = games_[gi];
        auto* top = new QTreeWidgetItem(tree_, {QString("%1  [%2]").arg(g.title, g.name)});
        QFont f = top->font(0);
        f.setBold(true);
        top->setFont(0, f);
        top->setFirstColumnSpanned(true);
        if (g.files.isEmpty()) {
            auto* none = new QTreeWidgetItem(top, {tr("No files found for this game")});
            none->setForeground(0, palette().color(QPalette::Disabled, QPalette::Text));
        }
        for (int fi = 0; fi < g.files.size(); ++fi) {
            const GameFile& file = g.files[fi];
            if (action_ != FileAction::Delete && file.kind == GameFile::Config) continue; // copy/move: ROMs and CHDs
            QFileInfo info(file.path);
            auto* it = new QTreeWidgetItem(top, {QDir::toNativeSeparators(file.path), kindText(file), sizeText(file.size),
                                                 QLocale::c().toString(info.lastModified(), "MMM/dd/yyyy HH:mm:ss")});
            it->setData(0, FileRole, int(file.kind));
            it->setData(0, IndexRole, QPoint(gi, fi));
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(0, Qt::Unchecked);
            it->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
            if (file.parentSet) it->setToolTip(0, tr("This file is the parent set: it holds the parent and its other clones too."));
        }
        top->setExpanded(true);
    }
    filling_ = false;
    lay->addWidget(tree_, 1);
    connect(tree_, &QTreeWidget::itemChanged, this, [this] { if (!filling_) updateTotals(); });

    // Auto-check file types
    auto* types = new QGroupBox(action_ == FileAction::Delete ? tr("Check File Types to Delete")
                                                              : tr("Auto-Check Arcade File Types"), this);
    auto* tl = new QHBoxLayout(types);
    rom_ = new QCheckBox(tr("ROM"), types);
    rom_->setToolTip(tr("Check this option to process the gamename .zip/.7z file"));
    chd_ = new QCheckBox(tr("CHD"), types);
    chd_->setToolTip(tr("Check this option to process CHD files (if supported by the game)"));
    cfg_ = new QCheckBox(tr("CFG/NVRAM"), types);
    cfg_->setToolTip(tr("Check this option to process game config/RAM files"));
    tl->addWidget(rom_);
    tl->addWidget(chd_);
    tl->addWidget(cfg_);
    tl->addStretch();
    cfg_->setVisible(action_ == FileAction::Delete);
    connect(rom_, &QCheckBox::toggled, this, [this](bool on) { autoCheck(GameFile::Rom, on); });
    connect(chd_, &QCheckBox::toggled, this, [this](bool on) { autoCheck(GameFile::Chd, on); });
    connect(cfg_, &QCheckBox::toggled, this, [this](bool on) { autoCheck(GameFile::Config, on); });
    lay->addWidget(types);

    // Destination (copy / move)
    if (action_ != FileAction::Delete) {
        auto* box = new QGroupBox(tr("Destination folder. Full path, no network path."), this);
        auto* bl = new QVBoxLayout(box);
        auto* row = new QHBoxLayout;
        dest_ = new QLineEdit(box);
        auto* pick = new QPushButton(tr("Select"), box);
        pick->setToolTip(tr("Click here to select a folder"));
        connect(pick, &QPushButton::clicked, this, [this] {
            QString d = QFileDialog::getExistingDirectory(this, tr("Destination folder"), dest_->text());
            if (!d.isEmpty()) dest_->setText(QDir::toNativeSeparators(d));
        });
        row->addWidget(dest_, 1);
        row->addWidget(pick);
        bl->addLayout(row);
        auto* opts = new QHBoxLayout;
        overwrite_ = new QCheckBox(tr("Overwrite Files"), box);
        overwrite_->setToolTip(tr("Check this option to overwrite existing files in the destination folder"));
        addSystem_ = new QCheckBox(tr("Add System Folder"), box);
        addSystem_->setToolTip(tr("Use this option to include a \"system name\" sub-folder in the destination path"));
        opts->addWidget(overwrite_);
        opts->addWidget(addSystem_);
        opts->addStretch();
        bl->addLayout(opts);
        lay->addWidget(box);
    }

    // Totals and buttons
    auto* bottom = new QHBoxLayout;
    totals_ = new QLabel(this);
    bottom->addWidget(totals_, 1);
    auto* buttons = new QDialogButtonBox(this);
    auto* help = buttons->addButton(tr("Help"), QDialogButtonBox::HelpRole);
    go_ = buttons->addButton(action_ == FileAction::Delete ? tr("Delete Files")
                             : action_ == FileAction::Copy ? tr("Copy Files") : tr("Move Files"),
                             QDialogButtonBox::AcceptRole);
    go_->setToolTip(tr("Click here to process all checked files"));
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &GameFilesDialog::process);
    connect(buttons, &QDialogButtonBox::rejected, this, [this] { writeSettings(); reject(); }); // kept either way
    connect(help, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, tr("Help"),
            action_ == FileAction::Delete
                ? tr("Check the files to delete, one by one or by type with the boxes below the list, then click Delete Files.\n\n"
                     "Files go to the Recycle Bin, so a mistake can be undone there. Afterwards the games list is updated to "
                     "show which sets are now missing.\n\nA clone stored in its parent's set (merged) shows the parent's file, "
                     "unchecked: deleting it removes the parent and all its clones.")
                : tr("Check the files to copy or move, pick the destination folder, then click the button.\n\n"
                     "ROM sets go straight into the folder (or into a folder named after the system with Add System Folder). "
                     "CHDs go into a folder named after their game, as MAME expects. Existing files are left alone unless "
                     "Overwrite Files is checked."));
    });
    bottom->addWidget(buttons);
    lay->addLayout(bottom);

    readSettings();
    updateTotals();
    resize(860, 520);
}

void GameFilesDialog::autoCheck(GameFile::Kind kind, bool on) {
    filling_ = true;
    for (int t = 0; t < tree_->topLevelItemCount(); ++t) {
        QTreeWidgetItem* top = tree_->topLevelItem(t);
        for (int c = 0; c < top->childCount(); ++c) {
            QTreeWidgetItem* it = top->child(c);
            if (!it->data(0, IndexRole).isValid() || it->data(0, FileRole).toInt() != kind) continue;
            QPoint ix = it->data(0, IndexRole).toPoint();
            bool parentSet = games_[ix.x()].files[ix.y()].parentSet;
            it->setCheckState(0, on && !parentSet ? Qt::Checked : Qt::Unchecked); // a parent set only by hand
        }
    }
    filling_ = false;
    updateTotals();
}

QList<GameFile> GameFilesDialog::checkedFiles() const {
    QList<GameFile> out;
    for (int t = 0; t < tree_->topLevelItemCount(); ++t) {
        QTreeWidgetItem* top = tree_->topLevelItem(t);
        for (int c = 0; c < top->childCount(); ++c) {
            QTreeWidgetItem* it = top->child(c);
            if (!it->data(0, IndexRole).isValid() || it->checkState(0) != Qt::Checked) continue;
            QPoint ix = it->data(0, IndexRole).toPoint();
            out << games_[ix.x()].files[ix.y()];
        }
    }
    return out;
}

void GameFilesDialog::updateTotals() {
    int total = 0;
    for (int t = 0; t < tree_->topLevelItemCount(); ++t)
        for (int c = 0; c < tree_->topLevelItem(t)->childCount(); ++c)
            if (tree_->topLevelItem(t)->child(c)->data(0, IndexRole).isValid()) ++total;
    QList<GameFile> checked = checkedFiles();
    qint64 size = 0;
    for (const GameFile& f : checked) size += f.size;
    totals_->setText(tr("%1 games, %2 files, %3 checked (%4)")
                         .arg(games_.size()).arg(total).arg(checked.size()).arg(sizeText(size)));
    go_->setEnabled(!checked.isEmpty());
}

void GameFilesDialog::process() {
    QList<GameFile> files = checkedFiles();
    if (files.isEmpty()) return;
    QString dest;
    if (action_ != FileAction::Delete) {
        dest = QDir::fromNativeSeparators(dest_->text().trimmed());
        if (dest.isEmpty() || QDir::isRelativePath(dest)) {
            QMessageBox::warning(this, windowTitle(), tr("Please enter the full path of the destination folder."));
            dest_->setFocus();
            return;
        }
    } else {
        qint64 size = 0;
        for (const GameFile& f : files) size += f.size;
        if (QMessageBox::question(this, windowTitle(),
                                  tr("Move %1 files (%2) to the Recycle Bin?").arg(files.size()).arg(sizeText(size))) != QMessageBox::Yes)
            return;
    }
    writeSettings();
    ProgressWindow win(windowTitle(), this);
    win.step(action_ == FileAction::Delete ? tr("Deleting files...")
             : action_ == FileAction::Copy ? tr("Copying files to %1...").arg(QDir::toNativeSeparators(dest))
                                           : tr("Moving files to %1...").arg(QDir::toNativeSeparators(dest)),
             files.size());
    QStringList problems = processGameFiles(files, action_, dest, addSystem_ && addSystem_->isChecked() ? systemFolder_ : QString(),
                                            overwrite_ && overwrite_->isChecked(), [&](int done, const QString& file) {
        win.setProgress(done, file.isEmpty() ? QString() : QDir::toNativeSeparators(QFileInfo(file).fileName()));
        return !win.canceled();
    });
    win.close();
    changed_ = action_ != FileAction::Copy;
    int ok = int(files.size()) - int(problems.size());
    if (problems.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("%1 files done.").arg(files.size()));
    } else {
        QDialog log(this);
        log.setWindowTitle(windowTitle());
        auto* l = new QVBoxLayout(&log);
        l->addWidget(new QLabel(tr("%1 files done, %2 problems:").arg(std::max(0, ok)).arg(problems.size()), &log));
        auto* text = new QPlainTextEdit(problems.join('\n'), &log);
        text->setReadOnly(true);
        l->addWidget(text);
        auto* b = new QDialogButtonBox(QDialogButtonBox::Close, &log);
        connect(b, &QDialogButtonBox::rejected, &log, &QDialog::reject);
        l->addWidget(b);
        log.resize(700, 300);
        log.exec();
    }
    accept();
}

// CheckDelSettings: [DeleteFiles] / [CopyMoveFiles] in el_extras.ini
void GameFilesDialog::readSettings() {
    WinIni ini;
    ini.load(extrasIni_);
    const bool del = action_ == FileAction::Delete;
    const QString sec = del ? "DeleteFiles" : "CopyMoveFiles";
    rom_->setChecked(ini.readInteger(sec, "ROM_AlwaysSelect", 1) != 0);
    chd_->setChecked(ini.readInteger(sec, "CHD_AlwaysSelect", del ? 0 : 1) != 0);
    if (del) cfg_->setChecked(ini.readInteger(sec, "CFG_AlwaysSelect", 1) != 0);
    if (!del) {
        dest_->setText(ini.readString(sec, "DestinationFolder"));
        overwrite_->setChecked(ini.readInteger(sec, "OverwriteFiles", 0) != 0);
        addSystem_->setChecked(ini.readInteger(sec, "AddSystemFolder", 1) != 0);
    }
    // the boxes' toggled() only fires on a change: check the files by them once now
    autoCheck(GameFile::Rom, rom_->isChecked());
    autoCheck(GameFile::Chd, chd_->isChecked());
    autoCheck(GameFile::Config, del && cfg_->isChecked());
}

void GameFilesDialog::writeSettings() {
    WinIni ini;
    ini.load(extrasIni_);
    const bool del = action_ == FileAction::Delete;
    const QString sec = del ? "DeleteFiles" : "CopyMoveFiles";
    ini.writeInteger(sec, "ROM_AlwaysSelect", rom_->isChecked() ? 1 : 0);
    ini.writeInteger(sec, "CHD_AlwaysSelect", chd_->isChecked() ? 1 : 0);
    if (del) ini.writeInteger(sec, "CFG_AlwaysSelect", cfg_->isChecked() ? 1 : 0);
    else {
        ini.writeString(sec, "DestinationFolder", dest_->text().trimmed());
        ini.writeInteger(sec, "OverwriteFiles", overwrite_->isChecked() ? 1 : 0);
        ini.writeInteger(sec, "AddSystemFolder", addSystem_->isChecked() ? 1 : 0);
    }
    ini.save(extrasIni_);
}

} // namespace eld
