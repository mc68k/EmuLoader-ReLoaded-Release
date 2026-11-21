#include "ImagesDeleteClonesDialog.h"

#include "RecycleBin.h"

#include "ProgressWindow.h"
#include "VclRuntime.h"
#include "WinIni.h"
#include "ui_FormImagesDeleteClones.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace eld {

namespace {
enum Role { NameRole = Qt::UserRole + 1, CloneRole, FileRole, ParentFileRole };
const char* kSection = "ImagesDeleteClones";
} // namespace

ImagesDeleteClonesDialog::ImagesDeleteClonesDialog(const Setup& setup, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormImagesDeleteClones), s_(setup) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Delete MAME and Arcade Clone Images"));
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);

    // Top bar: the system and the image category (click the category to pick another)
    ui_->SystemIcon->setPixmap(s_.systemIcon.pixmap(24, 24));
    ui_->LabelSystem->setText(s_.system);
    ui_->SystemIcon->setToolTip(QString());
    ui_->LabelSystem->setToolTip(QString());
    for (QWidget* w : {(QWidget*)ui_->ImageCategoryIcon, (QWidget*)ui_->LabelImageCategory}) {
        w->setToolTip(tr("Click here to select an image category"));
        w->setCursor(Qt::PointingHandCursor);
        w->installEventFilter(this);
    }
    struct B { QPushButton* b; const char* text; const char* tip; };
    const B buttons[] = {
        {ui_->ButtonScan, QT_TR_NOOP("Start Scan"), QT_TR_NOOP("Click here to search the games list for clone images")},
        {ui_->ButtonRemoveSelected, QT_TR_NOOP("Remove Selected"), QT_TR_NOOP("Remove selected games from the list (their images are kept)")},
        {ui_->ButtonRenameToParent, QT_TR_NOOP("Rename to Parent"), QT_TR_NOOP("Rename images to the parent game's name (all selected games)")},
        {ui_->ButtonDeleteImages, QT_TR_NOOP("Delete All"), QT_TR_NOOP("Delete images of all clone games on the list")},
        {ui_->ButtonDeleteSelected, QT_TR_NOOP("Delete Selected"), QT_TR_NOOP("Delete images of all selected games on the list")},
        {ui_->ButtonHelp, QT_TR_NOOP("Help"), QT_TR_NOOP("How do I use this thing? :)")}};
    for (const B& b : buttons) {
        b.b->setText(tr(b.text));
        b.b->setToolTip(tr(b.tip));
        b.b->setAutoDefault(false);
    }
    // In layouts: the system and the category (with Help) on top, then one row of buttons: Start
    // Scan | Remove Selected, Rename to Parent | Delete All, Delete Selected
    {
        using vcl::boxed, vcl::loose;
        QWidget* top = ui_->PanelTop;
        auto* v = boxed(new QVBoxLayout(top), 1, 3, 3, 3, 3);
        auto* first = boxed(new QHBoxLayout, 0, 0, 0, 0, 3);
        for (QLabel* l : {(QLabel*)ui_->LabelSystem, (QLabel*)ui_->LabelImageCategory}) loose(l);
        first->addWidget(ui_->SystemIcon);
        first->addWidget(ui_->LabelSystem, 0, Qt::AlignVCenter);
        first->addSpacing(6);
        first->addWidget(ui_->ImageCategoryIcon);
        first->addWidget(ui_->LabelImageCategory, 0, Qt::AlignVCenter);
        first->addSpacing(16);
        first->addWidget(vcl::textButton(ui_->ButtonHelp));
        ui_->ButtonHelp->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed); // (its text's width)
        ui_->ButtonHelp->setFixedWidth(ui_->ButtonHelp->minimumWidth());
        first->addStretch();
        auto* second = boxed(new QHBoxLayout, 2, 0, 0, 0, 6);
        second->addWidget(vcl::textButton(ui_->ButtonScan));
        second->addSpacing(20);
        second->addWidget(vcl::textButton(ui_->ButtonRemoveSelected));
        second->addWidget(vcl::textButton(ui_->ButtonRenameToParent));
        second->addSpacing(20);
        second->addWidget(vcl::textButton(ui_->ButtonDeleteImages));
        second->addWidget(vcl::textButton(ui_->ButtonDeleteSelected));
        second->addStretch();
        v->addLayout(first);
        v->addLayout(second);
        top->setProperty("vclLaidOut", true);
        top->setFixedHeight(std::max(57, v->sizeHint().height()));
    }
    connect(ui_->ButtonScan, &QPushButton::clicked, this, &ImagesDeleteClonesDialog::scan);
    connect(ui_->ButtonRemoveSelected, &QPushButton::clicked, this, &ImagesDeleteClonesDialog::removeSelected);
    connect(ui_->ButtonRenameToParent, &QPushButton::clicked, this, &ImagesDeleteClonesDialog::renameToParent);
    connect(ui_->ButtonDeleteImages, &QPushButton::clicked, this, [this] { deleteImages(false); });
    connect(ui_->ButtonDeleteSelected, &QPushButton::clicked, this, [this] { deleteImages(true); });
    connect(ui_->ButtonHelp, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, tr("Help"), tr(
            "Select an image category, then click Start Scan. The clones in the games list (as it's shown, with "
            "its filters) that have pictures of their own in that category are listed; pictures in zip files "
            "aren't. A new scan always starts a new list.\n\n"
            "Then:\n"
            "1. Remove from the list the games whose pictures you want to keep: Remove Selected, or the Delete key.\n"
            "2. A clone that has pictures when its parent has none can give them to its parent: Rename to Parent.\n"
            "3. Delete the pictures of every clone listed (Delete All) or of the selected ones (Delete Selected). "
            "They go to the Recycle Bin.\n"
            "4. Pick another category and start again.\n\n"
            "The parent's picture is shown under the clone's: when they're the same, the parent's is all you need. "
            "Several games can be selected with Ctrl or Shift."));
    });

    // The list: Title, Name, Clone of
    model_ = new QStandardItemModel(0, 3, this);
    model_->setHorizontalHeaderLabels({tr("Title"), tr("Name"), tr("Clone of")});
    QTreeView* list = ui_->DeleteClonesList;
    list->setModel(model_);
    list->setRootIsDecorated(false);
    list->setUniformRowHeights(true);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setSelectionBehavior(QAbstractItemView::SelectRows);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list->setSortingEnabled(true);
    list->header()->setSortIndicator(0, Qt::AscendingOrder);
    list->header()->setStretchLastSection(false);
    list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list->setColumnWidth(1, 110);
    list->setColumnWidth(2, 110);
    list->installEventFilter(this);
    connect(list->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &ImagesDeleteClonesDialog::showSelected);

    for (QLabel* l : {ui_->LabelTextInfoScr1, ui_->LabelTextInfoParentScr1}) l->setAlignment(Qt::AlignCenter);
    ui_->LabelTextInfoScr1->setText(tr("GAME"));
    ui_->LabelTextInfoParentScr1->setText(tr("PARENT GAME"));
    for (QLabel* l : {ui_->ImageScr1, ui_->ImageParentScr1}) {
        l->setAlignment(Qt::AlignCenter);
        l->setAutoFillBackground(true);
    }
    ui_->PanelImages->installEventFilter(this);

    // ReadSettings
    WinIni ini;
    ini.load(s_.extrasIni);
    resize(ini.readInteger(kSection, "ScreenWidth", 1006), ini.readInteger(kSection, "ScreenHeight", 610));
    ui_->PanelImages->setProperty("vclWidth", ini.readInteger(kSection, "SplitterPos", 352));
    ui_->PanelImages->resize(ini.readInteger(kSection, "SplitterPos", 352), ui_->PanelImages->height());
    vcl::relayout(this);
    setCategory(s_.category);
    updateCount();
}

ImagesDeleteClonesDialog::~ImagesDeleteClonesDialog() = default;

void ImagesDeleteClonesDialog::done(int r) {
    WinIni ini; // WriteSettings
    ini.load(s_.extrasIni);
    ini.eraseSection(kSection);
    ini.writeInteger(kSection, "ScreenWidth", width());
    ini.writeInteger(kSection, "ScreenHeight", height());
    ini.writeInteger(kSection, "SplitterPos", ui_->PanelImages->width());
    ini.save(s_.extrasIni);
    QDialog::done(r);
}

void ImagesDeleteClonesDialog::setCategory(int c) {
    category_ = std::clamp(c, 0, ImageFinder::Count - 1);
    ui_->ImageCategoryIcon->setPixmap(s_.categoryIcon ? s_.categoryIcon(category_).pixmap(24, 24) : QPixmap());
    ui_->LabelImageCategory->setText(ImageFinder::title(category_));
    // FixBackgroundColor
    QColor bg = s_.background ? s_.background(category_) : QColor(Qt::black);
    for (QLabel* l : {ui_->ImageScr1, ui_->ImageParentScr1}) {
        QPalette p = l->palette();
        p.setColor(QPalette::Window, bg);
        l->setPalette(p);
    }
    model_->removeRows(0, model_->rowCount()); // the list was for the old category
    showSelected();
    updateCount();
}

// SelectImageCategory: the categories, in a menu under the label.
void ImagesDeleteClonesDialog::selectCategory() {
    QMenu m(this);
    for (int c = 0; c < ImageFinder::Count; ++c) {
        QAction* a = m.addAction(s_.categoryIcon ? s_.categoryIcon(c) : QIcon(), ImageFinder::title(c));
        a->setCheckable(true);
        a->setChecked(c == category_);
        a->setData(c);
    }
    QAction* a = m.exec(ui_->LabelImageCategory->mapToGlobal(QPoint(0, ui_->LabelImageCategory->height())));
    if (a && a->data().toInt() != category_) setCategory(a->data().toInt());
}

void ImagesDeleteClonesDialog::updateCount() {
    bool any = model_->rowCount() > 0;
    for (QPushButton* b : {ui_->ButtonRemoveSelected, ui_->ButtonRenameToParent, ui_->ButtonDeleteImages, ui_->ButtonDeleteSelected})
        b->setEnabled(any);
    setWindowTitle(any ? tr("Delete MAME and Arcade Clone Images") + "  [" + tr("Games: %1").arg(model_->rowCount()) + "]"
                       : tr("Delete MAME and Arcade Clone Images"));
}

// BuildGamesList: the clones (not BIOS or device sets) with a picture of their own.
void ImagesDeleteClonesDialog::scan() {
    model_->removeRows(0, model_->rowCount());
    showSelected();
    if (!s_.finder) return;
    ProgressWindow win(tr("Scan Games Images"), this);
    win.setCancelable(true);
    win.step(tr("Validating images of clone games."), int(s_.games.size()));
    int done = 0;
    QFont listFont = ui_->DeleteClonesList->font();
    for (const MameGame& g : s_.games) {
        if (++done % 200 == 0) {
            win.setProgress(done, {});
            if (win.canceled()) break;
        }
        if (g.cloneOf.isEmpty() || g.kind == SetKind::Bios || g.kind == SetKind::Device) continue;
        QStringList mine = s_.finder->gameFiles(category_, g.imageName()); // (nes/smb: a software list game)
        if (mine.isEmpty()) continue;
        QStringList parents = s_.finder->gameFiles(category_, g.imageParent());
        auto* title = new QStandardItem(s_.gameIcon ? s_.gameIcon(g) : QIcon(), g.title);
        title->setData(g.imageName(), NameRole);
        title->setData(g.imageParent(), CloneRole);
        title->setData(mine.first(), FileRole);
        title->setData(parents.value(0), ParentFileRole);
        title->setToolTip(QDir::toNativeSeparators(mine.join('\n')));
        if (g.setStatus == 1) title->setForeground(QColor(0xC0, 0, 0)); // missing ROMs: red, as the games list
        model_->appendRow({title, new QStandardItem(g.imageName()), new QStandardItem(g.imageParent())});
    }
    win.close();
    ui_->DeleteClonesList->sortByColumn(ui_->DeleteClonesList->header()->sortIndicatorSection(),
                                        ui_->DeleteClonesList->header()->sortIndicatorOrder());
    updateCount();
    if (model_->rowCount()) ui_->DeleteClonesList->setCurrentIndex(model_->index(0, 0));
    else QMessageBox::information(this, windowTitle(), tr("No clone has pictures of its own in this category."));
    ui_->DeleteClonesList->setFocus();
}

void ImagesDeleteClonesDialog::showImage(QLabel* view, QLabel* caption, const QString& file, const QString& title) {
    QPixmap pm;
    if (!file.isEmpty()) pm.load(file);
    if (pm.isNull()) {
        view->clear();
        caption->setText(title);
        return;
    }
    QSize box = view->size();
    view->setPixmap(pm.width() > box.width() || pm.height() > box.height()
                        ? pm.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation) : pm);
    caption->setText(title + " -- " + QFileInfo(file).fileName().toUpper());
}

// LoadImages: the clone's picture on top, its parent's under it.
void ImagesDeleteClonesDialog::showSelected() {
    QModelIndex i = ui_->DeleteClonesList->currentIndex();
    QStandardItem* it = i.isValid() ? model_->item(i.row(), 0) : nullptr;
    showImage(ui_->ImageScr1, ui_->LabelTextInfoScr1, it ? it->data(FileRole).toString() : QString(), tr("GAME"));
    showImage(ui_->ImageParentScr1, ui_->LabelTextInfoParentScr1, it ? it->data(ParentFileRole).toString() : QString(),
              tr("PARENT GAME"));
}

void ImagesDeleteClonesDialog::removeSelected() {
    QList<int> rows;
    for (const QModelIndex& i : ui_->DeleteClonesList->selectionModel()->selectedRows()) rows << i.row();
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int r : rows) model_->removeRow(r);
    updateCount();
    showSelected();
    ui_->DeleteClonesList->setFocus();
}

// RenameImages: each selected clone's pictures get its parent's name, unless the parent has a
// picture of that name already.
void ImagesDeleteClonesDialog::renameToParent() {
    const auto rows = ui_->DeleteClonesList->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;
    int renamed = 0, kept = 0;
    for (const QModelIndex& i : rows) {
        QStandardItem* it = model_->item(i.row(), 0);
        QString name = it->data(NameRole).toString(), parent = it->data(CloneRole).toString();
        QString first;
        for (const QString& f : s_.finder->gameFiles(category_, name)) {
            QString to = ImageFinder::renamedFor(f, name, parent);
            if (to.isEmpty()) continue;
            if (QFileInfo::exists(to)) { ++kept; continue; }
            QDir().mkpath(QFileInfo(to).absolutePath());
            if (QFile::rename(f, to)) {
                ++renamed;
                if (first.isEmpty()) first = to;
            }
        }
        if (!first.isEmpty()) {
            changed_ = true;
            QStringList left = s_.finder->gameFiles(category_, name);
            it->setData(left.value(0), FileRole);
            if (it->data(ParentFileRole).toString().isEmpty()) it->setData(first, ParentFileRole);
        }
    }
    showSelected();
    QString msg = tr("Pictures renamed: %1").arg(renamed);
    if (kept) msg += "\n" + tr("Pictures kept, because the parent has a picture of that name: %1").arg(kept);
    QMessageBox::information(this, tr("Rename to Parent"), msg);
    ui_->DeleteClonesList->setFocus();
}

// DeleteFiles: every picture (1..99, PNG and JPG) of the listed or selected clones.
void ImagesDeleteClonesDialog::deleteImages(bool selectedOnly) {
    QList<int> rows;
    if (selectedOnly)
        for (const QModelIndex& i : ui_->DeleteClonesList->selectionModel()->selectedRows()) rows << i.row();
    else
        for (int r = 0; r < model_->rowCount(); ++r) rows << r;
    if (rows.isEmpty()) return;
    QString ask = selectedOnly
        ? tr("You are about to delete the images of the selected games (%1). Images of parent games will not be deleted.").arg(rows.size())
        : tr("You are about to delete the images of all the games in the list (%1). Images of parent games will not be deleted.").arg(rows.size());
    if (QMessageBox::question(this, tr("Delete clone images."), ask + "\n\n" + tr("They go to the Recycle Bin. Continue?")) !=
        QMessageBox::Yes)
        return;
    ProgressWindow win(tr("Delete Clone Images"), this);
    win.step(tr("Deleting all images (.png; .jpg)..."), int(rows.size()));
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    int n = 0, failed = 0, step = 0;
    for (int r : rows) {
        win.setProgress(++step, {});
        QString name = model_->item(r, 0)->data(NameRole).toString();
        bool any = false;
        const QStringList files = s_.finder->gameFiles(category_, name);
        const QStringList gone = moveToRecycleBin(files, this);
        n += int(gone.size());
        failed += int(files.size() - gone.size());
        any = !gone.isEmpty();
        // MAME's <name>\ folder, when it's empty now
        for (const QString& dir : s_.finder->folders(category_)) QDir(dir).rmdir(name);
        if (any) model_->removeRow(r);
    }
    win.close();
    changed_ |= n > 0;
    updateCount();
    showSelected();
    QString msg = tr("Pictures deleted: %1").arg(n);
    if (failed) msg += "\n" + tr("Pictures that could not be deleted: %1").arg(failed);
    QMessageBox::information(this, tr("Delete clone images."), msg);
    ui_->DeleteClonesList->setFocus();
}

bool ImagesDeleteClonesDialog::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::MouseButtonRelease && (o == ui_->ImageCategoryIcon || o == ui_->LabelImageCategory)) {
        selectCategory();
        return true;
    }
    if (o == ui_->DeleteClonesList && e->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(e)->key() == Qt::Key_Delete) {
        removeSelected();
        return true;
    }
    if (o == ui_->PanelImages && e->type() == QEvent::Resize) { // PanelImagesResize: two halves
        int h = ui_->PanelImages->height() / 2, w = ui_->PanelImages->width();
        vcl::place(ui_->PanelGameScr1, QRect(0, 0, w, h));
        vcl::place(ui_->PanelParentGameScr1, QRect(0, h, w, ui_->PanelImages->height() - h));
        for (auto [panel, bar, label, img] : {std::tuple{ui_->PanelGameScr1, ui_->BarTextInfoScr1, ui_->LabelTextInfoScr1, ui_->ImageScr1},
                                              std::tuple{ui_->PanelParentGameScr1, ui_->BarTextInfoParentScr1, ui_->LabelTextInfoParentScr1, ui_->ImageParentScr1}}) {
            vcl::place(bar, QRect(0, 0, panel->width(), 20));
            vcl::place(label, QRect(0, 0, panel->width(), 20));
            vcl::place(img, QRect(0, 20, panel->width(), panel->height() - 20));
        }
        showSelected();
    }
    return QDialog::eventFilter(o, e);
}

} // namespace eld
