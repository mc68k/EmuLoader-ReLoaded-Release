#include "FavoritesManagerDialog.h"
#include "RecycleBin.h"
#include "FileProblem.h"

#include "VclRuntime.h"
#include "WinIni.h"
#include "ui_FormFavoritesManager.h"

#include <QApplication>
#include <algorithm>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

namespace eld {

namespace {
enum Col { Title, Games, FileName, Modified, ColCount };
enum Role { SortRole = Qt::UserRole + 1, FileRole };
const int kDefaultWidths[ColCount] = {300, 70, 250, 155}; // ReadSettings
}

FavoritesManagerDialog::FavoritesManagerDialog(const Setup& setup, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormFavoritesManager), s_(setup), active_(setup.activeFile) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setSizeGripEnabled(true);
    setWindowTitle(s_.selectOnly ? tr("Select a Favorites List") : tr("Favorites Manager"));

    // Buttons (the speed buttons of NewFavoritePanel)
    ui_->ButtonSettings->setIcon(s_.settingsIcon);
    ui_->ButtonSettings->setIconSize(QSize(16, 16));
    ui_->ButtonNew->setText(tr("New"));
    ui_->ButtonNew->setToolTip(tr("Create a new favorites list"));
    ui_->ButtonClearGames->setText(tr("Clear Games"));
    ui_->ButtonClearGames->setToolTip(tr("Take every game off the selected list"));
    ui_->ButtonRemoveInvalidEntries->setText(tr("Clean Out"));
    ui_->ButtonRemoveInvalidEntries->setToolTip(tr("Take the games that aren't in the games list off the selected list"));
    ui_->ButtonReplicate->setText(tr("Replicate"));
    ui_->ButtonReplicate->setToolTip(tr("Make a copy of the selected list"));
    ui_->ButtonDelete->setText(tr("Delete"));
    ui_->ButtonDelete->setToolTip(tr("Delete the selected list (Delete key); it goes to the Recycle Bin"));
    ui_->ButtonSetSelectedProfileActive->setText(tr("Use the Selected List"));
    ui_->ButtonSetSelectedProfileActive->setToolTip(tr("Show the selected list's games as the favorites (Space bar)"));
    QPushButton* setBtn = ui_->ButtonSetSelectedProfileActive;
    connect(ui_->ButtonNew, &QPushButton::clicked, this, [this] { createProfile(false); });
    connect(ui_->ButtonReplicate, &QPushButton::clicked, this, [this] { createProfile(true); });
    connect(ui_->ButtonClearGames, &QPushButton::clicked, this, &FavoritesManagerDialog::clearGames);
    connect(ui_->ButtonRemoveInvalidEntries, &QPushButton::clicked, this, &FavoritesManagerDialog::cleanse);
    connect(ui_->ButtonDelete, &QPushButton::clicked, this, &FavoritesManagerDialog::deleteProfile);
    connect(setBtn, &QPushButton::clicked, this, &FavoritesManagerDialog::setActive);
    // Speed buttons (TSpeedButtonEx): they never take the keyboard focus, so the keys always
    // reach the list.
    for (QPushButton* b : findChildren<QPushButton*>()) {
        b->setAutoDefault(false);
        b->setFocusPolicy(Qt::NoFocus);
    }
    if (s_.selectOnly)
        for (QPushButton* b : {ui_->ButtonSettings, ui_->ButtonNew, ui_->ButtonClearGames,
                               ui_->ButtonRemoveInvalidEntries, ui_->ButtonReplicate, ui_->ButtonDelete})
            b->setEnabled(false);

    // Settings: Center Window, Reset Window Size (PopupFavoritesManagerSettings)
    auto* settings = new QMenu(this);
    QAction* center = settings->addAction(tr("Center Window"), this, [this] {
        if (QScreen* sc = screen()) move(sc->availableGeometry().center() - rect().center());
    });
    settings->addAction(tr("Reset Window Size"), this, [this] {
        showNormal();
        resize(781, 401 + ui_->NewFavoritePanel->height() - 47);
        for (int c = 0; c < ColCount; ++c) ui_->FavoritesList->setColumnWidth(c, kDefaultWidths[c]);
    });
    connect(ui_->ButtonSettings, &QPushButton::clicked, this, [this, settings, center] {
        center->setEnabled(!isMaximized());
        settings->exec(ui_->ButtonSettings->mapToGlobal(QPoint(0, ui_->ButtonSettings->height())));
    });

    // The keys, in one line (the original lays two labels over each other).
    ui_->LabelHotkeyText->setText(tr("F2: edit title    F3: edit file name    Space: set active    "
                                     "Enter/Double-click: set active and exit    Esc: exit"));
    QFont hk = font();
    if (hk.pointSizeF() > 0) hk.setPointSizeF(hk.pointSizeF() * 0.85);
    else hk.setPixelSize(std::max(1, qRound(hk.pixelSize() * 0.85))); // the forms set sizes in pixels
    ui_->LabelHotkeyText->setFont(hk);
    // The tool bar in layouts: the buttons as wide as their captions (translations differ in
    // length), the keys under them
    {
        using vcl::boxed;
        QWidget* bar = ui_->NewFavoritePanel;
        auto* v = boxed(new QVBoxLayout(bar), 2, 2, 2, 4, 3);
        auto* row = boxed(new QHBoxLayout, 0, 0, 0, 0, 0);
        row->addWidget(ui_->ButtonSettings);
        for (QPushButton* b : {ui_->ButtonNew, ui_->ButtonClearGames, ui_->ButtonRemoveInvalidEntries, ui_->ButtonReplicate,
                               ui_->ButtonDelete})
            row->addWidget(vcl::textButton(b));
        row->addSpacing(30);
        row->addWidget(vcl::textButton(setBtn));
        row->addStretch();
        v->addLayout(row);
        vcl::loose(ui_->LabelHotkeyText);
        auto* keys = boxed(new QHBoxLayout, 6, 0, 0, 0, 0);
        keys->addWidget(ui_->LabelHotkeyText);
        v->addLayout(keys);
        bar->setProperty("vclLaidOut", true);
        bar->setFixedHeight(v->sizeHint().height());
    }

    // The profiles
    QTreeView* v = ui_->FavoritesList;
    model_ = new QStandardItemModel(0, ColCount, this);
    model_->setHorizontalHeaderLabels({tr("Title"), tr("Games"), tr("File Name"), tr("Date Modified")});
    model_->setSortRole(SortRole);
    v->setModel(model_);
    v->setRootIsDecorated(false);
    v->setUniformRowHeights(true);
    v->setSelectionBehavior(QAbstractItemView::SelectRows);
    v->setSelectionMode(QAbstractItemView::SingleSelection);
    v->setEditTriggers(QAbstractItemView::NoEditTriggers); // F2 / F3 only
    v->setSortingEnabled(true);
    v->header()->setSortIndicator(Modified, Qt::DescendingOrder); // the original's default
    v->header()->setSectionResizeMode(Games, QHeaderView::Fixed); // FavoritesListColumnSizeChanging
    v->installEventFilter(this);
    connect(v, &QTreeView::doubleClicked, this, [this] { setActive(); if (selectedRow() >= 0) accept(); });
    connect(model_, &QStandardItemModel::itemChanged, this, &FavoritesManagerDialog::onEdited);

    readSettings();
    load(active_);
}

FavoritesManagerDialog::~FavoritesManagerDialog() = default;

void FavoritesManagerDialog::fillRow(int row, const FavoriteProfiles::Profile& p) {
    auto* title = new QStandardItem(p.title);
    title->setData(p.title.toLower(), SortRole);
    title->setData(p.file, FileRole);
    title->setEditable(!p.isDefault());
    if (p.isDefault()) {
        QFont f = title->font();
        f.setBold(true);
        title->setFont(f);
    }
    auto* games = new QStandardItem(QLocale().toString(p.games));
    games->setData(p.games, SortRole);
    games->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    games->setEditable(false);
    auto* file = new QStandardItem(p.file);
    file->setData(p.file.toLower(), SortRole);
    file->setEditable(!p.isDefault());
    QString when = p.modified.isValid() ? QLocale::c().toString(p.modified, "MMM/dd/yyyy HH:mm:ss") : QString();
    auto* mod = new QStandardItem(when); // GetDateTimeStr
    mod->setData(p.modified.isValid() ? p.modified.toMSecsSinceEpoch() : 0, SortRole);
    mod->setEditable(false);
    QList<QStandardItem*> items{title, games, file, mod};
    if (row < 0 || row >= model_->rowCount()) model_->appendRow(items);
    else for (int c = 0; c < ColCount; ++c) model_->setItem(row, c, items[c]);
}

// LoadFavoritesProfiles + SelectCurrentProfile
void FavoritesManagerDialog::load(const QString& select) {
    filling_ = true;
    model_->removeRows(0, model_->rowCount());
    for (const FavoriteProfiles::Profile& p : FavoriteProfiles::list(s_.folder)) fillRow(-1, p);
    ui_->FavoritesList->sortByColumn(ui_->FavoritesList->header()->sortIndicatorSection(),
                                     ui_->FavoritesList->header()->sortIndicatorOrder());
    filling_ = false;
    markActive();
    for (int r = 0; r < model_->rowCount(); ++r)
        if (fileAt(r).compare(select, Qt::CaseInsensitive) == 0) {
            QModelIndex i = model_->index(r, 0);
            ui_->FavoritesList->setCurrentIndex(i);
            ui_->FavoritesList->scrollTo(i);
        }
    ui_->FavoritesList->setFocus();
}

QString FavoritesManagerDialog::fileAt(int row) const { return model_->item(row, Title)->data(FileRole).toString(); }

int FavoritesManagerDialog::selectedRow() const {
    QModelIndex i = ui_->FavoritesList->currentIndex();
    return i.isValid() && ui_->FavoritesList->selectionModel()->isRowSelected(i.row(), {}) ? i.row() : -1;
}

void FavoritesManagerDialog::markActive() {
    filling_ = true;
    for (int r = 0; r < model_->rowCount(); ++r)
        model_->item(r, Title)->setIcon(fileAt(r).compare(active_, Qt::CaseInsensitive) == 0 ? s_.activeIcon : QIcon());
    filling_ = false;
}

bool FavoritesManagerDialog::needSelection(const QString& title) {
    if (selectedRow() >= 0) return true;
    QMessageBox::information(this, title, tr("Select a list first."));
    return false;
}

// ExecuteFavAction 0 (create) and 3 (replicate)
void FavoritesManagerDialog::createProfile(bool copy) {
    const QString title = copy ? tr("Replicate") : tr("New List");
    if (copy && !needSelection(title)) return;
    QString file = FavoriteProfiles::create(s_.folder, copy ? fileAt(selectedRow()) : QString());
    if (file.isEmpty()) {
        const QString full = QDir(s_.folder).filePath("favorites1000.txt");
        if (QFileInfo::exists(full))
            QMessageBox::warning(this, title,
                                 tr("A new list couldn't be made: the favorites folder already holds 1,000 lists, the most "
                                    "EmuLoader can number. Delete or rename some lists, then try again."));
        else
            FileProblem::cantSave(this, title, QDir(s_.folder).filePath("favorites1.txt"));
        return;
    }
    load(file);
}

// ExecuteFavAction 2: clear games
void FavoritesManagerDialog::clearGames() {
    const QString title = tr("Clear Games");
    if (!needSelection(title)) return;
    int r = selectedRow();
    QString file = fileAt(r);
    if (QMessageBox::question(this, title,
                              tr("Take every game off this list? The list itself stays, empty.\n\n"
                                 "Title: %1\nFile: %2\nModified: %3")
                                  .arg(model_->item(r, Title)->text(), file, model_->item(r, Modified)->text())) != QMessageBox::Yes)
        return;
    if (!FavoriteProfiles::clear(s_.folder, file)) {
        FileProblem::cantSave(this, title, QDir(s_.folder).filePath(file));
        return;
    }
    if (file.compare(active_, Qt::CaseInsensitive) == 0) activeContentsChanged_ = true;
    load(file);
}

// ExecuteFavAction 1: Clean Out (the original's Cleanse, RemoveInvalidEntries): the games that aren't
// in the games list, to take off the selected list; any to keep are taken off this list of them
// (TFormFavoritesManagerCleanseProfile). The games taken off can be put in a new list of their own.
void FavoritesManagerDialog::cleanse() {
    const QString title = tr("Clean Out");
    if (!needSelection(title)) return;
    const int row = selectedRow();
    const QString file = fileAt(row), listTitle = model_->item(row, Title)->text();
    QStringList all = FavoriteProfiles::entries(s_.folder, file);
    QStringList bad;
    for (const QString& l : all)
        if (s_.isValid && !s_.isValid(l)) bad << l;
    if (bad.isEmpty()) {
        QMessageBox::information(this, title, tr("Every game in \u201c%1\u201d is in the games list. There's nothing to clean out.").arg(listTitle));
        return;
    }
    QDialog dlg(this);
    dlg.setObjectName("CleanOutDialog");
    dlg.setWindowTitle(tr("Clean Out \u201c%1\u201d").arg(listTitle));
    auto* lay = new QVBoxLayout(&dlg);
    auto* top = new QLabel(tr("These games aren't in the games list: another MAME version may have renamed or removed them. "
                              "They'll be taken off the list. To keep any, select them and click Keep Selected."), &dlg);
    top->setWordWrap(true);
    auto* total = new QLabel(&dlg);
    total->setAlignment(Qt::AlignRight);
    auto* list = new QTreeView(&dlg);
    auto* m = new QStandardItemModel(0, 2, &dlg);
    m->setHorizontalHeaderLabels({tr("Name"), tr("System")});
    for (const QString& l : bad) {
        auto* n = new QStandardItem(l.section('=', 0, 0));
        n->setData(l);
        auto* sys = new QStandardItem(s_.systemOf ? s_.systemOf(l) : l.section('=', 1));
        m->appendRow({n, sys});
    }
    list->setModel(m);
    list->setRootIsDecorated(false);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setSelectionBehavior(QAbstractItemView::SelectRows);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list->setSortingEnabled(true);
    list->sortByColumn(0, Qt::AscendingOrder);
    list->setColumnWidth(0, 360);
    auto updateTotal = [&] {
        total->setText(m->rowCount() == 1 ? tr("1 of %1 games to take off").arg(all.size())
                                          : tr("%1 of %2 games to take off").arg(m->rowCount()).arg(all.size()));
    };
    updateTotal();
    auto* keep = new QPushButton(tr("Keep Selected (Delete key)"), &dlg);
    keep->setObjectName("CleanOutKeep");
    auto keepSelected = [&] {
        QModelIndexList rows = list->selectionModel()->selectedRows();
        std::sort(rows.begin(), rows.end(), [](const QModelIndex& a, const QModelIndex& b) { return a.row() > b.row(); });
        for (const QModelIndex& i : rows) m->removeRow(i.row());
        updateTotal();
    };
    QObject::connect(keep, &QPushButton::clicked, &dlg, keepSelected);
    auto* del = new QAction(&dlg);
    del->setShortcut(QKeySequence::Delete);
    QObject::connect(del, &QAction::triggered, &dlg, keepSelected);
    list->addAction(del);
    // where the games taken off go: nowhere, or a new list of their own
    auto* toList = new QCheckBox(tr("Put them in a new favorites list:"), &dlg);
    toList->setObjectName("CleanOutToNewList");
    auto* newTitle = new QLineEdit(tr("%1 (not in the games list)").arg(listTitle), &dlg);
    newTitle->setObjectName("CleanOutNewListTitle");
    newTitle->setEnabled(false);
    QObject::connect(toList, &QCheckBox::toggled, newTitle, &QWidget::setEnabled);
    auto* where = new QHBoxLayout;
    where->addWidget(toList);
    where->addWidget(newTitle, 1);
    auto* box = new QDialogButtonBox(&dlg);
    box->addButton(keep, QDialogButtonBox::ActionRole);
    QPushButton* go = box->addButton(tr("Clean Out"), QDialogButtonBox::AcceptRole);
    go->setObjectName("CleanOutGo");
    box->addButton(QDialogButtonBox::Cancel);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto* head = new QHBoxLayout;
    head->addWidget(top, 1);
    head->addWidget(total, 0, Qt::AlignBottom);
    lay->addLayout(head);
    lay->addWidget(list, 1);
    lay->addLayout(where);
    lay->addWidget(box);
    dlg.resize(730, 560);
    if (dlg.exec() != QDialog::Accepted || m->rowCount() == 0) return;
    QStringList gone;
    for (int r = 0; r < m->rowCount(); ++r) gone << m->item(r, 0)->data().toString();

    QString newFile;
    if (toList->isChecked()) { // first the new list (nothing changed yet if it can't be made)
        const QString t = newTitle->text().trimmed().isEmpty() ? tr("%1 (not in the games list)").arg(listTitle) : newTitle->text().trimmed();
        newFile = FavoriteProfiles::create(s_.folder);
        if (newFile.isEmpty() || !FavoriteProfiles::setTitle(s_.folder, newFile, t) || !FavoriteProfiles::addEntries(s_.folder, newFile, gone)) {
            FileProblem::cantSave(this, title, QDir(s_.folder).filePath(newFile.isEmpty() ? QStringLiteral("favorites1.txt") : newFile));
            return;
        }
    }
    if (FavoriteProfiles::removeEntries(s_.folder, file, gone) <= 0) {
        FileProblem::cantSave(this, title, QDir(s_.folder).filePath(file));
        return;
    }
    if (file.compare(active_, Qt::CaseInsensitive) == 0) activeContentsChanged_ = true;
    load(file);
    const int n = int(gone.size());
    QString said = n == 1 ? tr("1 game was taken off \u201c%1\u201d.").arg(listTitle)
                          : tr("%1 games were taken off \u201c%2\u201d.").arg(n).arg(listTitle);
    if (!newFile.isEmpty()) said += "\n" + tr("They're in the new list \u201c%1\u201d.").arg(FavoriteProfiles::title(s_.folder, newFile));
    QMessageBox::information(this, title, said);
}

// ExecuteFavAction 4: delete
void FavoritesManagerDialog::deleteProfile() {
    const QString title = tr("Delete List");
    if (!needSelection(title)) return;
    int r = selectedRow();
    QString file = fileAt(r);
    if (file.compare("favorites.txt", Qt::CaseInsensitive) == 0) {
        QMessageBox::information(this, title, tr("The default list (favorites.txt) can't be deleted. To empty it, use Clear Games."));
        return;
    }
    if (QMessageBox::question(this, title,
                              tr("Delete this list? Its file goes to the Recycle Bin.\n\nTitle: %1\nFile: %2\nGames: %3\n"
                                 "Modified: %4\n\nIf it's the list in use, the default list is used instead.")
                                  .arg(model_->item(r, Title)->text(), file, model_->item(r, Games)->text(),
                                       model_->item(r, Modified)->text())) != QMessageBox::Yes)
        return;
    if (moveToRecycleBin({QDir(s_.folder).filePath(file)}, this).isEmpty()) {
        QMessageBox::warning(this, title, tr("%1 couldn't be moved to the Recycle Bin, so the list wasn't deleted.").arg(file));
        return;
    }
    if (file.compare(active_, Qt::CaseInsensitive) == 0) {
        active_ = "favorites.txt";
        activeContentsChanged_ = true;
    }
    load(active_);
}

// ButtonSetSelectedProfileActiveClick
void FavoritesManagerDialog::setActive() {
    int r = selectedRow();
    if (r < 0) {
        QMessageBox::information(this, tr("Use the Selected List"), tr("Select a list first."));
        return;
    }
    active_ = fileAt(r);
    markActive();
}

// EditTitleFileName: F2 the title, F3 the file name; the default profile stays as it is.
void FavoritesManagerDialog::edit(int column) {
    if (s_.selectOnly) return;
    int r = selectedRow();
    if (r < 0) return;
    if (fileAt(r).compare("favorites.txt", Qt::CaseInsensitive) == 0) {
        QMessageBox::information(this, tr("Edit List"), tr("The default list's title and file name can't be changed."));
        return;
    }
    ui_->FavoritesList->edit(model_->index(r, column));
}

// FavoritesListItemEdited: a title or a file name must be new among the profiles.
void FavoritesManagerDialog::onEdited(QStandardItem* item) {
    if (filling_) return;
    int r = item->row();
    QString file = fileAt(r);
    QString value = item->text().trimmed();
    bool ok = !value.isEmpty();
    if (item->column() == Title && ok) {
        for (int i = 0; i < model_->rowCount() && ok; ++i)
            if (i != r && model_->item(i, Title)->text().compare(value, Qt::CaseInsensitive) == 0) ok = false;
        ok = ok && FavoriteProfiles::setTitle(s_.folder, file, value);
        if (!ok) QMessageBox::warning(this, tr("Change Title"), tr("A list's title can't be empty or the same as another list's."));
    } else if (item->column() == FileName && ok) {
        QString to = QFileInfo(value).completeBaseName() + ".txt";
        ok = to.compare(file, Qt::CaseInsensitive) == 0 || FavoriteProfiles::rename(s_.folder, file, to);
        if (!ok)
            QMessageBox::warning(this, tr("Rename File"), tr("%1 couldn't be renamed to %2: another list has that name, or the name "
                                                             "isn't allowed in a file name.").arg(file, to));
        else if (active_.compare(file, Qt::CaseInsensitive) == 0) active_ = to;
        if (ok) file = to;
    }
    // Whatever happened, show the files as they are now.
    QMetaObject::invokeMethod(this, [this, file] { load(file); }, Qt::QueuedConnection);
}

bool FavoritesManagerDialog::eventFilter(QObject* o, QEvent* e) {
    // Keys an open title/file name editor passes on (Enter ends the edit) aren't the list's.
    if (o == ui_->FavoritesList && e->type() == QEvent::KeyPress && QApplication::focusWidget() == ui_->FavoritesList) {
        switch (static_cast<QKeyEvent*>(e)->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            setActive();
            if (selectedRow() >= 0) accept();
            return true;
        case Qt::Key_Space: setActive(); return true;
        case Qt::Key_F2: edit(Title); return true;
        case Qt::Key_F3: edit(FileName); return true;
        case Qt::Key_Delete: if (!s_.selectOnly) deleteProfile(); return true;
        default: break;
        }
    }
    return QDialog::eventFilter(o, e);
}

// FormCloseQuery: the settings are kept whichever way the window closes.
void FavoritesManagerDialog::done(int r) {
    writeSettings();
    QDialog::done(r);
}

void FavoritesManagerDialog::readSettings() {
    WinIni ini;
    ini.load(s_.extrasIni);
    const QString sec = "FavoritesManager";
    resize(ini.readInteger(sec, "ScreenWidth", 781), ini.readInteger(sec, "ScreenHeight", 401) + ui_->NewFavoritePanel->height() - 47);
    const char* keys[ColCount] = {"ColumnTitleWidth", "ColumnGamesCountWidth", "ColumnFileNameWidth", "ColumnDateModifiedWidth"};
    for (int c = 0; c < ColCount; ++c) ui_->FavoritesList->setColumnWidth(c, ini.readInteger(sec, keys[c], kDefaultWidths[c]));
    if (ini.readString(sec, "WindowState", "Normal") == "Maximized") setWindowState(Qt::WindowMaximized);
}

void FavoritesManagerDialog::writeSettings() {
    WinIni ini;
    ini.load(s_.extrasIni);
    const QString sec = "FavoritesManager";
    ini.writeString(sec, "WindowState", isMaximized() ? "Maximized" : "Normal");
    if (!isMaximized()) {
        ini.writeInteger(sec, "ScreenWidth", width());
        ini.writeInteger(sec, "ScreenHeight", height() - (ui_->NewFavoritePanel->height() - 47));
    }
    const char* keys[ColCount] = {"ColumnTitleWidth", "ColumnGamesCountWidth", "ColumnFileNameWidth", "ColumnDateModifiedWidth"};
    for (int c = 0; c < ColCount; ++c) ini.writeInteger(sec, keys[c], ui_->FavoritesList->columnWidth(c));
    ini.save(s_.extrasIni);
}

} // namespace eld
