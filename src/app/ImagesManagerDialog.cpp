#include "ImagesManagerDialog.h"

#include "ProgressWindow.h"
#include "RecycleBin.h"
#include "TextFile.h"
#include "VclRuntime.h"
#include "WinIni.h"
#include "ElWidgets.h"
#include "ui_FormImagesManager.h"

#include <QCheckBox>
#include <QRadioButton>
#include <QHBoxLayout>
#include <QStorageInfo>
#include <QBuffer>
#include <QPlainTextEdit>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QImageReader>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace eld {

namespace {
const char* kSection = "ImagesManager";
enum Role { NameRole = Qt::UserRole + 1, CloneRole, FileRole, OriginalRole, SortRole, EntryRole, StoredRole, DateRole };
// Title, Name, Clone of, Driver, Status (PopupRestoreColumnsSizes); Images, Where (Scan Category)
const int kGameColumns[] = {270, 85, 85, 90, 85, 60, 300};
const int kGameColumnCount = 7;

QString sizeText(qint64 bytes) {
    if (bytes < 1024) return ImagesManagerDialog::tr("%1 Bytes").arg(bytes);
    return QLocale().formattedDataSize(bytes, 2, QLocale::DataSizeTraditionalFormat);
}

// The question before deleting: the zip part only when some pictures are inside zip files
QString deleteQuestion(const QString& first, int zipped) {
    QString q = first + " " + ImagesManagerDialog::tr("They go to the Recycle Bin; a file that can't be deleted stays in the list.");
    if (zipped)
        q += "\n\n" + ImagesManagerDialog::tr("%1 of them are inside zip files: they are taken out when you click Apply Zip Changes "
                                               "(it appears at the bottom right) or close the window.").arg(zipped);
    return q + "\n\n" + ImagesManagerDialog::tr("Continue?");
}

// Rows taken out of a list in runs (one at a time is slow for thousands)
void removeItems(QStandardItemModel* model, const QList<QStandardItem*>& items) {
    QHash<QStandardItem*, QList<int>> rows; // parent (or the model's root) -> rows
    for (QStandardItem* it : items) rows[it->parent() ? it->parent() : model->invisibleRootItem()] << it->row();
    for (auto p = rows.begin(); p != rows.end(); ++p) {
        QList<int>& r = p.value();
        std::sort(r.begin(), r.end(), std::greater<int>());
        r.erase(std::unique(r.begin(), r.end()), r.end());
        for (int i = 0; i < r.size();) {
            int j = i;
            while (j + 1 < r.size() && r[j + 1] == r[j] - 1) ++j;
            p.key()->removeRows(r[j], j - i + 1);
            i = j + 1;
        }
    }
}

// "D:\mame\snap.zip" + "pacman.png" ->"D:\mame\snap.zip|pacman.png" (Save List To File)
QString joined(const QString& file, const QString& entry) {
    return entry.isEmpty() ? QDir::toNativeSeparators(file) : QDir::toNativeSeparators(file) + "|" + entry;
}
} // namespace

ImagesManagerDialog::ImagesManagerDialog(const Setup& setup, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormImagesManager), s_(setup) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("MAME Images Manager"));
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setMinimumSize(931, 400);

    // Tool bar
    for (QWidget* w : {(QWidget*)ui_->ImageCategoryIcon, (QWidget*)ui_->ImageCategorySelectLabel}) {
        w->setCursor(Qt::PointingHandCursor);
        w->setToolTip(tr("Click here to select an image category"));
        w->installEventFilter(this);
    }
    struct B { QPushButton* b; const char* text; const char* tip; };
    const B buttons[] = {
        {ui_->ButtonScanMissing, QT_TR_NOOP("Scan Missing Images"), QT_TR_NOOP("Click here to scan games with missing images")},
        {ui_->ButtonScanNotUsedImages, QT_TR_NOOP("Images of Games You Don't Have"), QT_TR_NOOP("Click here to list the games you don't have that have an image")},
        {ui_->ButtonScanInvalidImages, QT_TR_NOOP("Scan Invalid Images"), QT_TR_NOOP("Click here to scan unknown image file names, not used by any game")},
        {ui_->ButtonHelp, QT_TR_NOOP("Help"), QT_TR_NOOP("How do I use this thing? :)")},
        {ui_->ButtonNotUsedImagesDeleteFiles, QT_TR_NOOP("Delete Not Used Images"), QT_TR_NOOP("Delete the images of the missing games listed")},
        {ui_->ButtonInvalidImagesDeleteFiles, QT_TR_NOOP("Delete Invalid Images"), QT_TR_NOOP("Delete all the files listed")}};
    for (const B& b : buttons) {
        b.b->setAutoDefault(false);
        b.b->setText(tr(b.text));
        b.b->setToolTip(tr(b.tip));
    }
    // new buttons made like the window's own
    auto likeThis = [](QPushButton* model) {
        auto* b = new ElButton(model->parentWidget());
        for (const QByteArray& name : model->dynamicPropertyNames()) b->setProperty(name, model->property(name));
        b->setFont(model->font());
        b->setCursor(model->cursor());
        return b;
    };
    scanAll_ = likeThis(ui_->ButtonHelp);
    scanAll_->setText(tr("Scan All Categories"));
    scanAll_->setObjectName("ButtonScanAllCategories");
    scanAll_->setAutoDefault(false);
    scanAll_->setToolTip(tr("Count the images, games without an image and invalid files of every image category"));
    connect(scanAll_, &QPushButton::clicked, this, [this] { scanAll(true); });
    scanCategory_ = likeThis(ui_->ButtonScanMissing);
    scanCategory_->setText(tr("Scan Category"));
    scanCategory_->setObjectName("ButtonScanCategory");
    scanCategory_->setAutoDefault(false);
    scanCategory_->setToolTip(tr("List the games that have a valid image in this category"));
    connect(scanCategory_, &QPushButton::clicked, this, [this] { scanValid(); });
    scanOptions_ = likeThis(ui_->ButtonHelp);
    scanOptions_->setText(tr("Scan Options") + QStringLiteral(" \u25BE"));
    scanOptions_->setObjectName("ButtonScanOptions");
    scanOptions_->setAutoDefault(false);
    scanOptions_->setToolTip(tr("Which games the scans list: clones, games you don't have, BIOS and device sets..."));
    zipButton_ = likeThis(ui_->ButtonHelp);
    zipButton_->setText(tr("Zip Images") + QStringLiteral(" \u25BE"));
    zipButton_->setObjectName("ButtonZipImages");
    zipButton_->setAutoDefault(false);
    zipButton_->setToolTip(tr("Put the images into a zip file in their folder (snap\\snap.zip)"));
    connect(zipButton_, &QPushButton::clicked, this, [this] {
        QMenu m(this);
        m.addAction(tr("This Category (%1)").arg(ImageFinder::title(category_)), this, [this] { zipImages(ZipCategory); });
        QAction* sel = m.addAction(tr("Selected Games' Images"), this, [this] { zipImages(ZipSelected); });
        sel->setEnabled(mode_ == Valid && !ui_->MissingImagesList->selectionModel()->selectedRows().isEmpty());
        if (!sel->isEnabled()) sel->setToolTip(tr("Select games in the Scan Category list first"));
        m.addSeparator();
        m.addAction(tr("All Categories..."), this, [this] { zipImages(ZipAllCategories); });
        m.setToolTipsVisible(true);
        m.exec(zipButton_->mapToGlobal(QPoint(0, zipButton_->height())));
    });
    unzipButton_ = likeThis(ui_->ButtonHelp);
    unzipButton_->setText(tr("Unzip Images"));
    unzipButton_->setObjectName("ButtonUnzipImages");
    unzipButton_->setAutoDefault(false);
    unzipButton_->setToolTip(tr("Unpack the category's zip files into their folders"));
    connect(unzipButton_, &QPushButton::clicked, this, [this] { unzipImages(); });
    connect(scanOptions_, &QPushButton::clicked, this, [this] {
        QMenu m(this);
        for (QAction* a : {searchClones_, scanMissingGames_, scanBios_, scanDevices_, scanArcade_, scanNonArcade_}) m.addAction(a);
        m.exec(scanOptions_->mapToGlobal(QPoint(0, scanOptions_->height())));
    });
    applyZips_ = likeThis(ui_->ButtonInvalidImagesDeleteFiles);
    applyZips_->setObjectName("ButtonApplyZipChanges");
    applyZips_->setAutoDefault(false);
    applyZips_->setToolTip(tr("Write the changes to the zip files now. The pictures taken out go to the Recycle Bin."));
    applyZips_->hide();
    connect(applyZips_, &QPushButton::clicked, this, [this] { applyZipChanges(); });
    connect(ui_->ButtonScanMissing, &QPushButton::clicked, this, [this] { scanMissing(false); });
    connect(ui_->ButtonScanNotUsedImages, &QPushButton::clicked, this, [this] { scanMissing(true); });
    connect(ui_->ButtonScanInvalidImages, &QPushButton::clicked, this, [this] { scanInvalid(); });
    connect(ui_->ButtonNotUsedImagesDeleteFiles, &QPushButton::clicked, this, [this] { deleteNotUsed(); }); // (clicked(bool) must not reach "ask")
    connect(ui_->ButtonInvalidImagesDeleteFiles, &QPushButton::clicked, this, [this] { deleteInvalid(); });
    connect(ui_->ButtonHelp, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, tr("Images Manager"), tr(
            "Games With an Image\n"
            "Click Scan Category: the games with a valid image in the category are listed, with how many images each one "
            "has and where they are; the selected game's image is shown on the right. With Search Clone Images, a clone "
            "without an image of its own shows its parent's.\n\n"
            "Games With Missing Images\n"
            "Select an image category (Scan Options says which games are scanned) and click Scan Missing "
            "Images. To take a snapshot, run the selected game with Enter, a double click or Play in the right-click "
            "menu. Remove games from the list with the Delete key or Remove Selected from List; Save Games List To File "
            "exports the list to a .txt file.\n\n"
            "Invalid Image File Names\n"
            "Click Scan Invalid Images: the files in the category's folders that aren't any game's picture are "
            "listed on the right. Remove the ones you want to keep from the list (Delete key), then click Delete "
            "Invalid Images to delete the others. Files can also be renamed (F2) or moved to another folder.\n\n"
            "Images of Games You Don't Have\n"
            "Click Images of Games You Don't Have: the games you don't have that have a picture are listed, with the "
            "picture on the right. Remove the games whose pictures you want to keep, then click Delete Not Used "
            "Images.\n\n"
            "Deleted files go to the Recycle Bin.\n\n"
            "Zip Files\n"
            "The pictures inside the category's zip files (snap.zip, flyers.zip...) are listed too. Deleting, renaming "
            "or moving them is written to the zip file all at once: click Apply Zip Changes, or close the window. A new "
            "zip file is written and takes the old one's place once it has been checked; the pictures taken out go to "
            "the Recycle Bin.\n\n"
            "Scan All Categories\n"
            "Counts the images, the games without an image, the images of games you don't have and the invalid files of every "
            "category, with the scan options. Double click a count to list it.\n\n"
            "Zip Images / Unzip Images\n"
            "Zip Images puts the category's valid images into a zip file in their folder (snap\\snap.zip), for "
            "the category, the games selected in the Scan Category list, or every category. The images are stored as "
            "they are; afterwards the folder's copies can go to the Recycle Bin. Unzip Images unpacks the category's "
            "zip files into their folders.\n\n"
            "Icons (MAMu_)\n"
            "The last category is the MAMu_ icons (the MAMu_ Icons Manager): the same lists and tools for the .ico files "
            "of the icons folder and icons.zip. Driver icons and deviceset.ico aren't invalid; the excluded list "
            "(right-click the invalid icons) holds icons that never are. Create / Edit Icon (right-click a game) "
            "makes a blank icon for it and opens it in your icon editor."));
    });

    // The games list (MissingImagesList)
    games_ = new QStandardItemModel(0, kGameColumnCount, this);
    games_->setHorizontalHeaderLabels({tr("Title"), tr("Name"), tr("Clone of"), tr("Driver"), tr("Status"), tr("Images"), tr("Where")});
    games_->setSortRole(SortRole);
    QTreeView* gl = ui_->MissingImagesList;
    gl->setModel(games_);
    gl->setRootIsDecorated(false);
    gl->setUniformRowHeights(true);
    gl->setSelectionMode(QAbstractItemView::ExtendedSelection);
    gl->setSelectionBehavior(QAbstractItemView::SelectRows);
    gl->setEditTriggers(QAbstractItemView::NoEditTriggers);
    gl->setSortingEnabled(true);
    gl->header()->setSortIndicator(0, Qt::AscendingOrder);
    gl->setContextMenuPolicy(Qt::CustomContextMenu);
    gl->installEventFilter(this);
    restoreColumns();
    gl->header()->moveSection(5, 2); // Images and Where after Name (Scan Category)
    gl->header()->moveSection(6, 3);
    showGameColumns(false);
    connect(gl->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &ImagesManagerDialog::missingSelected);
    connect(gl, &QTreeView::doubleClicked, this, &ImagesManagerDialog::playSelected);

    gamesMenu_ = new QMenu(this);
    gamesMenu_->addAction(tr("Play"), this, &ImagesManagerDialog::playSelected);
    createIcon_ = gamesMenu_->addAction(tr("Create / Edit Icon"), this, &ImagesManagerDialog::createEditIcon);
    gamesMenu_->addSeparator();
    auto opt = [this](const QString& text) {
        QAction* a = gamesMenu_->addAction(text);
        a->setCheckable(true);
        return a;
    };
    searchClones_ = opt(tr("Search Clone Images"));
    scanMissingGames_ = opt(tr("Scan Missing Games"));
    scanBios_ = opt(tr("Scan Bios Sets"));
    scanDevices_ = opt(tr("Scan Device Sets"));
    scanArcade_ = opt(tr("Scan Arcade Games"));
    scanNonArcade_ = opt(tr("Scan Non-Arcade Machines"));
    scanSoftware_ = opt(tr("Scan Software List Games"));
    gamesMenu_->addSeparator();
    gamesMenu_->addAction(tr("Remove Selected from List"), this, &ImagesManagerDialog::removeSelectedGames);
    gamesMenu_->addAction(tr("Clear Games List"), this, [this] {
        games_->removeRows(0, games_->rowCount());
        mode_ = None;
        showPreview({});
        updateLabels();
    });
    gamesMenu_->addSeparator();
    QMenu* filter = gamesMenu_->addMenu(tr("Games Filter"));
    const char* filters[] = {QT_TR_NOOP("Show All Games"), QT_TR_NOOP("Show Parent Games Only"), QT_TR_NOOP("Show Clone Games Only")};
    for (int i = 0; i < 3; ++i) {
        QAction* a = filter->addAction(tr(filters[i]), this, [this, i] { filterGames(i); });
        a->setCheckable(true);
        a->setData(i);
    }
    connect(filter, &QMenu::aboutToShow, filter, [this, filter] {
        for (QAction* a : filter->actions()) a->setChecked(a->data().toInt() == filter_);
    });
    gamesMenu_->addSeparator();
    gamesMenu_->addAction(tr("Save Games List To File"), this, &ImagesManagerDialog::saveGamesList);
    gamesMenu_->addAction(tr("Restore Columns Sizes"), this, &ImagesManagerDialog::restoreColumns);
    gamesMenu_->addSeparator();
    zipSelected_ = gamesMenu_->addAction(tr("Zip Selected Games' Images"), this, [this] { zipImages(ZipSelected); });
    connect(gl, &QWidget::customContextMenuRequested, this, [this, gl](const QPoint& p) {
        zipSelected_->setVisible(mode_ == Valid);
        createIcon_->setVisible(category_ == ImageFinder::Icons);
        createIcon_->setEnabled(gl->selectionModel()->selectedRows().size() == 1 && !s_.finder->folders(ImageFinder::Icons).isEmpty());
        zipSelected_->setEnabled(!gl->selectionModel()->selectedRows().isEmpty());
        gamesMenu_->exec(gl->viewport()->mapToGlobal(p));
    });

    // The files list (NotUsedImagesList): grouped by folder
    files_ = new QStandardItemModel(0, 3, this);
    files_->setHorizontalHeaderLabels({tr("File"), tr("Size / Date"), tr("Problem")});
    QTreeView* fl = ui_->NotUsedImagesList;
    fl->setModel(files_);
    fl->setUniformRowHeights(true);
    fl->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fl->setSelectionBehavior(QAbstractItemView::SelectRows);
    fl->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fl->setIconSize(QSize(32, 24));
    fl->setColumnWidth(0, 170);
    fl->setColumnWidth(1, 170);
    fl->setContextMenuPolicy(Qt::CustomContextMenu);
    fl->installEventFilter(this);
    connect(fl->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &ImagesManagerDialog::filesSelected);
    connect(fl, &QTreeView::doubleClicked, this, &ImagesManagerDialog::openFile);
    filesMenu_ = new QMenu(this);
    filesMenu_->addAction(tr("Select All"), this, [this] {
        ui_->NotUsedImagesList->expandAll();
        ui_->NotUsedImagesList->selectAll(); // the folder rows can't be selected: only the files are
    });
    deleteSelected_ = filesMenu_->addAction(tr("Delete Selected Images"), this, [this] { deleteInvalid(true, true); });
    filesMenu_->addSeparator();
    filesMenu_->addAction(tr("View with Associated App"), this, &ImagesManagerDialog::openFile);
    filesMenu_->addAction(tr("Rename File"), this, &ImagesManagerDialog::renameFile);
    filesMenu_->addSeparator();
    filesMenu_->addAction(tr("Update Selected Image Info"), this, [this] { refreshFileInfo(false); });
    filesMenu_->addAction(tr("Update All Images Info"), this, [this] { refreshFileInfo(true); });
    filesMenu_->addSeparator();
    filesMenu_->addAction(tr("Clear Files List"), this, [this] {
        files_->removeRows(0, files_->rowCount());
        showPreview({});
        updateLabels();
    });
    filesMenu_->addAction(tr("Remove Selected from List"), this, [this] { removeSelectedFiles(false); });
    filesMenu_->addAction(tr("Remove Renamed Images"), this, [this] { removeSelectedFiles(true); });
    filesMenu_->addAction(tr("Move Selected to Folder"), this, &ImagesManagerDialog::moveFiles);
    filesMenu_->addSeparator();
    filesMenu_->addAction(tr("Save List To File"), this, &ImagesManagerDialog::saveFilesList);
    filesMenu_->addSeparator();
    sortByName_ = filesMenu_->addAction(tr("Sort List by File Names"), this, [this] { byDate_ = false; sortFiles(); });
    sortByDate_ = filesMenu_->addAction(tr("Sort List by Newest Date/Time"), this, [this] { byDate_ = true; sortFiles(); });
    for (QAction* a : {sortByName_, sortByDate_}) a->setCheckable(true);
    // the MAMu_ icons' excluded list (Icons category)
    iconsSeparator_ = filesMenu_->addSeparator();
    excludeAdd_ = filesMenu_->addAction(tr("Add Selected To Excluded List"), this, &ImagesManagerDialog::excludeSelected);
    excludeEdit_ = filesMenu_->addAction(tr("View/Edit Excluded List"), this, &ImagesManagerDialog::editExcluded);
    connect(fl, &QWidget::customContextMenuRequested, this, [this, fl](const QPoint& p) {
        deleteSelected_->setEnabled(!selectedFiles().isEmpty());
        sortByName_->setChecked(!byDate_);
        sortByDate_->setChecked(byDate_);
        const bool icons = category_ == ImageFinder::Icons;
        for (QAction* a : {iconsSeparator_, excludeAdd_, excludeEdit_}) a->setVisible(icons);
        excludeAdd_->setEnabled(!selectedFiles().isEmpty());
        filesMenu_->exec(fl->viewport()->mapToGlobal(p));
    });

    // The preview (click: open it)
    ui_->ImagePreview->setAlignment(Qt::AlignCenter);
    ui_->ImagePreview->setAutoFillBackground(true);
    {
        QPalette p = ui_->ImagePreview->palette();
        p.setColor(QPalette::Window, Qt::black);
        ui_->ImagePreview->setPalette(p);
    }
    ui_->ImagePreview->setCursor(Qt::PointingHandCursor);
    ui_->ImagePreview->setToolTip(tr("Click to open the image with its associated application"));
    ui_->ImagePreview->installEventFilter(this);

    connect(ui_->SplitterList, &ElSplitter::moved, this, [this] { updateLabels(); }); // SplitterListMoved
    layoutBars();
    readSettings();
    setCategory(s_.category);
    updateLabels();
}

ImagesManagerDialog::~ImagesManagerDialog() = default;

// The tool bar and the bottom bar in layouts: their buttons as wide as their texts and the bars as
// high as they need (the lists, the picture and the splitters between them are placed as before).
// The bottom bar's halves are under the two lists.
void ImagesManagerDialog::layoutBars() {
    using vcl::boxed, vcl::loose;
    QWidget* top = ui_->PanelToolBarButtons;
    auto* t = boxed(new QHBoxLayout(top), 2, 2, 4, 2, 4);
    t->addWidget(ui_->ImageCategoryIcon);
    loose(ui_->ImageCategorySelectLabel);
    t->addWidget(ui_->ImageCategorySelectLabel);
    t->addSpacing(4);
    for (QPushButton* b : {(QPushButton*)scanCategory_, (QPushButton*)ui_->ButtonScanMissing, (QPushButton*)ui_->ButtonScanNotUsedImages,
                           (QPushButton*)ui_->ButtonScanInvalidImages, (QPushButton*)scanAll_, (QPushButton*)scanOptions_,
                           (QPushButton*)zipButton_, (QPushButton*)unzipButton_, (QPushButton*)ui_->ButtonHelp})
        t->addWidget(vcl::textButton(b));
    t->addStretch();
    top->setFixedHeight(std::max(31, t->sizeHint().height()));

    QWidget* bottom = ui_->BottomBar;
    auto* b = boxed(new QHBoxLayout(bottom), 0, 1, 6, 1, 0);
    bottomLeft_ = new QWidget(bottom);
    auto* l = boxed(new QHBoxLayout(bottomLeft_), 4, 0, 6, 0, 12);
    for (QLabel* x : {(QLabel*)ui_->LabelTotalItemsMissing, (QLabel*)ui_->LabelTotalItemsNotUsed}) loose(x);
    l->addWidget(ui_->LabelTotalItemsMissing);
    l->addWidget(vcl::textButton(ui_->ButtonNotUsedImagesDeleteFiles));
    l->addStretch();
    b->addWidget(bottomLeft_);
    b->addSpacing(4);
    b->addWidget(ui_->LabelTotalItemsNotUsed);
    b->addStretch();
    b->addWidget(vcl::textButton(applyZips_));
    b->addSpacing(6);
    b->addWidget(vcl::textButton(ui_->ButtonInvalidImagesDeleteFiles));
    bottom->setFixedHeight(std::max(24, b->sizeHint().height()));
    for (QWidget* x : {top, bottom}) x->setProperty("vclLaidOut", true);
}

void ImagesManagerDialog::readSettings() {
    WinIni ini;
    ini.load(s_.extrasIni);
    resize(ini.readInteger(kSection, "ScreenWidth", 1200), ini.readInteger(kSection, "ScreenHeight", 600));
    int w = ini.readInteger(kSection, "MainSplitterPos", 594);
    ui_->PanelNotUsed->resize(w, ui_->PanelNotUsed->height());
    ui_->PanelNotUsed->setProperty("vclWidth", w);
    int h = ini.readInteger(kSection, "ImageSplitterPos", 200);
    ui_->ImagePreview->resize(ui_->ImagePreview->width(), h);
    ui_->ImagePreview->setProperty("vclHeight", h);
    searchClones_->setChecked(ini.readBool(kSection, "SearchCloneImages", false));
    byDate_ = ini.readBool(kSection, "SortFilesByDateTime", false);
    scanMissingGames_->setChecked(ini.readBool(kSection, "ScanMissingGames", false));
    scanBios_->setChecked(ini.readBool(kSection, "ScanBiosSets", false));
    scanDevices_->setChecked(ini.readBool(kSection, "ScanDeviceSets", false));
    scanArcade_->setChecked(ini.readBool(kSection, "ScanArcadeMachines", true));
    scanNonArcade_->setChecked(ini.readBool(kSection, "ScanNonArcadeMachines", true));
    scanSoftware_->setChecked(ini.readBool(kSection, "ScanSoftwareListGames", false));
    for (int c = 0; c < 4; ++c)
        ui_->MissingImagesList->setColumnWidth(c, ini.readInteger(kSection, QString("MissingListColWidth_%1").arg(c), kGameColumns[c]));
    vcl::relayout(this);
    if (ini.readString(kSection, "WindowState", "Normal") == "Maximized") setWindowState(Qt::WindowMaximized);
}

void ImagesManagerDialog::writeSettings() {
    WinIni ini;
    ini.load(s_.extrasIni);
    ini.eraseSection(kSection);
    ini.writeString(kSection, "WindowState", isMaximized() ? "Maximized" : "Normal");
    if (!isMaximized()) {
        ini.writeInteger(kSection, "ScreenWidth", width());
        ini.writeInteger(kSection, "ScreenHeight", height());
    }
    ini.writeInteger(kSection, "MainSplitterPos", ui_->PanelNotUsed->width());
    ini.writeInteger(kSection, "ImageSplitterPos", ui_->ImagePreview->height());
    ini.writeBool(kSection, "SearchCloneImages", searchClones_->isChecked());
    ini.writeBool(kSection, "SortFilesByDateTime", byDate_);
    ini.writeBool(kSection, "ScanMissingGames", scanMissingGames_->isChecked());
    ini.writeBool(kSection, "ScanBiosSets", scanBios_->isChecked());
    ini.writeBool(kSection, "ScanDeviceSets", scanDevices_->isChecked());
    ini.writeBool(kSection, "ScanArcadeMachines", scanArcade_->isChecked());
    ini.writeBool(kSection, "ScanNonArcadeMachines", scanNonArcade_->isChecked());
    ini.writeBool(kSection, "ScanSoftwareListGames", scanSoftware_->isChecked());
    for (int c = 0; c < 4; ++c)
        ini.writeInteger(kSection, QString("MissingListColWidth_%1").arg(c), ui_->MissingImagesList->columnWidth(c));
    ini.save(s_.extrasIni);
}

void ImagesManagerDialog::done(int r) {
    if (const int n = pendingZipChanges(); n && !quiet_) {
        auto a = QMessageBox::question(this, windowTitle(),
                                       tr("Changes to the zip files not written yet: %1.\nWrite them now?\n\n"
                                          "No: the zip files stay as they are.").arg(n),
                                       QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
        if (a == QMessageBox::Cancel) return;
        if (a == QMessageBox::Yes && !applyZipChanges()) return;
    }
    edits_.clear();
    writeSettings();
    QDialog::done(r);
}

void ImagesManagerDialog::restoreColumns() {
    for (int c = 0; c < kGameColumnCount; ++c) ui_->MissingImagesList->setColumnWidth(c, kGameColumns[c]);
}

void ImagesManagerDialog::setCategory(int c) {
    category_ = std::clamp(c, 0, ImageFinder::CountWithIcons - 1);
    ui_->ImageCategoryIcon->setPixmap(s_.categoryIcon ? s_.categoryIcon(category_).pixmap(24, 24) : QPixmap());
    QLabel* l = ui_->ImageCategorySelectLabel;
    l->setText(ImageFinder::title(category_));
    // Images or icons in the window's words (the MAMu_ Icons Manager)
    {
        const bool icons = category_ == ImageFinder::Icons;
        setWindowTitle(icons ? tr("MAMu_ Icons Manager") : tr("MAME Images Manager"));
        auto words = [](QPushButton* b, const QString& t) {
            b->setText(t);
            vcl::textButton(b); // (as wide as its text)
        };
        words(ui_->ButtonScanMissing, icons ? tr("Scan Missing Icons") : tr("Scan Missing Images"));
        words(ui_->ButtonScanNotUsedImages, icons ? tr("Icons of Games You Don't Have") : tr("Images of Games You Don't Have"));
        words(ui_->ButtonScanInvalidImages, icons ? tr("Scan Invalid Icons") : tr("Scan Invalid Images"));
        words(zipButton_, (icons ? tr("Zip Icons") : tr("Zip Images")) + QStringLiteral(" \u25BE"));
        words(unzipButton_, icons ? tr("Unzip Icons") : tr("Unzip Images"));
        words(ui_->ButtonNotUsedImagesDeleteFiles, icons ? tr("Delete Not Used Icons") : tr("Delete Not Used Images"));
        words(ui_->ButtonInvalidImagesDeleteFiles, icons ? tr("Delete Invalid Icons") : tr("Delete Invalid Images"));
        deleteSelected_->setText(icons ? tr("Delete Selected Icons") : tr("Delete Selected Images"));
    }
    if (QLayout* bar = ui_->PanelToolBarButtons->layout()) { // the tool bar's buttons all shown
        bar->activate();
        setMinimumWidth(std::max(931, bar->minimumSize().width()));
    }
    // A list made for another category doesn't count any more.
    invalidScanned_ = false;
    games_->removeRows(0, games_->rowCount());
    files_->removeRows(0, files_->rowCount());
    mode_ = None;
    showPreview({});
    updateLabels();
}

void ImagesManagerDialog::selectCategory() {
    QMenu m(this);
    for (int c = 0; c < ImageFinder::CountWithIcons; ++c) {
        if (c == ImageFinder::Icons) m.addSeparator();
        QAction* a = m.addAction(s_.categoryIcon ? s_.categoryIcon(c) : QIcon(), ImageFinder::title(c));
        a->setCheckable(true);
        a->setChecked(c == category_);
        a->setData(c);
    }
    QLabel* l = ui_->ImageCategorySelectLabel;
    if (QAction* a = m.exec(l->mapToGlobal(QPoint(0, l->height())))) switchCategory(a->data().toInt());
}

// The lists shown follow the new category (from what Scan All Categories read, when it did)
void ImagesManagerDialog::switchCategory(int c) {
    if (c == category_) return;
    const Mode was = mode_;
    const bool invalidShown = files_->rowCount() > 0 || invalidScanned_;
    setCategory(c);
    quiet_ = true;
    if (was == Valid) scanValid(false);
    else if (was != None) scanMissing(was == NotUsed, false);
    if (invalidShown) scanInvalid(false);
    quiet_ = false;
}

void ImagesManagerDialog::updateLabels() {
    int games = 0;
    for (int r = 0; r < games_->rowCount(); ++r) games += !ui_->MissingImagesList->isRowHidden(r, QModelIndex());
    int files = 0;
    for (int r = 0; r < files_->rowCount(); ++r) files += files_->item(r, 0)->rowCount();
    auto set = [](QLabel* l, const QString& t) { l->setText(t); };
    const bool icons = category_ == ImageFinder::Icons;
    const QString valid = QLocale().toString(validImages_), listed = QLocale().toString(games);
    set(ui_->LabelTotalItemsMissing, mode_ == NotUsed ? (icons ? tr("Icons Found: %1") : tr("Images Found: %1")).arg(games)
                                     : mode_ == Valid ? (icons ? tr("Valid Icons: %1 (%2 games)") : tr("Valid Images: %1 (%2 games)")).arg(valid, listed)
                                                      : (icons ? tr("Missing Icons: %1") : tr("Missing Images: %1")).arg(games));
    set(ui_->LabelTotalItemsNotUsed, (icons ? tr("Invalid Icons: %1") : tr("Invalid Images: %1")).arg(files));
    ui_->ButtonNotUsedImagesDeleteFiles->setVisible(mode_ == NotUsed && games > 0);
    // the bottom bar's two halves under the two lists (SplitterListMoved)
    if (bottomLeft_) bottomLeft_->setFixedWidth(ui_->PanelNotUsed->x());
    const int pending = pendingZipChanges();
    applyZips_->setVisible(pending > 0);
    if (pending) {
        applyZips_->setText(tr("Apply Zip Changes (%1)").arg(pending));
        vcl::textButton(applyZips_);
    }
}

int ImagesManagerDialog::pendingZipChanges() const {
    int n = 0;
    for (const ZipEditor& e : edits_) n += e.changes();
    return n;
}

// The category's pictures, built again when asked (fresh) or when files changed since; with
// content: each one checked to be a PNG or a JPEG. Shown in `win` as it goes (Cancel stops it).
bool ImagesManagerDialog::indexOf(int c, bool fresh, bool content, ProgressWindow* win, int barValue) {
    ImageIndex& idx = index_[c];
    if (!fresh && idx.isBuilt() && (!content || checked_.contains(c))) return true;
    std::function<bool(int, int)> progress;
    if (win)
        progress = [win, barValue](int done, int) {
            win->setProgress(barValue, tr("%1 files").arg(QLocale().toString(done)));
            return !win->canceled();
        };
    const bool ok = idx.build(*s_.finder, c, content, progress, &edits_);
    if (!ok) {
        index_.remove(c);
        checked_.remove(c);
        return false;
    }
    if (content) checked_.insert(c);
    else checked_.remove(c);
    return true;
}

// Files deleted, renamed or moved: the lists of pictures are made again when needed
void ImagesManagerDialog::zipsChanged() {
    index_.clear();
    checked_.clear();
    changed_ = true;
    updateLabels();
}

// The scan options of the right-click menu (AddItem)
bool ImagesManagerDialog::include(const MameGame& g, bool forNotUsed, int category) const {
    if (!g.cloneOf.isEmpty() && !searchClones_->isChecked()) return false;
    if (g.isSoftware()) { // Scan Software List Games (they have no icons of their own: the list's)
        if (!scanSoftware_->isChecked() || (category < 0 ? category_ : category) == ImageFinder::Icons) return false;
        return forNotUsed || scanMissingGames_->isChecked() || g.have();
    }
    if (g.systemType == 0 && !scanArcade_->isChecked()) return false;
    if (g.systemType == 1 && !scanNonArcade_->isChecked()) return false;
    if (!scanDevices_->isChecked() && (g.kind == SetKind::Device || g.size == 0)) return false;
    if (!forNotUsed && !scanMissingGames_->isChecked() && !g.have()) return false;
    if (!scanBios_->isChecked() && g.kind == SetKind::Bios) return false;
    return true;
}

// LoadGamesToMissingList / ScanNotUsedImagesToMissingList
void ImagesManagerDialog::scanMissing(bool notUsed, bool fresh) {
    if (!s_.finder) return;
    games_->removeRows(0, games_->rowCount());
    showPreview({});
    mode_ = notUsed ? NotUsed : Missing;
    filter_ = 0;
    showGameColumns(false);
    ProgressWindow win(windowTitle(), this);
    win.setCancelable(true);
    win.step(tr("Reading images: %1").arg(ImageFinder::title(category_)));
    if (!indexOf(category_, fresh, false, &win)) {
        mode_ = None;
        updateLabels();
        return;
    }
    const ImageIndex& idx = index_[category_];
    win.step(notUsed ? tr("Scanning for images of missing games") : tr("Scanning for games with missing images"),
             int(s_.shown.size()));
    int done = 0;
    const char* status[] = {QT_TR_NOOP("Good"), QT_TR_NOOP("Imperfect"), QT_TR_NOOP("Preliminary")};
    for (const MameGame& g : s_.shown) {
        if (++done % 300 == 0) {
            win.setProgress(done);
            if (win.canceled()) break;
        }
        if (notUsed && g.setStatus != 2) continue; // only the games you don't have at all
        if (!include(g, notUsed)) continue;
        QString file, entry;
        if (notUsed) {
            const QList<int> pics = idx.ofGame(g.imageName());
            if (pics.isEmpty()) continue;
            int pick = pics.first();
            for (int i : pics)
                if (idx.entries()[i].first) pick = i;
            file = idx.entries()[pick].file;
            entry = idx.entries()[pick].stored;
        } else if (idx.hasFirst(g.imageName())) {
            continue;
        }
        auto* t = new QStandardItem(s_.gameIcon ? s_.gameIcon(g) : QIcon(), g.title);
        t->setData(g.imageName(), NameRole);
        t->setData(g.imageParent(), CloneRole);
        t->setData(file, FileRole);
        t->setData(entry, EntryRole);
        t->setData(g.title.toLower(), SortRole);
        if (g.setStatus == 1) t->setForeground(QColor(0xC0, 0, 0));
        QString drv = QFileInfo(g.driver).fileName();
        QList<QStandardItem*> row{t, new QStandardItem(g.imageName()), new QStandardItem(g.imageParent()), new QStandardItem(drv),
                                  new QStandardItem(g.driverStatus >= 0 && g.driverStatus <= 2 ? tr(status[g.driverStatus]) : QString())};
        for (int c = 1; c < row.size(); ++c) row[c]->setData(row[c]->text().toLower(), SortRole);
        games_->appendRow(row);
    }
    win.close();
    QTreeView* gl = ui_->MissingImagesList;
    gl->sortByColumn(gl->header()->sortIndicatorSection(), gl->header()->sortIndicatorOrder());
    // a software list game's name (megadriv/sonic) shown whole: the Name column widened to fit
    if (const int w = static_cast<QAbstractItemView*>(gl)->sizeHintForColumn(1) + 8; w > gl->columnWidth(1)) gl->setColumnWidth(1, w);
    updateLabels();
    if (!games_->rowCount()) {
        updateLabels();
        if (!quiet_)
            QMessageBox::information(this, windowTitle(),
                                     notUsed ? tr("Scan complete, but no images were found for missing games. More scan options are "
                                                  "in the right-click menu.")
                                             : tr("Scan complete, but it seems that all games have images. More scan options are in "
                                                  "the right-click menu."));
        return;
    }
    gl->setCurrentIndex(games_->index(0, 0));
    gl->setFocus();
}

// The names a file of the category may have: the games' (and, for the MAMu_ icons, the drivers',
// deviceset.ico and the excluded list's: not listed as invalid)
QSet<QString> ImagesManagerDialog::usedNames(int c) const {
    QSet<QString> names;
    for (const MameGame& g : s_.all) names.insert(g.imageName().toLower()); // (software list games: <list>/<name>)
    if (c == ImageFinder::Icons) {
        for (const MameGame& g : s_.all)
            if (!g.driver.isEmpty()) names.insert(QFileInfo(g.driver).completeBaseName().toLower());
        names.insert(QStringLiteral("deviceset"));
        for (const QString& f : excluded()) names.insert(QFileInfo(f).completeBaseName().toLower());
    }
    return names;
}

// arcade\el_mamu_exclude.ini: icon file names, one per line
QStringList ImagesManagerDialog::excluded() const {
    QStringList out;
    QFile f(s_.excludeFile);
    if (f.open(QFile::ReadOnly))
        for (const QByteArray& l : f.readAll().split('\n'))
            if (QString n = QString::fromUtf8(l).trimmed(); !n.isEmpty()) out << n;
    return out;
}

void ImagesManagerDialog::setExcluded(const QStringList& files) {
    QDir().mkpath(QFileInfo(s_.excludeFile).absolutePath());
    writeTextFile(s_.excludeFile, files.join("\r\n") + (files.isEmpty() ? "" : "\r\n"), false);
}

// Add Selected To Excluded List (PopupAddSelectedToExcludeFilesListClick)
void ImagesManagerDialog::excludeSelected() {
    QStringList list = excluded();
    for (QStandardItem* it : selectedFiles()) {
        const QString name = QFileInfo(it->text()).fileName();
        if (!list.contains(name, Qt::CaseInsensitive)) list << name;
    }
    setExcluded(list);
    removeSelectedFiles(false);
}

// View/Edit Excluded List (TFormArcadeMAMu_ExcludedList)
void ImagesManagerDialog::editExcluded() {
    QDialog d(this);
    d.setWindowTitle(tr("Excluded List for Not Used MAMu_ Icons"));
    auto* lay = new QVBoxLayout(&d);
    auto* note = new QLabel(tr("Icon files listed here are never listed as invalid icons. One file name per line."), &d);
    note->setWordWrap(true);
    lay->addWidget(note);
    auto* edit = new QPlainTextEdit(excluded().join('\n'), &d);
    edit->setMinimumSize(420, 320);
    lay->addWidget(edit);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* ok = new QPushButton(tr("OK"), &d);
    auto* cancel = new QPushButton(tr("Cancel"), &d);
    connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    lay->addLayout(buttons);
    if (d.exec() != QDialog::Accepted) return;
    QStringList list;
    for (const QString& l : edit->toPlainText().split('\n'))
        if (!l.trimmed().isEmpty()) list << l.trimmed();
    setExcluded(list);
}

// Create / Edit (CreateEditIcon): a game without an icon gets a copy of resources\blank_icon.ico,
// opened with the program the system edits icons with
void ImagesManagerDialog::createEditIcon() {
    const auto rows = ui_->MissingImagesList->selectionModel()->selectedRows();
    const QStringList folders = s_.finder->folders(ImageFinder::Icons);
    if (rows.size() != 1 || folders.isEmpty()) return;
    const QString name = games_->item(rows.first().row(), 0)->data(NameRole).toString();
    const QString file = QDir(folders.first()).filePath(name + ".ico");
    if (!QFileInfo::exists(file)) {
        QDir().mkpath(folders.first());
        if (!QFile::copy(s_.blankIcon, file)) {
            QMessageBox::warning(this, tr("Create / Edit Icon"),
                                 tr("%1 could not be created from %2.").arg(QDir::toNativeSeparators(file), QDir::toNativeSeparators(s_.blankIcon)));
            return;
        }
        QFile(file).setPermissions(QFile(file).permissions() | QFileDevice::WriteOwner);
        zipsChanged();
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(file));
}

// Sort List by Filenames / by Newest Date/Time (NotUsedIconsSortByDateTime)
void ImagesManagerDialog::sortFiles() {
    files_->setSortRole(byDate_ ? DateRole : SortRole);
    for (int g = 0; g < files_->rowCount(); ++g)
        files_->item(g, 0)->sortChildren(0, byDate_ ? Qt::DescendingOrder : Qt::AscendingOrder);
}

void ImagesManagerDialog::showGameColumns(bool images) {
    ui_->MissingImagesList->setColumnHidden(5, !images);
    ui_->MissingImagesList->setColumnHidden(6, !images);
}

void ImagesManagerDialog::setSearchClones(bool on) { searchClones_->setChecked(on); }
void ImagesManagerDialog::setScanSoftware(bool on) { scanSoftware_->setChecked(on); }

// Scan Category (added): the games with a valid image (named after the game, a PNG or JPEG
// inside), with the scan options; a clone without one of its own shows its parent's (Search
// Clone Images)
void ImagesManagerDialog::scanValid(bool fresh) {
    if (!s_.finder) return;
    games_->removeRows(0, games_->rowCount());
    showPreview({});
    mode_ = Valid;
    filter_ = 0;
    validImages_ = 0;
    showGameColumns(true);
    ProgressWindow win(windowTitle(), this);
    win.setCancelable(true);
    win.step(tr("Reading images: %1").arg(ImageFinder::title(category_)));
    if (!indexOf(category_, fresh, true, &win)) {
        mode_ = None;
        updateLabels();
        return;
    }
    const ImageIndex& idx = index_[category_];
    auto validOf = [&idx](const QString& name) {
        QList<int> out;
        for (int i : idx.ofGame(name))
            if (idx.entries()[i].picture) out << i;
        return out;
    };
    const char* status[] = {QT_TR_NOOP("Good"), QT_TR_NOOP("Imperfect"), QT_TR_NOOP("Preliminary")};
    for (const MameGame& g : s_.shown) {
        if (!include(g, false)) continue;
        QList<int> pics = validOf(g.imageName());
        bool parents = false;
        if (pics.isEmpty() && !g.cloneOf.isEmpty()) { // the parent's image, as the main window shows
            pics = validOf(g.imageParent());
            parents = true;
        }
        if (pics.isEmpty()) continue;
        int pick = pics.first();
        for (int i : pics)
            if (idx.entries()[i].first) pick = i;
        const ImageEntry& e = idx.entries()[pick];
        auto* t = new QStandardItem(s_.gameIcon ? s_.gameIcon(g) : QIcon(), g.title);
        t->setData(g.imageName(), NameRole);
        t->setData(g.imageParent(), CloneRole);
        t->setData(e.file, FileRole);
        t->setData(e.stored, EntryRole);
        t->setData(g.title.toLower(), SortRole);
        const QString where = parents ? tr("Parent's image (%1)").arg(g.imageParent())
                                      : QDir::toNativeSeparators(e.zipped() ? e.file : QFileInfo(e.file).absolutePath());
        QList<QStandardItem*> row{t, new QStandardItem(g.imageName()), new QStandardItem(g.imageParent()), new QStandardItem(QFileInfo(g.driver).fileName()),
                                  new QStandardItem(g.driverStatus >= 0 && g.driverStatus <= 2 ? tr(status[g.driverStatus]) : QString()),
                                  new QStandardItem(parents ? QString() : QLocale().toString(pics.size())), new QStandardItem(where)};
        for (int c = 1; c < row.size(); ++c) row[c]->setData(row[c]->text().toLower(), SortRole);
        row[5]->setData(parents ? 0 : pics.size(), SortRole);
        row[5]->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row[6]->setToolTip(where);
        if (parents) row[6]->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        else validImages_ += pics.size();
        games_->appendRow(row);
    }
    win.close();
    QTreeView* gl = ui_->MissingImagesList;
    gl->sortByColumn(gl->header()->sortIndicatorSection(), gl->header()->sortIndicatorOrder());
    // a software list game's name (megadriv/sonic) shown whole: the Name column widened to fit
    if (const int w = static_cast<QAbstractItemView*>(gl)->sizeHintForColumn(1) + 8; w > gl->columnWidth(1)) gl->setColumnWidth(1, w);
    updateLabels();
    if (!games_->rowCount()) {
        if (!quiet_)
            QMessageBox::information(this, windowTitle(), tr("Scan complete, but no game has an image in this category. More scan options "
                                                             "are in Scan Options."));
        return;
    }
    gl->setCurrentIndex(games_->index(0, 0));
    gl->setFocus();
}

// ScanFiles: the pictures that are no game's (and, added, the files that aren't pictures)
void ImagesManagerDialog::scanInvalid(bool fresh) {
    if (!s_.finder) return;
    files_->removeRows(0, files_->rowCount());
    showPreview({});
    const QSet<QString> names = usedNames(category_);
    {
        ProgressWindow win(windowTitle(), this);
        win.setCancelable(true);
        win.step(tr("Reading images: %1").arg(ImageFinder::title(category_)));
        if (!indexOf(category_, fresh, true, &win)) return;
    }
    const ImageIndex& idx = index_[category_];
    QList<int> bad = idx.unused(names);
    { // folders first, then zip files, each in name order
        QHash<int, QString> key;
        for (int i : bad) {
            const ImageEntry& e = idx.entries()[i];
            key.insert(i, (e.zipped() ? QStringLiteral("1") + e.file : QStringLiteral("0") + QFileInfo(e.file).absolutePath()) +
                              QChar(1) + e.name().toLower());
        }
        std::sort(bad.begin(), bad.end(), [&key](int a, int b) { return key.value(a) < key.value(b); });
    }
    invalidScanned_ = true;
    for (int i : bad) addFile(idx.entries()[i]);
    sortFiles();
    ui_->NotUsedImagesList->expandAll();
    ui_->NotUsedImagesList->resizeColumnToContents(1);
    updateLabels();
    if (bad.isEmpty()) {
        QStringList where = idx.folders() + idx.zips();
        if (!quiet_)
            QMessageBox::information(this, windowTitle(),
                                     tr("Scanning complete, but nothing was found in:\n%1").arg(QDir::toNativeSeparators(where.join('\n'))));
    } else {
        ui_->NotUsedImagesList->setCurrentIndex(files_->index(0, 0, files_->index(0, 0)));
        ui_->NotUsedImagesList->setFocus();
    }
}

// One file under its folder's (or zip file's) group (AddToGroup)
void ImagesManagerDialog::addFile(const ImageEntry& e) {
    const QString groupText = e.zipped() ? QDir::toNativeSeparators(e.file)
                                         : QDir::toNativeSeparators(QFileInfo(e.file).absolutePath() + "/");
    QStandardItem* group = nullptr;
    for (int r = 0; r < files_->rowCount(); ++r)
        if (files_->item(r, 0)->text() == groupText) group = files_->item(r, 0);
    if (!group) {
        group = new QStandardItem(s_.categoryIcon ? s_.categoryIcon(category_) : QIcon(), groupText);
        QFont f = group->font();
        f.setBold(true);
        group->setFont(f);
        group->setSelectable(false);
        files_->appendRow({group, new QStandardItem(), new QStandardItem()});
        ui_->NotUsedImagesList->setFirstColumnSpanned(group->row(), QModelIndex(), true); // the whole folder shows
    }
    auto* it = new QStandardItem(e.name());
    it->setData(e.file, FileRole);
    it->setData(e.zipped() ? e.entry : QString(), EntryRole);
    it->setData(e.stored, StoredRole);
    it->setData(e.name(), OriginalRole);
    it->setToolTip(joined(e.file, e.entry));
    it->setData(e.name().toLower(), SortRole);
    it->setData(e.modified.toSecsSinceEpoch(), DateRole);
    auto* info = new QStandardItem(sizeText(e.size) + "  " + QLocale().toString(e.modified, QLocale::ShortFormat));
    auto* why = new QStandardItem(!e.picture ? (!e.size ? tr("Empty file")
                                                        : category_ == ImageFinder::Icons ? tr("Not an icon file") : tr("Not a PNG or JPEG picture"))
                                             : tr("Not used by any game"));
    if (!e.picture) why->setForeground(QColor(0xC0, 0, 0));
    group->appendRow({it, info, why});
}

// A zipped picture, unpacked for the program that opens it
QString ImagesManagerDialog::extractForView(const QString& zip, const QString& entry) {
    if (!viewDir_) viewDir_ = std::make_unique<QTemporaryDir>();
    if (!viewDir_->isValid()) return {};
    ZipArchive za;
    if (!za.open(zip)) return {};
    QByteArray data = za.read(entry);
    if (data.isEmpty()) return {};
    QString to = viewDir_->filePath(QString(entry).replace('/', ' '));
    QFile f(to);
    if (!f.open(QFile::WriteOnly) || f.write(data) != data.size()) return {};
    return to;
}

void ImagesManagerDialog::showPreview(const QString& file, const QString& entry) {
    previewFile_ = file;
    previewEntry_ = entry;
    QLabel* p = ui_->ImagePreview;
    QImage img;
    if (!file.isEmpty()) {
        QByteArray data;
        if (entry.isEmpty()) {
            QFile f(file);
            if (f.open(QFile::ReadOnly)) data = f.readAll();
        } else {
            ZipArchive za;
            if (za.open(file)) data = za.read(entry);
        }
        // the largest of an icon's pictures (16x16, 32x32, 48x48, 256x256...)
        QBuffer buf(&data);
        QImageReader r(&buf);
        r.setDecideFormatFromContent(true);
        do {
            QImage frame = r.read();
            if (frame.width() * frame.height() > img.width() * img.height()) img = frame;
        } while (r.jumpToNextImage());
    }
    if (img.isNull()) {
        p->clear();
        return;
    }
    QSize box = p->size();
    p->setPixmap(QPixmap::fromImage(img.width() > box.width() || img.height() > box.height()
                                        ? img.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation) : img));
}

// The picture of a missing game (Scan Images of Missing Games): in the preview (the original
// shows it in a floating window).
void ImagesManagerDialog::missingSelected() {
    QModelIndex i = ui_->MissingImagesList->currentIndex();
    if (!i.isValid() || (mode_ != NotUsed && mode_ != Valid)) return;
    QStandardItem* it = games_->item(i.row(), 0);
    showPreview(it->data(FileRole).toString(), it->data(EntryRole).toString());
}

// NotUsedImagesListItemSelectionChanged
void ImagesManagerDialog::filesSelected() {
    QModelIndex i = ui_->NotUsedImagesList->currentIndex();
    QStandardItem* it = i.isValid() ? files_->itemFromIndex(i.siblingAtColumn(0)) : nullptr;
    if (it && it->parent()) showPreview(it->data(FileRole).toString(), it->data(StoredRole).toString());
    else showPreview({});
}

QList<QStandardItem*> ImagesManagerDialog::selectedFiles() const {
    QList<QStandardItem*> out;
    for (const QModelIndex& i : ui_->NotUsedImagesList->selectionModel()->selectedRows())
        if (QStandardItem* it = files_->itemFromIndex(i); it && it->parent()) out << it;
    return out;
}

// PopupPlayGameClick
void ImagesManagerDialog::playSelected() {
    const auto rows = ui_->MissingImagesList->selectionModel()->selectedRows();
    if (rows.size() != 1 || !s_.play) return;
    QString name = games_->item(rows.first().row(), 0)->data(NameRole).toString();
    if (!s_.play(name))
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not find the game in the main games list. It must be there and visible for this to work."));
    ui_->MissingImagesList->setFocus();
}

void ImagesManagerDialog::removeSelectedGames() {
    QList<int> rows;
    for (const QModelIndex& i : ui_->MissingImagesList->selectionModel()->selectedRows()) rows << i.row();
    QList<QStandardItem*> items;
    for (int r : rows) items << games_->item(r, 0);
    removeItems(games_, items);
    if (!games_->rowCount()) showPreview({});
    updateLabels();
    ui_->MissingImagesList->setFocus();
}

// PopupShowAllGamesClick: all, parents only, clones only
void ImagesManagerDialog::filterGames(int which) {
    filter_ = which;
    for (int r = 0; r < games_->rowCount(); ++r) {
        bool clone = !games_->item(r, 0)->data(CloneRole).toString().isEmpty();
        ui_->MissingImagesList->setRowHidden(r, QModelIndex(), (which == 1 && clone) || (which == 2 && !clone));
    }
    updateLabels();
}

// PopupMissingSaveToFileClick
void ImagesManagerDialog::saveGamesList() {
    if (!games_->rowCount()) return;
    QString file = QFileDialog::getSaveFileName(this, tr("Save Missing Images List To a File"), {}, tr("Text Files (*.txt)"));
    if (file.isEmpty()) return;
    QString out = (mode_ == NotUsed ? QString("----------> Images of Games You Don't Have <----------\r\n\r\n")
                   : mode_ == Valid ? QString("----------> Valid Images <----------\r\n\r\n")
                                    : QString("----------> Missing Images <----------\r\n\r\n"));
    out += "  -> System: " + s_.system + "\r\n";
    out += "  -> Image Category: " + ImageFinder::title(category_) + "\r\n";
    out += QString("     Total Games: %1\r\n\r\n").arg(games_->rowCount());
    out += QString("%1 %2 %3\r\n").arg("[Name]", -16).arg("[Clone of]", -16).arg("[Title]");
    for (int r = 0; r < games_->rowCount(); ++r) {
        QStandardItem* it = games_->item(r, 0);
        out += QString("%1 %2 %3\r\n").arg(it->data(NameRole).toString(), -16).arg(it->data(CloneRole).toString(), -16).arg(it->text());
        if (mode_ == NotUsed || mode_ == Valid)
            out += "    " + joined(it->data(FileRole).toString(), it->data(EntryRole).toString()) + "\r\n";
    }
    if (writeTextFile(file, out, true))
        QMessageBox::information(this, windowTitle(), tr("The file \"%1\" was created from the list.").arg(QDir::toNativeSeparators(file)));
}

// ButtonNotUsedImagesDeleteFilesClick: the pictures of the missing games listed
void ImagesManagerDialog::deleteNotUsed(bool ask) {
    if (mode_ != NotUsed || !games_->rowCount()) return;
    if (!indexOf(category_, false, false)) return;
    const ImageIndex& idx = index_[category_];
    if (ask) {
        int zipped = 0;
        for (int r = 0; r < games_->rowCount(); ++r)
            if (!ui_->MissingImagesList->isRowHidden(r, QModelIndex()))
                for (int i : idx.ofGame(games_->item(r, 0)->data(NameRole).toString())) zipped += idx.entries()[i].zipped();
        if (QMessageBox::question(this, windowTitle(),
                                  deleteQuestion(tr("You are about to delete the images of all the games in the list."), zipped)) !=
            QMessageBox::Yes)
            return;
    }
    QStringList loose;
    QHash<QString, int> rowOf; // file -> the row of its game
    QList<int> rows;
    int queued = 0, inSevenZip = 0;
    for (int r = games_->rowCount() - 1; r >= 0; --r) {
        if (ui_->MissingImagesList->isRowHidden(r, QModelIndex())) continue;
        rows << r;
        for (int i : idx.ofGame(games_->item(r, 0)->data(NameRole).toString())) {
            const ImageEntry& e = idx.entries()[i];
            if (e.zipped() && ZipArchive::sevenZipName(e.file)) {
                ++inSevenZip; // (read only)
            } else if (e.zipped()) {
                ZipEditor& ed = edits_[e.file];
                if (ed.zip().isEmpty()) ed = ZipEditor(e.file);
                ed.remove(e.stored);
                ++queued;
            } else {
                loose << e.file;
                rowOf.insert(e.file, r);
            }
        }
    }
    const QStringList goneList = moveToRecycleBin(loose, this);
    const QSet<QString> gone(goneList.begin(), goneList.end());
    QSet<int> keep; // a game whose file stays stays in the list
    for (const QString& f : loose)
        if (!gone.contains(f)) keep.insert(rowOf.value(f));
    QList<QStandardItem*> out;
    for (int r : rows)
        if (!keep.contains(r)) out << games_->item(r, 0);
    removeItems(games_, out);
    if (!gone.isEmpty() || queued) zipsChanged();
    showPreview({});
    updateLabels();
    if (ask) {
        QString msg = tr("Files deleted: %1").arg(gone.size());
        if (queued) msg += "\n" + tr("Pictures to take out of zip files: %1 (Apply Zip Changes, at the bottom right)").arg(queued);
        QMessageBox::information(this, windowTitle(), msg);
    }
    if (ask) sevenZipReadOnly(inSevenZip);
}

// ButtonInvalidImagesDeleteFilesClick (all of them), or Delete Selected Images (added)
void ImagesManagerDialog::deleteInvalid(bool ask, bool selectedOnly) {
    QList<QStandardItem*> items;
    if (selectedOnly) {
        items = selectedFiles();
    } else {
        for (int g = 0; g < files_->rowCount(); ++g)
            for (int r = 0; r < files_->item(g, 0)->rowCount(); ++r) items << files_->item(g, 0)->child(r, 0);
    }
    if (items.isEmpty()) return;
    if (ask) {
        int zipped = 0;
        for (QStandardItem* it : items) zipped += !it->data(EntryRole).toString().isEmpty();
        const QString first = selectedOnly ? tr("You are about to delete the selected files (%1).").arg(items.size())
                                           : tr("You are about to delete all the files in the list (%1).").arg(items.size());
        if (QMessageBox::question(this, windowTitle(), deleteQuestion(first, zipped)) != QMessageBox::Yes) return;
    }
    QStringList loose;
    for (QStandardItem* it : items)
        if (it->data(EntryRole).toString().isEmpty()) loose << it->data(FileRole).toString();
    const QStringList goneList = moveToRecycleBin(loose, this);
    const QSet<QString> gone(goneList.begin(), goneList.end());
    int queued = 0, inSevenZip = 0;
    QList<QStandardItem*> out;
    for (QStandardItem* it : items) {
        const QString file = it->data(FileRole).toString();
        if (!it->data(EntryRole).toString().isEmpty() && ZipArchive::sevenZipName(file)) {
            ++inSevenZip; // (read only)
            continue;
        }
        if (!it->data(EntryRole).toString().isEmpty()) {
            ZipEditor& ed = edits_[file];
            if (ed.zip().isEmpty()) ed = ZipEditor(file);
            ed.remove(it->data(StoredRole).toString());
            ++queued;
        } else if (!gone.contains(file)) {
            continue;
        }
        out << it;
    }
    removeItems(files_, out);
    for (int g = files_->rowCount() - 1; g >= 0; --g)
        if (!files_->item(g, 0)->rowCount()) files_->removeRow(g);
    if (!gone.isEmpty() || queued) zipsChanged();
    showPreview({});
    updateLabels();
    if (ask) {
        QString msg = tr("Files deleted: %1").arg(gone.size());
        if (queued) msg += "\n" + tr("Pictures to take out of zip files: %1 (Apply Zip Changes, at the bottom right)").arg(queued);
        QMessageBox::information(this, windowTitle(), msg);
    }
    if (ask) sevenZipReadOnly(inSevenZip);
}

// PopupNotUsedRemoveSelectedClick / PopupNotUsedRemoveRenamedIconsClick
void ImagesManagerDialog::removeSelectedFiles(bool renamedOnly) {
    QList<QStandardItem*> items;
    if (renamedOnly) {
        for (int g = 0; g < files_->rowCount(); ++g)
            for (int r = 0; r < files_->item(g, 0)->rowCount(); ++r) {
                QStandardItem* it = files_->item(g, 0)->child(r, 0);
                if (it->text() != it->data(OriginalRole).toString()) items << it;
            }
    } else {
        items = selectedFiles();
    }
    removeItems(files_, items);
    for (int g = files_->rowCount() - 1; g >= 0; --g) // ELV_RemoveEmptyGroups
        if (!files_->item(g, 0)->rowCount()) files_->removeRow(g);
    showPreview({});
    updateLabels();
    ui_->NotUsedImagesList->setFocus();
}

// PopupNotUsedRenameFileClick + RenameFileButtonOkClick: the extension stays.
void ImagesManagerDialog::renameFile() {
    QList<QStandardItem*> sel = selectedFiles();
    if (sel.size() != 1) return;
    QStandardItem* it = sel.first();
    bool ok = false;
    const QString old = QFileInfo(it->text()).fileName();
    QString name = QInputDialog::getText(this, tr("Rename File"), tr("New name for %1:").arg(old), QLineEdit::Normal, old, &ok).trimmed();
    if (!ok || name.isEmpty() || name == ".") return;
    doRename(it, name);
}

bool ImagesManagerDialog::doRename(QStandardItem* it, QString name) {
    static const QRegularExpression bad(R"([/\\*?<>|:;"])");
    name.remove(bad);
    const QString entry = it->data(EntryRole).toString();
    const QString path = it->data(FileRole).toString();
    const QFileInfo fi(entry.isEmpty() ? path : entry);
    int dot = name.lastIndexOf('.');
    name = (dot > 0 ? name.left(dot) : name) + "." + fi.suffix();
    if (name == fi.fileName()) return false;
    if (!entry.isEmpty() && ZipArchive::sevenZipName(path)) {
        sevenZipReadOnly(1);
        return false;
    }
    if (!entry.isEmpty()) { // inside a zip: written with the other zip changes
        const int slash = entry.lastIndexOf('/');
        const QString to = (slash >= 0 ? entry.left(slash + 1) : QString()) + name;
        ZipArchive za;
        if (za.open(path) && za.contains(to) && to.compare(entry, Qt::CaseInsensitive) != 0) {
            QMessageBox::warning(this, tr("Rename File"), tr("%1 already has a file named %2.").arg(QDir::toNativeSeparators(path), to));
            return false;
        }
        ZipEditor& ed = edits_[path];
        if (ed.zip().isEmpty()) ed = ZipEditor(path);
        ed.rename(it->data(StoredRole).toString(), to);
        it->setText(to);
        it->setData(to, EntryRole);
        it->setToolTip(joined(path, to));
        it->setForeground(QColor(0, 0x80, 0));
        zipsChanged();
        return true;
    }
    QString to = fi.dir().filePath(name);
    if (QFileInfo::exists(to) && name.compare(fi.fileName(), Qt::CaseInsensitive) != 0) {
        if (QMessageBox::question(this, tr("Rename File"), tr("%1 already exists. Replace it?").arg(QDir::toNativeSeparators(to))) !=
            QMessageBox::Yes)
            return false;
        if (moveToRecycleBin({to}, this).isEmpty()) return false;
    }
    bool done = name.compare(fi.fileName(), Qt::CaseInsensitive) == 0
                    ? QFile::rename(path, path + ".tmp") && QFile::rename(path + ".tmp", to)
                    : QFile::rename(path, to);
    if (!done) {
        QMessageBox::warning(this, tr("Rename File"),
                             tr("The file could not be renamed. Make sure it isn't being used by another program."));
        return false;
    }
    it->setText(name);
    it->setData(to, FileRole);
    it->setToolTip(QDir::toNativeSeparators(to));
    it->setForeground(QColor(0, 0x80, 0)); // renamed: Remove Renamed Images takes these out of the list
    zipsChanged();
    return true;
}

// PopupMoveSelectedToFolderClick: a zipped picture is unpacked there and taken out of its zip
void ImagesManagerDialog::moveFiles() {
    QList<QStandardItem*> sel = selectedFiles();
    if (sel.isEmpty()) return;
    QString dir = QFileDialog::getExistingDirectory(this, tr("Move Selected Files To Folder"));
    if (dir.isEmpty()) return;
    int n = 0, inSevenZip = 0;
    QList<QStandardItem*> moved;
    std::unique_ptr<ProgressWindow> win;
    if (sel.size() > 30) {
        win = std::make_unique<ProgressWindow>(windowTitle(), this);
        win->setCancelable(true);
        win->step(tr("Moving files to %1").arg(QDir::toNativeSeparators(dir)), int(sel.size()));
    }
    for (QStandardItem* it : sel) {
        if (win && n % 10 == 0) {
            win->setProgress(n, tr("%1 of %2").arg(QLocale().toString(n), QLocale().toString(sel.size())));
            if (win->canceled()) break;
        }
        const QString from = it->data(FileRole).toString();
        const QString entry = it->data(EntryRole).toString();
        if (!entry.isEmpty() && ZipArchive::sevenZipName(from)) {
            ++inSevenZip; // (read only)
            continue;
        }
        if (!entry.isEmpty()) {
            ZipArchive za;
            QByteArray data;
            if (za.open(from)) data = za.read(it->data(StoredRole).toString());
            za.close();
            QFile out(QDir(dir).filePath(QFileInfo(entry).fileName()));
            if (data.isEmpty() || !out.open(QFile::WriteOnly) || out.write(data) != data.size()) continue;
            ZipEditor& ed = edits_[from];
            if (ed.zip().isEmpty()) ed = ZipEditor(from);
            ed.remove(it->data(StoredRole).toString());
        } else {
            QString to = QDir(dir).filePath(QFileInfo(from).fileName());
            if (QFileInfo::exists(to)) QFile::remove(to);
            if (!QFile::rename(from, to) && !(QFile::copy(from, to) && QFile::remove(from))) continue;
        }
        moved << it;
        ++n;
    }
    if (win) win->close();
    removeItems(files_, moved);
    for (int g = files_->rowCount() - 1; g >= 0; --g)
        if (!files_->item(g, 0)->rowCount()) files_->removeRow(g);
    if (n) zipsChanged();
    showPreview({});
    updateLabels();
    sevenZipReadOnly(inSevenZip);
}

// PopupNotUsedUpdateIconInfo*Click (the files; a zip's pictures stay as listed)
void ImagesManagerDialog::refreshFileInfo(bool all) {
    QList<QStandardItem*> items;
    if (all) {
        for (int g = 0; g < files_->rowCount(); ++g)
            for (int r = 0; r < files_->item(g, 0)->rowCount(); ++r) items << files_->item(g, 0)->child(r, 0);
    } else {
        items = selectedFiles();
    }
    for (QStandardItem* it : items) {
        if (!it->data(EntryRole).toString().isEmpty()) continue;
        QFileInfo fi(it->data(FileRole).toString());
        it->parent()->child(it->row(), 1)->setText(sizeText(fi.size()) + "  " + QLocale().toString(fi.lastModified(), QLocale::ShortFormat));
    }
}

// PopupNotUsedSaveIconsListToFileClick
void ImagesManagerDialog::saveFilesList() {
    QStringList paths = listedFiles();
    if (paths.isEmpty()) return;
    QString file = QFileDialog::getSaveFileName(this, tr("Save Invalid Images List To a File"), {}, tr("Text Files (*.txt)"));
    if (file.isEmpty()) return;
    QString out = "----------> Invalid Images <----------\r\n\r\n  -> System: " + s_.system +
                  "\r\n  -> Image Category: " + ImageFinder::title(category_) +
                  QString("\r\n     Total Files: %1\r\n\r\n").arg(paths.size()) + paths.join("\r\n") + "\r\n";
    if (writeTextFile(file, out, true))
        QMessageBox::information(this, windowTitle(), tr("The file \"%1\" was created from the list.").arg(QDir::toNativeSeparators(file)));
}

// ViewImageFullScreen: with the program the system opens pictures with
void ImagesManagerDialog::openFile() {
    QString f = previewFile_, e = previewEntry_;
    if (QList<QStandardItem*> sel = selectedFiles(); sel.size() == 1) {
        f = sel.first()->data(FileRole).toString();
        e = sel.first()->data(StoredRole).toString();
    }
    if (!e.isEmpty()) f = extractForView(f, e);
    if (!f.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(f));
}

// Apply Zip Changes: each zip written again without the pictures taken out (they go to the
// Recycle Bin) and with the new names
bool ImagesManagerDialog::applyZipChanges() {
    if (!pendingZipChanges()) return true;
    ProgressWindow win(windowTitle(), this);
    win.setCancelable(true);
    bool allOk = true;
    QStringList kept; // removed pictures that stayed in their folder
    for (auto it = edits_.begin(); it != edits_.end(); ++it) {
        ZipEditor& ed = it.value();
        if (!ed.changes()) continue;
        win.step(tr("Writing %1").arg(QDir::toNativeSeparators(ed.zip())), 100);
        const QString removedDir = ed.zip() + " - " + tr("removed pictures");
        QStringList extracted;
        QString err = ed.apply(removedDir, &extracted, [&win](int done, int total) {
            win.setProgress(total ? done * 100 / total : 0);
            return !win.canceled();
        });
        if (!err.isEmpty()) {
            allOk = false;
            for (const QString& f : extracted) QFile::remove(f);
            QDir().rmdir(removedDir);
            if (!win.canceled())
                QMessageBox::warning(this, windowTitle(), tr("%1 could not be changed (%2). It is as it was.")
                                                              .arg(QDir::toNativeSeparators(ed.zip()), err));
            continue;
        }
        const QStringList gone = moveToRecycleBin(extracted, this);
        if (gone.size() < extracted.size()) kept << QDir::toNativeSeparators(removedDir);
        else QDir().rmdir(removedDir);
    }
    win.close();
    for (auto it = edits_.begin(); it != edits_.end();)
        it = it->changes() ? std::next(it) : edits_.erase(it);
    s_.finder->forgetZips();
    zipsChanged();
    if (!kept.isEmpty())
        QMessageBox::information(this, windowTitle(), tr("The pictures taken out of the zip files that weren't deleted are in:\n%1")
                                                          .arg(kept.join('\n')));
    return allOk;
}

// Scan All Categories: every category's counts, with the games list's scan options
QList<ImagesManagerDialog::Summary> ImagesManagerDialog::scanAll(bool show) {
    QList<Summary> rows;
    if (!s_.finder) return rows;
    // (the names a file may have per category: usedNames(c))
    ProgressWindow win(tr("Scan All Categories"), this);
    win.setCancelable(true);
    for (int c = 0; c < ImageFinder::CountWithIcons; ++c) {
        win.step(tr("Scanning %1").arg(ImageFinder::title(c)), ImageFinder::CountWithIcons);
        win.setProgress(c);
        // the names only: reading every file to find the damaged ones is left to Scan Invalid Images
        if (win.canceled() || !indexOf(c, true, false, &win, c)) return {};
        const ImageIndex& idx = index_[c];
        Summary s{c, int(idx.entries().size()), 0, 0, 0, int(idx.unused(usedNames(c)).size()), !(idx.folders().isEmpty() && idx.zips().isEmpty())};
        for (const MameGame& g : s_.shown) {
            if (include(g, false, c)) ++(idx.hasFirst(g.imageName()) ? s.valid : s.missing);
            if (g.setStatus == 2 && include(g, true, c) && !idx.ofGame(g.imageName()).isEmpty()) ++s.ofMissing;
        }
        rows << s;
    }
    win.close();
    QApplication::processEvents(); // the window under it drawn again
    if (show) showSummary(rows);
    return rows;
}

void ImagesManagerDialog::showSummary(const QList<Summary>& rows) {
    QDialog d(this);
    d.setWindowTitle(tr("Scan All Categories"));
    auto* lay = new QVBoxLayout(&d);
    auto* t = new QTableWidget(int(rows.size()), 6, &d);
    t->setObjectName("SummaryTable");
    t->setHorizontalHeaderLabels({tr("Image Category"), tr("Images"), tr("Games With an Image"), tr("Games Without an Image"),
                                  tr("Images of Games You Don't Have"), tr("Invalid Images")});
    t->verticalHeader()->hide();
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectItems);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    for (int r = 0; r < rows.size(); ++r) {
        const Summary& s = rows[r];
        auto* cat = new QTableWidgetItem(s_.categoryIcon ? s_.categoryIcon(s.category) : QIcon(), ImageFinder::title(s.category));
        t->setItem(r, 0, cat);
        if (!s.folders) {
            auto* none = new QTableWidgetItem(tr("No folders"));
            none->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
            t->setItem(r, 1, none);
            t->setSpan(r, 1, 1, 5);
            continue;
        }
        const int v[] = {s.images, s.valid, s.missing, s.ofMissing, s.invalid};
        for (int c = 0; c < 5; ++c) {
            auto* it = new QTableWidgetItem(QLocale().toString(v[c]));
            it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (c > 0 && v[c]) it->setToolTip(tr("Double click to list them"));
            t->setItem(r, c + 1, it);
        }
    }
    t->resizeColumnsToContents();
    t->setCurrentCell(-1, -1);
    t->setFocusPolicy(Qt::NoFocus);
    for (int c = 1; c < 6; ++c) t->setColumnWidth(c, std::max(t->columnWidth(c), 90) + 12);
    t->horizontalHeader()->setStretchLastSection(false);
    int w = 4;
    for (int c = 0; c < 6; ++c) w += t->columnWidth(c);
    int h = t->horizontalHeader()->height() + 4;
    for (int r = 0; r < t->rowCount(); ++r) h += t->rowHeight(r);
    t->setMinimumSize(w, std::min(h, 600));
    lay->addWidget(t);
    auto* note = new QLabel(tr("Double click a count to list it. The counts use the scan options (Scan Options). "
                               "Damaged pictures are found when the invalid images are listed."), &d);
    note->setWordWrap(true);
    lay->addWidget(note);
    auto* close = new QPushButton(tr("Close"), &d);
    close->setAutoDefault(false);
    connect(close, &QPushButton::clicked, &d, &QDialog::reject);
    lay->addWidget(close, 0, Qt::AlignRight);
    int pick = -1, column = 0;
    connect(t, &QTableWidget::cellDoubleClicked, &d, [&](int r, int c) {
        if (r < 0 || r >= rows.size() || !rows[r].folders || c < 2) return;
        pick = r;
        column = c;
        d.accept();
    });
    if (d.exec() != QDialog::Accepted || pick < 0) return;
    const int c = rows[pick].category;
    setCategory(c);
    if (column == 2) scanValid(false);
    else if (column == 3) scanMissing(false, false);
    else if (column == 4) scanMissing(true, false);
    else scanInvalid(false);
}

// ---------------------------------------------------------------- Zip Images / Unzip Images

namespace {
QString bytesText(qint64 b) { return QLocale().formattedDataSize(b, 1, QLocale::DataSizeTraditionalFormat); }
QString imagesText(int n) {
    return n == 1 ? ImagesManagerDialog::tr("1 image") : ImagesManagerDialog::tr("%1 images").arg(QLocale().toString(n));
}
}

// What Zip Images would do: per folder of the category (each category, for all of them), its
// valid loose images (the selected games' only, for ZipSelected) and the invalid ones apart
QList<ImagesManagerDialog::ZipJob> ImagesManagerDialog::zipJobs(ZipScope scope, int category, ProgressWindow* win) {
    QList<ZipJob> jobs;
    // (the names a file may have per category: usedNames(c))
    QSet<QString> selected;
    if (scope == ZipSelected)
        for (const QModelIndex& i : ui_->MissingImagesList->selectionModel()->selectedRows())
            if (games_->item(i.row(), 6) && games_->item(i.row(), 5) && !games_->item(i.row(), 5)->text().isEmpty()) // not a parent's image
                selected.insert(games_->item(i.row(), 0)->data(NameRole).toString().toLower());
    const QList<int> cats = scope == ZipAllCategories ? [] { QList<int> l; for (int c = 0; c < ImageFinder::CountWithIcons; ++c) l << c; return l; }()
                                                      : QList<int>{category};
    for (int c : cats) {
        if (win) win->step(tr("Reading images: %1").arg(ImageFinder::title(c)));
        if (!indexOf(c, true, true, win)) return {}; // read now: what's in the folders this moment
        const ImageIndex& idx = index_[c];
        QSet<int> bad;
        for (int i : idx.unused(usedNames(c))) bad.insert(i);
        QSet<int> wanted;
        if (scope == ZipSelected)
            for (const QString& g : selected)
                for (int i : idx.ofGame(g)) wanted.insert(i);
        QHash<QString, int> jobOf; // folder -> its job
        for (int i = 0; i < idx.entries().size(); ++i) {
            const ImageEntry& e = idx.entries()[i];
            if (e.zipped() || (scope == ZipSelected && !wanted.contains(i))) continue;
            QString folder; // the category folder it's in (the longest that fits)
            for (const QString& f : idx.folders())
                if (e.file.startsWith(f + "/") && f.size() > folder.size()) folder = f;
            if (folder.isEmpty()) continue;
            int j = jobOf.value(folder, -1);
            if (j < 0) {
                ZipJob job;
                job.category = c;
                job.folder = folder;
                // in the folder, named after the category (snap\snap.zip): where EmuLoader and MAME look
                job.zip = QDir(folder).filePath(QString(ImageFinder::category(c).folder) + ".zip");
                jobs << job;
                j = int(jobs.size()) - 1;
                jobOf.insert(folder, j);
            }
            ZipJob& job = jobs[j];
            const ZipAdd add{e.file, QDir(folder).relativeFilePath(e.file)};
            if (bad.contains(i)) {
                job.invalid << add;
                job.invalidBytes += e.size;
            } else {
                job.files << add;
                job.bytes += e.size;
            }
        }
    }
    for (ZipJob& job : jobs) { // what the zip has already
        ZipArchive za;
        if (!za.open(job.zip)) continue;
        job.inZip = za.count();
        for (const QVector<ZipAdd>* list : {&job.files, &job.invalid})
            for (const ZipAdd& a : *list) job.conflicts += za.contains(a.name);
    }
    return jobs;
}

bool ImagesManagerDialog::runZip(QList<ZipJob> jobs, bool move, bool includeInvalid, bool replace, QStringList* report) {
    ProgressWindow win(tr("Zip Images"), this);
    win.setCancelable(true);
    bool allOk = true;
    QStringList done;
    for (ZipJob& job : jobs) {
        QVector<ZipAdd> files = job.files;
        if (includeInvalid) files += job.invalid;
        if (files.isEmpty()) continue;
        win.step(tr("Zipping %1").arg(QFileInfo(job.zip).fileName()), int(files.size()));
        const QLocale loc;
        QStringList added;
        const int before = job.inZip;
        const QString err = addToZip(job.zip, files, replace, [&](int d, int total) {
            const int mine = std::max(0, d - (total - int(files.size()))); // the zip's own files are copied first
            win.setProgress(mine, tr("Zipping %1 / %2").arg(loc.toString(mine), loc.toString(files.size())));
            return !win.canceled();
        }, &added);
        if (!err.isEmpty()) {
            allOk = false;
            done << tr("%1: not changed (%2)").arg(QDir::toNativeSeparators(job.zip), err);
            if (win.canceled()) break;
            continue;
        }
        Q_UNUSED(before);
        done << tr("%1: %2 added").arg(QDir::toNativeSeparators(job.zip), imagesText(int(added.size())));
        if (move && !added.isEmpty()) {
            win.hide();
            const QStringList gone = moveToRecycleBin(added, this);
            win.show();
            for (const QString& f : gone) // MAME's <name>\ folders, when they're empty now
                if (QFileInfo(f).absolutePath() != QDir(job.folder).absolutePath()) QDir().rmdir(QFileInfo(f).absolutePath());
            done << "  " + tr("%1 moved to the Recycle Bin").arg(imagesText(int(gone.size())));
            if (gone.size() < added.size())
                done << "  " + tr("%1 stayed in the folder").arg(imagesText(int(added.size() - gone.size())));
        }
    }
    win.close();
    s_.finder->forgetZips();
    zipsChanged();
    if (report) *report = done;
    return allOk;
}

void ImagesManagerDialog::zipImages(ZipScope scope) {
    if (!s_.finder) return;
    if (pendingZipChanges()) {
        if (QMessageBox::question(this, tr("Zip Images"), tr("Some changes to the zip files haven't been written yet. Write them first?")) !=
                QMessageBox::Yes ||
            !applyZipChanges())
            return;
    }
    QList<ZipJob> jobs;
    {
        ProgressWindow win(tr("Zip Images"), this);
        win.setCancelable(true);
        jobs = zipJobs(scope, category_, &win);
        if (win.canceled()) return;
    }
    int images = 0, invalid = 0;
    for (const ZipJob& j : jobs) {
        images += j.files.size();
        invalid += j.invalid.size();
    }
    if (!images && !invalid) {
        QMessageBox::information(this, tr("Zip Images"), tr("There are no images outside zip files to zip."));
        return;
    }
    // The window: what will happen, the choices
    QDialog d(this);
    d.setWindowTitle(tr("Zip Images"));
    auto* lay = new QVBoxLayout(&d);
    const QLocale loc;
    QString text = scope == ZipAllCategories ? tr("Every category's images go into a zip file in their folder:")
                                             : tr("The images go into a zip file in their folder:");
    text += "\n";
    int shown = 0;
    for (const ZipJob& j : jobs) {
        if (j.files.isEmpty() && j.invalid.isEmpty()) continue;
        if (++shown > 12) continue;
        text += "\n" + tr("%1 (%2): %3 -> %4").arg(imagesText(int(j.files.size())), bytesText(j.bytes),
                                                         QDir::toNativeSeparators(j.folder), QDir::toNativeSeparators(j.zip));
        if (j.inZip) text += " " + tr("(added to the %1 already in it)").arg(imagesText(j.inZip));
    }
    if (shown > 12) text += "\n" + tr("... and %1 more").arg(shown - 12);
    text += "\n\n" + (scope == ZipAllCategories
                          ? tr("Zipping every category can take a long time: an hour or more for a large collection. You can "
                               "cancel at any time; the zip being written then stays as it was.")
                          : tr("This may take a few minutes. You can cancel at any time; the zip then stays as it was."));
    auto* label = new QLabel(text, &d);
    label->setWordWrap(true);
    label->setMinimumWidth(560);
    lay->addWidget(label);
    auto* move = new QCheckBox(tr("Move the images into the zip (the folder's images go to the Recycle Bin afterwards)"), &d);
    move->setChecked(true);
    lay->addWidget(move);
    auto* inv = new QCheckBox(tr("Include the invalid images (%1: no game's, or damaged)").arg(loc.toString(invalid)), &d);
    inv->setVisible(invalid > 0);
    lay->addWidget(inv);
    // all categories: the question about images already in a zip, asked now (it may run unattended)
    QRadioButton *repl = nullptr, *keep = nullptr;
    int conflicts = 0;
    for (const ZipJob& j : jobs) conflicts += j.conflicts;
    if (scope == ZipAllCategories && conflicts) {
        lay->addWidget(new QLabel(tr("When an image is already in the zip (%1 images):").arg(loc.toString(conflicts)), &d));
        repl = new QRadioButton(tr("Replace it with the folder's"), &d);
        keep = new QRadioButton(tr("Keep the zip's"), &d);
        repl->setChecked(true);
        lay->addWidget(repl);
        lay->addWidget(keep);
    }
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* ok = new QPushButton(tr("Zip"), &d);
    auto* cancel = new QPushButton(tr("Cancel"), &d);
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    lay->addLayout(buttons);
    if (d.exec() != QDialog::Accepted) return;
    const bool withInvalid = inv->isChecked();
    // enough room: the new zip is written next to the old one
    QHash<QString, qint64> need; // drive root -> bytes
    for (const ZipJob& j : jobs) {
        const QString root = QStorageInfo(j.folder).rootPath();
        need[root] += j.bytes + (withInvalid ? j.invalidBytes : 0) + QFileInfo(j.zip).size();
    }
    for (auto it = need.cbegin(); it != need.cend(); ++it) {
        const qint64 free = QStorageInfo(it.key()).bytesAvailable();
        if (free >= 0 && free < it.value() + 50 * 1024 * 1024) {
            QMessageBox::warning(this, tr("Zip Images"), tr("There isn't enough free space on %1: %2 are needed, %3 are free.")
                                                             .arg(QDir::toNativeSeparators(it.key()), bytesText(it.value()), bytesText(free)));
            return;
        }
    }
    bool replace = true;
    if (repl) {
        replace = repl->isChecked();
    } else if (conflicts) { // one zip: asked now, with the facts
        const ZipJob* j = nullptr;
        for (const ZipJob& x : jobs)
            if (x.conflicts) j = &x;
        QMessageBox q(QMessageBox::Question, tr("Zip Images"),
                      tr("%1 already exists (%2). %3 of the folder's images are also in it.")
                          .arg(QDir::toNativeSeparators(j->zip), imagesText(j->inZip), loc.toString(conflicts)),
                      QMessageBox::Cancel, this);
        QPushButton* r = q.addButton(tr("Replace Them"), QMessageBox::AcceptRole);
        QPushButton* k = q.addButton(tr("Keep the Zip's"), QMessageBox::AcceptRole);
        q.exec();
        if (q.clickedButton() == r) replace = true;
        else if (q.clickedButton() == k) replace = false;
        else return;
    }
    QStringList report;
    runZip(jobs, move->isChecked(), withInvalid, replace, &report);
    refreshLists();
    QMessageBox::information(this, tr("Zip Images"), report.join('\n'));
}

// Pictures inside a .7z can't be deleted, renamed or moved: it's read only (no 7z writer)
void ImagesManagerDialog::sevenZipReadOnly(int count) {
    if (count <= 0) return;
    const QString then = tr("Convert it to a .zip first (Menu Options > Images > Convert .7z Picture Archives to .zip), "
                            "or unzip it (Unzip Images).");
    QMessageBox::information(this, windowTitle(),
                             (count == 1 ? tr("1 picture inside a .7z archive was left as it is: a .7z can only be read.")
                                         : tr("%1 pictures inside .7z archives were left as they are: a .7z can only be read.").arg(count)) +
                                 "\n\n" + then);
}

// Unzip Images: the category's zip files into their folders (snap.zip -> snap\)
// Where a zip's images go: <folder>\<category>.zip (and <folder>\<category>_sl.zip) into the
// folder; <folder>.zip into the folder beside it; a software list's <folder>\nes.zip into
// <folder>\nes (and <folder>\nes\nes.zip into that same folder)
QString ImagesManagerDialog::unzipFolder(int c, const QString& zip) const {
    QString folder = QFileInfo(zip).absolutePath();
    for (const QString& f : s_.finder->folders(c))
        if (QDir::cleanPath(QFileInfo(f).absoluteFilePath()) + ".zip" == QDir::cleanPath(zip)) folder = QFileInfo(f).absoluteFilePath();
    const QString list = s_.finder->zipPrefix(zip).chopped(s_.finder->zipPrefix(zip).isEmpty() ? 0 : 1);
    if (!list.isEmpty() && QFileInfo(folder).fileName().compare(list, Qt::CaseInsensitive) != 0) folder = QDir(folder).filePath(list);
    return folder;
}

bool ImagesManagerDialog::runUnzip(int c, bool replace, bool removeZips, QStringList* report) {
    QStringList done;
    bool allOk = true;
    ProgressWindow win(tr("Unzip Images"), this);
    win.setCancelable(true);
    const QLocale loc;
    for (const QString& zip : s_.finder->zipFiles(c)) {
        const QString folder = unzipFolder(c, zip);
        ZipArchive za;
        const int n = za.open(zip) ? za.count() : 0;
        za.close();
        win.step(tr("Unzipping %1").arg(QFileInfo(zip).fileName()), n);
        QStringList written;
        const QString err = extractZip(zip, folder, replace, [&](int d, int total) {
            win.setProgress(d, tr("Unzipping %1 / %2").arg(loc.toString(d), loc.toString(total)));
            return !win.canceled();
        }, &written);
        done << tr("%1: %2 unzipped into %3").arg(QFileInfo(zip).fileName(), imagesText(int(written.size())), QDir::toNativeSeparators(folder));
        if (!err.isEmpty()) {
            allOk = false;
            done << "  " + err;
            if (win.canceled()) break;
            continue;
        }
        if (removeZips) {
            win.hide();
            ZipArchive::release(zip); // the image panels let go of it first (Windows can't move an open file)
            s_.finder->forgetZips();
            if (moveToRecycleBin({zip}, this).isEmpty()) done << tr("%1 stayed").arg(QDir::toNativeSeparators(zip));
            win.show();
        }
    }
    win.close();
    s_.finder->forgetZips();
    zipsChanged();
    if (report) *report = done;
    return allOk;
}

void ImagesManagerDialog::unzipImages() {
    if (!s_.finder) return;
    const QStringList zips = s_.finder->zipFiles(category_);
    if (zips.isEmpty()) {
        QMessageBox::information(this, tr("Unzip Images"), tr("This category has no zip file."));
        return;
    }
    if (pendingZipChanges()) {
        if (QMessageBox::question(this, tr("Unzip Images"), tr("Some changes to the zip files haven't been written yet. Write them first?")) !=
                QMessageBox::Yes ||
            !applyZipChanges())
            return;
    }
    const QLocale loc;
    QString text = tr("The zip files' images go into their folders:") + "\n";
    int existing = 0;
    for (const QString& zip : zips) {
        ZipArchive za;
        if (!za.open(zip)) continue;
        qint64 bytes = 0;
        const QString folder = unzipFolder(category_, zip);
        for (const ZipArchive::Entry& e : za.entries()) {
            bytes += e.size;
            existing += QFileInfo::exists(QDir(folder).filePath(e.name));
        }
        text += "\n" + tr("%1 (%2): %3 -> %4").arg(imagesText(za.count()), bytesText(bytes), QDir::toNativeSeparators(zip),
                                                         QDir::toNativeSeparators(folder));
    }
    text += "\n\n" + tr("This may take a few minutes. You can cancel at any time.");
    QDialog d(this);
    d.setWindowTitle(tr("Unzip Images"));
    auto* lay = new QVBoxLayout(&d);
    auto* label = new QLabel(text, &d);
    label->setWordWrap(true);
    label->setMinimumWidth(520);
    lay->addWidget(label);
    auto* removeZip = new QCheckBox(tr("Move the zip files to the Recycle Bin afterwards"), &d);
    lay->addWidget(removeZip);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* ok = new QPushButton(tr("Unzip"), &d);
    auto* cancel = new QPushButton(tr("Cancel"), &d);
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    lay->addLayout(buttons);
    if (d.exec() != QDialog::Accepted) return;
    bool replace = false;
    if (existing) {
        QMessageBox q(QMessageBox::Question, tr("Unzip Images"),
                      tr("%1 of the zipped images are already in the folder.").arg(loc.toString(existing)), QMessageBox::Cancel, this);
        QPushButton* r = q.addButton(tr("Replace All"), QMessageBox::AcceptRole);
        QPushButton* k = q.addButton(tr("Skip All"), QMessageBox::AcceptRole);
        q.exec();
        if (q.clickedButton() == r) replace = true;
        else if (q.clickedButton() == k) replace = false;
        else return;
    }
    QStringList report;
    runUnzip(category_, replace, removeZip->isChecked(), &report);
    refreshLists();
    QMessageBox::information(this, tr("Unzip Images"), report.join('\n'));
}

// Files moved in or out of zips: the lists shown are made again
void ImagesManagerDialog::refreshLists() {
    const Mode was = mode_;
    const bool invalidShown = files_->rowCount() > 0 || invalidScanned_;
    quiet_ = true;
    if (was == Valid) scanValid(true);
    else if (was != None) scanMissing(was == NotUsed, true);
    if (invalidShown) scanInvalid(true);
    quiet_ = false;
}

void ImagesManagerDialog::runScan(int category, Scan scan) {
    quiet_ = true;
    if (category != category_) setCategory(category);
    if (scan == ScanInvalid) scanInvalid();
    else if (scan == ScanValid) scanValid();
    else scanMissing(scan == ScanImagesOfMissingGames);
    quiet_ = false;
}

QStringList ImagesManagerDialog::listedGames() const {
    QStringList out;
    for (int r = 0; r < games_->rowCount(); ++r) out << games_->item(r, 0)->data(NameRole).toString();
    return out;
}

QStringList ImagesManagerDialog::listedFiles() const {
    QStringList out;
    for (int g = 0; g < files_->rowCount(); ++g)
        for (int r = 0; r < files_->item(g, 0)->rowCount(); ++r) {
            const QStandardItem* it = files_->item(g, 0)->child(r, 0);
            out << joined(it->data(FileRole).toString(), it->data(EntryRole).toString());
        }
    return out;
}

void ImagesManagerDialog::deleteListed(Scan scan, bool selectedOnly) {
    if (scan == ScanInvalid) deleteInvalid(false, selectedOnly);
    else deleteNotUsed(false);
}

void ImagesManagerDialog::selectListed(const QList<int>& rows) {
    QList<QStandardItem*> all;
    for (int g = 0; g < files_->rowCount(); ++g)
        for (int r = 0; r < files_->item(g, 0)->rowCount(); ++r) all << files_->item(g, 0)->child(r, 0);
    QItemSelectionModel* sel = ui_->NotUsedImagesList->selectionModel();
    sel->clearSelection();
    for (int r : rows)
        if (r >= 0 && r < all.size()) sel->select(all[r]->index(), QItemSelectionModel::Select | QItemSelectionModel::Rows);
}

bool ImagesManagerDialog::renameListed(int row, const QString& name) {
    QList<QStandardItem*> all;
    for (int g = 0; g < files_->rowCount(); ++g)
        for (int r = 0; r < files_->item(g, 0)->rowCount(); ++r) all << files_->item(g, 0)->child(r, 0);
    return row >= 0 && row < all.size() && doRename(all[row], name);
}

// The window's anchors move the lists; the labels and buttons under them follow (SplitterListMoved)
void ImagesManagerDialog::resizeEvent(QResizeEvent* e) {
    QDialog::resizeEvent(e);
    if (applyZips_) QTimer::singleShot(0, this, [this] { updateLabels(); });
}

bool ImagesManagerDialog::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::MouseButtonRelease) {
        if (o == ui_->ImageCategoryIcon || o == ui_->ImageCategorySelectLabel) {
            selectCategory();
            return true;
        }
        if (o == ui_->ImagePreview && !previewFile_.isEmpty()) {
            QString f = previewEntry_.isEmpty() ? previewFile_ : extractForView(previewFile_, previewEntry_);
            if (!f.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(f));
            return true;
        }
    }
    if (e->type() == QEvent::KeyPress) {
        int k = static_cast<QKeyEvent*>(e)->key();
        if (o == ui_->MissingImagesList) {
            if (k == Qt::Key_Delete) { removeSelectedGames(); return true; }
            if (k == Qt::Key_Return || k == Qt::Key_Enter) { playSelected(); return true; }
        }
        if (o == ui_->NotUsedImagesList) {
            if (k == Qt::Key_Delete) { removeSelectedFiles(false); return true; }
            if (k == Qt::Key_F2) { renameFile(); return true; }
            if (k == Qt::Key_Return || k == Qt::Key_Enter) { openFile(); return true; }
        }
    }
    return QDialog::eventFilter(o, e);
}

} // namespace eld
