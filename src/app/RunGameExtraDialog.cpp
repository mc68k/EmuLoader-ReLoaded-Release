#include "RunGameExtraDialog.h"

#include "RecycleBin.h"

#include "ElWidgets.h"
#include "MameIni.h"
#include "RunExtras.h"
#include "TextFile.h"
#include "VclRuntime.h"
#include "WinIni.h"
#include "ui_FormArcadeRunGameExtraMAME.h"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QRadioButton>
#include <QRegularExpressionValidator>
#include <QStandardItemModel>
#include <QTreeView>

namespace eld {

namespace {

enum Role { FolderRole = Qt::UserRole + 1, SortRole, FileRole };
enum Column { NameCol, SizeCol, DateCol, FolderCol };

// GetSizeType
QString sizeText(qint64 bytes) {
    if (bytes < 1024) return RunGameExtraDialog::tr("%1 Bytes").arg(bytes);
    static const char* units[] = {"KB", "MB", "GB"};
    double v = bytes;
    int u = -1;
    while (v >= 1024 && u < 2) { v /= 1024; ++u; }
    QString s = QLocale().toString(v, 'f', 2);
    QChar dp = QLocale().decimalPoint().at(0);
    while (s.contains(dp) && (s.endsWith('0') || s.endsWith(dp))) s.chop(1);
    return s + " " + units[u];
}

QString withSlash(const QString& folder) {
    QString f = QDir::cleanPath(folder);
    return f.endsWith('/') ? f : f + '/';
}

// FullEmuFolderFix: the first folder of the option, relative to MAME's folder.
QString mameFolderOption(const QMap<QString, QString>& ini, const char* key, const char* def, const QString& mameFolder) {
    QString v = ini.value(key);
    QStringList dirs = splitPathList(v.isEmpty() ? QString(def) : v);
    return withSlash(resolvePath(mameFolder, dirs.isEmpty() ? QString(def) : dirs.first()));
}

bool iniBool(const QMap<QString, QString>& ini, const char* key, bool def = false) {
    QString v = ini.value(key);
    return v.isEmpty() ? def : v.toInt() != 0;
}

} // namespace

RunGameExtraDialog::RunGameExtraDialog(Frontend& fe, SystemId sys, const Input& in, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormArcadeRunGameExtraMAME), fe_(fe), sys_(sys), in_(in) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Run Game (Extra)"));
    const MameGame& g = in_.game;

    // Top bar
    static const int topColors[] = {0xF0FAE5, 0xE5F0FA, 0xEEEEEE};
    ui_->TopBar->setProperty("vclColor1", ElColorBox::toQColor(topColors[std::clamp<int>(g.setStatus, 0, 2)]).name());
    ui_->TopBar->setProperty("vclColor2", QColor(0xF1F1F1).name());
    ui_->GameIcon->setPixmap(in_.gameIcon.pixmap(68, 68));
    ui_->GameIcon->setAlignment(Qt::AlignCenter);
    ui_->SystemIcon->setPixmap(QIcon(in_.emuIcon).pixmap(24, 24));
    ui_->LabelGameTitle->setText(g.title);
    QString version = fe_.emulatorVersion(sys_);
    ui_->LabelGameName->setText(tr("name: %1").arg(in_.nameLine) + (version.isEmpty() ? QString() : "\n" + version));
    ui_->LabelGameName->adjustSize();

    ui_->LabelInfo->setText(tr("Check features you want to use. Uncheck to disable."));
    ui_->LabelInfo->adjustSize();
    ui_->ButtonOk->setText(tr("OK"));
    ui_->ButtonAbort->setText(tr("Cancel"));
    connect(ui_->ButtonOk, &QPushButton::clicked, this, &RunGameExtraDialog::accept);
    connect(ui_->ButtonAbort, &QPushButton::clicked, this, &QDialog::reject);
    for (QPushButton* b : findChildren<QPushButton*>()) b->setAutoDefault(false);

    readMameSettings();
    setupPages();
    setupMemoryCards();
    for (Page p : {PageInput, PageState, PageCard, PageMovie}) fillList(p);
    setInputMode(false);
    filterMovies();
    showPage(PageInput);
    for (int p = PageInput; p <= PageMovie; ++p) setEnabledPage(p, false);
    // FormShow: a white window as wide as a page, down to the buttons.
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::white);
    setPalette(pal);
    // rows and columns from the design: as big as the texts need (a translation, a larger font);
    // the folders and files shown are data, shortened when long
    for (QWidget* l : {(QWidget*)ui_->LabelRecordMovieRootFolder, (QWidget*)ui_->LabelInputSelectedFileFolder,
                       (QWidget*)ui_->LabelSaveStateSelectedFileFolder, (QWidget*)ui_->LabelMemoryCardSelectedFileFolder,
                       (QWidget*)ui_->LabelRecordMovieSelectedFileFolder})
        vcl::setDataLabel(l);
    for (QPushButton* b : {ui_->PageButtonInput, ui_->PageButtonSaveState, ui_->PageButtonMemoryCard, ui_->PageButtonRecordMovie}) {
        b->setFixedHeight(22); // (the tabs at their design height; showPage makes the page's taller)
        vcl::place(b, QRect(b->x(), 89, b->width(), 22));
    }
    vcl::layoutForm(this);
    showPage(PageInput); // (its button taller again: laying out set the design heights)
}

RunGameExtraDialog::~RunGameExtraDialog() = default;

RunGameExtraDialog::List& RunGameExtraDialog::list(Page page) { return lists_[page - 1]; }

// ParseMAMEIni / ReadMAMESettings: the folders from mame.ini; the options as MAME would see
// them for this game (mame.ini, then the ini files up to <game>.ini).
void RunGameExtraDialog::readMameSettings() {
    const QString mameFolder = fe_.mameFolder(sys_);
    ChainMachine m = fe_.chainMachine(in_.game);
    auto base = MameIniChain::effective(m, fe_.mameIniPath(sys_), fe_.iniDir(sys_), fe_.mameBuild(sys_), 0);
    auto all = MameIniChain::effective(m, fe_.mameIniPath(sys_), fe_.iniDir(sys_), fe_.mameBuild(sys_),
                                       MameIniChain::kLevels - 1);
    folderInput_ = mameFolderOption(base, "input_directory", "inp", mameFolder);
    folderState_ = mameFolderOption(base, "state_directory", "sta", mameFolder);
    folderCard_ = mameFolderOption(base, "memcard_directory", "memcard", mameFolder);
    movieRoot_ = mameFolderOption(base, "snapshot_directory", "snap", mameFolder);
    folderMovie_ = movieRoot_ + "record_video/";
    autosave_ = iniBool(all, "autosave");
    exitAfterPlayback_ = iniBool(all, "exit_after_playback");
    recordTimecode_ = iniBool(all, "record_timecode");
    // Older MAME builds don't have these two (FoundInputExtra).
    inputExtras_ = base.contains("exit_after_playback") || base.contains("record_timecode") || base.isEmpty();
}

void RunGameExtraDialog::setupPages() {
    const MameGame& g = in_.game;
    // Page buttons (TSpeedButtonEx, GroupIndex 1): as wide as their captions, one after the other.
    QPushButton* pages[] = {ui_->PageButtonInput, ui_->PageButtonSaveState, ui_->PageButtonMemoryCard,
                            ui_->PageButtonRecordMovie};
    const char* captions[] = {QT_TR_NOOP("Playback/Record an Input File"), QT_TR_NOOP("Load a Save State"),
                              QT_TR_NOOP("Insert Memory Card"), QT_TR_NOOP("Record Current Section to a Movie")};
    int x = 4;
    for (int i = 0; i < 4; ++i) {
        QPushButton* b = pages[i];
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setStyleSheet("QPushButton { border: 1px solid #B4B4B4; border-bottom: none; background: #F0F0F0;"
                         " color: #505050; padding: 0 8px; }"
                         "QPushButton:checked { background: white; color: black; }"
                         "QPushButton:disabled { color: #A0A0A0; }");
        int w = b->fontMetrics().horizontalAdvance(tr(captions[i])) + 30;
        vcl::place(b, QRect(x, 89, w, 22));
        b->setText(tr(captions[i])); // once it's wide enough (it's shortened with "..." otherwise)
        vcl::setRowBottom(b); // (tabs standing on the page: the selected one taller, showPage)
        x += w - 1;
        connect(b, &QPushButton::clicked, this, [this, i] { showPage(i + 1); });
    }

    // The pages all sit where the first one is.
    QWidget* panels[] = {ui_->Panel1_PlayRecInputFile, ui_->Panel2_SaveState, ui_->Panel3_MemoryCard,
                         ui_->Panel4_RecordMovie};
    QRect area = ui_->Panel1_PlayRecInputFile->geometry();
    for (QWidget* p : panels) vcl::place(p, area);

    // The lists
    struct L { QTreeView* v; Page p; bool folderCol; };
    const L ls[] = {{ui_->InputListView, PageInput, true}, {ui_->SaveStateListView, PageState, false},
                    {ui_->MemoryCardListView, PageCard, true}, {ui_->RecordMovieListView, PageMovie, true}};
    for (const L& l : ls) {
        auto* model = new QStandardItemModel(0, l.folderCol ? 4 : 3, this);
        QStringList heads{tr("Name"), tr("Size"), tr("Date Modified")};
        if (l.folderCol) heads << tr("Folder");
        model->setHorizontalHeaderLabels(heads);
        model->setSortRole(SortRole);
        QTreeView* v = l.v;
        v->setModel(model);
        v->setRootIsDecorated(false);
        v->setUniformRowHeights(true);
        v->setSelectionMode(QAbstractItemView::SingleSelection);
        v->setSelectionBehavior(QAbstractItemView::SelectRows);
        v->setEditTriggers(QAbstractItemView::NoEditTriggers);
        v->setSortingEnabled(true);
        v->header()->setSortIndicator(DateCol, Qt::DescendingOrder);
        v->setColumnWidth(NameCol, l.folderCol ? 330 : 430);
        v->setColumnWidth(SizeCol, 100);
        v->setColumnWidth(DateCol, 145);
        v->setStyleSheet("QTreeView::item { height: 20px; }");
        v->setContextMenuPolicy(Qt::CustomContextMenu);
        v->installEventFilter(this);
        lists_.append({v, model, l.p});
        Page page = l.p;
        connect(v->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this, page] { selected(page); });
        connect(model, &QStandardItemModel::itemChanged, this, [this, page](QStandardItem* it) {
            if (renaming_ && it->column() == NameCol) onRenamed(page, it->row());
        });
        connect(v, &QWidget::customContextMenuRequested, this, [this, page, v](const QPoint& pos) {
            QMenu m(this);
            bool any = v->currentIndex().isValid();
            if (page == PageCard) {
                for (int s = 0; s < 4; ++s) {
                    QAction* a = m.addAction(tr("Assign to Slot %1").arg(s + 1), this, [this, s] { assignCard(s); });
                    a->setVisible(s < cardSlots_.size());
                    a->setEnabled(any);
                }
                m.addSeparator();
            }
            m.addAction(tr("Rename File"), this, [this, page] { renameFile(page); })->setEnabled(any);
            m.addAction(tr("Delete File"), this, [this, page] { deleteFile(page); })->setEnabled(any);
            m.exec(v->viewport()->mapToGlobal(pos));
        });
        if (page == PageCard)
            connect(v, &QTreeView::doubleClicked, this, [this] { assignCard(-1); });
    }

    // Enabled boxes (navy when ticked: SetCheckBoxStateColor)
    QCheckBox* enabled[] = {ui_->Enabled_PlaybackRecordInput, ui_->Enabled_LoadSaveState, ui_->Enabled_InsertMemoryCard,
                            ui_->Enabled_RecordMovie};
    for (int i = 0; i < 4; ++i) {
        enabled[i]->setText(tr("Enabled"));
        enabled[i]->adjustSize();
        connect(enabled[i], &QCheckBox::toggled, this, [this, i](bool on) { setEnabledPage(i + 1, on); });
    }

    // Folder labels
    for (QLabel* l : {ui_->LabelInputSelectedFileFolder, ui_->LabelSaveStateSelectedFileFolder,
                      ui_->LabelMemoryCardSelectedFileFolder, ui_->LabelRecordMovieSelectedFileFolder})
        l->clear();

    // Input page
    ui_->InputPlayback->setText(tr("Playback"));
    ui_->InputRecord->setText(tr("Record"));
    ui_->InputExitEmulatorAfterInputPlayback->setText(tr("Exit Emulator After Input Playback"));
    ui_->InputExitEmulatorAfterInputPlayback->setToolTip(tr("To be used with playback input"));
    ui_->InputRecordTimecodeFile->setText(tr("Record an Input Timecode File"));
    ui_->InputRecordTimecodeFile->setToolTip(tr("To be used with record input"));
    ui_->InputExitEmulatorAfterInputPlayback->setChecked(exitAfterPlayback_);
    ui_->InputRecordTimecodeFile->setChecked(recordTimecode_);
    int rx = ui_->InputPlayback->x();
    for (QWidget* w : {(QWidget*)ui_->InputPlayback, (QWidget*)ui_->InputRecord}) {
        w->adjustSize();
        vcl::place(w, QRect(rx, w->y(), w->width(), w->height()));
        rx += w->width() + 10;
    }
    for (QWidget* w : {(QWidget*)ui_->InputExitEmulatorAfterInputPlayback, (QWidget*)ui_->InputRecordTimecodeFile}) {
        w->adjustSize();
        vcl::place(w, QRect(rx + 16, w->y(), w->width(), w->height()));
    }
    connect(ui_->InputPlayback, &QRadioButton::clicked, this, [this] { setInputMode(false); });
    connect(ui_->InputRecord, &QRadioButton::clicked, this, [this] { setInputMode(true); });
    ui_->ButtonInputResetFileName->setText(tr("Reset"));
    ui_->ButtonInputResetFileName->setToolTip(tr("Click here to reset filename to game name"));
    ui_->ButtonInputResetFileNameTitle->setText(tr("Reset to Title"));
    ui_->ButtonInputResetFileNameTitle->setToolTip(tr("Click here to reset filename to game title"));
    connect(ui_->ButtonInputResetFileName, &QPushButton::clicked, this, [this] {
        recordFile_ = in_.game.name + ".inp";
        reselectInput();
    });
    connect(ui_->ButtonInputResetFileNameTitle, &QPushButton::clicked, this, [this] {
        recordFile_ = cleanName(in_.game.title) + ".inp";
        reselectInput();
    });
    // Playback/record files can't have a path (InputFileNameKeyPress)
    auto* noPath = new QRegularExpressionValidator(QRegularExpression(R"([^/*?<>|:;\\"]*)"), this);
    ui_->InputFileName->setValidator(noPath);
    connect(ui_->InputFileName, &QLineEdit::textEdited, this, [this](const QString& t) { recordFile_ = t; });

    // Save state page
    ui_->AutoSaveState->setText(tr("Auto Save State"));
    ui_->AutoSaveState->setToolTip(tr("Enable automatic restore at startup, and automatic save at exit time"));
    ui_->AutoSaveState->setChecked(autosave_);
    ui_->AutoSaveState->adjustSize();
    ui_->AutoSaveState->show();
    ui_->LabelLoadSaveStateNotSupportedMsg->setText(tr("Save states are not officially supported by this machine!"));
    ui_->LabelLoadSaveStateNotSupportedMsg->adjustSize();
    QRect ns = ui_->LabelLoadSaveStateNotSupportedMsg->geometry();
    vcl::place(ui_->LabelLoadSaveStateNotSupportedMsg,
               QRect(ui_->Panel2_SaveState->width() - 10 - ns.width(), ns.y(), ns.width(), ns.height()));
    ui_->PanelDisabledSaveStateNotSupportedMsg->hide();

    // Memory card page
    for (QPushButton* b : {ui_->ButtonInsertMemoryCard_Slot1_SelectFile, ui_->ButtonInsertMemoryCard_Slot2_SelectFile,
                           ui_->ButtonInsertMemoryCard_Slot3_SelectFile, ui_->ButtonInsertMemoryCard_Slot4_SelectFile}) {
        b->setText(tr("Select"));
        b->setToolTip(tr("Click here to select a file"));
    }
    for (QPushButton* b : {ui_->ButtonInsertMemoryCard_Slot1_LastUsed, ui_->ButtonInsertMemoryCard_Slot2_LastUsed,
                           ui_->ButtonInsertMemoryCard_Slot3_LastUsed, ui_->ButtonInsertMemoryCard_Slot4_LastUsed}) {
        b->setText(tr("Last Used"));
        b->setToolTip(tr("Select last used memory card file (might not be the newest)"));
    }
    for (QPushButton* b : {ui_->ButtonInsertMemoryCard_Slot1_Clear, ui_->ButtonInsertMemoryCard_Slot2_Clear,
                           ui_->ButtonInsertMemoryCard_Slot3_Clear, ui_->ButtonInsertMemoryCard_Slot4_Clear}) {
        b->setText(tr("Clear"));
        b->setToolTip(tr("Click here to empty this memcard slot"));
    }

    // Movie page
    ui_->RecordMovieAVI->setText(tr("AVI File"));
    ui_->RecordMovieAVI->setToolTip(tr("Uncompressed video/audio, huge space disk required!"));
    ui_->RecordMovieMNG->setText(tr("MNG File"));
    ui_->RecordMovieWAV->setText(tr("WAV File"));
    int mx = ui_->RecordMovieAVI->x();
    for (QWidget* w : {(QWidget*)ui_->RecordMovieAVI, (QWidget*)ui_->RecordMovieMNG, (QWidget*)ui_->RecordMovieWAV}) {
        w->adjustSize();
        vcl::place(w, QRect(mx, w->y(), w->width(), w->height()));
        mx += w->width() + 10;
    }
    ui_->RecordMovieAVI->setChecked(true);
    for (QRadioButton* r : {ui_->RecordMovieAVI, ui_->RecordMovieMNG, ui_->RecordMovieWAV})
        connect(r, &QRadioButton::clicked, this, [this] { filterMovies(); });
    QString root = tr("Root folder:") + " " + QDir::toNativeSeparators(folderMovie_);
    QRect rf = ui_->LabelRecordMovieRootFolder->geometry();
    vcl::place(ui_->LabelRecordMovieRootFolder, QRect(mx + 6, rf.y(), ui_->Panel4_RecordMovie->width() - 10 - mx - 6, rf.height()));
    ui_->LabelRecordMovieRootFolder->setText(root);
    ui_->LabelRecordMovieRootFolder->setToolTip(root);
    ui_->LabelRecordMovieFileName->setText(tr("File to Save (file extension optional)"));
    ui_->ButtonRecordMovieResetFileName->setText(tr("Reset"));
    ui_->ButtonRecordMovieResetFileName->setToolTip(tr("Click here to reset filename to game name"));
    ui_->ButtonRecordMovieResetFileNameTitle->setText(tr("Reset to Title"));
    ui_->ButtonRecordMovieResetFileNameTitle->setToolTip(tr("Click here to reset filename to game title"));
    ui_->RecordMovieFileName->setValidator(noPath);
    auto movieIndex = [this] { return ui_->RecordMovieAVI->isChecked() ? 0 : ui_->RecordMovieMNG->isChecked() ? 1 : 2; };
    connect(ui_->RecordMovieFileName, &QLineEdit::textChanged, this,
            [this, movieIndex](const QString& t) { movieFile_[movieIndex()] = t; });
    connect(ui_->ButtonRecordMovieResetFileName, &QPushButton::clicked, this, [this] {
        ui_->RecordMovieFileName->setText(in_.game.name + movieExtension());
        reselectMovie();
    });
    connect(ui_->ButtonRecordMovieResetFileNameTitle, &QPushButton::clicked, this, [this] {
        ui_->RecordMovieFileName->setText(cleanName(in_.game.title) + movieExtension());
        reselectMovie();
    });
    // The label before each edit box as wide as its caption, the box after it.
    auto labelEdit = [](QLabel* l, QWidget* edit) {
        l->adjustSize();
        QRect e = edit->geometry();
        int x = l->x() + l->width() + 8;
        vcl::place(edit, QRect(x, e.y(), e.right() - x + 1, e.height()));
    };
    labelEdit(ui_->LabelRecordMovieFileName, ui_->RecordMovieFileName);
    QRect lf = ui_->LabelInputFileName->geometry();
    ui_->LabelInputFileName->setText(tr("New File (file extension optional)"));
    ui_->LabelInputFileName->adjustSize();
    int lw = ui_->LabelInputFileName->width();
    ui_->LabelInputFileName->setText(tr("Selected File (file extension optional)"));
    ui_->LabelInputFileName->adjustSize();
    lw = std::max(lw, ui_->LabelInputFileName->width());
    vcl::place(ui_->LabelInputFileName, QRect(lf.x(), lf.y(), lw, lf.height()));
    QRect ie = ui_->InputFileName->geometry();
    vcl::place(ui_->InputFileName, QRect(lf.x() + lw + 8, ie.y(), ie.right() - lf.x() - lw - 7, ie.height()));

    // The panels that hide a page's controls while it's not enabled (PanelDisabled*, AdjustDisabledPanel).
    QWidget* covers[] = {ui_->PanelDisabledInput, ui_->PanelDisabledSaveState, ui_->PanelDisabledMemoryCard,
                         ui_->PanelDisabledRecordMovie};
    for (QWidget* c : covers) {
        QWidget* p = c->parentWidget();
        vcl::place(c, QRect(8, 22, p->width() - 16, p->height() - 22 - 8));
        vcl::setOverlay(c); // (over the page's controls, as big as the page grows)
        c->setProperty("vclColor", QColor(Qt::white).name()); // the page's color (FormShow: clWhite)
        c->setProperty("vclBevelOuter", "bvNone");
        c->raise();
    }
    Q_UNUSED(g);
}

// PageButtonInputClick
void RunGameExtraDialog::showPage(int page) {
    QPushButton* pages[] = {ui_->PageButtonInput, ui_->PageButtonSaveState, ui_->PageButtonMemoryCard,
                            ui_->PageButtonRecordMovie};
    QWidget* panels[] = {ui_->Panel1_PlayRecInputFile, ui_->Panel2_SaveState, ui_->Panel3_MemoryCard,
                         ui_->Panel4_RecordMovie};
    page_ = page;
    for (int i = 0; i < 4; ++i) {
        bool on = i + 1 == page;
        pages[i]->setChecked(on);
        pages[i]->setFixedHeight(on ? 25 : 22); // the page's button stands out (at the bottom of the row)
        panels[i]->setVisible(on);
    }
}

// Enabled_*Click: a page's controls show while its box is ticked.
void RunGameExtraDialog::setEnabledPage(int page, bool on) {
    QCheckBox* boxes[] = {ui_->Enabled_PlaybackRecordInput, ui_->Enabled_LoadSaveState, ui_->Enabled_InsertMemoryCard,
                          ui_->Enabled_RecordMovie};
    QWidget* covers[] = {ui_->PanelDisabledInput, ui_->PanelDisabledSaveState, ui_->PanelDisabledMemoryCard,
                         ui_->PanelDisabledRecordMovie};
    QCheckBox* b = boxes[page - 1];
    if (b->isChecked() != on) b->setChecked(on);
    b->setStyleSheet(on ? "QCheckBox { color: navy; }" : QString());
    covers[page - 1]->setVisible(!on);
    if (page == PageState) {
        ui_->LabelLoadSaveStateNotSupportedMsg->setVisible(on && in_.game.saveState != 1);
        ui_->AutoSaveState->setVisible(on);
    }
    if (page == PageInput) {
        ui_->InputExitEmulatorAfterInputPlayback->setVisible(on && inputExtras_ && !ui_->InputRecord->isChecked());
        ui_->InputRecordTimecodeFile->setVisible(on && inputExtras_ && ui_->InputRecord->isChecked());
    }
    if (on) {
        if (page == PageInput) reselectInput();
        else if (page == PageMovie) reselectMovie();
        else if (list(Page(page)).model->rowCount() && !list(Page(page)).view->currentIndex().isValid())
            list(Page(page)).view->setCurrentIndex(list(Page(page)).model->index(0, 0));
        list(Page(page)).view->setFocus();
    }
}

void RunGameExtraDialog::addFile(Page page, const QString& folder, const QString& file) {
    List& l = list(page);
    QFileInfo fi(folder + file);
    if (!fi.isFile()) return;
    for (int r = 0; r < l.model->rowCount(); ++r) // GetFilesMulti can find a file twice (name and title)
        if (QFileInfo(l.model->item(r, 0)->data(FolderRole).toString() + l.model->item(r, 0)->text()) == fi) return;
    auto* name = new QStandardItem(file);
    name->setData(folder, FolderRole);
    name->setData(file.toLower(), SortRole);
    name->setData(file, FileRole); // the name on disk while it's edited
    name->setEditable(true);
    auto* size = new QStandardItem(sizeText(fi.size()));
    size->setData(fi.size(), SortRole);
    size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QDateTime t = fi.lastModified();
    auto* date = new QStandardItem(QLocale().toString(t, QLocale::ShortFormat));
    date->setData(t, SortRole);
    QList<QStandardItem*> row{name, size, date};
    if (l.model->columnCount() > 3) {
        // the folder under the page's own one (TFileInfo.eFileFolderRelative)
        QString base = page == PageInput ? folderInput_ : page == PageCard ? folderCard_ : folderMovie_;
        QString rel = folder.startsWith(base, Qt::CaseInsensitive) ? folder.mid(base.size()) : folder;
        auto* f = new QStandardItem(QDir::toNativeSeparators(rel));
        f->setData(rel.toLower(), SortRole);
        f->setToolTip(QDir::toNativeSeparators(folder));
        row << f;
    }
    for (int c = 1; c < row.size(); ++c) row[c]->setEditable(false);
    l.model->appendRow(row);
}

void RunGameExtraDialog::addMatching(Page page, const QString& folder, const QString& pattern) {
    QDir d(folder);
    if (!d.exists()) return;
    for (const QString& f : d.entryList({pattern}, QDir::Files, QDir::Name)) addFile(page, folder, f);
}

// GetFiles: the game's files in the page's folder, and every file in its <game> subfolder.
void RunGameExtraDialog::fillList(Page page) {
    List& l = list(page);
    l.model->removeRows(0, l.model->rowCount());
    const QString name = in_.game.name, title = cleanName(in_.game.title);
    switch (page) {
    case PageInput:
        addFile(PageInput, folderInput_, name + ".inp");
        addMatching(PageInput, folderInput_ + name + "/", "*.inp");
        break;
    case PageState:
        addMatching(PageState, folderState_ + name + "/", "*.sta");
        break;
    case PageCard:
        addMatching(PageCard, folderCard_, name + ".*");
        if (!title.isEmpty()) addMatching(PageCard, folderCard_, title + ".*");
        addMatching(PageCard, folderCard_ + name + "/", "*");
        break;
    case PageMovie:
        for (const char* ext : {".avi", ".mng", ".wav"}) {
            addFile(PageMovie, folderMovie_, name + ext);
            if (!title.isEmpty()) addFile(PageMovie, folderMovie_, title + ext);
            addMatching(PageMovie, folderMovie_ + name + "/", QString("*") + ext);
        }
        break;
    }
    l.view->sortByColumn(l.view->header()->sortIndicatorSection(), l.view->header()->sortIndicatorOrder());
}

// *ListViewItemSelectionChanged: the file's folder under the list; its name in the edit box.
void RunGameExtraDialog::selected(Page page) {
    List& l = list(page);
    QModelIndex i = l.view->currentIndex();
    QLabel* folder = page == PageInput ? ui_->LabelInputSelectedFileFolder
                   : page == PageState ? ui_->LabelSaveStateSelectedFileFolder
                   : page == PageCard ? ui_->LabelMemoryCardSelectedFileFolder
                                        : ui_->LabelRecordMovieSelectedFileFolder;
    if (!i.isValid()) {
        folder->clear();
        return;
    }
    QStandardItem* it = l.model->item(i.row(), NameCol);
    QString dir = it->data(FolderRole).toString();
    folder->setText(" " + QDir::toNativeSeparators(dir));
    folder->setToolTip(QDir::toNativeSeparators(dir));
    if (page == PageInput) {
        ui_->InputFileName->setText(it->text());
        (ui_->InputRecord->isChecked() ? recordFile_ : playbackFile_) = dir + it->text();
    } else if (page == PageMovie) {
        ui_->RecordMovieFileName->setText(it->text());
    }
}

void RunGameExtraDialog::selectFile(Page page, const QString& path) {
    List& l = list(page);
    bool full = path.contains('/');
    for (int r = 0; r < l.model->rowCount(); ++r) {
        QStandardItem* it = l.model->item(r, NameCol);
        if (l.view->isRowHidden(r, QModelIndex())) continue;
        QString p = full ? it->data(FolderRole).toString() + it->text() : it->text();
        if (p.compare(path, Qt::CaseInsensitive) == 0) {
            l.view->setCurrentIndex(l.model->index(r, 0));
            l.view->scrollTo(l.model->index(r, 0));
            return;
        }
    }
    l.view->selectionModel()->clear();
    l.view->setCurrentIndex(QModelIndex());
}

// InputPlaybackClick
void RunGameExtraDialog::setInputMode(bool record) {
    ui_->InputPlayback->setChecked(!record);
    ui_->InputRecord->setChecked(record);
    QFont f = ui_->InputPlayback->font();
    f.setBold(!record);
    ui_->InputPlayback->setFont(f);
    f.setBold(record);
    ui_->InputRecord->setFont(f);
    bool on = ui_->Enabled_PlaybackRecordInput->isChecked();
    ui_->InputExitEmulatorAfterInputPlayback->setVisible(on && inputExtras_ && !record);
    ui_->InputRecordTimecodeFile->setVisible(on && inputExtras_ && record);
    ui_->ButtonInputResetFileName->setEnabled(record);
    ui_->ButtonInputResetFileNameTitle->setEnabled(record);
    ui_->InputFileName->setEnabled(record);
    ui_->LabelInputFileName->setText(record ? tr("New File (file extension optional)")
                                            : tr("Selected File (file extension optional)"));
    ui_->LabelInputFileName->setStyleSheet(record ? "color: black;" : "color: #505050;");
    reselectInput();
}

// ReSelectInputFile
void RunGameExtraDialog::reselectInput() {
    List& l = list(PageInput);
    bool record = ui_->InputRecord->isChecked();
    if (record) {
        if (recordFile_.isEmpty()) recordFile_ = in_.game.name + ".inp";
        QString want = recordFile_;
        selectFile(PageInput, want);
        if (!l.view->currentIndex().isValid()) ui_->InputFileName->setText(QFileInfo(want).fileName());
    } else {
        if (!playbackFile_.isEmpty()) selectFile(PageInput, playbackFile_);
        if (!l.view->currentIndex().isValid() && l.model->rowCount()) l.view->setCurrentIndex(l.model->index(0, 0));
        if (!l.model->rowCount()) ui_->InputFileName->clear();
    }
}

QString RunGameExtraDialog::movieExtension() const {
    return ui_->RecordMovieAVI->isChecked() ? ".avi" : ui_->RecordMovieMNG->isChecked() ? ".mng" : ".wav";
}

// RecordMovieAVIClick + RecordMovieFilter_FileType: the list shows the chosen kind of file.
void RunGameExtraDialog::filterMovies() {
    QFont f = ui_->RecordMovieAVI->font();
    for (QRadioButton* r : {ui_->RecordMovieAVI, ui_->RecordMovieMNG, ui_->RecordMovieWAV}) {
        f.setBold(r->isChecked());
        r->setFont(f);
    }
    List& l = list(PageMovie);
    const QString ext = movieExtension();
    for (int r = 0; r < l.model->rowCount(); ++r)
        l.view->setRowHidden(r, QModelIndex(), !l.model->item(r, NameCol)->text().endsWith(ext, Qt::CaseInsensitive));
    reselectMovie();
}

// ReSelectRecordMovieFile
void RunGameExtraDialog::reselectMovie() {
    int i = ui_->RecordMovieAVI->isChecked() ? 0 : ui_->RecordMovieMNG->isChecked() ? 1 : 2;
    QString want = movieFile_[i];
    List& l = list(PageMovie);
    if (want.isEmpty()) { // the first file of the kind, else the game's name
        for (int r = 0; r < l.model->rowCount(); ++r)
            if (!l.view->isRowHidden(r, QModelIndex())) {
                l.view->setCurrentIndex(l.model->index(r, 0));
                return;
            }
        want = in_.game.name + movieExtension();
    }
    selectFile(PageMovie, want);
    if (!list(PageMovie).view->currentIndex().isValid()) ui_->RecordMovieFileName->setText(want);
}

// CheckMemoryCardSupport
void RunGameExtraDialog::setupMemoryCards() {
    const MameGame& g = in_.game;
    MemoryCardSlots s = MemoryCards::read(MemoryCards::supportFile(fe_.gamesFolder(), Frontend::fileBase(sys_)), g.name,
                                          MemoryCards::isNeoGeo(g.bios, g.driver));
    cardSection_ = s.section;
    cardSlots_ = s.options;
    cardExtensions_ = s.extensions;
    if (cardSlots_.isEmpty()) {
        ui_->PageButtonMemoryCard->setEnabled(false); // DisablePage(2)
        ui_->PageButtonMemoryCard->setToolTip(tr("This machine has no memory card slots."));
    }
    QLabel* labels[] = {ui_->LabelInsertMemoryCard_Slot1, ui_->LabelInsertMemoryCard_Slot2, ui_->LabelInsertMemoryCard_Slot3,
                        ui_->LabelInsertMemoryCard_Slot4};
    QLineEdit* edits[] = {ui_->InsertMemoryCard_Slot1, ui_->InsertMemoryCard_Slot2, ui_->InsertMemoryCard_Slot3,
                          ui_->InsertMemoryCard_Slot4};
    QPushButton* select[] = {ui_->ButtonInsertMemoryCard_Slot1_SelectFile, ui_->ButtonInsertMemoryCard_Slot2_SelectFile,
                             ui_->ButtonInsertMemoryCard_Slot3_SelectFile, ui_->ButtonInsertMemoryCard_Slot4_SelectFile};
    QPushButton* last[] = {ui_->ButtonInsertMemoryCard_Slot1_LastUsed, ui_->ButtonInsertMemoryCard_Slot2_LastUsed,
                           ui_->ButtonInsertMemoryCard_Slot3_LastUsed, ui_->ButtonInsertMemoryCard_Slot4_LastUsed};
    QPushButton* clear[] = {ui_->ButtonInsertMemoryCard_Slot1_Clear, ui_->ButtonInsertMemoryCard_Slot2_Clear,
                            ui_->ButtonInsertMemoryCard_Slot3_Clear, ui_->ButtonInsertMemoryCard_Slot4_Clear};
    WinIni lastUsed;
    lastUsed.load(MemoryCards::lastUsedFile(fe_.userDir(), Frontend::fileBase(sys_), cardSection_));
    // Memory card files can have a path, not the characters Windows doesn't allow.
    auto* v = new QRegularExpressionValidator(QRegularExpression(R"([^*?<>|;"]*)"), this);
    QString filter = cardExtensions_.isEmpty() ? QString()
                   : tr("Memory Card Files (%1)").arg("*." + cardExtensions_.join("* *.") + "*") + ";;";
    filter += tr("All Files (*.*)");
    for (int i = 0; i < 4; ++i) {
        cardEdit_[i] = edits[i];
        bool on = i < cardSlots_.size();
        labels[i]->setText(tr("Slot %1").arg(i + 1));
        labels[i]->adjustSize();
        labels[i]->setToolTip(on ? "-" + cardSlots_[i] : QString());
        for (QWidget* w : {(QWidget*)labels[i], (QWidget*)edits[i], (QWidget*)select[i], (QWidget*)last[i], (QWidget*)clear[i]})
            w->setEnabled(on);
        edits[i]->setValidator(v);
        lastUsedCard_[i] = lastUsed.readString(g.name, QString("slot%1").arg(i + 1));
        if (on) edits[i]->setText(lastUsedCard_[i]);
        connect(clear[i], &QPushButton::clicked, edits[i], &QLineEdit::clear);
        connect(last[i], &QPushButton::clicked, this, [this, i] { cardEdit_[i]->setText(lastUsedCard_[i]); });
        connect(select[i], &QPushButton::clicked, this, [this, i, filter] {
            QString start = cardEdit_[i]->text().isEmpty() ? folderCard_ : QFileInfo(cardEdit_[i]->text()).absolutePath();
            QString f = QFileDialog::getOpenFileName(this, tr("Select a memory card file for SLOT %1").arg(i + 1), start, filter);
            if (!f.isEmpty()) cardEdit_[i]->setText(QDir::toNativeSeparators(f));
        });
    }
    // Each row: the label, the box, then Select / Last Used / Clear as wide as their captions.
    int labelW = 0;
    for (QLabel* l : labels) labelW = std::max(labelW, l->width());
    int bw[3] = {};
    for (int i = 0; i < 4; ++i) {
        QPushButton* bs[3] = {select[i], last[i], clear[i]};
        for (int k = 0; k < 3; ++k) bw[k] = std::max({bw[k], bs[k]->width(), bs[k]->fontMetrics().horizontalAdvance(bs[k]->text()) + 14});
    }
    const int right = ui_->Panel3_MemoryCard->width() - 8;
    for (int i = 0; i < 4; ++i) {
        QPushButton* bs[3] = {select[i], last[i], clear[i]};
        int x = right + 1;
        for (int k = 2; k >= 0; --k) {
            x -= bw[k];
            vcl::place(bs[k], QRect(x, bs[k]->y(), bw[k], bs[k]->height()));
            x -= 2;
        }
        vcl::place(labels[i], QRect(8, labels[i]->y(), labelW, labels[i]->height()));
        QRect e = edits[i]->geometry();
        vcl::place(edits[i], QRect(8 + labelW + 4, e.y(), x - (8 + labelW + 4) - 1, e.height()));
    }
    // Fewer slots: the unused slots' rows hidden, so the list starts higher (as the original's
    // iTop 53/80/107, which laid the list over them)
    const int n = int(cardSlots_.size());
    if (n >= 1)
        for (int i = n; i < 4; ++i)
            for (QWidget* w : {(QWidget*)labels[i], (QWidget*)edits[i], (QWidget*)select[i], (QWidget*)last[i], (QWidget*)clear[i]})
                w->hide();
}

// AddMemoryCardToSlot: Enter / double click puts a .mc2 file in slot 2, anything else in slot 1.
void RunGameExtraDialog::assignCard(int slot) {
    List& l = list(PageCard);
    QModelIndex i = l.view->currentIndex();
    if (!i.isValid()) return;
    QStandardItem* it = l.model->item(i.row(), NameCol);
    if (slot < 0) slot = (cardSlots_.size() > 1 && it->text().endsWith(".mc2", Qt::CaseInsensitive)) ? 1 : 0;
    if (slot >= cardSlots_.size()) return;
    cardEdit_[slot]->setText(QDir::toNativeSeparators(it->data(FolderRole).toString() + it->text()));
}

bool RunGameExtraDialog::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::KeyPress) {
        for (List& l : lists_) {
            if (o != l.view) continue;
            int k = static_cast<QKeyEvent*>(e)->key();
            if (k == Qt::Key_F2) { renameFile(l.page); return true; }
            if (k == Qt::Key_Delete) { deleteFile(l.page); return true; }
            if ((k == Qt::Key_Return || k == Qt::Key_Enter) && l.page == PageCard) { assignCard(-1); return true; }
        }
    }
    return QDialog::eventFilter(o, e);
}

// EditFileName: the name is edited in the list.
void RunGameExtraDialog::renameFile(Page page) {
    List& l = list(page);
    QModelIndex i = l.view->currentIndex();
    if (!i.isValid()) return;
    renaming_ = true;
    l.view->edit(l.model->index(i.row(), NameCol));
}

// *ListViewItemEdited: the file gets the new name (with the page's extension); a file that has
// it already is overwritten after asking. An input file's .timecode file follows it.
void RunGameExtraDialog::onRenamed(Page page, int row) {
    renaming_ = false;
    List& l = list(page);
    QStandardItem* it = l.model->item(row, NameCol);
    const QString folder = it->data(FolderRole).toString();
    const QString oldFile = it->data(FileRole).toString();
    QString name = it->text().trimmed();
    auto restore = [&] {
        QSignalBlocker b(l.model);
        it->setText(oldFile);
    };
    if (oldFile.isEmpty()) return;
    if (name.isEmpty() || name.contains('/') || name.contains('\\')) { restore(); return; }
    QString ext = page == PageInput ? ".inp" : page == PageState ? ".sta" : page == PageMovie ? movieExtension()
                : (QFileInfo(name).suffix().isEmpty() ? "." + QFileInfo(oldFile).suffix() : QString());
    if (!ext.isEmpty() && !name.endsWith(ext, Qt::CaseInsensitive)) name = QFileInfo(name).completeBaseName() + ext;
    if (name == oldFile) { restore(); return; }
    QString from = folder + oldFile, to = folder + name;
    if (QFileInfo::exists(to) && name.compare(oldFile, Qt::CaseInsensitive) != 0) {
        QFileInfo a(to), b(from);
        QString age = a.lastModified() > b.lastModified() ? tr("(newer)") : a.lastModified() < b.lastModified() ? tr("(older)") : tr("(same date)");
        if (QMessageBox::question(this, tr("Rename File"),
                                  tr("The destination file already exists.\n\n%1\nSize: %2\nDate modified: %3 %4\n\n"
                                     "Do you want to rename %5 and overwrite the file above?")
                                      .arg(QDir::toNativeSeparators(to), sizeText(a.size()),
                                           QLocale().toString(a.lastModified(), QLocale::ShortFormat), age, oldFile)) !=
            QMessageBox::Yes) {
            restore();
            return;
        }
        QFile::remove(to);
    }
    bool ok = name.compare(oldFile, Qt::CaseInsensitive) == 0
                  ? (QFile::rename(from, from + ".tmp") && QFile::rename(from + ".tmp", to))
                  : QFile::rename(from, to);
    if (!ok) {
        QMessageBox::warning(this, tr("Rename File"), tr("Could not rename %1").arg(QDir::toNativeSeparators(from)));
        restore();
        return;
    }
    if (page == PageInput) {
        QString tc = folder + QFileInfo(oldFile).completeBaseName() + ".timecode";
        if (QFileInfo::exists(tc)) QFile::rename(tc, folder + QFileInfo(name).completeBaseName() + ".timecode");
    }
    QString keep = folder + name;
    fillList(page);
    if (page == PageMovie) filterMovies();
    selectFile(page, keep);
    l.view->setFocus();
}

// DeleteFileName
void RunGameExtraDialog::deleteFile(Page page) {
    List& l = list(page);
    QModelIndex i = l.view->currentIndex();
    if (!i.isValid()) return;
    int row = i.row();
    QStandardItem* it = l.model->item(row, NameCol);
    QString file = it->data(FolderRole).toString() + it->text();
    const char* what[] = {"", QT_TR_NOOP("You're about to delete an input file."), QT_TR_NOOP("You're about to delete a save state file."),
                          QT_TR_NOOP("You're about to delete a memory card file."), QT_TR_NOOP("You're about to delete a movie file.")};
    QFileInfo fi(file);
    if (QMessageBox::question(this, tr("Delete File"),
                              tr(what[page]) + "\n\n" + QDir::toNativeSeparators(file) + "\n" +
                                  tr("Size: %1").arg(sizeText(fi.size())) + "\n" +
                                  tr("Date modified: %1").arg(QLocale().toString(fi.lastModified(), QLocale::ShortFormat)) +
                                  "\n\n" + tr("Are you sure you want to delete this file? It goes to the Recycle Bin.")) !=
        QMessageBox::Yes)
        return;
    auto remove = [this](const QString& f) { return !moveToRecycleBin({f}, this).isEmpty(); };
    if (!remove(file)) {
        QMessageBox::warning(this, tr("Delete File"), tr("Could not delete %1").arg(QDir::toNativeSeparators(file)));
        return;
    }
    QString tc = fi.absolutePath() + "/" + fi.completeBaseName() + ".timecode";
    if (page == PageInput && QFileInfo::exists(tc)) remove(tc);
    l.model->removeRow(row);
    // the file before it, else the one after (NearItem)
    int next = row > 0 ? row - 1 : 0;
    while (next < l.model->rowCount() && l.view->isRowHidden(next, QModelIndex())) ++next;
    if (next < l.model->rowCount()) l.view->setCurrentIndex(l.model->index(next, 0));
    else selected(page);
}

// FormCloseQuery + MountCommandLine
void RunGameExtraDialog::accept() {
    for (List& l : lists_)
        if (QWidget* f = QApplication::focusWidget(); f && f != l.view && l.view->isAncestorOf(f)) return; // a name being edited
    if (!buildArguments()) return;
    saveLastUsedCards();
    QDialog::accept();
}

bool RunGameExtraDialog::buildArguments() {
    args_.clear();
    const QString name = in_.game.name;
    auto relative = [](const QString& path, const QString& base) {
        return path.startsWith(base, Qt::CaseInsensitive) ? path.mid(base.size()) : path;
    };
    if (ui_->Enabled_PlaybackRecordInput->isChecked()) {
        List& l = list(PageInput);
        QModelIndex i = l.view->currentIndex();
        QString path;
        if (ui_->InputPlayback->isChecked()) {
            if (!i.isValid() && l.model->rowCount()) {
                showPage(PageInput);
                QMessageBox::warning(this, tr("Playback/Record an Input File"),
                                     tr("Choose the input file to play back in the list, or untick Enabled."));
                return false;
            }
            if (i.isValid()) {
                QStandardItem* it = l.model->item(i.row(), NameCol);
                path = relative(it->data(FolderRole).toString() + it->text(), folderInput_);
            }
        } else {
            QString file = ui_->InputFileName->text().trimmed();
            if (file.isEmpty()) {
                showPage(PageInput);
                QMessageBox::warning(this, tr("Playback/Record an Input File"),
                                     tr("Type a name for the new input file, or choose one in the list."));
                return false;
            }
            file = QFileInfo(file).completeBaseName() + ".inp";
            QString full = folderInput_ + name + "/" + file;
            for (int r = 0; r < l.model->rowCount(); ++r) // a file of the list with that name
                if (l.model->item(r, NameCol)->text().compare(file, Qt::CaseInsensitive) == 0) {
                    full = l.model->item(r, NameCol)->data(FolderRole).toString() + l.model->item(r, NameCol)->text();
                    break;
                }
            path = relative(full, folderInput_);
        }
        if (!path.isEmpty()) {
            int build = fe_.mameBuild(sys_);
            if (build > 0 && build < 59) path = path.left(path.size() - 4); // before 0.59 MAME adds ".inp"
            if (ui_->InputPlayback->isChecked()) {
                args_ << "-playback" << QDir::toNativeSeparators(path);
                bool exitAfter = ui_->InputExitEmulatorAfterInputPlayback->isChecked();
                if (inputExtras_ && exitAfter != exitAfterPlayback_)
                    args_ << (exitAfter ? "-exit_after_playback" : "-noexit_after_playback");
            } else {
                args_ << "-record" << QDir::toNativeSeparators(path);
                bool timecode = ui_->InputRecordTimecodeFile->isChecked();
                if (inputExtras_ && timecode != recordTimecode_)
                    args_ << (timecode ? "-record_timecode" : "-norecord_timecode");
            }
        }
    }
    if (ui_->Enabled_LoadSaveState->isChecked()) {
        List& l = list(PageState);
        QModelIndex i = l.view->currentIndex();
        if (!i.isValid() && l.model->rowCount()) {
            showPage(PageState);
            QMessageBox::warning(this, tr("Load a Save State"),
                                 tr("Choose the save state to load in the list, or untick Enabled."));
            return false;
        }
        if (i.isValid()) {
            args_ << "-state" << QFileInfo(l.model->item(i.row(), NameCol)->text()).completeBaseName();
            bool autosave = ui_->AutoSaveState->isChecked();
            if (autosave != autosave_) args_ << (autosave ? "-autosave" : "-noautosave");
        }
    }
    if (ui_->Enabled_InsertMemoryCard->isChecked())
        for (int s = 0; s < cardSlots_.size(); ++s) {
            QString f = cardEdit_[s]->text().trimmed();
            if (!f.isEmpty()) args_ << "-" + cardSlots_[s] << f;
        }
    if (ui_->Enabled_RecordMovie->isChecked()) {
        QString file = ui_->RecordMovieFileName->text().trimmed();
        if (file.isEmpty()) {
            showPage(PageMovie);
            QMessageBox::warning(this, tr("Record a Movie"),
                                 tr("Type a name for the movie file, or choose one in the list, or untick Enabled."));
            return false;
        }
        file = QFileInfo(file).completeBaseName() + movieExtension();
        QString full = folderMovie_ + name + "/" + file;
        List& l = list(PageMovie);
        for (int r = 0; r < l.model->rowCount(); ++r)
            if (!l.view->isRowHidden(r, QModelIndex()) && l.model->item(r, NameCol)->text().compare(file, Qt::CaseInsensitive) == 0) {
                full = l.model->item(r, NameCol)->data(FolderRole).toString() + l.model->item(r, NameCol)->text();
                break;
            }
        if (ui_->RecordMovieWAV->isChecked()) {
            // MAME opens the WAV file as given (sound_manager::start_recording): from its own folder
            // and without creating folders. So the full path, with the folder made first.
            QDir().mkpath(QFileInfo(full).absolutePath());
            args_ << "-wavwrite" << QDir::toNativeSeparators(full);
        } else {
            // AVI and MNG go in snapshot_directory (video_manager::begin_recording_screen), which
            // MAME creates; a path relative to it works with every MAME version.
            args_ << (ui_->RecordMovieAVI->isChecked() ? "-aviwrite" : "-mngwrite")
                  << QDir::toNativeSeparators(relative(full, movieRoot_));
        }
    }
    return true;
}

// UpdateLastMemoryCardUsed
void RunGameExtraDialog::saveLastUsedCards() {
    if (!ui_->Enabled_InsertMemoryCard->isChecked() || cardSection_.isEmpty()) return;
    bool any = false;
    for (int s = 0; s < cardSlots_.size(); ++s) any |= !cardEdit_[s]->text().trimmed().isEmpty();
    if (!any) return;
    QString file = MemoryCards::lastUsedFile(fe_.userDir(), Frontend::fileBase(sys_), cardSection_);
    QDir().mkpath(QFileInfo(file).absolutePath());
    WinIni ini;
    ini.load(file);
    for (int s = 0; s < cardSlots_.size(); ++s)
        ini.writeString(in_.game.name, QString("slot%1").arg(s + 1), cardEdit_[s]->text().trimmed());
    ini.save(file);
}

QString RunGameExtraDialog::cleanName(QString name) {
    static const QRegularExpression bad(R"([/\\:*?"<>|])");
    return name.remove(bad).trimmed();
}

} // namespace eld
